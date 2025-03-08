#include <syncos/elf.h>
#include <kstd/string.h>
#include <kstd/stdio.h>
#include <syncos/vmm.h>
#include <syncos/pmm.h>

// ELF identification indices
#define EI_MAG0       0  // File identification
#define EI_MAG1       1  // File identification
#define EI_MAG2       2  // File identification
#define EI_MAG3       3  // File identification
#define EI_CLASS      4  // File class
#define EI_DATA       5  // Data encoding
#define EI_VERSION    6  // File version
#define EI_OSABI      7  // OS/ABI identification
#define EI_ABIVERSION 8  // ABI version
#define EI_PAD        9  // Start of padding bytes
#define EI_NIDENT    16  // Size of e_ident[]

// ELF magic number
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

// ELF ABI types
#define ELFOSABI_NONE     0  // UNIX System V ABI
#define ELFOSABI_LINUX    3  // Linux ABI

// Debug macro
#define ELF_DEBUG 1
#ifdef ELF_DEBUG
#define ELF_LOG(fmt, ...) printf("ELF: " fmt "\n", ##__VA_ARGS__)
#else
#define ELF_LOG(fmt, ...)
#endif

// Internal helper functions

static bool validate_elf_header(const elf64_header_t* header) {
    // Check ELF magic number
    if (header->e_ident[EI_MAG0] != ELFMAG0 ||
        header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 ||
        header->e_ident[EI_MAG3] != ELFMAG3) {
        ELF_LOG("Invalid ELF magic number");
        return false;
    }
    
    // Check class (64-bit)
    if (header->e_ident[EI_CLASS] != ELFCLASS64) {
        ELF_LOG("Not a 64-bit ELF file");
        return false;
    }
    
    // Check data encoding (little endian for x86_64)
    if (header->e_ident[EI_DATA] != ELFDATA2LSB) {
        ELF_LOG("Not a little-endian ELF file");
        return false;
    }
    
    // Check ELF version
    if (header->e_version != 1) {
        ELF_LOG("Invalid ELF version");
        return false;
    }
    
    // Check machine type (x86_64)
    if (header->e_machine != EM_X86_64) {
        ELF_LOG("Not an x86_64 ELF file (machine type 0x%x)", header->e_machine);
        return false;
    }
    
    // Check type (executable or shared object)
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        ELF_LOG("Not an executable or shared object ELF file (type 0x%x)", header->e_type);
        return false;
    }
    
    // Check for program headers
    if (header->e_phoff == 0 || header->e_phnum == 0) {
        ELF_LOG("No program headers found");
        return false;
    }
    
    // Check for valid entry point
    if (header->e_entry == 0) {
        ELF_LOG("Invalid entry point");
        return false;
    }
    
    return true;
}

static bool validate_elf_size(const elf_context_t* ctx) {
    const elf64_header_t* header = &ctx->header;
    
    // Check ELF header
    if (ctx->size < sizeof(elf64_header_t)) {
        ELF_LOG("ELF file too small for header");
        return false;
    }
    
    // Check program headers size
    size_t ph_size = header->e_phnum * header->e_phentsize;
    if (header->e_phoff + ph_size > ctx->size) {
        ELF_LOG("ELF file too small for program headers");
        return false;
    }
    
    // Check section headers size
    size_t sh_size = header->e_shnum * header->e_shentsize;
    if (header->e_shoff + sh_size > ctx->size) {
        ELF_LOG("ELF file too small for section headers");
        return false;
    }
    
    return true;
}

/**
 * Parse program headers from ELF file
 * @param ctx Pointer to the ELF context
 * @return true if parsed successfully, false otherwise
 */
