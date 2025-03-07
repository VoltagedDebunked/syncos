#ifndef _SYNCOS_PMM_H
#define _SYNCOS_PMM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

extern struct limine_memmap_response;

// Bitmap-based physical memory manager

// PMM initialization flags
#define PMM_FLAG_ZERO_PAGES    (1 << 0)  // Zero out pages on allocation
#define PMM_FLAG_CLEAR_BITMAP  (1 << 1)  // Clear bitmap during initialization

// Physical page information
typedef struct {
    uintptr_t address;       // Physical address of the page
    uint64_t flags;          // Page flags (reserved, used, etc.)
} pmm_page_info_t;

// PMM configuration structure
typedef struct {
    uintptr_t total_memory;         // Total physical memory
    uintptr_t kernel_start;         // Kernel start address
    uintptr_t kernel_end;           // Kernel end address
    size_t    page_size;            // Page size (typically 4096)
    uint32_t  max_pages;            // Maximum number of pages
} pmm_config_t;

// Initialize the physical memory manager
void pmm_init(const struct limine_memmap_response *memmap, unsigned int flags);

// Allocate a single physical page
uintptr_t pmm_alloc_page(void);

// Allocate multiple consecutive physical pages
uintptr_t pmm_alloc_pages(size_t count);

// Free a single physical page
void pmm_free_page(uintptr_t page);

// Free multiple consecutive physical pages
void pmm_free_pages(uintptr_t page, size_t count);

// Check if a page is free
bool pmm_is_page_free(uintptr_t page);

// Get total free memory
size_t pmm_get_free_memory(void);

// Get total used memory
size_t pmm_get_used_memory(void);

// Get memory information
void pmm_get_info(pmm_config_t *config);

// Debug functions
void pmm_dump_bitmap(void);

#endif // _SYNCOS_PMM_H