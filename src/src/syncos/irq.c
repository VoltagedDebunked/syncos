#include <syncos/irq.h>
#include <syncos/pic.h>
#include <syncos/idt.h>
#include <syncos/serial.h>
#include <kstd/stdio.h>
#include <kstd/io.h>  // For outb function
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Base vector for IRQ remapping
#define IRQ_BASE_VECTOR 0x20

// PIC command and register definitions - needed here for spurious IRQ handling
#define PIC1_COMMAND    0x20
#define PIC2_COMMAND    0xA0
#define PIC_EOI         0x20

// IRQ handler entry
typedef struct {
    irq_handler_t handler;
    void *context;
    bool active;
} irq_handler_entry_t;

// Handler tables
static irq_handler_entry_t irq_handlers[16][MAX_IRQ_HANDLERS] = {{{0}}};

// IRQ statistics for debugging and monitoring
static uint64_t irq_counts[16] = {0};
static uint64_t irq_spurious_counts[16] = {0};
static uint64_t system_ticks = 0;

// Flag to track IRQ initialization
static bool irq_system_initialized = false;

// Mask used to track which IRQs are currently enabled
static uint16_t irq_enabled_mask = 0;

// Timer frequency (for uptime calculations)
static uint32_t timer_frequency_hz = 1000; // Default to 1000 Hz (1ms resolution)

// Forward declaration for ISR setup
static void irq_setup_isrs(void);

// External references to assembly ISR stubs
extern void irq0_handler(void);
extern void irq1_handler(void);
extern void irq2_handler(void);
extern void irq3_handler(void);
extern void irq4_handler(void);
extern void irq5_handler(void);
extern void irq6_handler(void);
extern void irq7_handler(void);
extern void irq8_handler(void);
extern void irq9_handler(void);
extern void irq10_handler(void);
extern void irq11_handler(void);
extern void irq12_handler(void);
extern void irq13_handler(void);
extern void irq14_handler(void);
extern void irq15_handler(void);

// Forward declarations for spurious IRQ handlers
static bool irq_is_spurious(uint8_t irq);
static void irq_handle_spurious(uint8_t irq);

// Initialize the IRQ system
void irq_init(void) {
    if (irq_system_initialized) {
        printf("IRQ system already initialized\n");
        return;
    }
    
    // Initialize PIC with standard remapping (IRQs 0-15 -> INT 0x20-0x2F)
    pic_init(IRQ_BASE_VECTOR, IRQ_BASE_VECTOR + 8);
    
    // Set up IRQ handlers in the IDT
    irq_setup_isrs();
    
    // Start with all IRQs masked (disabled)
    irq_set_all_masked();
    
    // Mark system as initialized
    irq_system_initialized = true;
    
    printf("IRQ: System initialized, remapped to vectors 0x%02X-0x%02X\n",
            IRQ_BASE_VECTOR, IRQ_BASE_VECTOR + 15);
}

// Set up ISRs for all IRQs
static void irq_setup_isrs(void) {
    // Register IRQ handlers in the IDT
    idt_set_handler(IRQ_BASE_VECTOR + 0,  (interrupt_handler_t)irq0_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 1,  (interrupt_handler_t)irq1_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 2,  (interrupt_handler_t)irq2_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 3,  (interrupt_handler_t)irq3_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 4,  (interrupt_handler_t)irq4_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 5,  (interrupt_handler_t)irq5_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 6,  (interrupt_handler_t)irq6_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 7,  (interrupt_handler_t)irq7_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 8,  (interrupt_handler_t)irq8_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 9,  (interrupt_handler_t)irq9_handler,  IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 10, (interrupt_handler_t)irq10_handler, IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 11, (interrupt_handler_t)irq11_handler, IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 12, (interrupt_handler_t)irq12_handler, IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 13, (interrupt_handler_t)irq13_handler, IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 14, (interrupt_handler_t)irq14_handler, IDT_GATE_INTERRUPT);
    idt_set_handler(IRQ_BASE_VECTOR + 15, (interrupt_handler_t)irq15_handler, IDT_GATE_INTERRUPT);
    
    printf("IRQ: Registered all IRQ handlers at vectors 0x%02X-0x%02X\n", 
            IRQ_BASE_VECTOR, IRQ_BASE_VECTOR + 15);
}

