#include "vm/frame.h"
#include <hash.h>
#include <debug.h>
#include "devices/timer.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/pte.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

#define TAU 50 // parameter for page age (in timer ticks)

struct frame
  {
    struct hash_elem framehashelem;
    struct list_elem framelistelem;
    void *kaddr;
    uint32_t pd;

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
	ASSERT (flags && PAL_USER);

	//  void *page = palloc_get_multiple (PAL_ASSERT | PAL_USER | flags, page_cnt);
	void *victim = palloc_get_page (flags);
	if (victim == NULL) {
		// page swapping:
		void *swap_buffer = NULL; //TODO: maybe change to array/list
		int64_t age = 0;
		uint32_t *pd;
		uint32_t *victim_pd;
		struct list_elem *first_elem = hand;
page_swapping:
		if (hand == list_tail(&clock))
			hand = list_begin(&clock);
		struct frame *e = list_entry(hand, struct frame, framelistelem);
		pd = e->pd;
		void *e_kaddr = e->kaddr;
		struct page *page = hash_entry(page_lookup(e_kaddr), struct page, pagehashelem);
		int64_t page_age = timer_elapsed(page->last_accessed_time);
		if(page_age > TAU) {
			age = page_age;
			victim = e_kaddr;
			victim_pd = pd;
			if(!pagedir_is_dirty(pd, e_kaddr)) {
				goto do_swap;
			}
			if(swap_buffer == NULL) {
				swap_buffer = e_kaddr;
				pagedir_set_dirty(pd, e_kaddr, false);
			}
		}
		else {
			if(page_age > age) {
				age = page_age;
				victim = e_kaddr;
				victim_pd = pd;
			}
		}
		// Move hand up one entry
		hand = list_next(hand);
		if (hand != first_elem)
			goto page_swapping;

		do_swap:
		//if(swap_buffer != victim)
		//	swap_out(pd, swap_buffer);
		swap_out(victim_pd, victim);
		frame_free_page(victim);
	}
	struct frame *f = malloc (sizeof(struct frame));
	if (f == NULL)
		PANIC ("frame_get_multiple: out of memory");
	f->kaddr = victim;
	f->pd = thread_current ()->pagedir;
	lock_acquire (&frame_lock);
	hash_insert (&frame_table, &f->framehashelem);
	lock_release (&frame_lock);

	lock_acquire (&clock_lock);
	list_push_back(&clock, &f->framelistelem);
	lock_release (&clock_lock);
	return victim;
}

void
frame_free_page (void *page)
{
      void *addr = page;

      lock_acquire (&frame_lock);
      struct hash_elem *he = hash_delete (&frame_table, frame_lookup (addr));
      lock_release (&frame_lock);

      struct frame *entry = hash_entry (he, struct frame, framehashelem);

      lock_acquire (&clock_lock);
      list_remove (&entry->framelistelem);
      lock_release (&clock_lock);

      if (he != NULL)
        free (entry);
      palloc_free_page (page);
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
