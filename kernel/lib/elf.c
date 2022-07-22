#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <elf.h>
#include <lib/elf.h>
#include <lib/errno.h>
#include <lib/libc.h>
#include <lib/misc.h>
#include <lib/print.h>
#include <mm/mmap.h>
#include <mm/vmm.h>

bool elf_load(struct pagemap *pagemap, struct resource *res, uint64_t load_base,
              struct auxval *auxv, const char **ld_path) {
    Elf64_Ehdr header;
    if (res->read(res, &header, 0, sizeof(header)) < 0) {
        return false;
    }

    if (memcmp(header.e_ident, ELFMAG, 4) != 0) {
        errno = ENOEXEC;
        return false;
    }

    if (header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_ident[EI_OSABI] != 0 /* ELFOSABI_SYSV */ || header.e_machine != EM_X86_64) {
        errno = ENOEXEC;
        return false;
    }

    for (size_t i = 0; i < header.e_phnum; i++) {
        Elf64_Phdr phdr;
        if (res->read(res, &phdr, header.e_phoff + i * header.e_phentsize, sizeof(phdr)) < 0) {
            goto fail;
        }

        switch (phdr.p_type) {
            case PT_LOAD: {
                uintptr_t aligned_virt = ALIGN_DOWN(phdr.p_vaddr, PAGE_SIZE);

                size_t misalignment = phdr.p_vaddr - aligned_virt;
                size_t length = MAX(phdr.p_memsz, phdr.p_filesz) + misalignment;
                size_t file_offset = phdr.p_offset + misalignment;

                int prot = PROT_READ;
                if (phdr.p_flags & PF_W) {
                    prot |= PROT_WRITE;
                }
                if (phdr.p_flags & PF_X) {
                    prot |= PROT_EXEC;
                }

                if (mmap(pagemap, aligned_virt + load_base, length, prot, MAP_FIXED, res, file_offset) == NULL) {
                    goto fail;
                }
                break;
            }
            case PT_PHDR:
                auxv->at_phdr = phdr.p_vaddr + load_base;
                break;
            case PT_INTERP: {
                void *path = alloc(phdr.p_filesz + 1);
                if (path == NULL) {
                    goto fail;
                }

                if (res->read(res, path, phdr.p_offset, phdr.p_filesz) < 0) {
                    free(path);
                    goto fail;
                }

                if (ld_path != NULL) {
                    *ld_path = path;
                }
                break;
            }
        }
    }

    auxv->at_entry = header.e_entry + load_base;
    auxv->at_phent = header.e_phentsize;
    auxv->at_phnum = header.e_phnum;
    return true;

fail:
    if (ld_path != NULL && *ld_path != NULL) {
        free((void *)*ld_path);
    }
    return false;
}
