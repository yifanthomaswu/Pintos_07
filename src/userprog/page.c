#include "userprog/page.h"
#include <hash.h>
#include <debug.h>
#include <string.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"

struct page
  {
    struct hash_elem pagehashelem;
    void *vaddr;
    enum page_location location;
    int fd;
    char *file_name;
  };

static struct hash page_table;
static struct lock page_lock;

static unsigned page_hash (const struct hash_elem *e, void *aux UNUSED);
static bool page_less (const struct hash_elem *a, const struct hash_elem *b,
                       void *aux UNUSED);
static struct hash_elem *page_lookup (void *vaddr);

void
page_init (void)
{
  lock_init (&page_lock);
  hash_init (&page_table, page_hash, page_less, NULL);
}

void
page_new_page (void *page, enum page_location location, int fd,
               char *file_name)
{
  struct page *p = malloc (sizeof(struct page));
  if (p == NULL)
    PANIC("page_new_page: out of memory");
  p->vaddr = page;
  p->location = location;
  int length;
  switch (location)
    {
    case PAGE_FILESYS:
      p->fd = fd;
      break;
    case PAGE_SWAP:
      break;
    case PAGE_FRAME:
      length = strlen (file_name) + 1;
      p->file_name = malloc (length * sizeof(char));
      if (p->file_name == NULL)
        {
          free (p);
          PANIC("page_new_page: out of memory");
        }
      memcpy (p->file_name, file_name, length);
      break;
    }
  lock_acquire (&page_lock);
  hash_insert (&page_table, &p->pagehashelem);
  lock_release (&page_lock);
}

void
page_remove_page (void *page)
{
  lock_acquire (&page_lock);
  struct hash_elem *e = hash_delete (&page_table, page_lookup (page));
  lock_release (&page_lock);

  if (e == NULL)
    return;
  struct page *p = hash_entry(e, struct page, pagehashelem);
  free (p->file_name);
  free (p);
}

static unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *f = hash_entry (e, struct page, pagehashelem);
  return hash_bytes (&f->vaddr, sizeof f->vaddr);
}

static bool
page_less (const struct hash_elem *a, const struct hash_elem *b,
            void *aux UNUSED)
{
  return hash_entry (a, struct page, pagehashelem)->vaddr <
      hash_entry (b, struct page, pagehashelem)->vaddr;
}

static struct hash_elem *
page_lookup (void *vaddr)
{
  //Caller needs to hold page_lock already
  struct page f;
  struct hash_elem *e;
  f.vaddr = vaddr;
  e = hash_find (&page_table, &f.pagehashelem);
  return e;
}
