#include <core/drivers/sata.h>
#include <core/drivers/pci.h>
#include <syncos/vmm.h>
#include <syncos/pmm.h>
#include <syncos/spinlock.h>
#include <syncos/timer.h>
#include <kstd/stdio.h>
#include <kstd/string.h>
#include <kstd/io.h>

// Global SATA driver state
static sata_driver_t sata_driver;

// Define debugging macros
#define SATA_DEBUG 1
#ifdef SATA_DEBUG
#define SATA_TRACE(fmt, ...) printf("SATA: " fmt "\n", ##__VA_ARGS__)
#else
#define SATA_TRACE(fmt, ...) 
#endif

// AHCI PCI device information
#define AHCI_PCI_CLASS     0x01  // Mass Storage Controller
#define AHCI_PCI_SUBCLASS  0x06  // SATA Controller
#define AHCI_PCI_PROGIF    0x01  // AHCI 1.0

// AHCI register bit definitions (additional to those in header)
#define AHCI_CAP_S64A      (1 << 31)  // Supports 64-bit addressing
#define AHCI_CAP_NCQ       (1 << 30)  // Supports Native Command Queuing
#define AHCI_CAP_SSS       (1 << 27)  // Supports staggered spin-up
#define AHCI_CAP_NCS_MASK  0x1F00     // Number of command slots mask
#define AHCI_CAP_NCS_SHIFT 8          // Number of command slots shift

// Port status
#define HBA_PxIS_TFES      (1 << 30)  // Task File Error Status

// Port Task File Data Status bits
#define HBA_PxTFD_BSY      0x80       // Interface is busy
#define HBA_PxTFD_DRQ      0x08       // Data transfer requested
#define HBA_PxTFD_ERR      0x01       // Error bit

// Command timeout
#define SATA_COMMAND_TIMEOUT_MS 5000  // 5 seconds timeout for commands

// Memory allocation sizes
#define COMMAND_LIST_SIZE  1024        // Command list size (1KB per port)
#define RECEIVED_FIS_SIZE  256         // Received FIS buffer size (256B per port)
#define COMMAND_TABLE_SIZE 256         // Command table size (256B per command slot)
#define DMA_BUFFER_SIZE    (128 * 1024) // 128KB per port for DMA transfers

// Static function declarations
static bool find_and_init_controllers(void);
static bool init_controller(pci_device_t* pci_dev);
static bool init_port(sata_controller_t* controller, uint32_t port_num);
static void stop_port(hba_port_t* port);
static void start_port(hba_port_t* port);
static bool reset_port(sata_controller_t* controller, uint32_t port_num);
static bool identify_device(sata_port_t* port);
static bool allocate_port_memory(sata_port_t* port);
static void free_port_memory(sata_port_t* port);
static int find_command_slot(sata_port_t* port);
static void* alloc_dma_buffer(size_t size, uintptr_t* phys_addr);
static void free_dma_buffer(void* virt_addr, size_t size);
static bool wait_for_command_completion(sata_port_t* port, uint32_t slot, uint32_t timeout_ms);
static bool port_send_command(sata_port_t* port, uint32_t slot, uint8_t cmd, uint64_t lba, 
                             uint32_t count, void* buffer, bool write);

// Initialize the SATA driver
bool sata_init(void) {
    SATA_TRACE("Initializing SATA driver");
    
    // Initialize driver state
    memset(&sata_driver, 0, sizeof(sata_driver));
    spinlock_init(&sata_driver.global_lock);
    
    // Find and initialize AHCI controllers
    if (!find_and_init_controllers()) {
        SATA_TRACE("No AHCI controllers found or initialized");
        return false;
    }
    
    sata_driver.initialized = true;
    SATA_TRACE("SATA driver initialized with %u controller(s), %u total ports", 
               sata_driver.controller_count, sata_driver.total_ports);
    
    // Debug output
    sata_debug_dump_info();
    
    return true;
}

// Shutdown the SATA driver
void sata_shutdown(void) {
    if (!sata_driver.initialized) {
        return;
    }
    
    spinlock_acquire(&sata_driver.global_lock);
    
    // Stop all ports and free resources
    for (uint32_t i = 0; i < sata_driver.controller_count; i++) {
        sata_controller_t* controller = &sata_driver.controllers[i];
        
        if (!controller->initialized) {
            continue;
        }
        
        // Stop all active ports
        for (uint32_t j = 0; j < 32; j++) {
            if (controller->ports_active & (1 << j)) {
                sata_port_t* port = &controller->ports[j];
                
                spinlock_acquire(&port->lock);
                stop_port(port->port_base);
                free_port_memory(port);
                spinlock_release(&port->lock);
            }
        }
        
        // Unmap ABAR
        if (controller->abar) {
            vmm_unmap_physical(controller->abar, 4096);
            controller->abar = NULL;
        }
        
        controller->initialized = false;
    }
    
    sata_driver.controller_count = 0;
    sata_driver.total_ports = 0;
    sata_driver.initialized = false;
    
    spinlock_release(&sata_driver.global_lock);
    
    SATA_TRACE("SATA driver shutdown complete");
}

