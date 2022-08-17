#include <dev/pci.h>
#include <acpi/acpi.h>
#include <mm/vmm.h>
#include <lib/vector.h>
#include <lib/print.h>

struct mcfg_entry {
    uint64_t mmio_base;
    uint16_t segment;
    uint8_t  start;
    uint8_t  end;
    uint32_t reserved;
};

static uint32_t (*read_func)(struct pci_device*, uint32_t, int);
static void (*write_func)(struct pci_device*, uint32_t, uint32_t, int);

static VECTOR_TYPE(struct mcfg_entry) mcfg_entries = VECTOR_INIT;
static VECTOR_TYPE(struct pci_device*) devlist = VECTOR_INIT;
static void scan_bus(uint8_t bus);

static uint32_t mcfg_read(struct pci_device* dev, uint32_t offset, int access_size) {
    VECTOR_FOR_EACH(&mcfg_entries, ent,
	if (dev->seg != ent->segment)
            continue;
	else if ((dev->bus > ent->end) || (dev->bus < ent->start))
            continue;

	uintptr_t target = ((dev->bus - ent->start) << 20) | (dev->slot << 15) | (dev->func << 12);
	target += ent->mmio_base + offset + VMM_HIGHER_HALF;

        switch (access_size) {
            case 1:
                return *(uint8_t*)target;
            case 2:
                return *(uint16_t*)target;
            case 4:
                return *(uint32_t*)target;
	}
    );

    return 0;
}

static void mcfg_write(struct pci_device* dev, uint32_t offset, uint32_t value, int access_size) {
    VECTOR_FOR_EACH(&mcfg_entries, ent,
	if (dev->seg != ent->segment)
            continue;
	else if ((dev->bus > ent->end) || (dev->bus < ent->start))
            continue;

	uintptr_t target = ((dev->bus - ent->start) << 20) | (dev->slot << 15) | (dev->func << 12);
	target += ent->mmio_base + offset + VMM_HIGHER_HALF;
        
	switch (access_size) {
            case 1:
                *(uint8_t*)target = (uint8_t)value;
		return;
            case 2:
                *(uint16_t*)target = (uint16_t)value;
		return;
            case 4:
                *(uint32_t*)target = (uint32_t)value;
		return;
	}
    );
}

uint32_t pci_read(struct pci_device* d, uint32_t off, int access_size) {
    return read_func(d, off, access_size);
}

void pci_write(struct pci_device* d, uint32_t off, uint32_t val, int access_size) {
    return write_func(d, off, val, access_size);
}

static void scan_function(uint8_t bus, uint8_t slot, uint8_t func) {
    struct pci_device* dev = ALLOC(struct pci_device, ALLOC_MISC);
    *dev = (struct pci_device) {
        .bus = bus,
	.slot = slot,
	.func = func
    };

    if (pci_readd(dev, 0) == 0xFFFFFFFF) {
        FREE(dev, ALLOC_MISC);
	return;
    }

    uint32_t reg_0 = pci_readd(dev, 0);
    uint32_t reg_2 = pci_readd(dev, 2 * sizeof(uint32_t));

    dev->device_id = (uint16_t)(reg_0 >> 16);
    dev->vendor_id = (uint16_t)reg_0;
    dev->rev_id    = (uint8_t)reg_2;
    dev->subclass  = (uint8_t)(reg_2 >> 16);
    dev->dev_class = (uint8_t)(reg_2 >> 24);
    dev->prog_if   = (uint8_t)(reg_2 >> 8);

    if (dev->dev_class == 0x06 && dev->subclass == 0x04) {
        // Check if there are more devices hidden behind this bridge
        uint32_t reg_6 = pci_readd(dev, 6 * sizeof(uint32_t));
        scan_bus((reg_6 >> 8) & 0xFF);
    }

    uint16_t sreg = pci_readw(dev, 6);
    if (sreg & (1 << 4)) {
        // Traverse the caps list, looking for MSI/MSI-X
        uint8_t next_off = pci_readb(dev, 0x34);

        while (next_off) {
            uint8_t ident = pci_readb(dev, next_off);

            switch (ident) {
                case 5: // MSI compatible
                    dev->msi_supported = true;
                    dev->msi_offset = next_off;
                    break;
	    }

            next_off = pci_readb(dev, next_off + 1);
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
    
    if (!(pci_readd(&root_dev, 0xC) & 0x800000)) {
        // Only one PCI host controller
        scan_bus(0);
    } else {
        // Multiple PCI host controllers
        for (int i = 0; i < 8; i++) {
            root_dev.func = i;

	    if (pci_readd(&root_dev, 0x0) == 0xffffffff)
                continue;

	    scan_bus(i);
	}
    }

    kernel_print("pci: detected devices:\n");
    VECTOR_FOR_EACH(&devlist, device,
        struct pci_device* dev = *device; 
        kernel_print("  - %d:%d:%d %x:%x %d:%d:%d\n", 
                     dev->bus, dev->slot, dev->func,
                     dev->device_id, dev->vendor_id,
                     dev->dev_class, dev->subclass, dev->prog_if);

        if (dev->msi_supported)
            kernel_print("    * MSI Supported\n");
    );
}

void pci_init(void) {
    // Try to use the MCFG when possible
    struct sdt* mcfg = acpi_find_sdt("MCFG", 0);
    if (mcfg == NULL)
      goto legacy;

    int entries_count = (mcfg->length - 44) / 16;
    if (entries_count == 0)
      goto legacy;

    struct mcfg_entry* mcfg_base = (void*)((uintptr_t)mcfg + sizeof(struct sdt) + sizeof(uint64_t));
    for (int i = 0; i < entries_count; i++) {
      struct mcfg_entry ent = mcfg_base[i];
      VECTOR_PUSH_BACK(&mcfg_entries, ent); 
      kernel_print("pci: found ECAM space for segment %d, bus range %d-%d\n", ent.segment, ent.start, ent.end);
    }

    read_func = mcfg_read;
    write_func = mcfg_write;
    scan_root_bus();

legacy:
    return;
}

