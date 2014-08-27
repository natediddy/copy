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
# include <sys/acl.h>
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
#define PROGRAM_VERSION "1.0.0"

#define PERCSTR_BUFMAX         6
#define SIZESTR_BUFMAX        24
#define PATH_BUFMAX         1024
#define PROGRESS_OUT_BUFMAX 1024

#define PROGRESS_BAR_START     '['
#define PROGRESS_BAR_SOFAR     '#'
#define PROGRESS_BAR_REMAINING '-'
#define PROGRESS_BAR_END       ']'

#define CHUNK_SIZE          10000
#define MILLIS_PER_SEC       1000
#define FALLBACK_CONSOLE_WIDTH 40
#define UPDATE_INTERVAL       1.0

typedef unsigned char bool;
#define false ((bool) 0)
#define true  ((bool) 1)

typedef uint64_t byte_t;
#define BYTE_C(n)            UINT64_C(n)
#define BYTE_M                "%" PRIu64
#define BYTE2LDBL(n) ((long double) (n))

#define B_SYMBOL 'B'
#define K_SYMBOL 'K'
#define M_SYMBOL 'M'
#define G_SYMBOL 'G'
#define T_SYMBOL 'T'
#define P_SYMBOL 'P'
#define E_SYMBOL 'E'

#define K_FACTOR                BYTE_C (1000)
#define M_FACTOR             BYTE_C (1000000)
#define G_FACTOR          BYTE_C (1000000000)
#define T_FACTOR       BYTE_C (1000000000000)
#define P_FACTOR    BYTE_C (1000000000000000)
#define E_FACTOR BYTE_C (1000000000000000000)

#define SIZESTR_BYTES_FORMAT   BYTE_M "%c"
#define SIZESTR_PRECISION_FORMAT "%.1Lf%c"

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
    snprintf (__tag, 256, "DEBUG:%s:%s:%i: ", __FILE__, __func__, __LINE__); \
    __debug (__tag, __VA_ARGS__); \
  } while (0)
#else
# define debug(...)
#endif

#define progress_interval_has_passed(m) \
  ((m) >= (MILLIS_PER_SEC * update_interval))

#ifdef _WIN32
# define DIRSEP '\\'
# define x_mkdir(path) (_mkdir(path) == 0)
#else
# define DIRSEP '/'
# define x_mkdir(path) (mkdir (path, S_IRWXU) == 0)
#endif

enum
{
  TYPE_UNKNOWN,
  TYPE_NON_EXISTING,
  TYPE_UNSUPPORTED,
  TYPE_FILE,
  TYPE_DIRECTORY
};

/* long options with no corresponding short option */
enum
{
  NO_PROGRESS_OPTION = CHAR_MAX + 1
};

static const char *program_name                                 ;
static bool        showing_progress            =            true;
static bool        preserving_ownership        =           false;
static bool        preserving_permissions      =           false;
static bool        preserving_timestamp        =           false;
static double      update_interval             = UPDATE_INTERVAL;
static size_t      total_src                   =               0;
static time_t      start_time                  =               0;
static byte_t      total_bytes                 =      BYTE_C (0);
static byte_t      sofar_bytes                 =      BYTE_C (0);
static char        directory_transfer_src_root     [PATH_BUFMAX];
static char        directory_transfer_dst_root     [PATH_BUFMAX];

struct
{
  size_t n_src;
  byte_t total;
  byte_t sofar;
  struct timeval i_last;
  struct timeval i_current;
  char s_total[SIZESTR_BUFMAX];
  struct
  {
    int size;
    int stop_pos;
    int sofar_pos;
    long double factor;
  } bar;
} pdata;

