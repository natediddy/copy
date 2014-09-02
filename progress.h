#ifndef __PROGRESS_H__
#define __PROGRESS_H__

#include "utils.h"

/* the default value */
#define PROGRESS_UPDATE_INTERVAL 0.5

void progress_init (byte_t current_total_bytes, size_t src_item);
void progress_finish (void);
void progress_update (byte_t bytes);

#endif /* __PROGRESS_H__ */

