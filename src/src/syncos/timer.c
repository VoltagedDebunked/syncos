#include <syncos/timer.h>
#include <syncos/irq.h>
#include <syncos/pic.h>
#include <syncos/idt.h>
#include <kstd/io.h>
#include <kstd/stdio.h>
#include <kstd/string.h>

// Maximum number of timer callbacks
#define MAX_TIMER_CALLBACKS 16

// Timer callback entry structure
typedef struct {
    timer_callback_t callback;
    void *context;
    uint32_t interval_ms;
    uint64_t next_tick;
    bool active;
} timer_callback_entry_t;

// Timer state
static uint32_t timer_frequency = 0;
static volatile uint64_t timer_tick_count = 0;
static timer_callback_entry_t timer_callbacks[MAX_TIMER_CALLBACKS];
static bool timer_initialized = false;

// Atomic lock for callback list manipulation
static volatile _Atomic bool timer_callbacks_lock = false;

// Timer IRQ handler - forward declaration
extern void timer_irq_stub(void);
static bool timer_irq_handler(uint8_t irq, void *context);

// Initialize the timer subsystem
void timer_init(uint32_t frequency_hz) {
    if (timer_initialized) {
        printf("Timer: Already initialized\n");
        return;
    }
    
    // Clear callback table
    memset(timer_callbacks, 0, sizeof(timer_callbacks));
    
    // Set the timer frequency
    timer_set_frequency(frequency_hz);
    
    // Register IRQ handler for the timer (IRQ 0)
    if (!irq_register_handler(IRQ_TIMER, timer_irq_handler, NULL)) {
        printf("Timer: Failed to register IRQ handler\n");
        return;
    }
    
    // Unmask the timer IRQ in the PIC
    pic_enable_irq(IRQ_TIMER);
    
    timer_initialized = true;
    printf("Timer: Initialized at %u Hz (%u ms period)\n", 
            frequency_hz, 1000 / frequency_hz);
}

// Set the timer frequency
void timer_set_frequency(uint32_t frequency_hz) {
    // Validate frequency (must be between 19 Hz and 1193182 Hz)
    if (frequency_hz < 19 || frequency_hz > PIT_BASE_FREQUENCY) {
        printf("Timer: Invalid frequency %u Hz, using 1000 Hz\n", frequency_hz);
        frequency_hz = 1000;
    }
    
    // Calculate divisor
    uint32_t divisor = PIT_BASE_FREQUENCY / frequency_hz;
    
    // Disable interrupts while reprogramming the PIT
    bool interrupts_enabled = idt_are_interrupts_enabled();
    if (interrupts_enabled) {
        idt_disable_interrupts();
    }
    
    // Update our tracking
    timer_frequency = frequency_hz;
    
    // Send command to PIT - Channel 0, Access mode: lobyte/hibyte, Mode 3 (square wave)
    outb(PIT_COMMAND, 0x36);  
    io_wait();  // Small delay after command
    
    // Send divisor (split into low and high bytes)
    outb(PIT_CHANNEL0_DATA, divisor & 0xFF);          // Low byte
    io_wait();
    outb(PIT_CHANNEL0_DATA, (divisor >> 8) & 0xFF);   // High byte
    
    // Restore interrupt state
    if (interrupts_enabled) {
        idt_enable_interrupts();
    }
    
    printf("Timer: Frequency set to %u Hz (divisor: %u)\n", frequency_hz, divisor);
}

// Get the current timer frequency
uint32_t timer_get_frequency(void) {
    return timer_frequency;
}

// Register a timer callback
bool timer_register_callback(timer_callback_t callback, void *context, uint32_t interval_ms) {
    if (!callback || interval_ms == 0 || !timer_initialized) {
        return false;
    }
    
    // Obtain lock for callback table
    while (__atomic_exchange_n(&timer_callbacks_lock, true, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");  // CPU hint for spin-wait
    }
    
    bool result = false;
    
    // Find an open slot
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (!timer_callbacks[i].active) {
            timer_callbacks[i].callback = callback;
            timer_callbacks[i].context = context;
            timer_callbacks[i].interval_ms = interval_ms;
            
            // Calculate next tick with proper scaling to avoid overflow
            uint64_t tick_increment = ((uint64_t)interval_ms * timer_frequency) / 1000;
            if (tick_increment == 0) tick_increment = 1;  // Ensure at least 1 tick
            
            timer_callbacks[i].next_tick = timer_tick_count + tick_increment;
            
            // Set active flag last to ensure all fields are initialized
            __atomic_store_n(&timer_callbacks[i].active, true, __ATOMIC_RELEASE);
            
            printf("Timer: Registered callback %p with interval %u ms (slot %d)\n", 
                    (void*)callback, interval_ms, i);
            result = true;
            break;
        }
    }
    
    // Release lock
    __atomic_store_n(&timer_callbacks_lock, false, __ATOMIC_RELEASE);
    
    if (!result) {
        printf("Timer: Failed to register callback - no free slots\n");
    }
    
    return result;
}

// Unregister a timer callback
bool timer_unregister_callback(timer_callback_t callback) {
    if (!callback || !timer_initialized) {
        return false;
    }
    
    // Obtain lock for callback table
    while (__atomic_exchange_n(&timer_callbacks_lock, true, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");  // CPU hint for spin-wait
    }
    
    bool result = false;
    
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (timer_callbacks[i].active && timer_callbacks[i].callback == callback) {
            timer_callbacks[i].active = false;
            timer_callbacks[i].callback = NULL;
            timer_callbacks[i].context = NULL;
            
            printf("Timer: Unregistered callback %p (slot %d)\n", (void*)callback, i);
            result = true;
            break;
        }
    }
    
    // Release lock
    __atomic_store_n(&timer_callbacks_lock, false, __ATOMIC_RELEASE);
    
    return result;
}

