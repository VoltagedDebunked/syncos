#ifndef _SYNCOS_ELF_H
#define _SYNCOS_ELF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ELF file types
#define ET_NONE   0       // No file type
#define ET_REL    1       // Relocatable file
#define ET_EXEC   2       // Executable file
#define ET_DYN    3       // Shared object file
#define ET_CORE   4       // Core file
#define ET_LOPROC 0xFF00  // Processor-specific
#define ET_HIPROC 0xFFFF  // Processor-specific

// ELF machine types (partial list of common architectures)
#define EM_NONE     0  // No machine
#define EM_X86_64  62  // AMD x86-64 architecture

// ELF class types
#define ELFCLASSNONE 0  // Invalid class
#define ELFCLASS32   1  // 32-bit objects
#define ELFCLASS64   2  // 64-bit objects

// ELF data encoding
#define ELFDATANONE 0  // Invalid data encoding
#define ELFDATA2LSB 1  // 2's complement, little endian
#define ELFDATA2MSB 2  // 2's complement, big endian

// ELF program header types
#define PT_NULL         0  // Unused entry
#define PT_LOAD         1  // Loadable segment
#define PT_DYNAMIC      2  // Dynamic linking information
#define PT_INTERP       3  // Interpreter pathname
#define PT_NOTE         4  // Auxiliary information
#define PT_SHLIB        5  // Reserved
#define PT_PHDR         6  // Program header table
#define PT_TLS          7  // Thread-local storage
#define PT_LOOS  0x60000000  // OS-specific
#define PT_HIOS  0x6FFFFFFF  // OS-specific
#define PT_LOPROC 0x70000000  // Processor-specific
#define PT_HIPROC 0x7FFFFFFF  // Processor-specific

// ELF program header flags
#define PF_X        0x1      // Executable
#define PF_W        0x2      // Writable
#define PF_R        0x4      // Readable
#define PF_MASKOS   0x0FF00000  // OS-specific
#define PF_MASKPROC 0xF0000000  // Processor-specific

// ELF section header types
#define SHT_NULL     0  // Inactive section
#define SHT_PROGBITS 1  // Program data
#define SHT_SYMTAB   2  // Symbol table
#define SHT_STRTAB   3  // String table
#define SHT_RELA     4  // Relocation entries with addends
#define SHT_HASH     5  // Symbol hash table
#define SHT_DYNAMIC  6  // Dynamic linking information
#define SHT_NOTE     7  // Notes
#define SHT_NOBITS   8  // Program space with no data (bss)
#define SHT_REL      9  // Relocation entries, no addends
#define SHT_SHLIB   10  // Reserved
#define SHT_DYNSYM  11  // Dynamic linker symbol table

// ELF section header flags
#define SHF_WRITE     0x1        // Writable
#define SHF_ALLOC     0x2        // Occupies memory during execution
#define SHF_EXECINSTR 0x4        // Executable
#define SHF_MERGE     0x10       // Might be merged
#define SHF_STRINGS   0x20       // Contains null-terminated strings
#define SHF_INFO_LINK 0x40       // sh_info contains SHT index
#define SHF_LINK_ORDER 0x80      // Preserve order after combining
#define SHF_MASKOS    0x0F000000 // OS-specific
#define SHF_MASKPROC  0xF0000000 // Processor-specific

// ELF 64-bit header structure
typedef struct {
    unsigned char e_ident[16];  // ELF identification
    uint16_t e_type;            // Object file type
    uint16_t e_machine;         // Machine type
    uint32_t e_version;         // Object file version
    uint64_t e_entry;           // Entry point address
    uint64_t e_phoff;           // Program header offset
    uint64_t e_shoff;           // Section header offset
    uint32_t e_flags;           // Processor-specific flags
    uint16_t e_ehsize;          // ELF header size
    uint16_t e_phentsize;       // Program header entry size
    uint16_t e_phnum;           // Program header entry count
    uint16_t e_shentsize;       // Section header entry size
    uint16_t e_shnum;           // Section header entry count
    uint16_t e_shstrndx;        // Section name string table index
} elf64_header_t;

