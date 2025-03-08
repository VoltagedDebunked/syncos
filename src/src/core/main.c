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

void test_storage_devices(void) {
    printf("\n=== Storage Test ===\n");
    
    // Buffer for read/write tests (4KB)
    uint8_t test_buffer[4096];
    uint8_t verify_buffer[4096];
    
    // Initialize test pattern
    for (int i = 0; i < 4096; i++) {
        test_buffer[i] = (i & 0xFF);
    }
    
    // Test NVMe devices if available
    uint32_t nvme_count = nvme_get_device_count();
    if (nvme_count > 0) {
        printf("Testing NVMe devices...\n");
        
        for (uint32_t dev = 0; dev < nvme_count; dev++) {
            // Get namespace count for this device
            uint32_t ns_count = nvme_get_namespace_count(dev);
            
            for (uint32_t ns = 1; ns <= ns_count; ns++) {
                // Get namespace info to determine sector size
                uint64_t sector_count;
                uint32_t sector_size;
                
                if (nvme_get_namespace_info(dev, ns, &sector_count, &sector_size)) {
                    printf("NVMe Device %u, Namespace %u: %lu sectors, %u bytes/sector\n", 
                           dev, ns, sector_count, sector_size);
                    
                    // Only run the test if the device has enough sectors
                    if (sector_count >= 10) {
                        // Write test pattern to LBA 5-6 (safe area)
                        printf("  Writing test pattern to LBA 5...");
                        if (nvme_write(dev, ns, 5, test_buffer, 1)) {
                            printf("Success\n");
                            
                            // Read back data from LBA 5
                            printf("  Reading back from LBA 5...");
                            memset(verify_buffer, 0, 4096);
                            
                            if (nvme_read(dev, ns, 5, verify_buffer, 1)) {
                                printf("Success\n");
                                
                                // Verify data
                                printf("  Verifying data...");
                                bool match = true;
                                for (uint32_t i = 0; i < sector_size; i++) {
                                    if (verify_buffer[i] != test_buffer[i]) {
                                        match = false;
                                        printf("Mismatch at offset %u: expected 0x%02X, got 0x%02X\n", 
                                               i, test_buffer[i], verify_buffer[i]);
                                        break;
                                    }
                                }
                                
                                if (match) {
                                    printf("Data verified correctly!\n");
                                }
                            } else {
                                printf("Failed\n");
                            }
                        } else {
                            printf("Failed\n");
                        }
                    } else {
                        printf("  Device too small for testing\n");
                    }
                }
            }
        }
    }
    
    // Test SATA devices if available
    uint32_t sata_count = sata_get_port_count();
    if (sata_count > 0) {
        printf("\nTesting SATA devices...\n");
        
        for (uint32_t port = 0; port < sata_count; port++) {
            // Get port info
            char port_info[256];
            if (sata_get_port_info(port, port_info, sizeof(port_info))) {
                printf("%s\n", port_info);
                
                // Write test pattern to LBA 5-6 (safe area)
                printf("  Writing test pattern to LBA 5...");
                if (sata_write(port, 5, test_buffer, 1)) {
                    printf("Success\n");
                    
                    // Flush cache to ensure data is written
                    sata_flush(port);
                    
                    // Read back data from LBA 5
                    printf("  Reading back from LBA 5...");
                    memset(verify_buffer, 0, 4096);
                    
                    if (sata_read(port, 5, verify_buffer, 1)) {
                        printf("Success\n");
                        
                        // Verify data
                        printf("  Verifying data...");
                        bool match = true;
                        // Assume 512 bytes per sector if we couldn't get exact size
                        uint32_t sector_size = 512;
                        
                        for (uint32_t i = 0; i < sector_size; i++) {
                            if (verify_buffer[i] != test_buffer[i]) {
                                match = false;
                                printf("Mismatch at offset %u: expected 0x%02X, got 0x%02X\n", 
                                       i, test_buffer[i], verify_buffer[i]);
                                break;
                            }
                        }
                        
                        if (match) {
                            printf("Data verified correctly!\n");
                        }
                    } else {
                        printf("Failed\n");
                    }
                } else {
                    printf("Failed\n");
                }
            } else {
                printf("Failed to get info for SATA port %u\n", port);
            }
        }
    }
    
    printf("\nStorage test completed!\n");
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

    printf("\n==============================================\n");
    printf("       SyncOS - Finished Initialization       \n");
    printf("==============================================\n\n");

    test_storage_devices();
    
    // Hang forever
    while (1) {
        __asm__ volatile("hlt");
    }
}