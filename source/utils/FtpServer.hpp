#pragma once
#include <string>
#include <coreinit/thread.h>

class FtpServer {
public:
    static bool Start();
    static void Stop();
    static bool IsRunning();
    static std::string GetLocalIP();
    static uint16_t GetPort();
    static int GetActiveConnections();

private:
    static int sListenSock;
    static OSThread sServerThread;
    static uint8_t sServerStack[16384];
    static volatile bool sRunning;
    static volatile bool sServerDone;
    static uint16_t sPort;
    static std::string sLocalIP;
    static int sActiveConnections;
    static volatile int sActiveClientSock;
    static int sDataListenSock;
    static int sDataPort;

    static int ServerThread(int argc, const char* argv[]);
    static void HandleClient(int clientSock, const char* clientIP);
    static void SendReply(int sock, int code, const std::string& msg);
    static std::string RecvLine(int sock);
    static int PasvAccept(int dataListenSock, uint32_t timeoutMs);
    static void ResolveLocalIP();
};
