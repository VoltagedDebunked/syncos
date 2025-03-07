#include <syncos/pmm.h>
#include <limine.h>
#include <kstd/stdio.h>
#include <kstd/string.h>

// Static PMM configuration
static pmm_config_t pmm_config = {0};

// Use a larger static bitmap for managing more memory
#define STATIC_BITMAP_SIZE 8192 // 8KB = 64K pages = 256MB of memory
static uint8_t static_bitmap[STATIC_BITMAP_SIZE] = {0};
static uint8_t *page_bitmap = NULL;
static size_t bitmap_size = 0;

// Statistics tracking
static size_t total_allocations = 0;
static size_t failed_allocations = 0;

// Initialize the PMM with memory map data
void pmm_init(const struct limine_memmap_response *memmap, unsigned int flags) {
    printf("Initializing physical memory manager\n");
    
    // Fixed page size
    pmm_config.page_size = 4096;
    
    // Calculate how many pages we can track with our static bitmap
    pmm_config.max_pages = STATIC_BITMAP_SIZE * 8;
    
    // Set up our bitmap to point to the static array
    page_bitmap = static_bitmap;
    bitmap_size = STATIC_BITMAP_SIZE;
    
    // Use the memory map to find a good starting address
    uintptr_t start_address = 0x100000; // Default to 1MB if no better option
    size_t largest_usable_size = 0;
    
    // Count total memory and identify the largest contiguous usable region
    uintptr_t total_memory = 0;
    
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        
        // Log key memory regions
        if (entry->length >= 1024*1024) { // Only log regions 1MB or larger
            printf("Memory region: 0x%lx - 0x%lx (%lu MB), Type: %lu\n", 
                  entry->base, entry->base + entry->length, 
                  entry->length / (1024*1024),
                  entry->type);
        }
              
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_memory += entry->length;
            
            // Find the largest usable region bigger than 1MB that starts above 1MB
            if (entry->length > largest_usable_size && entry->base >= 0x100000) {
                largest_usable_size = entry->length;
                start_address = entry->base;
            }
        }
    }
    
    printf("Total memory: %lu MB\n", total_memory / (1024 * 1024));
    printf("Found usable region at 0x%lx (%lu MB)\n", 
           start_address, largest_usable_size / (1024 * 1024));
    
    // Store the base address for our managed memory region
    pmm_config.kernel_start = start_address;
    
    // Calculate how much memory we can actually manage
    size_t manageable_size = pmm_config.max_pages * pmm_config.page_size;
    if (manageable_size > largest_usable_size) {
        // Adjust downward if we can't track that much memory
        pmm_config.max_pages = largest_usable_size / pmm_config.page_size;
        printf("Adjusted max pages to %u to fit in available memory\n", 
               pmm_config.max_pages);
    }
    
    // Calculate the end address of our managed memory region
    pmm_config.kernel_end = start_address + (pmm_config.max_pages * pmm_config.page_size);
    
    // Initialize all memory as free (already zeroed in our static array)
    
    // Now mark specific regions as used based on memory map
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        
        // Skip usable regions and regions outside our management range
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            continue;
        }
        
        // Skip regions completely outside our managed range
        if (entry->base >= pmm_config.kernel_end || 
            entry->base + entry->length <= pmm_config.kernel_start) {
            continue;
        }
        
        // Calculate overlap with our managed memory
        uintptr_t region_start = entry->base;
        uintptr_t region_end = entry->base + entry->length;
        
        // Adjust to fit within our managed range
        if (region_start < pmm_config.kernel_start) {
            region_start = pmm_config.kernel_start;
        }
        
        if (region_end > pmm_config.kernel_end) {
            region_end = pmm_config.kernel_end;
        }
        
        // Mark the overlapping part as used
        for (uintptr_t addr = region_start; addr < region_end; addr += pmm_config.page_size) {
            // Calculate offset from base
            uintptr_t offset = addr - pmm_config.kernel_start;
            size_t page_index = offset / pmm_config.page_size;
            
            // Mark as used in bitmap
            size_t byte_index = page_index / 8;
            uint8_t bit_mask = 1 << (page_index % 8);
            page_bitmap[byte_index] |= bit_mask;
        }
    }
    
    // Also mark the first few pages as used to avoid conflicts with low memory
    for (size_t i = 0; i < 256 && i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        page_bitmap[byte_index] |= bit_mask;
    }
    
    // Store total memory size
    pmm_config.total_memory = total_memory;
    
    printf("PMM managing memory from 0x%lx to 0x%lx (%u MB)\n", 
           pmm_config.kernel_start, pmm_config.kernel_end, 
           (unsigned int)((pmm_config.kernel_end - pmm_config.kernel_start) / (1024 * 1024)));
}

