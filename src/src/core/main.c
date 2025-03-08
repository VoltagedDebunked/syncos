#include <limine.h>
#include <syncos/gdt.h>
#include <syncos/idt.h>
#include <syncos/irq.h>
#include <syncos/pic.h>
#include <syncos/timer.h>
#include <syncos/keyboard.h>
#include <syncos/mouse.h>
#include <syncos/pmm.h>
#include <syncos/vmm.h>
#include <core/drivers/nvme.h>
#include <core/drivers/pci.h>
#include <kstd/stdio.h>
#include <kstd/string.h>
#include <syncos/serial.h>
#include <kstd/asm.h>

// Limine requests
__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(1);

// Memory map request
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

// Framebuffer request
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// Kernel main function
void kmain(void) {
    // Early serial initialization
    serial_init_stdio();
    
    printf("\n\n");
    printf("===================================\n");
    printf("        SyncOS - Starting...       \n");
    printf("===================================\n\n");

    // Hardware initialization
    gdt_init();
    idt_init();
    pic_init(0x20, 0x28);
    irq_init();
    timer_init(1000);
    keyboard_init();
    mouse_init();
    
    // Enable interrupts
    idt_enable_interrupts();

    // Initialize PMM
    if (memmap_request.response) {
        pmm_init(memmap_request.response, 0);
        
        // Initialize VMM after PMM
        vmm_init();
    } else {
        printf("ERROR: No memory map response from bootloader\n");
    }

    pci_init();

    if (nvme_init()) {
        printf("NVMe driver initialized successfully\n");
    } else {
        printf("NVMe driver initialization failed\n");
    }

    printf("\nSystem initialization complete.\n");
    
    // Hang forever
    while (1) {
        __asm__ volatile("hlt");
    }
}