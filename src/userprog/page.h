#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include "filesys/off_t.h"

enum page_location
{
  PAGE_FILESYS,
  PAGE_SWAP,
  PAGE_ZERO,
  PAGE_FRAME,
  PAGE_EXEC
};

void page_init (struct hash *page_table);
void page_new_page (void *page, enum page_location location, int fd,
                    const char *file_name, void *kaddr, off_t ofs,
                    uint32_t read_bytes, uint32_t zero_bytes);
void page_remove_page (void *page);
bool page_check_page (void *page, bool write);
bool page_load_page (void *page);

#endif /* vm_page_h */
