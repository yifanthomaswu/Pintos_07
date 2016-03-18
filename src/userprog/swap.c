#include "swap.h"
#include <bitmap.h>
#include <debug.h>
#include "devices/block.h"

bool
clean_page(uint32_t *pd, void *page)
{

}

void *
get_free_swap_slot(struct page_table *pt, int page_count)
{

}

void
swap_init (void)
{

}

void
init_swap_table(struct swap_table *st)
{
	st->swap_block = block_get_role(BLOCK_SWAP);
	if(st->swap_block == NULL)
		PANIC("ERROR: Couldn't initialise swap_table instance");
	st->sector_bm = bitmap_create(block_size(st->swap_block));
}

void *
swap_page(uint32_t *pd, void *page_addr)
{
	if(pagedir_is_dirty(pd, page_addr))
		clean_page(pd, page_addr);

}
