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

#ifndef __COPY_PROGRESS_H__
#define __COPY_PROGRESS_H__

#include "copy-utils.h"

/* the default value */
#define PROGRESS_UPDATE_INTERVAL 0.5

void progress_init (byte_t current_total_bytes, size_t src_item);
void progress_finish (void);
void progress_update (byte_t bytes);

#endif /* __COPY_PROGRESS_H__ */

