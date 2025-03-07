#include <syncos/gdt.h>
#include <kstd/string.h>
#include <kstd/stdio.h>
#include <stddef.h>
#include <stdbool.h>

// GDT and TSS structures with cache-line alignment to prevent false sharing
// Using 64-byte alignment which is common cache line size
static gdt_entry_t gdt[GDT_ENTRIES + 1] __attribute__((aligned(64)));
static gdt_entry_t gdt_backup_v[GDT_ENTRIES + 1] __attribute__((aligned(64)));
static gdtr_t gdtr __attribute__((aligned(64)));
static tss_t tss __attribute__((aligned(64)));

// Track initialization status with memory barrier semantics
static volatile _Atomic bool initialized = false;
static volatile _Atomic bool recovery_in_progress = false;
static volatile uint32_t gdt_verification_errors = 0;
static volatile uint64_t last_verification_time = 0;

// GDT checksum for integrity verification
static uint32_t gdt_checksum = 0;

// Maximum recovery attempts before panic
#define MAX_GDT_RECOVERY_ATTEMPTS 3
static volatile uint32_t recovery_attempts = 0;

// Interval for periodic verification in timer ticks
#define GDT_VERIFICATION_INTERVAL 1000  // Every 1000 timer ticks

// Forward declarations
static bool validate_gdt(void);
static uint32_t calculate_gdt_checksum(void);
static void gdt_panic(const char *message);

// Calculate a simple checksum of GDT content
static uint32_t calculate_gdt_checksum(void) {
    uint32_t checksum = 0;
    uint8_t *ptr = (uint8_t *)&gdt[0];
    
    for (size_t i = 0; i < sizeof(gdt_entry_t) * (GDT_ENTRIES + 1); i++) {
        checksum = ((checksum << 5) | (checksum >> 27)) ^ ptr[i];
    }
    
    return checksum;
}

// Validate GDT entry number is in bounds
static inline bool is_valid_gdt_entry(uint32_t num) {
    return num < (GDT_ENTRIES + 1);
}

