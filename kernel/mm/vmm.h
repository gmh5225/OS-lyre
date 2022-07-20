#ifndef _MM__VMM_H
#define _MM__VMM_H

#include <limine.h>

#define PAGE_SIZE 4096

extern volatile struct limine_hhdm_request hhdm_request;

#define VMM_HIGHER_HALF (hhdm_request.response->offset)

#endif
