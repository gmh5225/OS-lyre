#ifndef _DEV__PCI_H
#define _DEV__PCI_H

#include <stdint.h>
#include <stdbool.h>

struct pci_device {
    uint8_t seg;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;

    uint8_t dev_class;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t rev_id;
    uint16_t device_id;
    uint16_t vendor_id;

    bool msi_supported;
    uint16_t msi_offset;
};

#define pci_readd(dev, off) pci_read(dev, off, 4)
#define pci_readw(dev, off) pci_read(dev, off, 2)
#define pci_readb(dev, off) pci_read(dev, off, 1)
#define pci_writed(dev, off, val) pci_write(dev, off, val, 4)
#define pci_writew(dev, off, val) pci_write(dev, off, val, 2)
#define pci_writeb(dev, off, val) pci_write(dev, off, val, 1)

void pci_init(void);
uint32_t pci_read(struct pci_device* d, uint32_t off, int access_size);
void pci_write(struct pci_device* d, uint32_t off, uint32_t val, int access_size);

#endif