// Register an IRQ handler
bool irq_register_handler(uint8_t irq, irq_handler_t handler, void *context) {
    if (irq >= 16 || handler == NULL) {
        return false;
    }
    
    // Find an open slot in the handler table
    for (int i = 0; i < MAX_IRQ_HANDLERS; i++) {
        if (!irq_handlers[irq][i].active) {
            irq_handlers[irq][i].handler = handler;
            irq_handlers[irq][i].context = context;
            irq_handlers[irq][i].active = true;
            
            // Enable this IRQ if it's the first handler
            if (i == 0) {
                irq_enable(irq);
            }
            
            printf("IRQ: Registered handler for IRQ %d (slot %d)\n", irq, i);
            return true;
        }
    }
    
    printf("IRQ: Failed to register handler for IRQ %d - no free slots\n", irq);
    return false;
}

// Unregister an IRQ handler
bool irq_unregister_handler(uint8_t irq, irq_handler_t handler) {
    if (irq >= 16 || handler == NULL) {
        return false;
    }
    
    bool found = false;
    
    // Find the handler in the table
    for (int i = 0; i < MAX_IRQ_HANDLERS; i++) {
        if (irq_handlers[irq][i].active && irq_handlers[irq][i].handler == handler) {
            irq_handlers[irq][i].active = false;
            irq_handlers[irq][i].handler = NULL;
            irq_handlers[irq][i].context = NULL;
            found = true;
            
            printf("IRQ: Unregistered handler for IRQ %d (slot %d)\n", irq, i);
            break;
        }
    }
    
    // If no active handlers remain, disable the IRQ
    bool any_active = false;
    for (int i = 0; i < MAX_IRQ_HANDLERS; i++) {
        if (irq_handlers[irq][i].active) {
            any_active = true;
            break;
        }
    }
    
    if (!any_active) {
        irq_disable(irq);
    }
    
    return found;
}

// Enable an IRQ
void irq_enable(uint8_t irq) {
    if (irq >= 16) {
        return;
    }
    
    // Enable in the PIC
    pic_enable_irq(irq);
    
    // Track in our mask
    irq_enabled_mask |= (1 << irq);
    
    printf("IRQ: Enabled IRQ %d\n", irq);
}

// Disable an IRQ
void irq_disable(uint8_t irq) {
    if (irq >= 16) {
        return;
    }
    
    // Disable in the PIC
    pic_disable_irq(irq);
    
    // Track in our mask
    irq_enabled_mask &= ~(1 << irq);
    
    printf("IRQ: Disabled IRQ %d\n", irq);
}

// Check if an IRQ is enabled
bool irq_is_enabled(uint8_t irq) {
    if (irq >= 16) {
        return false;
    }
    
    return (irq_enabled_mask & (1 << irq)) != 0;
}

// Dispatch an IRQ to all registered handlers
void irq_dispatch(uint8_t irq) {
    if (irq >= 16) {
        return;
    }
    
    // Check for spurious IRQ
    if (irq_is_spurious(irq)) {
        irq_handle_spurious(irq);
        return;
    }
    
    // Track IRQ statistics
    irq_counts[irq]++;
    
    // Special case for timer IRQ
    if (irq == IRQ_TIMER) {
        system_ticks++;
    }
    
    // Call all registered handlers
    bool handled = false;
    for (int i = 0; i < MAX_IRQ_HANDLERS; i++) {
        if (irq_handlers[irq][i].active && irq_handlers[irq][i].handler != NULL) {
            bool result = irq_handlers[irq][i].handler(irq, irq_handlers[irq][i].context);
            handled |= result;
        }
    }
    
    // If IRQ7 or IRQ15 was not handled, it might be spurious
    if (!handled && (irq == 7 || irq == 15)) {
        // Double-check with the In-Service Register
        uint16_t isr = pic_get_isr();
        if (!(isr & (1 << irq))) {
            irq_spurious_counts[irq]++;
            
            // For IRQ15, send EOI only to master
            if (irq == 15) {
                outb(PIC1_COMMAND, PIC_EOI);
                return;
            }
            // For IRQ7, we don't need to send any EOI
            return;
        }
    }
    
    // Send EOI
    irq_end_of_interrupt(irq);
}

