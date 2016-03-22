#include "vm/page.h"
#include <debug.h>
#include <string.h>
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "userprog/syscall.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

/* A struct for a shared page */
struct shared
  {
    struct hash_elem sharedhashelem;
    void *kaddr;
    char *file_name;
    struct file *file;
    off_t ofs;
    uint32_t read_bytes;
    bool dirty;
    int share_count;
  };

/* hash_table for all shared pages */
static struct hash shared_pages;
/* Lock to synchronise access to shared_pages */
static struct lock shared_lock;

static bool page_load_shared (struct page *p);
static void page_unload_shared (struct page *p);
static bool page_add_shared (struct page *p);

static void page_destroy (struct hash_elem *e, void *aux UNUSED);
static unsigned page_hash (const struct hash_elem *e, void *aux UNUSED);
static bool page_less (const struct hash_elem *a, const struct hash_elem *b,
                       void *aux UNUSED);
static struct hash_elem *page_lookup (const void *uaddr);
static unsigned page_shared_hash (const struct hash_elem *e,
                                  void *aux UNUSED);
static bool page_shared_less (const struct hash_elem *a,
                              const struct hash_elem *b,
                              void *aux UNUSED);
static struct hash_elem *page_shared_lookup (const char *file_name,
                                             off_t ofs);

/* Initialises global static variables */
void
page_init (void)
{
  ASSERT (
      hash_init (&shared_pages, page_shared_hash, page_shared_less, NULL));
  lock_init (&shared_lock);
}

/* Destroys the shared_pages hash_table */
void
page_done (void)
{
  hash_destroy (&shared_pages, NULL);
}

/* Initialises the hash_table page_table */
bool
page_create_table (struct hash *page_table)
{
  return hash_init (page_table, page_hash, page_less, NULL);
}

/* Destroys the page_table hash_table */
void
page_destroy_table (struct hash *page_table)
{
  hash_destroy (page_table, page_destroy);
}

/* Removes page from hash_table and frees it */
static void
page_destroy (struct hash_elem *e, void *aux UNUSED)
{
  struct page *p = hash_entry (e, struct page, pagehashelem);
  page_unload_shared (p);
  lock_acquire (&file_lock);
  file_close (p->file);
  lock_release (&file_lock);
  free (p->file_name);
  free (p);
}

/* Add new page to the page_table. Returns success state */
bool
page_new_page (void *page, enum page_flags flags, const char *file_name,
               off_t ofs, uint32_t read_bytes)
{
  if (pagedir_get_page (thread_current ()->pagedir, page) != NULL)
    return false;
  // set up the page
  struct page *p = malloc (sizeof(struct page));
  if (p == NULL)
    return false;
  if (file_name != NULL)
    {
      int length = strlen (file_name) + 1;
      p->file_name = malloc (length * sizeof(char));
      if (p->file_name == NULL)
        {
          free (p);
          return false;
        }
      memcpy (p->file_name, file_name, length);
      if (!(flags & PAGE_ZERO))
        {
          lock_acquire (&file_lock);
          p->file = filesys_open (p->file_name);
          lock_release (&file_lock);
          if (p->file == NULL)
            {
              free (p->file_name);
              free (p);
              return false;
            }
        }
      else
        p->file = NULL;
    }
  else
    {
      p->file_name = NULL;
      p->file = NULL;
    }
  p->tid = thread_tid ();
  p->uaddr = page;
  p->flags = flags;
  p->ofs = ofs;
  p->read_bytes = read_bytes;
  p->last_accessed_time = timer_ticks ();
  p->pd = thread_current ()->pagedir;
  p->pinned = false;

  // Inset the page into the page_table
  if (hash_insert (&thread_current ()->page_table, &p->pagehashelem) != NULL)
    {
      lock_acquire (&file_lock);
      file_close (p->file);
      lock_release (&file_lock);
      free (p->file_name);
      free (p);
      return false;
    }
  return true;
}

struct page *
page_get_page (void *page)
{
  struct hash_elem *e = page_lookup (page);
  if (e == NULL)
    return NULL;
  return hash_entry (e, struct page, pagehashelem);
}

/* Removes page from page_table */
void
page_remove_page (void *page)
{
  struct hash_elem *e = hash_delete (&thread_current ()->page_table,
                                     page_lookup (page));
  if (e != NULL)
    page_destroy (e, NULL);
}

bool
page_load_page (void *page, bool write)
{
  page = pg_round_down (page);
  struct hash_elem *e = page_lookup (page);
  if (e == NULL)
    return false;
  struct page *p = hash_entry (e, struct page, pagehashelem);
  bool writable = p->flags & PAGE_WRITABLE;
  if (write && !writable)
    return false;

  bool share = p->flags & PAGE_SHARE;
  if (share)
    if (page_load_shared (p))
      return true;

  if (p->flags & PAGE_ZERO)
    {
      void *kaddr = frame_get_page (PAL_USER | PAL_ZERO, page);
      if (kaddr == NULL)
        return false;
      if (!install_page (page, kaddr, writable))
        {
          frame_free_page (kaddr);
          return false;
        }
      p->flags ^= PAGE_ZERO;
      p->kaddr = kaddr;
    }
  else
    {
      if (!lock_held_by_current_thread (&file_lock))
        lock_acquire (&file_lock);
      if (!load_segment (p->file, p->ofs, page, p->read_bytes,
                         PGSIZE - p->read_bytes, writable))
        {
          if (lock_held_by_current_thread (&file_lock))
            lock_release (&file_lock);
          return false;
        }
      if (lock_held_by_current_thread (&file_lock))
        lock_release (&file_lock);
    }

  if (share)
    page_add_shared (p);

  return true;
}

