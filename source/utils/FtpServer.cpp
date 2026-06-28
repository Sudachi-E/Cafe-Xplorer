#include "FtpServer.hpp"
#include "../filemanager/PathConverter.hpp"
#include <coreinit/time.h>
#include <nn/acp.h>
#include <whb/log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>

#define FTP_BUFSZ 4096

int FtpServer::sListenSock = -1;
OSThread FtpServer::sServerThread;
uint8_t FtpServer::sServerStack[16384];
volatile bool FtpServer::sRunning = false;
volatile bool FtpServer::sServerDone = false;
uint16_t FtpServer::sPort = 2121;
std::string FtpServer::sLocalIP;
int FtpServer::sActiveConnections = 0;
volatile int FtpServer::sActiveClientSock = -1;
int FtpServer::sDataListenSock = -1;
int FtpServer::sDataPort = 0;

void FtpServer::ResolveLocalIP() {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        sLocalIP = "0.0.0.0";
        return;
    }
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr("8.8.8.8");
    serv.sin_port = htons(53);
    if (connect(s, (struct sockaddr*)&serv, sizeof(serv)) == 0) {
        struct sockaddr_in name;
        socklen_t namelen = sizeof(name);
        if (getsockname(s, (struct sockaddr*)&name, &namelen) == 0) {
            sLocalIP = inet_ntoa(name.sin_addr);
        }
    }
    close(s);
    if (sLocalIP.empty()) sLocalIP = "0.0.0.0";
}

