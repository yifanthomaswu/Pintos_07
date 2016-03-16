#include "swap.h"
#include <bitmap.h>
#include <debug.h>
#include "devices/block.h"

struct swap_table {
	struct block *swap_block;
	struct bitmap *sector_bm;
};

void
swap_init (void)
{
	swap = block_get_role (BLOCK_SWAP);
	if (swap == NULL)
	    PANIC ("No swap partition found, can't initialize swap table.");

}

void
swap_pages()
{

}
