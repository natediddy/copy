/*
 * copy - Copy files and directories
 *
 * Copyright (C) 2014  Nathan Forbes
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifndef _WIN32
# include <sys/ioctl.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#ifdef _WIN32
# include <windows.h>
#else
# include <unistd.h>
#endif
#include <utime.h>

#define PROGRAM_NAME     "copy"
#define PROGRAM_VERSION "1.1.0"

#define PERCENT_BUFMAX         6
#define SIZE_BUFMAX           64
#define TIME_BUFMAX          128
#define RESPONSE_BUFMAX      128
#define PATH_BUFMAX         1024
#define PROGRESS_OUT_BUFMAX 1024

#define PROGRESS_BAR_START     '['
#define PROGRESS_BAR_SO_FAR    '='
#define PROGRESS_BAR_HEAD      '>'
#define PROGRESS_BAR_REMAINING ' '
#define PROGRESS_BAR_END       ']'

#define CHUNK_SIZE 10000

#define FALLBACK_CONSOLE_WIDTH 40

#define UPDATE_INTERVAL 0.5

#define MILLISECONDS_PER_SECOND 1000
#define SECONDS_PER_HOUR        3600
#define SECONDS_PER_MINUTE        60

typedef unsigned char bool;
#define false ((bool)0)
#define true  ((bool)1)

typedef uint64_t byte_t;
#define BYTE_C(n)           UINT64_C(n)
#define BYTE_M               "%" PRIu64
#define BYTE2LDBL(n) ((long double)(n))

#define B_SHORT        "B"
#define B_LONG      "byte"
#define KB_SHORT        "K"
#define KB_LONG "kilobytes"
#define MB_SHORT        "M"
#define MB_LONG "megabytes"
#define GB_SHORT        "G"
#define GB_LONG "gigabytes"
#define TB_SHORT        "T"
#define TB_LONG "terabytes"
#define PB_SHORT        "P"
#define PB_LONG "petabytes"
#define EB_SHORT        "E"
#define EB_LONG  "exabytes"

#define KB_FACTOR                BYTE_C(1000)
#define MB_FACTOR             BYTE_C(1000000)
#define GB_FACTOR          BYTE_C(1000000000)
#define TB_FACTOR       BYTE_C(1000000000000)
#define PB_FACTOR    BYTE_C(1000000000000000)
#define EB_FACTOR BYTE_C(1000000000000000000)

#define SIZE_BYTES_FORMAT   BYTE_M "%c"
#define SIZE_PRECISION_FORMAT "%.1Lf%c"

#define die(errNo, ...) \
    do { \
        pError((errNo), __VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while (0)

#ifdef DEBUGGING
# define debug(...) \
    do { \
        char __tag[256]; \
        snprintf(__tag, 256, "DEBUG:%s:%s:%i: ", \
                 __FILE__, __func__, __LINE__); \
        __debug(__tag, __VA_ARGS__); \
    } while (0)
#else
# define debug(...)
#endif

#define progressIntervalHasPassed(m) \
    ((m) >= (MILLISECONDS_PER_SECOND * updateInterval))

#ifdef _WIN32
# define DIR_SEPARATOR     '\\'
# define isDirSeparator(c) (((c) == DIR_SEPARATOR) || ((c) == '/'))
# define makeDir(path)     (_mkdir(path) == 0)
#else
# define DIR_SEPARATOR     '/'
# define isDirSeparator(c) ((c) == DIR_SEPARATOR)
# define makeDir(path)     (mkdir(path, S_IRWXU) == 0)
#endif

#define forever ;;

enum {
    TYPE_UNKNOWN,
    TYPE_NON_EXISTING,
    TYPE_UNSUPPORTED,
    TYPE_FILE,
    TYPE_DIRECTORY
};

/* long options with no corresponding short options */
enum {
    NO_PROGRESS_OPTION = CHAR_MAX + 1,
    NO_REPORT_OPTION
};

static const char *   programName;

static bool           showingProgress       = true;
static bool           showingReport         = true;
static bool           preservingOwnership   = false;
static bool           preservingPermissions = false;
static bool           preservingTimestamp   = false;

static size_t         totalSources          = 0;

static byte_t         totalBytes            = BYTE_C(0);
static byte_t         soFarBytes            = BYTE_C(0);

static double         updateInterval        = UPDATE_INTERVAL;

static char           directoryTransferSourceRoot[PATH_BUFMAX];
static char           directoryTransferDestinationRoot[PATH_BUFMAX];

static struct timeval startTime;

struct {
    size_t nSource;
    byte_t total;
    byte_t soFar;
    struct timeval iLast;
    struct timeval iCurrent;
    char sTotal[SIZE_BUFMAX];
    struct {
        int size;
        int stopPos;
        int soFarPos;
        long double factor;
    } bar;
} pData;