// Allocate a single physical page
uintptr_t pmm_alloc_page(void) {
    if (!page_bitmap) {
        return 0; // PMM not initialized
    }
    
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        // Check if page is free
        if (!(page_bitmap[byte_index] & bit_mask)) {
            // Mark as used
            page_bitmap[byte_index] |= bit_mask;
            
            // Calculate physical address
            uintptr_t phys_addr = pmm_config.kernel_start + (i * pmm_config.page_size);
            
            total_allocations++;
            return phys_addr;
        }
    }
    
    failed_allocations++;
    return 0; // No free pages
}

// Allocate multiple consecutive physical pages
uintptr_t pmm_alloc_pages(size_t count) {
    if (!page_bitmap || count == 0) {
        return 0;
    }
    
    // For small allocations, use the simple approach
    if (count == 1) {
        return pmm_alloc_page();
    }
    
    // For larger allocations, find a continuous run of free pages
    size_t found_pages = 0;
    size_t start_page = 0;
    
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        if (!(page_bitmap[byte_index] & bit_mask)) {
            // This page is free
            if (found_pages == 0) {
                // Start of a new run
                start_page = i;
            }
            
            found_pages++;
            
            if (found_pages == count) {
                // Found enough consecutive pages, mark them all as used
                for (size_t j = 0; j < count; j++) {
                    size_t page_idx = start_page + j;
                    size_t byte_idx = page_idx / 8;
                    uint8_t mask = 1 << (page_idx % 8);
                    page_bitmap[byte_idx] |= mask;
                }
                
                // Calculate physical address
                total_allocations++;
                return pmm_config.kernel_start + (start_page * pmm_config.page_size);
            }
        } else {
            // This page is used, reset counter
            found_pages = 0;
        }
    }
    
    failed_allocations++;
    return 0; // Couldn't find enough consecutive pages
}

// Free a physical page
void pmm_free_page(uintptr_t page) {
    if (!page_bitmap) {
        return;
    }
    
    // Check if this page is in our managed range
    if (page < pmm_config.kernel_start || 
        page >= pmm_config.kernel_end ||
        (page % pmm_config.page_size) != 0) {
        return;
    }
    
    // Calculate the page index
    size_t page_index = (page - pmm_config.kernel_start) / pmm_config.page_size;
    size_t byte_index = page_index / 8;
    uint8_t bit_mask = 1 << (page_index % 8);
    
    // Mark as free
    page_bitmap[byte_index] &= ~bit_mask;
}

// Free multiple consecutive physical pages
void pmm_free_pages(uintptr_t page, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_page(page + (i * pmm_config.page_size));
    }
}

// Check if a page is free
bool pmm_is_page_free(uintptr_t page) {
    if (!page_bitmap) {
        return false;
    }
    
    // Check if this page is in our managed range
    if (page < pmm_config.kernel_start || 
        page >= pmm_config.kernel_end ||
        (page % pmm_config.page_size) != 0) {
        return false;
    }
    
    // Calculate the page index
    size_t page_index = (page - pmm_config.kernel_start) / pmm_config.page_size;
    size_t byte_index = page_index / 8;
    uint8_t bit_mask = 1 << (page_index % 8);
    
    return !(page_bitmap[byte_index] & bit_mask);
}

// Get total free memory
size_t pmm_get_free_memory(void) {
    if (!page_bitmap) {
        return 0;
    }
    
    size_t free_pages = 0;
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        if (!(page_bitmap[byte_index] & bit_mask)) {
            free_pages++;
        }
    }
    
    return free_pages * pmm_config.page_size;
}

// Get total used memory
size_t pmm_get_used_memory(void) {
    if (!page_bitmap) {
        return 0;
    }
    
    size_t used_pages = 0;
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        if (page_bitmap[byte_index] & bit_mask) {
            used_pages++;
        }
    }
    
    return used_pages * pmm_config.page_size;
}

// Get memory information
void pmm_get_info(pmm_config_t *config) {
    if (config) {
        *config = pmm_config;
    }
}

// Debug function to dump bitmap info
void pmm_dump_bitmap(void) {
    if (!page_bitmap) {
        printf("PMM not initialized\n");
        return;
    }
    
    size_t free_pages = 0;
    size_t used_pages = 0;
    
    for (size_t i = 0; i < pmm_config.max_pages; i++) {
        size_t byte_index = i / 8;
        uint8_t bit_mask = 1 << (i % 8);
        
        if (page_bitmap[byte_index] & bit_mask) {
            used_pages++;
        } else {
            free_pages++;
        }
    }
    
    printf("PMM Statistics:\n");
    printf("  Total pages: %u\n", pmm_config.max_pages);
    printf("  Used pages: %zu (%zu MB)\n", used_pages, (used_pages * pmm_config.page_size) / (1024 * 1024));
    printf("  Free pages: %zu (%zu MB)\n", free_pages, (free_pages * pmm_config.page_size) / (1024 * 1024));
    printf("  Total allocations: %zu\n", total_allocations);
    printf("  Failed allocations: %zu\n", failed_allocations);
    printf("  Memory range: 0x%lx - 0x%lx\n", pmm_config.kernel_start, pmm_config.kernel_end);
}