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

#include <math.h>
#include <string.h>

#include "progress.h"
#include "utils.h"

#define PROGRESS_OUT_BUFMAX 1024

#define PROGRESS_BAR_START     '['
#define PROGRESS_BAR_SO_FAR    '='
#define PROGRESS_BAR_HEAD      '>'
#define PROGRESS_BAR_REMAINING ' '
#define PROGRESS_BAR_END       ']'

#define progress_interval_has_passed(m) \
  ((m) >= (MILLISECONDS_PER_SECOND * update_interval))

extern size_t   total_sources;
extern byte_t    so_far_bytes;
extern byte_t     total_bytes;
extern double update_interval;

static struct
{
  size_t src_item;
  byte_t current_so_far_bytes;
  byte_t current_total_bytes;
  struct timeval last_update_time;
  struct timeval current_time;
  char current_total_size[SIZE_BUFMAX];
  struct
  {
    int size;
    int fill;
    long double factor;
  } bar;
} pdata;

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

static long
progress_interval_init (void)
{
  if ((pdata.last_update_time.tv_sec != -1) &&
      (pdata.last_update_time.tv_usec != -1))
  {
    x_gettimeofday (&pdata.current_time);
    return get_milliseconds (&pdata.last_update_time, &pdata.current_time);
  }
  return -1;
}

static void
progress_interval_update (long milliseconds)
{
  if (milliseconds != -1)
  {
    pdata.last_update_time.tv_sec = pdata.current_time.tv_sec;
    pdata.last_update_time.tv_usec = pdata.current_time.tv_usec;
  }
  else
    x_gettimeofday (&pdata.last_update_time);
}


static void
progress_bar_set (int remaining_space, int space_after_bar)
{
  pdata.bar.size = remaining_space - space_after_bar - 2;
  pdata.bar.factor = (((long double) pdata.current_so_far_bytes) /
                      ((long double) pdata.current_total_bytes));
  pdata.bar.fill = roundl (pdata.bar.factor * pdata.bar.size);
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
  int space_after_bar;
  char current_so_far_percent[PERCENT_BUFMAX];
  char current_so_far_size[SIZE_BUFMAX];
  char all_total_percent[PERCENT_BUFMAX];
  char all_so_far_size[SIZE_BUFMAX];
  char all_total_size[SIZE_BUFMAX];

  format_percent (current_so_far_percent,
                  pdata.current_so_far_bytes,
                  pdata.current_total_bytes);
  format_size (current_so_far_size, pdata.current_so_far_bytes, false);
  remaining_space = console_width ();

  if (total_sources > 1)
  {
    progress_printf (&remaining_space,
                     "%s %s/%s (item %zu/%zu) ",
                     current_so_far_percent,
                     current_so_far_size,
                     pdata.current_total_size,
                     pdata.src_item,
                     total_sources);
    format_percent (all_total_percent, so_far_bytes, total_bytes);
    format_size (all_so_far_size, so_far_bytes, false);
    format_size (all_total_size, total_bytes, false);
    space_after_bar = strlen (" total: ") +
                      strlen (all_so_far_size) +
                      strlen (all_total_size) +
                      strlen (all_total_percent) + 3;
  }
  else
  {
    progress_printf (&remaining_space,
                     "%s/%s ",
                     current_so_far_size,
                     pdata.current_total_size);
    space_after_bar = strlen (current_so_far_percent) + 2;
  }

  progress_bar_set (remaining_space, space_after_bar);
  if (pdata.bar.size)
  {
    progress_putchar (&remaining_space, PROGRESS_BAR_START);
    for (x = 0; (x < pdata.bar.fill); ++x)
      progress_putchar (&remaining_space, PROGRESS_BAR_SO_FAR);
    progress_putchar (&remaining_space, PROGRESS_BAR_HEAD);
    for (; (x < pdata.bar.size); ++x)
      progress_putchar (&remaining_space, PROGRESS_BAR_REMAINING);
    progress_putchar (&remaining_space, PROGRESS_BAR_END);
  }

  if (total_sources > 1)
    progress_printf (&remaining_space,
                     " total: %s/%s %s",
                     all_so_far_size,
                     all_total_size,
                     all_total_percent);
  else
    progress_printf (&remaining_space, " %s", current_so_far_percent);

  fputc ('\r', stdout);
  fflush (stdout);
}

void
progress_init (byte_t current_total_bytes, size_t src_item)
{
  pdata.src_item = src_item;
  pdata.current_total_bytes = current_total_bytes;
  pdata.current_so_far_bytes = BYTE_C (0);
  pdata.last_update_time.tv_sec = -1;
  pdata.last_update_time.tv_usec = -1;
  pdata.current_time.tv_sec = -1;
  pdata.current_time.tv_usec = -1;
  format_size (pdata.current_total_size, pdata.current_total_bytes, false);
  pdata.bar.size = 0;
  pdata.bar.fill = 0;
  pdata.bar.factor = 0.0;
}

void
progress_finish (void)
{
  progress_show ();
  fputc ('\n', stdout);
  fflush (stdout);
  *pdata.current_total_size = '\0';
}

void
progress_update (byte_t bytes)
{
  long m;

  pdata.current_so_far_bytes += bytes;
  so_far_bytes += bytes;

  m = progress_interval_init ();
  if ((m == -1) || progress_interval_has_passed (m))
  {
    progress_show ();
    progress_interval_update (m);
  }
}