static struct option const options[] = {
    {"preserve-ownership", no_argument, NULL, 'o'},
    {"preserve-permissions", no_argument, NULL, 'p'},
    {"preserve-all", no_argument, NULL, 'P'},
    {"preserve-timestamp", no_argument, NULL, 't'},
    {"update-interval", required_argument, NULL, 'u'},
    {"no-progress", no_argument, NULL, NO_PROGRESS_OPTION},
    {"no-report", no_argument, NULL, NO_REPORT_OPTION},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0}
};

static void setProgramName(const char *argv0)
{
    char *p;

    if (argv0 && *argv0) {
        p = strrchr(argv0, DIR_SEPARATOR);
#ifdef _WIN32
        if (!p)
            p = strrchr(argv0, '/');
#endif
        if (p && *p && *(p + 1))
            programName = p + 1;
        else
            programName = argv0;
        return;
    }
    programName = PROGRAM_NAME;
}

static void usage(bool hadError)
{
    fprintf((!hadError) ? stdout : stderr,
            "Usage: %s [OPTION...] SOURCE... DESTINATION\n",
            programName);

    if (!hadError)
        fputs("Options:\n"
              "  -o, --preserve-ownership\n"
              "                     preserve ownership\n"
              "  -p, --preserve-permissions\n"
              "                     preserve permissions\n"
              "  -P, --preserve-all preserve all timestamp, ownership, and\n"
              "                     permission data\n"
              "  -t, --preserve-timestamp\n"
              "                     preserve timestamps\n"
              "  -u <N>, --update-interval=<N>\n"
              "                     set the progress update interval to\n"
              "                     every <N> seconds (default is 1 second)\n"
              "  --no-progress      do not show any progress during copy\n"
              "                     operations\n"
              "  --no-report        do not show completion report after\n"
              "                     copy operations are finished\n"
              "  -h, --help         print this text and exit\n"
              "  -v, --version      print version information and exit\n",
              stdout);

    if (hadError)
        exit(EXIT_FAILURE);
    exit(EXIT_SUCCESS);
}

static void version(void)
{
    fputs(PROGRAM_NAME " " PROGRAM_VERSION "\n"
          "Copyright (C) 2014 Nathan Forbes <sforbes41@gmail.com>\n"
          "This is free software; see the source for copying conditions.\n"
          "There is NO warranty; not even for MERCHANTABILITY or FITNESS\n"
          "FOR A PARTICULAR PURPOSE.\n",
          stdout);
    exit(EXIT_SUCCESS);
}

#ifdef DEBUGGING
static void __debug(const char *tag, const char *fmt, ...)
{
    va_list args;

    fputs(programName, stderr);
    fputc(':', stderr);
    fputs(tag, stderr);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}
#endif

static void pError(int errNo, const char *fmt, ...)
{
    va_list args;

    fputs(programName, stderr);
    fputs(": error: ", stderr);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if (errNo != 0) {
        fputs(": ", stderr);
        fputs(strerror(errNo), stderr);
    }
    fputc('\n', stderr);
}

static bool stringsAreEqual(const char *s1, const char *s2, bool ignoreCase)
{
    size_t n;

    n = strlen(s1);
    if (n != strlen(s2))
        return false;

    if (ignoreCase) {
        for (; (*s1 && *s2); ++s1, ++s2)
            if (tolower(*s1) != tolower(*s2))
                return false;
    } else if (memcmp(s1, s2, n) != 0)
        return false;
    return true;
}

/* Path basename routine from glib-2.0 (but without mallocing anything). */
static void baseName(char *buffer, const char *path)
{
    size_t n;
    ssize_t base;
    ssize_t lastNonSlash;

    if (path) {
        if (!*path) {
            buffer[0] = '.';
            buffer[1] = '\0';
            return;
        }
        lastNonSlash = strlen(path) - 1;
        while ((lastNonSlash >= 0) && isDirSeparator(path[lastNonSlash]))
            lastNonSlash--;
        if (lastNonSlash == -1) {
            buffer[0] = DIR_SEPARATOR;
            buffer[1] = '\0';
            return;
        }
#ifdef _WIN32
        if ((lastNonSlash == 1) && isalpha(path[0]) && (path[1] == ':')) {
            buffer[0] = DIR_SEPARATOR;
            buffer[1] = '\0';
            return;
        }
#endif
        base = lastNonSlash;
        while ((base >= 0) && !isDirSeparator(path[base]))
            base--;
#ifdef _WIN32
        if ((base == -1) && isalpha(path[0]) && (path[1] == ':'))
            base = 1;
#endif
        n = lastNonSlash - base;
        if (n >= (PATH_BUFMAX - 1))
            n = PATH_BUFMAX - 1;
        memcpy(buffer, path + base + 1, n);
        buffer[n] = '\0';
        return;
    }
    buffer[0] = '\0';
}

