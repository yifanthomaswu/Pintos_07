#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>
#include <stdbool.h>

void swap_init (void);
bool swap_page(uint32_t *, void *);
bool swap_back_in(void *);
#endif /* vm_swap_h_*/
