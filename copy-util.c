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

#include <ctype.h>
#include <math.h>
#include <string.h>
#include <sys/ioctl.h>
#ifndef _WIN32
# include <unistd.h>
#endif
#include <utime.h>
#ifdef _WIN32
# include <windows.h>
#endif

#include "copy-util.h"

#define B_SHORT  "B"
#define KB_SHORT "K"
#define MB_SHORT "M"
#define GB_SHORT "G"
#define TB_SHORT "T"
#define PB_SHORT "P"
#define EB_SHORT "E"

#define B_LONG       "byte"
#define KB_LONG "kilobytes"
#define MB_LONG "megabytes"
#define GB_LONG "gigabytes"
#define TB_LONG "terabytes"
#define PB_LONG "petabytes"
#define EB_LONG  "exabytes"

#define KB_FACTOR                BYTE_C (1000)
#define MB_FACTOR             BYTE_C (1000000)
#define GB_FACTOR          BYTE_C (1000000000)
#define TB_FACTOR       BYTE_C (1000000000000)
#define PB_FACTOR    BYTE_C (1000000000000000)
#define EB_FACTOR BYTE_C (1000000000000000000)

#define SIZE_BYTES_FORMAT   BYTE_M "%c"
#define SIZE_PRECISION_FORMAT "%.1Lf%c"

#define FALLBACK_CONSOLE_WIDTH 40

enum
{
  RESPONSE_UNRECOGNIZED,
  RESPONSE_POSITIVE,
  RESPONSE_NEGATIVE
};

extern const char *program_name;

static const char *positive_response[] =
{
  "y", "yes", "yep", "yeah", "ok",
  "okay", "1", "true", "positive", NULL
};

static const char *negative_response[] =
{
  "n", "no", "nope", "nah",
  "0", "false", "negative", NULL
};

static int
get_response_type (const char *response)
{
  size_t i;

  for (i = 0; positive_response[i]; ++i)
    if (streq (response, positive_response[i], true))
      return RESPONSE_POSITIVE;
  for (i = 0; negative_response[i]; ++i)
    if (streq (response, negative_response[i], true))
      return RESPONSE_NEGATIVE;
  return RESPONSE_UNRECOGNIZED;
}

static void
get_response (char *buffer)
{
  int c;
  size_t p;

  p = 0;
  for (;;)
  {
    c = fgetc (stdin);
    if ((c == EOF) || (c == '\n') || (p == (RESPONSE_BUFMAX - 1)))
      break;
    buffer[p++] = (char) c;
  }
  buffer[p] = '\0';
}

static bool
is_absolute_path (const char *path)
{
  if (path)
  {
    if (is_dir_separator (*path))
      return true;
#ifdef _WIN32
    if (isalpha (path[0]) && (path[1] == ':') && is_dir_separator (path[2]))
      return true;
#endif
  }
  return false;
}

static void
absolute_path (char *buffer, const char *path)
{
  size_t n_path;

  if (path)
  {
    n_path = strlen (path);
    if (n_path >= (PATH_BUFMAX - 1))
      die (0, "preventing buffer overflow");
    if (!is_absolute_path (path))
    {
      size_t n;
      size_t n_cwd;
      char cwd[PATH_BUFMAX];
      if (!getcwd (cwd, PATH_BUFMAX))
        die (errno, "failed to get current working directory");
      n_cwd = strlen (cwd);
      n = n_cwd + n_path + 1;
      if (n >= (PATH_BUFMAX - 1))
        die (0, "preventing buffer overflow");
      memcpy (buffer, cwd, n_cwd);
      buffer[n_cwd] = DIR_SEPARATOR;
      memcpy (buffer + (n_cwd + 1), path, n_path);
      buffer[n] = '\0';
    }
    else
      memcpy (buffer, path, n_path + 1);
    return;
  }
  buffer[0] = '\0';
}

#ifdef DEBUGGING
void
__debug (const char *tag, const char *fmt, ...)
{
  va_list args;

  fputs (program_name, stderr);
  fputc (':', stderr);
  fputs (tag, stderr);

  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);
  fputc ('\n', stderr);
}
#endif

void
x_error (int errnum, const char *fmt, ...)
{
  va_list args;

  fputs (program_name, stderr);
  fputs (": error: ", stderr);

  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);

  if (errnum != 0)
  {
    fputs (": ", stderr);
    fputs (strerror (errnum), stderr);
  }
  fputc ('\n', stderr);
}

FILE *
x_fopen (const char *path, const char *mode)
{
  FILE *fp;

  fp = fopen (path, mode);
  if (!fp)
    x_error (errno, "failed to open file `%s'", path);
  return fp;
}

bool
x_fclose (FILE *fp, const char *path)
{
  if (fclose (fp) != 0)
  {
    x_error (errno, "failed to properly close file `%s'", path);
    return false;
  }
  return true;
}

DIR *
x_opendir (const char *path)
{
  DIR *dp;

  dp = opendir (path);
  if (!dp)
    x_error (errno, "failed to open directory `%s'", path);
  return dp;
}

