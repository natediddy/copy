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

#ifdef HAVE_CONFIG_H
# include "copy-config.h"
#endif

#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#ifdef ENABLE_SOUND
# include "SDL/SDL.h"
# include "SDL/SDL_sound.h"
#endif

#include "copy-checksum.h"
#include "copy-progress.h"
#include "copy-utils.h"

#define CHUNK_SIZE 4000

#ifdef ENABLE_SOUND
# define SOUND_PATH SOUNDSDIR DIR_SEPARATOR_S SOUNDFILE
#endif

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
#ifdef ENABLE_SOUND
  , NO_SOUND_OPTION
#endif
};

#ifdef ENABLE_SOUND
struct sound_data
{
  Uint32 decoded_bytes;
  SDL_AudioSpec device_format;
  Uint8 *decoded_ptr;
  Sound_Sample *sample;
};
#endif

/* global variables */
const char *          program_name;
size_t                total_sources          =                        0;
byte_t                total_bytes            =               BYTE_C (0);
byte_t                so_far_bytes           =               BYTE_C (0);
double                update_interval        = PROGRESS_UPDATE_INTERVAL;

/* local variables */
#ifdef ENABLE_SOUND
static bool           playing_sound          =                     true;
static volatile int   sound_done             =                        0;
#endif
static bool           showing_progress       =                     true;
static bool           showing_report         =                     true;
static bool           preserving_ownership   =                    false;
static bool           preserving_permissions =                    false;
static bool           preserving_timestamp   =                    false;
static bool           verifying_checksums    =                    false;
static size_t         chunk_size             =               CHUNK_SIZE;
static void *         chunk                  =                     NULL;
static struct timeval start_time;
static char           directory_transfer_source_root[PATH_BUFMAX];
static char           directory_transfer_destination_root[PATH_BUFMAX];

static struct option const options[] =
{
  {"chunk-size", required_argument, NULL, 'c'},
  {"preserve-ownership", no_argument, NULL, 'o'},
  {"preserve-permissions", no_argument, NULL, 'p'},
  {"preserve-all", no_argument, NULL, 'P'},
  {"preserve-timestamp", no_argument, NULL, 't'},
  {"update-interval", required_argument, NULL, 'u'},
  {"verify", no_argument, NULL, 'V'},
  {"no-progress", no_argument, NULL, NO_PROGRESS_OPTION},
  {"no-report", no_argument, NULL, NO_REPORT_OPTION},
#ifdef ENABLE_SOUND
  {"no-sound", no_argument, NULL, NO_SOUND_OPTION},
#endif
  {"help", no_argument, NULL, 'h'},
  {"version", no_argument, NULL, 'v'},
  {NULL, 0, NULL, 0}
};

static const struct
{
  char short_opt;
  const char *long_opt;
  const char *argument_name;
  const char *description;
}
help_display_opts[] =
{
  {
    'c', "chunk-size", "SIZE",
    "Set the size of the individual chunks of data that will be read and "
    "written during copy operations to SIZE bytes. The default for this "
    "value is 4000 bytes (4kB)."
  },
  {
    'o', "preserve-ownership", NULL, "Preserve ownership."
  },
  {
    'p', "preserve-permissions", NULL, "Preserve permissions."
  },
  {
    'P', "preserve-all", NULL,
    "Preserve all timestamp, ownership, and permission data."
  },
  {
    't', "preserve-timestamp", NULL, "Preserve timestamps."
  },
  {
    'u', "update-interval", "INTERVAL",
    "Set the progress update interval to every INTERVAL seconds. The "
    "default for this value is 0.5 seconds."
  },
  {
    'V', "verify", NULL,
    "Perform an MD5 checksum verification after all copy operations are "
    "finished to ensure integrity of the files. Note that using this option "
    "may take considerably more time to complete."
  },
  {
    0, "no-progress", NULL,
    "Do not show any progress updates during copy operations."
  },
  {
    0, "no-report", NULL,
    "Do not show completion report after all copy operations are "
    "finished."
  },
#ifdef ENABLE_SOUND
  {
    0, "no-sound", NULL,
    "Do not play notification sound when all copy operations are "
    "finished."
  },
#endif
  {
    'h', "help", NULL, "Print this message and exit."
  },
  {
    'v', "version", NULL, "Print version information and exit."
  },
  {
    0, NULL, NULL, NULL
  }
};


