#include "swap.h"
#include <bitmap.h>
#include <hash.h>
#include <debug.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

struct lock swap_lock;
struct block swap_block;
struct bitmap sector_bm;
struct hash swap_table;

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
  lock_init(&swap_lock);
  swap_block = block_get_role(BLOCK_SWAP);
  if(swap_block == NULL)
    PANIC("ERROR: Couldn't initialise swap_table instance");
  sector_bm = bitmap_create(block_size(swap_block) * 8);
  hash_init (&swap_table, swap_hash, swap_less, NULL);
}

void
init_swap_table(struct swap_table *st)
{

}

void *
swap_back_in(void *)
{

}

bool
swap_page(struct swap_table *st, uint32_t *pd, void *page_addr)
{
  if(pagedir_is_dirty(pd, page_addr)) {
      // mark swap table entry in bitmap
      size_t bm_sector = bitmap_scan_and_flip(sector_bm, 0, 1, false);
      if(bm_sector == BITMAP_ERROR)
	Panic("ERROR: Swap partition full!");
      //create swap_table entry
      struct swap *s = malloc (sizeof(struct swap));
      if (s == NULL)
	PANIC ("swap_multiple: out of memory");
      s->uaddr = page_addr;
      s->sector = bm_sector;
      lock_acquire (&swap_lock);
      hash_insert (&swap_table, &s->swaphashelem);
      lock_release (&swap_lock);
      // Copy frame = 8 sectors
      const void* buffer = malloc(512);
      if (!buffer)
	return false;
      int i;
      for (i = 0; i < 8; i++)
	{
	  memcpy(buffer, page_addr + (i*512), 512);
	  block_write(&swap_block, bm_sector + i, buffer);
	}
      return true;
  }
}

void *
swap_multiple(struct swap_table *st, uint32_t *pd, void *page_addr, int page_cnt)
{
  int i;
  for (i = 0; i < page_cnt; ++i) {

      }
  }
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
