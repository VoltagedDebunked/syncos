#ifndef _SYNCOS_TIMER_H
#define _SYNCOS_TIMER_H

#include <stdint.h>
#include <stdbool.h>

// PIT (Programmable Interval Timer) constants
#define PIT_CHANNEL0_DATA      0x40    // Channel 0 data port (read/write)
#define PIT_CHANNEL1_DATA      0x41    // Channel 1 data port (read/write)
#define PIT_CHANNEL2_DATA      0x42    // Channel 2 data port (read/write)
#define PIT_COMMAND            0x43    // Mode/Command register (write only)

// PIT control word bits
#define PIT_CHANNEL0           0x00    // Select channel 0
#define PIT_CHANNEL1           0x40    // Select channel 1
#define PIT_CHANNEL2           0x80    // Select channel 2
#define PIT_READ_BACK          0xC0    // Read-back command

#define PIT_LATCH              0x00    // Latch count value
#define PIT_ACCESS_LOW         0x10    // Access low byte only
#define PIT_ACCESS_HIGH        0x20    // Access high byte only
#define PIT_ACCESS_BOTH        0x30    // Access low then high byte

#define PIT_MODE0              0x00    // Interrupt on terminal count
#define PIT_MODE1              0x02    // Hardware retriggerable one-shot
#define PIT_MODE2              0x04    // Rate generator
#define PIT_MODE3              0x06    // Square wave generator
#define PIT_MODE4              0x08    // Software triggered strobe
#define PIT_MODE5              0x0A    // Hardware triggered strobe

#define PIT_BCD                0x01    // BCD counter (instead of binary)

// Standard PIT frequencies
#define PIT_BASE_FREQUENCY     1193182    // ~1.193182 MHz
#define PIT_FREQUENCY_100HZ    11932      // Divisor for ~100 Hz (10ms)
#define PIT_FREQUENCY_1000HZ   1193       // Divisor for ~1000 Hz (1ms)
#define PIT_FREQUENCY_10000HZ  119        // Divisor for ~10000 Hz (0.1ms)

// Time conversion constants
#define NANOSECONDS_PER_TICK   (1000000000ULL / PIT_BASE_FREQUENCY)  // ~838ns per tick

// Callback function type for timer events
typedef void (*timer_callback_t)(uint64_t tick_count, void *context);

/**
 * Initialize the timer subsystem with the specified frequency
 * 
 * @param frequency_hz The desired timer frequency in Hz (usually 1000 for 1ms precision)
 */
void timer_init(uint32_t frequency_hz);

/**
 * Set the timer frequency
 * 
 * @param frequency_hz The desired timer frequency in Hz (19-1193182)
 */
void timer_set_frequency(uint32_t frequency_hz);

/**
 * Get the current timer frequency
 * 
 * @return The current timer frequency in Hz
 */
uint32_t timer_get_frequency(void);

/**
 * Register a periodic timer callback
 * 
 * @param callback The function to call at the specified interval
 * @param context User-provided context that will be passed to the callback
 * @param interval_ms The interval in milliseconds between callbacks
 * @return true if the callback was registered, false otherwise
 */
bool timer_register_callback(timer_callback_t callback, void *context, uint32_t interval_ms);

/**
 * Unregister a timer callback
 * 
 * @param callback The callback function to unregister
 * @return true if the callback was unregistered, false if not found
 */
bool timer_unregister_callback(timer_callback_t callback);

/**
 * Get the current tick count (incremented at the timer frequency)
 * 
 * @return The number of timer ticks since boot
 */
uint64_t timer_get_ticks(void);

/**
 * Get system uptime in milliseconds
 * 
 * @return The number of milliseconds since boot
 */
uint64_t timer_get_uptime_ms(void);

/**
 * Sleep for the specified number of milliseconds
 * This is a blocking call that will yield the CPU using hlt
 * 
 * @param milliseconds The number of milliseconds to sleep
 */
void timer_sleep_ms(uint32_t milliseconds);

/**
 * Busy-wait for the specified number of microseconds
 * This is a blocking call that will not yield the CPU (uses RDTSC)
 * 
 * @param microseconds The number of microseconds to wait
 */
void timer_busy_wait_us(uint32_t microseconds);

/**
 * Dump timer status information for debugging
 */
void timer_dump_status(void);

#endif // _SYNCOS_TIMER_H