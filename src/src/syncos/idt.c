#include <syncos/idt.h>
#include <syncos/isr.h>
#include <kstd/string.h>
#include <kstd/stdio.h>

#define EMERGENCY_PRINT(str) \
    do { \
        const char *msg = str; \
        for (int i = 0; msg[i]; i++) { \
            __asm__ volatile ("outb %%al, $0x3F8" : : "a"(msg[i])); \
        } \
        __asm__ volatile ("outb %%al, $0x3F8" : : "a"('\n')); \
    } while(0)

// Static IDT entries and IDTR
static idt_entry_t idt[IDT_ENTRIES] __attribute__((aligned(16)));
static idtr_t idtr __attribute__((aligned(16)));

// Exception handler registry
static exception_handler_t exception_handlers[IDT_ENTRIES] = {0};

// Atomic initialization flag
static volatile _Atomic bool initialized = false;

// External assembly interrupt wrapper functions
extern void *isr_stubs[];

// Set IDT gate with full address handling
static void idt_set_gate(uint8_t vector, void *handler, uint16_t selector, uint8_t type, uint8_t ist) {
    if (vector >= IDT_ENTRIES) {
        printf("ERROR: Invalid vector %d\n", vector);
        return;
    }

    uint64_t addr = (uint64_t)handler;
    
    idt[vector].offset_low = addr & 0xFFFF;
    idt[vector].selector = selector;
    idt[vector].ist = ist;
    idt[vector].type_attr = type;
    idt[vector].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void idt_init(void) {
    // Emergency initial debug
    EMERGENCY_PRINT("Entering idt_init");

    // Prevent multiple initialization
    bool was_initialized = __atomic_exchange_n(&initialized, true, __ATOMIC_SEQ_CST);
    if (was_initialized) {
        printf("IDT already initialized\n");
        return;
    }

    // Zero out IDT and IDTR
    printf("Zeroing IDT and IDTR...\n");
    memset(&idt, 0, sizeof(idt));
    memset(&idtr, 0, sizeof(idtr));
    
    // Configure IDTR
    idtr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idtr.base = (uint64_t)&idt[0];

    printf("IDTR: Limit=0x%x, Base=0x%lx\n", idtr.limit, idtr.base);

    // Configure interrupt gates
    uint16_t kernel_code_selector = 0x08;  // Matches your GDT setup

    printf("Setting up interrupt gates...\n");
    
    // Set gates for first 32 exception vectors
    for (int i = 0; i < 32; i++) {
        if (isr_stubs[i] == NULL) {
            printf("WARNING: NULL ISR stub for vector %d\n", i);
            continue;
        }
        
        idt_set_gate(i, isr_stubs[i], kernel_code_selector, IDT_GATE_INTERRUPT, 0);
    }

    // Attempt to load IDT
    printf("Loading IDT...\n");
    __asm__ volatile (
        "lidt %0"
        : 
        : "m" (idtr)
        : "memory"
    );

    // Validate IDT integrity
    if (idtr.base == 0 || idtr.limit == 0) {
        printf("ERROR: IDT initialization failed\n");
        EMERGENCY_PRINT("IDT INIT FAILED");
        __asm__ volatile ("cli; hlt");
    }

    // Final debug
    EMERGENCY_PRINT("IDT Init Complete");
    printf("IDT initialization complete\n");
}

// Internal register exception handler
void idt_register_exception_handler(uint8_t vector, exception_handler_t handler) {
    if (vector < IDT_ENTRIES) {
        exception_handlers[vector] = handler;
    }
}

// Generic exception handler
void handle_exception(uint64_t vector, uint64_t error_code, uint64_t rip) {
    // Check if a custom handler is registered
    if (exception_handlers[vector]) {
        exception_handlers[vector](error_code, rip);
        return;
    }
    
    // Default exception handling
    printf("\n!!! KERNEL EXCEPTION !!!\n");
    printf("Exception Vector: %lu\n", vector);
    printf("Error Code: %lu\n", error_code);
    printf("Instruction Pointer: 0x%lx\n", rip);
    
    // Halt the system
    __asm__ volatile ("cli; hlt");
}

// Enable interrupts
void idt_enable_interrupts(void) {
    __asm__ volatile ("sti");
}

// Disable interrupts
void idt_disable_interrupts(void) {
    __asm__ volatile ("cli");
}

// Check if interrupts are enabled
bool idt_are_interrupts_enabled(void) {
    unsigned long flags;
    __asm__ volatile (
        "pushfq\n\t"
        "pop %0"
        : "=r" (flags)
    );
    return flags & (1UL << 9);  // Check interrupt flag (IF)
}

// Set custom interrupt handler
void idt_set_handler(uint8_t vector, interrupt_handler_t handler, uint8_t type) {
    if (vector < IDT_ENTRIES) {
        uint16_t kernel_code_selector = 0x08;  // Matches your GDT setup
        idt_set_gate(vector, handler, kernel_code_selector, type, 0);
    }
}