/* Path dirname routine from glib-2.0 (but without mallocing anything). */
static void dirName(char *buffer, const char *path)
{
    size_t n;
    char *base;

    if (path) {
        base = strrchr(path, DIR_SEPARATOR);
#ifdef _WIN32
        {
            char *p = strrchr(path, '/');
            if (!base || (p && (p > base)))
                base = p;
        }
#endif
        if (!base) {
#ifdef _WIN32
            if (isalpha(path[0]) && (path[1] == ':')) {
                buffer[0] = path[0];
                buffer[1] = ':';
                buffer[2] = '.';
                buffer[3] = '\0';
                return;
            }
#endif
            buffer[0] = '.';
            buffer[1] = '\0';
            return;
        }
        while ((base > path) && isDirSeparator(*base))
            base--;
#ifdef _WIN32
        if ((base == (path + 1)) && isalpha(path[0]) && (path[1] == ':'))
            base++;
        else if (isDirSeparator(path[0]) && isDirSeparator(path[1]) && path[2]
                 && !isDirSeparator(path[2]) && (base >= (path + 2))) {
            const char *p = path + 2;
            while (*p && !isDirSeparator(*p))
                p++;
            if (p == (base + 1)) {
                n = (unsigned int)strlen(path) + 1;
                if (n >= (PATH_BUFMAX - 1))
                    n = PATH_BUFMAX - 1;
                strcpy(buffer, path);
                buffer[n - 1] = DIR_SEPARATOR;
                buffer[n] = '\0';
                return;
            }
            if (isDirSeparator(*p)) {
                p++;
                while (*p && !isDirSeparator(*p))
                    p++;
                if (p == (base + 1))
                    base++;
            }
        }
#endif
        n = (unsigned int)1 + base - path;
        if (n >= (PATH_BUFMAX - 1))
            n = PATH_BUFMAX - 1;
        memcpy(buffer, path, n);
        buffer[n] = '\0';
        return;
    }
    buffer[0] = '\0';
}

static void getTimeOfDay(struct timeval *tv)
{
    memset(tv, 0, sizeof (struct timeval));
    if (gettimeofday(tv, NULL) != 0)
        die(errno, "failed to get time of day");
}

static bool isAbsolutePath(const char *path)
{
    if (path) {
        if (isDirSeparator(*path))
            return true;
#ifdef _WIN32
        if (isalpha(path[0]) && (path[1] == ':') && isDirSeparator(path[2]))
            return true;
#endif
    }
    return false;
}

static void absolutePath(char *buffer, const char *path)
{
    size_t nPath;

    if (path) {
        nPath = strlen(path);
        if (nPath >= (PATH_BUFMAX - 1))
            die(0, "preventing buffer overflow");
        if (!isAbsolutePath(path)) {
            size_t n;
            size_t nCwd;
            char cwd[PATH_BUFMAX];
            if (!getcwd(cwd, PATH_BUFMAX))
                die(errno, "failed to get current working directory");
            nCwd = strlen(cwd);
            n = nCwd + nPath + 1;
            if (n >= (PATH_BUFMAX - 1))
                die(0, "preventing buffer overflow");
            memcpy(buffer, cwd, nCwd);
            buffer[nCwd] = DIR_SEPARATOR;
            memcpy(buffer + (nCwd + 1), path, nPath);
            buffer[n] = '\0';
        } else
            memcpy(buffer, path, nPath + 1);
        return;
    }
    buffer[0] = '\0';
}

static void makePath(const char *path)
{
    char c;
    char *p;
    char abs[PATH_BUFMAX];

    absolutePath(abs, path);
    p = abs;

    while (*p) {
        p++;
        while (*p && !isDirSeparator(*p))
            p++;
        c = *p;
        *p = '\0';
        if (!makeDir(abs) && (errno != EEXIST)) {
            pError(errno, "failed to create directory `%s'", abs);
            exit(EXIT_FAILURE);
        }
        *p = c;
    }
}

static void setDirectoryTransferSourceRoot(const char *src)
{
    directoryTransferSourceRoot[0] = '\0';
    memcpy(directoryTransferSourceRoot, src, strlen(src) + 1);
}

static void setDirectoryTransferDestinationRoot(const char *dst)
{
    directoryTransferDestinationRoot[0] = '\0';
    memcpy(directoryTransferDestinationRoot, dst, strlen(dst) + 1);
}