static bool parse_program_headers(elf_context_t* ctx) {
    const elf64_header_t* header = &ctx->header;
    
    // Allocate memory for program headers
    ctx->program_headers = ctx->alloc_pages(
        (header->e_phnum * header->e_phentsize + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K);
    
    if (!ctx->program_headers) {
        ELF_LOG("Failed to allocate memory for program headers");
        return false;
    }
    
    // Copy program headers from ELF data
    memcpy(ctx->program_headers, 
           (const uint8_t*)ctx->data + header->e_phoff, 
           header->e_phnum * header->e_phentsize);
    
    return true;
}

/**
 * Parse section headers from ELF file
 * @param ctx Pointer to the ELF context
 * @return true if parsed successfully, false otherwise
 */
static bool parse_section_headers(elf_context_t* ctx) {
    const elf64_header_t* header = &ctx->header;
    
    // Allocate memory for section headers
    ctx->section_headers = ctx->alloc_pages(
        (header->e_shnum * header->e_shentsize + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K);
    
    if (!ctx->section_headers) {
        ELF_LOG("Failed to allocate memory for section headers");
        return false;
    }
    
    // Copy section headers from ELF data
    memcpy(ctx->section_headers, 
           (const uint8_t*)ctx->data + header->e_shoff, 
           header->e_shnum * header->e_shentsize);
    
    // Get section name string table
    if (header->e_shstrndx < header->e_shnum) {
        const elf64_section_header_t* shstrtab = &ctx->section_headers[header->e_shstrndx];
        ctx->section_name_table = (const char*)ctx->data + shstrtab->sh_offset;
    } else {
        ctx->section_name_table = NULL;
    }
    
    return true;
}

// Public functions implementation

bool elf_init(elf_context_t* ctx, const void* data, size_t size, 
              void* (*alloc_pages)(size_t), void (*free_pages)(void*, size_t)) {
    if (!ctx || !data || size < sizeof(elf64_header_t) || !alloc_pages || !free_pages) {
        return false;
    }
    
    // Clear context
    memset(ctx, 0, sizeof(elf_context_t));
    
    // Set data and size
    ctx->data = data;
    ctx->size = size;
    
    // Set memory management functions
    ctx->alloc_pages = alloc_pages;
    ctx->free_pages = free_pages;
    
    // Copy ELF header
    memcpy(&ctx->header, data, sizeof(elf64_header_t));
    
    // Validate ELF header
    if (!validate_elf_header(&ctx->header)) {
        return false;
    }
    
    // Validate ELF size
    if (!validate_elf_size(ctx)) {
        return false;
    }
    
    // Parse program headers
    if (!parse_program_headers(ctx)) {
        elf_cleanup(ctx);
        return false;
    }
    
    // Parse section headers
    if (!parse_section_headers(ctx)) {
        elf_cleanup(ctx);
        return false;
    }
    
    ELF_LOG("ELF file initialized successfully");
    ELF_LOG("  Entry point: 0x%lx", ctx->header.e_entry);
    ELF_LOG("  Program headers: %u", ctx->header.e_phnum);
    ELF_LOG("  Section headers: %u", ctx->header.e_shnum);
    
    return true;
}

bool elf_load(elf_context_t* ctx, uint64_t base_addr) {
    if (!ctx || !ctx->program_headers) {
        return false;
    }
    
    // Store base address
    ctx->base_address = base_addr;
    
    // Calculate load address adjustment for PIE executables
    uint64_t load_bias = 0;
    if (ctx->header.e_type == ET_DYN && base_addr != 0) {
        load_bias = base_addr;
    }
    
    // Process each program header
    for (uint16_t i = 0; i < ctx->header.e_phnum; i++) {
        const elf64_program_header_t* ph = &ctx->program_headers[i];
        
        // Only process loadable segments
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        
        // Calculate virtual address with load bias
        uint64_t vaddr = ph->p_vaddr + load_bias;
        
        // Calculate memory size (rounded up to page size)
        uint64_t mem_size = (ph->p_memsz + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
        
        ELF_LOG("Loading segment %d: vaddr=0x%lx, size=0x%lx, flags=0x%x", 
               i, vaddr, mem_size, ph->p_flags);
        
        // Allocate memory for the segment
        size_t page_count = mem_size / PAGE_SIZE_4K;
        void* segment_addr = ctx->alloc_pages(page_count);
        if (!segment_addr) {
            ELF_LOG("Failed to allocate memory for segment");
            return false;
        }
        
        // Clear the memory
        memset(segment_addr, 0, mem_size);
        
        // Copy segment data
        if (ph->p_filesz > 0) {
            size_t copy_size = ph->p_filesz;
            if (copy_size > mem_size) {
                copy_size = mem_size;
            }
            
            memcpy(segment_addr, (const uint8_t*)ctx->data + ph->p_offset, copy_size);
        }
        
        // Store loaded segment info
        if (ctx->loaded_segment_count < 16) {
            ctx->loaded_segments[ctx->loaded_segment_count].vaddr = segment_addr;
            ctx->loaded_segments[ctx->loaded_segment_count].size = mem_size;
            ctx->loaded_segment_count++;
        }
        
        // Map the memory to the virtual address
        uint32_t vm_flags = VMM_FLAG_PRESENT;
        if (ph->p_flags & PF_W) vm_flags |= VMM_FLAG_WRITABLE;
        if (!(ph->p_flags & PF_X)) vm_flags |= VMM_FLAG_NO_EXECUTE;
        if (ph->p_flags & PF_R) vm_flags |= VMM_FLAG_USER; // User-accessible
        
        // Map pages into virtual memory
        if (!vmm_map_pages(vaddr, (uintptr_t)segment_addr, page_count, vm_flags)) {
            ELF_LOG("Failed to map segment to virtual memory");
            ctx->free_pages(segment_addr, page_count);
            return false;
        }
    }
    
    // Calculate entry point with load bias
    ctx->entry_point = ctx->header.e_entry + load_bias;
    
    ELF_LOG("ELF loaded successfully, entry point: 0x%lx", ctx->entry_point);
    return true;
}

int elf_execute(elf_context_t* ctx, int argc, char* argv[], char* envp[]) {
    if (!ctx || ctx->entry_point == 0) {
        return -1;
    }
    
    // Define function type for entry point
    typedef int (*entry_func_t)(int, char**, char**);
    
    // Cast entry point to function pointer
    entry_func_t entry = (entry_func_t)ctx->entry_point;
    
    ELF_LOG("Jumping to entry point 0x%lx with %d arguments", ctx->entry_point, argc);
    
    // Call entry point
    return entry(argc, argv, envp);
}

void elf_cleanup(elf_context_t* ctx) {
    if (!ctx) {
        return;
    }
    
    // Free program headers
    if (ctx->program_headers) {
        ctx->free_pages(ctx->program_headers, 1);
        ctx->program_headers = NULL;
    }
    
    // Free section headers
    if (ctx->section_headers) {
        ctx->free_pages(ctx->section_headers, 1);
        ctx->section_headers = NULL;
    }
    
    // Free loaded segments
    for (size_t i = 0; i < ctx->loaded_segment_count; i++) {
        if (ctx->loaded_segments[i].vaddr) {
            // Unmap from virtual memory
            // Note: We don't know the exact virtual address, so we rely on
            // the VMM layer to handle cleanup of mappings
            
            // Free the physical memory
            size_t page_count = ctx->loaded_segments[i].size / PAGE_SIZE_4K;
            ctx->free_pages(ctx->loaded_segments[i].vaddr, page_count);
            ctx->loaded_segments[i].vaddr = NULL;
            ctx->loaded_segments[i].size = 0;
        }
    }
    
    ctx->loaded_segment_count = 0;
    ctx->entry_point = 0;
    ctx->base_address = 0;
    ctx->data = NULL;
    ctx->size = 0;
}

uint64_t elf_get_entry_point(elf_context_t* ctx) {
    if (!ctx) {
        return 0;
    }
    
    return ctx->entry_point != 0 ? ctx->entry_point : ctx->header.e_entry;
}

const void* elf_find_section(elf_context_t* ctx, const char* name, elf64_section_header_t* header) {
    if (!ctx || !name || !ctx->section_headers || !ctx->section_name_table) {
        return NULL;
    }
    
    // Search for section by name
    for (uint16_t i = 0; i < ctx->header.e_shnum; i++) {
        const elf64_section_header_t* sh = &ctx->section_headers[i];
        const char* section_name = ctx->section_name_table + sh->sh_name;
        
        if (strcmp(section_name, name) == 0) {
            // Found matching section
            if (header) {
                *header = *sh;
            }
            
            // Return pointer to section data
            return (const uint8_t*)ctx->data + sh->sh_offset;
        }
    }
    
    return NULL;
}

bool elf_is_valid(const void* data, size_t size) {
    if (!data || size < sizeof(elf64_header_t)) {
        return false;
    }
    
    const elf64_header_t* header = (const elf64_header_t*)data;
    
    // Check ELF magic number
    if (header->e_ident[EI_MAG0] != ELFMAG0 ||
        header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 ||
        header->e_ident[EI_MAG3] != ELFMAG3) {
        return false;
    }
    
    // Check class (64-bit)
    if (header->e_ident[EI_CLASS] != ELFCLASS64) {
        return false;
    }
    
    return true;
}