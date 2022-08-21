#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <dev/pci.h>
#include <dev/dev.h>
#include <acpi/acpi.h>
#include <mm/vmm.h>
#include <lib/vector.h>
#include <lib/print.h>
#include <sys/cpu.h>

struct mcfg_entry {
    uint64_t mmio_base;
    uint16_t segment;
    uint8_t start;
    uint8_t end;
    uint32_t reserved;
};

union msi_address {
    struct {
        uint32_t reserved0 : 2;
        uint32_t dest_mode : 1;
        uint32_t redir_hint : 1;
        uint32_t reserved1 : 8;
        uint32_t dest_id : 8;
        uint32_t base_address : 12;
    };

    uint32_t raw;
};

union msi_data {
    struct {
        uint32_t vector : 8;
        uint32_t delivery : 3;
        uint32_t reserved : 3;
        uint32_t level : 1;
        uint32_t trig_mode : 1;
        uint32_t reserved0 : 16;
    };

    uint32_t raw;
};

uint32_t (*pci_read)(struct pci_device *, uint32_t, int);
void (*pci_write)(struct pci_device *, uint32_t, uint32_t, int);

static VECTOR_TYPE(struct mcfg_entry) mcfg_entries = VECTOR_INIT;
static VECTOR_TYPE(struct pci_device *) devlist = VECTOR_INIT;
static void scan_bus(uint8_t bus);

static uint32_t mcfg_read(struct pci_device *dev, uint32_t offset, int access_size) {
    VECTOR_FOR_EACH(&mcfg_entries, ent,
        if (dev->seg != ent->segment) {
            continue;
        } else if ((dev->bus > ent->end) || (dev->bus < ent->start)) {
            continue;
        }

        uintptr_t target = ((dev->bus - ent->start) << 20) | (dev->slot << 15) | (dev->func << 12);
        target += ent->mmio_base + offset + VMM_HIGHER_HALF;

        switch (access_size) {
            case 1:
                return *(uint8_t *)target;
            case 2:
                return *(uint16_t *)target;
            case 4:
                return *(uint32_t *)target;
        }
    );

    return 0;
}

static void mcfg_write(struct pci_device *dev, uint32_t offset, uint32_t value, int access_size) {
    VECTOR_FOR_EACH(&mcfg_entries, ent,
        if (dev->seg != ent->segment) {
            continue;
        } else if ((dev->bus > ent->end) || (dev->bus < ent->start)) {
            continue;
        }

        uintptr_t target = ((dev->bus - ent->start) << 20) | (dev->slot << 15) | (dev->func << 12);
        target += ent->mmio_base + offset + VMM_HIGHER_HALF;
        
        switch (access_size) {
            case 1:
                *(uint8_t *)target = (uint8_t)value;
                return;
            case 2:
                *(uint16_t *)target = (uint16_t)value;
                return;
            case 4:
                *(uint32_t *)target = (uint32_t)value;
                return;
        }
    );
}

static void scan_function(uint8_t bus, uint8_t slot, uint8_t func) {
    struct pci_device *dev = ALLOC(struct pci_device, ALLOC_MISC);
    *dev = (struct pci_device) {
        .bus = bus,
        .slot = slot,
        .func = func
    };

    if (PCI_READD(dev, 0) == (uint32_t)-1) {
        FREE(dev, ALLOC_MISC);
        return;
    }

    uint32_t reg_0 = PCI_READD(dev, 0);
    uint32_t reg_2 = PCI_READD(dev, 2 * sizeof(uint32_t));

    dev->device_id = (uint16_t)(reg_0 >> 16);
    dev->vendor_id = (uint16_t)reg_0;
    dev->rev_id = (uint8_t)reg_2;
    dev->subclass = (uint8_t)(reg_2 >> 16);
    dev->pci_class = (uint8_t)(reg_2 >> 24);
    dev->prog_if = (uint8_t)(reg_2 >> 8);

    if (dev->pci_class == 6 && dev->subclass == 4) {
        // Check if there are more devices hidden behind this bridge
        uint32_t reg_6 = PCI_READD(dev, 6 * sizeof(uint32_t));
        scan_bus((reg_6 >> 8) & 0xff);
    }

    uint16_t sreg = PCI_READW(dev, 6);
    if (sreg & (1 << 4)) {
        // Traverse the caps list, looking for MSI/MSI-X
        uint8_t next_off = PCI_READB(dev, 0x34);

        while (next_off) {
            uint8_t ident = PCI_READB(dev, next_off);

            switch (ident) {
                case 5: // MSI compatible
                    dev->msi_supported = true;
                    dev->msi_offset = next_off;
                    break;
                case 17: // MSI-X compatible
                    dev->msix_supported = true;
                    dev->msix_offset = next_off;
                    break;
            }

            next_off = PCI_READB(dev, next_off + 1);
        }
    }

    VECTOR_PUSH_BACK(&devlist, dev);
}

