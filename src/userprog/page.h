#ifndef PAGE_H_
#define PAGE_H_

#include <hash.h>
#include <stdbool.h>
#include "filesys/off_t.h"

enum page_location
{
  PAGE_FILESYS,
  PAGE_SWAP,
  PAGE_ZERO,
  PAGE_SHARED,
  PAGE_EXEC
};

void page_init (void);
void page_done (void);
bool page_create (struct hash *page_table);
bool page_new_page (void *page, enum page_location location, int fd,
                    const char *file_name, off_t ofs, uint32_t read_bytes,
                    uint32_t zero_bytes);
void page_remove_page (void *page);
bool page_load_page (void *page, bool write);
bool page_load_shared (void *page, const char *file_name, off_t ofs);
void page_unload_shared (void *page);
bool page_add_shared (void *kaddr, const char *file_name, off_t ofs);
void page_remove_shared (const char *file_name, off_t ofs);

#endif /* PAGE_H_ */