// Find and initialize AHCI controllers
bool find_and_init_controllers(void) {
    // Find SATA AHCI controllers
    uint32_t count = 0;
    pci_device_t* devices = pci_find_devices(AHCI_PCI_CLASS, AHCI_PCI_SUBCLASS, &count);
    
    if (!devices || count == 0) {
        SATA_TRACE("No AHCI controllers found on PCI bus");
        return false;
    }
    
    // Limit to max supported controllers
    if (count > SATA_MAX_CONTROLLERS) {
        SATA_TRACE("Limiting to %u controller(s)", SATA_MAX_CONTROLLERS);
        count = SATA_MAX_CONTROLLERS;
    }
    
    uint32_t initialized = 0;
    
    // Initialize each controller
    for (uint32_t i = 0; i < count; i++) {
        pci_device_t* pci_dev = &devices[i];
        
        if (init_controller(pci_dev)) {
            initialized++;
        }
    }
    
    return initialized > 0;
}

// Initialize an AHCI controller
bool init_controller(pci_device_t* pci_dev) {
    // Skip if we're at max controllers
    if (sata_driver.controller_count >= SATA_MAX_CONTROLLERS) {
        return false;
    }
    
    SATA_TRACE("Initializing AHCI controller at %02x:%02x.%x", 
               pci_dev->bus, pci_dev->device, pci_dev->function);
    
    // Get controller index
    uint32_t controller_idx = sata_driver.controller_count;
    sata_controller_t* controller = &sata_driver.controllers[controller_idx];
    
    // Store PCI information
    controller->pci_bus = pci_dev->bus;
    controller->pci_device = pci_dev->device;
    controller->pci_function = pci_dev->function;
    controller->vendor_id = pci_dev->vendor_id;
    controller->device_id = pci_dev->device_id;
    
    // Enable the device
    pci_enable_device(pci_dev);
    
    // Get ABAR (AHCI Base Memory Register)
    controller->abar_phys = pci_get_bar_address(pci_dev, 5); // BAR5 for AHCI
    if (controller->abar_phys == 0) {
        SATA_TRACE("Failed to get AHCI bar address");
        return false;
    }
    
    size_t abar_size = pci_get_bar_size(pci_dev, 5);
    if (abar_size < sizeof(hba_mem_t)) {
        abar_size = sizeof(hba_mem_t); // Ensure we map enough memory
    }
    
    // Map the ABAR to virtual memory
    controller->abar = vmm_map_physical(controller->abar_phys, abar_size, 
                                       VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NOCACHE);
    if (!controller->abar) {
        SATA_TRACE("Failed to map AHCI bar memory");
        return false;
    }
    
    SATA_TRACE("AHCI controller at %p (phys: 0x%lx), size: %zu bytes", 
               controller->abar, controller->abar_phys, abar_size);
    
    // Perform HBA reset
    controller->abar->ghc |= AHCI_GHC_HR;
    
    // Wait for reset to complete
    uint64_t timeout = timer_get_uptime_ms() + 1000; // 1 second timeout
    while (controller->abar->ghc & AHCI_GHC_HR) {
        if (timer_get_uptime_ms() >= timeout) {
            SATA_TRACE("HBA reset timed out");
            vmm_unmap_physical(controller->abar, abar_size);
            return false;
        }
        
        timer_sleep_ms(10);
    }
    
    // Enable AHCI mode
    controller->abar->ghc |= AHCI_GHC_AE;
    
    // Get ports implemented
    controller->ports_impl = controller->abar->pi;
    
    // Get number of command slots
    controller->slot_count = ((controller->abar->cap & AHCI_CAP_NCS_MASK) >> AHCI_CAP_NCS_SHIFT) + 1;
    if (controller->slot_count > 32) {
        controller->slot_count = 32; // Clamp to maximum
    }
    
    SATA_TRACE("Controller has %u command slots, ports_impl=0x%x", 
               controller->slot_count, controller->ports_impl);
    
    // Initialize each implemented port
    controller->ports_active = 0;
    for (uint32_t i = 0; i < 32; i++) {
        if (controller->ports_impl & (1 << i)) {
            if (init_port(controller, i)) {
                controller->ports_active |= (1 << i);
                sata_driver.total_ports++;
            }
        }
    }
    
    if (controller->ports_active == 0) {
        SATA_TRACE("No active ports on controller");
        vmm_unmap_physical(controller->abar, abar_size);
        return false;
    }
    
    // Mark controller as initialized
    controller->initialized = true;
    sata_driver.controller_count++;
    
    SATA_TRACE("AHCI controller initialized with %u active ports", 
               __builtin_popcount(controller->ports_active));
    
    return true;
}

// Initialize a port
bool init_port(sata_controller_t* controller, uint32_t port_num) {
    if (sata_driver.total_ports >= SATA_MAX_PORTS) {
        SATA_TRACE("Maximum ports reached, skipping port %u", port_num);
        return false;
    }
    
    // Get port registers
    hba_port_t* port_base = &controller->abar->ports[port_num];
    
    // Check if device is present on this port
    uint32_t ssts = port_base->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;
    
    if (det != 3 || ipm != 1) {
        SATA_TRACE("No device present on port %u (det=%u, ipm=%u)", port_num, det, ipm);
        return false;
    }
    
    // Configure port
    sata_port_t* port = &controller->ports[port_num];
    port->port_base = port_base;
    port->port_num = port_num;
    port->controller_id = sata_driver.controller_count;
    port->status = SATA_PORT_STATUS_PRESENT;
    
    // Initialize port lock
    spinlock_init(&port->lock);
    
    // Stop the port command engine
    if (!reset_port(controller, port_num)) {
        SATA_TRACE("Failed to reset port %u", port_num);
        return false;
    }
    
    // Allocate memory for command structures
    if (!allocate_port_memory(port)) {
        SATA_TRACE("Failed to allocate memory for port %u", port_num);
        return false;
    }
    
    // Start the port
    start_port(port_base);
    
    // Identify the device
    if (!identify_device(port)) {
        SATA_TRACE("Failed to identify device on port %u", port_num);
        stop_port(port_base);
        free_port_memory(port);
        return false;
    }
    
    port->status = SATA_PORT_STATUS_ACTIVE;
    
    SATA_TRACE("Port %u initialized successfully: %s, %s", port_num, port->model, port->serial);
    return true;
}

