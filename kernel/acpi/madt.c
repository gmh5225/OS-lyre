#include <stddef.h>
#include <stdint.h>
#include <lib/print.h>
#include <lib/panic.h>
#include <lib/vector.h>
#include <acpi/acpi.h>
#include <acpi/madt.h>

__auto_type madt_lapics = (typeof(madt_lapics))VECTOR_INIT;
__auto_type madt_io_apics = (typeof(madt_io_apics))VECTOR_INIT;
__auto_type madt_isos = (typeof(madt_isos))VECTOR_INIT;
__auto_type madt_nmis = (typeof(madt_nmis))VECTOR_INIT;

struct madt {
    struct sdt;
    uint32_t local_controller_addr;
    uint32_t flags;
    char entries_data[];
};

void madt_init(void) {
    struct madt *madt = acpi_find_sdt("APIC", 0);
    if (madt == NULL) {
        panic(NULL, true, "System does not have an MADT");
    }

    size_t offset = 0;
    for (;;) {
        if (offset + sizeof(struct madt) - 1 >= madt->length) {
            break;
        }

        struct madt_header *header = (struct madt_header *)(madt->entries_data + offset);
        switch (header->id) {
            case 0:
                print("madt: Found local APIC #%lu\n", madt_lapics.length);
                VECTOR_PUSH_BACK(&madt_lapics, (struct madt_lapic *)header);
                break;
            case 1:
                print("madt: Found IO APIC #%lu\n", madt_io_apics.length);
                VECTOR_PUSH_BACK(&madt_io_apics, (struct madt_io_apic *)header);
                break;
            case 2:
                print("madt: Found ISO #%lu\n", madt_isos.length);
                VECTOR_PUSH_BACK(&madt_isos, (struct madt_iso *)header);
                break;
            case 4:
                print("madt: Found NMI #%lu\n", madt_nmis.length);
                VECTOR_PUSH_BACK(&madt_nmis, (struct madt_nmi *)header);
                break;
        }

        offset += header->length;
    }
}
