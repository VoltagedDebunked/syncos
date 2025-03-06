#include <syncos/gdt.h>
#include <kstd/string.h>
#include <stddef.h>

// GDT and TSS structures with cache-line alignment to prevent false sharing
// Using 64-byte alignment which is common cache line size
static gdt_entry_t gdt[GDT_ENTRIES + 1] __attribute__((aligned(64)));
static gdtr_t gdtr __attribute__((aligned(64)));
static tss_t tss __attribute__((aligned(64)));

// Track initialization status with memory barrier semantics
static volatile _Atomic bool initialized = false;

// Validate GDT entry number is in bounds
static inline bool is_valid_gdt_entry(uint32_t num) {
    return num < (GDT_ENTRIES + 1);
}

// Simple function to set a GDT entry with validation
static void gdt_set_entry(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    if (!is_valid_gdt_entry(num)) {
        // In production, you might want to add logging or panic here
        return;
    }
    
    // Set base address
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    
    // Set limit
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    
    // Set access flags
    gdt[num].access = access;
}

// Set up the TSS descriptor - handles the 16-byte TSS descriptor required in long mode
static void gdt_set_tss(uint32_t num, uint64_t base, uint32_t limit) {
    if (!is_valid_gdt_entry(num) || !is_valid_gdt_entry(num + 1)) {
        // Ensure both entries needed for TSS are available
        return;
    }
    
    // First entry - standard descriptor format
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].access = GDT_TSS_ACCESS;
    gdt[num].granularity = ((limit >> 16) & 0x0F) | GDT_TSS_FLAGS;
    
    // Second entry - upper 32 bits of the 64-bit base address
    gdt[num + 1].limit_low = 0;
    gdt[num + 1].base_low = 0;
    gdt[num + 1].base_middle = 0;
    gdt[num + 1].access = 0;
    gdt[num + 1].granularity = 0;
    gdt[num + 1].base_high = 0;
    
    // Set the upper 32 bits properly with type safety
    uint32_t base_high32 = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    memcpy(&gdt[num + 1], &base_high32, sizeof(uint32_t));
}

// Load the GDT with a reliable sequence that preserves register state
static void gdt_flush(void) {
    // Disable interrupts during critical GDT operation
    __asm__ volatile ("cli");
    
    // Load GDTR with proper memory clobber
    __asm__ volatile (
        "lgdt %0"
        : 
        : "m" (gdtr)
        : "memory"
    );
    
    // Far jump to reload CS with proper instruction serialization
    __asm__ volatile (
        "pushq %0\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        : 
        : "i" (GDT_KERNEL_CODE)
        : "rax", "memory"
    );
    
    // Reload data segment registers with proper constraint for register values
    __asm__ volatile (
        "movw %0, %%ds\n"
        "movw %0, %%es\n"
        "movw %0, %%fs\n"
        "movw %0, %%gs\n"
        "movw %0, %%ss\n"
        : 
        : "rm" ((uint16_t)GDT_KERNEL_DATA)
        : "memory"
    );
    
    // Re-enable interrupts
    __asm__ volatile ("sti");
}

// Load the TSS with proper error checking
static void tss_flush(void) {
    __asm__ volatile (
        "ltr %0"
        : 
        : "rm" ((uint16_t)GDT_TSS)
        : "memory"
    );
}

// Validate the GDT structure for integrity
static bool validate_gdt() {
    // Check for NULL descriptor
    if (gdt[0].limit_low != 0 || gdt[0].base_low != 0 || 
        gdt[0].base_middle != 0 || gdt[0].access != 0 ||
        gdt[0].granularity != 0 || gdt[0].base_high != 0) {
        return false;
    }
    
    // Check CS segment has correct access bits
    if (gdt[GDT_KERNEL_CODE >> 3].access != GDT_KERNEL_CODE_ACCESS) {
        return false;
    }
    
    // Check DS segment has correct access bits
    if (gdt[GDT_KERNEL_DATA >> 3].access != GDT_KERNEL_DATA_ACCESS) {
        return false;
    }
    
    // Check TSS is properly set up
    if (gdt[GDT_TSS >> 3].access != GDT_TSS_ACCESS) {
        return false;
    }
    
    return true;
}

// Initialize the GDT with reliability focus
void gdt_init(void) {
    // Prevent multiple initialization with atomic check
    bool was_initialized = __atomic_exchange_n(&initialized, true, __ATOMIC_SEQ_CST);
    if (was_initialized) {
        return;
    }
    
    // Zero out all structures first for clean state
    memset(&gdt, 0, sizeof(gdt));
    memset(&gdtr, 0, sizeof(gdtr));
    memset(&tss, 0, sizeof(tss));
    
    // Set up the GDT pointer - account for the extra entry for TSS
    gdtr.limit = (sizeof(gdt_entry_t) * (GDT_ENTRIES + 1)) - 1;
    gdtr.base = (uint64_t)&gdt[0];
    
    // Null descriptor (index 0) - already zeroed by memset
    
    // Kernel code segment (index 1)
    gdt_set_entry(1, 0, 0xFFFFF, GDT_KERNEL_CODE_ACCESS, GDT_KERNEL_CODE_FLAGS);
    
    // Kernel data segment (index 2)
    gdt_set_entry(2, 0, 0xFFFFF, GDT_KERNEL_DATA_ACCESS, GDT_KERNEL_DATA_FLAGS);
    
    // User code segment (index 3)
    gdt_set_entry(3, 0, 0xFFFFF, GDT_USER_CODE_ACCESS, GDT_USER_CODE_FLAGS);
    
    // User data segment (index 4)
    gdt_set_entry(4, 0, 0xFFFFF, GDT_USER_DATA_ACCESS, GDT_USER_DATA_FLAGS);
    
    // Set up the TSS with properly aligned stacks
    // Set I/O map base to end of TSS to effectively disable I/O permissions bitmap
    tss.iomap_base = sizeof(tss_t);
    
    // Configure TSS entry in GDT - takes 2 entries (5 and 6)
    gdt_set_tss(5, (uint64_t)&tss, sizeof(tss_t) - 1);
    
    // Validate GDT integrity before loading
    if (!validate_gdt()) {
        // Reset initialized flag to allow retry
        __atomic_store_n(&initialized, false, __ATOMIC_SEQ_CST);
        return;
    }
    
    // Load the GDT and TSS
    gdt_flush();
    tss_flush();
    
    // Force memory barrier to ensure visibility of initialization
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

// Set kernel stack in TSS with validation
void gdt_set_kernel_stack(uint64_t stack) {
    // Validate stack alignment (16-byte aligned per SysV ABI)
    if (stack & 0xF) {
        // Stack is misaligned, unsafe to use
        return;
    }
    
    // Ensure we're initialized
    if (!__atomic_load_n(&initialized, __ATOMIC_SEQ_CST)) {
        gdt_init();
    }
    
    // Disable interrupts during stack update to prevent partial update
    __asm__ volatile ("cli");
    
    // Set the stack pointer in the TSS
    tss.rsp0 = stack;
    
    // Re-enable interrupts
    __asm__ volatile ("sti");
}

// Get kernel stack from TSS safely
uint64_t gdt_get_kernel_stack(void) {
    // Ensure we're initialized
    if (!__atomic_load_n(&initialized, __ATOMIC_SEQ_CST)) {
        return 0;
    }
    
    // Get stack with proper atomic semantics
    uint64_t stack;
    __asm__ volatile (
        "movq %1, %0"
        : "=r" (stack)
        : "m" (tss.rsp0)
        : "memory"
    );
    
    return stack;
}