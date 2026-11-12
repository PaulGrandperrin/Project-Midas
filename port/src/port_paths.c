/**
 * Port file layout: where the game reads and writes its files.
 *
 * Every file lives next to the executable, as the releases ship it. Only this
 * file knows that: the rest of the port asks for a role - user data, the
 * settings file, the files the release ships, the exe directory - instead of
 * passing a name that happens to spell the folder.
 *
 * Kept out of main.c so it can be built on its own.
 */
#define _FILE_OFFSET_BITS 64   /* stat() on 32-bit ELF with 64-bit inodes: EOVERFLOW, as romdata.c notes */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>   /* stat, mkdir */
#ifdef _WIN32
#include <direct.h>    /* _mkdir */
#include <windows.h>   /* GetModuleFileNameA */
#else
#include <unistd.h>    /* readlink */
#endif
#include "port_paths.h"

extern void portConLogf(const char *fmt, ...);   /* port_console.c */

static char sPortExeDir[PORT_DIR_MAX];   /* with trailing separator; "" = cwd fallback */
static char sPortLogDir[PORT_PATH_MAX];  /* Data/Logs/ next to the exe */

/* ==== small path helpers ================================================= */

#ifdef _WIN32
static void portMkdirOne(const char *path) { _mkdir(path); }
#else
static void portMkdirOne(const char *path) { mkdir(path, 0755); }
#endif

static int portIsDir(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* out = dir + name. Returns 0 and "" when it does not fit, so a caller's
 * open/mkdir fails visibly instead of using a truncated path. */
static int portPathCpy(char *out, size_t n, const char *dir, const char *name)
{
    int len = snprintf(out, n, "%s%s", dir, name);

    if (len > 0 && (size_t) len < n) {
        return 1;
    }
    out[0] = '\0';
    return 0;
}

/* dir + name into a role's buffer, with the one-line warning the callers
 * cannot give (they have no size to check against) */
static const char *portJoinName(char *out, const char *dir, const char *name)
{
    if (!portPathCpy(out, PORT_PATH_MAX, dir, name)) {
        static int warned;

        if (!warned) {
            warned = 1;
            portConLogf("ge: path too long: %s%s\n", dir, name);
        }
    }
    return out;
}

/* keep a directory path's trailing separator (portSharePath and portEnsureDir
 * append a file or child name to it) */
static void portAddSep(char *out, size_t n)
{
    size_t len = 0;

    while (out[len] != '\0') {
        len++;
    }
    if (len > 0 && len + 1 < n && out[len - 1] != '/' && out[len - 1] != '\\') {
        out[len] = '/';
        out[len + 1] = '\0';
    }
}

int portEnsureDir(const char *path)
{
    char buf[PORT_PATH_MAX];
    size_t i = 0;

    if (path == NULL || path[0] == '\0' || !portPathCpy(buf, sizeof(buf), path, "")) {
        return 0;
    }
    if (portIsDir(buf)) {
        return 1;   /* the common case after the first run */
    }
    /* skip a drive letter and the root separator: neither is ours to create
     * (_mkdir("C:") would target the drive itself) */
    if (buf[1] == ':') {
        i = 2;
    }
    if (buf[i] == '/' || buf[i] == '\\') {
        i++;
    }
    for (; buf[i] != '\0'; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char sep = buf[i];

            buf[i] = '\0';
            portMkdirOne(buf);   /* fails harmlessly when it exists */
            buf[i] = sep;
        }
    }
    portMkdirOne(buf);
    if (portIsDir(buf)) {
        portConLogf("ge: paths: created %s\n", buf);
        return 1;
    }
    portConLogf("ge: paths: could not create %s (read-only install dir?)\n", buf);
    return 0;
}

/* ==== the roles: everything next to the executable ====================== */

/* The settings file was called ge.ini before the Project Midas rename: when
 * there is no midas.ini yet, rename an existing ge.ini once so players keep
 * their settings. */
