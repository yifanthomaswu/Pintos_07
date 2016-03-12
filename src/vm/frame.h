#ifndef FRAME_H_
#define FRAME_H_

#include <stddef.h>
#include "threads/palloc.h"

void frame_init (void);
void *frame_get_page (enum palloc_flags flags);
void *frame_get_multiple (enum palloc_flags flags, size_t page_cnt);

#endif /* FRAME_H_ */
