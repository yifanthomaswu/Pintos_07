#ifndef VM_SWAP_H
#define VM_SWAP_H

struct swap_table {
	struct block *swap_block;
	struct bitmap *sector_bm;
};

void swap_init (void);
void *swap_page(uint32_t *, void *);

#endif /* vm_swap_h_*/