static void
set_program_name (const char *argv0)
{
  char *p;

  if (argv0 && *argv0)
  {
    p = strrchr (argv0, DIR_SEPARATOR_C);
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
  program_name = PACKAGE_NAME;
}

static void
show_help_options (void)
{
  int i;
  int n;
  int s;
  int width;
  int indent_size;
  int stop_point;
  size_t pos;
  char *w;
  const char *p;

  width = console_width ();
  indent_size = width * 0.05;
  stop_point = width - indent_size;

#define __out_c(__c) \
  do \
  { \
    fputc (__c, stdout); \
    ++n; \
  } while (0)

#define __out_w(__w) \
  do \
  { \
    for (w = ((char *) __w); *w; ++w) \
      __out_c (*w); \
  } while (0)

#define __indent(__n) \
  do \
  { \
    n = 0; \
    s = indent_size * __n; \
    for (i = 0; (i < s); ++i) \
      __out_c (' '); \
  } while (0)

  fputs ("Options:\n", stdout);
  for (pos = 0;; ++pos)
  {
    if (!help_display_opts[pos].short_opt &&
        !help_display_opts[pos].long_opt &&
        !help_display_opts[pos].argument_name &&
        !help_display_opts[pos].description)
      break;
    __indent (1);
    if (help_display_opts[pos].short_opt)
    {
      __out_c ('-');
      __out_c (help_display_opts[pos].short_opt);
      if (help_display_opts[pos].argument_name)
      {
        __out_c (' ');
        __out_w (help_display_opts[pos].argument_name);
      }
      __out_w (", ");
    }
    if (help_display_opts[pos].long_opt)
    {
      __out_w ("--");
      __out_w (help_display_opts[pos].long_opt);
      if (help_display_opts[pos].argument_name)
      {
        __out_c ('=');
        __out_w (help_display_opts[pos].argument_name);
      }
    }
    fputc('\n', stdout);
    if (help_display_opts[pos].description)
    {
      __indent (4);
      for (p = help_display_opts[pos].description; *p;)
      {
        while (isspace (*p))
          p++;
        char word[256];
        for (w = word; (*p && !isspace (*p)); ++p, ++w)
          *w = *p;
        *w = '\0';
        if ((n + strlen (word)) >= stop_point)
        {
          fputc ('\n', stdout);
          __indent (4);
        }
        __out_w (word);
        __out_c (*p);
      }
      fputc ('\n', stdout);
    }
  }
#undef __out_c
#undef __out_w
#undef __indent
}

static void
usage (bool had_error)
{
  fprintf ((!had_error) ? stdout : stderr,
           "Usage: %s [OPTION...] SOURCE... DESTINATION\n",
           program_name);
  if (!had_error)
  {
    show_help_options ();
    exit (EXIT_SUCCESS);
  }
  exit (EXIT_FAILURE);
}

static void
version (void)
{
  fputs (PACKAGE_NAME " " PACKAGE_VERSION "\n"
         "Copyright (C) 2014 Nathan Forbes <" PACKAGE_BUGREPORT ">\n"
         "This is free software; see the source for copying conditions.\n"
         "There is NO warranty; not even for MERCHANTABILITY or FITNESS\n"
         "FOR A PARTICULAR PURPOSE.\n",
         stdout);
  exit (EXIT_SUCCESS);
}

#ifdef ENABLE_SOUND
static void
sound_callback (void *data, Uint8 *stream, int len)
{
  int size;
  int written;
  struct sound_data *p;

  size = 0;
  p = (struct sound_data *) data;

  while (size < len)
  {
    if (p->decoded_bytes == 0)
    {
      if (((p->sample->flags & SOUND_SAMPLEFLAG_ERROR) == 0) &&
          ((p->sample->flags & SOUND_SAMPLEFLAG_EOF) == 0))
      {
        p->decoded_bytes = Sound_Decode (p->sample);
        p->decoded_ptr = p->sample->buffer;
      }
      if (p->decoded_bytes == 0)
      {
        memset (stream + size, 0, len - size);
        sound_done = 1;
        return;
      }
    }
    written = len - size;
    if (written > p->decoded_bytes)
      written = p->decoded_bytes;
    if (written > 0)
    {
      memcpy (stream + size, (Uint8 *) p->decoded_ptr, written);
      size += written;
      p->decoded_ptr += written;
      p->decoded_bytes -= written;
    }
  }
}

static void
get_sound_file_path (char *buffer)
{
  struct stat s;

  *buffer = '\0';
  memset (&s, 0, sizeof (struct stat));

  if ((stat (SOUNDFILE, &s) == 0) && S_ISREG (s.st_mode))
    memcpy (buffer, SOUNDFILE, strlen (SOUNDFILE) + 1);
  else
  {
    memset (&s, 0, sizeof (struct stat));
    if ((stat (SOUND_PATH, &s) == 0) && S_ISREG (s.st_mode))
      memcpy (buffer, SOUND_PATH, strlen (SOUND_PATH) + 1);
  }
}

static void
play_sound (void)
{
  struct sound_data data;
  char path[PATH_BUFMAX];

  get_sound_file_path (path);
  if (!*path)
    return;

  memset (&data, 0, sizeof (struct sound_data));
  data.sample = Sound_NewSampleFromFile (path, NULL, 65536);

  if (!data.sample)
    return;

  data.device_format.freq = data.sample->actual.rate;
  data.device_format.format = data.sample->actual.format;
  data.device_format.channels = data.sample->actual.channels;
  data.device_format.samples = 4096;
  data.device_format.callback = sound_callback;
  data.device_format.userdata = &data;

  if (SDL_OpenAudio (&data.device_format, NULL) < 0)
  {
    Sound_FreeSample (data.sample);
    return;
  }

  SDL_PauseAudio (0);

  sound_done = 0;
  while (!sound_done)
    SDL_Delay (10);

  SDL_PauseAudio (1);
  SDL_Delay (2 * 1000 * data.device_format.samples / data.device_format.freq);
  Sound_FreeSample (data.sample);
  SDL_CloseAudio ();
}
#endif

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
    child[n_path] = DIR_SEPARATOR_C;
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

  memset (chunk, 0, sizeof (chunk));
  for (;;)
  {
    bytes_read = fread (chunk, 1, chunk_size, src_fp);
    fwrite (chunk, 1, bytes_read, dst_fp);
    if (showing_progress)
      progress_update (bytes_read);
    if (ferror (src_fp) || feof (src_fp))
      break;
  }

  x_fclose (src_fp, src_path);
  x_fclose (dst_fp, dst_path);
}

