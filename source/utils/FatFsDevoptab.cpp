#include "FatFsDevoptab.hpp"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include <coreinit/debug.h>
#include <sys/iosupport.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_FAT_DEVICES FF_VOLUMES

typedef struct {
    devoptab_t tab;
    char name[32];
    int driveNum;
    bool registered;
} FatDevice;

static FatDevice s_fatDevices[MAX_FAT_DEVICES];
static int s_deviceCount = 0;

static void buildFatPath(int drv, char* out, size_t outSize, const char* path) {
    const char* colon = strchr(path, ':');
    if (colon) path = colon + 1;
    if (path[0] == '/')
        snprintf(out, outSize, "%d:%s", drv, path);
    else
        snprintf(out, outSize, "%d:/%s", drv, path);
}

static BYTE posixToFatFlags(int flags) {
    BYTE mode = 0;
    int access = flags & O_ACCMODE;
    if (access == O_RDONLY) mode |= FA_READ;
    else if (access == O_WRONLY) mode |= FA_WRITE;
    else mode |= FA_READ | FA_WRITE;
    if (flags & O_CREAT) {
        if (flags & O_EXCL) mode |= FA_CREATE_NEW;
        else if (flags & O_TRUNC) mode |= FA_CREATE_ALWAYS;
        else mode |= FA_OPEN_ALWAYS;
    } else {
        mode |= FA_OPEN_EXISTING;
    }
    return mode;
}

static int fatfs_to_errno(FRESULT fr) {
    switch (fr) {
        case FR_OK: return 0;
        case FR_DISK_ERR: return EIO;
        case FR_INT_ERR: return EIO;
        case FR_NOT_READY: return ENXIO;
        case FR_NO_FILE: return ENOENT;
        case FR_NO_PATH: return ENOENT;
        case FR_INVALID_NAME: return EINVAL;
        case FR_DENIED: return EACCES;
        case FR_EXIST: return EEXIST;
        case FR_INVALID_OBJECT: return EBADF;
        case FR_WRITE_PROTECTED: return EROFS;
        case FR_INVALID_DRIVE: return ENODEV;
        case FR_NOT_ENABLED: return ENODEV;
        case FR_NO_FILESYSTEM: return ENODEV;
        case FR_MKFS_ABORTED: return EIO;
        case FR_TIMEOUT: return ETIMEDOUT;
        case FR_LOCKED: return EBUSY;
        case FR_NOT_ENOUGH_CORE: return ENOMEM;
        case FR_TOO_MANY_OPEN_FILES: return ENFILE;
        case FR_INVALID_PARAMETER: return EINVAL;
        default: return EIO;
    }
}

static int fatfs_close_r(struct _reent *r, void *fd) {
    FRESULT fr = f_close((FIL*)fd);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    return 0;
}

static ssize_t fatfs_write_r(struct _reent *r, void *fd, const char *ptr, size_t len) {
    UINT bw;
    FRESULT fr = f_write((FIL*)fd, ptr, len, &bw);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    return (ssize_t)bw;
}

static ssize_t fatfs_read_r(struct _reent *r, void *fd, char *ptr, size_t len) {
    UINT br;
    FRESULT fr = f_read((FIL*)fd, ptr, len, &br);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    return (ssize_t)br;
}

static off_t fatfs_seek_r(struct _reent *r, void *fd, off_t pos, int dir) {
    FIL* fp = (FIL*)fd;
    FSIZE_t newPos;
    switch (dir) {
        case SEEK_SET: newPos = pos; break;
        case SEEK_CUR: newPos = f_tell(fp) + pos; break;
        case SEEK_END: newPos = f_size(fp) + pos; break;
        default: r->_errno = EINVAL; return -1;
    }
    FRESULT fr = f_lseek(fp, newPos);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    return (off_t)f_tell(fp);
}