// Stop port command engine
void stop_port(hba_port_t* port) {
    // Clear ST bit to stop port
    port->cmd &= ~AHCI_PxCMD_ST;
    
    // Wait for CR bit to clear
    uint64_t timeout = timer_get_uptime_ms() + 500; // 500ms timeout
    while (port->cmd & AHCI_PxCMD_CR) {
        if (timer_get_uptime_ms() >= timeout) {
            SATA_TRACE("Warning: CR bit didn't clear when stopping port");
            break;
        }
        
        timer_sleep_ms(1);
    }
    
    // Clear FRE bit
    port->cmd &= ~AHCI_PxCMD_FRE;
    
    // Wait for FR bit to clear
    timeout = timer_get_uptime_ms() + 500; // 500ms timeout
    while (port->cmd & AHCI_PxCMD_FR) {
        if (timer_get_uptime_ms() >= timeout) {
            SATA_TRACE("Warning: FR bit didn't clear when stopping port");
            break;
        }
        
        timer_sleep_ms(1);
    }
}

// Start port command engine
void start_port(hba_port_t* port) {
    // Wait until CR bit is cleared
    while (port->cmd & AHCI_PxCMD_CR) {
        timer_sleep_ms(1);
    }
    
    // Set FRE bit
    port->cmd |= AHCI_PxCMD_FRE;
    
    // Set ST bit
    port->cmd |= AHCI_PxCMD_ST;
}

// Reset a port
bool reset_port(sata_controller_t* controller, uint32_t port_num) {
    hba_port_t* port = &controller->abar->ports[port_num];
    
    // Stop the port if it's running
    stop_port(port);
    
    // Clear pending interrupts
    port->is = (uint32_t)-1;
    
    return true;
}

