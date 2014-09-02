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

#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include "copy-md5.h"
#include "copy-util.h"

#define PROGRAM_NAME    "copy"
#define PROGRAM_VERSION "1.1.1"

#define PROGRESS_OUT_BUFMAX 1024

#define PROGRESS_BAR_START     '['
#define PROGRESS_BAR_SO_FAR    '='
#define PROGRESS_BAR_HEAD      '>'
#define PROGRESS_BAR_REMAINING ' '
#define PROGRESS_BAR_END       ']'

#define CHUNK_SIZE 10000

#define UPDATE_INTERVAL 0.5

#define progress_interval_has_passed(m) \
  ((m) >= (MILLISECONDS_PER_SECOND * update_interval))

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

const char *program_name;

static bool           showing_progress                   =            true;
static bool           showing_report                     =            true;
static bool           preserving_ownership               =           false;
static bool           preserving_permissions             =           false;
static bool           preserving_timestamp               =           false;
static bool           verifying_checksums                =           false;
static size_t         total_sources                      =               0;
static byte_t         total_bytes                        =      BYTE_C (0);
static byte_t         so_far_bytes                       =      BYTE_C (0);
static double         update_interval                    = UPDATE_INTERVAL;
static struct timeval start_time                                          ;
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
  {"verify", no_argument, NULL, 'V'},
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
           "                      Preserve ownership.\n"
           "  -p, --preserve-permissions\n"
           "                      Preserve permissions.\n"
           "  -P, --preserve-all  Preserve all timestamp, ownership, and\n"
           "                      permission data.\n"
           "  -t, --preserve-timestamp\n"
           "                      Preserve timestamps.\n"
           "  -u <N>, --update-interval=<N>\n"
           "                      Set the progress update interval to every\n"
           "                      <N> seconds. The default is 0.5 seconds.\n"
           "  -V, --verify        Perform a MD5 checksum verification on\n"
           "                      DESTINATION files to ensure they match up\n"
           "                      with their corresponding SOURCE file.\n"
           "                      Note that this will take quite a bit more\n"
           "                      time to complete.\n"
           "  --no-progress       Do not show any progress during copy\n"
           "                      operations.\n"
           "  --no-report         Do not show completion report after\n"
           "                      copy operations are finished.\n"
           "  -h, --help          Print this text and exit.\n"
           "  -v, --version       Print version information and exit.\n",
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
  bool err;
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
    ep = x_readdir (dp, &err, path);
    if (!ep)
    {
      if (err)
      {
        x_closedir (dp, path);
        exit (EXIT_FAILURE);
      }
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
  x_closedir (dp, path);
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
  p_data.bar.factor =
    (BYTE_TO_LDBL (p_data.so_far) / BYTE_TO_LDBL (p_data.total));
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
progress_interval_init (void)
{
  if ((p_data.i_last.tv_sec != -1) && (p_data.i_last.tv_usec != -1))
  {
    x_gettimeofday (&p_data.i_current);
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
    x_gettimeofday (&p_data.i_last);
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

  src_fp = x_fopen (src_path, "rb");
  if (!src_fp)
    exit (EXIT_FAILURE);

  dst_fp = x_fopen (dst_path, "wb");
  if (!dst_fp)
  {
    x_fclose (src_fp, src_path);
    exit (EXIT_FAILURE);
  }

  for (;;)
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

  x_fclose (src_fp, src_path);
  x_fclose (dst_fp, dst_path);
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
    preserve_timestamp (dst_path, src_st->st_atime, src_st->st_mtime);

  if (preserving_ownership)
    x_chown (dst_path, src_st->st_uid, src_st->st_gid);

  if (preserving_permissions)
    x_chmod (dst_path, src_st->st_mode);
}

static void
transfer_directory (const char *root_path)
{
  bool err;
  size_t n_child_path;
  size_t n_root_path;
  size_t n_name;
  struct stat child_st;
  DIR *dp;
  struct dirent *ep;

  dp = x_opendir (root_path);
  if (!dp)
    exit (EXIT_FAILURE);

  n_root_path = strlen (root_path);
  for (;;)
  {
    ep = x_readdir (dp, &err, root_path);
    if (!ep)
    {
      if (err)
      {
        x_closedir (dp, root_path);
        exit (EXIT_FAILURE);
      }
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
  x_closedir (dp, root_path);
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
        x_error (0, "not overwriting destination -- `%s'", rpath);
        return false;
      }
    }
  }
  return true;
}

static void
verify_checksums (const char *src_path, const char *dst_path)
{
  char src_sum[MD5_BUFMAX];
  char dst_sum[MD5_BUFMAX];

  fputs ("Verifying MD5 checksums... ", stdout);
  get_md5_checksum (src_sum, src_path);
  get_md5_checksum (dst_sum, dst_path);
  if (!streq (src_sum, dst_sum, false))
  {
    fputs ("FAILED\n", stdout);
    fprintf (stderr,
             "  Source:\n"
             "    %s\n"
             "  Destination (CORRUPT):\n"
             "    %s\n",
             src_path, dst_path);
  }
  else
    fputs ("PASSED\n", stdout);
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
    if (verifying_checksums)
      verify_checksums (src_path[x], rpath);
  }
}

static void
report_init (void)
{
  x_gettimeofday (&start_time);
}

static void
report_show (void)
{
  struct timeval end_time;
  char time_taken[TIME_BUFMAX];
  char total_copied[SIZE_BUFMAX];

  format_size (total_copied, total_bytes, true);
  x_gettimeofday (&end_time);
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
  for (;;)
  {
    c = getopt_long (argc, argv, "opPtu:Vhv", options, NULL);
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
      case 'V':
        verifying_checksums = true;
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

