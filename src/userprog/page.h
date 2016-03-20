#ifndef PAGE_H_
#define PAGE_H_

#include <hash.h>
#include <stdbool.h>
#include "filesys/off_t.h"

enum page_flags
{
  PAGE_ZERO = 1,
  PAGE_WRITABLE = 2,
  PAGE_SHARED = 4
};

void page_init (void);
void page_done (void);
bool page_create_table (struct hash *page_table);
bool page_new_page (void *page, enum page_flags flags, const char *file_name,
                    off_t ofs, uint32_t read_bytes);
void page_remove_page (void *page);
bool page_load_page (void *page, bool write);

#endif /* PAGE_H_ */
