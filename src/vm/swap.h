#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "vm/page.h"

void swap_init (void);
bool swap_out(struct page *);
bool swap_in(struct page *);

// Debug helper
void print_swap_table(void);

#endif /* vm_swap_h_*/
