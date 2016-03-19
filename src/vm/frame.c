#include "vm/frame.h"
#include <hash.h>
#include <debug.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

struct frame
  {
    struct hash_elem framehashelem;
    struct list_elem framelistelem;
    void *kaddr;
  };

static struct hash frame_table;
static struct lock frame_lock;
static struct list clock;
static struct lock clock_lock;
static struct list_elem *hand;


static unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED);
static bool frame_less (const struct hash_elem *a, const struct hash_elem *b,
                        void *aux UNUSED);
static struct hash_elem *frame_lookup (void *kaddr);

void
frame_init (void)
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  list_init (&clock);
  hand = list_begin(&clock);
  lock_init (&frame_lock);
  lock_init (&clock_lock);
}

void *
frame_get_page (enum palloc_flags flags)
{
  return frame_get_multiple (flags, 1);
}

void *
frame_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  ASSERT (flags && PAL_USER);

//  void *pages = palloc_get_multiple (PAL_ASSERT | PAL_USER | flags, page_cnt);
  void *pages = palloc_get_multiple (flags, page_cnt);
  if (pages == NULL) {
	// page swapping:
	  uint32_t *pd = thread_current()->pagedir;
page_swapping:
		if(hand == list_tail(&clock)) //TODO: Check comparison operator
		   hand = list_begin(&clock);
		void *e_kaddr = list_entry(hand, struct frame, framelistelem)->kaddr;
		// get the user page from kernel address
		void *e_uaddr = e_kaddr; //TODO: get the actual user page address
		// Move hand up one entry
		hand = list_next(hand);
		// check if accessed bit is set
		if (pagedir_is_accessed(pd, e_uaddr)) {
			// If yes, then reset it to 0 and try finding another page to swap
			pagedir_set_accessed(pd, e_uaddr, false);
			goto page_swapping;
		}
		// If a good page is found, swap it
		pages = swap_page(pd, e_uaddr);
		frame_free_multiple(pages, page_cnt);
  }
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

      lock_acquire (&clock_lock);
      list_push_back(&clock, &f->framelistelem);
      lock_release (&clock_lock);
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
      struct hash_elem *he = hash_delete (&frame_table, frame_lookup (addr));
      lock_release (&frame_lock);

      struct frame *entry = hash_entry (he, struct frame, framehashelem);

      lock_acquire (&clock_lock);
      struct list_elem *le = list_remove (&entry->framelistelem);
      lock_release (&clock_lock);

      if (he != NULL)
        free (entry);
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
frame_lookup (void *kaddr)
{
  //Caller needs to hold frame_lock already
  struct frame f;
  struct hash_elem *e;
  f.kaddr = kaddr;
  e = hash_find (&frame_table, &f.framehashelem);
  return e;
}

int
frame_get_size (void)
{
  lock_acquire (&frame_lock);
  int size = hash_size (&frame_table);
  lock_release (&frame_lock);
  return size;
}