bool
x_closedir (DIR *dp, const char *path)
{
  if (closedir (dp) != 0)
  {
    x_error (errno, "failed to properly close directory `%s'", path);
    return false;
  }
  return true;
}

struct dirent *
x_readdir (DIR *dp, bool *error, const char *path)
{
  struct dirent *e;

  *error = false;
  e = readdir (dp);
  if (!e && ((errno != 0) && (errno != ENOENT) && (errno != EEXIST)))
  {
    x_error (errno, "failed to read directory `%s'", path);
    *error = true;
  }
  return e;
}

void
x_gettimeofday (struct timeval *tv)
{
  memset (tv, 0, sizeof (struct timeval));
  if (gettimeofday (tv, NULL) != 0)
    die (errno, "failed to get time of day");
}

void
x_utime (const char *path, const struct utimbuf *times)
{
  if (utime (path, times) != 0)
    x_error (errno, "failed to set timestamp for `%s'", path);
}

void
x_chown (const char *path, uid_t uid, gid_t gid)
{
  if (chown (path, uid, gid) != 0)
    x_error (errno, "failed to set ownership for `%s'", path);
}

void
x_chmod (const char *path, mode_t mode)
{
  if (chmod (path, mode) != 0)
    x_error (errno, "failed to set permissions for `%s'", path);
}

bool
streq (const char *s1, const char *s2, bool ignore_case)
{
  size_t n;

  n = strlen (s1);
  if (n != strlen (s2))
    return false;

  if (ignore_case)
  {
    for (; (*s1 && *s2); ++s1, ++s2)
      if (tolower (*s1) != tolower (*s2))
        return false;
  }
  else if (memcmp (s1, s2, n) != 0)
    return false;
  return true;
}

/* Path basename routine from glib-2.0 (but without mallocing anything). */
void
basename (char *buffer, const char *path)
{
  size_t n;
  ssize_t base;
  ssize_t last_non_slash;

  if (path)
  {
    if (!*path)
    {
      buffer[0] = '.';
      buffer[1] = '\0';
      return;
    }
    last_non_slash = strlen (path) - 1;
    while ((last_non_slash >= 0) && is_dir_separator (path[last_non_slash]))
      last_non_slash--;
    if (last_non_slash == -1)
    {
      buffer[0] = DIR_SEPARATOR;
      buffer[1] = '\0';
      return;
    }
#ifdef _WIN32
    if ((last_non_slash == 1) && isalpha (path[0]) && (path[1] == ':'))
    {
      buffer[0] = DIR_SEPARATOR;
      buffer[1] = '\0';
      return;
    }
#endif
    base = last_non_slash;
    while ((base >= 0) && !is_dir_separator (path[base]))
      base--;
#ifdef _WIN32
    if ((base == -1) && isalpha (path[0]) && (path[1] == ':'))
      base = 1;
#endif
    n = last_non_slash - base;
    if (n >= (PATH_BUFMAX - 1))
      n = PATH_BUFMAX - 1;
    memcpy (buffer, path + base + 1, n);
    buffer[n] = '\0';
    return;
  }
  buffer[0] = '\0';
}

