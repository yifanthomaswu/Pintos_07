#include "userprog/frame.h"
#include <hash.h>
#include <debug.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"

struct frame
  {
    struct hash_elem framehashelem;
    struct list_elem framelist;
    void *vaddr;
  };

static struct hash frame_table;

static struct lock frame_lock;

//static struct list active_frames;

static unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED);
static bool frame_less (const struct hash_elem *a, const struct hash_elem *b,
                        void *aux UNUSED);
static struct hash_elem *frame_lookup (void *vaddr);

void
frame_init (void)
{
  lock_init(&frame_lock);
  hash_init (&frame_table, frame_hash, frame_less, NULL);
}

void *
frame_get_page (enum palloc_flags flags)
{
  return frame_get_multiple (flags, 1);
}

void *
frame_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
//  void *pages = palloc_get_multiple (PAL_ASSERT | PAL_USER | flags, page_cnt);
  void *pages = palloc_get_multiple (PAL_USER | flags, page_cnt);
  if (pages == NULL)
    return NULL;
  int i;
  for (i = 0; i < (int) page_cnt; i++)
    {
      struct frame *f = malloc (sizeof(struct frame));
      if (f == NULL)
        PANIC("frame_get_multiple: out of memory");
      f->vaddr = pages + i * PGSIZE;
      lock_acquire(&frame_lock);
      hash_insert (&frame_table, &f->framehashelem);
      lock_release(&frame_lock);
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

      lock_acquire(&frame_lock);
      struct hash_elem *e = hash_delete (&frame_table, frame_lookup (addr));
      lock_release(&frame_lock);

      if (e == NULL)
        continue;
      free (hash_entry (e, struct frame, framehashelem));
    }
  palloc_free_multiple (pages, page_cnt);
}

static unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct frame *f = hash_entry (e, struct frame, framehashelem);
  return hash_bytes (&f->vaddr, sizeof f->vaddr);
}

static bool
frame_less (const struct hash_elem *a, const struct hash_elem *b,
            void *aux UNUSED)
{
  return hash_entry (a, struct frame, framehashelem)->vaddr <
      hash_entry (b, struct frame, framehashelem)->vaddr;
}

static struct hash_elem *
frame_lookup (void *vaddr)
{
  //Caller needs to hold frame_lock already
  struct frame f;
  struct hash_elem *e;
  f.vaddr = vaddr;
  e = hash_find (&frame_table, &f.framehashelem);
  return e;
}

int
frame_get_size(void)
{
  lock_acquire(&frame_lock);
  int size = hash_size(&frame_table);
  lock_release(&frame_lock);
  return size;
}
