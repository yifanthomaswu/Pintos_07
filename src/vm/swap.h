#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>
#include <stdbool.h>

void swap_init (void);
bool swap_out(uint32_t *, void *);
bool swap_in(void *);
#endif /* vm_swap_h_*/
