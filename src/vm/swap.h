#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "vm/page.h"

void swap_init (void);
bool swap_out(struct page *);
int64_t swap_free(void *page_addr);
bool swap_in(struct page *);
#endif /* vm_swap_h_*/