static void scan_bus(uint8_t bus) {
    for (int slot = 0; slot < 32; slot++) {
        for (int func = 0; func < 8; func++) {
            scan_function(bus, slot, func);
        }
    } 
}

static void scan_root_bus(void) {
    struct pci_device root_dev = {0};
    
    if (!(PCI_READD(&root_dev, 0xC) & 0x800000)) {
        // Only one PCI host controller
        scan_bus(0);
    } else {
        // Multiple PCI host controllers
        for (int i = 0; i < 8; i++) {
            root_dev.func = i;

            if (PCI_READD(&root_dev, 0x0) == (uint32_t)-1) {
                continue;
            }

            scan_bus(i);
        }
    }

    kernel_print("pci: detected devices:\n");
    VECTOR_FOR_EACH(&devlist, device,
        struct pci_device *dev = *device; 
        kernel_print("  - %02d:%02d:%02d %04x:%04x %02d:%02d:%02d\n", 
            dev->bus, dev->slot, dev->func,
            dev->vendor_id, dev->device_id,
            dev->pci_class, dev->subclass, dev->prog_if);
    );
}

static void dispatch_drivers(void) {
    DRIVER_FOR_EACH(DRIVER_PCI, dev,
        struct pci_driver *p = dev->pci_dev;

        VECTOR_FOR_EACH(&devlist, device,
            struct pci_device *d = *device; 
      
            if (p->match & PCI_MATCH_DEVICE) {
                if ((d->vendor_id != p->vendor) || (d->device_id != p->device)) {
                    continue;
                }

                p->init(d);
            } else {
                if ((p->match & PCI_MATCH_CLASS) && (d->pci_class != p->pci_class)) {
                    continue;
                }

                if ((p->match & PCI_MATCH_SUBCLASS) && (d->subclass != p->subclass)) {
                    continue;
                }

                if ((p->match & PCI_MATCH_PROG_IF) && (d->prog_if != p->prog_if)) {
                    continue;
                }

                p->init(d);
            }
        );
    );
}

void pci_init(void) {
    // Try to use the MCFG when possible
    struct sdt *mcfg = acpi_find_sdt("MCFG", 0);
    if (mcfg == NULL) {
        goto legacy;
    }

    int entries_count = (mcfg->length - 44) / 16;
    if (entries_count == 0) {
        goto legacy;
    }

    struct mcfg_entry *mcfg_base = (void *)((uintptr_t)mcfg + sizeof(struct sdt) + sizeof(uint64_t));
    for (int i = 0; i < entries_count; i++) {
        struct mcfg_entry ent = mcfg_base[i];
        VECTOR_PUSH_BACK(&mcfg_entries, ent); 
        kernel_print("pci: found ECAM space for segment %d, bus range %d-%d\n", ent.segment, ent.start, ent.end);
    }

    pci_read = mcfg_read;
    pci_write = mcfg_write;
    scan_root_bus();
    dispatch_drivers();

legacy:
    // TODO: Support PCI access mechanism 1 (legacy PIO)
    return;
}

void pci_set_privl(struct pci_device *d, uint16_t flags) {
    uint16_t priv = PCI_READW(d, 0x4);
    priv &= ~0b111;
    priv |= flags & 0b111;
    PCI_WRITEW(d, 0x4, priv);
}

