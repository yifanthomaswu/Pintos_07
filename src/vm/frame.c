#include "vm/frame.h"
#include <hash.h>
#include <debug.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"

struct frame
  {
    struct hash_elem framehashelem;
    struct list_elem framelist;
    void *vaddr;
  };

static struct hash frame_table;
//static struct list active_frames;

static unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED);
static bool frame_less (const struct hash_elem *a, const struct hash_elem *b,
                        void *aux UNUSED);
void
frame_init (void)
{
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
  void *pages = palloc_get_multiple (PAL_ASSERT | PAL_USER | flags, page_cnt);
  int i;
  for (i = 0; i < (int) page_cnt; i++)
    {
      struct frame *f = malloc (sizeof(struct frame));
      if (f == NULL)
        PANIC("get_frames: out of memory");
      f->vaddr = pages + i * PGSIZE;
      hash_insert (&frame_table, &f->framehashelem);
    }
  return pages;
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