static struct option const options[] =
{
  {"preserve-ownership", no_argument, NULL, 'o'},
  {"preserve-permissions", no_argument, NULL, 'p'},
  {"preserve-all", no_argument, NULL, 'P'},
  {"preserve-timestamp", no_argument, NULL, 't'},
  {"update-interval", required_argument, NULL, 'u'},
  {"no-progress", no_argument, NULL, NO_PROGRESS_OPTION},
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
    p = strrchr (argv0, DIRSEP);
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
usage (bool error)
{
  fprintf ((!error) ? stdout : stderr,
           "Usage: %s [OPTION...] SOURCE... DESTINATION\n",
           program_name);

  if (!error)
    fputs ("Options:\n"
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
           "  -h, --help         print this text and exit\n"
           "  -v, --version      print version information and exit\n",
           stdout);

  if (error)
    exit (EXIT_FAILURE);
  exit (EXIT_SUCCESS);
}

static void
version (void)
{
  fputs (PROGRAM_NAME " " PROGRAM_VERSION "\n"
         "Copyright (C) 2014 Nathan Forbes <sforbes41@gmail.com>\n"
         "This is free software; see the source for copying conditions.\n"
         "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR\n"
         "A PARTICULAR PURPOSE.\n",
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

/* Path basename routine from glib-2.0 (but without mallocing anything). */
static void
x_basename (char *buffer, const char *path)
{
  size_t n;
  ssize_t base;
  ssize_t last_nonslash;

  if (path)
  {
    if (!*path)
    {
      buffer[0] = '.';
      buffer[1] = '\0';
      return;
    }
    last_nonslash = strlen (path) - 1;
    while ((last_nonslash >= 0) && (path[last_nonslash] == DIRSEP))
      last_nonslash--;
    if (last_nonslash == -1)
    {
      buffer[0] = DIRSEP;
      buffer[1] = '\0';
      return;
    }
    base = last_nonslash;
    while ((base >= 0) && (path[base] != DIRSEP))
      base--;
    n = last_nonslash - base;
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
x_dirname (char *buffer, const char *path)
{
  size_t n;
  char *base;

  if (path)
  {
    base = strrchr (path, DIRSEP);
    if (!base)
    {
      memcpy (buffer, ".", 2);
      return;
    }
    while ((base > path) && (*base == DIRSEP))
      base--;
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
x_gettimeofday (struct timeval *tv)
{
  memset (tv, 0, sizeof (struct timeval));
  if (gettimeofday (tv, NULL) != 0)
    die (errno, "failed to get time of day");
}

static void
abs_path (char *buffer, const char *path)
{
  size_t n;
  size_t n_cwd;
  size_t n_path;

  if (path)
  {
    n_path = strlen (path);
    if (n_path >= (PATH_BUFMAX - 1))
      die (0, "preventing buffer overflow");
    if (*path != DIRSEP)
    {
      char cwd[PATH_BUFMAX];
      if (!getcwd (cwd, PATH_BUFMAX))
        die (errno, "failed to get current working directory");
      n_cwd = strlen (cwd);
      n = n_cwd + n_path + 1;
      if (n >= (PATH_BUFMAX - 1))
        die (0, "preventing buffer overflow");
      memcpy (buffer, cwd, n_cwd);
      buffer[n_cwd] = DIRSEP;
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

  abs_path (abs, path);
  p = abs;

  while (*p)
  {
    p++;
    while (*p && (*p != DIRSEP)
#ifdef _WIN32
           && (*p != '/')
#endif
           )
      p++;
    c = *p;
    *p = '\0';
    if (!x_mkdir (abs) && (errno != EEXIST))
    {
      x_error (errno, "failed to create directory `%s'", abs);
      exit (EXIT_FAILURE);
    }
    *p = c;
  }
}

static void
set_directory_transfer_src_root (const char *src)
{
  directory_transfer_src_root[0] = '\0';
  memcpy (directory_transfer_src_root, src, strlen (src) + 1);
}

static void
set_directory_transfer_dst_root (const char *dst)
{
  directory_transfer_dst_root[0] = '\0';
  memcpy (directory_transfer_dst_root, dst, strlen (dst) + 1);
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
  for (;;)
  {
    ep = readdir (dp);
    if (!ep)
    {
      if ((errno != 0) && (errno != ENOENT) && (errno != EEXIST))
        x_error (errno, "failed to read directory -- `%s'", path);
      break;
    }
    if ((strcmp (ep->d_name, ".") == 0) || (strcmp (ep->d_name, "..") == 0))
      continue;
    n_name = strlen (ep->d_name);
    n_child = n_path + n_name + 1;
    char child[n_child + 1];
    memcpy (child, path, n_path);
    child[n_path] = DIRSEP;
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
  for (;;)
  {
    fputs ("Overwrite? (data will be lost) [y/n] ", stdout);
    char res[128];
    p = 0;
    for (;;)
    {
      c = fgetc (stdin);
      if ((c == EOF) || (c == '\n') || (p == 127))
        break;
      res[p++] = (char) c;
    }
    res[p] = '\0';
    fputc ('\n', stdout);
    if ((strcasecmp (res, "y") == 0) || (strcasecmp (res, "yes") == 0) ||
        (strcasecmp (res, "ok") == 0) || (strcasecmp (res, "okay") == 0) ||
        (strcmp (res, "1") == 0) || (strcasecmp (res, "true") == 0))
      return true;
    if ((strcasecmp (res, "n") == 0) || (strcasecmp (res, "no") == 0) ||
        (strcmp (res, "0") == 0) || (strcasecmp (res, "false") == 0))
      return false;
    x_error (0, "unrecognized response, try again...\n", res);
  }
  return false;
}

static void
format_size (char *buffer, byte_t bytes)
{
  if (bytes < K_FACTOR)
    snprintf (buffer, SIZESTR_BUFMAX, SIZESTR_BYTES_FORMAT, bytes, B_SYMBOL);
#define __pfmt(__factor, __symbol) \
  snprintf (buffer, SIZESTR_BUFMAX, SIZESTR_PRECISION_FORMAT, \
            (BYTE2LDBL (bytes) / BYTE2LDBL (__factor)), __symbol)
  else if (bytes < M_FACTOR)
    __pfmt (K_FACTOR, K_SYMBOL);
  else if (bytes < G_FACTOR)
    __pfmt (M_FACTOR, M_SYMBOL);
  else if (bytes < T_FACTOR)
    __pfmt (G_FACTOR, G_SYMBOL);
  else if (bytes < P_FACTOR)
    __pfmt (T_FACTOR, T_SYMBOL);
  else if (bytes < E_FACTOR)
    __pfmt (P_FACTOR, P_SYMBOL);
  else
    __pfmt (E_FACTOR, E_SYMBOL);
#undef __pfmt
}

static void
format_percent (char *buffer, byte_t sofar, byte_t total)
{
  long double x;

  x = (BYTE2LDBL (sofar) / BYTE2LDBL (total));
  if (isnan (x) || isnan (x * 100))
    memcpy (buffer, "0%", 3);
  else
    snprintf (buffer, PERCSTR_BUFMAX, "%.0Lf%%", x * 100);
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

static long
get_milliseconds (const struct timeval *s, const struct timeval *e)
{
  return (((e->tv_sec - s->tv_sec) * MILLIS_PER_SEC) +
          ((e->tv_usec - s->tv_usec) / MILLIS_PER_SEC));
}

static void
progress_init (byte_t total, size_t n_src)
{
  pdata.n_src = n_src;
  pdata.total = total;
  pdata.sofar = BYTE_C (0);
  pdata.i_last.tv_sec = -1;
  pdata.i_last.tv_usec = -1;
  pdata.i_current.tv_sec = -1;
  pdata.i_current.tv_usec = -1;
  format_size (pdata.s_total, pdata.total);
  memset (&pdata.bar, 0, sizeof (pdata.bar));
}

static void
progress_printf (int *remaining_space, const char *fmt, ...)
{
  int n;
  va_list args;
  char buffer[PROGRESS_OUT_BUFMAX];

  /* We need to write the formatted string to a buffer first
     in order to obtain its length so it can be subtracted from
     the remaining space of the console */
  va_start (args, fmt);
  vsnprintf (buffer, PROGRESS_OUT_BUFMAX, fmt, args);
  va_end (args);
  n = strlen (buffer);
  fputs (buffer, stdout);
  *remaining_space -= n;
}

static void
progress_bar_set (int remaining_space, int end_ss)
{
  pdata.bar.stop_pos = end_ss;
  pdata.bar.size = remaining_space - pdata.bar.stop_pos - 2;
  pdata.bar.factor = (BYTE2LDBL (pdata.sofar) / BYTE2LDBL (pdata.total));
  pdata.bar.sofar_pos = roundl (pdata.bar.factor * pdata.bar.size);
}

static void
progress_show (void)
{
  /*
   * Progress format:
   *
   *   For example if copying a single item that is 1GB:
   *     500.0M/1.0G [##########################-------------------------] 50%
   *
   *   For example if copying 3 items that are 1GB each:
   *     50% 500.0M/1.0G (item 1/3) [###--------------] total: 500.0M/3.0G 16%
   *   The remaining 2 items will each get their own line and progress bar.
   */

  int x;
  int rem;
  int end_ss;
  char p_sofar[PERCSTR_BUFMAX];
  char s_sofar[SIZESTR_BUFMAX];
  char p_total_total[PERCSTR_BUFMAX];
  char s_total_sofar[SIZESTR_BUFMAX];
  char s_total_total[SIZESTR_BUFMAX];

  rem = console_width ();
  format_percent (p_sofar, pdata.sofar, pdata.total);
  format_size (s_sofar, pdata.sofar);

  if (total_src > 1)
  {
    progress_printf (&rem, "%s %s/%s (item %zu/%zu) ",
                     p_sofar, s_sofar, pdata.s_total, pdata.n_src, total_src);
    format_percent (p_total_total, sofar_bytes, total_bytes);
    format_size (s_total_sofar, sofar_bytes);
    format_size (s_total_total, total_bytes);
    end_ss = strlen (" total: ") + strlen (s_total_sofar) +
             strlen (s_total_total) + strlen (p_total_total) + 3;
  }
  else
  {
    progress_printf (&rem, "%s/%s ", s_sofar, pdata.s_total);
    end_ss = strlen (p_sofar) + 2;
  }

  progress_bar_set (rem, end_ss);
  if (pdata.bar.size)
  {
    progress_printf (&rem, "%c", PROGRESS_BAR_START);
    for (x = 0; (x < pdata.bar.sofar_pos); ++x)
      progress_printf (&rem, "%c", PROGRESS_BAR_SOFAR);
    for (; (x < pdata.bar.size); ++x)
      progress_printf (&rem, "%c", PROGRESS_BAR_REMAINING);
    progress_printf (&rem, "%c", PROGRESS_BAR_END);
  }

  if (total_src > 1)
    progress_printf (&rem, " total: %s/%s %s",
                     s_total_sofar, s_total_total, p_total_total);
  else
    progress_printf (&rem, " %s", p_sofar);

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
progress_interval_init (void)
{
  if ((pdata.i_last.tv_sec != -1) && (pdata.i_last.tv_usec != -1))
  {
    x_gettimeofday (&pdata.i_current);
    return get_milliseconds (&pdata.i_last, &pdata.i_current);
  }
  return -1;
}

static void
progress_interval_update (long milliseconds)
{
  if (milliseconds != -1)
    memcpy (&pdata.i_last, &pdata.i_current, sizeof (struct timeval));
  else
    x_gettimeofday (&pdata.i_last);
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
transfer_file (const char *src, const char *dst)
{
  size_t n;
  FILE *sp;
  FILE *dp;

  sp = fopen (src, "rb");
  if (!sp)
    die (errno, "failed to open source -- `%s'", src);

  dp = fopen (dst, "wb");
  if (!dp)
  {
    x_error (errno, "failed to open destination -- `%s'", dst);
    fclose (sp);
    exit (EXIT_FAILURE);
  }

  for (;;)
  {
    unsigned char chunk[CHUNK_SIZE];
    n = fread (chunk, 1, CHUNK_SIZE, sp);
    fwrite (chunk, 1, n, dp);
    pdata.sofar += (byte_t) n;
    sofar_bytes += (byte_t) n;
    if (showing_progress)
      progress_update ();
    if (ferror (sp) || feof (sp))
      break;
  }

  fclose (sp);
  fclose (dp);
}

static void
get_directory_transfer_dst_path (char *buffer, const char *spath)
{
  size_t n_spath;
  size_t n_sroot;
  size_t n_droot;
  size_t n_result;

  n_spath = strlen (spath);
  n_sroot = strlen (directory_transfer_src_root);
  n_droot = strlen (directory_transfer_dst_root);
  n_result = (n_spath - n_sroot) + n_droot;

  if (n_result >= (PATH_BUFMAX - 1))
    die (0, "preventing buffer overflow");

  memcpy (buffer, directory_transfer_dst_root, n_droot);
  memcpy (buffer + n_droot, spath + n_sroot, n_spath - n_sroot);
  buffer[n_result] = '\0';
}

#ifndef _WIN32
static void
copy_access_control_lists (const char *spath, const char *dpath, mode_t mode)
{
  int ret;
  acl_t acl;

  acl = acl_get_file (spath, ACL_TYPE_ACCESS);
  if (!acl)
  {
    x_error (errno, "failed to get access control lists for `%s'", spath);
    return;
  }

  ret = acl_set_file (dpath, ACL_TYPE_ACCESS, acl);
  if ((ret != 0) && (chmod (dpath, mode) != 0))
  {
    x_error (errno, "failed to set permissions for `%s'", dpath);
    return;
  }

  acl_free (acl);

  if ((mode & (S_ISUID | S_ISGID | S_ISVTX)) && (chmod (dpath, mode) != 0))
  {
    x_error (errno, "failed to set permissions for `%s'", dpath);
    return;
  }

  if (S_ISDIR (mode))
  {
    acl = acl_get_file (spath, ACL_TYPE_DEFAULT);
    if (!acl)
    {
      x_error (errno, "failed to get access control lists for `%s'", spath);
      return;
    }
    if (acl_set_file (dpath, ACL_TYPE_DEFAULT, acl) != 0)
      x_error (errno, "failed to set access control lists for `%s'", dpath);
    acl_free (acl);
  }
}
#endif

static void
preserve_attributes (const char *spath, const char *dpath, struct stat *st)
{
  if (preserving_timestamp)
  {
    struct utimbuf timestamp;
    timestamp.actime = st->st_atime;
    timestamp.modtime = st->st_mtime;
    if (utime (dpath, &timestamp) != 0)
      x_error (errno, "failed to preserve timestamp for `%s'", dpath);
  }

  if (preserving_ownership && chown (dpath, st->st_uid, st->st_gid) != 0)
    x_error (errno, "failed to preserve ownership for `%s'", dpath);

  if (preserving_permissions)
  {
#ifndef _WIN32
    copy_access_control_lists (spath, dpath, st->st_mode);
#endif
    if (chmod (dpath, st->st_mode) != 0)
      x_error (errno, "failed to preserve permissions for `%s'", dpath);
  }
}

static void
transfer_directory (const char *root)
{
  size_t n_child;
  size_t n_root;
  size_t n_name;
  struct stat st;
  DIR *dp;
  struct dirent *ep;

  dp = opendir (root);
  if (!dp)
    die (errno, "failed to open directory -- `%s'", root);

  n_root = strlen (root);
  for (;;)
  {
    ep = readdir (dp);
    if (!ep)
    {
      if ((errno != 0) && (errno != ENOENT) && (errno != EEXIST))
        x_error (errno, "failed to read directory -- `%s'", root);
      break;
    }
    if ((strcmp (ep->d_name, ".") == 0) || (strcmp (ep->d_name, "..") == 0))
      continue;
    n_name = strlen (ep->d_name);
    n_child = n_root + n_name + 1;
    char child[n_child + 1];
    memcpy (child, root, n_root);
    child[n_root] = DIRSEP;
    memcpy (child + (n_root + 1), ep->d_name, n_name);
    child[n_child] = '\0';
    memset (&st, 0, sizeof (struct stat));
    char dst_path[PATH_BUFMAX];
    get_directory_transfer_dst_path (dst_path, child);
    if (stat (child, &st) == 0)
    {
      if (S_ISDIR (st.st_mode))
      {
        make_path (dst_path);
        transfer_directory (child);
      }
      else if (S_ISREG (st.st_mode))
        transfer_file (child, dst_path);
      preserve_attributes (child, dst_path, &st);
    }
  }
  closedir (dp);
}

static void
do_copy (const char *src,
         int s_type,
         byte_t s_size,
         size_t s_item,
         const char *dst,
         int d_type)
{
  if (showing_progress)
    progress_init (s_size, s_item);

  if (s_type == TYPE_DIRECTORY)
  {
    set_directory_transfer_src_root (src);
    set_directory_transfer_dst_root (dst);
    make_path (directory_transfer_dst_root);
    transfer_directory (directory_transfer_src_root);
  }
  else
    transfer_file (src, dst);

  if (preserving_ownership || preserving_permissions || preserving_timestamp)
  {
    struct stat st;
    memset (&st, 0, sizeof (struct stat));
    (void) stat (src, &st);
    preserve_attributes (src, dst, &st);
  }

  if (showing_progress)
    progress_finish ();
}

static void
real_dst_path (char *buffer, const char *dst, int d_type, const char *src)
{
  size_t n_dst;
  size_t n_sbase;
  char sbase[PATH_BUFMAX];

  n_dst = strlen (dst);
  if (d_type != TYPE_DIRECTORY)
  {
    memcpy (buffer, dst, n_dst + 1);
    return;
  }
  x_basename (sbase, src);
  n_sbase = strlen (sbase);
  memcpy (buffer, dst, n_dst);
  buffer[n_dst] = DIRSEP;
  memcpy (buffer + (n_dst + 1), sbase, n_sbase);
  buffer[n_dst + n_sbase + 1] = '\0';
}

static bool
check_real_dst (const char *real_dst)
{
  struct stat st;

  memset (&st, 0, sizeof (struct stat));
  if (stat (real_dst, &st) == 0)
  {
    if (S_ISREG (st.st_mode))
    {
      if (!get_overwrite_permission (real_dst))
      {
        x_error (0, "not overwriting destination -- `%s'", real_dst);
        return false;
      }
    }
  }
  return true;
}

static void
try_copy (const char **src, size_t n_src, const char *dst)
{
  bool overwriting;
  int type_dst;
  size_t x;
  struct stat st_dst;
  struct stat st_src[n_src];
  byte_t size_src[n_src];
  int type_src[n_src];

  memset (&st_dst, 0, sizeof (struct stat));;
  for (x = 0; (x < n_src); ++x)
  {
    memset (&st_src[x], 0, sizeof (struct stat));
    size_src[x] = BYTE_C (0);
  }

  type_dst = TYPE_UNKNOWN;
  if (stat (dst, &st_dst) != 0)
  {
    if (errno == ENOENT)
    {
      if (n_src > 1)
      {
        make_path (dst);
        memset (&st_dst, 0, sizeof (struct stat));
        if (stat (dst, &st_dst) != 0)
          die (errno, "failed to stat destination -- `%s'", dst);
        if (!S_ISDIR (st_dst.st_mode))
          die (0, "failed to create destination directory -- `%s'", dst);
        type_dst = TYPE_DIRECTORY;
      }
      else
      {
        char parent[PATH_BUFMAX];
        x_dirname (parent, dst);
        make_path (parent);
        memset (&st_dst, 0, sizeof (struct stat));
        if ((stat (dst, &st_dst) != 0) && (errno != ENOENT))
          die (errno, "failed to stat destination -- `%s'", dst);
        type_dst = TYPE_NON_EXISTING;
      }
    }
    else
      die (errno, "failed to stat destination -- `%s'", dst);
  }

  if (type_dst == TYPE_UNKNOWN)
  {
    if (S_ISDIR (st_dst.st_mode))
      type_dst = TYPE_DIRECTORY;
    else if (S_ISREG (st_dst.st_mode))
      type_dst = TYPE_FILE;
    else
      type_dst = TYPE_UNSUPPORTED;
  }

  if ((n_src > 1) && (type_dst != TYPE_DIRECTORY))
    die (0, "cannot copy multiple sources into "
            "something that is not a directory -- `%s'", dst);

  overwriting = false;
  if ((n_src == 1) && (type_dst == TYPE_FILE))
  {
    overwriting = get_overwrite_permission (dst);
    if (!overwriting)
      die (0, "not overwriting destination -- `%s'", dst);
  }

  for (x = 0; (x < n_src); ++x)
  {
    type_src[x] = TYPE_UNKNOWN;
    if (stat (src[x], &st_src[x]) != 0)
      die (errno, "failed to stat source -- `%s'", src[x]);
    if (S_ISDIR (st_src[x].st_mode))
      type_src[x] = TYPE_DIRECTORY;
    else if (S_ISREG (st_src[x].st_mode))
      type_src[x] = TYPE_FILE;
    else
      type_src[x] = TYPE_UNSUPPORTED;
    if (type_src[x] == TYPE_UNSUPPORTED)
      die (0, "source is neither a directory nor a file -- `%s'", src[x]);
    if (type_src[x] == TYPE_DIRECTORY)
      directory_content_size (src[x], &size_src[x]);
    else
      size_src[x] = (byte_t) st_src[x].st_size;
    total_bytes += size_src[x];
  }

  total_src = n_src;
  for (x = 0; (x < n_src); ++x)
  {
    char real_dst[PATH_BUFMAX];
    real_dst_path (real_dst, dst, type_dst, src[x]);
    if (!check_real_dst (real_dst))
      break;
    do_copy (src[x], type_src[x], size_src[x], x + 1, real_dst, type_dst);
  }
}

int
main (int argc, char **argv)
{
  int c;
  size_t n_files;
  char **files;
  const char *dst;
  const char **src;

  set_program_name (argv[0]);
  for (;;)
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
        update_interval = strtod (optarg, (char **) NULL);
        if (isnan (update_interval) || isinf (update_interval))
          update_interval = UPDATE_INTERVAL;
        break;
      case NO_PROGRESS_OPTION:
        showing_progress = false;
        break;
      case 'h':
        usage (false);
      case 'v':
        version ();
      default:
        usage (true);
    }
  }

  if (argc <= optind)
  {
    x_error (0, "missing operand");
    usage (true);
  }

  n_files = argc - optind;
  if (n_files == 1)
  {
    x_error (0, "not enough arguments");
    usage (true);
  }

  files = argv + optind;
  dst = files[--n_files];
  files[n_files] = NULL;
  src = (const char **) files;

  try_copy (src, n_files, dst);
  exit (EXIT_SUCCESS);
}

