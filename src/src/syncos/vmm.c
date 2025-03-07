#include <syncos/vmm.h>
#include <syncos/pmm.h>
#include <syncos/idt.h>
#include <kstd/stdio.h>
#include <kstd/string.h>
#include <limine.h>

// Limine HHDM and memory map requests
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static volatile struct limine_kernel_address_request kernel_addr_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

// Page size definitions
#define PAGE_SIZE_4K  0x1000UL
#define PAGE_SIZE_2M  0x200000UL
#define PAGE_SIZE_1G  0x40000000UL

// Page table entry indices
#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)

// Page table entry flags
#define PAGE_PRESENT        (1UL << 0)
#define PAGE_WRITABLE       (1UL << 1)
#define PAGE_USER           (1UL << 2)
#define PAGE_WRITETHROUGH   (1UL << 3)
#define PAGE_CACHE_DISABLE  (1UL << 4)
#define PAGE_ACCESSED       (1UL << 5)
#define PAGE_DIRTY          (1UL << 6)
#define PAGE_HUGE           (1UL << 7)
#define PAGE_GLOBAL         (1UL << 8)
#define PAGE_NO_EXECUTE     (1UL << 63)

// Address mask for page tables
#define PAGE_ADDR_MASK ~0xFFFUL

// Physical memory regions for dynamic allocation
#define MAX_MEMORY_AREAS 32

typedef struct {
    uintptr_t base;
    size_t size;
    bool is_used;
    uint32_t flags;
} memory_area_t;

// VMM configuration and state
static vmm_config_t vmm_config;
static uint64_t hhdm_offset;
static uintptr_t kernel_phys_base;
static uintptr_t kernel_virt_base;
static uintptr_t current_pml4_phys;

// Virtual memory areas for allocation
static memory_area_t user_areas[MAX_MEMORY_AREAS];
static memory_area_t kernel_areas[MAX_MEMORY_AREAS];
static int user_area_count = 0;
static int kernel_area_count = 0;

// Statistics for memory usage
static struct {
    size_t pages_allocated;
    size_t pages_freed;
    size_t page_faults_handled;
} vmm_stats = {0};

// Forward declarations
static void* phys_to_virt(uintptr_t phys);
static uintptr_t virt_to_phys(void* virt);
static bool map_page_internal(uintptr_t pml4_phys, uintptr_t virt, uintptr_t phys, uint64_t flags);
static void register_memory_area(memory_area_t* areas, int* count, uintptr_t base, size_t size, uint32_t flags);
static void page_fault_handler(uint64_t error_code, uint64_t rip);

