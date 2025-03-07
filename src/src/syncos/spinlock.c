#include <syncos/spinlock.h>
#include <kstd/stdio.h>
#include <syncos/idt.h>
#include <kstd/string.h>

// Maximum spinlock name length for debugging
#define MAX_SPINLOCK_NAME 32

// Extended spinlock tracking structure
typedef struct {
    spinlock_t base;
    char name[MAX_SPINLOCK_NAME];
    uint64_t contention_count;
    uint64_t hold_count;
    uint32_t flags;
} spinlock_debug_t;

// Static array of spinlock tracking
#define MAX_TRACKED_SPINLOCKS 64
static spinlock_debug_t spinlock_tracking[MAX_TRACKED_SPINLOCKS] = {0};
static volatile uint32_t spinlock_tracking_count = 0;

// Atomic test-and-set for lock acquisition
static inline bool atomic_test_and_set(volatile uint64_t *lock) {
    uint8_t result;
    __asm__ volatile (
        "lock bts $0, (%1)\n\t"  // Atomic bit test and set
        "setc %0"                // Set result based on previous bit state
        : "=r" (result)
        : "r" (lock)
        : "memory"
    );
    return result;
}

// Initialize a spinlock
void spinlock_init(spinlock_t *lock) {
    spinlock_init_flags(lock, 0);
}

// Initialize a spinlock with specific flags
void spinlock_init_flags(spinlock_t *lock, uint32_t flags) {
    if (!lock) return;
    
    lock->value = 0;  // Unlocked state
    lock->owner = 0;  // No owner
    
    // Track for debugging if space available
    if (spinlock_tracking_count < MAX_TRACKED_SPINLOCKS) {
        spinlock_debug_t *debug = &spinlock_tracking[spinlock_tracking_count++];
        debug->base = *lock;
        debug->flags = flags;
        debug->contention_count = 0;
        debug->hold_count = 0;
        debug->name[0] = '\0';
    }
}

// Acquire the spinlock
void spinlock_acquire(spinlock_t *lock) {
    if (!lock) return;
    
    // Disable interrupts during critical section on IRQ-safe locks
    bool interrupts_enabled = false;
    if (lock->owner & SPINLOCK_FLAG_IRQSAFE) {
        interrupts_enabled = idt_are_interrupts_enabled();
        if (interrupts_enabled) {
            idt_disable_interrupts();
        }
    }
    
    // Busy-wait spin
    while (atomic_test_and_set(&lock->value)) {
        // Pause instruction to reduce power consumption and improve spinlock performance
        __asm__ volatile ("pause");
    }
    
    // Set owner
    lock->owner = (uintptr_t)__builtin_return_address(0);
    
    // Debugging: Track contention
    for (uint32_t i = 0; i < spinlock_tracking_count; i++) {
        if (&spinlock_tracking[i].base == lock) {
            __atomic_fetch_add(&spinlock_tracking[i].contention_count, 1, __ATOMIC_RELAXED);
            __atomic_fetch_add(&spinlock_tracking[i].hold_count, 1, __ATOMIC_RELAXED);
            break;
        }
    }
}

// Try to acquire the spinlock without blocking
bool spinlock_try_acquire(spinlock_t *lock) {
    if (!lock) return false;
    
    // Attempt to acquire without blocking
    if (!atomic_test_and_set(&lock->value)) {
        // Successfully acquired
        lock->owner = (uintptr_t)__builtin_return_address(0);
        return true;
    }
    
    return false;
}

// Release the spinlock
void spinlock_release(spinlock_t *lock) {
    if (!lock) return;
    
    // Clear owner
    lock->owner = 0;
    
    // Memory barrier to ensure previous writes are completed
    __asm__ volatile ("" ::: "memory");
    
    // Clear lock value (release)
    lock->value = 0;
    
    // Re-enable interrupts if they were disabled
    if (lock->owner & SPINLOCK_FLAG_IRQSAFE) {
        idt_enable_interrupts();
    }
}

// Check if lock is currently held
bool spinlock_is_held(spinlock_t *lock) {
    return lock && lock->value != 0;
}

// Dump spinlock statistics for debugging
void spinlock_dump_stats(spinlock_t *lock) {
    for (uint32_t i = 0; i < spinlock_tracking_count; i++) {
        if (&spinlock_tracking[i].base == lock) {
            spinlock_debug_t *debug = &spinlock_tracking[i];
            printf("Spinlock Stats:\n");
            printf("  Name:    %s\n", debug->name[0] ? debug->name : "Unnamed");
            printf("  Flags:   0x%x\n", debug->flags);
            printf("  Owner:   0x%lx\n", lock->owner);
            printf("  Held:    %s\n", spinlock_is_held(lock) ? "Yes" : "No");
            printf("  Contention Count: %lu\n", debug->contention_count);
            printf("  Hold Count:       %lu\n", debug->hold_count);
            return;
        }
    }
    
    printf("Spinlock not found in tracking\n");
}

// Set a name for the spinlock (for debugging)
void spinlock_set_name(spinlock_t *lock, const char *name) {
    for (uint32_t i = 0; i < spinlock_tracking_count; i++) {
        if (&spinlock_tracking[i].base == lock) {
            strncpy(spinlock_tracking[i].name, name, MAX_SPINLOCK_NAME - 1);
            spinlock_tracking[i].name[MAX_SPINLOCK_NAME - 1] = '\0';
            return;
        }
    }
}