bool identify_device(sata_port_t* port) {
    // Check if this is an ATA device
    uint32_t signature = port->port_base->sig;
    
    if (signature == SATA_SIG_ATAPI) {
        SATA_TRACE("Port %u: ATAPI device detected (not supported)", port->port_num);
        port->type = 1; // ATAPI
        return false; // We don't support ATAPI
    } else if (signature != SATA_SIG_ATA) {
        SATA_TRACE("Port %u: Unknown device signature: 0x%x", port->port_num, signature);
        return false;
    }
    
    port->type = 0; // ATA device
    
    // First, make sure the device is ready
    // Wait for BSY to clear
    uint64_t timeout = timer_get_uptime_ms() + 1000;  // 1 second timeout
    while ((port->port_base->tfd & HBA_PxTFD_BSY) && timer_get_uptime_ms() < timeout) {
        timer_sleep_ms(1);
    }
    
    if (port->port_base->tfd & HBA_PxTFD_BSY) {
        SATA_TRACE("Port %u: Device is busy", port->port_num);
        return false;
    }
    
    // Build IDENTIFY command
    int slot = find_command_slot(port);
    if (slot == -1) {
        SATA_TRACE("No free command slots");
        return false;
    }
    
    // Allocate a buffer for IDENTIFY data
    ata_identify_t* identify_data = alloc_dma_buffer(512, NULL);
    if (!identify_data) {
        SATA_TRACE("Failed to allocate memory for IDENTIFY data");
        return false;
    }
    
    // Clear buffer
    memset(identify_data, 0, 512);
    
    // Initialize command header
    hba_cmd_header_t* cmd_header = &port->cmd_list[slot];
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);  // Command FIS size in DWORDs
    cmd_header->w = 0;  // Read operation
    cmd_header->prdtl = 1;  // One PRD entry
    cmd_header->p = 1;  // Prefetchable
    
    // Set up command table
    hba_cmd_tbl_t* cmd_table = port->cmd_tables[slot];
    memset(cmd_table, 0, sizeof(hba_cmd_tbl_t));
    
    // Set up PRDT entry
    cmd_table->prdt[0].dba = (uint32_t)((uintptr_t)identify_data);
    cmd_table->prdt[0].dbau = (uint32_t)(((uintptr_t)identify_data) >> 32);
    cmd_table->prdt[0].dbc = 512 - 1;  // 512 bytes, 0-based
    cmd_table->prdt[0].i = 1;  // Interrupt on completion
    
    // Set up command FIS (H2D Register FIS)
    fis_reg_h2d_t* cmd_fis = (fis_reg_h2d_t*)cmd_table->cfis;
    memset(cmd_fis, 0, sizeof(fis_reg_h2d_t));
    
    cmd_fis->fis_type = FIS_TYPE_REG_H2D;
    cmd_fis->c = 1;  // Command
    cmd_fis->command = ATA_CMD_IDENTIFY;
    
    // Issue command
    port->port_base->ci |= (1 << slot);
    
    // Wait for completion with timeout
    uint64_t start_time = timer_get_uptime_ms();
    while ((port->port_base->ci & (1 << slot)) && timer_get_uptime_ms() - start_time < 5000) {
        if (port->port_base->is & HBA_PxIS_TFES) {
            SATA_TRACE("Port %u: IDENTIFY command error", port->port_num);
            port->port_base->is = HBA_PxIS_TFES;  // Clear the error
            free_dma_buffer(identify_data, 512);
            return false;
        }
        timer_sleep_ms(1);
    }
    
    if (port->port_base->ci & (1 << slot)) {
        SATA_TRACE("Port %u: IDENTIFY command timeout", port->port_num);
        free_dma_buffer(identify_data, 512);
        return false;
    }
    
    // Extract device information
    
    // Model name (bytes 54-93, 40 bytes)
    // ATA strings are stored in 16-bit words with byte swapping
    char* model_src = (char*)&identify_data->model;
    for (int i = 0; i < 40; i += 2) {
        port->model[i] = model_src[i+1];
        port->model[i+1] = model_src[i];
    }
    port->model[40] = '\0';
    
    // Clean up model string (trim trailing spaces)
    for (int i = 39; i >= 0; i--) {
        if (port->model[i] == ' ')
            port->model[i] = '\0';
        else if (port->model[i] != '\0')
            break;
    }
    
    // Serial number (bytes 20-39, 20 bytes)
    char* serial_src = (char*)&identify_data->serial;
    for (int i = 0; i < 20; i += 2) {
        port->serial[i] = serial_src[i+1];
        port->serial[i+1] = serial_src[i];
    }
    port->serial[20] = '\0';
    
    // Clean up serial string
    for (int i = 19; i >= 0; i--) {
        if (port->serial[i] == ' ')
            port->serial[i] = '\0';
        else if (port->serial[i] != '\0')
            break;
    }
    
    // Firmware revision (bytes 46-53, 8 bytes)
    char* firmware_src = (char*)&identify_data->firmware;
    for (int i = 0; i < 8; i += 2) {
        port->firmware[i] = firmware_src[i+1];
        port->firmware[i+1] = firmware_src[i];
    }
    port->firmware[8] = '\0';
    
    // Clean up firmware string
    for (int i = 7; i >= 0; i--) {
        if (port->firmware[i] == ' ')
            port->firmware[i] = '\0';
        else if (port->firmware[i] != '\0')
            break;
    }
    
    // Get total sectors (capacity)
    // Check if device supports 48-bit LBA
    if ((identify_data->commands_supported[1] & (1 << 10)) != 0) {
        // Use 48-bit LBA
        port->sector_count = identify_data->max_lba;
        SATA_TRACE("Port %u: Using 48-bit LBA, max_lba = %lu", port->port_num, port->sector_count);
    } else {
        // Use 28-bit LBA
        port->sector_count = identify_data->total_sectors;
        SATA_TRACE("Port %u: Using 28-bit LBA, total_sectors = %lu", port->port_num, port->sector_count);
    }
    
    // Check sector size (logical/physical)
    if ((identify_data->sector_size & (1 << 12)) != 0) {
        // Logical sector size is set in word
        if ((identify_data->sector_size & (1 << 13)) != 0) {
            // Logical sector size > 512 bytes
            port->sector_size = identify_data->words_per_sector * 2;
        } else {
            port->sector_size = 512;
        }
    } else {
        port->sector_size = 512; // Default sector size
    }
    
    // Sanity check the sector count
    if (port->sector_count == 0 || port->sector_count > 0xFFFFFFFFFFFFULL) {
        SATA_TRACE("Port %u: Invalid sector count %lu, defaulting to 1GB", 
                  port->port_num, port->sector_count);
        port->sector_count = 2 * 1024 * 1024; // Default to 1GB (2M sectors)
    }
    
    SATA_TRACE("Port %u: ATA device: model=%s, serial=%s, firmware=%s", 
               port->port_num, port->model, port->serial, port->firmware);
    SATA_TRACE("Port %u: %lu sectors, %u bytes per sector (%lu MB)", 
               port->port_num, port->sector_count, port->sector_size,
               port->sector_count * port->sector_size / (1024 * 1024));
    
    free_dma_buffer(identify_data, 512);
    return true;
}

