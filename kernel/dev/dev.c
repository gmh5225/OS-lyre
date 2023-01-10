#include <dev/dev.h>
#include <dev/char/console.h>
#include <dev/char/streams.h>
#include <dev/char/mouse.h>
#include <dev/video/fbdev.h>
#include <dev/ps2.h>
#include <dev/pci.h>

void dev_init(void) {
    ps2_init();
    mouse_init();
    console_init();
    streams_init();
    pci_init();
    fbdev_init();
}