// Read CR3 register
static inline uintptr_t read_cr3(void) {
    uintptr_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

// Write CR3 register
static inline void write_cr3(uintptr_t cr3) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

// Invalidate TLB entry
static inline void invlpg(uintptr_t addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

// Convert physical address to virtual using Limine's HHDM
static void* phys_to_virt(uintptr_t phys) {
    if (phys == 0) return NULL;
    return (void*)(phys + hhdm_offset);
}

// Convert virtual address to physical
static uintptr_t virt_to_phys(void* virt) {
    uintptr_t addr = (uintptr_t)virt;
    
    // Handle higher half direct mapping
    if (addr >= hhdm_offset) {
        return addr - hhdm_offset;
    }
    
    // For other addresses, walk the page tables
    uintptr_t pml4_idx = PML4_INDEX(addr);
    uintptr_t pdpt_idx = PDPT_INDEX(addr);
    uintptr_t pd_idx = PD_INDEX(addr);
    uintptr_t pt_idx = PT_INDEX(addr);
    
    uint64_t* pml4 = (uint64_t*)phys_to_virt(current_pml4_phys);
    if (!pml4 || !(pml4[pml4_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & PAGE_ADDR_MASK);
    if (!pdpt || !(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    // Check for 1GB page
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        return (pdpt[pdpt_idx] & PAGE_ADDR_MASK) + (addr & 0x3FFFFFFF);
    }
    
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & PAGE_ADDR_MASK);
    if (!pd || !(pd[pd_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    // Check for 2MB page
    if (pd[pd_idx] & PAGE_HUGE) {
        return (pd[pd_idx] & PAGE_ADDR_MASK) + (addr & 0x1FFFFF);
    }
    
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & PAGE_ADDR_MASK);
    if (!pt || !(pt[pt_idx] & PAGE_PRESENT)) {
        return 0;
    }
    
    // 4KB page
    return (pt[pt_idx] & PAGE_ADDR_MASK) + (addr & 0xFFF);
}

// Create a new page table
static uintptr_t create_page_table(void) {
    uintptr_t page = pmm_alloc_page();
    if (page == 0) {
        return 0;
    }
    
    // Clear the page
    memset(phys_to_virt(page), 0, PAGE_SIZE_4K);
    return page;
}

// Map a page in the specified page table
static bool map_page_internal(uintptr_t pml4_phys, uintptr_t virt, uintptr_t phys, uint64_t flags) {
    if (!pml4_phys || !virt || !phys) {
        return false;
    }
    
    // Align addresses to page boundaries
    virt &= PAGE_ADDR_MASK;
    phys &= PAGE_ADDR_MASK;
    
    uint64_t* pml4 = (uint64_t*)phys_to_virt(pml4_phys);
    if (!pml4) {
        return false;
    }
    
    // Get indices
    uintptr_t pml4_idx = PML4_INDEX(virt);
    uintptr_t pdpt_idx = PDPT_INDEX(virt);
    uintptr_t pd_idx = PD_INDEX(virt);
    uintptr_t pt_idx = PT_INDEX(virt);
    
    // Ensure PDPT exists
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        uintptr_t pdpt_phys = create_page_table();
        if (pdpt_phys == 0) {
            return false;
        }
        pml4[pml4_idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE;
        
        // Add USER flag for user pages (lower half)
        if (virt < 0x8000000000000000UL) {
            pml4[pml4_idx] |= PAGE_USER;
        }
    }
    
    // Get PDPT
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & PAGE_ADDR_MASK);
    if (!pdpt) {
        return false;
    }
    
    // Handle 1GB pages
    if ((flags & VMM_FLAG_HUGE) && ((virt & 0x3FFFFFFF) == 0) && ((phys & 0x3FFFFFFF) == 0)) {
        // 1GB alignment
        pdpt[pdpt_idx] = phys | flags | PAGE_HUGE;
        invlpg(virt);
        return true;
    }
    
    // Ensure PD exists
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        uintptr_t pd_phys = create_page_table();
        if (pd_phys == 0) {
            return false;
        }
        pdpt[pdpt_idx] = pd_phys | PAGE_PRESENT | PAGE_WRITABLE;
        
        // Add USER flag for user pages
        if (virt < 0x8000000000000000UL) {
            pdpt[pdpt_idx] |= PAGE_USER;
        }
    }
    
    // Get PD
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & PAGE_ADDR_MASK);
    if (!pd) {
        return false;
    }
    
    // Handle 2MB pages
    if ((flags & VMM_FLAG_HUGE) && ((virt & 0x1FFFFF) == 0) && ((phys & 0x1FFFFF) == 0)) {
        // 2MB alignment
        pd[pd_idx] = phys | flags | PAGE_HUGE;
        invlpg(virt);
        return true;
    }
    
    // Ensure PT exists
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        uintptr_t pt_phys = create_page_table();
        if (pt_phys == 0) {
            return false;
        }
        pd[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE;
        
        // Add USER flag for user pages
        if (virt < 0x8000000000000000UL) {
            pd[pd_idx] |= PAGE_USER;
        }
    }
    
    // Get PT
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & PAGE_ADDR_MASK);
    if (!pt) {
        return false;
    }
    
    // Set the page entry
    pt[pt_idx] = phys | flags;
    
    // Invalidate TLB
    invlpg(virt);
    
    return true;
}

// Register a memory area for dynamic allocation
static void register_memory_area(memory_area_t* areas, int* count, uintptr_t base, size_t size, uint32_t flags) {
    if (*count >= MAX_MEMORY_AREAS) {
        printf("VMM: Too many memory areas\n");
        return;
    }
    
    areas[*count].base = base;
    areas[*count].size = size;
    areas[*count].flags = flags;
    areas[*count].is_used = false;
    (*count)++;
}

// Find a free memory area of the requested size
static memory_area_t* find_free_area(memory_area_t* areas, int count, size_t size, uint32_t flags) {
    for (int i = 0; i < count; i++) {
        if (!areas[i].is_used && areas[i].size >= size) {
            return &areas[i];
        }
    }
    return NULL;
}

// Page fault handler
static void page_fault_handler(uint64_t error_code, uint64_t rip) {
    uintptr_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    
    // Call VMM handler
    if (vmm_handle_page_fault(fault_addr, error_code)) {
        vmm_stats.page_faults_handled++;
        return;
    }
    
    // Unhandled - print information and panic
    printf("\n!!! PAGE FAULT !!!\n");
    printf("Fault address: 0x%lx\n", fault_addr);
    printf("Error code: 0x%lx (%s%s%s%s)\n", error_code,
           (error_code & 1) ? "PRESENT " : "NOT_PRESENT ",
           (error_code & 2) ? "WRITE " : "READ ",
           (error_code & 4) ? "USER " : "SUPERVISOR ",
           (error_code & 8) ? "RESERVED_BIT " : "");
    printf("Instruction pointer: 0x%lx\n", rip);
    
    // Is this address mapped?
    if (vmm_is_mapped(fault_addr)) {
        uintptr_t phys = vmm_get_physical_address(fault_addr);
        printf("Address mapped to physical: 0x%lx\n", phys);
    } else {
        printf("Address not mapped\n");
    }
    
    // Halt the system
    printf("System halted\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}

// Initialize the virtual memory manager
void vmm_init(void) {
    printf("Initializing VMM\n");
    
    // Get HHDM from Limine
    if (hhdm_request.response) {
        hhdm_offset = hhdm_request.response->offset;
        printf("HHDM offset: 0x%lx\n", hhdm_offset);
    } else {
        printf("WARNING: HHDM response not available, using default\n");
        hhdm_offset = 0xffff800000000000UL;
    }
    
    // Get kernel address info
    if (kernel_addr_request.response) {
        kernel_phys_base = kernel_addr_request.response->physical_base;
        kernel_virt_base = kernel_addr_request.response->virtual_base;
        printf("Kernel physical base: 0x%lx\n", kernel_phys_base);
        printf("Kernel virtual base: 0x%lx\n", kernel_virt_base);
    } else {
        printf("WARNING: Kernel address response not available\n");
        kernel_phys_base = 0x100000; // 1MB default
        kernel_virt_base = hhdm_offset + kernel_phys_base;
    }
    
    // Get CR3 (physical address of PML4)
    current_pml4_phys = read_cr3();
    printf("Current PML4 physical address: 0x%lx\n", current_pml4_phys);
    
    // Store configuration
    vmm_config.kernel_pml4 = current_pml4_phys;
    vmm_config.kernel_virtual_base = kernel_virt_base;
    vmm_config.kernel_virtual_size = 0x10000000; // 256MB
    
    // Check for NX bit support
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001));
    vmm_config.using_nx = (edx & (1 << 20)) != 0;
    printf("NX bit %s\n", vmm_config.using_nx ? "supported" : "not supported");
    
    // Register page fault handler
    idt_register_exception_handler(14, page_fault_handler);
    
    // Set up memory areas for kernel
    register_memory_area(kernel_areas, &kernel_area_count, 
                        hhdm_offset + 0x10000000, // Start at 256MB physical
                        0x10000000, // 256MB size
                        VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
    
    // Set up memory areas for user mode
    register_memory_area(user_areas, &user_area_count,
                        0x400000, // Start at 4MB
                        0x10000000, // 256MB size
                        VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    
    printf("VMM initialized successfully\n");
}

// Map a virtual page to a physical page
bool vmm_map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint64_t flags) {
    if (virt_addr == 0) {
        printf("VMM: Cannot map null virtual address\n");
        return false;
    }
    
    if (phys_addr == 0) {
        printf("VMM: Cannot map null physical address\n");
        return false;
    }
    
    // Translate VMM flags to hardware flags
    uint64_t hw_flags = PAGE_PRESENT;
    
    if (flags & VMM_FLAG_WRITABLE)     hw_flags |= PAGE_WRITABLE;
    if (flags & VMM_FLAG_USER)         hw_flags |= PAGE_USER;
    if (flags & VMM_FLAG_WRITETHROUGH) hw_flags |= PAGE_WRITETHROUGH;
    if (flags & VMM_FLAG_NOCACHE)      hw_flags |= PAGE_CACHE_DISABLE;
    if (flags & VMM_FLAG_GLOBAL)       hw_flags |= PAGE_GLOBAL;
    if (flags & VMM_FLAG_HUGE)         hw_flags |= PAGE_HUGE;
    
    // NX bit
    if ((flags & VMM_FLAG_NO_EXECUTE) && vmm_config.using_nx) {
        hw_flags |= PAGE_NO_EXECUTE;
    }
    
    return map_page_internal(current_pml4_phys, virt_addr, phys_addr, hw_flags);
}

// Unmap a virtual page
bool vmm_unmap_page(uintptr_t virt_addr) {
    if (virt_addr == 0) {
        return false;
    }
    
    // Align to page boundary
    virt_addr &= PAGE_ADDR_MASK;
    
    // Get indices
    uintptr_t pml4_idx = PML4_INDEX(virt_addr);
    uintptr_t pdpt_idx = PDPT_INDEX(virt_addr);
    uintptr_t pd_idx = PD_INDEX(virt_addr);
    uintptr_t pt_idx = PT_INDEX(virt_addr);
    
    // Navigate the page tables
    uint64_t* pml4 = (uint64_t*)phys_to_virt(current_pml4_phys);
    if (!pml4 || !(pml4[pml4_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & PAGE_ADDR_MASK);
    if (!pdpt || !(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    // Handle 1GB page
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        pdpt[pdpt_idx] = 0;
        invlpg(virt_addr);
        return true;
    }
    
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & PAGE_ADDR_MASK);
    if (!pd || !(pd[pd_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    // Handle 2MB page
    if (pd[pd_idx] & PAGE_HUGE) {
        pd[pd_idx] = 0;
        invlpg(virt_addr);
        return true;
    }
    
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & PAGE_ADDR_MASK);
    if (!pt || !(pt[pt_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    // Unmap the page
    pt[pt_idx] = 0;
    invlpg(virt_addr);
    
    return true;
}

// Map multiple pages
bool vmm_map_pages(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint64_t flags) {
    // Check if we can use huge pages
    if ((flags & VMM_FLAG_HUGE) && 
        (virt_addr & 0x1FFFFF) == 0 && 
        (phys_addr & 0x1FFFFF) == 0 && 
        count >= 512) {
        
        // Use 2MB pages
        size_t huge_pages = count / 512;
        size_t remaining = count % 512;
        
        // Map huge pages
        for (size_t i = 0; i < huge_pages; i++) {
            if (!vmm_map_page(virt_addr + i * PAGE_SIZE_2M, 
                              phys_addr + i * PAGE_SIZE_2M, 
                              flags)) {
                // Clean up on failure
                for (size_t j = 0; j < i; j++) {
                    vmm_unmap_page(virt_addr + j * PAGE_SIZE_2M);
                }
                return false;
            }
        }
        
        // Map remaining regular pages if any
        if (remaining > 0) {
            uintptr_t start_virt = virt_addr + huge_pages * PAGE_SIZE_2M;
            uintptr_t start_phys = phys_addr + huge_pages * PAGE_SIZE_2M;
            
            for (size_t i = 0; i < remaining; i++) {
                if (!vmm_map_page(start_virt + i * PAGE_SIZE_4K,
                                 start_phys + i * PAGE_SIZE_4K,
                                 flags & ~VMM_FLAG_HUGE)) {
                    // Clean up on failure
                    for (size_t j = 0; j < i; j++) {
                        vmm_unmap_page(start_virt + j * PAGE_SIZE_4K);
                    }
                    for (size_t j = 0; j < huge_pages; j++) {
                        vmm_unmap_page(virt_addr + j * PAGE_SIZE_2M);
                    }
                    return false;
                }
            }
        }
        
        return true;
    }
    
    // Use regular 4KB pages
    for (size_t i = 0; i < count; i++) {
        if (!vmm_map_page(virt_addr + i * PAGE_SIZE_4K, 
                         phys_addr + i * PAGE_SIZE_4K, 
                         flags & ~VMM_FLAG_HUGE)) {
            // Clean up on failure
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(virt_addr + j * PAGE_SIZE_4K);
            }
            return false;
        }
    }
    
    return true;
}

// Unmap multiple pages
bool vmm_unmap_pages(uintptr_t virt_addr, size_t count) {
    // Check each page since we might have mixed page sizes
    for (size_t i = 0; i < count; i++) {
        vmm_unmap_page(virt_addr + i * PAGE_SIZE_4K);
    }
    return true;
}

// Get physical address for a virtual address
uintptr_t vmm_get_physical_address(uintptr_t virt_addr) {
    return virt_to_phys((void*)virt_addr);
}

// Check if address is mapped
bool vmm_is_mapped(uintptr_t virt_addr) {
    // Handle direct mapping range
    if (virt_addr >= hhdm_offset) {
        return true;
    }
    
    // Navigate the page tables
    uintptr_t pml4_idx = PML4_INDEX(virt_addr);
    uintptr_t pdpt_idx = PDPT_INDEX(virt_addr);
    uintptr_t pd_idx = PD_INDEX(virt_addr);
    uintptr_t pt_idx = PT_INDEX(virt_addr);
    
    uint64_t* pml4 = (uint64_t*)phys_to_virt(current_pml4_phys);
    if (!pml4 || !(pml4[pml4_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & PAGE_ADDR_MASK);
    if (!pdpt || !(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    // Check for 1GB page
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        return true;
    }
    
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & PAGE_ADDR_MASK);
    if (!pd || !(pd[pd_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    // Check for 2MB page
    if (pd[pd_idx] & PAGE_HUGE) {
        return true;
    }
    
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & PAGE_ADDR_MASK);
    if (!pt || !(pt[pt_idx] & PAGE_PRESENT)) {
        return false;
    }
    
    return true;
}

// Allocate virtual memory
void* vmm_allocate(size_t size, uint64_t flags) {
    if (size == 0) {
        return NULL;
    }
    
    // Round up to page size
    size = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    size_t page_count = size / PAGE_SIZE_4K;
    
    // Choose appropriate areas based on flags
    memory_area_t* areas;
    int* area_count;
    
    if (flags & VMM_FLAG_USER) {
        areas = user_areas;
        area_count = &user_area_count;
    } else {
        areas = kernel_areas;
        area_count = &kernel_area_count;
    }
    
    // Find a free area
    memory_area_t* area = find_free_area(areas, *area_count, size, flags);
    if (!area) {
        printf("VMM: No free memory area for allocation of size %zu\n", size);
        return NULL;
    }
    
    // Mark the area as used
    area->is_used = true;
    
    // Allocate physical pages and map them
    for (size_t i = 0; i < page_count; i++) {
        uintptr_t phys = pmm_alloc_page();
        if (phys == 0) {
            // Out of physical memory, clean up
            for (size_t j = 0; j < i; j++) {
                uintptr_t addr = area->base + j * PAGE_SIZE_4K;
                uintptr_t page_phys = vmm_get_physical_address(addr);
                vmm_unmap_page(addr);
                pmm_free_page(page_phys);
            }
            area->is_used = false;
            return NULL;
        }
        
        // Apply additional flags
        uint64_t combined_flags = flags | area->flags;
        
        // Map the page
        if (!vmm_map_page(area->base + i * PAGE_SIZE_4K, phys, combined_flags)) {
            // Failed to map, clean up
            pmm_free_page(phys);
            for (size_t j = 0; j < i; j++) {
                uintptr_t addr = area->base + j * PAGE_SIZE_4K;
                uintptr_t page_phys = vmm_get_physical_address(addr);
                vmm_unmap_page(addr);
                pmm_free_page(page_phys);
            }
            area->is_used = false;
            return NULL;
        }
        
        // Zero the memory
        memset(phys_to_virt(phys), 0, PAGE_SIZE_4K);
    }
    
    // Update statistics
    vmm_stats.pages_allocated += page_count;
    
    return (void*)area->base;
}

// Free allocated memory
void vmm_free(void* addr, size_t size) {
    if (!addr || size == 0) {
        return;
    }
    
    uintptr_t virt_addr = (uintptr_t)addr;
    
    // Round up to page size
    size = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    size_t page_count = size / PAGE_SIZE_4K;
    
    // Free each page
    for (size_t i = 0; i < page_count; i++) {
        uintptr_t page_addr = virt_addr + i * PAGE_SIZE_4K;
        uintptr_t phys_addr = vmm_get_physical_address(page_addr);
        
        if (phys_addr) {
            vmm_unmap_page(page_addr);
            pmm_free_page(phys_addr);
        }
    }
    
    // Find and free the memory area
    // Find and free the memory area
    memory_area_t* areas;
    int count;
    
    if (virt_addr >= hhdm_offset) {
        areas = kernel_areas;
        count = kernel_area_count;
    } else {
        areas = user_areas;
        count = user_area_count;
    }
    
    for (int i = 0; i < count; i++) {
        if (areas[i].base == virt_addr) {
            areas[i].is_used = false;
            break;
        }
    }
    
    // Update statistics
    vmm_stats.pages_freed += page_count;
}

// Create a new address space
uintptr_t vmm_create_address_space(void) {
    // Allocate a physical page for the PML4
    uintptr_t pml4_phys = create_page_table();
    if (pml4_phys == 0) {
        return 0;
    }
    
    // Get current PML4
    uint64_t* src_pml4 = (uint64_t*)phys_to_virt(current_pml4_phys);
    uint64_t* new_pml4 = (uint64_t*)phys_to_virt(pml4_phys);
    
    // Copy kernel space mappings (higher half)
    for (size_t i = 256; i < 512; i++) {
        new_pml4[i] = src_pml4[i];
    }
    
    return pml4_phys;
}

// Delete an address space
void vmm_delete_address_space(uintptr_t pml4_phys) {
    if (pml4_phys == 0 || pml4_phys == current_pml4_phys) {
        return;
    }
    
    // Get the PML4 table
    uint64_t* pml4 = (uint64_t*)phys_to_virt(pml4_phys);
    
    // Free user space page tables (lower half)
    for (size_t pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
        if (pml4[pml4_idx] & PAGE_PRESENT) {
            uint64_t pdpt_phys = pml4[pml4_idx] & PAGE_ADDR_MASK;
            uint64_t* pdpt = (uint64_t*)phys_to_virt(pdpt_phys);
            
            // Free PDPTs
            for (size_t pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
                if ((pdpt[pdpt_idx] & PAGE_PRESENT) && !(pdpt[pdpt_idx] & PAGE_HUGE)) {
                    uint64_t pd_phys = pdpt[pdpt_idx] & PAGE_ADDR_MASK;
                    uint64_t* pd = (uint64_t*)phys_to_virt(pd_phys);
                    
                    // Free PDs
                    for (size_t pd_idx = 0; pd_idx < 512; pd_idx++) {
                        if ((pd[pd_idx] & PAGE_PRESENT) && !(pd[pd_idx] & PAGE_HUGE)) {
                            uint64_t pt_phys = pd[pd_idx] & PAGE_ADDR_MASK;
                            
                            // Free the page table
                            pmm_free_page(pt_phys);
                        }
                    }
                    
                    // Free the page directory
                    pmm_free_page(pd_phys);
                }
            }
            
            // Free the PDPT
            pmm_free_page(pdpt_phys);
        }
    }
    
    // Free the PML4 itself
    pmm_free_page(pml4_phys);
}

// Switch to a different address space
void vmm_switch_address_space(uintptr_t pml4_phys) {
    if (pml4_phys == 0 || pml4_phys == current_pml4_phys) {
        return;
    }
    
    // Update our tracking
    current_pml4_phys = pml4_phys;
    
    // Load the new CR3
    write_cr3(pml4_phys);
}

// Get current address space
uintptr_t vmm_get_current_address_space(void) {
    return current_pml4_phys;
}

// Handle page fault (minimal implementation)
bool vmm_handle_page_fault(uintptr_t fault_addr, uint32_t error_code) {
    printf("PAGE FAULT!");
    return false;
}

// Flush TLB for a specific address
void vmm_flush_tlb_page(uintptr_t virt_addr) {
    invlpg(virt_addr);
}

// Flush entire TLB
void vmm_flush_tlb_full(void) {
    write_cr3(read_cr3());
}

// Get VMM configuration
void vmm_get_config(vmm_config_t *config) {
    if (config) {
        *config = vmm_config;
    }
}

// Dump page tables for debugging
void vmm_dump_page_tables(uintptr_t virt_addr) {
    printf("Page table info for address 0x%lx:\n", virt_addr);
    
    // Get indices
    uintptr_t pml4_idx = PML4_INDEX(virt_addr);
    uintptr_t pdpt_idx = PDPT_INDEX(virt_addr);
    uintptr_t pd_idx = PD_INDEX(virt_addr);
    uintptr_t pt_idx = PT_INDEX(virt_addr);
    
    printf("Indices: PML4=%lu, PDPT=%lu, PD=%lu, PT=%lu\n", 
           pml4_idx, pdpt_idx, pd_idx, pt_idx);
    
    // Navigate the page tables
    uint64_t* pml4 = (uint64_t*)phys_to_virt(current_pml4_phys);
    if (!pml4) {
        printf("Cannot access PML4!\n");
        return;
    }
    
    printf("PML4 entry: 0x%lx\n", pml4[pml4_idx]);
    
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        printf("PML4 entry not present\n");
        return;
    }
    
    uint64_t* pdpt = (uint64_t*)phys_to_virt(pml4[pml4_idx] & PAGE_ADDR_MASK);
    if (!pdpt) {
        printf("Cannot access PDPT!\n");
        return;
    }
    
    printf("PDPT entry: 0x%lx\n", pdpt[pdpt_idx]);
    
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        printf("PDPT entry not present\n");
        return;
    }
    
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        printf("1GB page at physical address 0x%lx\n", pdpt[pdpt_idx] & PAGE_ADDR_MASK);
        dump_page_flags(pdpt[pdpt_idx]);
        return;
    }
    
    uint64_t* pd = (uint64_t*)phys_to_virt(pdpt[pdpt_idx] & PAGE_ADDR_MASK);
    if (!pd) {
        printf("Cannot access PD!\n");
        return;
    }
    
    printf("PD entry: 0x%lx\n", pd[pd_idx]);
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        printf("PD entry not present\n");
        return;
    }
    
    if (pd[pd_idx] & PAGE_HUGE) {
        printf("2MB page at physical address 0x%lx\n", pd[pd_idx] & PAGE_ADDR_MASK);
        dump_page_flags(pd[pd_idx]);
        return;
    }
    
    uint64_t* pt = (uint64_t*)phys_to_virt(pd[pd_idx] & PAGE_ADDR_MASK);
    if (!pt) {
        printf("Cannot access PT!\n");
        return;
    }
    
    printf("PT entry: 0x%lx\n", pt[pt_idx]);
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) {
        printf("PT entry not present\n");
        return;
    }
    
    printf("4KB page at physical address 0x%lx\n", pt[pt_idx] & PAGE_ADDR_MASK);
    dump_page_flags(pt[pt_idx]);
}

// Helper function to print page flags
void dump_page_flags(uint64_t entry) {
    printf("Flags: %s%s%s%s%s%s%s%s%s\n",
           (entry & PAGE_PRESENT) ? "PRESENT " : "",
           (entry & PAGE_WRITABLE) ? "WRITABLE " : "",
           (entry & PAGE_USER) ? "USER " : "",
           (entry & PAGE_WRITETHROUGH) ? "WRITETHROUGH " : "",
           (entry & PAGE_CACHE_DISABLE) ? "NOCACHE " : "",
           (entry & PAGE_ACCESSED) ? "ACCESSED " : "",
           (entry & PAGE_DIRTY) ? "DIRTY " : "",
           (entry & PAGE_HUGE) ? "HUGE " : "",
           (entry & PAGE_GLOBAL) ? "GLOBAL " : "");
}