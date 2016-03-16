#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include "threads/palloc.h"

void frame_init (void);
void *frame_get_page (enum palloc_flags flags);
void *frame_get_multiple (enum palloc_flags flags, size_t page_cnt);
void frame_free_page (void *page);
void frame_free_multiple (void *pages, size_t page_cnt);
int frame_get_size(void);

#endif /* vm_frame_h */