// ELF 64-bit program header structure
typedef struct {
    uint32_t p_type;            // Segment type
    uint32_t p_flags;           // Segment flags
    uint64_t p_offset;          // Segment file offset
    uint64_t p_vaddr;           // Segment virtual address
    uint64_t p_paddr;           // Segment physical address
    uint64_t p_filesz;          // Segment size in file
    uint64_t p_memsz;           // Segment size in memory
    uint64_t p_align;           // Segment alignment
} elf64_program_header_t;

// ELF 64-bit section header structure
typedef struct {
    uint32_t sh_name;           // Section name (string table index)
    uint32_t sh_type;           // Section type
    uint64_t sh_flags;          // Section flags
    uint64_t sh_addr;           // Section virtual addr at execution
    uint64_t sh_offset;         // Section file offset
    uint64_t sh_size;           // Section size in bytes
    uint32_t sh_link;           // Link to another section
    uint32_t sh_info;           // Additional section information
    uint64_t sh_addralign;      // Section alignment
    uint64_t sh_entsize;        // Entry size if section holds table
} elf64_section_header_t;

// ELF 64-bit symbol table entry
typedef struct {
    uint32_t st_name;           // Symbol name (string table index)
    uint8_t  st_info;           // Symbol type and binding
    uint8_t  st_other;          // Symbol visibility
    uint16_t st_shndx;          // Section index
    uint64_t st_value;          // Symbol value
    uint64_t st_size;           // Symbol size
} elf64_symbol_t;

// ELF parser specific structures
typedef struct {
    elf64_header_t header;
    elf64_program_header_t* program_headers;
    elf64_section_header_t* section_headers;
    const char* section_name_table;
    
    // Buffer info
    const void* data;
    size_t size;
    
    // Memory management functions
    void* (*alloc_pages)(size_t page_count);
    void (*free_pages)(void* addr, size_t page_count);
    
    // Loaded segments info
    struct {
        void* vaddr;
        size_t size;
    } loaded_segments[16];
    size_t loaded_segment_count;
    
    uint64_t base_address;
    uint64_t entry_point;
} elf_context_t;

// Function prototypes

/**
 * Initialize an ELF context
 * @param ctx Pointer to the context to initialize
 * @param data Pointer to the ELF data buffer
 * @param size Size of the ELF data buffer
 * @param alloc_pages Function to allocate pages
 * @param free_pages Function to free pages
 * @return true if context initialized successfully, false otherwise
 */
bool elf_init(elf_context_t* ctx, const void* data, size_t size, 
              void* (*alloc_pages)(size_t), void (*free_pages)(void*, size_t));

/**
 * Load ELF segments into memory
 * @param ctx Pointer to the ELF context
 * @param base_addr Base address for loading (0 for no relocation)
 * @return true if loaded successfully, false otherwise
 */
bool elf_load(elf_context_t* ctx, uint64_t base_addr);

/**
 * Execute a loaded ELF file
 * @param ctx Pointer to the ELF context
 * @param argc Argument count
 * @param argv Argument vector 
 * @param envp Environment pointer
 * @return Exit status from the executed program (usually doesn't return)
 */
int elf_execute(elf_context_t* ctx, int argc, char* argv[], char* envp[]);

/**
 * Clean up ELF context and free resources
 * @param ctx Pointer to the ELF context
 */
void elf_cleanup(elf_context_t* ctx);

/**
 * Get ELF entry point
 * @param ctx Pointer to the ELF context
 * @return Entry point virtual address
 */
uint64_t elf_get_entry_point(elf_context_t* ctx);

/**
 * Find a section by name
 * @param ctx Pointer to the ELF context
 * @param name Name of the section to find
 * @param header Pointer to store the found section header
 * @return Pointer to section data, NULL if not found
 */
const void* elf_find_section(elf_context_t* ctx, const char* name, elf64_section_header_t* header);

/**
 * Check if the given buffer contains a valid ELF file
 * @param data Pointer to the data buffer
 * @param size Size of the data buffer
 * @return true if valid ELF, false otherwise
 */
bool elf_is_valid(const void* data, size_t size);

#endif // _SYNCOS_ELF_H