bool FtpServer::Start() {
    if (sRunning) return true;

    ResolveLocalIP();
    ACPInitialize();

    sListenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (sListenSock < 0) {
        WHBLogPrintf("[FTP] Failed to create socket");
        return false;
    }

    int opt = 1;
    setsockopt(sListenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(sPort);

    if (bind(sListenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        WHBLogPrintf("[FTP] Bind failed on port %d", sPort);
        close(sListenSock);
        sListenSock = -1;
        return false;
    }

    if (listen(sListenSock, 5) < 0) {
        close(sListenSock);
        sListenSock = -1;
        return false;
    }

    sRunning = true;
    sActiveConnections = 0;
    sActiveClientSock = -1;

    if (!OSCreateThread(&sServerThread, (OSThreadEntryPointFn)ServerThread, 0, nullptr,
                        sServerStack + sizeof(sServerStack), sizeof(sServerStack),
                        16, OS_THREAD_ATTRIB_DETACHED)) {
        WHBLogPrintf("[FTP] Failed to create thread");
        sRunning = false;
        close(sListenSock);
        sListenSock = -1;
        return false;
    }
    OSResumeThread(&sServerThread);

    WHBLogPrintf("[FTP] Server started on %s:%d", sLocalIP.c_str(), sPort);
    return true;
}

void FtpServer::Stop() {
    if (!sRunning && sServerDone) return;
    sRunning = false;

    // Close listen socket to break accept() in server thread
    if (sListenSock >= 0) {
        close(sListenSock);
        sListenSock = -1;
    }

    // Close active client socket to interrupt HandleClient's RecvLine
    if (sActiveClientSock > 0) {
        close(sActiveClientSock);
        sActiveClientSock = -1;
    }

    // Poll for server thread to finish (RETR/STOR loops check sRunning)
    OSTime deadline = OSGetTime() + OSMillisecondsToTicks(3000);
    while (OSGetTime() < deadline) {
        if (sServerDone) break;
        OSSleepTicks(OSMillisecondsToTicks(50));
    }

    ACPTurnOffDrcLed();
    ACPFinalize();
    sLocalIP.clear();
    sServerDone = true;
    WHBLogPrintf("[FTP] Server stopped");
}

bool FtpServer::IsRunning() {
    return sRunning;
}

std::string FtpServer::GetLocalIP() {
    return sLocalIP;
}

uint16_t FtpServer::GetPort() {
    return sPort;
}

int FtpServer::GetActiveConnections() {
    return sActiveConnections;
}

int FtpServer::ServerThread(int argc, const char* argv[]) {
    int mySock = sListenSock;
    int flags = fcntl(mySock, F_GETFL, 0);
    if (flags >= 0) fcntl(mySock, F_SETFL, flags | O_NONBLOCK);

    while (sRunning) {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = accept(mySock, (struct sockaddr*)&clientAddr, &addrLen);
        if (!sRunning) break;
        if (clientSock < 0) {
            OSSleepTicks(OSMillisecondsToTicks(100));
            continue;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

        int cflags = fcntl(clientSock, F_GETFL, 0);
        if (cflags >= 0) fcntl(clientSock, F_SETFL, cflags & ~O_NONBLOCK);

        sActiveConnections++;
        sActiveClientSock = clientSock;
        HandleClient(clientSock, clientIP);
        sActiveClientSock = -1;
        sActiveConnections--;
    }

    if (mySock > 0) close(mySock);
    sListenSock = -1;
    sServerDone = true;
    return 0;
}

void FtpServer::SendReply(int sock, int code, const std::string& msg) {
    std::string reply = std::to_string(code) + " " + msg + "\r\n";
    if (sock > 0) send(sock, reply.c_str(), reply.size(), 0);
}

std::string FtpServer::RecvLine(int sock) {
    std::string line;
    char c;
    while (recv(sock, &c, 1, 0) == 1) {
        if (c == '\r') continue;
        if (c == '\n') break;
        line += c;
    }
    return line;
}

int FtpServer::PasvAccept(int dataListenSock, uint32_t timeoutMs) {
    if (dataListenSock < 0) return -1;

    int flags = fcntl(dataListenSock, F_GETFL, 0);
    fcntl(dataListenSock, F_SETFL, flags | O_NONBLOCK);

    OSTime deadline = OSGetTime() + OSMillisecondsToTicks(timeoutMs);
    while (OSGetTime() < deadline) {
        if (!sRunning) return -1;
        struct sockaddr_in dataAddr;
        socklen_t addrLen = sizeof(dataAddr);
        int acc = accept(dataListenSock, (struct sockaddr*)&dataAddr, &addrLen);
        if (acc >= 0) {
            fcntl(dataListenSock, F_SETFL, flags);
            int dflags = fcntl(acc, F_GETFL, 0);
            if (dflags >= 0) fcntl(acc, F_SETFL, dflags & ~O_NONBLOCK);
            WHBLogPrintf("[FTP] PasvAccept accepted connection");
            return acc;
        }
        OSSleepTicks(OSMillisecondsToTicks(50));
    }

    fcntl(dataListenSock, F_SETFL, flags);
    WHBLogPrintf("[FTP] PasvAccept timed out after %u ms", timeoutMs);
    return -1;
}

static std::string FtpResolvePath(const std::string& displayPath) {
    if (displayPath == "/" || displayPath.empty()) return "/";
    std::string real = PathConverter::ToRealPath(displayPath);
    WHBLogPrintf("[FTP] ResolvePath: '%s' -> '%s'", displayPath.c_str(), real.c_str());
    return real;
}

void FtpServer::HandleClient(int clientSock, const char* clientIP) {
    WHBLogPrintf("[FTP] Client connected from %s", clientIP);
    ACPTurnOnDrcLed(0, 3);

    int dataListenSock = -1;
    std::string renamePath;

    SendReply(clientSock, 220, "Cafe-Xplorer FTP ready");

    std::string cwd = "/";
    bool binary = true;

    while (sRunning) {
        std::string cmdLine = RecvLine(clientSock);
        if (cmdLine.empty()) break;

        size_t space = cmdLine.find(' ');
        std::string cmd = (space != std::string::npos) ? cmdLine.substr(0, space) : cmdLine;
        std::string arg = (space != std::string::npos) ? cmdLine.substr(space + 1) : "";

        for (auto& c : cmd) c = toupper((unsigned char)c);

        WHBLogPrintf("[FTP] CMD: %s ARG: '%s' CWD: '%s'", cmd.c_str(), arg.c_str(), cwd.c_str());

        if (cmd == "USER" || cmd == "PASS") {
            SendReply(clientSock, 230, "Login successful");
        } else if (cmd == "QUIT") {
            SendReply(clientSock, 221, "Goodbye");
            break;
        } else if (cmd == "SYST") {
            SendReply(clientSock, 215, "UNIX Type: L8");
        } else if (cmd == "FEAT") {
            std::string feats = "211-Features:\r\n SIZE\r\n MDTM\r\n211 End\r\n";
            if (clientSock > 0) send(clientSock, feats.c_str(), feats.size(), 0);
        } else if (cmd == "PWD") {
            SendReply(clientSock, 257, "\"" + cwd + "\"");
        } else if (cmd == "TYPE") {
            binary = (arg == "I");
            (void)binary;
            SendReply(clientSock, 200, "Type set to " + arg);
        } else if (cmd == "MODE" || cmd == "STRU") {
            SendReply(clientSock, 200, "OK");
        } else if (cmd == "PASV") {
            if (dataListenSock > 0) {
                close(dataListenSock);
                dataListenSock = -1;
                sDataListenSock = -1;
            }
            dataListenSock = socket(AF_INET, SOCK_STREAM, 0);
            if (dataListenSock < 0) {
                sDataListenSock = -1;
                SendReply(clientSock, 425, "Can't open data connection");
                continue;
            }
            struct sockaddr_in dataAddr;
            memset(&dataAddr, 0, sizeof(dataAddr));
            dataAddr.sin_family = AF_INET;
            dataAddr.sin_addr.s_addr = INADDR_ANY;
            dataAddr.sin_port = 0;
            if (bind(dataListenSock, (struct sockaddr*)&dataAddr, sizeof(dataAddr)) < 0) {
                close(dataListenSock);
                dataListenSock = -1;
                sDataListenSock = -1;
                SendReply(clientSock, 425, "Can't open data connection");
                continue;
            }
            listen(dataListenSock, 1);
            sDataListenSock = dataListenSock;

            struct sockaddr_in boundAddr;
            socklen_t boundLen = sizeof(boundAddr);
            getsockname(dataListenSock, (struct sockaddr*)&boundAddr, &boundLen);
            int dataPort = ntohs(boundAddr.sin_port);

            struct in_addr localAddr;
            socklen_t localLen = sizeof(localAddr);
            if (getsockname(clientSock, (struct sockaddr*)&localAddr, &localLen) != 0) {
                unsigned int h1, h2, h3, h4;
                if (sscanf(sLocalIP.c_str(), "%u.%u.%u.%u", &h1, &h2, &h3, &h4) == 4) {
                    localAddr.s_addr = htonl((h1 << 24) | (h2 << 16) | (h3 << 8) | h4);
                } else {
                    localAddr.s_addr = htonl(0x7F000001);
                }
            }

            uint32_t ip = ntohl(localAddr.s_addr);
            WHBLogPrintf("[FTP] PASV port=%d ip=%d.%d.%d.%d", dataPort,
                (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
            std::string pasvResp = "Entering Passive Mode (" +
                std::to_string((ip >> 24) & 0xFF) + "," +
                std::to_string((ip >> 16) & 0xFF) + "," +
                std::to_string((ip >> 8) & 0xFF) + "," +
                std::to_string(ip & 0xFF) + "," +
                std::to_string((dataPort >> 8) & 0xFF) + "," +
                std::to_string(dataPort & 0xFF) + ")";
            SendReply(clientSock, 227, pasvResp);
        } else if (cmd == "CWD") {
            std::string newPath = arg;
            if (newPath.empty()) {
                SendReply(clientSock, 550, "No path");
                continue;
            }
            if (newPath[0] != '/') {
                if (cwd != "/") newPath = cwd + "/" + newPath;
                else newPath = "/" + newPath;
            }

            bool ok = false;
            if (PathConverter::IsVirtualDirectory(newPath)) {
                ok = true;
                WHBLogPrintf("[FTP] CWD '%s' -> virtual dir, ok", newPath.c_str());
            } else {
                std::string realPath = FtpResolvePath(newPath);
                struct stat st;
                ok = (stat(realPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
                WHBLogPrintf("[FTP] CWD '%s' real='%s' -> %s", newPath.c_str(), realPath.c_str(), ok ? "OK" : "FAIL");
            }
            if (ok) {
                cwd = newPath;
                SendReply(clientSock, 250, "CWD successful");
            } else {
                SendReply(clientSock, 550, "No such directory");
            }
        } else if (cmd == "CDUP") {
            if (cwd == "/") {
                SendReply(clientSock, 250, "OK");
            } else {
                size_t pos = cwd.rfind('/');
                if (pos == 0) cwd = "/";
                else cwd = cwd.substr(0, pos);
                SendReply(clientSock, 250, "CDUP successful");
            }
        } else if (cmd == "SIZE") {
            std::string path = arg;
            if (path.empty()) { SendReply(clientSock, 501, "No path"); continue; }
            if (path[0] != '/') {
                if (cwd != "/") path = cwd + "/" + path;
                else path = "/" + path;
            }
            std::string realPath = FtpResolvePath(path);
            struct stat st;
            if (stat(realPath.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
                SendReply(clientSock, 213, std::to_string(st.st_size));
            } else {
                SendReply(clientSock, 550, "No such file");
            }
        } else if (cmd == "LIST" || cmd == "NLST") {
            std::string listPath = arg;
            while (!listPath.empty() && listPath[0] == '-') {
                size_t sp = listPath.find(' ');
                if (sp != std::string::npos) listPath = listPath.substr(sp + 1);
                else { listPath.clear(); break; }
            }
            if (listPath.empty()) {
                listPath = cwd;
            } else if (listPath[0] != '/') {
                if (cwd != "/") listPath = cwd + "/" + listPath;
                else listPath = "/" + listPath;
            }

            WHBLogPrintf("[FTP] LIST path='%s'", listPath.c_str());

            int dataSock = PasvAccept(dataListenSock, 15000);
            if (dataSock < 0) {
                WHBLogPrintf("[FTP] LIST PasvAccept failed");
                SendReply(clientSock, 425, "Can't open data connection");
                continue;
            }

            SendReply(clientSock, 150, "Opening data connection");

            std::string listing;

            if (PathConverter::IsVirtualDirectory(listPath)) {
                auto subdirs = PathConverter::GetVirtualSubdirs(listPath);
                WHBLogPrintf("[FTP] LIST isVirtual=1 subdirCount=%zu", subdirs.size());
                if (!subdirs.empty()) {
                    for (const auto& name : subdirs) {
                        if (cmd == "NLST") {
                            listing += name + "\r\n";
                        } else {
                            char buf[256];
                            snprintf(buf, sizeof(buf),
                                "drwxr-xr-x 1 owner group %10d Jan  1 00:00 %s\r\n",
                                4096, name.c_str());
                            listing += buf;
                        }
                    }
                    send(dataSock, listing.c_str(), listing.size(), 0);
                    if (dataSock > 0) close(dataSock);
                    SendReply(clientSock, 226, "Transfer complete");
                    continue;
                }
            }
            {
                std::string realPath = FtpResolvePath(listPath);
                DIR* d = opendir(realPath.c_str());
                WHBLogPrintf("[FTP] LIST opendir('%s') -> %s", realPath.c_str(), d ? "OK" : "FAILED");
                if (d) {
                    struct dirent* entry;
                    while ((entry = readdir(d)) != nullptr) {
                        std::string name = entry->d_name;
                        if (name == "." || name == "..") continue;

                        std::string fullRealPath = realPath;
                        if (fullRealPath != "/" && fullRealPath.back() != '/') fullRealPath += "/";
                        fullRealPath += name;

                        struct stat st;
                        bool isDir = false;
                        if (stat(fullRealPath.c_str(), &st) == 0) {
                            isDir = S_ISDIR(st.st_mode);
                        }

                        if (cmd == "NLST") {
                            listing += name + "\r\n";
                        } else {
                            char buf[256];
                            snprintf(buf, sizeof(buf),
                                "%s%s%10s 1 owner group %10lld Jan  1 00:00 %s\r\n",
                                isDir ? "d" : "-",
                                "rwxr-xr-x",
                                "",
                                (long long)st.st_size,
                                name.c_str());
                            listing += buf;
                        }
                    }
                    closedir(d);
                }
                WHBLogPrintf("[FTP] LIST listing size before send = %zu bytes, entries found", listing.size());
            }

            send(dataSock, listing.c_str(), listing.size(), 0);
            WHBLogPrintf("[FTP] LIST sent %zu bytes", listing.size());
            if (dataSock > 0) close(dataSock);
            SendReply(clientSock, 226, "Transfer complete");
        } else if (cmd == "RETR") {
            std::string path = arg;
            if (path.empty()) { SendReply(clientSock, 501, "No path"); continue; }
            if (path[0] != '/') {
                if (cwd != "/") path = cwd + "/" + path;
                else path = "/" + path;
            }

            std::string realPath = FtpResolvePath(path);
            WHBLogPrintf("[FTP] RETR '%s' real='%s'", path.c_str(), realPath.c_str());
            FILE* f = fopen(realPath.c_str(), "rb");
            if (!f) {
                SendReply(clientSock, 550, "No such file");
                continue;
            }

            int dataSock = PasvAccept(dataListenSock, 15000);
            if (dataSock < 0) {
                fclose(f);
                SendReply(clientSock, 425, "Can't open data connection");
                continue;
            }

            SendReply(clientSock, 150, "Opening data connection");

            char buf[FTP_BUFSZ];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
                if (!sRunning) break;
                const char* p = buf;
                size_t remaining = n;
                while (remaining > 0) {
                    int sent = send(dataSock, p, remaining, 0);
                    if (sent <= 0) break;
                    p += sent;
                    remaining -= sent;
                }
                if (remaining > 0) break;
            }
            fclose(f);
            if (dataSock > 0) close(dataSock);
            SendReply(clientSock, 226, "Transfer complete");
        } else if (cmd == "STOR") {
            std::string path = arg;
            if (path.empty()) { SendReply(clientSock, 501, "No path"); continue; }
            if (path[0] != '/') {
                if (cwd != "/") path = cwd + "/" + path;
                else path = "/" + path;
            }

            WHBLogPrintf("[FTP] PasvAccept: dataListenSock=%d timeout=%ums", dataListenSock, 15000);
            int dataSock = PasvAccept(dataListenSock, 15000);
            if (dataSock < 0) {
                WHBLogPrintf("[FTP] PasvAccept returned -1");
                SendReply(clientSock, 425, "Can't open data connection");
                continue;
            }

            SendReply(clientSock, 150, "Opening data connection");

            std::string realPath = FtpResolvePath(path);
            FILE* f = fopen(realPath.c_str(), "wb");
            if (!f) {
                if (dataSock > 0) close(dataSock);
                SendReply(clientSock, 550, "Can't create file");
                continue;
            }

            char buf[FTP_BUFSZ];
            int n;
            while ((n = recv(dataSock, buf, sizeof(buf), 0)) > 0) {
                if (!sRunning) break;
                size_t written = fwrite(buf, 1, n, f);
                if (written != (size_t)n) break;
            }
            fclose(f);
            if (dataSock > 0) close(dataSock);
            SendReply(clientSock, 226, "Transfer complete");
        } else if (cmd == "DELE") {
            std::string path = arg;
            if (path.empty()) { SendReply(clientSock, 501, "No path"); continue; }
            if (path[0] != '/') {
                if (cwd != "/") path = cwd + "/" + path;
                else path = "/" + path;
            }
            if (remove(FtpResolvePath(path).c_str()) == 0) {
                SendReply(clientSock, 250, "Delete successful");
            } else {
                SendReply(clientSock, 550, "Delete failed");
            }
        } else if (cmd == "RMD") {
            std::string path = arg;
            if (path.empty()) { SendReply(clientSock, 501, "No path"); continue; }
            if (path[0] != '/') {
                if (cwd != "/") path = cwd + "/" + path;
                else path = "/" + path;
            }
            if (rmdir(FtpResolvePath(path).c_str()) == 0) {
                SendReply(clientSock, 250, "RMD successful");
            } else {
                SendReply(clientSock, 550, "RMD failed");
            }
        } else if (cmd == "MKD") {
            std::string path = arg;
            if (path.empty()) { SendReply(clientSock, 501, "No path"); continue; }
            if (path[0] != '/') {
                if (cwd != "/") path = cwd + "/" + path;
                else path = "/" + path;
            }
            std::string realPath = FtpResolvePath(path);
            if (mkdir(realPath.c_str(), 0777) == 0) {
                SendReply(clientSock, 257, "\"" + path + "\" created");
            } else {
                SendReply(clientSock, 550, "MKD failed");
            }
        } else if (cmd == "RNFR") {
            renamePath = arg;
            if (renamePath.empty()) { SendReply(clientSock, 501, "No path"); continue; }
            if (renamePath[0] != '/') {
                if (cwd != "/") renamePath = cwd + "/" + renamePath;
                else renamePath = "/" + renamePath;
            }
            std::string resolvedSrc = FtpResolvePath(renamePath);
            struct stat st;
            if (stat(resolvedSrc.c_str(), &st) == 0) {
                renamePath = resolvedSrc;
                SendReply(clientSock, 350, "RNFR accepted");
            } else {
                renamePath.clear();
                SendReply(clientSock, 550, "No such file");
            }
        } else if (cmd == "RNTO") {
            if (renamePath.empty()) {
                SendReply(clientSock, 503, "Need RNFR first");
                continue;
            }
            std::string newPath = arg;
            if (newPath.empty()) { SendReply(clientSock, 501, "No path"); continue; }
            if (newPath[0] != '/') {
                if (cwd != "/") newPath = cwd + "/" + newPath;
                else newPath = "/" + newPath;
            }
            std::string resolvedDst = FtpResolvePath(newPath);
            if (rename(renamePath.c_str(), resolvedDst.c_str()) == 0) {
                SendReply(clientSock, 250, "Rename successful");
            } else {
                SendReply(clientSock, 550, "Rename failed");
            }
            renamePath.clear();
        } else if (cmd == "MDTM") {
            std::string path = arg;
            if (path.empty()) { SendReply(clientSock, 501, "No path"); continue; }
            if (path[0] != '/') {
                if (cwd != "/") path = cwd + "/" + path;
                else path = "/" + path;
            }
            std::string realPath = FtpResolvePath(path);
            struct stat st;
            if (stat(realPath.c_str(), &st) == 0) {
                char tbuf[32];
                strftime(tbuf, sizeof(tbuf), "%Y%m%d%H%M%S", gmtime(&st.st_mtime));
                SendReply(clientSock, 213, tbuf);
            } else {
                SendReply(clientSock, 550, "No such file");
            }
        } else if (cmd == "NOOP") {
            SendReply(clientSock, 200, "OK");
        } else if (cmd == "PORT") {
            WHBLogPrintf("[FTP] PORT command ignored, using PASV");
            SendReply(clientSock, 200, "PORT ignored, use PASV");
        } else {
            WHBLogPrintf("[FTP] Unknown command: %s", cmd.c_str());
            SendReply(clientSock, 500, "Unknown command");
        }
    }

    ACPTurnOffDrcLed();
    if (dataListenSock > 0) {
        close(dataListenSock);
        sDataListenSock = -1;
    }
    if (clientSock > 0) close(clientSock);
    WHBLogPrintf("[FTP] Client disconnected from %s", clientIP);
}