static void directoryContentSize(const char *path, byte_t *size)
{
    size_t nChild;
    size_t nPath;
    size_t nName;
    struct stat st;
    DIR *pDir;
    struct dirent *pDirEnt;

    pDir = opendir(path);
    if (!pDir)
        die(errno, "failed to open directory -- `%s'", path);

    nPath = strlen(path);
    for (forever) {
        pDirEnt = readdir(pDir);
        if (!pDirEnt) {
            if ((errno != 0) && (errno != ENOENT) && (errno != EEXIST))
                pError(errno, "failed to read directory -- `%s'", path);
            break;
        }
        if (stringsAreEqual(pDirEnt->d_name, ".", true)
            || stringsAreEqual(pDirEnt->d_name, "..", true))
            continue;
        nName = strlen(pDirEnt->d_name);
        nChild = nPath + nName + 1;
        char child[nChild + 1];
        memcpy(child, path, nPath);
        child[nPath] = DIR_SEPARATOR;
        memcpy(child + (nPath + 1), pDirEnt->d_name, nName);
        child[nChild] = '\0';
        memset(&st, 0, sizeof (struct stat));
        if (stat(child, &st) == 0) {
            if (S_ISDIR(st.st_mode))
                directoryContentSize(child, size);
            else
                *size += (byte_t)st.st_size;
        }
    }
    closedir(pDir);
}

static bool getOverwritePermission(const char *path)
{
    int c;
    size_t p;

    printf("\nDestination already exists -- `%s'\n", path);
    for (forever)
    {
        fputs("Overwrite? (data will be lost) [y/n] ", stdout);
        char res[RESPONSE_BUFMAX];
        p = 0;
        for (forever) {
            c = fgetc(stdin);
            if ((c == EOF) || (c == '\n') || (p == (RESPONSE_BUFMAX - 1)))
                break;
            res[p++] = (char)c;
        }
        res[p] = '\0';
        fputc('\n', stdout);
        if (stringsAreEqual(res, "y", true)
            || stringsAreEqual(res, "yes", true)
            || stringsAreEqual(res, "yep", true)
            || stringsAreEqual(res, "yeah", true)
            || stringsAreEqual(res, "ok", true)
            || stringsAreEqual(res, "okay", true)
            || stringsAreEqual(res, "1", false)
            || stringsAreEqual(res, "true", true))
            return true;
        if (stringsAreEqual(res, "n", true)
            || stringsAreEqual(res, "no", true)
            || stringsAreEqual(res, "nope", true)
            || stringsAreEqual(res, "nah", true)
            || stringsAreEqual(res, "0", false)
            || stringsAreEqual(res, "false", true))
            return false;
        pError(0, "unrecognized response, please try again...\n", res);
    }
    return false;
}

static long getMilliseconds(const struct timeval *s, const struct timeval *e)
{
    return (((e->tv_sec - s->tv_sec) * MILLISECONDS_PER_SECOND) +
            ((e->tv_usec - s->tv_usec) / MILLISECONDS_PER_SECOND));
}

static void formatTime(char *buffer,
                       const struct timeval *start,
                       const struct timeval *end)
{
    double totalSeconds;

    totalSeconds = (getMilliseconds(start, end) / MILLISECONDS_PER_SECOND);
    if (totalSeconds < 1.0) {
        snprintf(buffer, TIME_BUFMAX, "%g seconds", totalSeconds);
        return;
    }

    int hours = (((int)totalSeconds) / SECONDS_PER_HOUR);
    int minutes = (((int)totalSeconds) / SECONDS_PER_MINUTE);
    int seconds = (((int)totalSeconds) % SECONDS_PER_MINUTE);
    size_t nBuffer = 0;

    if (hours > 0) {
        snprintf(buffer, TIME_BUFMAX, "%i hour%s",
                 hours, (hours == 1) ? "" : "s");
        nBuffer = strlen(buffer);
    }

    if (minutes > 0) {
        if (hours > 0)
            buffer[nBuffer++] = ' ';
        snprintf(buffer + nBuffer, TIME_BUFMAX - nBuffer, "%i minute%s",
                 minutes, (minutes == 1) ? "" : "s");
        nBuffer = strlen(buffer);
    }

    if (seconds > 0) {
        if ((hours > 0) || (minutes > 0))
            buffer[nBuffer++] = ' ';
        snprintf(buffer + nBuffer, TIME_BUFMAX - nBuffer, "%i second%s",
                 seconds, (seconds == 1) ? "" : "s");
    }
}