static void
get_directory_transfer_destination_path (char *buffer, const char *src_path)
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
    child_path[n_root_path] = DIR_SEPARATOR_C;
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
  buffer[n_dst_path] = DIR_SEPARATOR_C;
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
  int x;
  char src_sum[CHECKSUM_BUFMAX];
  char dst_sum[CHECKSUM_BUFMAX];

  for (x = console_width (); (x > 0); --x)
    fputc ('-', stdout);
  fputc ('\n', stdout);
  fputs ("Verifying MD5 checksums... ", stdout);

  get_checksum (src_sum, src_path);
  get_checksum (dst_sum, dst_path);
  if (!streq (src_sum, dst_sum, false))
  {
    fputs ("FAILED\n", stdout);
    fprintf (stderr,
             "  Source:\n"
             "    %s\n"
             "    %s\n"
             "  Destination (CORRUPT):\n"
             "    %s\n"
             "    %s\n",
             src_path, src_sum, dst_path, dst_sum);
  }
  else
  {
    fputs ("PASSED\n", stdout);
    printf ("  Source:\n"
            "    %s\n"
            "    %s\n"
            "  Destination:\n"
            "    %s\n"
            "    %s\n",
            src_path, src_sum, dst_path, dst_sum);
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

static void
try_copy (const char **src_path, size_t n_src, const char *dst_path)
{
  int dst_type;
  size_t x;
  struct stat dst_st;
  struct stat src_st[n_src];
  byte_t src_size[n_src];
  int src_type[n_src];
#ifdef ENABLE_SOUND
  char *error_msg;
#endif

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

  if (showing_report)
    report_init ();

  chunk = malloc (chunk_size);
  if (!chunk)
    die (errno, "failed to initialize data chunk for transfers");

  for (x = 0; (x < total_sources); ++x)
  {
    char rpath[PATH_BUFMAX];
    get_real_destination_path (rpath, dst_path, dst_type, src_path[x]);
    if (!check_real_destination_path (rpath))
      break;
    do_copy (src_path[x], src_type[x], src_size[x], x + 1, rpath, dst_type);
  }

  if (showing_report)
    report_show ();

#ifdef ENABLE_SOUND
  if (playing_sound)
  {
    if (!Sound_Init ())
    {
      error_msg = SDL_GetError ();
      if (error_msg && *error_msg)
        x_error (0, "error initializing SDL: %s", error_msg);
      error_msg = (char *) Sound_GetError ();
      if (error_msg && *error_msg)
        x_error (0, "error initializing SDL_sound subsystem: %s", error_msg);
    }
    else
    {
      play_sound ();
      Sound_Quit ();
    }
    SDL_Quit ();
  }
#endif

  if (verifying_checksums)
  {
    for (x = 0; (x < total_sources); ++x)
    {
      char rpath[PATH_BUFMAX];
      get_real_destination_path (rpath, dst_path, dst_type, src_path[x]);
      verify_checksums (src_path[x], rpath);
    }
  }
}

static void
exit_cleanup (void)
{
  if (chunk)
    free (chunk);
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
  atexit (exit_cleanup);

  for (;;)
  {
    c = getopt_long (argc, argv, "c:opPtu:Vhv", options, NULL);
    if (c == -1)
      break;
    switch (c)
    {
      case 'c':
        chunk_size = (size_t) strtoul (optarg, (char **) NULL, 10);
        if (chunk_size == 0)
        {
          x_error (0, "chunk size cannot be zero -- reverting to default");
          chunk_size = CHUNK_SIZE;
        }
        break;
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
        if ((update_interval < 0.0) ||
            isnan (update_interval) ||
            isinf (update_interval))
        update_interval = PROGRESS_UPDATE_INTERVAL;
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
#ifdef ENABLE_SOUND
      case NO_SOUND_OPTION:
        playing_sound = false;
        break;
#endif
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
  dst_path = files[--n_files];
  files[n_files] = NULL;
  src_path = (const char **) files;

  try_copy (src_path, n_files, dst_path);
  exit (EXIT_SUCCESS);
}