static bool
page_load_shared (struct page *p)
{
  lock_acquire (&shared_lock);
  struct hash_elem *e = page_shared_lookup (p->file_name, p->ofs);
  if (e != NULL)
    {
      struct shared *s = hash_entry (e, struct shared, sharedhashelem);
      if (install_page (p->uaddr, s->kaddr, p->flags & PAGE_WRITABLE))
        {
          s->share_count++;
          p->flags |= PAGE_FRAME;
          lock_release (&shared_lock);
          return true;
        }
    }
  lock_release (&shared_lock);
  return false;
}

static void
page_unload_shared (struct page *p)
{
  if (!(p->flags & PAGE_FRAME))
    return;

  lock_acquire (&shared_lock);
  struct hash_elem *e = page_shared_lookup (p->file_name, p->ofs);
  if (e != NULL)
    {
      struct shared *s = hash_entry (e, struct shared, sharedhashelem);
      uint32_t *pd = thread_current ()->pagedir;
      s->dirty = pagedir_is_dirty (pd, p->uaddr);
      s->share_count--;
      if (s->share_count == 0)
        {
          hash_delete (&shared_pages, e);
          lock_acquire (&file_lock);
          if (s->dirty)
            {
              file_seek (s->file, s->ofs);
              file_write (s->file, p->uaddr, s->read_bytes);
            }
          file_close (s->file);
          lock_release (&file_lock);
          frame_free_page (s->kaddr);
          free (s->file_name);
          free (s);
        }
      pagedir_clear_page (pd, p->uaddr);
    }
  lock_release (&shared_lock);
}

/* Add a page to shared_pages. Returns success state */
static bool
page_add_shared (struct page *p)
{
  lock_acquire (&shared_lock);
  struct hash_elem *e = page_shared_lookup (p->file_name, p->ofs);
  if (e == NULL)
    {
      struct shared *s = malloc (sizeof(struct shared));
      if (s == NULL)
        return false;
      int length = strlen (p->file_name) + 1;
      s->file_name = malloc (length * sizeof(char));
      if (s->file_name == NULL)
        {
          free (s);
          lock_release (&shared_lock);
          return false;
        }
      memcpy (s->file_name, p->file_name, length);
      if (!lock_held_by_current_thread (&file_lock))
        lock_acquire (&file_lock);
      s->file = filesys_open (s->file_name);
      if (lock_held_by_current_thread (&file_lock))
        lock_release (&file_lock);
      if (s->file == NULL)
        {
          free (s->file_name);
          free (s);
          lock_release (&shared_lock);
          return false;
        }
      s->kaddr = pagedir_get_page (thread_current ()->pagedir, p->uaddr);
      s->ofs = p->ofs;
      s->read_bytes = p->flags & PAGE_WRITABLE ? p->read_bytes : 0;
      s->dirty = false;
      s->share_count = 1;
      hash_insert (&shared_pages, &s->sharedhashelem);
    }
  else
    hash_entry (e, struct shared, sharedhashelem)->share_count++;
  p->flags |= PAGE_FRAME;
  lock_release (&shared_lock);
  return true;
}

/* Hash helper for the page_table hash_table */
static unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *p = hash_entry (e, struct page, pagehashelem);
  return hash_bytes (&p->uaddr, sizeof p->uaddr);
}

/* Hash helper for the page_table hash_table */
static bool
page_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux UNUSED)
{
  return hash_entry (a, struct page, pagehashelem)->uaddr <
  hash_entry (b, struct page, pagehashelem)->uaddr;
}

/* Returns the hash_elem corresponding to the given user virtual address */
struct hash_elem *
page_lookup (const void *uaddr)
{
  struct page p;
  p.uaddr = uaddr;
  return hash_find (&thread_current ()->page_table, &p.pagehashelem);
}

/* Hash helper for the shared_pages hash_table */
static unsigned
page_shared_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct shared *s = hash_entry (e, struct shared, sharedhashelem);
  return hash_string (s->file_name) ^ hash_int (s->ofs);
}

/* Hash helper for the shared_pages hash_table */
static bool
page_shared_less (const struct hash_elem *a, const struct hash_elem *b,
                  void *aux UNUSED)
{
  struct shared *s_a = hash_entry (a, struct shared, sharedhashelem);
  struct shared *s_b = hash_entry (b, struct shared, sharedhashelem);
  int cmp = strcmp (s_a->file_name, s_b->file_name);
  if (cmp == 0)
    return s_a->ofs < s_b->ofs;
  else
    return cmp < 0;
}

/* Returns the hash_elem corresponding to the given file_name and offset */
static struct hash_elem *
page_shared_lookup (const char *file_name, off_t ofs)
{
  //Caller needs to hold frame_lock already
  struct shared s;
  s.file_name = file_name;
  s.ofs = ofs;
  return hash_find (&shared_pages, &s.sharedhashelem);
}
