#include "userprog/frame.h"
#include <hash.h>
#include <debug.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"

struct frame
  {
    struct hash_elem framehashelem;
    struct list_elem framelistelem;
    void *kaddr;
  };

static struct hash frame_table;
static struct lock frame_lock;

static unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED);
static bool frame_less (const struct hash_elem *a, const struct hash_elem *b,
                        void *aux UNUSED);
static struct hash_elem *frame_lookup (const void *kaddr);

void
frame_init (void)
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&frame_lock);
}

void *
frame_get_page (enum palloc_flags flags)
{
  return frame_get_multiple (flags, 1);
}

void *
frame_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  ASSERT (flags & PAL_USER);

//  void *pages = palloc_get_multiple (PAL_ASSERT | PAL_USER | flags, page_cnt);
  void *pages = palloc_get_multiple (flags, page_cnt);
  if (pages == NULL)
    return NULL;
  int i;
  for (i = 0; i < (int) page_cnt; i++)
    {
      struct frame *f = malloc (sizeof(struct frame));
      if (f == NULL)
        PANIC ("frame_get_multiple: out of memory");
      f->kaddr = pages + i * PGSIZE;
      lock_acquire (&frame_lock);
      hash_insert (&frame_table, &f->framehashelem);
      lock_release (&frame_lock);
    }
  return pages;
}

void
frame_free_page (void *page)
{
  frame_free_multiple (page, 1);
}

void
frame_free_multiple (void *pages, size_t page_cnt)
{
  int i;
  for (i = 0; i < (int) page_cnt; i++)
    {
      void *addr = pages + i * PGSIZE;

      lock_acquire (&frame_lock);
      struct hash_elem *e = hash_delete (&frame_table, frame_lookup (addr));
      lock_release (&frame_lock);

      if (e != NULL)
        free (hash_entry (e, struct frame, framehashelem));
    }
  palloc_free_multiple (pages, page_cnt);
}

static unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct frame *f = hash_entry (e, struct frame, framehashelem);
  return hash_bytes (&f->kaddr, sizeof f->kaddr);
}

static bool
frame_less (const struct hash_elem *a, const struct hash_elem *b,
            void *aux UNUSED)
{
  return hash_entry (a, struct frame, framehashelem)->kaddr <
      hash_entry (b, struct frame, framehashelem)->kaddr;
}

static struct hash_elem *
frame_lookup (const void *kaddr)
{
  //Caller needs to hold frame_lock already
  struct frame f;
  f.kaddr = kaddr;
  return hash_find (&frame_table, &f.framehashelem);
}

int
frame_get_size (void)
{
  lock_acquire (&frame_lock);
  int size = hash_size (&frame_table);
  lock_release (&frame_lock);
  return size;
}