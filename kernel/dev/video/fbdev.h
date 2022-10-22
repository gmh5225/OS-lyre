#ifndef _DEV__VIDEO__FBDEV_H
#define _DEV__VIDEO__FBDEV_H

extern volatile struct limine_framebuffer_request framebuffer_request;

void fbdev_init(void);

#endif