// Allocate memory for port structures
bool allocate_port_memory(sata_port_t* port) {
    // Allocate command list - 1KB aligned
    uintptr_t cmd_list_phys;
    port->cmd_list = alloc_dma_buffer(COMMAND_LIST_SIZE, &cmd_list_phys);
    if (!port->cmd_list) {
        SATA_TRACE("Failed to allocate command list");
        return false;
    }
    
    // Zero the command list
    memset(port->cmd_list, 0, COMMAND_LIST_SIZE);
    
    // Allocate FIS buffer - 256B aligned
    uintptr_t fis_phys;
    port->fis = alloc_dma_buffer(RECEIVED_FIS_SIZE, &fis_phys);
    if (!port->fis) {
        SATA_TRACE("Failed to allocate received FIS buffer");
        free_dma_buffer(port->cmd_list, COMMAND_LIST_SIZE);
        return false;
    }
    
    // Zero the FIS buffer
    memset(port->fis, 0, RECEIVED_FIS_SIZE);
    
    // Allocate command tables - 128B aligned
    for (uint32_t i = 0; i < 32; i++) {
        port->cmd_tables[i] = NULL;
    }
    
    // Allocate command tables for each slot
    for (uint32_t i = 0; i < 32; i++) {
        uintptr_t cmd_table_phys;
        port->cmd_tables[i] = alloc_dma_buffer(COMMAND_TABLE_SIZE, &cmd_table_phys);
        if (!port->cmd_tables[i]) {
            SATA_TRACE("Failed to allocate command table for slot %u", i);
            
            // Free previously allocated command tables
            for (uint32_t j = 0; j < i; j++) {
                free_dma_buffer(port->cmd_tables[j], COMMAND_TABLE_SIZE);
                port->cmd_tables[j] = NULL;
            }
            
            free_dma_buffer(port->fis, RECEIVED_FIS_SIZE);
            free_dma_buffer(port->cmd_list, COMMAND_LIST_SIZE);
            return false;
        }
        
        // Zero the command table
        memset(port->cmd_tables[i], 0, COMMAND_TABLE_SIZE);
        
        // Set command table address in command header
        port->cmd_list[i].ctba = (uint32_t)cmd_table_phys;
        port->cmd_list[i].ctbau = (uint32_t)(cmd_table_phys >> 32);
    }
    
    // Allocate DMA buffer for data transfers
    port->dma_buffer = alloc_dma_buffer(DMA_BUFFER_SIZE, &port->dma_buffer_phys);
    if (!port->dma_buffer) {
        SATA_TRACE("Failed to allocate DMA buffer");
        
        // Free command tables
        for (uint32_t i = 0; i < 32; i++) {
            if (port->cmd_tables[i]) {
                free_dma_buffer(port->cmd_tables[i], COMMAND_TABLE_SIZE);
                port->cmd_tables[i] = NULL;
            }
        }
        
        free_dma_buffer(port->fis, RECEIVED_FIS_SIZE);
        free_dma_buffer(port->cmd_list, COMMAND_LIST_SIZE);
        return false;
    }
    
    // Zero the DMA buffer
    memset(port->dma_buffer, 0, DMA_BUFFER_SIZE);
    port->dma_buffer_size = DMA_BUFFER_SIZE;
    
    // Set command list and FIS base addresses in port registers
    port->port_base->clb = (uint32_t)cmd_list_phys;
    port->port_base->clbu = (uint32_t)(cmd_list_phys >> 32);
    port->port_base->fb = (uint32_t)fis_phys;
    port->port_base->fbu = (uint32_t)(fis_phys >> 32);
    
    // Clear pending interrupts
    port->port_base->is = (uint32_t)-1;
    
    // Clear error register
    port->port_base->serr = (uint32_t)-1;
    
    return true;
}

// Free port memory
void free_port_memory(sata_port_t* port) {
    // Free DMA buffer
    if (port->dma_buffer) {
        free_dma_buffer(port->dma_buffer, port->dma_buffer_size);
        port->dma_buffer = NULL;
    }
    
    // Free command tables
    for (uint32_t i = 0; i < 32; i++) {
        if (port->cmd_tables[i]) {
            free_dma_buffer(port->cmd_tables[i], COMMAND_TABLE_SIZE);
            port->cmd_tables[i] = NULL;
        }
    }
    
    // Free FIS buffer
    if (port->fis) {
        free_dma_buffer(port->fis, RECEIVED_FIS_SIZE);
        port->fis = NULL;
    }
    
    // Free command list
    if (port->cmd_list) {
        free_dma_buffer(port->cmd_list, COMMAND_LIST_SIZE);
        port->cmd_list = NULL;
    }
}

// Find a free command slot
int find_command_slot(sata_port_t* port) {
    // Check if NCQ is supported for optimized slot allocation
    // For now, just use a simple approach
    
    // Get the current command issue and active registers
    uint32_t cmd_issue = port->port_base->ci;
    uint32_t cmd_active = port->port_base->sact;
    
    // Check for a slot that is not in use
    for (uint32_t i = 0; i < 32; i++) {
        if (((cmd_issue >> i) & 1) == 0 && ((cmd_active >> i) & 1) == 0) {
            return i;
        }
    }
    
    // No free slots found
    return -1;
}