static int fatfs_fstat_r(struct _reent *r, void *fd, struct stat *st) {
    FIL* fp = (FIL*)fd;
    memset(st, 0, sizeof(struct stat));
    st->st_mode = S_IRUSR | S_IRGRP | S_IROTH;
    st->st_size = f_size(fp);
    if (fp->flag & FA_WRITE)
        st->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
    return 0;
}

static int fatfs_dirreset_r(struct _reent *r, DIR_ITER *dirState) {
    FRESULT fr = f_rewinddir((DIR*)dirState->dirStruct);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    return 0;
}

static int fatfs_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat) {
    FILINFO fno;
    FRESULT fr = f_readdir((DIR*)dirState->dirStruct, &fno);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    if (fno.fname[0] == 0) { r->_errno = 0; return -1; }
    strncpy(filename, fno.fname, NAME_MAX);
    if (filestat) {
        memset(filestat, 0, sizeof(struct stat));
        filestat->st_size = fno.fsize;
        if (fno.fattrib & AM_DIR)
            filestat->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
        else {
            filestat->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
            if (!(fno.fattrib & AM_RDO))
                filestat->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;
        }
    }
    return 0;
}

static int fatfs_dirclose_r(struct _reent *r, DIR_ITER *dirState) {
    FRESULT fr = f_closedir((DIR*)dirState->dirStruct);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    return 0;
}

static int fatfs_ftruncate_r(struct _reent *r, void *fd, off_t len) {
    FIL* fp = (FIL*)fd;
    FRESULT fr = f_lseek(fp, len);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    fr = f_truncate(fp);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    return 0;
}

static int fatfs_fsync_r(struct _reent *r, void *fd) {
    FRESULT fr = f_sync((FIL*)fd);
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; }
    return 0;
}

static long fatfs_fpathconf_r(struct _reent *r, void *fd, int name) {
    (void)fd;
    switch (name) {
        case _PC_LINK_MAX: return 1;
        case _PC_MAX_CANON: return 255;
        case _PC_MAX_INPUT: return 255;
        case _PC_NAME_MAX: return FF_MAX_LFN;
        case _PC_PATH_MAX: return 260;
        case _PC_PIPE_BUF: return 512;
        case _PC_CHOWN_RESTRICTED: return 1;
        case _PC_NO_TRUNC: return 1;
        case _PC_VDISABLE: return 0;
        default: r->_errno = EINVAL; return -1;
    }
}

