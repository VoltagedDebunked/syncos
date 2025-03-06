#include <syncos/idt_extensions.h>
#include <syncos/idt.h>
#include <syncos/serial.h>
#include <kstd/io.h>
#include <kstd/stdio.h>
#include <kstd/string.h>

// Maximum number of callback handlers per IRQ
#define MAX_IRQ_CALLBACKS 8

// External reference to IDT table
extern idt_entry_t idt[IDT_ENTRIES];
extern idtr_t idtr;

// Extended exception handlers array
static extended_exception_handler_t extended_exception_handlers[IDT_ENTRIES] = {0};

// IRQ callback registry
typedef struct {
    interrupt_callback_t callback;
    void *context;
    bool used;
} irq_callback_entry_t;

static irq_callback_entry_t irq_callbacks[16][MAX_IRQ_CALLBACKS] = {{{0}}};

// PIC constants
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

#define PIC_EOI         0x20
#define PIC_READ_IRR    0x0A
#define PIC_READ_ISR    0x0B

#define ICW1_ICW4       0x01
#define ICW1_SINGLE     0x02
#define ICW1_INTERVAL4  0x04
#define ICW1_LEVEL      0x08
#define ICW1_INIT       0x10

#define ICW4_8086       0x01
#define ICW4_AUTO       0x02
#define ICW4_BUF_SLAVE  0x08
#define ICW4_BUF_MASTER 0x0C
#define ICW4_SFNM       0x10

// Track PIC remapping offsets
static uint8_t pic_master_offset = 0;
static uint8_t pic_slave_offset = 0;

// Exception names for better debugging
static const char *exception_names[] = {
    "Division Error",                   // 0
    "Debug Exception",                  // 1
    "Non-Maskable Interrupt",           // 2
    "Breakpoint",                       // 3
    "Overflow",                         // 4
    "Bound Range Exceeded",             // 5
    "Invalid Opcode",                   // 6
    "Device Not Available",             // 7
    "Double Fault",                     // 8
    "Coprocessor Segment Overrun",      // 9
    "Invalid TSS",                      // 10
    "Segment Not Present",              // 11
    "Stack-Segment Fault",              // 12
    "General Protection Fault",         // 13
    "Page Fault",                       // 14
    "Reserved",                         // 15
    "x87 Floating-Point Exception",     // 16
    "Alignment Check",                  // 17
    "Machine Check",                    // 18
    "SIMD Floating-Point Exception",    // 19
    "Virtualization Exception",         // 20
    "Control Protection Exception",     // 21
    "Reserved",                         // 22
    "Reserved",                         // 23
    "Reserved",                         // 24
    "Reserved",                         // 25
    "Reserved",                         // 26
    "Reserved",                         // 27
    "Reserved",                         // 28
    "Reserved",                         // 29
    "Security Exception",               // 30
    "Reserved"                          // 31
};

// Register an extended exception handler
void idt_register_extended_exception_handler(uint8_t vector, extended_exception_handler_t handler) {
    if (vector < IDT_ENTRIES) {
        extended_exception_handlers[vector] = handler;
    }
}

// Fetch the exception name for a given vector
const char *idt_exception_name(uint8_t vector) {
    if (vector <= 31) {
        return exception_names[vector];
    }
    return "Unknown Exception";
}

// Register an IRQ handler callback
bool idt_register_irq_handler(uint8_t irq, interrupt_callback_t callback, void *context) {
    if (irq >= 16 || !callback) {
        return false;
    }
    
    // Find an empty slot
    for (int i = 0; i < MAX_IRQ_CALLBACKS; i++) {
        if (!irq_callbacks[irq][i].used) {
            irq_callbacks[irq][i].callback = callback;
            irq_callbacks[irq][i].context = context;
            irq_callbacks[irq][i].used = true;
            return true;
        }
    }
    
    // No slots available
    return false;
}

// Unregister an IRQ handler
bool idt_unregister_irq_handler(uint8_t irq, interrupt_callback_t callback) {
    if (irq >= 16 || !callback) {
        return false;
    }
    
    // Find the callback
    for (int i = 0; i < MAX_IRQ_CALLBACKS; i++) {
        if (irq_callbacks[irq][i].used && irq_callbacks[irq][i].callback == callback) {
            irq_callbacks[irq][i].used = false;
            irq_callbacks[irq][i].callback = NULL;
            irq_callbacks[irq][i].context = NULL;
            return true;
        }
    }
    
    // Callback not found
    return false;
}