// Allocate DMA-capable memory
void* alloc_dma_buffer(size_t size, uintptr_t* phys_addr) {
    // Round up to page size
    size = (size + 4095) & ~4095;
    
    // Allocate physical pages
    uintptr_t phys = (uintptr_t)pmm_alloc_pages(size / 4096);
    if (phys == 0) {
        return NULL;
    }
    
    // Map to virtual memory with appropriate flags
    void* virt = vmm_map_physical(phys, size, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
    if (!virt) {
        pmm_free_pages(phys, size / 4096);
        return NULL;
    }
    
    // Return physical address if requested
    if (phys_addr) {
        *phys_addr = phys;
    }
    
    return virt;
}

// Free DMA-capable memory
void free_dma_buffer(void* virt_addr, size_t size) {
    if (!virt_addr) {
        return;
    }
    
    // Round up to page size
    size = (size + 4095) & ~4095;
    
    // Get physical address
    uintptr_t phys = vmm_get_physical_address((uintptr_t)virt_addr);
    
    // Unmap virtual memory
    vmm_unmap_physical(virt_addr, size);
    
    // Free physical memory
    if (phys) {
        pmm_free_pages(phys, size / 4096);
    }
}

// Wait for command completion
bool wait_for_command_completion(sata_port_t* port, uint32_t slot, uint32_t timeout_ms) {
    uint64_t start_time = timer_get_uptime_ms();
    uint64_t deadline = start_time + timeout_ms;
    
    // Wait for command to complete
    while (timer_get_uptime_ms() < deadline) {
        // Check if command has completed
        if ((port->port_base->ci & (1 << slot)) == 0) {
            // Check for errors
            if (port->port_base->is & HBA_PxIS_TFES) {
                SATA_TRACE("Command error, TFD=0x%x, IS=0x%x", 
                          port->port_base->tfd, port->port_base->is);
                
                // Clear error bits
                port->port_base->is = HBA_PxIS_TFES;
                return false;
            }
            
            return true;
        }
        
        // Brief pause to avoid hammering the registers
        timer_sleep_ms(1);
    }
    
    SATA_TRACE("Command timeout after %u ms", timeout_ms);
    return false;
}

// Add these missing functions to your sata.c file:

// Read data from SATA device
bool sata_read(uint32_t port_id, uint64_t lba, void* buffer, uint32_t sector_count) {
    if (!sata_driver.initialized) {
        SATA_TRACE("SATA driver not initialized");
        return false;
    }
    
    if (port_id >= sata_driver.total_ports) {
        SATA_TRACE("Invalid port ID %u", port_id);
        return false;
    }
    
    // Find the port
    sata_port_t* port = NULL;
    uint32_t ports_found = 0;
    
    // Iterate controllers and ports to find the port with the given ID
    for (uint32_t ctrl_idx = 0; ctrl_idx < sata_driver.controller_count; ctrl_idx++) {
        sata_controller_t* ctrl = &sata_driver.controllers[ctrl_idx];
        
        if (!ctrl->initialized) {
            continue;
        }
        
        for (uint32_t port_idx = 0; port_idx < 32; port_idx++) {
            if (ctrl->ports_active & (1 << port_idx)) {
                if (ports_found == port_id) {
                    port = &ctrl->ports[port_idx];
                    break;
                }
                ports_found++;
            }
        }
        
        if (port) {
            break;
        }
    }
    
    if (!port) {
        SATA_TRACE("Port ID %u not found", port_id);
        return false;
    }
    
    // Check if port is active
    if (port->status != SATA_PORT_STATUS_ACTIVE) {
        SATA_TRACE("Port %u not active", port->port_num);
        return false;
    }
    
    // Check LBA range
    if (lba + sector_count > port->sector_count) {
        SATA_TRACE("Invalid LBA range: %lu-%lu (max: %lu)", 
                  lba, lba + sector_count - 1, port->sector_count - 1);
        return false;
    }
    
    // Lock the port
    spinlock_acquire(&port->lock);
    
    // Find a free command slot
    int slot = find_command_slot(port);
    if (slot == -1) {
        SATA_TRACE("No free command slots");
        spinlock_release(&port->lock);
        return false;
    }
    
    // Check if data fits in one request
    bool success = true;
    uint32_t max_sectors_per_cmd = port->dma_buffer_size / port->sector_size;
    uint32_t sectors_remaining = sector_count;
    uint64_t current_lba = lba;
    uint8_t* buf_ptr = (uint8_t*)buffer;
    
    // Process in chunks if necessary
    while (sectors_remaining > 0 && success) {
        uint32_t sectors_this_cmd = (sectors_remaining > max_sectors_per_cmd) ? 
                                    max_sectors_per_cmd : sectors_remaining;
        
        // Send READ DMA EXT command
        success = port_send_command(port, slot, ATA_CMD_READ_DMA_EXT, 
                                   current_lba, sectors_this_cmd, buf_ptr, false);
        
        if (success) {
            sectors_remaining -= sectors_this_cmd;
            current_lba += sectors_this_cmd;
            buf_ptr += sectors_this_cmd * port->sector_size;
        }
    }
    
    // Release port lock
    spinlock_release(&port->lock);
    
    return success;
}

// Write data to SATA device
bool sata_write(uint32_t port_id, uint64_t lba, const void* buffer, uint32_t sector_count) {
    if (!sata_driver.initialized) {
        SATA_TRACE("SATA driver not initialized");
        return false;
    }
    
    if (port_id >= sata_driver.total_ports) {
        SATA_TRACE("Invalid port ID %u", port_id);
        return false;
    }
    
    // Find the port
    sata_port_t* port = NULL;
    uint32_t ports_found = 0;
    
    // Iterate controllers and ports to find the port with the given ID
    for (uint32_t ctrl_idx = 0; ctrl_idx < sata_driver.controller_count; ctrl_idx++) {
        sata_controller_t* ctrl = &sata_driver.controllers[ctrl_idx];
        
        if (!ctrl->initialized) {
            continue;
        }
        
        for (uint32_t port_idx = 0; port_idx < 32; port_idx++) {
            if (ctrl->ports_active & (1 << port_idx)) {
                if (ports_found == port_id) {
                    port = &ctrl->ports[port_idx];
                    break;
                }
                ports_found++;
            }
        }
        
        if (port) {
            break;
        }
    }
    
    if (!port) {
        SATA_TRACE("Port ID %u not found", port_id);
        return false;
    }
    
    // Check if port is active
    if (port->status != SATA_PORT_STATUS_ACTIVE) {
        SATA_TRACE("Port %u not active", port->port_num);
        return false;
    }
    
    // Check LBA range
    if (lba + sector_count > port->sector_count) {
        SATA_TRACE("Invalid LBA range: %lu-%lu (max: %lu)", 
                  lba, lba + sector_count - 1, port->sector_count - 1);
        return false;
    }
    
    // Lock the port
    spinlock_acquire(&port->lock);
    
    // Find a free command slot
    int slot = find_command_slot(port);
    if (slot == -1) {
        SATA_TRACE("No free command slots");
        spinlock_release(&port->lock);
        return false;
    }
    
    // Check if data fits in one request
    bool success = true;
    uint32_t max_sectors_per_cmd = port->dma_buffer_size / port->sector_size;
    uint32_t sectors_remaining = sector_count;
    uint64_t current_lba = lba;
    const uint8_t* buf_ptr = (const uint8_t*)buffer;
    
    // Process in chunks if necessary
    while (sectors_remaining > 0 && success) {
        uint32_t sectors_this_cmd = (sectors_remaining > max_sectors_per_cmd) ? 
                                    max_sectors_per_cmd : sectors_remaining;
        
        // Send WRITE DMA EXT command
        success = port_send_command(port, slot, ATA_CMD_WRITE_DMA_EXT, 
                                   current_lba, sectors_this_cmd, (void*)buf_ptr, true);
        
        if (success) {
            sectors_remaining -= sectors_this_cmd;
            current_lba += sectors_this_cmd;
            buf_ptr += sectors_this_cmd * port->sector_size;
        }
    }
    
    // Release port lock
    spinlock_release(&port->lock);
    
    return success;
}

// Flush SATA device cache
bool sata_flush(uint32_t port_id) {
    if (!sata_driver.initialized) {
        SATA_TRACE("SATA driver not initialized");
        return false;
    }
    
    if (port_id >= sata_driver.total_ports) {
        SATA_TRACE("Invalid port ID %u", port_id);
        return false;
    }
    
    // Find the port
    sata_port_t* port = NULL;
    uint32_t ports_found = 0;
    
    // Iterate controllers and ports to find the port with the given ID
    for (uint32_t ctrl_idx = 0; ctrl_idx < sata_driver.controller_count; ctrl_idx++) {
        sata_controller_t* ctrl = &sata_driver.controllers[ctrl_idx];
        
        if (!ctrl->initialized) {
            continue;
        }
        
        for (uint32_t port_idx = 0; port_idx < 32; port_idx++) {
            if (ctrl->ports_active & (1 << port_idx)) {
                if (ports_found == port_id) {
                    port = &ctrl->ports[port_idx];
                    break;
                }
                ports_found++;
            }
        }
        
        if (port) {
            break;
        }
    }
    
    if (!port) {
        SATA_TRACE("Port ID %u not found", port_id);
        return false;
    }
    
    // Check if port is active
    if (port->status != SATA_PORT_STATUS_ACTIVE) {
        SATA_TRACE("Port %u not active", port->port_num);
        return false;
    }
    
    // Lock the port
    spinlock_acquire(&port->lock);
    
    // Find a free command slot
    int slot = find_command_slot(port);
    if (slot == -1) {
        SATA_TRACE("No free command slots");
        spinlock_release(&port->lock);
        return false;
    }
    
    // Send FLUSH CACHE EXT command
    bool success = port_send_command(port, slot, ATA_CMD_FLUSH_CACHE_EXT, 0, 0, NULL, false);
    
    // Release port lock
    spinlock_release(&port->lock);
    
    return success;
}

// Get port information
bool sata_get_port_info(uint32_t port_id, char* buffer, size_t buffer_size) {
    if (!sata_driver.initialized || !buffer || buffer_size == 0) {
        return false;
    }
    
    if (port_id >= sata_driver.total_ports) {
        return false;
    }
    
    // Find the port
    sata_port_t* port = NULL;
    uint32_t ports_found = 0;
    
    // Iterate controllers and ports to find the port with the given ID
    for (uint32_t ctrl_idx = 0; ctrl_idx < sata_driver.controller_count; ctrl_idx++) {
        sata_controller_t* ctrl = &sata_driver.controllers[ctrl_idx];
        
        if (!ctrl->initialized) {
            continue;
        }
        
        for (uint32_t port_idx = 0; port_idx < 32; port_idx++) {
            if (ctrl->ports_active & (1 << port_idx)) {
                if (ports_found == port_id) {
                    port = &ctrl->ports[port_idx];
                    break;
                }
                ports_found++;
            }
        }
        
        if (port) {
            break;
        }
    }
    
    if (!port) {
        return false;
    }
    
    // Format port information
    int written = snprintf(buffer, buffer_size,
                          "SATA Port %u:\n"
                          "  Model: %s\n"
                          "  Serial: %s\n"
                          "  Firmware: %s\n"
                          "  Size: %lu MB (%lu sectors, %u bytes/sector)\n",
                          port_id,
                          port->model,
                          port->serial,
                          port->firmware,
                          port->sector_count * port->sector_size / (1024 * 1024),
                          port->sector_count,
                          port->sector_size);
    
    return written > 0;
}

// Get number of SATA ports
uint32_t sata_get_port_count(void) {
    return sata_driver.total_ports;
}

// Diagnostics function - Dump information about all SATA devices
void sata_debug_dump_info(void) {
    if (!sata_driver.initialized) {
        printf("SATA: Driver not initialized\n");
        return;
    }
    
    printf("SATA Driver Status:\n");
    printf("  Controllers: %u\n", sata_driver.controller_count);
    printf("  Total Ports: %u\n", sata_driver.total_ports);
    
    uint32_t port_id = 0;
    
    // Iterate controllers
    for (uint32_t ctrl_idx = 0; ctrl_idx < sata_driver.controller_count; ctrl_idx++) {
        sata_controller_t* ctrl = &sata_driver.controllers[ctrl_idx];
        
        if (!ctrl->initialized) {
            continue;
        }
        
        printf("\nController %u: PCI %02x:%02x.%x (VendorID: 0x%04X, DeviceID: 0x%04X)\n",
               ctrl_idx, ctrl->pci_bus, ctrl->pci_device, ctrl->pci_function,
               ctrl->vendor_id, ctrl->device_id);
        printf("  Ports Implemented: 0x%08X\n", ctrl->ports_impl);
        printf("  Ports Active: 0x%08X\n", ctrl->ports_active);
        printf("  Command Slots: %u\n", ctrl->slot_count);
        
        // Iterate ports
        for (uint32_t port_idx = 0; port_idx < 32; port_idx++) {
            if (ctrl->ports_active & (1 << port_idx)) {
                sata_port_t* port = &ctrl->ports[port_idx];
                
                printf("\n  Port %u (Global ID: %u):\n", port_idx, port_id);
                printf("    Status: %s\n", port->status == SATA_PORT_STATUS_ACTIVE ? "Active" : "Present");
                printf("    Type: %s\n", port->type == 0 ? "ATA" : "ATAPI");
                printf("    Model: %s\n", port->model);
                printf("    Serial: %s\n", port->serial);
                printf("    Firmware: %s\n", port->firmware);
                printf("    Capacity: %lu MB (%lu sectors, %u bytes/sector)\n",
                       port->sector_count * port->sector_size / (1024 * 1024),
                       port->sector_count, port->sector_size);
                
                port_id++;
            }
        }
    }
}

// Send a command to port
bool port_send_command(sata_port_t* port, uint32_t slot, uint8_t cmd, uint64_t lba, 
                     uint32_t count, void* buffer, bool write) {
    // Reset command table
    memset(port->cmd_tables[slot], 0, COMMAND_TABLE_SIZE);
    
    // Setup command table
    hba_cmd_tbl_t* cmd_table = port->cmd_tables[slot];
    
    // Set up command FIS
    fis_reg_h2d_t* cmd_fis = (fis_reg_h2d_t*)(&cmd_table->cfis);
    memset(cmd_fis, 0, sizeof(fis_reg_h2d_t));
    
    cmd_fis->fis_type = FIS_TYPE_REG_H2D;  // Host to device
    cmd_fis->c = 1;                        // Command FIS
    cmd_fis->command = cmd;                // Command
    
    // Set up LBA (handle both 28-bit and 48-bit addressing)
    if (cmd == ATA_CMD_READ_DMA_EXT || cmd == ATA_CMD_WRITE_DMA_EXT || cmd == ATA_CMD_FLUSH_CACHE_EXT) {
        // 48-bit addressing
        cmd_fis->device = 1 << 6;  // LBA mode
        
        cmd_fis->lba0 = (uint8_t)lba;
        cmd_fis->lba1 = (uint8_t)(lba >> 8);
        cmd_fis->lba2 = (uint8_t)(lba >> 16);
        cmd_fis->lba3 = (uint8_t)(lba >> 24);
        cmd_fis->lba4 = (uint8_t)(lba >> 32);
        cmd_fis->lba5 = (uint8_t)(lba >> 40);
        
        cmd_fis->countl = count & 0xFF;
        cmd_fis->counth = (count >> 8) & 0xFF;
    } else if (cmd == ATA_CMD_IDENTIFY) {
        // IDENTIFY device command
        cmd_fis->device = 0;  // Not using LBA for IDENTIFY
        
        // No LBA or count needed for IDENTIFY
    } else {
        // 28-bit addressing (legacy)
        cmd_fis->device = 0xE0 | ((lba >> 24) & 0x0F);  // LBA mode, high 4 bits
        
        cmd_fis->lba0 = (uint8_t)lba;
        cmd_fis->lba1 = (uint8_t)(lba >> 8);
        cmd_fis->lba2 = (uint8_t)(lba >> 16);
        
        cmd_fis->countl = count & 0xFF;
    }
    
    // Set up a PRDT (Physical Region Descriptor Table) entry if buffer provided
    if (buffer && count > 0 && cmd != ATA_CMD_FLUSH_CACHE_EXT) {
        uint32_t data_size = count * port->sector_size;
        
        // Check if DMA buffer is big enough
        if (data_size > port->dma_buffer_size) {
            SATA_TRACE("Data too large for DMA buffer (%u > %zu)", 
                      data_size, port->dma_buffer_size);
            return false;
        }
        
        // Copy data to DMA buffer for write operations
        if (write && buffer) {
            memcpy(port->dma_buffer, buffer, data_size);
        }
        
        // Set up PRD entry
        cmd_table->prdt[0].dba = (uint32_t)port->dma_buffer_phys;
        cmd_table->prdt[0].dbau = (uint32_t)(port->dma_buffer_phys >> 32);
        cmd_table->prdt[0].dbc = data_size - 1;  // DBC is 0-based
        cmd_table->prdt[0].i = 1;  // Interrupt on completion
        
        // Set length and attributes
        hba_cmd_header_t* cmd_header = &port->cmd_list[slot];
        cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);  // Command FIS size in DWORDs
        cmd_header->w = write ? 1 : 0;  // Write = 1, Read = 0
        cmd_header->prdtl = 1;  // One PRD entry
        
        // Special handling for IDENTIFY
        if (cmd == ATA_CMD_IDENTIFY) {
            cmd_header->w = 0;  // IDENTIFY is a read operation
        }
    } else {
        // No data transfer or flush command
        hba_cmd_header_t* cmd_header = &port->cmd_list[slot];
        cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
        cmd_header->w = 0;  // No write
        cmd_header->prdtl = 0;  // No PRD entries
    }
    
    // Issue command
    port->port_base->ci |= (1 << slot);
    
    // Wait for completion
    if (!wait_for_command_completion(port, slot, SATA_COMMAND_TIMEOUT_MS)) {
        SATA_TRACE("Command failed or timed out");
        return false;
    }
    
    // For read operations, copy data from DMA buffer to provided buffer
    if (!write && buffer && count > 0 && cmd != ATA_CMD_FLUSH_CACHE_EXT) {
        uint32_t data_size = count * port->sector_size;
        memcpy(buffer, port->dma_buffer, data_size);
    }
    
    return true;
}