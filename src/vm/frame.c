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
#define VICTIM_CANDIDATES 4 // number of additional dirty pages to store on swap partition

/* frame_table entries contain the virtual kernel address and the page_directory */
struct frame
  {
    struct hash_elem framehashelem;
    struct list_elem framelistelem;
    void *kaddr;
    uint32_t pd;
  };

static struct hash frame_table; // frame_table
static struct lock frame_lock; // Lock to synchronise frame_table changes
static struct list clock; // Clock-list used for eviction algorithm
static struct lock clock_lock; // Lock to synchronise clock changes
static struct list_elem *hand; // list_elem pointing to an element of the clock


static unsigned frame_hash (const struct hash_elem *e, void *aux UNUSED);
static bool frame_less (const struct hash_elem *a, const struct hash_elem *b,
                        void *aux UNUSED);
static struct hash_elem *frame_lookup (void *kaddr);

/* Initialises the global static variables */
void
frame_init (void)
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  list_init (&clock);
  hand = list_begin(&clock);
  lock_init (&frame_lock);
  lock_init (&clock_lock);
}

/* Ensures a free frame, either by swapping out a page or by
 * calling palloc_get_page */
void *
frame_get_page (enum palloc_flags flags)
{
	ASSERT (flags && PAL_USER);

	void *victim = palloc_get_page (flags);
	// If there is no free frame:
	if (victim == NULL) {
		// Swap algorithm: WSClock

		// Set up locals
		void *candidate_victims[VICTIM_CANDIDATES];
		uint32_t *candidate_victims_pd[VICTIM_CANDIDATES];
		int num_candidate = 0;
		int64_t age = 0;
		uint32_t *pd;
		uint32_t *victim_pd;
		struct list_elem *first_elem = hand;
page_swapping:
		// If the hand points to the lists tail, start over from beginning of list
		if (hand == list_tail(&clock))
			hand = list_begin(&clock);
		// acquire frame that "hand" points to
		struct frame *e = list_entry(hand, struct frame, framelistelem);
		// get this frame's page's page_directory
		pd = e->pd;
		// and this frame's virtual kernel address
		void *e_kaddr = e->kaddr;
		// get the page that is stored in that frame
		struct page *page = hash_entry(page_lookup(e_kaddr), struct page, pagehashelem);
		// find the age of last access for this page
		int64_t page_age = timer_elapsed(page->last_accessed_time);
		if(page_age > TAU) {
			// If it's older than the parameter TAU,
			// update local variables to store best victim yet
			age = page_age;
			victim = e_kaddr;
			victim_pd = pd;
			if(!pagedir_is_dirty(pd, e_kaddr)) {
				// If the page is clean, choose this page and perform swap
				goto do_swap;
			}
			//if the page is dirty,
			if (num_candidate < VICTIM_CANDIDATES)
			  {
				// and if there is space for extra victims to be swapped,
				// add the page to the candidate_victims array
			    candidate_victims[num_candidate] = e_kaddr;
			    candidate_victims_pd[num_candidate] = pd;
			    num_candidate++;
			  }
		}
		else {
			// if the page is not older than TAU,
			if(page_age > age) {
				// but is the oldest page yet,
				// update locals
				age = page_age;
				victim = e_kaddr;
				victim_pd = pd;
			}
		}
		// Move hand up one entry
		hand = list_next(hand);
		// If the clock is fully traversed, do the swap, otherwise restart algorithm
		if (hand != first_elem)
			goto page_swapping;

do_swap:
		// Perform the swap of the victim
		swap_out(victim_pd, victim);
		// if victim in candidate_victim[] this means it will do nothing
		pagedir_set_dirty(victim_pd, victim, false);
		// free the page inside the frame
		frame_free_page(victim);
		// get new page using palloc_get_page
		victim = palloc_get_page (flags);
		// release lock after the this so new page can be read in into victim
		int n;
		for (n = 0; n < VICTIM_CANDIDATES; n++)
		  {
			// swap all the candidates out as well
		    void *candidate_victim = candidate_victims[n];
		    uint32_t *candidate_victim_pd = candidate_victims_pd[n];
		    if (candidate_victim != NULL)
		      {
			swap_out(candidate_victims_pd, candidate_victim);
			pagedir_set_dirty(candidate_victims_pd, candidate_victim, false);
		      }
		  }


	}
	// Set up the frame
	struct frame *f = malloc (sizeof(struct frame));
	if (f == NULL)
		PANIC ("frame_get_multiple: out of memory");
	f->kaddr = victim;
	f->pd = thread_current ()->pagedir;
	// insert frame into frame_table
	lock_acquire (&frame_lock);
	hash_insert (&frame_table, &f->framehashelem);
	lock_release (&frame_lock);

	// insert frame into clock
	lock_acquire (&clock_lock);
	list_push_back(&clock, &f->framelistelem);
	lock_release (&clock_lock);
	return victim;
}

/* Frees frame entry and corresponding page */
void
frame_free_page (void *page)
{
      void *addr = page;

      // Remove frame from frame_table
      lock_acquire (&frame_lock);
      struct hash_elem *he = hash_delete (&frame_table, frame_lookup (addr));
      lock_release (&frame_lock);

      struct frame *entry = hash_entry (he, struct frame, framehashelem);

      // Remove frame from clock
      lock_acquire (&clock_lock);
      list_remove (&entry->framelistelem);
      lock_release (&clock_lock);

      // free frame
      if (he != NULL)
        free (entry);
      // free page
      palloc_free_page (page);
}

/* Hash helper for the swap_table hash_table */
static unsigned
frame_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct frame *f = hash_entry (e, struct frame, framehashelem);
  return hash_bytes (&f->kaddr, sizeof f->kaddr);
}

/* Hash helper for the swap_table hash_table */
static bool
frame_less (const struct hash_elem *a, const struct hash_elem *b,
            void *aux UNUSED)
{
  return hash_entry (a, struct frame, framehashelem)->kaddr <
      hash_entry (b, struct frame, framehashelem)->kaddr;
}

/* Returns the hash_elem corresponding to the given virtual kernel address */
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
