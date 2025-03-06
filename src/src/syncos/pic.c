#include <syncos/pic.h>
#include <kstd/io.h>
#include <kstd/stdio.h>
#include <syncos/serial.h>

// PIC initialization constants
#define ICW1_ICW4       0x01    // ICW4 needed
#define ICW1_SINGLE     0x02    // Single (cascade) mode
#define ICW1_INTERVAL4  0x04    // Call address interval 4 (8)
#define ICW1_LEVEL      0x08    // Level triggered (edge) mode
#define ICW1_INIT       0x10    // Initialization - required!

#define ICW4_8086       0x01    // 8086/88 (MCS-80/85) mode
#define ICW4_AUTO       0x02    // Auto (normal) EOI
#define ICW4_BUF_SLAVE  0x08    // Buffered mode/slave
#define ICW4_BUF_MASTER 0x0C    // Buffered mode/master
#define ICW4_SFNM       0x10    // Special fully nested (not)

// Track current PIC state
static uint8_t pic_master_mask = 0xFF;  // All IRQs masked by default
static uint8_t pic_slave_mask = 0xFF;   // All IRQs masked by default
static uint8_t pic_master_offset = 0;
static uint8_t pic_slave_offset = 0;
static volatile _Atomic bool pic_initialized = false;

// Initialize the 8259 PIC
void pic_init(uint8_t master_offset, uint8_t slave_offset) {
    // Check if already initialized - test and set atomically
    bool was_initialized = __atomic_exchange_n(&pic_initialized, true, __ATOMIC_SEQ_CST);
    if (was_initialized) {
        printf("PIC: Already initialized\n");
        return;
    }
    
    // Save offsets for later use
    pic_master_offset = master_offset;
    pic_slave_offset = slave_offset;
    
    // ICW1: Start initialization sequence in cascade mode
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    
    // ICW2: Set vector offsets
    outb(PIC1_DATA, master_offset);  // Master PIC starts at offset
    io_wait();
    outb(PIC2_DATA, slave_offset);   // Slave PIC starts at offset+8
    io_wait();
    
    // ICW3: Tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    outb(PIC1_DATA, 4);
    io_wait();
    
    // ICW3: Tell Slave PIC its cascade identity (0000 0010)
    outb(PIC2_DATA, 2);
    io_wait();
    
    // ICW4: Set PICs to 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();
    
    // Mask all interrupts initially
    outb(PIC1_DATA, pic_master_mask);
    outb(PIC2_DATA, pic_slave_mask);
    
    printf("PIC: Initialized with offsets 0x%02X and 0x%02X\n", 
           master_offset, slave_offset);
}

// Disable the 8259 PIC entirely (useful when using APIC)
void pic_disable(void) {
    // If not initialized, nothing to do
    if (!__atomic_load_n(&pic_initialized, __ATOMIC_SEQ_CST)) {
        return;
    }
    
    // Mask all interrupts
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    
    // Update our tracking variables
    pic_master_mask = 0xFF;
    pic_slave_mask = 0xFF;
    
    printf("PIC: Disabled\n");
}

// Enable a specific IRQ
void pic_enable_irq(uint8_t irq) {
    if (irq >= 16) {
        printf("PIC: Invalid IRQ number %u\n", irq);
        return;
    }
    
    uint8_t mask;
    
    if (irq < 8) {
        // IRQ is on master PIC
        mask = pic_master_mask & ~(1 << irq);
        
        // Only update if actually changing
        if (mask != pic_master_mask) {
            pic_master_mask = mask;
            outb(PIC1_DATA, pic_master_mask);
        }
    } else {
        // IRQ is on slave PIC
        irq -= 8;  // Adjust to slave's numbering
        mask = pic_slave_mask & ~(1 << irq);
        
        // Only update if actually changing
        if (mask != pic_slave_mask) {
            pic_slave_mask = mask;
            outb(PIC2_DATA, pic_slave_mask);
            
            // Also make sure IRQ2 (cascade) is enabled on master
            if (pic_master_mask & (1 << 2)) {
                pic_master_mask &= ~(1 << 2);
                outb(PIC1_DATA, pic_master_mask);
            }
        }
    }
}

// Disable a specific IRQ
void pic_disable_irq(uint8_t irq) {
    if (irq >= 16) {
        printf("PIC: Invalid IRQ number %u\n", irq);
        return;
    }
    
    uint8_t mask;
    
    if (irq < 8) {
        // IRQ is on master PIC
        mask = pic_master_mask | (1 << irq);
        
        // Only update if actually changing
        if (mask != pic_master_mask) {
            pic_master_mask = mask;
            outb(PIC1_DATA, pic_master_mask);
        }
    } else {
        // IRQ is on slave PIC
        irq -= 8;  // Adjust to slave's numbering
        mask = pic_slave_mask | (1 << irq);
        
        // Only update if actually changing
        if (mask != pic_slave_mask) {
            pic_slave_mask = mask;
            outb(PIC2_DATA, pic_slave_mask);
        }
    }
}

// Send End-Of-Interrupt signal to PICs
void pic_send_eoi(uint8_t irq) {
    // If this came from the slave PIC, send EOI to both PICs
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    
    outb(PIC1_COMMAND, PIC_EOI);
}

// Read a specific register from the PIC
static uint16_t pic_get_register(uint8_t reg_command) {
    // Send the command to both PICs
    outb(PIC1_COMMAND, reg_command);
    outb(PIC2_COMMAND, reg_command);
    
    // Read the values with proper io_wait between operations
    uint8_t master_value = inb(PIC1_COMMAND);
    io_wait();
    uint8_t slave_value = inb(PIC2_COMMAND);
    
    // Combine both PICs' registers into a 16-bit value
    // Bits 0-7 = master PIC, Bits 8-15 = slave PIC
    return ((uint16_t)slave_value << 8) | master_value;
}

// Get the combined Interrupt Request Register (pending interrupts)
uint16_t pic_get_irr(void) {
    return pic_get_register(PIC_READ_IRR);
}

// Get the combined In-Service Register (interrupts being serviced)
uint16_t pic_get_isr(void) {
    return pic_get_register(PIC_READ_ISR);
}

// Check if an IRQ is currently being serviced
bool pic_is_irq_in_service(uint8_t irq) {
    if (irq >= 16) {
        return false;
    }
    
    uint16_t isr = pic_get_isr();
    return (isr & (1 << irq)) != 0;
}

// Check if an IRQ is pending
bool pic_is_irq_pending(uint8_t irq) {
    if (irq >= 16) {
        return false;
    }
    
    uint16_t irr = pic_get_irr();
    return (irr & (1 << irq)) != 0;
}

// Wait for an interrupt to finish processing
void pic_wait_for_irq_completion(uint8_t irq) {
    if (irq >= 16) {
        return;
    }
    
    // Wait until the IRQ is no longer in service
    while (pic_is_irq_in_service(irq)) {
        __asm__ volatile("pause");  // CPU hint for spin-wait
    }
}