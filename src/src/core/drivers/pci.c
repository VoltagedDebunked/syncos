#include <core/drivers/pci.h>
#include <kstd/stdio.h>
#include <kstd/io.h>
#include <kstd/string.h>
#include <syncos/pmm.h>
#include <syncos/vmm.h>

// Maximum number of PCI devices to detect
#define MAX_PCI_DEVICES 32

// PCI configuration address/data ports
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// Static list of detected PCI devices
pci_device_t pci_devices[MAX_PCI_DEVICES];
uint32_t pci_device_count = 0;

// Generate PCI configuration address value
static uint32_t pci_make_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (1 << 31) | 
           ((uint32_t)bus << 16) | 
           ((uint32_t)device << 11) | 
           ((uint32_t)function << 8) | 
           (offset & 0xFC);
}

// Read 8 bits from PCI configuration space
uint8_t pci_read_config_byte(pci_device_t *device, uint8_t offset) {
    uint32_t address = pci_make_address(device->bus, device->device, device->function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inb(PCI_CONFIG_DATA + (offset & 3));
}

// Read 16 bits from PCI configuration space
uint16_t pci_read_config_word(pci_device_t *device, uint8_t offset) {
    uint32_t address = pci_make_address(device->bus, device->device, device->function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inw(PCI_CONFIG_DATA + (offset & 2));
}

// Read 32 bits from PCI configuration space
uint32_t pci_read_config_dword(pci_device_t *device, uint8_t offset) {
    uint32_t address = pci_make_address(device->bus, device->device, device->function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

// Write 8 bits to PCI configuration space
void pci_write_config_byte(pci_device_t *device, uint8_t offset, uint8_t value) {
    uint32_t address = pci_make_address(device->bus, device->device, device->function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outb(PCI_CONFIG_DATA + (offset & 3), value);
}

// Write 16 bits to PCI configuration space
void pci_write_config_word(pci_device_t *device, uint8_t offset, uint16_t value) {
    uint32_t address = pci_make_address(device->bus, device->device, device->function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outw(PCI_CONFIG_DATA + (offset & 2), value);
}

// Write 32 bits to PCI configuration space
void pci_write_config_dword(pci_device_t *device, uint8_t offset, uint32_t value) {
    uint32_t address = pci_make_address(device->bus, device->device, device->function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// Check if a PCI device exists
static bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t address = pci_make_address(bus, device, function, 0);
    outl(PCI_CONFIG_ADDRESS, address);
    uint16_t vendor = inw(PCI_CONFIG_DATA);
    return vendor != 0xFFFF;
}

// Read basic PCI device information
static void pci_read_device_info(uint8_t bus, uint8_t device, uint8_t function, pci_device_t *pci_dev) {
    pci_dev->bus = bus;
    pci_dev->device = device;
    pci_dev->function = function;
    
    // Read device identification
    uint32_t address = pci_make_address(bus, device, function, 0);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t id_reg = inl(PCI_CONFIG_DATA);
    pci_dev->vendor_id = id_reg & 0xFFFF;
    pci_dev->device_id = (id_reg >> 16) & 0xFFFF;
    
    // Read class, subclass, and interface
    address = pci_make_address(bus, device, function, 8);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t class_reg = inl(PCI_CONFIG_DATA);
    pci_dev->revision_id = class_reg & 0xFF;
    pci_dev->prog_if = (class_reg >> 8) & 0xFF;
    pci_dev->subclass = (class_reg >> 16) & 0xFF;
    pci_dev->class_code = (class_reg >> 24) & 0xFF;
    
    // Read header type
    address = pci_make_address(bus, device, function, 0x0C);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t header_reg = inl(PCI_CONFIG_DATA);
    pci_dev->header_type = (header_reg >> 16) & 0xFF;
    
    // Read interrupt information
    address = pci_make_address(bus, device, function, 0x3C);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t int_reg = inl(PCI_CONFIG_DATA);
    pci_dev->interrupt_line = int_reg & 0xFF;
    pci_dev->interrupt_pin = (int_reg >> 8) & 0xFF;
}

// Check if a PCI device is a multi-function device
static bool pci_is_multifunction(uint8_t bus, uint8_t device) {
    uint32_t address = pci_make_address(bus, device, 0, 0x0E);
    outl(PCI_CONFIG_ADDRESS, address);
    uint8_t header_type = inb(PCI_CONFIG_DATA);
    return (header_type & PCI_HEADER_TYPE_MULTI_FUNC) != 0;
}

// Enumerate PCI devices on the system
static void pci_enumerate_devices() {
    pci_device_count = 0;
    
    // Scan all buses, devices, and functions
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            bool is_multifunction = pci_is_multifunction(bus, device);
            
            // Check function 0 for all devices
            if (pci_device_exists(bus, device, 0)) {
                if (pci_device_count < MAX_PCI_DEVICES) {
                    pci_read_device_info(bus, device, 0, &pci_devices[pci_device_count]);
                    pci_device_count++;
                }
            }
            
            // If multi-function device, check other functions
            if (is_multifunction) {
                for (uint8_t function = 1; function < 8; function++) {
                    if (pci_device_exists(bus, device, function)) {
                        if (pci_device_count < MAX_PCI_DEVICES) {
                            pci_read_device_info(bus, device, function, &pci_devices[pci_device_count]);
                            pci_device_count++;
                        }
                    }
                }
            }
        }
    }
}

// Initialize PCI subsystem
void pci_init(void) {
    printf("PCI: Initializing PCI bus\n");
    pci_enumerate_devices();
    printf("PCI: Detected %u devices\n", pci_device_count);
}

// Find PCI devices matching a given class and subclass
pci_device_t* pci_find_devices(uint8_t class_code, uint8_t subclass, uint32_t* count) {
    static pci_device_t matching_devices[MAX_PCI_DEVICES];
    uint32_t match_count = 0;
    
    // Find all matching devices
    for (uint32_t i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code && 
            pci_devices[i].subclass == subclass) {
            if (match_count < MAX_PCI_DEVICES) {
                matching_devices[match_count] = pci_devices[i];
                match_count++;
            }
        }
    }
    
    // Set the count of found devices
    if (count) {
        *count = match_count;
    }
    
    return match_count > 0 ? matching_devices : NULL;
}

// Find a specific PCI device by vendor and device ID
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (uint32_t i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && 
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    
    return NULL;
}

// Get a BAR address from a PCI device
uintptr_t pci_get_bar_address(pci_device_t *device, uint8_t bar) {
    if (bar > 5) {
        return 0;
    }
    
    uint32_t bar_value = pci_read_config_dword(device, PCI_CONFIG_BAR0 + (bar * 4));
    
    // Check BAR type (memory or I/O)
    if ((bar_value & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_MEMORY) {
        // Memory BAR
        uintptr_t address = bar_value & PCI_BAR_MEMORY_MASK;
        
        // Check if 64-bit BAR (which takes up 2 BAR slots)
        if ((bar_value & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_TYPE_64) {
            if (bar < 5) {
                uint32_t high_dword = pci_read_config_dword(device, PCI_CONFIG_BAR0 + ((bar + 1) * 4));
                address |= ((uintptr_t)high_dword << 32);
            }
        }
        
        return address;
    } else {
        // I/O BAR
        return bar_value & PCI_BAR_IO_MASK;
    }
}

// Get a BAR size from a PCI device
uint32_t pci_get_bar_size(pci_device_t *device, uint8_t bar) {
    if (bar > 5) {
        return 0;
    }
    
    uint8_t bar_offset = PCI_CONFIG_BAR0 + (bar * 4);
    uint32_t original = pci_read_config_dword(device, bar_offset);
    
    // Write all 1s to the BAR
    pci_write_config_dword(device, bar_offset, 0xFFFFFFFF);
    
    // Read it back - the hardware will return a bit mask of the size
    uint32_t size_mask = pci_read_config_dword(device, bar_offset);
    
    // Restore the original value
    pci_write_config_dword(device, bar_offset, original);
    
    // Check BAR type
    if ((original & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_MEMORY) {
        // Memory BAR - mask appropriate bits
        size_mask &= PCI_BAR_MEMORY_MASK;
    } else {
        // I/O BAR - mask appropriate bits
        size_mask &= PCI_BAR_IO_MASK;
    }
    
    // If the BAR is not implemented, return 0
    if (size_mask == 0) {
        return 0;
    }
    
    // The size is the complement of the mask plus 1
    return (~size_mask) + 1;
}

// Enable bus mastering for a PCI device
bool pci_enable_bus_mastering(pci_device_t *device) {
    uint16_t command = pci_read_config_word(device, PCI_CONFIG_COMMAND);
    
    // Enable bus mastering
    command |= PCI_COMMAND_MASTER;
    pci_write_config_word(device, PCI_CONFIG_COMMAND, command);
    
    // Verify that bus mastering is enabled
    command = pci_read_config_word(device, PCI_CONFIG_COMMAND);
    return (command & PCI_COMMAND_MASTER) != 0;
}

// Enable a PCI device for memory and I/O operations
void pci_enable_device(pci_device_t *device) {
    uint16_t command = pci_read_config_word(device, PCI_CONFIG_COMMAND);
    
    // Enable memory and I/O space, bus mastering
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_write_config_word(device, PCI_CONFIG_COMMAND, command);
}

// Dump information about a PCI device
void pci_dump_device_info(pci_device_t *device) {
    printf("PCI Device %02x:%02x.%x:\n", 
           device->bus, device->device, device->function);
    printf("  Vendor: 0x%04X, Device: 0x%04X\n", 
           device->vendor_id, device->device_id);
    printf("  Class: 0x%02X, Subclass: 0x%02X, ProgIF: 0x%02X\n", 
           device->class_code, device->subclass, device->prog_if);
    printf("  IRQ Line: %u, Pin: %u\n", 
           device->interrupt_line, device->interrupt_pin);
    
    // Print BAR information
    for (uint8_t bar = 0; bar < 6; bar++) {
        uint32_t bar_value = pci_read_config_dword(device, PCI_CONFIG_BAR0 + (bar * 4));
        if (bar_value == 0) {
            continue;
        }
        
        uintptr_t address = pci_get_bar_address(device, bar);
        uint32_t size = pci_get_bar_size(device, bar);
        
        if ((bar_value & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_MEMORY) {
            printf("  BAR%d: Memory at 0x%lX (%u bytes)\n", bar, address, size);
            
            // Skip the next BAR if this is a 64-bit BAR
            if ((bar_value & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_TYPE_64) {
                bar++;
            }
        } else {
            printf("  BAR%d: I/O at 0x%X (%u bytes)\n", bar, (uint32_t)address, size);
        }
    }
}