static void portMigrateIni(void)
{
    char oldpath[PORT_PATH_MAX];
    FILE *f = portCfgFopen("midas.ini", "r");

    if (f != NULL) {
        fclose(f);
        return;
    }
    snprintf(oldpath, sizeof(oldpath), "%s", portCfgPath("ge.ini"));
    if (rename(oldpath, portCfgPath("midas.ini")) == 0) {
        portConLogf("ge: renamed ge.ini to midas.ini\n");
    }
}

/* one exe dir, four roles: a call site still says which file it wants */
const char *portDataPath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortExeDir, name);
}

const char *portCfgPath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortExeDir, name);
}

const char *portSharePath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortExeDir, name);
}

int portLegacyFiles(void)
{
    return 1;   /* old eeprom.bin, Profiles/, WeaponSets/, Devices/, ge.ini */
}

const char *portExePath(const char *name)
{
    static char path[PORT_PATH_MAX];

    return portJoinName(path, sPortExeDir, name);
}

FILE *portDataFopen(const char *name, const char *mode)
{
    return fopen(portDataPath(name), mode);
}

FILE *portCfgFopen(const char *name, const char *mode)
{
    return fopen(portCfgPath(name), mode);
}

/* Data/Logs/<name>, made on first use: the run log and crash.log live there,
 * beside the saves and profiles, not next to the exe. Its folder has its own
 * buffer, so asking for a log path never clobbers portDataPath's. */
const char *portLogPath(const char *name)
{
    static char path[PORT_PATH_MAX];

    portEnsureDir(sPortLogDir);
    return portJoinName(path, sPortLogDir, name);
}

/* ==== startup =========================================================== */

/* always-on: log where each class of file lives this run */
void portPathsLogDirs(void)
{
    portConLogf("ge: root: %s\n", sPortExeDir[0] != '\0' ? sPortExeDir : "(cwd)");
}

static void portPathsFinish(void)
{
    snprintf(sPortLogDir, sizeof(sPortLogDir), "%sData/Logs/", sPortExeDir);
    portPathsLogDirs();
    portMigrateIni();
}

void portPathsInitFromDir(const char *dir)
{
    snprintf(sPortExeDir, sizeof(sPortExeDir), "%s", dir != NULL ? dir : "");
    portAddSep(sPortExeDir, sizeof(sPortExeDir));
    /* no separator (a bare name launched through PATH): the roles stay
     * cwd-relative, as before */
    portPathsFinish();
}

void portPathsInit(const char *argv0)
{
    char file[PORT_DIR_MAX];
    char dir[PORT_DIR_MAX];
    int last = -1;
    int i;

#ifdef _WIN32
    /* authoritative on Windows regardless of how we were launched */
    DWORD n;

    (void) argv0;
    n = GetModuleFileNameA(NULL, file, sizeof(file) - 1);
    if (n == 0 || n >= sizeof(file) - 1) {
        file[0] = '\0';
    } else {
        file[n] = '\0';
    }
#else
    /* argv[0] is not authoritative on Linux (a PATH launch has no slash, and
     * launchers/symlinks point elsewhere), so use the /proc equivalent of
     * GetModuleFileNameA and keep argv[0] for other POSIX only. */
    ssize_t n = readlink("/proc/self/exe", file, sizeof(file) - 1);

    if (n > 0 && (size_t) n < sizeof(file) - 1) {
        file[n] = '\0';
    } else if (argv0 == NULL || snprintf(file, sizeof(file), "%s", argv0) >= (int) sizeof(file)) {
        file[0] = '\0';
    }
#endif
    /* keep the separator, drop the basename */
    for (i = 0; file[i] != '\0'; i++) {
        if (file[i] == '/' || file[i] == '\\') {
            last = i;
        }
    }
    if (last < 0) {
        dir[0] = '\0';   /* no separator: no directory to use */
    } else {
        snprintf(dir, sizeof(dir), "%.*s", last + 1, file);
    }
    portPathsInitFromDir(dir);
}
