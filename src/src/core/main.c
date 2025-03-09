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
#include <core/drivers/sata.h>
#include <core/drivers/pci.h>
#include <kstd/stdio.h>
#include <kstd/string.h>
#include <syncos/serial.h>
#include <kstd/asm.h>
#include <syncos/fs/ext4.h>
#include <syncos/elf.h>
#include <syncos/process.h>
#include <core/drivers/net/e1000.h>
#include <syncos/net/net.h>

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

bool kernel_init_processes(void) {
    // First initialize the process subsystem
    if (!process_init()) {
        printf("Failed to initialize process manager\n");
        return false;
    }

    // Initialize the process scheduler
    if (!process_scheduler_init()) {
        printf("Failed to initialize process scheduler\n");
        return false;
    }

    // Initialize process architecture-specific features
    if (!process_arch_init()) {
        printf("Failed to initialize process architecture\n");
        return false;
    }

    // Register the current kernel thread as the idle process
    if (!process_register_kernel_idle()) {
        printf("Failed to register kernel idle process\n");
        return false;
    }

    printf("Process subsystem initialized successfully\n");
    return true;
}

bool network_init(void) {
    printf("\n=== Network Initialization ===\n");
    
    // Find network devices through PCI
    bool network_available = false;
    
    // Find Intel E1000 network cards - use the PCI find functions
    uint32_t count = 0;
    pci_device_t* network_devices = pci_find_devices(0x02, 0x00, &count); // Network controller, Ethernet
    
    if (network_devices != NULL && count > 0) {
        for (uint32_t i = 0; i < count; i++) {
            pci_device_t* dev = &network_devices[i];
            
            // Check if this is an Intel E1000 network card
            if (dev->vendor_id == 0x8086 && 
                (dev->device_id == 0x100E || // 82540EM
                 dev->device_id == 0x100F || // 82545EM
                 dev->device_id == 0x10D3)) { // 82574L
                
                printf("Found Intel E1000 network card at bus %d, device %d, function %d\n",
                      dev->bus, dev->device, dev->function);
                
                // Enable the PCI device
                pci_enable_device(dev);
                
                // Get the I/O base address from BAR0
                uint16_t iobase = (uint16_t)pci_get_bar_address(dev, 0);
                
                e1000_init(i, iobase);
                
                // Initialize the networking stack
                net_init();
                
                network_available = true;
                break;
            }
        }
    }
    
    if (network_available) {
        printf("Network subsystem initialized successfully\n");
    } else {
        printf("No compatible network devices detected\n");
    }
    
    return network_available;
}

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

    // Initialize PCI subsystem (required for storage detection)
    pci_init();

    printf("\n=== Storage Device Detection ===\n");
    
    // Initialize storage drivers based on what's available
    bool storage_available = false;
    
    // Try to initialize NVMe
    bool nvme_available = nvme_init();
    if (nvme_available) {
        printf("NVMe storage detected and initialized successfully\n");
        printf("NVMe devices found: %u\n", nvme_get_device_count());
        
        // Print information about each NVMe device
        for (uint32_t i = 0; i < nvme_get_device_count(); i++) {
            nvme_debug_dump_controller(i);
        }
        
        storage_available = true;
    } else {
        printf("No NVMe devices detected or initialization failed\n");
    }
    
    // Try to initialize SATA
    bool sata_available = sata_init();
    if (sata_available) {
        printf("SATA storage detected and initialized successfully\n");
        printf("SATA ports found: %u\n", sata_get_port_count());
        
        // Print information about SATA devices
        sata_debug_dump_info();
        
        storage_available = true;
    } else {
        printf("No SATA devices detected or initialization failed\n");
    }
    
    // Storage status summary
    if (storage_available) {
        printf("\nStorage subsystem initialized successfully\n");
    } else {
        printf("\nWARNING: No storage devices were detected!\n");
        printf("The system will run without persistent storage capability.\n");
    }

    ext4_init();
    
    kernel_init_processes();

    bool network_available = network_init();
    if (!network_available) {
        printf("WARNING: No network devices were detected!\n");
        printf("The system will run without network connectivity.\n");
    }
    
    printf("\n==============================================\n");
    printf("       SyncOS - Finished Initialization       \n");
    printf("==============================================\n\n");
    
    // Hang forever
    while (1) {
        __asm__ volatile("hlt");
    }
}