#define GENERATE_DRIVE_FUNCTIONS(drv) \
static int fatfs_open_r_##drv(struct _reent *r, void *fileStruct, const char *path, int flags, int mode) { \
    (void)mode; \
    char fatPath[256]; \
    buildFatPath(drv, fatPath, sizeof(fatPath), path); \
    FRESULT fr = f_open((FIL*)fileStruct, fatPath, posixToFatFlags(flags)); \
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; } \
    if (flags & O_APPEND) f_lseek((FIL*)fileStruct, f_size((FIL*)fileStruct)); \
    return 0; \
} \
static int fatfs_stat_r_##drv(struct _reent *r, const char *file, struct stat *st) { \
    char fatPath[256]; \
    buildFatPath(drv, fatPath, sizeof(fatPath), file); \
    FILINFO fno; \
    FRESULT fr = f_stat(fatPath, &fno); \
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; } \
    memset(st, 0, sizeof(struct stat)); \
    st->st_size = fno.fsize; \
    if (fno.fattrib & AM_DIR) \
        st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO; \
    else { \
        st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH; \
        if (!(fno.fattrib & AM_RDO)) st->st_mode |= S_IWUSR | S_IWGRP | S_IWOTH; \
    } \
    return 0; \
} \
static int fatfs_unlink_r_##drv(struct _reent *r, const char *name) { \
    char fatPath[256]; \
    buildFatPath(drv, fatPath, sizeof(fatPath), name); \
    FRESULT fr = f_unlink(fatPath); \
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; } \
    return 0; \
} \
static int fatfs_rename_r_##drv(struct _reent *r, const char *oldName, const char *newName) { \
    char fatOld[256], fatNew[256]; \
    buildFatPath(drv, fatOld, sizeof(fatOld), oldName); \
    buildFatPath(drv, fatNew, sizeof(fatNew), newName); \
    FRESULT fr = f_rename(fatOld, fatNew); \
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; } \
    return 0; \
} \
static int fatfs_mkdir_r_##drv(struct _reent *r, const char *path, int mode) { \
    (void)mode; \
    char fatPath[256]; \
    buildFatPath(drv, fatPath, sizeof(fatPath), path); \
    FRESULT fr = f_mkdir(fatPath); \
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; } \
    return 0; \
} \
static DIR_ITER* fatfs_diropen_r_##drv(struct _reent *r, DIR_ITER *dirState, const char *path) { \
    char fatPath[256]; \
    buildFatPath(drv, fatPath, sizeof(fatPath), path); \
    memset(dirState->dirStruct, 0, sizeof(DIR)); \
    FRESULT fr = f_opendir((DIR*)dirState->dirStruct, fatPath); \
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return NULL; } \
    return dirState; \
} \
static int fatfs_rmdir_r_##drv(struct _reent *r, const char *name) { \
    char fatPath[256]; \
    buildFatPath(drv, fatPath, sizeof(fatPath), name); \
    FRESULT fr = f_unlink(fatPath); \
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; } \
    return 0; \
} \
static int fatfs_lstat_r_##drv(struct _reent *r, const char *file, struct stat *st) { \
    return fatfs_stat_r_##drv(r, file, st); \
} \
static int fatfs_statvfs_r_##drv(struct _reent *r, const char *path, struct statvfs *buf) { \
    (void)path; \
    char drvPath[4]; \
    snprintf(drvPath, sizeof(drvPath), "%d:", drv); \
    DWORD freeClusters; \
    FATFS* fatfsPtr = NULL; \
    FRESULT fr = f_getfree((const TCHAR*)drvPath, &freeClusters, &fatfsPtr); \
    if (fr != FR_OK) { r->_errno = fatfs_to_errno(fr); return -1; } \
    memset(buf, 0, sizeof(struct statvfs)); \
    buf->f_bsize = fatfsPtr->csize * 512; \
    buf->f_frsize = fatfsPtr->csize * 512; \
    buf->f_blocks = fatfsPtr->n_fatent - 2; \
    buf->f_bfree = freeClusters; \
    buf->f_bavail = freeClusters; \
    buf->f_files = 0; \
    buf->f_ffree = 0; \
    buf->f_namemax = FF_MAX_LFN; \
    return 0; \
}

static int fatfs_stub_link_r(struct _reent *r, const char *existing, const char *newLink) {
    (void)existing; (void)newLink;
    r->_errno = ENOTSUP; return -1;
}

static int fatfs_stub_chdir_r(struct _reent *r, const char *name) {
    (void)name; return 0;
}

static int fatfs_stub_chmod_r(struct _reent *r, const char *path, mode_t mode) {
    (void)path; (void)mode; return 0;
}

static int fatfs_stub_fchmod_r(struct _reent *r, void *fd, mode_t mode) {
    (void)fd; (void)mode; return 0;
}

static int fatfs_stub_utimes_r(struct _reent *r, const char *filename, const struct timeval times[2]) {
    (void)filename; (void)times; return 0;
}

static long fatfs_stub_pathconf_r(struct _reent *r, const char *path, int name) {
    (void)path; return fatfs_fpathconf_r(r, NULL, name);
}

static int fatfs_stub_symlink_r(struct _reent *r, const char *target, const char *linkpath) {
    (void)target; (void)linkpath;
    r->_errno = ENOTSUP; return -1;
}

static ssize_t fatfs_stub_readlink_r(struct _reent *r, const char *path, char *buf, size_t bufsiz) {
    (void)path; (void)buf; (void)bufsiz;
    r->_errno = ENOTSUP; return -1;
}

