/*
 * copy - Copy files and directories
 *
 * Copyright (C) 2014 Nathan Forbes
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

#ifndef __UTILS_H__
#define __UTILS_H__

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#define die(errnum, ...) \
  do \
  { \
    x_error ((errnum), __VA_ARGS__); \
    exit (EXIT_FAILURE); \
  } while (0)

#ifdef DEBUGGING
# define debug(...) \
  do \
  { \
    char __tag[256]; \
    snprintf (__tag, 256, "DEBUG:%s:%s:%i: ", \
              __FILE__, __func__, __LINE__); \
    __debug (__tag, __VA_ARGS__); \
  } while (0)
#else
# define debug(...)
#endif

#define PERCENT_BUFMAX     6
#define SIZE_BUFMAX       64
#define TIME_BUFMAX      128
#define RESPONSE_BUFMAX  128
#define PATH_BUFMAX     1024

#define MILLISECONDS_PER_SECOND 1000
#define SECONDS_PER_HOUR        3600
#define SECONDS_PER_MINUTE        60

#ifdef _WIN32
# define DIR_SEPARATOR       '\\'
# define is_dir_separator(c) (((c) == DIR_SEPARATOR) || ((c) == '/'))
# define make_dir(path)      (_mkdir (path) == 0)
#else
# define DIR_SEPARATOR        '/'
# define is_dir_separator(c)  ((c) == DIR_SEPARATOR)
# define make_dir(path)       (mkdir (path, S_IRWXU) == 0)
#endif

typedef unsigned char bool;
#define false ((bool) 0)
#define true  ((bool) 1)

typedef uint64_t byte_t;
#define BYTE_C(n)       UINT64_C (n)
#define BYTE_M          "%" PRIu64
#define BYTE_TO_LDBL(n) ((long double) (n))

#ifdef DEBUGGING
void __debug (const char *tag, const char *fmt, ...);
#endif
void x_error (int errnum, const char *fmt, ...);
FILE *x_fopen (const char *path, const char *mode);
bool x_fclose (FILE *fp, const char *path);
DIR *x_opendir (const char *path);
bool x_closedir (DIR *dp, const char *path);
struct dirent *x_readdir (DIR *dp, bool *error, const char *path);
void x_gettimeofday (struct timeval *tv);
void x_chown (const char *path, uid_t uid, gid_t gid);
void x_chmod (const char *path, mode_t mode);
bool streq (const char *s1, const char *s2, bool ignore_case);
void basename (char *buffer, const char *path);
void dirname (char *buffer, const char *path);
void make_path (const char *path);
bool get_overwrite_permission (const char *path);
long get_milliseconds (const struct timeval *s, const struct timeval *e);
void format_time (char *buffer,
                  const struct timeval *s,
                  const struct timeval *e);
void format_size (char *buffer, byte_t bytes, bool long_format);
void format_percent (char *buffer, byte_t so_far, byte_t total);
int console_width (void);
void preserve_timestamp (const char *path, time_t atime, time_t mtime);

#endif /* __UTILS_H__ */