// Simple function to set a GDT entry with validation
static bool gdt_set_entry(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    if (!is_valid_gdt_entry(num)) {
        printf("ERROR: GDT entry %u out of bounds\n", num);
        return false;
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
    
    return true;
}

// Set up the TSS descriptor - handles the 16-byte TSS descriptor required in long mode
static bool gdt_set_tss(uint32_t num, uint64_t base, uint32_t limit) {
    if (!is_valid_gdt_entry(num) || !is_valid_gdt_entry(num + 1)) {
        // Ensure both entries needed for TSS are available
        printf("ERROR: TSS GDT entries %u and %u out of bounds\n", num, num + 1);
        return false;
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
    
    return true;
}

// This function actually loads the GDT
void gdt_flush(void) {
    // Disable interrupts during critical GDT operation
    __asm__ volatile ("cli");
    
    // Create a local copy of the GDTR to ensure it's properly aligned
    gdtr_t local_gdtr;
    local_gdtr.limit = gdtr.limit;
    local_gdtr.base = gdtr.base;
    
    // Load GDTR with proper memory clobber and exception safeguards
    bool gdt_load_failed = false;
    
    __asm__ volatile (
        "lgdt %0\n"
        "jnc 1f\n"            // Jump if carry flag not set (success)
        "movb $1, %1\n"       // Set failure flag
        "1:\n"
        : "+m" (local_gdtr), "+m" (gdt_load_failed)
        :
        : "memory"
    );
    
    if (gdt_load_failed) {
        printf("CRITICAL: Failed to load GDTR!\n");
        if (!recovery_in_progress) {
            gdt_recover();
            return;
        } else {
            gdt_panic("Cannot recover GDT - GDTR load failed during recovery");
        }
    }
    
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
static bool tss_flush(void) {
    bool tss_load_failed = false;
    
    __asm__ volatile (
        "ltr %1\n"
        "jnc 1f\n"            // Jump if carry flag not set (success)
        "movb $1, %0\n"       // Set failure flag
        "1:\n"
        : "=m" (tss_load_failed)
        : "rm" ((uint16_t)GDT_TSS)
        : "memory"
    );
    
    if (tss_load_failed) {
        printf("ERROR: Failed to load TSS!\n");
        return false;
    }
    
    return true;
}

// Reload the GDT - useful for recovery
void gdt_reload(void) {
    // Reload GDTR and segments
    gdt_flush();
    
    // Reload TSS
    tss_flush();
    
    printf("GDT reloaded successfully\n");
}

// Validate the GDT structure for integrity
static bool validate_gdt() {
    // Check for NULL descriptor
    if (gdt[0].limit_low != 0 || gdt[0].base_low != 0 || 
        gdt[0].base_middle != 0 || gdt[0].access != 0 ||
        gdt[0].granularity != 0 || gdt[0].base_high != 0) {
        printf("ERROR: Invalid NULL descriptor in GDT\n");
        return false;
    }
    
    // Check CS segment has correct access bits
    if (gdt[GDT_KERNEL_CODE >> 3].access != GDT_KERNEL_CODE_ACCESS) {
        printf("ERROR: Invalid kernel code segment access rights\n");
        return false;
    }
    
    // Check DS segment has correct access bits
    if (gdt[GDT_KERNEL_DATA >> 3].access != GDT_KERNEL_DATA_ACCESS) {
        printf("ERROR: Invalid kernel data segment access rights\n");
        return false;
    }
    
    // Check TSS is properly set up
    if (gdt[GDT_TSS >> 3].access != GDT_TSS_ACCESS) {
        printf("ERROR: Invalid TSS access rights\n");
        return false;
    }
    
    // Verify checksum matches if already initialized
    if (initialized && gdt_checksum != 0) {
        uint32_t current_checksum = calculate_gdt_checksum();
        if (current_checksum != gdt_checksum) {
            printf("ERROR: GDT checksum mismatch! Expected 0x%x, got 0x%x\n", 
                  gdt_checksum, current_checksum);
            return false;
        }
    }
    
    return true;
}

// Backup the current GDT state
static void gdt_backup(void) {
    memcpy(gdt_backup_v, gdt, sizeof(gdt));
}

// Restore GDT from backup
static bool gdt_restore_from_backup(void) {
    memcpy(gdt, gdt_backup_v, sizeof(gdt));
    
    // Validate GDT after restoration
    if (!validate_gdt()) {
        printf("ERROR: Backup GDT validation failed!\n");
        return false;
    }
    
    // Reload GDT to apply changes
    gdt_reload();
    
    return true;
}

// Panic function for unrecoverable errors
static void gdt_panic(const char *message) {
    printf("PANIC: %s\n", message);
    printf("System halted due to unrecoverable GDT error\n");
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}

// Periodic GDT verification - call this from a timer callback
void gdt_verify(uint64_t tick_count, void *context) {
    // Skip verification if not initialized or during recovery
    if (!initialized || recovery_in_progress) {
        return;
    }
    
    // Only verify periodically
    if (tick_count - last_verification_time < GDT_VERIFICATION_INTERVAL) {
        return;
    }
    
    last_verification_time = tick_count;
    
    // Verify GDT integrity
    if (!validate_gdt()) {
        gdt_verification_errors++;
        printf("WARNING: GDT verification failed (%u errors total)\n", 
              gdt_verification_errors);
        
        // Try recovery if errors exceed threshold
        if (gdt_verification_errors >= 3) {
            printf("ERROR: GDT corrupted, attempting recovery\n");
            gdt_recover();
        }
    } else {
        // Reset error counter after successful verification
        if (gdt_verification_errors > 0) {
            gdt_verification_errors = 0;
            printf("INFO: GDT verification passed after previous errors\n");
        }
    }
}

// Try to recover from GDT corruption
bool gdt_recover(void) {
    // Prevent nested recovery attempts
    if (__atomic_exchange_n(&recovery_in_progress, true, __ATOMIC_SEQ_CST)) {
        printf("WARNING: GDT recovery already in progress\n");
        return false;
    }
    
    printf("ALERT: Attempting GDT recovery (attempt %u of %u)\n", 
           recovery_attempts + 1, MAX_GDT_RECOVERY_ATTEMPTS);
    
    // Increment recovery attempt counter
    recovery_attempts++;
    
    // Check if we've exceeded max attempts
    if (recovery_attempts > MAX_GDT_RECOVERY_ATTEMPTS) {
        __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
        gdt_panic("Maximum GDT recovery attempts exceeded");
        return false;
    }
    
    // First try to restore from backup
    if (initialized) {
        printf("Attempting GDT recovery from backup...\n");
        if (gdt_restore_from_backup()) {
            printf("GDT successfully recovered from backup\n");
            __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
            return true;
        }
    }
    
    // If backup restoration failed, reinitialize from scratch
    printf("Backup restoration failed, reinitializing GDT from scratch...\n");
    
    // Zero out all structures for clean state
    memset(&gdt, 0, sizeof(gdt));
    memset(&gdtr, 0, sizeof(gdtr));
    memset(&tss, 0, sizeof(tss));
    
    // Set up the GDT pointer
    gdtr.limit = (sizeof(gdt_entry_t) * (GDT_ENTRIES + 1)) - 1;
    gdtr.base = (uint64_t)&gdt[0];
    
    // Create the actual GDT entries
    
    // Null descriptor (index 0) - already zeroed by memset
    
    // Kernel code segment (index 1)
    if (!gdt_set_entry(1, 0, 0xFFFFF, GDT_KERNEL_CODE_ACCESS, GDT_KERNEL_CODE_FLAGS)) {
        __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set kernel code segment during recovery");
        return false;
    }
    
    // Kernel data segment (index 2)
    if (!gdt_set_entry(2, 0, 0xFFFFF, GDT_KERNEL_DATA_ACCESS, GDT_KERNEL_DATA_FLAGS)) {
        __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set kernel data segment during recovery");
        return false;
    }
    
    // User code segment (index 3)
    if (!gdt_set_entry(3, 0, 0xFFFFF, GDT_USER_CODE_ACCESS, GDT_USER_CODE_FLAGS)) {
        __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set user code segment during recovery");
        return false;
    }
    
    // User data segment (index 4)
    if (!gdt_set_entry(4, 0, 0xFFFFF, GDT_USER_DATA_ACCESS, GDT_USER_DATA_FLAGS)) {
        __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set user data segment during recovery");
        return false;
    }
    
    // Set up the TSS
    tss.iomap_base = sizeof(tss_t);  // Disable I/O permissions bitmap
    
    // Configure TSS entry in GDT - takes 2 entries (5 and 6)
    if (!gdt_set_tss(5, (uint64_t)&tss, sizeof(tss_t) - 1)) {
        __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set TSS during recovery");
        return false;
    }
    
    // Validate GDT before loading
    if (!validate_gdt()) {
        __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
        gdt_panic("GDT validation failed during recovery");
        return false;
    }
    
    // Create a backup of the new GDT
    gdt_backup();
    
    // Update the checksum
    gdt_checksum = calculate_gdt_checksum();
    
    // Load the GDT and TSS
    gdt_flush();
    if (!tss_flush()) {
        __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
        gdt_panic("TSS load failed during recovery");
        return false;
    }
    
    printf("GDT successfully reinitialized and reloaded\n");
    
    // Mark as initialized and end recovery
    if (!initialized) {
        __atomic_store_n(&initialized, true, __ATOMIC_SEQ_CST);
    }
    __atomic_store_n(&recovery_in_progress, false, __ATOMIC_SEQ_CST);
    
    // Force memory barrier to ensure visibility of initialization
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    
    return true;
}

// Initialize the GDT with reliability focus
void gdt_init(void) {
    // Prevent multiple initialization with atomic check
    bool was_initialized = __atomic_exchange_n(&initialized, true, __ATOMIC_SEQ_CST);
    if (was_initialized) {
        printf("GDT already initialized, skipping initialization\n");
        return;
    }
    
    printf("Initializing GDT...\n");
    
    // Zero out all structures first for clean state
    memset(&gdt, 0, sizeof(gdt));
    memset(&gdtr, 0, sizeof(gdtr));
    memset(&tss, 0, sizeof(tss));
    
    // Set up the GDT pointer - account for the extra entry for TSS
    gdtr.limit = (sizeof(gdt_entry_t) * (GDT_ENTRIES + 1)) - 1;
    gdtr.base = (uint64_t)&gdt[0];
    
    printf("GDTR: Base=0x%lx, Limit=0x%x\n", gdtr.base, gdtr.limit);
    
    // Null descriptor (index 0) - already zeroed by memset
    
    // Kernel code segment (index 1)
    if (!gdt_set_entry(1, 0, 0xFFFFF, GDT_KERNEL_CODE_ACCESS, GDT_KERNEL_CODE_FLAGS)) {
        __atomic_store_n(&initialized, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set kernel code segment");
        return;
    }
    
    // Kernel data segment (index 2)
    if (!gdt_set_entry(2, 0, 0xFFFFF, GDT_KERNEL_DATA_ACCESS, GDT_KERNEL_DATA_FLAGS)) {
        __atomic_store_n(&initialized, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set kernel data segment");
        return;
    }
    
    // User code segment (index 3)
    if (!gdt_set_entry(3, 0, 0xFFFFF, GDT_USER_CODE_ACCESS, GDT_USER_CODE_FLAGS)) {
        __atomic_store_n(&initialized, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set user code segment");
        return;
    }
    
    // User data segment (index 4)
    if (!gdt_set_entry(4, 0, 0xFFFFF, GDT_USER_DATA_ACCESS, GDT_USER_DATA_FLAGS)) {
        __atomic_store_n(&initialized, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set user data segment");
        return;
    }
    
    // Set up the TSS
    tss.iomap_base = sizeof(tss_t);  // Disable I/O permissions bitmap
    
    // Configure TSS entry in GDT - takes 2 entries (5 and 6)
    if (!gdt_set_tss(5, (uint64_t)&tss, sizeof(tss_t) - 1)) {
        __atomic_store_n(&initialized, false, __ATOMIC_SEQ_CST);
        gdt_panic("Failed to set TSS");
        return;
    }
    
    // Validate GDT integrity before loading
    if (!validate_gdt()) {
        __atomic_store_n(&initialized, false, __ATOMIC_SEQ_CST);
        gdt_panic("GDT validation failed during initialization");
        return;
    }
    
    // Create a backup of the initial GDT
    gdt_backup();
    
    // Calculate and store the checksum
    gdt_checksum = calculate_gdt_checksum();
    
    // Load the GDT and TSS
    gdt_flush();
    if (!tss_flush()) {
        __atomic_store_n(&initialized, false, __ATOMIC_SEQ_CST);
        gdt_panic("TSS load failed during initialization");
        return;
    }
    
    // Initialize verification time
    last_verification_time = 0;
    
    printf("GDT initialization complete\n");
    
    // Force memory barrier to ensure visibility of initialization
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

// Set kernel stack in TSS with validation
void gdt_set_kernel_stack(uint64_t stack) {
    // Validate stack alignment (16-byte aligned per SysV ABI)
    if (stack & 0xF) {
        printf("WARNING: Misaligned kernel stack address 0x%lx\n", stack);
        // Align it down to 16 bytes
        stack &= ~0xF;
    }
    
    // Validate stack address range (basic sanity check)
    if (stack < 0x1000 || stack > 0xFFFFFFFFFFFF0000) {
        printf("ERROR: Invalid kernel stack address 0x%lx\n", stack);
        return;
    }
    
    // Ensure we're initialized
    if (!__atomic_load_n(&initialized, __ATOMIC_SEQ_CST)) {
        printf("WARNING: GDT not initialized before setting kernel stack\n");
        gdt_init();
    }
    
    // Disable interrupts during stack update to prevent partial update
    __asm__ volatile ("cli");
    
    // Set the stack pointer in the TSS
    tss.rsp0 = stack;
    
    // Re-enable interrupts
    __asm__ volatile ("sti");
    
    printf("Kernel stack set to 0x%lx\n", stack);
}

// Get kernel stack from TSS safely
uint64_t gdt_get_kernel_stack(void) {
    // Ensure we're initialized
    if (!__atomic_load_n(&initialized, __ATOMIC_SEQ_CST)) {
        printf("WARNING: Attempting to get kernel stack before GDT initialization\n");
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

// Dump GDT for debug purposes
void gdt_dump(void) {
    printf("GDT Status:\n");
    printf("  Initialized: %s\n", initialized ? "Yes" : "No");
    printf("  Recovery in progress: %s\n", recovery_in_progress ? "Yes" : "No");
    printf("  Recovery attempts: %u of %u\n", recovery_attempts, MAX_GDT_RECOVERY_ATTEMPTS);
    printf("  GDTR Base: 0x%lx\n", gdtr.base);
    printf("  GDTR Limit: 0x%x\n", gdtr.limit);
    printf("  GDT Checksum: 0x%x\n", gdt_checksum);
    printf("  Verification Errors: %u\n", gdt_verification_errors);
    
    printf("GDT Entries:\n");
    
    const char *segment_types[] = {
        "NULL",
        "Kernel Code",
        "Kernel Data",
        "User Code",
        "User Data",
        "TSS Low",
        "TSS High"
    };
    
    for (int i = 0; i <= 6; i++) {
        printf("  [%d] %s: Base=0x%02x%02x%04x, Limit=0x%02x%04x, Access=0x%02x, Flags=0x%02x\n",
               i, segment_types[i],
               gdt[i].base_high, gdt[i].base_middle, gdt[i].base_low,
               (gdt[i].granularity & 0x0F), gdt[i].limit_low,
               gdt[i].access, (gdt[i].granularity & 0xF0));
    }
    
    printf("TSS Info:\n");
    printf("  RSP0: 0x%lx\n", tss.rsp0);
    printf("  IOMap Base: 0x%x\n", tss.iomap_base);
}