// IRQ handler for the timer (IRQ 0)
static bool timer_irq_handler(uint8_t irq, void *context) {
    // Increment tick count with atomic semantics to ensure visibility
    __atomic_fetch_add(&timer_tick_count, 1, __ATOMIC_SEQ_CST);
    
    // Process callbacks without holding the global lock
    // This uses optimistic concurrency - we check active flag for each callback
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        // Load active flag with acquire semantics
        if (__atomic_load_n(&timer_callbacks[i].active, __ATOMIC_ACQUIRE)) {
            // Check if this callback is due
            if (timer_tick_count >= timer_callbacks[i].next_tick) {
                // Make a local copy of the callback details
                timer_callback_t callback = timer_callbacks[i].callback;
                void *cb_context = timer_callbacks[i].context;
                uint32_t interval = timer_callbacks[i].interval_ms;
                
                if (callback) {
                    // Call the callback - it's safe to do this without holding lock
                    callback(timer_tick_count, cb_context);
                    
                    // Reschedule for next interval if still active
                    if (__atomic_load_n(&timer_callbacks[i].active, __ATOMIC_ACQUIRE)) {
                        uint64_t tick_increment = ((uint64_t)interval * timer_frequency) / 1000;
                        if (tick_increment == 0) tick_increment = 1;
                        
                        // Update next_tick with relaxed semantics
                        __atomic_store_n(&timer_callbacks[i].next_tick, 
                                        timer_tick_count + tick_increment,
                                        __ATOMIC_RELAXED);
                    }
                }
            }
        }
    }
    
    return true;
}

// Get the current tick count
uint64_t timer_get_ticks(void) {
    return __atomic_load_n(&timer_tick_count, __ATOMIC_ACQUIRE);
}

// Get system uptime in milliseconds
uint64_t timer_get_uptime_ms(void) {
    uint64_t ticks = timer_get_ticks();
    uint32_t freq = timer_get_frequency();
    
    if (freq == 0) return 0; // Avoid division by zero
    
    // Scale ticks to milliseconds with proper integer math to avoid overflow
    return (ticks * 1000ULL) / freq;
}

// Sleep for a specified number of milliseconds
void timer_sleep_ms(uint32_t milliseconds) {
    if (!timer_initialized || timer_frequency == 0) {
        // Fallback to a crude busy wait if timer isn't initialized
        timer_busy_wait_us(milliseconds * 1000);
        return;
    }
    
    uint64_t current_ticks = timer_get_ticks();
    uint64_t tick_increment = ((uint64_t)milliseconds * timer_frequency) / 1000;
    uint64_t target_ticks = current_ticks + tick_increment;
    
    // Wait until we reach the target tick count
    while (timer_get_ticks() < target_ticks) {
        // Halt the CPU until next interrupt (power-saving)
        __asm__ volatile("sti; hlt");
    }
}

// Busy-wait for a specified number of microseconds
void timer_busy_wait_us(uint32_t microseconds) {
    // Use RDTSC for precise timing
    uint64_t start, current;
    uint64_t tsc_per_us;
    
    // Rough approximation of TSC frequency - in a real kernel we'd calibrate this
    // We're targeting a 64-bit x86 CPU with an expected frequency between 1-4 GHz
    tsc_per_us = 2000; // Assuming ~2GHz CPU
    
    // Read starting timestamp
    __asm__ volatile("rdtsc" : "=a"(((uint32_t*)&start)[0]), "=d"(((uint32_t*)&start)[1]));
    
    // Calculate number of cycles to wait
    uint64_t wait_cycles = (uint64_t)microseconds * tsc_per_us;
    
    // Busy wait until we've waited long enough
    do {
        __asm__ volatile("rdtsc" : "=a"(((uint32_t*)&current)[0]), "=d"(((uint32_t*)&current)[1]));
    } while (current - start < wait_cycles);
}

// Dump timer status information
void timer_dump_status(void) {
    printf("Timer Status:\n");
    printf("  Initialized: %s\n", timer_initialized ? "yes" : "no");
    printf("  Frequency: %u Hz\n", timer_frequency);
    printf("  Tick count: %lu\n", timer_get_ticks());
    printf("  Uptime: %lu ms\n", timer_get_uptime_ms());
    
    // Obtain lock for consistent view of callbacks
    while (__atomic_exchange_n(&timer_callbacks_lock, true, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
    
    // Count active callbacks
    int active_callbacks = 0;
    for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (timer_callbacks[i].active) {
            active_callbacks++;
        }
    }
    
    printf("  Active callbacks: %d\n", active_callbacks);
    if (active_callbacks > 0) {
        printf("  Callback details:\n");
        for (int i = 0; i < MAX_TIMER_CALLBACKS; i++) {
            if (timer_callbacks[i].active) {
                printf("    [%d] Func: %p, Interval: %u ms, Next tick: %lu\n",
                        i,
                        (void*)timer_callbacks[i].callback,
                        timer_callbacks[i].interval_ms,
                        timer_callbacks[i].next_tick);
            }
        }
    }
    
    // Release lock
    __atomic_store_n(&timer_callbacks_lock, false, __ATOMIC_RELEASE);
}