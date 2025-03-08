#ifndef _SYNCOS_VMM_H
#define _SYNCOS_VMM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Page attributes and flags for x86_64
#define VMM_FLAG_PRESENT       (1UL << 0)  // Page is present
#define VMM_FLAG_WRITABLE      (1UL << 1)  // Page is writable
#define VMM_FLAG_USER          (1UL << 2)  // Page is accessible from userspace
#define VMM_FLAG_WRITETHROUGH  (1UL << 3)  // Write-through caching
#define VMM_FLAG_NOCACHE       (1UL << 4)  // Disable caching
#define VMM_FLAG_ACCESSED      (1UL << 5)  // Page has been accessed
#define VMM_FLAG_DIRTY         (1UL << 6)  // Page has been written to
#define VMM_FLAG_HUGE          (1UL << 7)  // Huge page (2MB or 1GB)
#define VMM_FLAG_GLOBAL        (1UL << 8)  // Page is global (not flushed from TLB)
#define VMM_FLAG_NO_EXECUTE    (1UL << 63) // NX bit - prevent execution

// Special virtual memory addresses
#define KERNEL_VIRTUAL_BASE    0xFFFFFFFF80000000UL  // Higher half kernel
#define USER_STACK_TOP         0x00007FFFFFFFFFFFULL // Top of user stack
#define KERNEL_STACK_TOP       0xFFFFFFFFFFFFEFFFULL // Top of kernel stack
#define MMIO_BASE              0xFFFFFFFF40000000UL  // MMIO mapping region

// Page size definitions
#define PAGE_SIZE_4K           4096UL
#define PAGE_SIZE_2M           (PAGE_SIZE_4K * 512UL)
#define PAGE_SIZE_1G           (PAGE_SIZE_2M * 512UL)

// Virtual memory manager configuration
typedef struct {
    uintptr_t kernel_pml4;        // Physical address of kernel page tables
    uintptr_t kernel_virtual_base; // Base virtual address of kernel
    size_t    kernel_virtual_size; // Size of kernel virtual address space
    bool      using_pae;           // Is PAE enabled?
    bool      using_nx;            // Is NX bit supported?
} vmm_config_t;

// Initialize the virtual memory manager
void vmm_init(void);

// Map virtual address to physical address
bool vmm_map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint64_t flags);

// Unmap a virtual address
bool vmm_unmap_page(uintptr_t virt_addr);

// Map multiple consecutive pages
bool vmm_map_pages(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint64_t flags);

// Unmap multiple consecutive pages
bool vmm_unmap_pages(uintptr_t virt_addr, size_t count);

// Get physical address from virtual address
uintptr_t vmm_get_physical_address(uintptr_t virt_addr);

// Check if address is mapped
bool vmm_is_mapped(uintptr_t virt_addr);

// Create a new address space (page table)
uintptr_t vmm_create_address_space(void);

// Delete an address space
void vmm_delete_address_space(uintptr_t pml4_phys);

// Switch to a different address space
void vmm_switch_address_space(uintptr_t pml4_phys);

// Get current address space
uintptr_t vmm_get_current_address_space(void);

// Allocate virtual memory (returns virtual address)
void* vmm_allocate(size_t size, uint64_t flags);

// Free virtual memory
void vmm_free(void* addr, size_t size);

// Map physical memory to virtual address space
void* vmm_map_physical(uintptr_t phys_addr, size_t size, uint64_t flags);

// Unmap previously mapped physical memory
void vmm_unmap_physical(void* virt_addr, size_t size);

// Handle page fault
bool vmm_handle_page_fault(uintptr_t fault_addr, uint32_t error_code);

// Flush TLB for a specific address
void vmm_flush_tlb_page(uintptr_t virt_addr);

// Flush entire TLB
void vmm_flush_tlb_full(void);

// Get VMM configuration
void vmm_get_config(vmm_config_t *config);

// Debug function to dump page tables
void vmm_dump_page_tables(uintptr_t virt_addr);
void dump_page_flags(uint64_t entry);

#endif /* _SYNCOS_VMM_H */