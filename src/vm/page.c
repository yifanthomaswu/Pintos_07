#include "vm/page.h"
#include <debug.h>
#include <string.h>
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"

static unsigned page_hash (const struct hash_elem *e, void *aux UNUSED);
static bool page_less (const struct hash_elem *a, const struct hash_elem *b,
                       void *aux UNUSED);

void
page_init (struct hash *page_table)
{
  hash_init (page_table, page_hash, page_less, NULL);
}

void
page_new_page (void *page, enum page_location location, int fd,
               const char *file_name, void *kaddr, off_t ofs,
               uint32_t read_bytes, uint32_t zero_bytes)
{
  struct page *p = malloc (sizeof(struct page));
  if (p == NULL)
    PANIC("page_new_page: out of memory");
  p->uaddr = page;
  p->location = location;
  p->last_accessed_time = timer_ticks();
  int length;
  switch (location)
    {
    case PAGE_FILESYS:
      p->fd = fd;
      break;
    case PAGE_SWAP:
      break;
    case PAGE_FRAME:
      p->kaddr = kaddr;
    case PAGE_EXEC:
      length = strlen (file_name) + 1;
      p->file_name = malloc (length * sizeof(char));
      if (p->file_name == NULL)
        {
          free (p);
          PANIC("page_new_page: out of memory");
        }
      memcpy (p->file_name, file_name, length);
      p->ofs = ofs;
      p->read_bytes = read_bytes;
      p->zero_bytes = zero_bytes;
      break;
    }
  hash_insert (&thread_current ()->page_table, &p->pagehashelem);
}

void
page_remove_page (void *page)
{
  struct hash_elem *e = hash_delete (&thread_current ()->page_table, page_lookup (page));

  if (e != NULL)
    {
      struct page *p = hash_entry (e, struct page, pagehashelem);
      free (p->file_name);
      free (p);
    }
}

bool
page_check_page (void *page, bool write)
{
  struct hash_elem *e = page_lookup (page);

  if (e != NULL)
    {
      struct page *p = hash_entry (e, struct page, pagehashelem);
      if (!write || p->file_name == NULL)
        return true;
    }
  return false;
}

bool
page_load_page (void *page)
{
  struct hash_elem *e = page_lookup (page);

  if (e != NULL)
    {
      struct page *p = hash_entry (e, struct page, pagehashelem);
      void *kaddr = NULL;
      bool writable = true;
      switch (p->location)
        {
        case PAGE_ZERO:
          kaddr = frame_get_page (PAL_USER | PAL_ZERO);
          break;
        case PAGE_FRAME:
          kaddr = p->kaddr;
          writable = false;
          break;
        case PAGE_EXEC:
          kaddr = p->kaddr;
          writable = false;
          break;
        }
      if (kaddr != NULL)
        return install_page (page, kaddr, writable);
    }
  return false;
}

static unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *f = hash_entry (e, struct page, pagehashelem);
  return hash_bytes (&f->uaddr, sizeof f->uaddr);
}

static bool
page_less (const struct hash_elem *a, const struct hash_elem *b,
            void *aux UNUSED)
{
  return hash_entry (a, struct page, pagehashelem)->uaddr <
      hash_entry (b, struct page, pagehashelem)->uaddr;
}

struct hash_elem *
page_lookup (void *uaddr)
{
  struct page f;
  struct hash_elem *e;
  f.uaddr = uaddr;
  e = hash_find (&thread_current ()->page_table, &f.pagehashelem);
  return e;
}
