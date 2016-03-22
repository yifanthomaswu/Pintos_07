#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include "threads/palloc.h"
#include "vm/page.h"

void frame_init (void);
void *frame_get_page (enum palloc_flags flags, struct page *current_page);
void frame_free_page (void *page);
void frame_free_multiple (void *pages, size_t page_cnt);

struct hash_elem *frame_lookup (void *kaddr);

#endif /* vm_frame_h */