/* Path dirname routine from glib-2.0 (but without mallocing anything). */
void
dirname (char *buffer, const char *path)
{
  size_t n;
  char *base;

  if (path)
  {
    base = strrchr (path, DIR_SEPARATOR);
#ifdef _WIN32
    {
      char *p = strrchr (path, '/');
      if (!base || (p && (p > base)))
        base = p;
    }
#endif
    if (!base)
    {
#ifdef _WIN32
      if (isalpha (path[0]) && (path[1] == ':'))
      {
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
    while ((base > path) && is_dir_separator (*base))
      base--;
#ifdef _WIN32
    if ((base == (path + 1)) && isalpha (path[0]) && (path[1] == ':'))
      base++;
    else if (is_dir_separator (path[0]) &&
             is_dir_separator (path[1]) &&
             path[2] &&
             !is_dir_separator (path[2]) &&
             (base >= (path + 2)))
    {
      const char *p = path + 2;
      while (*p && !is_dir_separator (*p))
        p++;
      if (p == (base + 1))
      {
        n = (unsigned int) strlen (path) + 1;
        if (n >= (PATH_BUFMAX - 1))
          n = PATH_BUFMAX - 1;
        strcpy (buffer, path);
        buffer[n - 1] = DIR_SEPARATOR;
        buffer[n] = '\0';
        return;
      }
      if (is_dir_separator (*p))
      {
        p++;
        while (*p && !is_dir_separator (*p))
          p++;
        if (p == (base + 1))
          base++;
      }
    }
#endif
    n = (unsigned int) 1 + base - path;
    if (n >= (PATH_BUFMAX - 1))
      n = PATH_BUFMAX - 1;
    memcpy (buffer, path, n);
    buffer[n] = '\0';
    return;
  }
  buffer[0] = '\0';
}

void
make_path (const char *path)
{
  char c;
  char *p;
  char abs[PATH_BUFMAX];

  absolute_path (abs, path);
  p = abs;

  while (*p)
  {
    p++;
    while (*p && !is_dir_separator (*p))
      p++;
    c = *p;
    *p = '\0';
    if (!make_dir (abs) && (errno != EEXIST))
      die (errno, "failed to create directory `%s'", abs);
    *p = c;
  }
}

bool
get_overwrite_permission (const char *path)
{
  printf ("\nDestination already exists -- `%s'\n", path);
  for (;;)
  {
    fputs ("Overwrite? (data will be lost) [y/n] ", stdout);
    char response[RESPONSE_BUFMAX];
    get_response (response);
    fputc ('\n', stdout);
    switch (get_response_type (response))
    {
      case RESPONSE_POSITIVE:
        return true;
      case RESPONSE_NEGATIVE:
        return false;
      default:
        x_error (0, "unrecognized response -- please try again\n");
        break;
    }
  }
  return false;
}

long
get_milliseconds (const struct timeval *s, const struct timeval *e)
{
  return (((e->tv_sec - s->tv_sec) * MILLISECONDS_PER_SECOND) +
          ((e->tv_usec - s->tv_usec) / MILLISECONDS_PER_SECOND));
}

void
format_time (char *buffer, const struct timeval *s, const struct timeval *e)
{
  double total_seconds;

  total_seconds = (get_milliseconds (s, e) / MILLISECONDS_PER_SECOND);
  if (total_seconds < 1.0)
  {
    snprintf (buffer, TIME_BUFMAX, "%g seconds", total_seconds);
    return;
  }

  int hours = (((int) total_seconds) / SECONDS_PER_HOUR);
  int minutes = (((int) total_seconds) / SECONDS_PER_MINUTE);
  int seconds = (((int) total_seconds) % SECONDS_PER_MINUTE);
  size_t n = 0;

  if (hours > 0)
  {
    snprintf (buffer, TIME_BUFMAX, "%i hour%s",
              hours, (hours == 1) ? "" : "s");
    n = strlen (buffer);
  }

  if (minutes > 0)
  {
    if (hours > 0)
      buffer[n++] = ' ';
    snprintf (buffer + n, TIME_BUFMAX - n, "%i minute%s",
              minutes, (minutes == 1) ? "" : "s");
    n = strlen (buffer);
  }

  if (seconds > 0)
  {
    if ((hours > 0) || (minutes > 0))
      buffer[n++] = ' ';
    snprintf(buffer + n, TIME_BUFMAX - n, "%i second%s",
             seconds, (seconds == 1) ? "" : "s");
  }
}

void
format_size (char *buffer, byte_t bytes, bool long_format)
{
#define __sfmt(__prefix) \
  do \
  { \
    if (long_format) \
      snprintf (buffer, SIZE_BUFMAX, "%.2Lf " __prefix ## _LONG, \
                (BYTE_TO_LDBL (bytes) / \
                 BYTE_TO_LDBL (__prefix ## _FACTOR))); \
    else \
      snprintf (buffer, SIZE_BUFMAX, "%.1Lf" __prefix ## _SHORT, \
                (BYTE_TO_LDBL (bytes) / \
                 BYTE_TO_LDBL (__prefix ## _FACTOR))); \
  } while (0)

  if (bytes < KB_FACTOR)
  {
    if (long_format)
      snprintf (buffer, SIZE_BUFMAX, BYTE_M " " B_LONG "%s",
                bytes, (bytes == 1) ? "" : "s");
    else
      snprintf (buffer, SIZE_BUFMAX, BYTE_M B_SHORT, bytes);
  }
  else if (bytes < MB_FACTOR)
    __sfmt (KB);
  else if (bytes < GB_FACTOR)
    __sfmt (MB);
  else if (bytes < TB_FACTOR)
    __sfmt (GB);
  else if (bytes < PB_FACTOR)
    __sfmt (TB);
  else if (bytes < EB_FACTOR)
    __sfmt (PB);
  else
    __sfmt (EB);

#undef __sfmt
}

void
format_percent (char *buffer, byte_t so_far, byte_t total)
{
  long double x;

  x = (BYTE_TO_LDBL (so_far) / BYTE_TO_LDBL (total));
  if (isnan (x) || isnan (x * 100))
    memcpy (buffer, "0%", 3);
  else
    snprintf (buffer, PERCENT_BUFMAX, "%.0Lf%%", x * 100);
}

int
console_width (void)
{
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO x;

  if (GetConsoleScreenBufferInfo (GetStdHandle (STD_OUTPUT_HANDLE), &x))
    return x.dwSize.X;
#else
  struct winsize x;

  if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &x) != -1)
    return x.ws_col;
#endif
  return FALLBACK_CONSOLE_WIDTH;
}

void
preserve_timestamp (const char *path, time_t atime, time_t mtime)
{
  struct utimbuf timestamp;

  timestamp.actime = atime;
  timestamp.modtime = mtime;
  if (utime (path, &timestamp) != 0)
    x_error (errno, "failed to set timestamp for `%s'", path);
}

