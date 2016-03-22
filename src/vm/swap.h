#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "threads/thread.h"

void swap_init (void);
bool swap_out(uint32_t *, tid_t, void *);
int64_t swap_free(void *page_addr);
bool swap_in(void *);
#endif /* vm_swap_h_*/
