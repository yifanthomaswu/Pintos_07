#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>
#include <hash.h>
#include "threads/palloc.h"
#include "vm/page.h"

/* frame_table entries contain the virtual kernel address and the page_directory */
struct frame
{
  struct hash_elem framehashelem;
  struct list_elem framelistelem;
  void *kaddr;
  struct page *page;
};

void frame_init (void);
void *frame_get_page (enum palloc_flags flags, struct page *current_page);
void frame_free_page (void *page);
void frame_free_multiple (void *pages, size_t page_cnt);
struct frame *frame_get_frame (void *kaddr);

#endif /* vm_frame_h */