bool pci_enable_irq(struct pci_device *d, size_t index, int vec) {
    union msi_address addr = { .dest_id = this_cpu()->lapic_id, .base_address = 0xfee };  
    union msi_data data = { .vector = vec };

    if (d->msix_supported) {
        uint16_t control = PCI_READW(d, d->msix_offset + 2) | (1 << 15);
        uint16_t n_irqs  = (control & ((1 << 11) - 1)) + 1;
        uint32_t table_offset = PCI_READW(d, d->msix_offset + 8);
        if (index > n_irqs) {
            return false;
        }

        struct pci_bar bir = pci_get_bar(d, table_offset & 0b111);
        if (!bir.is_mmio || !bir.base) {
            return false;
        }

        uintptr_t target = bir.base + (table_offset & ~0b111);
        target += (index * sizeof(uint32_t) * 4) + VMM_HIGHER_HALF;

        ((uint32_t *)target)[0] = addr.raw;
        ((uint32_t *)target)[2] = data.raw;
        ((uint32_t *)target)[3] = 0; // Clear previous mask

        PCI_WRITEW(d, d->msix_offset + 2, control & ~(1 << 15));
    } else if (d->msi_supported) {
        uint16_t control = PCI_READW(d, d->msi_offset + 2) | 1;
        uint8_t data_off = (control & (1 << 7)) ? 0xc : 0x8;
        if ((control >> 1) & 0b111) {
            control &= ~(0b111 << 4); // Set MME to 0 (enable only 1 IRQ)
        }

        PCI_WRITED(d, d->msi_offset + 4, addr.raw);
        PCI_WRITEW(d, d->msi_offset + data_off, data.raw);
        PCI_WRITEW(d, d->msi_offset + 2, control);
    } else {
        return false;
    }

    return true;
}

bool pci_setmask(struct pci_device *d, size_t index, bool masked) {
    if (d->msix_supported) {
        uint16_t control = PCI_READW(d, d->msix_offset + 2);
        uint16_t n_irqs  = (control & ((1 << 11) - 1)) + 1;
        uint32_t table_offset = PCI_READW(d, d->msix_offset + 8);
        if (index > n_irqs) {
            return false;
        }

        struct pci_bar bir = pci_get_bar(d, table_offset & 0b111);
        if (!bir.is_mmio || !bir.base) {
            return false;
        }

        uintptr_t target = bir.base + (table_offset & ~0b111);
        target += (index * sizeof(uint32_t) * 4) + VMM_HIGHER_HALF;

        ((uint32_t *)target)[3] = (int)masked; // Set IRQ as masked

        PCI_WRITEW(d, d->msix_offset + 2, control);
    } if (d->msi_supported) {
        uint16_t control = PCI_READW(d, d->msi_offset + 2);
        
        if (masked) {
            PCI_WRITEW(d, d->msi_offset + 2, control & ~1);
        } else {
            PCI_WRITEW(d, d->msi_offset + 2, control | 1);
        }
    } else {
        return false;
    }

    return true;
}

struct pci_bar pci_get_bar(struct pci_device *d, uint8_t index) {
    struct pci_bar result = {0};

    if (index > 5) {
        return result;
    }

    uint16_t offset = 0x10 + index * sizeof(uint32_t);
    uint32_t base_low = PCI_READD(d, offset);
    PCI_WRITED(d, offset, ~0);
    uint32_t size_low = PCI_READD(d, offset);
    PCI_WRITED(d, offset, base_low);

    if (base_low & 1) {
        result.base = base_low & ~0b11;
        result.len  = ~(size_low & 0b11) + 1;
    } else {
        int type = (base_low >> 1) & 3;
        uint32_t base_high = PCI_READD(d, offset + 4);

        result.base = base_low & 0xfffffff0;
        if (type == 2) {
            result.base |= ((uint64_t)base_high << 32);
        }

        result.len = ~(size_low & 0b1111) + 1;
        result.is_mmio = true;
    }

    return result;
}

struct pci_device *pci_get_device(uint8_t class, uint8_t subclass, uint8_t prog_if, size_t index) {
    size_t count = 0;

    VECTOR_FOR_EACH(&devlist, device, 
        struct pci_device *dev = *device;

        if ((dev->pci_class == class) && (dev->subclass == subclass) && (dev->prog_if == prog_if)) {
            if (count++ == index) {
                return dev;
            }
        }
    );

    return NULL;
}

struct pci_device *pci_get_device_by_vendor(uint16_t vendor, uint16_t id, size_t index) {
    size_t count = 0;

    VECTOR_FOR_EACH(&devlist, device, 
        struct pci_device *dev = *device;

        if ((dev->vendor_id == vendor) && (dev->device_id == id)) {
            if (count++ == index) {
                return dev;
            }
        }
    );

    return NULL;
}

