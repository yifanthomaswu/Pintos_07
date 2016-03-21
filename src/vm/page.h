#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include "filesys/off_t.h"

enum page_flags
{
  PAGE_ZERO = 1,
  PAGE_WRITABLE = 2,
  PAGE_SHARE = 4,
  PAGE_FRAME = 8
};

/* A struct for pages, containing fields used to handle page_faults */
struct page
  {
    struct hash_elem pagehashelem;
    void *uaddr;
    enum page_flags flags;
    char *file_name;
    struct file *file;
    off_t ofs;
    uint32_t read_bytes;
    int64_t last_accessed_time;
  };

void page_init (void);
void page_done (void);
bool page_create_table (struct hash *page_table);
void page_destroy_table (struct hash *page_table);
bool page_new_page (void *page, enum page_flags flags, const char *file_name,
                    off_t ofs, uint32_t read_bytes);
void page_remove_page (void *page);
bool page_load_page (void *page, bool write);

struct hash_elem *page_lookup (const void *uaddr);

#endif /* vm_page_h */
