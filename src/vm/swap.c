#include "vm/swap.h"
#include <bitmap.h>
#include <hash.h>
#include <debug.h>
#include <string.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"

static struct lock swap_lock;
static struct block *swap_block;
static struct bitmap *sector_bm;
static struct hash swap_table;

static unsigned swap_hash (const struct hash_elem *e, void *aux UNUSED);
static bool swap_less (const struct hash_elem *a, const struct hash_elem *b,
                       void *aux UNUSED);
static struct hash_elem *swap_lookup (void *uaddr);

struct swap {
  struct hash_elem swaphashelem;
  void *uaddr;
  uint32_t sector;
};

void
swap_init (void)
{
  swap_block = block_get_role(BLOCK_SWAP);
  if(swap_block == NULL)
    PANIC("ERROR: Couldn't initialise swap_table instance");
  lock_init(&swap_lock);
  sector_bm = bitmap_create(block_size(swap_block));
  hash_init (&swap_table, swap_hash, swap_less, NULL);
}

bool
swap_in(void *page_addr)
{
  lock_acquire (&swap_lock);
  struct swap *s = hash_entry(hash_find(&swap_table, swap_lookup(page_addr)), struct swap, swaphashelem);
  lock_release (&swap_lock);
  if (s)
    {
      lock_acquire (&swap_lock);
      hash_delete(&swap_table, &s->swaphashelem);
      lock_release (&swap_lock);

      size_t bm_sector = s->sector;
      free(s);
      bitmap_scan_and_flip(sector_bm, bm_sector, 1, true);

      bm_sector *= 8;
      void* buffer = malloc(512);
      if (!buffer)
	return false;
      int i;
      for (i = 0; i < 8; i++)
	{
	  block_read(swap_block, bm_sector + i, buffer);
	  memcpy(page_addr + (i*512), buffer, 512);
	}
      free(buffer);
      return true;
    }
  return false;
}

bool
swap_out(uint32_t *pd, void *page_addr)
{
  if(pagedir_is_dirty(pd, page_addr)) {
      // mark swap table entry in bitmap
      size_t bm_sector = bitmap_scan_and_flip(sector_bm, 0, 1, false);
      if(bm_sector == BITMAP_ERROR)
	PANIC("ERROR: Swap partition full!");
      //create swap_table entry
      struct swap *s = malloc (sizeof(struct swap));
      if (s == NULL)
	PANIC ("swap_multiple: out of memory");
      s->uaddr = page_addr;
      s->sector = bm_sector;
      bm_sector *= 8;
      lock_acquire (&swap_lock);
      hash_insert (&swap_table, &s->swaphashelem);
      lock_release (&swap_lock);
      // Copy frame = 8 sectors
      void* buffer = malloc(512);
      if (!buffer)
	return false;
      int i;
      for (i = 0; i < 8; i++)
	{
	  memcpy(buffer, page_addr + (i*512), 512);
	  block_write(swap_block, bm_sector + i, buffer);
	}
      free(buffer);
      return true;
  }
  return false;
}

static unsigned
swap_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct swap *s = hash_entry (e, struct swap, swaphashelem);
  return hash_bytes (&s->uaddr, sizeof s->uaddr);
}

static bool
swap_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux UNUSED)
{
  return hash_entry (a, struct swap, swaphashelem)->uaddr <
      hash_entry (b, struct swap, swaphashelem)->uaddr;
}

static struct hash_elem *
swap_lookup (void *uaddr)
{
  //Caller needs to hold frame_lock already
  struct swap s;
  struct hash_elem *e;
  s.uaddr = uaddr;
  e = hash_find (&swap_table, &s.swaphashelem);
  return e;
}
