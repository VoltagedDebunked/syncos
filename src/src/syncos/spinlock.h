#ifndef _SYNCOS_SPINLOCK_H
#define _SYNCOS_SPINLOCK_H

#include <stdint.h>
#include <stdbool.h>

// Spinlock structure for low-level synchronization
typedef struct {
    volatile uint64_t value;  // Atomic lock value
    volatile uintptr_t owner; // Tracking the thread/CPU that holds the lock
} spinlock_t;

// Spinlock initialization flags
#define SPINLOCK_FLAG_RECURSIVE (1 << 0)
#define SPINLOCK_FLAG_IRQSAFE   (1 << 1)

// Spinlock function prototypes
void spinlock_init(spinlock_t *lock);
void spinlock_init_flags(spinlock_t *lock, uint32_t flags);
void spinlock_acquire(spinlock_t *lock);
bool spinlock_try_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
bool spinlock_is_held(spinlock_t *lock);

// Advanced spinlock diagnostics
void spinlock_dump_stats(spinlock_t *lock);
void spinlock_set_name(spinlock_t *lock, const char *name);

#endif // _SYNCOS_SPINLOCK_H