static void formatSize(char *buffer, byte_t bytes, bool longFormat)
{
#if 0
#define __sFmt(__factor, __long, __short) \
    if (longFormat) \
        snprintf(buffer, SIZE_BUFMAX, "%.2f " __long, \
                 (BYTE2LDBL(bytes) / BYTE2LDBL(__factor))); \
    else \
        snprintf(buffer, SIZE_BUFMAX, "%.1f" __short, \
                 (BYTE2LDBL(bytes) / BYTE2LDBL(__factor)))
#endif

#define __sFmt(__prefix) \
    if (longFormat) \
        snprintf(buffer, SIZE_BUFMAX, "%.2Lf " __prefix ## _LONG, \
                 (BYTE2LDBL(bytes) / BYTE2LDBL(__prefix ## _FACTOR))); \
    else \
        snprintf(buffer, SIZE_BUFMAX, "%.1Lf" __prefix ## _SHORT, \
                 (BYTE2LDBL(bytes) / BYTE2LDBL(__prefix ## _FACTOR)))

    if (bytes < KB_FACTOR) {
        if (longFormat)
            snprintf(buffer, SIZE_BUFMAX, BYTE_M " " B_LONG "%s",
                     bytes, (bytes == 1) ? "" : "s");
        else
            snprintf(buffer, SIZE_BUFMAX, BYTE_M B_SHORT, bytes);
    } else if (bytes < MB_FACTOR)
        __sFmt(KB);
    else if (bytes < GB_FACTOR)
        __sFmt(MB);
    else if (bytes < TB_FACTOR)
        __sFmt(GB);
    else if (bytes < PB_FACTOR)
        __sFmt(TB);
    else if (bytes < EB_FACTOR)
        __sFmt(PB);
    else
        __sFmt(EB);

#undef __sFmt
}

static void formatPercent(char *buffer, byte_t soFar, byte_t total)
{
    long double x;

    x = (BYTE2LDBL(soFar) / BYTE2LDBL(total));
    if (isnan (x) || isnan (x * 100))
        memcpy(buffer, "0%", 3);
    else
        snprintf(buffer, PERCENT_BUFMAX, "%.0Lf%%", x * 100);
}

static int consoleWidth(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO x;

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &x))
        return x.dwSize.X;
#else
    struct winsize x;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &x) != -1)
        return x.ws_col;
#endif
    return FALLBACK_CONSOLE_WIDTH;
}

static void progressInit(byte_t total, size_t nSource)
{
    pData.nSource = nSource;
    pData.total = total;
    pData.soFar = BYTE_C (0);
    pData.iLast.tv_sec = -1;
    pData.iLast.tv_usec = -1;
    pData.iCurrent.tv_sec = -1;
    pData.iCurrent.tv_usec = -1;
    formatSize(pData.sTotal, pData.total, false);
    memset(&pData.bar, 0, sizeof (pData.bar));
}

static void progressPrintf(int *remainingSpace, const char *fmt, ...)
{
    va_list args;
    char buffer[PROGRESS_OUT_BUFMAX];

    /* We need to write the formatted string to a buffer first
       in order to obtain its length so it can be subtracted from
       the remaining space of the console */
    va_start(args, fmt);
    vsnprintf(buffer, PROGRESS_OUT_BUFMAX, fmt, args);
    va_end(args);
    fputs(buffer, stdout);
    *remainingSpace -= (int)strlen(buffer);
}

static void progressPutChar(int *remainingSpace, char c)
{
    fputc(c, stdout);
    (*remainingSpace)--;
}

static void progressBarSet(int remainingSpace, int endStringSizes)
{
    pData.bar.stopPos = endStringSizes;
    pData.bar.size = remainingSpace - pData.bar.stopPos - 2;
    pData.bar.factor = (BYTE2LDBL(pData.soFar) / BYTE2LDBL(pData.total));
    pData.bar.soFarPos = roundl(pData.bar.factor * pData.bar.size);
}

static void progressShow(void)
{
    /*
     * Progress format:
     *
     *   For example if copying a single item that is 1GB:
     *     500.0M/1.0G [=======================>                       ] 50%
     *
     *   For example if copying 4 items that are 1GB each:
     *     100% 1.0G/1.0G (item 1/4) [===============>] total: 1.0G/4.0G 25%
     *     100% 1.0G/1.0G (item 2/4) [===============>] total: 2.0G/4.0G 50%
     *     100% 1.0G/1.0G (item 3/4) [===============>] total: 3.0G/4.0G 75%
     *     50% 500.0M/1.0G (item 4/4) [=======>       ] total: 3.5G/4.0G 87%
     *   Each item will get its own line and progress bar.
     */

    int x;
    int remainingSpace;
    int endStringSizes;
    char pSoFar[PERCENT_BUFMAX];
    char sSoFar[SIZE_BUFMAX];
    char pTotalTotal[PERCENT_BUFMAX];
    char sTotalSoFar[SIZE_BUFMAX];
    char sTotalTotal[SIZE_BUFMAX];

    formatPercent(pSoFar, pData.soFar, pData.total);
    formatSize(sSoFar, pData.soFar, false);
    remainingSpace = consoleWidth();

    if (totalSources > 1) {
        progressPrintf(&remainingSpace, "%s %s/%s (item %zu/%zu) ",
                       pSoFar, sSoFar, pData.sTotal,
                       pData.nSource, totalSources);
        formatPercent(pTotalTotal, soFarBytes, totalBytes);
        formatSize(sTotalSoFar, soFarBytes, false);
        formatSize(sTotalTotal, totalBytes, false);
        endStringSizes = strlen(" total: ") + strlen(sTotalSoFar) +
                         strlen(sTotalTotal) + strlen(pTotalTotal) + 3;
    } else {
        progressPrintf(&remainingSpace, "%s/%s ", sSoFar, pData.sTotal);
        endStringSizes = strlen(pSoFar) + 2;
    }

    progressBarSet(remainingSpace, endStringSizes);
    if (pData.bar.size) {
        progressPutChar(&remainingSpace, PROGRESS_BAR_START);
        for (x = 0; (x < pData.bar.soFarPos); ++x)
            progressPutChar(&remainingSpace, PROGRESS_BAR_SO_FAR);
        progressPutChar(&remainingSpace, PROGRESS_BAR_HEAD);
        for (; (x < pData.bar.size); ++x)
            progressPutChar(&remainingSpace, PROGRESS_BAR_REMAINING);
        progressPutChar(&remainingSpace, PROGRESS_BAR_END);
    }

    if (totalSources > 1)
        progressPrintf(&remainingSpace, " total: %s/%s %s",
                       sTotalSoFar, sTotalTotal, pTotalTotal);
    else
        progressPrintf(&remainingSpace, " %s", pSoFar);

    fputc('\r', stdout);
    fflush(stdout);
}

static void progressFinish(void)
{
    progressShow();
    fputc('\n', stdout);
}

static long progressIntervalInit(void)
{
    if ((pData.iLast.tv_sec != -1) && (pData.iLast.tv_usec != -1)) {
        getTimeOfDay (&pData.iCurrent);
        return getMilliseconds(&pData.iLast, &pData.iCurrent);
    }
    return -1;
}

static void progressIntervalUpdate(long milliseconds)
{
    if (milliseconds != -1)
        memcpy(&pData.iLast, &pData.iCurrent, sizeof (struct timeval));
    else
        getTimeOfDay(&pData.iLast);
}

static void progressUpdate(void)
{
    long m;

    m = progressIntervalInit();
    if ((m == -1) || progressIntervalHasPassed(m)) {
        progressShow();
        progressIntervalUpdate(m);
    }
}

static void transferFile(const char *sourcePath, const char *destinationPath)
{
    size_t bytesRead;
    FILE *pSourceFile;
    FILE *pDestinationFile;

    pSourceFile = fopen(sourcePath, "rb");
    if (!pSourceFile)
        die(errno, "failed to open source -- `%s'", sourcePath);

    pDestinationFile = fopen(destinationPath, "wb");
    if (!pDestinationFile) {
        pError(errno, "failed to open destination -- `%s'", destinationPath);
        fclose(pSourceFile);
        exit(EXIT_FAILURE);
    }

    for (forever) {
        unsigned char chunk[CHUNK_SIZE];
        bytesRead = fread(chunk, 1, CHUNK_SIZE, pSourceFile);
        fwrite(chunk, 1, bytesRead, pDestinationFile);
        pData.soFar += (byte_t)bytesRead;
        soFarBytes += (byte_t)bytesRead;
        if (showingProgress)
            progressUpdate();
        if (ferror(pSourceFile) || feof(pSourceFile))
            break;
    }

    fclose(pSourceFile);
    fclose(pDestinationFile);
}

static void getDirectoryTransferDestinationPath(char *buffer,
                                                const char *sourcePath)
{
    size_t nSourcePath;
    size_t nSourceRoot;
    size_t nDestinationRoot;
    size_t nResult;

    nSourcePath = strlen(sourcePath);
    nSourceRoot = strlen(directoryTransferSourceRoot);
    nDestinationRoot = strlen(directoryTransferDestinationRoot);
    nResult = (nSourcePath - nSourceRoot) + nDestinationRoot;

    if (nResult >= (PATH_BUFMAX - 1))
        die(0, "preventing buffer overflow");

    memcpy(buffer, directoryTransferDestinationRoot, nDestinationRoot);
    memcpy(buffer + nDestinationRoot,
           sourcePath + nSourceRoot,
           nSourcePath - nSourceRoot);
    buffer[nResult] = '\0';
}

static void preserveAttributes(const char *sourcePath,
                               const char *destinationPath,
                               struct stat *sourceStat)
{
    if (preservingTimestamp) {
        struct utimbuf timestamp;
        timestamp.actime = sourceStat->st_atime;
        timestamp.modtime = sourceStat->st_mtime;
        if (utime(destinationPath, &timestamp) != 0)
            pError(errno, "failed to preserve timestamp for `%s'",
                   destinationPath);
    }

    if (preservingOwnership
        && (chown(destinationPath,
                  sourceStat->st_uid,
                  sourceStat->st_gid) != 0))
        pError(errno, "failed to preserve ownership for `%s'",
               destinationPath);

    if (preservingPermissions
        && (chmod(destinationPath, sourceStat->st_mode) != 0))
        pError(errno, "failed to preserve permissions for `%s'",
               destinationPath);
}

static void transferDirectory(const char *rootPath)
{
    size_t nChildPath;
    size_t nRootPath;
    size_t nDName;
    struct stat childStat;
    DIR *pDir;
    struct dirent *pDirEnt;

    pDir = opendir(rootPath);
    if (!pDir)
        die(errno, "failed to open directory -- `%s'", rootPath);

    nRootPath = strlen(rootPath);
    for (forever) {
        pDirEnt = readdir(pDir);
        if (!pDirEnt) {
            if ((errno != 0) && (errno != ENOENT) && (errno != EEXIST))
                pError(errno, "failed to read directory -- `%s'", rootPath);
            break;
        }
        if (stringsAreEqual(pDirEnt->d_name, ".", false)
            || stringsAreEqual(pDirEnt->d_name, "..", false))
            continue;
        nDName = strlen(pDirEnt->d_name);
        nChildPath = nRootPath + nDName + 1;
        char childPath[nChildPath + 1];
        memcpy(childPath, rootPath, nRootPath);
        childPath[nRootPath] = DIR_SEPARATOR;
        memcpy(childPath + (nRootPath + 1), pDirEnt->d_name, nDName);
        childPath[nChildPath] = '\0';
        char destinationPath[PATH_BUFMAX];
        getDirectoryTransferDestinationPath (destinationPath, childPath);
        memset(&childStat, 0, sizeof (struct stat));
        if (stat(childPath, &childStat) == 0) {
            if (S_ISDIR(childStat.st_mode)) {
                makePath(destinationPath);
                transferDirectory(childPath);
            } else if (S_ISREG(childStat.st_mode))
                transferFile(childPath, destinationPath);
            preserveAttributes(childPath, destinationPath, &childStat);
        }
    }
    closedir(pDir);
}

static void doCopy(const char *sourcePath,
                   int sourceType,
                   byte_t sourceSize,
                   size_t sourceItemCount,
                   const char *destinationPath,
                   int destinationType)
{
    if (showingProgress)
        progressInit(sourceSize, sourceItemCount);

    if (sourceType == TYPE_DIRECTORY) {
        setDirectoryTransferSourceRoot(sourcePath);
        setDirectoryTransferDestinationRoot(destinationPath);
        makePath(directoryTransferDestinationRoot);
        transferDirectory(directoryTransferSourceRoot);
    } else
        transferFile(sourcePath, destinationPath);

    if (preservingOwnership || preservingPermissions || preservingTimestamp) {
        struct stat sourceStat;
        memset(&sourceStat, 0, sizeof (struct stat));
        (void)stat(sourcePath, &sourceStat);
        preserveAttributes(sourcePath, destinationPath, &sourceStat);
    }

    if (showingProgress)
        progressFinish();
}

static void getRealDestinationPath(char *buffer,
                                   const char *destinationPath,
                                   int destinationType,
                                   const char *sourcePath)
{
    size_t nDestinationPath;
    size_t nSourceBase;
    char sourceBase[PATH_BUFMAX];

    nDestinationPath = strlen(destinationPath);
    if (destinationType != TYPE_DIRECTORY) {
        memcpy(buffer, destinationPath, nDestinationPath + 1);
        return;
    }
    baseName(sourceBase, sourcePath);
    nSourceBase = strlen(sourceBase);
    memcpy(buffer, destinationPath, nDestinationPath);
    buffer[nDestinationPath] = DIR_SEPARATOR;
    memcpy(buffer + (nDestinationPath + 1), sourceBase, nSourceBase);
    buffer[nDestinationPath + nSourceBase + 1] = '\0';
}

static bool checkRealDestinationPath(const char *realDestinationPath)
{
    struct stat realDestinationStat;

    memset(&realDestinationStat, 0, sizeof (struct stat));
    if (stat(realDestinationPath, &realDestinationStat) == 0) {
        if (S_ISREG(realDestinationStat.st_mode)) {
            if (!getOverwritePermission(realDestinationPath)) {
                pError(0, "not overwriting destination -- `%s'",
                       realDestinationPath);
                return false;
            }
        }
    }
    return true;
}

static void tryCopy(const char **sourcePath,
                    size_t nSources,
                    const char *destinationPath)
{
    int destinationType;
    size_t x;
    struct stat destinationStat;
    struct stat sourceStat[nSources];
    byte_t sourceSize[nSources];
    int sourceType[nSources];

    memset(&destinationStat, 0, sizeof (struct stat));
    for (x = 0; (x < nSources); ++x) {
        memset(&sourceStat[x], 0, sizeof (struct stat));
        sourceSize[x] = BYTE_C(0);
    }

    destinationType = TYPE_UNKNOWN;
    if (stat(destinationPath, &destinationStat) != 0) {
        if (errno == ENOENT) {
            if (nSources > 1) {
                makePath(destinationPath);
                memset(&destinationStat, 0, sizeof (struct stat));
                if (stat(destinationPath, &destinationStat) != 0)
                    die(errno, "failed to stat destination -- `%s'",
                        destinationPath);
                if (!S_ISDIR(destinationStat.st_mode))
                    die(0, "failed to create destination directory -- `%s'",
                        destinationPath);
                destinationType = TYPE_DIRECTORY;
            } else {
                char destinationParent[PATH_BUFMAX];
                dirName(destinationParent, destinationPath);
                makePath(destinationParent);
                memset(&destinationStat, 0, sizeof (struct stat));
                if ((stat(destinationPath, &destinationStat) != 0)
                    && (errno != ENOENT))
                    die(errno, "failed to stat destination -- `%s'",
                        destinationPath);
                destinationType = TYPE_NON_EXISTING;
            }
        } else
            die(errno, "failed to stat destination -- `%s'", destinationPath);
    }

    if (destinationType == TYPE_UNKNOWN) {
        if (S_ISDIR(destinationStat.st_mode))
            destinationType = TYPE_DIRECTORY;
        else if (S_ISREG(destinationStat.st_mode))
            destinationType = TYPE_FILE;
        else
            destinationType = TYPE_UNSUPPORTED;
    }

    if ((nSources > 1) && (destinationType != TYPE_DIRECTORY))
        die(0, "cannot copy multiple sources into "
               "something that is not a directory -- `%s'",
            destinationPath);

    if ((nSources == 1)
        && (destinationType == TYPE_FILE)
        && !getOverwritePermission(destinationPath))
        die(0, "not overwriting destination -- `%s'", destinationPath);

    for (x = 0; (x < nSources); ++x) {
        sourceType[x] = TYPE_UNKNOWN;
        if (stat(sourcePath[x], &sourceStat[x]) == 0) {
            if (S_ISDIR(sourceStat[x].st_mode))
                sourceType[x] = TYPE_DIRECTORY;
            else if (S_ISREG(sourceStat[x].st_mode))
                sourceType[x] = TYPE_FILE;
            else
                die(0, "unsupported source -- `%s'", sourcePath[x]);
            if (sourceType[x] == TYPE_DIRECTORY)
                directoryContentSize(sourcePath[x], &sourceSize[x]);
            else
                sourceSize[x] = (byte_t)sourceStat[x].st_size;
            totalBytes += sourceSize[x];
        } else
            die(errno, "failed to stat `%s'", sourcePath[x]);
    }

    totalSources = nSources;
    for (x = 0; (x < nSources); ++x) {
        char realDestinationPath[PATH_BUFMAX];
        getRealDestinationPath(realDestinationPath, destinationPath,
                               destinationType, sourcePath[x]);
        if (!checkRealDestinationPath(realDestinationPath))
            break;
        doCopy(sourcePath[x], sourceType[x], sourceSize[x],
               x + 1, realDestinationPath, destinationType);
    }
}

static void reportInit(void)
{
    getTimeOfDay(&startTime);
}

static void reportShow(void)
{
    struct timeval endTime;
    char timeTook[TIME_BUFMAX];
    char totalCopied[SIZE_BUFMAX];

    formatSize(totalCopied, totalBytes, true);
    getTimeOfDay(&endTime);
    formatTime(timeTook, &startTime, &endTime);
    printf("Copied %s in %s\n", totalCopied, timeTook);
}

int main(int argc, char **argv)
{
    int c;
    size_t nFiles;
    char **files;
    const char *destinationPath;
    const char **sourcePath;

    setProgramName(argv[0]);
    for (forever) {
        c = getopt_long(argc, argv, "opPtu:hv", options, NULL);
        if (c == -1)
            break;
        switch (c) {
        case 'o':
            preservingOwnership = true;
            break;
        case 'p':
            preservingPermissions = true;
            break;
        case 'P':
            preservingOwnership = true;
            preservingPermissions = true;
            preservingTimestamp = true;
            break;
        case 't':
            preservingTimestamp = true;
            break;
        case 'u':
            updateInterval = strtod(optarg, (char **)NULL);
            if ((updateInterval <= 0.0)
                || isnan(updateInterval)
                || isinf(updateInterval))
                updateInterval = UPDATE_INTERVAL;
            break;
        case NO_PROGRESS_OPTION:
            showingProgress = false;
            break;
        case NO_REPORT_OPTION:
            showingReport = false;
            break;
        case 'h':
            usage(false);
        case 'v':
            version();
        default:
            usage(true);
        }
    }

    if (argc <= optind) {
        pError(0, "missing operand");
        usage(true);
    }

    nFiles = argc - optind;
    if (nFiles == 1) {
        pError(0, "not enough arguments");
        usage(true);
    }

    files = argv + optind;
    destinationPath = files[--nFiles];
    files[nFiles] = NULL;
    sourcePath = (const char **)files;

    if (showingReport)
        reportInit();

    tryCopy(sourcePath, nFiles, destinationPath);

    if (showingReport)
        reportShow();
    exit(EXIT_SUCCESS);
}