// Generates per-drive function sets
GENERATE_DRIVE_FUNCTIONS(0)
GENERATE_DRIVE_FUNCTIONS(1)
GENERATE_DRIVE_FUNCTIONS(2)

// Per-drive devoptab entries

#define BUILD_DEVOPTAB(drv) \
{ \
    .name = NULL, \
    .structSize = sizeof(FIL), \
    .open_r = fatfs_open_r_##drv, \
    .close_r = fatfs_close_r, \
    .write_r = fatfs_write_r, \
    .read_r = fatfs_read_r, \
    .seek_r = fatfs_seek_r, \
    .fstat_r = fatfs_fstat_r, \
    .stat_r = fatfs_stat_r_##drv, \
    .link_r = fatfs_stub_link_r, \
    .unlink_r = fatfs_unlink_r_##drv, \
    .chdir_r = fatfs_stub_chdir_r, \
    .rename_r = fatfs_rename_r_##drv, \
    .mkdir_r = fatfs_mkdir_r_##drv, \
    .dirStateSize = sizeof(DIR), \
    .diropen_r = fatfs_diropen_r_##drv, \
    .dirreset_r = fatfs_dirreset_r, \
    .dirnext_r = fatfs_dirnext_r, \
    .dirclose_r = fatfs_dirclose_r, \
    .statvfs_r = fatfs_statvfs_r_##drv, \
    .ftruncate_r = fatfs_ftruncate_r, \
    .fsync_r = fatfs_fsync_r, \
    .deviceData = NULL, \
    .chmod_r = fatfs_stub_chmod_r, \
    .fchmod_r = fatfs_stub_fchmod_r, \
    .rmdir_r = fatfs_rmdir_r_##drv, \
    .lstat_r = fatfs_lstat_r_##drv, \
    .utimes_r = fatfs_stub_utimes_r, \
    .fpathconf_r = fatfs_fpathconf_r, \
    .pathconf_r = fatfs_stub_pathconf_r, \
    .symlink_r = fatfs_stub_symlink_r, \
    .readlink_r = fatfs_stub_readlink_r, \
}

static const devoptab_t s_devoptabTemplates[MAX_FAT_DEVICES] = {
    BUILD_DEVOPTAB(0),
    BUILD_DEVOPTAB(1),
    BUILD_DEVOPTAB(2),
};

bool FatFsDevoptab_Register(const char* deviceName, unsigned char driveNumber) {
    if (driveNumber >= MAX_FAT_DEVICES) return false;

    // Checks if already registered for this name
    for (int i = 0; i < s_deviceCount; i++) {
        if (strcmp(s_fatDevices[i].name, deviceName) == 0)
            return true;
        if (s_fatDevices[i].driveNum == (int)driveNumber)
            return true;
    }

    if (s_deviceCount >= MAX_FAT_DEVICES) return false;

    FatDevice* dev = &s_fatDevices[s_deviceCount];
    dev->tab = s_devoptabTemplates[driveNumber];
    strncpy(dev->name, deviceName, sizeof(dev->name) - 1);
    dev->name[sizeof(dev->name) - 1] = '\0';
    dev->tab.name = dev->name;
    dev->driveNum = driveNumber;
    dev->registered = false;

    int existing = FindDevice(deviceName);
    if (existing >= 0) RemoveDevice(deviceName);

    int result = AddDevice(&dev->tab);
    if (result < 0) return false;

    dev->registered = true;
    s_deviceCount++;
    return true;
}

bool FatFsDevoptab_Unregister() {
    bool ok = true;
    for (int i = 0; i < s_deviceCount; i++) {
        int dev = FindDevice(s_fatDevices[i].name);
        if (dev >= 0) RemoveDevice(s_fatDevices[i].name);
        s_fatDevices[i].registered = false;
    }
    s_deviceCount = 0;
    return ok;
}