// Initialize the 8259 PIC with the specified offsets
void idt_pic_init(uint8_t master_offset, uint8_t slave_offset) {
    pic_master_offset = master_offset;
    pic_slave_offset = slave_offset;
    
    // Save masks
    uint8_t master_mask = inb(PIC1_DATA);
    uint8_t slave_mask = inb(PIC2_DATA);
    
    // ICW1: Start initialization sequence
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    
    // ICW2: Set vector offsets
    outb(PIC1_DATA, master_offset);
    io_wait();
    outb(PIC2_DATA, slave_offset);
    io_wait();
    
    // ICW3: Tell Master PIC that there is a slave PIC at IRQ2
    outb(PIC1_DATA, 4);
    io_wait();
    
    // ICW3: Tell Slave PIC its cascade identity
    outb(PIC2_DATA, 2);
    io_wait();
    
    // ICW4: Set 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    
    // Restore masks
    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
    
    printf("PIC initialized with master offset 0x%02X, slave offset 0x%02X\n", 
           master_offset, slave_offset);
}

// Disable the legacy PIC (useful when using APIC)
void idt_pic_disable(void) {
    // Mask all interrupts
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    
    printf("PIC disabled\n");
}

// Mask (disable) a specific IRQ
void idt_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
}

// Unmask (enable) a specific IRQ
void idt_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

// Signal end of interrupt to the PIC
void idt_end_of_interrupt(uint8_t irq) {
    // If this is from the slave PIC, send EOI to both PICs
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    
    outb(PIC1_COMMAND, PIC_EOI);
}

// Set privilege level for an interrupt vector
void idt_set_privilege_level(uint8_t vector, uint8_t ring_level) {
    if (vector >= IDT_ENTRIES || ring_level > 3) {
        return;
    }
    
    // Get the current type and set the DPL bits (bits 5-6)
    uint8_t type = idt[vector].type_attr & 0x8F;  // Preserve present and type bits
    type |= (ring_level & 0x3) << 5;              // Set DPL bits
    
    idt[vector].type_attr = type;
}

// Set the Interrupt Stack Table (IST) index for a vector
void idt_set_ist_stack(uint8_t vector, uint8_t ist_index) {
    if (vector >= IDT_ENTRIES || ist_index > 7) {
        return;
    }
    
    idt[vector].ist = ist_index & 0x7;
}

// Dump a single IDT entry for debugging
void idt_dump_entry(uint8_t vector) {
    if (vector >= IDT_ENTRIES) {
        printf("Invalid vector: %u\n", vector);
        return;
    }
    
    idt_entry_t *entry = &idt[vector];
    uint64_t addr = (uint64_t)entry->offset_low | 
                   ((uint64_t)entry->offset_mid << 16) | 
                   ((uint64_t)entry->offset_high << 32);
    
    printf("IDT[%3u]: addr=0x%016lx sel=0x%04x type=0x%02x ist=%u dpl=%u %s\n",
           vector, addr, entry->selector, entry->type_attr & 0x8F, 
           entry->ist & 0x7, (entry->type_attr >> 5) & 0x3,
           (entry->type_attr & 0x80) ? "present" : "not-present");
}

// Dump the entire IDT for debugging
void idt_dump_table(void) {
    if (idtr.base == 0) {
        printf("IDT not initialized\n");
        return;
    }
    
    printf("IDTR: base=0x%016lx limit=0x%04x\n", idtr.base, idtr.limit);
    
    // Dump the first 32 entries (exceptions)
    printf("Exception vectors:\n");
    for (int i = 0; i < 32; i++) {
        if (idt[i].type_attr & 0x80) { // If present
            idt_dump_entry(i);
        }
    }
    
    // Dump PIC IRQs
    if (pic_master_offset > 0) {
        printf("\nPIC IRQs:\n");
        for (int i = 0; i < 16; i++) {
            uint8_t vector = (i < 8) ? (pic_master_offset + i) : (pic_slave_offset + (i - 8));
            if (idt[vector].type_attr & 0x80) { // If present
                idt_dump_entry(vector);
            }
        }
    }
}

// This function is called from the ISR wrapper to handle IRQs
// The actual implementation will be in isr.c, but this prototype ensures we can reference it
bool idt_dispatch_irq(uint8_t irq) {
    bool handled = false;
    
    for (int i = 0; i < MAX_IRQ_CALLBACKS; i++) {
        if (irq_callbacks[irq][i].used && irq_callbacks[irq][i].callback) {
            handled |= irq_callbacks[irq][i].callback(irq, irq_callbacks[irq][i].context);
        }
    }
    
    return handled;
}