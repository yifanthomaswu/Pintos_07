#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>

void swap_init (void);
void init_swap_table(struct swap_table *);
void *swap_page(struct swap_table *, uint32_t *, void *);
void *swap_multiple(struct swap_table *, uint32_t *, void *, int);
void *swap_back_in(struct swap_table *, void *);
#endif /* vm_swap_h_*/
