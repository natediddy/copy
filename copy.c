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

#define PROGRAM_NAME    "copy"
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
#define false ((bool) 0)
#define true  ((bool) 1)

typedef uint64_t byte_t;
#define BYTE_C(n)           UINT64_C (n)
#define BYTE_M                "%" PRIu64
#define BYTE2LDBL(n) ((long double) (n))

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

#define die(errnum, ...) \
  do \
  { \
    p_error ((errnum), __VA_ARGS__); \
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

#define progress_interval_has_passed(m) \
  ((m) >= (MILLISECONDS_PER_SECOND * update_interval))

#ifdef _WIN32
# define DIR_SEPARATOR       '\\'
# define is_dir_separator(c) (((c) == DIR_SEPARATOR) || ((c) == '/'))
# define make_dir(path)      (_mkdir (path) == 0)
#else
# define DIR_SEPARATOR        '/'
# define is_dir_separator(c)  ((c) == DIR_SEPARATOR)
# define make_dir(path)       (mkdir (path, S_IRWXU) == 0)
#endif

#define forever ;;

enum
{
  TYPE_UNKNOWN,
  TYPE_NON_EXISTING,
  TYPE_UNSUPPORTED,
  TYPE_FILE,
  TYPE_DIRECTORY
};

/* long options with no corresponding short options */
enum
{
  NO_PROGRESS_OPTION = CHAR_MAX + 1,
  NO_REPORT_OPTION
};

static const char *   program_name;
static struct timeval start_time;
static bool           showing_progress                   =            true;
static bool           showing_report                     =            true;
static bool           preserving_ownership               =           false;
static bool           preserving_permissions             =           false;
static bool           preserving_timestamp               =           false;
static size_t         total_sources                      =               0;
static byte_t         total_bytes                        =      BYTE_C (0);
static byte_t         so_far_bytes                       =      BYTE_C (0);
static double         update_interval                    = UPDATE_INTERVAL;
static char           directory_transfer_source_root         [PATH_BUFMAX];
static char           directory_transfer_destination_root    [PATH_BUFMAX];

struct
{
  size_t n_source;
  byte_t total;
  byte_t so_far;
  struct timeval i_last;
  struct timeval i_current;
  char s_total[SIZE_BUFMAX];
  struct
  {
    int size;
    int stop_pos;
    int so_far_pos;
    long double factor;
  } bar;
} p_data;

static struct option const options[] =
{
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

static void
set_program_name (const char *argv0)
{
  char *p;

  if (argv0 && *argv0)
  {
    p = strrchr (argv0, DIR_SEPARATOR);
#ifdef _WIN32
    if (!p)
      p = strrchr (argv0, '/');
#endif
    if (p && *p && *(p + 1))
      program_name = p + 1;
    else
      program_name = argv0;
    return;
  }
  program_name = PROGRAM_NAME;
}

static void
usage (bool had_error)
{
  fprintf ((!had_error) ? stdout : stderr,
           "Usage: %s [OPTION...] SOURCE... DESTINATION\n",
           program_name);

  if (!had_error)
    fputs ("Options:\n"
           "  -o, --preserve-ownership\n"
           "                        preserve ownership\n"
           "  -p, --preserve-permissions\n"
           "                        preserve permissions\n"
           "  -P, --preserve-all    preserve all timestamp, ownership, and\n"
           "                        permission data\n"
           "  -t, --preserve-timestamp\n"
           "                        preserve timestamps\n"
           "  -u <N>, --update-interval=<N>\n"
           "                        set the progress update interval to\n"
           "                        every <N> seconds (default is 1 second)\n"
           "  --no-progress         do not show any progress during copy\n"
           "                        operations\n"
           "  --no-report           do not show completion report after\n"
           "                        copy operations are finished\n"
           "  -h, --help            print this text and exit\n"
           "  -v, --version         print version information and exit\n",
           stdout);

  if (had_error)
    exit (EXIT_FAILURE);
  exit (EXIT_SUCCESS);
}

static void
version (void)
{
  fputs (PROGRAM_NAME " " PROGRAM_VERSION "\n"
         "Copyright (C) 2014 Nathan Forbes <sforbes41@gmail.com>\n"
         "This is free software; see the source for copying conditions.\n"
         "There is NO warranty; not even for MERCHANTABILITY or FITNESS\n"
         "FOR A PARTICULAR PURPOSE.\n",
         stdout);
  exit (EXIT_SUCCESS);
}

#ifdef DEBUGGING
static void
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

static void
p_error (int errnum, const char *fmt, ...)
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

static bool
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
static void
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
static void
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

static void
get_time_of_day (struct timeval *tv)
{
  memset (tv, 0, sizeof (struct timeval));
  if (gettimeofday (tv, NULL) != 0)
    die (errno, "failed to get time of day");
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

static void
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

static void
set_directory_transfer_source_root (const char *src)
{
  directory_transfer_source_root[0] = '\0';
  memcpy (directory_transfer_source_root, src, strlen (src) + 1);
}

static void
set_directory_transfer_destination_root (const char *dst)
{
  directory_transfer_destination_root[0] = '\0';
  memcpy (directory_transfer_destination_root, dst, strlen (dst) + 1);
}

static void
directory_content_size (const char *path, byte_t *size)
{
  size_t n_child;
  size_t n_path;
  size_t n_name;
  struct stat st;
  DIR *dp;
  struct dirent *ep;

  dp = opendir (path);
  if (!dp)
    die (errno, "failed to open directory -- `%s'", path);

  n_path = strlen (path);
  for (forever)
  {
    ep = readdir (dp);
    if (!ep)
    {
      if ((errno != 0) && (errno != ENOENT) && (errno != EEXIST))
        p_error (errno, "failed to read directory -- `%s'", path);
      break;
    }
    if (streq (ep->d_name, ".", true) || streq (ep->d_name, "..", true))
      continue;
    n_name = strlen (ep->d_name);
    n_child = n_path + n_name + 1;
    char child[n_child + 1];
    memcpy (child, path, n_path);
    child[n_path] = DIR_SEPARATOR;
    memcpy (child + (n_path + 1), ep->d_name, n_name);
    child[n_child] = '\0';
    memset (&st, 0, sizeof (struct stat));
    if (stat (child, &st) == 0)
    {
      if (S_ISDIR (st.st_mode))
        directory_content_size (child, size);
      else
        *size += (byte_t) st.st_size;
    }
  }
  closedir (dp);
}

static bool
get_overwrite_permission (const char *path)
{
  int c;
  size_t p;

  printf ("\nDestination already exists -- `%s'\n", path);
  for (forever)
  {
    fputs ("Overwrite? (data will be lost) [y/n] ", stdout);
    char res[RESPONSE_BUFMAX];
    p = 0;
    for (forever)
    {
      c = fgetc (stdin);
      if ((c == EOF) || (c == '\n') || (p == (RESPONSE_BUFMAX - 1)))
        break;
      res[p++] = (char) c;
    }
    res[p] = '\0';
    fputc ('\n', stdout);
    if (streq(res, "y", true) || streq (res, "yes", true) ||
        streq (res, "yep", true) || streq (res, "yeah", true) ||
        streq (res, "ok", true) || streq (res, "okay", true) ||
        streq (res, "1", false) || streq (res, "true", true))
      return true;
    if (streq (res, "n", true) || streq (res, "no", true) ||
        streq (res, "nope", true) || streq (res, "nah", true) ||
        streq (res, "0", false) || streq (res, "false", true))
      return false;
    p_error (0, "unrecognized response, please try again...\n", res);
  }
  return false;
}

static long
get_milliseconds (const struct timeval *s, const struct timeval *e)
{
  return (((e->tv_sec - s->tv_sec) * MILLISECONDS_PER_SECOND) +
          ((e->tv_usec - s->tv_usec) / MILLISECONDS_PER_SECOND));
}

static void
format_time (char *buffer,
             const struct timeval *start,
             const struct timeval *end)
{
  double total_seconds;

  total_seconds = (get_milliseconds (start, end) / MILLISECONDS_PER_SECOND);
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

static void
format_size (char *buffer, byte_t bytes, bool long_format)
{
#define __sfmt(__prefix) \
  do \
  { \
    if (long_format) \
      snprintf (buffer, SIZE_BUFMAX, "%.2Lf " __prefix ## _LONG, \
                (BYTE2LDBL (bytes) / BYTE2LDBL (__prefix ## _FACTOR))); \
    else \
      snprintf (buffer, SIZE_BUFMAX, "%.1Lf" __prefix ## _SHORT, \
                (BYTE2LDBL (bytes) / BYTE2LDBL (__prefix ## _FACTOR))); \
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

static void
format_percent (char *buffer, byte_t so_far, byte_t total)
{
  long double x;

  x = (BYTE2LDBL (so_far) / BYTE2LDBL (total));
  if (isnan (x) || isnan (x * 100))
    memcpy (buffer, "0%", 3);
  else
    snprintf (buffer, PERCENT_BUFMAX, "%.0Lf%%", x * 100);
}

static int
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

static void
progress_init (byte_t total, size_t n_source)
{
  p_data.n_source = n_source;
  p_data.total = total;
  p_data.so_far = BYTE_C (0);
  p_data.i_last.tv_sec = -1;
  p_data.i_last.tv_usec = -1;
  p_data.i_current.tv_sec = -1;
  p_data.i_current.tv_usec = -1;
  format_size (p_data.s_total, p_data.total, false);
  memset (&p_data.bar, 0, sizeof (p_data.bar));
}

static void
progress_printf (int *remaining_space, const char *fmt, ...)
{
  va_list args;
  char buffer[PROGRESS_OUT_BUFMAX];

  /* We need to write the formatted string to a buffer first
     in order to obtain its length so it can be subtracted from
     the remaining space of the console */
  va_start (args, fmt);
  vsnprintf (buffer, PROGRESS_OUT_BUFMAX, fmt, args);
  va_end (args);
  fputs (buffer, stdout);
  *remaining_space -= (int) strlen (buffer);
}

static void
progress_putchar (int *remaining_space, char c)
{
  fputc (c, stdout);
  (*remaining_space)--;
}

static void
progress_bar_set (int remaining_space, int end_string_sizes)
{
  p_data.bar.stop_pos = end_string_sizes;
  p_data.bar.size = remaining_space - p_data.bar.stop_pos - 2;
  p_data.bar.factor = (BYTE2LDBL (p_data.so_far) / BYTE2LDBL (p_data.total));
  p_data.bar.so_far_pos = roundl (p_data.bar.factor * p_data.bar.size);
}

static void
progress_show (void)
{
  /*
   * Progress format:
   *
   *   For example if copying a single item that is 1GB:
   *       500.0M/1.0G [=======================>                       ] 50%
   *
   *   For example if copying 4 items that are 1GB each:
   *       100% 1.0G/1.0G (item 1/4) [===============>] total: 1.0G/4.0G 25%
   *       100% 1.0G/1.0G (item 2/4) [===============>] total: 2.0G/4.0G 50%
   *       100% 1.0G/1.0G (item 3/4) [===============>] total: 3.0G/4.0G 75%
   *       50% 500.0M/1.0G (item 4/4) [=======>       ] total: 3.5G/4.0G 87%
   *   Each item will get its own line and progress bar.
   */

  int x;
  int remaining_space;
  int end_string_sizes;
  char p_so_far[PERCENT_BUFMAX];
  char s_so_far[SIZE_BUFMAX];
  char p_total_total[PERCENT_BUFMAX];
  char s_total_so_far[SIZE_BUFMAX];
  char s_total_total[SIZE_BUFMAX];

  format_percent (p_so_far, p_data.so_far, p_data.total);
  format_size (s_so_far, p_data.so_far, false);
  remaining_space = console_width ();

  if (total_sources > 1)
  {
    progress_printf (&remaining_space, "%s %s/%s (item %zu/%zu) ",
                     p_so_far, s_so_far, p_data.s_total,
                     p_data.n_source, total_sources);
    format_percent (p_total_total, so_far_bytes, total_bytes);
    format_size (s_total_so_far, so_far_bytes, false);
    format_size (s_total_total, total_bytes, false);
    end_string_sizes = strlen (" total: ") + strlen (s_total_so_far) +
                       strlen (s_total_total) + strlen (p_total_total) + 3;
  }
  else
  {
    progress_printf (&remaining_space, "%s/%s ", s_so_far, p_data.s_total);
    end_string_sizes = strlen (p_so_far) + 2;
  }

  progress_bar_set (remaining_space, end_string_sizes);
  if (p_data.bar.size)
  {
    progress_putchar (&remaining_space, PROGRESS_BAR_START);
    for (x = 0; (x < p_data.bar.so_far_pos); ++x)
      progress_putchar (&remaining_space, PROGRESS_BAR_SO_FAR);
    progress_putchar (&remaining_space, PROGRESS_BAR_HEAD);
    for (; (x < p_data.bar.size); ++x)
      progress_putchar (&remaining_space, PROGRESS_BAR_REMAINING);
    progress_putchar (&remaining_space, PROGRESS_BAR_END);
  }

  if (total_sources > 1)
    progress_printf (&remaining_space, " total: %s/%s %s",
                     s_total_so_far, s_total_total, p_total_total);
  else
    progress_printf (&remaining_space, " %s", p_so_far);

  fputc ('\r', stdout);
  fflush (stdout);
}

static void
progress_finish (void)
{
  progress_show ();
  fputc ('\n', stdout);
}

static long
progress_interval_init(void)
{
  if ((p_data.i_last.tv_sec != -1) && (p_data.i_last.tv_usec != -1))
  {
    get_time_of_day (&p_data.i_current);
    return get_milliseconds (&p_data.i_last, &p_data.i_current);
  }
  return -1;
}

static void
progress_interval_update (long milliseconds)
{
  if (milliseconds != -1)
    memcpy (&p_data.i_last, &p_data.i_current, sizeof (struct timeval));
  else
    get_time_of_day (&p_data.i_last);
}

static void
progress_update (void)
{
  long m;

  m = progress_interval_init ();
  if ((m == -1) || progress_interval_has_passed (m))
  {
    progress_show ();
    progress_interval_update (m);
  }
}

static void
transfer_file (const char *src_path,
               const char *dst_path)
{
  size_t bytes_read;
  FILE *src_fp;
  FILE *dst_fp;

  src_fp = fopen (src_path, "rb");
  if (!src_fp)
    die (errno, "failed to open source -- `%s'", src_path);

  dst_fp = fopen (dst_path, "wb");
  if (!dst_fp)
  {
    p_error (errno, "failed to open destination -- `%s'", dst_path);
    fclose (src_fp);
    exit (EXIT_FAILURE);
  }

  for (forever)
  {
    unsigned char chunk[CHUNK_SIZE];
    bytes_read = fread (chunk, 1, CHUNK_SIZE, src_fp);
    fwrite (chunk, 1, bytes_read, dst_fp);
    p_data.so_far += (byte_t) bytes_read;
    so_far_bytes += (byte_t) bytes_read;
    if (showing_progress)
      progress_update ();
    if (ferror (src_fp) || feof (src_fp))
      break;
  }

  fclose (src_fp);
  fclose (dst_fp);
}

static void
get_directory_transfer_destination_path(char *buffer, const char *src_path)
{
  size_t n_src_path;
  size_t n_src_root;
  size_t n_dst_root;
  size_t n_result;

  n_src_path = strlen (src_path);
  n_src_root = strlen (directory_transfer_source_root);
  n_dst_root = strlen (directory_transfer_destination_root);
  n_result = (n_src_path - n_src_root) + n_dst_root;

  if (n_result >= (PATH_BUFMAX - 1))
    die (0, "preventing buffer overflow");

  memcpy (buffer, directory_transfer_destination_root, n_dst_root);
  memcpy (buffer + n_dst_root,
          src_path + n_src_root,
          n_src_path - n_src_root);
  buffer[n_result] = '\0';
}

static void
preserve_attributes (const char *src_path,
                     const char *dst_path,
                     struct stat *src_st)
{
  if (preserving_timestamp)
  {
    struct utimbuf timestamp;
    timestamp.actime = src_st->st_atime;
    timestamp.modtime = src_st->st_mtime;
    if (utime (dst_path, &timestamp) != 0)
      p_error (errno, "failed to preserve timestamp for `%s'", dst_path);
  }

  if (preserving_ownership &&
      (chown (dst_path, src_st->st_uid, src_st->st_gid) != 0))
    p_error (errno, "failed to preserve ownership for `%s'", dst_path);

  if (preserving_permissions && (chmod (dst_path, src_st->st_mode) != 0))
    p_error (errno, "failed to preserve permissions for `%s'", dst_path);
}

static void
transfer_directory (const char *root_path)
{
  size_t n_child_path;
  size_t n_root_path;
  size_t n_name;
  struct stat child_st;
  DIR *dp;
  struct dirent *ep;

  dp = opendir(root_path);
  if (!dp)
    die(errno, "failed to open directory -- `%s'", root_path);

  n_root_path = strlen (root_path);
  for (forever)
  {
    ep = readdir (dp);
    if (!ep)
    {
      if ((errno != 0) && (errno != ENOENT) && (errno != EEXIST))
        p_error (errno, "failed to read directory -- `%s'", root_path);
      break;
    }
    if (streq (ep->d_name, ".", false) || streq (ep->d_name, "..", false))
      continue;
    n_name = strlen (ep->d_name);
    n_child_path = n_root_path + n_name + 1;
    char child_path[n_child_path + 1];
    memcpy (child_path, root_path, n_root_path);
    child_path[n_root_path] = DIR_SEPARATOR;
    memcpy (child_path + (n_root_path + 1), ep->d_name, n_name);
    child_path[n_child_path] = '\0';
    char dst_path[PATH_BUFMAX];
    get_directory_transfer_destination_path (dst_path, child_path);
    memset (&child_st, 0, sizeof (struct stat));
    if (stat (child_path, &child_st) == 0)
    {
      if (S_ISDIR (child_st.st_mode))
      {
        make_path (dst_path);
        transfer_directory (child_path);
      }
      else if (S_ISREG (child_st.st_mode))
        transfer_file (child_path, dst_path);
      preserve_attributes (child_path, dst_path, &child_st);
    }
  }
  closedir (dp);
}

static void
do_copy (const char *src_path,
         int src_type,
         byte_t src_size,
         size_t src_item_count,
         const char *dst_path,
         int dst_type)
{
  if (showing_progress)
    progress_init (src_size, src_item_count);

  if (src_type == TYPE_DIRECTORY)
  {
    set_directory_transfer_source_root (src_path);
    set_directory_transfer_destination_root (dst_path);
    make_path (directory_transfer_destination_root);
    transfer_directory (directory_transfer_source_root);
  }
  else
    transfer_file (src_path, dst_path);

  if (preserving_ownership || preserving_permissions || preserving_timestamp)
  {
    struct stat src_st;
    memset (&src_st, 0, sizeof (struct stat));
    (void) stat (src_path, &src_st);
    preserve_attributes (src_path, dst_path, &src_st);
  }

  if (showing_progress)
    progress_finish ();
}

static void
get_real_destination_path (char *buffer,
                           const char *dst_path,
                           int dst_type,
                           const char *src_path)
{
  size_t n_dst_path;
  size_t n_src_base;
  char src_base[PATH_BUFMAX];

  n_dst_path = strlen (dst_path);
  if (dst_type != TYPE_DIRECTORY)
  {
    memcpy (buffer, dst_path, n_dst_path + 1);
    return;
  }

  basename (src_base, src_path);
  n_src_base = strlen (src_base);
  memcpy (buffer, dst_path, n_dst_path);
  buffer[n_dst_path] = DIR_SEPARATOR;
  memcpy (buffer + (n_dst_path + 1), src_base, n_src_base);
  buffer[n_dst_path + n_src_base + 1] = '\0';
}

static bool
check_real_destination_path (const char *rpath)
{
  struct stat st;

  memset (&st, 0, sizeof (struct stat));
  if (stat (rpath, &st) == 0)
  {
    if (S_ISREG (st.st_mode))
    {
      if (!get_overwrite_permission (rpath))
      {
        p_error (0, "not overwriting destination -- `%s'", rpath);
        return false;
      }
    }
  }
  return true;
}

static void
try_copy (const char **src_path, size_t n_src, const char *dst_path)
{
  int dst_type;
  size_t x;
  struct stat dst_st;
  struct stat src_st[n_src];
  byte_t src_size[n_src];
  int src_type[n_src];

  memset (&dst_st, 0, sizeof (struct stat));
  for (x = 0; (x < n_src); ++x)
  {
    memset (&src_st[x], 0, sizeof (struct stat));
    src_size[x] = BYTE_C (0);
  }

  dst_type = TYPE_UNKNOWN;
  if (stat (dst_path, &dst_st) != 0)
  {
    if (errno == ENOENT)
    {
      if (n_src > 1)
      {
        make_path (dst_path);
        memset (&dst_st, 0, sizeof (struct stat));
        if (stat (dst_path, &dst_st) != 0)
          die (errno, "failed to stat destination -- `%s'", dst_path);
        if (!S_ISDIR (dst_st.st_mode))
          die(0, "failed to create destination directory -- `%s'", dst_path);
        dst_type = TYPE_DIRECTORY;
      }
      else
      {
        char dst_parent[PATH_BUFMAX];
        dirname (dst_parent, dst_path);
        make_path (dst_parent);
        memset (&dst_st, 0, sizeof (struct stat));
        if ((stat (dst_path, &dst_st) != 0) && (errno != ENOENT))
          die (errno, "failed to stat destination -- `%s'", dst_path);
        dst_type = TYPE_NON_EXISTING;
      }
    }
    else
      die (errno, "failed to stat destination -- `%s'", dst_path);
  }

  if (dst_type == TYPE_UNKNOWN)
  {
    if (S_ISDIR (dst_st.st_mode))
      dst_type = TYPE_DIRECTORY;
    else if (S_ISREG (dst_st.st_mode))
      dst_type = TYPE_FILE;
    else
      dst_type = TYPE_UNSUPPORTED;
  }

  if ((n_src > 1) && (dst_type != TYPE_DIRECTORY))
    die (0, "cannot copy multiple sources into "
            "something that is not a directory -- `%s'", dst_path);

  if ((n_src == 1) &&
      (dst_type == TYPE_FILE) &&
      !get_overwrite_permission (dst_path))
    die (0, "not overwriting destination -- `%s'", dst_path);

  for (x = 0; (x < n_src); ++x)
  {
    src_type[x] = TYPE_UNKNOWN;
    if (stat (src_path[x], &src_st[x]) == 0)
    {
      if (S_ISDIR (src_st[x].st_mode))
        src_type[x] = TYPE_DIRECTORY;
      else if (S_ISREG (src_st[x].st_mode))
        src_type[x] = TYPE_FILE;
      else
        die (0, "unsupported source -- `%s'", src_path[x]);
      if (src_type[x] == TYPE_DIRECTORY)
        directory_content_size (src_path[x], &src_size[x]);
      else
        src_size[x] = (byte_t) src_st[x].st_size;
      total_bytes += src_size[x];
    }
    else
      die (errno, "failed to stat `%s'", src_path[x]);
  }

  total_sources = n_src;
  for (x = 0; (x < n_src); ++x)
  {
    char rpath[PATH_BUFMAX];
    get_real_destination_path (rpath, dst_path, dst_type, src_path[x]);
    if (!check_real_destination_path (rpath))
      break;
    do_copy (src_path[x], src_type[x], src_size[x], x + 1, rpath, dst_type);
  }
}

static void
report_init (void)
{
  get_time_of_day (&start_time);
}

static void
report_show (void)
{
  struct timeval end_time;
  char time_taken[TIME_BUFMAX];
  char total_copied[SIZE_BUFMAX];

  format_size (total_copied, total_bytes, true);
  get_time_of_day (&end_time);
  format_time (time_taken, &start_time, &end_time);
  printf ("Copied %s in %s\n", total_copied, time_taken);
}

int
main (int argc, char **argv)
{
  int c;
  size_t n_files;
  char **files;
  const char *dst_path;
  const char **src_path;

  set_program_name (argv[0]);
  for (forever)
  {
    c = getopt_long (argc, argv, "opPtu:hv", options, NULL);
    if (c == -1)
      break;
    switch (c)
    {
      case 'o':
        preserving_ownership = true;
        break;
      case 'p':
        preserving_permissions = true;
        break;
      case 'P':
        preserving_ownership = true;
        preserving_permissions = true;
        preserving_timestamp = true;
        break;
      case 't':
        preserving_timestamp = true;
        break;
      case 'u':
        update_interval = strtod (optarg, (char **)NULL);
        if ((update_interval < 0.0) ||
            isnan (update_interval) ||
            isinf (update_interval))
        update_interval = UPDATE_INTERVAL;
        break;
      case NO_PROGRESS_OPTION:
        showing_progress = false;
        break;
      case NO_REPORT_OPTION:
        showing_report = false;
        break;
      case 'h':
        usage(false);
      case 'v':
        version();
      default:
        usage(true);
    }
  }

  if (argc <= optind)
  {
    p_error (0, "missing operand");
    usage (true);
  }

  n_files = argc - optind;
  if (n_files == 1)
  {
    p_error (0, "not enough arguments");
    usage (true);
  }

  files = argv + optind;
  dst_path = files[--n_files];
  files[n_files] = NULL;
  src_path = (const char **) files;

  if (showing_report)
    report_init ();

  try_copy (src_path, n_files, dst_path);

  if (showing_report)
    report_show ();
  exit (EXIT_SUCCESS);
}

