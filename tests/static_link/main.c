// Test: -static fully static ELF verification
//
// Compile: ccc -static -o test main.c
// This test verifies that when compiled with -static, the resulting
// ELF binary has no PT_INTERP or PT_DYNAMIC program headers,
// confirming it is fully statically linked with no dynamic dependencies.
//
// The program reads its own ELF binary via /proc/self/exe and parses the
// program header table using raw byte reads. This approach is architecture-
// independent, working correctly on both ELF32 (i686) and ELF64 (x86-64,
// AArch64, RISC-V 64) without relying on struct layouts or sizeof.
//
// ELF program header types checked:
//   PT_INTERP  (3) — dynamic linker path; absent in static binaries
//   PT_DYNAMIC (2) — dynamic linking info; absent in static binaries

#include <stdio.h>

// ELF constants — defined manually for portability (no <elf.h> dependency).
// These values are fixed by the ELF specification and identical across all
// architectures and ELF classes.
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define ELFCLASS32 1
#define ELFCLASS64 2

// ---------------------------------------------------------------------------
// Helper functions for reading little-endian values from a byte buffer.
// All four CCC target architectures (x86-64, AArch64, RISC-V 64, i686)
// are little-endian, so these helpers work correctly on all targets.
// ---------------------------------------------------------------------------

// Read a 2-byte little-endian unsigned value from buf at the given offset.
static unsigned int read_u16_le(const unsigned char *buf, int offset) {
    return (unsigned int)buf[offset]
         | ((unsigned int)buf[offset + 1] << 8);
}

// Read a 4-byte little-endian unsigned value from buf at the given offset.
static unsigned long read_u32_le(const unsigned char *buf, int offset) {
    return (unsigned long)buf[offset]
         | ((unsigned long)buf[offset + 1] << 8)
         | ((unsigned long)buf[offset + 2] << 16)
         | ((unsigned long)buf[offset + 3] << 24);
}

// Read an 8-byte little-endian unsigned value from buf at the given offset.
// Used for ELF64 e_phoff which is a 64-bit file offset.
static unsigned long read_u64_le(const unsigned char *buf, int offset) {
    unsigned long lo = read_u32_le(buf, offset);
    unsigned long hi = read_u32_le(buf, offset + 4);
    return lo | (hi << 32);
}

int main(void) {
    // Open our own executable via Linux procfs.
    // /proc/self/exe is a symlink to the currently running binary.
    FILE *f = fopen("/proc/self/exe", "rb");
    if (!f) {
        printf("FAIL: cannot open /proc/self/exe\n");
        return 1;
    }

    // Read the ELF header. 64 bytes is sufficient for both ELF32 (52 bytes)
    // and ELF64 (64 bytes) headers.
    unsigned char ehdr[64];
    if (fread(ehdr, 1, 64, f) < 52) {
        printf("FAIL: cannot read ELF header\n");
        fclose(f);
        return 1;
    }

    // Verify ELF magic number: 0x7f 'E' 'L' 'F'
    if (ehdr[0] != 0x7f || ehdr[1] != 'E' || ehdr[2] != 'L' || ehdr[3] != 'F') {
        printf("FAIL: not an ELF file\n");
        fclose(f);
        return 1;
    }

    // Determine ELF class (32-bit or 64-bit) from e_ident[EI_CLASS] at byte 4.
    unsigned char elf_class = ehdr[4];

    // Extract program header table location and dimensions based on ELF class.
    //
    // ELF64 layout (e_phoff at offset 32, 8 bytes):
    //   e_phoff:    bytes 32-39  (Elf64_Off, 8 bytes)
    //   e_phentsize: bytes 54-55 (Elf64_Half, 2 bytes)
    //   e_phnum:    bytes 56-57  (Elf64_Half, 2 bytes)
    //
    // ELF32 layout (e_phoff at offset 28, 4 bytes):
    //   e_phoff:    bytes 28-31  (Elf32_Off, 4 bytes)
    //   e_phentsize: bytes 42-43 (Elf32_Half, 2 bytes)
    //   e_phnum:    bytes 44-45  (Elf32_Half, 2 bytes)
    unsigned long phoff;
    unsigned int phentsize;
    unsigned int phnum;

    if (elf_class == ELFCLASS64) {
        phoff     = read_u64_le(ehdr, 32);
        phentsize = read_u16_le(ehdr, 54);
        phnum     = read_u16_le(ehdr, 56);
    } else if (elf_class == ELFCLASS32) {
        phoff     = read_u32_le(ehdr, 28);
        phentsize = read_u16_le(ehdr, 42);
        phnum     = read_u16_le(ehdr, 44);
    } else {
        printf("FAIL: unknown ELF class %d\n", (int)elf_class);
        fclose(f);
        return 1;
    }

    // Sanity check: program header entry size should be reasonable.
    // ELF32 phentsize is 32, ELF64 phentsize is 56.
    if (phentsize == 0 || phentsize > 64) {
        printf("FAIL: invalid phentsize %u\n", phentsize);
        fclose(f);
        return 1;
    }

    // Seek to the start of the program header table.
    if (fseek(f, (long)phoff, 0) != 0) {
        printf("FAIL: cannot seek to program headers at offset %lu\n", phoff);
        fclose(f);
        return 1;
    }

    // Iterate through all program headers and check for PT_INTERP and PT_DYNAMIC.
    // The p_type field is at offset 0 in each program header entry for both
    // ELF32 (Elf32_Phdr) and ELF64 (Elf64_Phdr), and is always a 4-byte LE uint.
    int found_interp = 0;
    int found_dynamic = 0;
    unsigned char phdr_buf[64];

    for (unsigned int i = 0; i < phnum; i++) {
        if (fread(phdr_buf, 1, phentsize, f) != phentsize) {
            printf("FAIL: cannot read program header %u\n", i);
            fclose(f);
            return 1;
        }

        // p_type is the first 4 bytes of every program header entry.
        unsigned long p_type = read_u32_le(phdr_buf, 0);

        if (p_type == PT_INTERP) {
            found_interp = 1;
        }
        if (p_type == PT_DYNAMIC) {
            found_dynamic = 1;
        }
    }

    fclose(f);

    // Report results. A fully static binary must have neither PT_INTERP
    // (no dynamic linker path) nor PT_DYNAMIC (no .dynamic section).
    if (!found_interp && !found_dynamic) {
        printf("PASS: static binary (no PT_INTERP, no PT_DYNAMIC)\n");
        return 0;
    }

    // If we reach here, the binary is not fully statically linked.
    if (found_interp) {
        printf("FAIL: found PT_INTERP (dynamic linker path present)\n");
    }
    if (found_dynamic) {
        printf("FAIL: found PT_DYNAMIC (dynamic section present)\n");
    }
    return 1;
}
