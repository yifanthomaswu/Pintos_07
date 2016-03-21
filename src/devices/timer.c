#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <hash.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "vm/page.h"

#define K 4 // K = TIME_SLICE

static bool active = false;

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

/* List of all processes that is asleep. Processes are added to this list
   when timer_sleep is called on them and removed when they wake up. */
static struct list sleep_list;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

static bool list_less_wake (const struct list_elem *a,
                            const struct list_elem *b, void *aux UNUSED);
static inline void wake_ready (void);

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void) 
{
  list_init (&sleep_list);
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

void activate(void)
{
  active = true;
}

void deactivate(void)
{
  active = false;
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) 
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1)) 
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (high_bit | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) 
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) 
{
  return timer_ticks () - then;
}

/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks)
{
  /* A request to sleep for 0 tick or less has no effect. */
  if (ticks > 0)
    {
      int64_t start = timer_ticks ();
      struct thread *t;
      enum intr_level old_level;

      ASSERT (intr_get_level () == INTR_ON);

      t = thread_current ();
      sema_init (&t->can_wake, 0); /* Initialise semaphore to 0. */
      t->wake_ticks = start + ticks; /* Set up the ticks to wake up at. */

      /* Disabling interrupts while add the thread to sleep_list. */
      old_level = intr_disable ();
      list_insert_ordered (&sleep_list, &t->sleepelem, &list_less_wake, NULL);
      intr_set_level (old_level);

      sema_down (&t->can_wake); /* Down the semaphore. */
      /* Current thread, on which sleep was called, now goes onto the
         waiting list of the semaphore, causing its state to change to
         BLOCKED. This means that a different thread, one that is READY
         can run, and if no other threads are READY, the idle thread runs. */
    }
}

/* Comparison function used to insert threads into sleep_list in
   ascending order of wake_ticks value. */
static bool
list_less_wake (const struct list_elem *a, const struct list_elem *b,
                void *aux UNUSED)
{
  return list_entry (a, struct thread, sleepelem)->wake_ticks
      < list_entry (b, struct thread, sleepelem)->wake_ticks;
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms) 
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us) 
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns) 
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms) 
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us) 
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns) 
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) 
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
#ifdef USERPROG

  if(active && thread_current ()->tid != 2)
	  // Every K ticks
	if(timer_ticks () % K == 0) {
	   struct thread *t = thread_current ();
	   uint32_t *pd = t->pagedir;
	   struct hash_iterator i;
	   hash_first (&i, &t->page_table);
	   while (hash_next (&i))
	   {
		  // If a page was accessed, update it's last_accessed field
		   struct page *p = hash_entry (hash_cur (&i), struct page, pagehashelem);
		  if(pagedir_is_accessed(pd, p->uaddr)) {
			  // set last_accessed_time to the current number of timer_ticks
			  p->last_accessed_time = timer_ticks ();
			  // reset the accessed but of the page
			  pagedir_set_accessed(pd, p->uaddr, false);
		  }
	   }
	}
#endif
  ticks++;
  thread_tick ();

  /* Check and wake up any threads ready to be woke up. */
  wake_ready ();
}

/* Wake up any threads that are ready. */
static inline void
wake_ready (void)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  /* Iterate thought elements in sleep_list and check if they are ready. */
  for (e = list_begin (&sleep_list); e != list_end (&sleep_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, sleepelem);
      /* If the thread's wake_ticks member is less then or equal to
         the current timer ticks, we can wake the thread up.
         Otherwise, stop the iteration since threads are placed in
         ascending order of wake_ticks. */
      if (t->wake_ticks <= timer_ticks ())
        {
          /* Remove the thread from sleep_list and up the semaphore.
             The thread will be READY. */
          list_remove (&t->sleepelem);
          sema_up (&t->can_wake);
        }
      else
        return;
    }
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) 
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) 
{
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) 
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.
          
        (NUM / DENOM) s          
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks. 
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */                
      timer_sleep (ticks); 
    }
  else 
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom); 
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
}
