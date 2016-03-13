#ifndef PAGE_H_
#define PAGE_H_

enum page_location
{
  PAGE_FILESYS,
  PAGE_SWAP,
  PAGE_ZERO,
  PAGE_FRAME
};

void page_init (void);
void page_new_page (void *page, enum page_location location, int fd,
                    char *file_name);
void page_remove_page (void *page);

#endif /* PAGE_H_ */