// Signal end of interrupt
void irq_end_of_interrupt(uint8_t irq) {
    pic_send_eoi(irq);
}

// Mask all IRQs
void irq_set_all_masked(void) {
    // Mask all interrupts in the PIC
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    
    // Update our tracking
    irq_enabled_mask = 0;
    
    printf("IRQ: All IRQs masked\n");
}

// Unmask all IRQs
void irq_clear_all_masked(void) {
    // Enable all interrupts in the PIC
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
    
    // Update our tracking
    irq_enabled_mask = 0xFFFF;
    
    printf("IRQ: All IRQs unmasked\n");
}

// Check if an IRQ is spurious
static bool irq_is_spurious(uint8_t irq) {
    // Only IRQ 7 and 15 can be spurious
    if (irq != 7 && irq != 15) {
        return false;
    }
    
    // Read the In-Service Register to check if it's really in service
    uint16_t isr = pic_get_isr();
    return !(isr & (1 << irq));
}

// Handle a spurious IRQ
static void irq_handle_spurious(uint8_t irq) {
    // Increment spurious IRQ counter
    irq_spurious_counts[irq]++;
    
    printf("IRQ: Spurious IRQ %d detected\n", irq);
    
    // For IRQ 15 (slave PIC), we need to send EOI to the master PIC
    if (irq == 15) {
        outb(PIC1_COMMAND, PIC_EOI);
    }
    
    // No EOI is sent to the spurious IRQ's controller
}

// Get number of system ticks since boot
uint64_t irq_get_ticks(void) {
    return system_ticks;
}

// Get uptime in milliseconds
uint64_t irq_get_uptime_ms(void) {
    return (system_ticks * 1000) / timer_frequency_hz;
}

// Sleep for a specified number of milliseconds
void irq_sleep_ms(uint32_t milliseconds) {
    uint64_t target_ticks = system_ticks + ((milliseconds * timer_frequency_hz) / 1000);
    
    // Wait until we reach the target tick count
    while (system_ticks < target_ticks) {
        // Halt the CPU until next interrupt (power-saving)
        __asm__ volatile("hlt");
    }
}

// Dump IRQ statistics
void irq_dump_statistics(void) {
    printf("IRQ Statistics:\n");
    printf("  System ticks: %lu\n", system_ticks);
    printf("  Uptime: %lu ms\n", irq_get_uptime_ms());
    printf("  IRQ counts:\n");
    
    for (int i = 0; i < 16; i++) {
        if (irq_counts[i] > 0 || irq_spurious_counts[i] > 0) {
            printf("    IRQ %2d: %lu (spurious: %lu)\n", 
                    i, irq_counts[i], irq_spurious_counts[i]);
        }
    }
}

// Dump IRQ handler information
void irq_dump_handlers(void) {
    printf("IRQ Handlers:\n");
    
    for (int i = 0; i < 16; i++) {
        bool has_handlers = false;
        
        for (int j = 0; j < MAX_IRQ_HANDLERS; j++) {
            if (irq_handlers[i][j].active) {
                if (!has_handlers) {
                    printf("  IRQ %2d: %s\n", i, irq_is_enabled(i) ? "enabled" : "disabled");
                    has_handlers = true;
                }
                
                printf("    Handler %d at %p (context: %p)\n", 
                        j, 
                        (void*)irq_handlers[i][j].handler, 
                        irq_handlers[i][j].context);
            }
        }
        
        if (!has_handlers && irq_is_enabled(i)) {
            printf("  IRQ %2d: enabled (no handlers)\n", i);
        }
    }
}