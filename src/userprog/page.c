#include "userprog/page.h"
#include <debug.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/frame.h"
#include "userprog/syscall.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

struct page
  {
    struct hash_elem pagehashelem;
    void *uaddr;
    enum page_location location;
    int fd;
    char *file_name;
    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
  };

struct shared
  {
    struct hash_elem sharedhashelem;
    void *kaddr;
    char *file_name;
    off_t ofs;
    int share_count;
  };

static struct hash shared_pages;
static struct lock shared_lock;

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

void
page_init (void)
{
  ASSERT (
      hash_init (&shared_pages, page_shared_hash, page_shared_less, NULL));
  lock_init (&shared_lock);
}

void
page_done (void)
{
  hash_destroy (&shared_pages, NULL);
}

bool
page_create (struct hash *page_table)
{
  return hash_init (page_table, page_hash, page_less, NULL);
}

bool
page_new_page (void *page, enum page_location location, int fd,
               const char *file_name, off_t ofs, uint32_t read_bytes,
               uint32_t zero_bytes)
{
  if (pagedir_get_page (thread_current ()->pagedir, page) != NULL)
    return false;
  struct page *p = malloc (sizeof(struct page));
  if (p == NULL)
    return false;
  p->uaddr = page;
  p->location = location;
  p->fd = -1;
  p->file_name = NULL;
  p->ofs = -1;
  p->read_bytes = -1;
  p->zero_bytes = -1;
  switch (location)
    {
    case PAGE_FILESYS:
      p->fd = fd;
      break;
    case PAGE_EXEC:
      p->read_bytes = read_bytes;
      p->zero_bytes = zero_bytes;
    case PAGE_SHARED:
      {
        int length = strlen (file_name) + 1;
        p->file_name = malloc (length * sizeof(char));
        if (p->file_name == NULL)
          {
            free (p);
            return false;
          }
        memcpy (p->file_name, file_name, length);
        p->ofs = ofs;
        break;
      }
    case PAGE_SWAP:
    case PAGE_ZERO:
      break;
    default:
      free (p);
      return false;
    }
  if (hash_insert (&thread_current ()->page_table, &p->pagehashelem) != NULL)
    {
      free (p->file_name);
      free (p);
      return false;
    }
  return true;
}

void
page_remove_page (void *page)
{
  struct hash_elem *e = hash_delete (&thread_current ()->page_table,
                                     page_lookup (page));

  if (e != NULL)
    {
      struct page *p = hash_entry (e, struct page, pagehashelem);
      free (p->file_name);
      free (p);
    }
}

bool
page_load_page (void *page, bool write)
{
  struct hash_elem *e = page_lookup (page);
  if (e == NULL)
    return false;
  struct page *p = hash_entry (e, struct page, pagehashelem);
  if (p->location == PAGE_SHARED && write)
    return false;

  switch (p->location)
    {
    case PAGE_FILESYS:
      break;
    case PAGE_ZERO:
      {
        void *kaddr = frame_get_page (PAL_USER | PAL_ZERO);
        if (kaddr == NULL)
          return false;
        if (!install_page (page, kaddr, true))
          {
            frame_free_page (kaddr);
            return false;
          }
        page_remove_page (page);
        break;
      }
    case PAGE_EXEC:
      if (!page_load_shared (page, p->file_name, p->ofs))
        {
          lock_acquire (&file_lock);
          struct file *file = filesys_open (p->file_name);
          if (file == NULL)
            {
              lock_release (&file_lock);
              return false;
            }
          if (!load_segment (file, p->ofs, page, p->read_bytes, p->zero_bytes,
                             false))
            {
              file_close (file);
              lock_release (&file_lock);
              return false;
            }
          file_close (file);
          lock_release (&file_lock);
          if (!page_add_shared (
              pagedir_get_page (thread_current ()->pagedir, page),
              p->file_name, p->ofs))
            {
              page_remove_page (page);
              break;
            }
        }
      p->location = PAGE_SHARED;
      p->read_bytes = -1;
      p->zero_bytes = -1;
      break;
    case PAGE_SWAP:
    case PAGE_SHARED:
      break;
    default:
      return false;
    }
  return true;
}

bool
page_load_shared (void *page, const char *file_name, off_t ofs)
{
  lock_acquire (&shared_lock);
  struct hash_elem *e = page_shared_lookup (file_name, ofs);
  if (e != NULL)
    {
      struct shared *s = hash_entry (e, struct shared, sharedhashelem);
      if (install_page (page, s->kaddr, false))
        {
          s->share_count++;
          lock_release (&shared_lock);
          return true;
        }
    }
  lock_release (&shared_lock);
  return false;
}

void
page_unload_shared (void *page)
{
  struct hash_elem *e = page_lookup (page);
  if (e == NULL)
    return;
  struct page *p = hash_entry (e, struct page, pagehashelem);
  if (p->location != PAGE_SHARED)
    return;

  lock_acquire (&shared_lock);
  e = page_shared_lookup (p->file_name, p->ofs);

  if (e != NULL)
    {
      struct shared *s = hash_entry (e, struct shared, sharedhashelem);
      pagedir_clear_page (thread_current ()->pagedir, page);
      s->share_count--;
      if (s->share_count == 0)
        {
          hash_delete (&shared_pages, e);
          frame_free_page (s->kaddr);
          free (s->file_name);
          free (s);
        }
    }
  lock_release (&shared_lock);
}

bool
page_add_shared (void *kaddr, const char *file_name, off_t ofs)
{
  lock_acquire (&shared_lock);
  struct hash_elem *e = page_shared_lookup (file_name, ofs);
  if (e == NULL)
    {
      struct shared *s = malloc (sizeof(struct shared));
      if (s == NULL)
        return false;
      int length = strlen (file_name) + 1;
      s->file_name = malloc (length * sizeof(char));
      if (s->file_name == NULL)
        {
          free (s);
          return false;
        }
      memcpy (s->file_name, file_name, length);
      s->kaddr = kaddr;
      s->ofs = ofs;
      s->share_count = 1;
      hash_insert (&shared_pages, &s->sharedhashelem);
    }
  else
    hash_entry (e, struct shared, sharedhashelem)->share_count++;
  lock_release (&shared_lock);
  return true;
}

static unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *p = hash_entry (e, struct page, pagehashelem);
  return hash_bytes (&p->uaddr, sizeof p->uaddr);
}

static bool
page_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux UNUSED)
{
  return hash_entry (a, struct page, pagehashelem)->uaddr <
  hash_entry (b, struct page, pagehashelem)->uaddr;
}

static struct hash_elem *
page_lookup (const void *uaddr)
{
  struct page p;
  p.uaddr = uaddr;
  return hash_find (&thread_current ()->page_table, &p.pagehashelem);
}

static unsigned
page_shared_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct shared *s = hash_entry (e, struct shared, sharedhashelem);
  return hash_string (s->file_name) ^ hash_int (s->ofs);
}

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

static struct hash_elem *
page_shared_lookup (const char *file_name, off_t ofs)
{
  //Caller needs to hold frame_lock already
  struct shared s;
  s.file_name = file_name;
  s.ofs = ofs;
  return hash_find (&shared_pages, &s.sharedhashelem);
}
