#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <syncos/gdt.h>
#include <syncos/idt.h>
#include <syncos/serial.h>
#include <kstd/string.h>
#include <kstd/stdio.h>
#include <kstd/asm.h>
#include <syncos/irq.h>
#include <syncos/timer.h>
#include <syncos/pic.h>

// Limine requests
__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

// Kernel stack - aligned to 16 bytes as required by the SysV ABI
__attribute__((section(".bss"), aligned(16)))
static uint8_t kernel_stack[65536]; // 64KB kernel stack

// Define the top of the stack
uint8_t *kernel_stack_top = &kernel_stack[sizeof(kernel_stack)];

// Early initialization
static void early_init(void) {
    // Initialize serial port for console output
    serial_init_stdio();
    
    // Print banner
    printf("\n\n");
    printf("===================================\n");
    printf("     SyncOS - Kernel Starting      \n");
    printf("===================================\n\n");
    
    printf("Early initialization complete\n");
}

// Hardware initialization
static void hw_init(void) {
    // Initialize the GDT
    gdt_init();
    
    // Set up the kernel stack in the TSS
    gdt_set_kernel_stack((uint64_t)kernel_stack_top);
    
    // Initialize the Interrupt Descriptor Table
    idt_init();
    pic_init(0x20, 0x28); // Remap PIC IRQs to vectors 0x20-0x2F
    irq_init();           // Initialize IRQ system
    timer_init(1000);     // Initialize timer with 1000Hz frequency (1ms resolution)
    idt_enable_interrupts();
    
    printf("Hardware initialization complete\n");
}

// Main kernel entry point
void kmain(void) {
    // Early initialization
    early_init();
    
    // Ensure the bootloader actually understands our base revision
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        printf("ERROR: Bootloader does not support requested base revision\n");
        hlt();
    }

    // Ensure we got a framebuffer
    if (framebuffer_request.response == NULL || 
        framebuffer_request.response->framebuffer_count < 1) {
        printf("ERROR: No framebuffer provided by bootloader\n");
        hlt();
    }

    // Fetch the first framebuffer
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
    printf("Framebuffer: %dx%d, %d BPP\n", 
            framebuffer->width, 
            framebuffer->height, 
            framebuffer->bpp);
    
    // Initialize hardware components
    hw_init();

    
    // If we reach here, something went wrong
    printf("\nSystem initialization complete. Entering idle loop.\n");
    
    // Hang forever
    while (1) {
        __asm__ volatile("hlt");
    }
}