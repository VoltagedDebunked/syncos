#ifndef _SYNCOS_SATA_H
#define _SYNCOS_SATA_H

#include <stdint.h>
#include <stdbool.h>
#include <core/drivers/pci.h>
#include <stddef.h>
#include <syncos/spinlock.h>

// Maximum number of SATA controllers and ports supported
#define SATA_MAX_CONTROLLERS 4
#define SATA_MAX_PORTS 32     // Maximum across all controllers

// SATA signature values
#define SATA_SIG_ATA    0x00000101  // SATA drive
#define SATA_SIG_ATAPI  0xEB140101  // SATAPI drive
#define SATA_SIG_SEMB   0xC33C0101  // Enclosure management bridge
#define SATA_SIG_PM     0x96690101  // Port multiplier

// AHCI port status & control register bits
#define AHCI_PxCMD_ST   (1 << 0)    // Start
#define AHCI_PxCMD_SUD  (1 << 1)    // Spin-Up Device
#define AHCI_PxCMD_POD  (1 << 2)    // Power On Device
#define AHCI_PxCMD_CLO  (1 << 3)    // Command List Override
#define AHCI_PxCMD_FRE  (1 << 4)    // FIS Receive Enable
#define AHCI_PxCMD_FR   (1 << 14)   // FIS Receive Running
#define AHCI_PxCMD_CR   (1 << 15)   // Command List Running

// AHCI global host control register bits
#define AHCI_GHC_HR     (1 << 0)    // HBA Reset
#define AHCI_GHC_IE     (1 << 1)    // Interrupt Enable
#define AHCI_GHC_MRSM   (1 << 2)    // MSI Revert to Single Message
#define AHCI_GHC_AE     (1 << 31)   // AHCI Enable

// ATA command set 
#define ATA_CMD_READ_DMA_EXT      0x25
#define ATA_CMD_WRITE_DMA_EXT     0x35
#define ATA_CMD_IDENTIFY          0xEC
#define ATA_CMD_IDENTIFY_PACKET   0xA1
#define ATA_CMD_PACKET            0xA0
#define ATA_CMD_FLUSH_CACHE_EXT   0xEA

// FIS types
#define FIS_TYPE_REG_H2D    0x27  // Register FIS - host to device
#define FIS_TYPE_REG_D2H    0x34  // Register FIS - device to host
#define FIS_TYPE_DMA_ACT    0x39  // DMA activate FIS
#define FIS_TYPE_DMA_SETUP  0x41  // DMA setup FIS
#define FIS_TYPE_DATA       0x46  // Data FIS
#define FIS_TYPE_BIST       0x58  // BIST activate FIS
#define FIS_TYPE_PIO_SETUP  0x5F  // PIO setup FIS
#define FIS_TYPE_DEV_BITS   0xA1  // Set device bits FIS

// AHCI port register offsets
#define AHCI_PxCLB       0x00  // Command List Base Address
#define AHCI_PxCLBU      0x04  // Command List Base Address Upper 32-Bits
#define AHCI_PxFB        0x08  // FIS Base Address
#define AHCI_PxFBU       0x0C  // FIS Base Address Upper 32-Bits
#define AHCI_PxIS        0x10  // Interrupt Status
#define AHCI_PxIE        0x14  // Interrupt Enable
#define AHCI_PxCMD       0x18  // Command and Status
#define AHCI_PxTFD       0x20  // Task File Data
#define AHCI_PxSIG       0x24  // Signature
#define AHCI_PxSSTS      0x28  // SATA Status (SCR0: SStatus)
#define AHCI_PxSCTL      0x2C  // SATA Control (SCR2: SControl)
#define AHCI_PxSERR      0x30  // SATA Error (SCR1: SError)
#define AHCI_PxSACT      0x34  // SATA Active (SCR3: SActive)
#define AHCI_PxCI        0x38  // Command Issue

// Port status values
#define SATA_PORT_STATUS_NO_DEVICE 0
#define SATA_PORT_STATUS_PRESENT   1
#define SATA_PORT_STATUS_ACTIVE    2

/* 
 * FIS - Frame Information Structure definitions 
 */

// Host to Device Register FIS structure
typedef struct {
    // DWORD 0
    uint8_t  fis_type;          // FIS_TYPE_REG_H2D
    uint8_t  pmport:4;          // Port multiplier
    uint8_t  reserved0:3;       // Reserved
    uint8_t  c:1;               // 1: Command, 0: Control
    uint8_t  command;           // Command register
    uint8_t  featurel;          // Feature register, 7:0
    
    // DWORD 1
    uint8_t  lba0;              // LBA low register, 7:0
    uint8_t  lba1;              // LBA mid register, 15:8
    uint8_t  lba2;              // LBA high register, 23:16
    uint8_t  device;            // Device register
    
    // DWORD 2
    uint8_t  lba3;              // LBA register, 31:24
    uint8_t  lba4;              // LBA register, 39:32
    uint8_t  lba5;              // LBA register, 47:40
    uint8_t  featureh;          // Feature register, 15:8
    
    // DWORD 3
    uint8_t  countl;            // Count register, 7:0
    uint8_t  counth;            // Count register, 15:8
    uint8_t  icc;               // Isochronous command completion
    uint8_t  control;           // Control register
    
    // DWORD 4
    uint8_t  reserved1[4];      // Reserved
} __attribute__((packed)) fis_reg_h2d_t;

// Device to Host Register FIS structure
typedef struct {
    // DWORD 0
    uint8_t  fis_type;          // FIS_TYPE_REG_D2H
    uint8_t  pmport:4;          // Port multiplier
    uint8_t  reserved0:2;       // Reserved
    uint8_t  i:1;               // Interrupt bit
    uint8_t  reserved1:1;       // Reserved
    uint8_t  status;            // Status register
    uint8_t  error;             // Error register
    
    // DWORD 1
    uint8_t  lba0;              // LBA low register, 7:0
    uint8_t  lba1;              // LBA mid register, 15:8
    uint8_t  lba2;              // LBA high register, 23:16
    uint8_t  device;            // Device register
    
    // DWORD 2
    uint8_t  lba3;              // LBA register, 31:24
    uint8_t  lba4;              // LBA register, 39:32
    uint8_t  lba5;              // LBA register, 47:40
    uint8_t  reserved2;         // Reserved
    
    // DWORD 3
    uint8_t  countl;            // Count register, 7:0
    uint8_t  counth;            // Count register, 15:8
    uint8_t  reserved3[2];      // Reserved
    
    // DWORD 4
    uint8_t  reserved4[4];      // Reserved
} __attribute__((packed)) fis_reg_d2h_t;

// Data FIS structure
typedef struct {
    // DWORD 0
    uint8_t  fis_type;          // FIS_TYPE_DATA
    uint8_t  pmport:4;          // Port multiplier
    uint8_t  reserved0:4;       // Reserved
    uint8_t  reserved1[2];      // Reserved
    
    // DWORD 1-N
    uint32_t data[1];           // Payload (variable size)
} __attribute__((packed)) fis_data_t;

// PIO Setup FIS structure
typedef struct {
    // DWORD 0
    uint8_t  fis_type;          // FIS_TYPE_PIO_SETUP
    uint8_t  pmport:4;          // Port multiplier
    uint8_t  reserved0:1;       // Reserved
    uint8_t  d:1;               // Data transfer direction, 1: device to host
    uint8_t  i:1;               // Interrupt bit
    uint8_t  reserved1:1;
    uint8_t  status;            // Status register
    uint8_t  error;             // Error register
    
    // DWORD 1
    uint8_t  lba0;              // LBA low register, 7:0
    uint8_t  lba1;              // LBA mid register, 15:8
    uint8_t  lba2;              // LBA high register, 23:16
    uint8_t  device;            // Device register
    
    // DWORD 2
    uint8_t  lba3;              // LBA register, 31:24
    uint8_t  lba4;              // LBA register, 39:32
    uint8_t  lba5;              // LBA register, 47:40
    uint8_t  reserved2;         // Reserved
    
    // DWORD 3
    uint8_t  countl;            // Count register, 7:0
    uint8_t  counth;            // Count register, 15:8
    uint8_t  reserved3;         // Reserved
    uint8_t  e_status;          // New value of status register
    
    // DWORD 4
    uint16_t tc;                // Transfer count
    uint8_t  reserved4[2];      // Reserved
} __attribute__((packed)) fis_pio_setup_t;

// DMA Setup FIS structure
typedef struct {
    // DWORD 0
    uint8_t  fis_type;          // FIS_TYPE_DMA_SETUP
    uint8_t  pmport:4;          // Port multiplier
    uint8_t  reserved0:1;       // Reserved
    uint8_t  d:1;               // Data transfer direction, 1: device to host
    uint8_t  i:1;               // Interrupt bit
    uint8_t  a:1;               // Auto-activate (DMA buffer ready)
    uint8_t  reserved1[2];      // Reserved
    
    // DWORD 1 & 2
    uint64_t dma_buffer_id;     // DMA Buffer Identifier
    
    // DWORD 3
    uint32_t reserved2;         // Reserved
    
    // DWORD 4
    uint32_t dma_buffer_offset; // DMA buffer offset
    
    // DWORD 5
    uint32_t transfer_count;    // Transfer count
    
    // DWORD 6
    uint32_t reserved3;         // Reserved
} __attribute__((packed)) fis_dma_setup_t;

/*
 * AHCI HBA memory structures
 */

// Command Header structure
typedef struct {
    // DWORD 0
    uint8_t  cfl:5;             // Command FIS length in DWORDS, 2 ~ 16
    uint8_t  a:1;               // ATAPI
    uint8_t  w:1;               // Write, 1: H2D, 0: D2H
    uint8_t  p:1;               // Prefetchable
    uint8_t  r:1;               // Reset
    uint8_t  b:1;               // BIST
    uint8_t  c:1;               // Clear busy upon R_OK
    uint8_t  reserved0:1;       // Reserved
    uint8_t  pmp:4;             // Port multiplier port
    uint16_t prdtl;             // Physical region descriptor table length in entries
    
    // DWORD 1
    volatile uint32_t prdbc;    // Physical region descriptor byte count transferred
    
    // DWORD 2, 3
    uint32_t ctba;              // Command table descriptor base address
    uint32_t ctbau;             // Command table descriptor base address upper 32 bits
    
    // DWORD 4-7
    uint16_t command_id;        // Command ID
    uint32_t reserved1[4];      // Reserved
} __attribute__((packed)) hba_cmd_header_t;

// Physical region descriptor table entry
typedef struct {
    uint32_t dba;               // Data base address
    uint32_t dbau;              // Data base address upper 32 bits
    uint32_t reserved0;         // Reserved
    
    // DWORD 3
    uint32_t dbc:22;            // Byte count, 4M max
    uint32_t reserved1:9;       // Reserved
    uint32_t i:1;               // Interrupt on completion
} __attribute__((packed)) hba_prdt_entry_t;

// Command table, one per command slot
typedef struct {
    uint8_t  cfis[64];          // Command FIS
    uint8_t  acmd[16];          // ATAPI command, 12 or 16 bytes
    uint8_t  reserved[48];      // Reserved
    hba_prdt_entry_t prdt[1];   // Physical region descriptor table entries, 0 ~ 65535
} __attribute__((packed)) hba_cmd_tbl_t;

// Received FIS structure
typedef volatile struct {
    // 0x00
    fis_dma_setup_t dsfis;      // DMA Setup FIS
    uint8_t         pad0[4];
    
    // 0x20
    fis_pio_setup_t psfis;      // PIO Setup FIS
    uint8_t         pad1[12];
    
    // 0x40
    fis_reg_d2h_t   rfis;       // Register – Device to Host FIS
    uint8_t         pad2[4];
    
    // 0x58
    uint8_t         sdbfis[8];  // Set Device Bits FIS
    
    // 0x60
    uint8_t         ufis[64];   // Unknown FIS
    
    // 0xA0
    uint8_t         reserved[0x100-0xA0]; // Reserved
} __attribute__((packed)) hba_fis_t;

// HBA Port structure
typedef volatile struct {
    uint32_t clb;               // 0x00, command list base address, 1K-byte aligned
    uint32_t clbu;              // 0x04, command list base address upper 32 bits
    uint32_t fb;                // 0x08, FIS base address, 256-byte aligned
    uint32_t fbu;               // 0x0C, FIS base address upper 32 bits
    uint32_t is;                // 0x10, interrupt status
    uint32_t ie;                // 0x14, interrupt enable
    uint32_t cmd;               // 0x18, command and status
    uint32_t reserved0;         // 0x1C, Reserved
    uint32_t tfd;               // 0x20, task file data
    uint32_t sig;               // 0x24, signature
    uint32_t ssts;              // 0x28, SATA status (SCR0:SStatus)
    uint32_t sctl;              // 0x2C, SATA control (SCR2:SControl)
    uint32_t serr;              // 0x30, SATA error (SCR1:SError)
    uint32_t sact;              // 0x34, SATA active (SCR3:SActive)
    uint32_t ci;                // 0x38, command issue
    uint32_t sntf;              // 0x3C, SATA notification (SCR4:SNotification)
    uint32_t fbs;               // 0x40, FIS-based switch control
    uint32_t reserved1[11];     // 0x44 ~ 0x6F, Reserved
    uint32_t vendor[4];         // 0x70 ~ 0x7F, vendor specific
} __attribute__((packed)) hba_port_t;

// AHCI HBA Memory Registers
typedef volatile struct {
    // 0x00 - 0x2B, Generic Host Control
    uint32_t cap;               // 0x00, Host capability
    uint32_t ghc;               // 0x04, Global host control
    uint32_t is;                // 0x08, Interrupt status
    uint32_t pi;                // 0x0C, Port implemented
    uint32_t vs;                // 0x10, Version
    uint32_t ccc_ctl;           // 0x14, Command completion coalescing control
    uint32_t ccc_pts;           // 0x18, Command completion coalescing ports
    uint32_t em_loc;            // 0x1C, Enclosure management location
    uint32_t em_ctl;            // 0x20, Enclosure management control
    uint32_t cap2;              // 0x24, Host capabilities extended
    uint32_t bohc;              // 0x28, BIOS/OS handoff control and status
    
    // 0x2C - 0x9F, Reserved
    uint8_t  reserved[0xA0-0x2C];
    
    // 0xA0 - 0xFF, Vendor specific registers
    uint8_t  vendor[0x100-0xA0];
    
    // 0x100 - 0x10FF, Port control registers
    hba_port_t ports[32];       // 1 ~ 32 port control registers
} __attribute__((packed)) hba_mem_t;

// ATA identify device data (responses to ATA_CMD_IDENTIFY)
typedef struct {
    // Word 0
    uint16_t config;            // General configuration
    
    // Word 1-9
    uint16_t obsolete1[9];      // Obsolete
    
    // Word 10-19
    uint8_t  serial[20];        // Serial number
    
    // Word 20-22
    uint16_t obsolete2[3];      // Obsolete
    
    // Word 23-26
    uint8_t  firmware[8];       // Firmware revision
    
    // Word 27-46
    uint8_t  model[40];         // Model number
    
    // Word 47
    uint16_t sectors_per_int;   // Max sectors per interrupt
    
    // Word 48
    uint16_t trusted;           // Trusted computing features
    
    // Word 49-59
    uint16_t capabilities[11];  // Various capability bits
    
    // Word 60-61
    uint32_t total_sectors;     // Total addressable sectors (28-bit)
    
    // Word 62
    uint16_t obsolete3;         // Obsolete
    
    // Word 63
    uint16_t multiword_dma;     // Multiword DMA support and status
    
    // Word 64
    uint16_t pio_modes;         // PIO modes supported
    
    // Word 65-70
    uint16_t min_multiword_time; // Min multiword DMA transfer cycle time (ns)
    uint16_t rec_multiword_time; // Recommended multiword DMA cycle time (ns)
    uint16_t min_pio_time;      // Min PIO transfer cycle time (ns)
    uint16_t min_pio_iordy_time; // Min PIO transfer cycle time with IORDY (ns)
    
    // Word 71-74
    uint16_t additional_supported[4]; // Additional supported features
    
    // Word 75-79
    uint16_t queue_depth;       // Queue depth
    uint16_t sata_capabilities;  // SATA capabilities
    uint16_t sata_additional;   // SATA additional features
    uint16_t sata_features_enabled; // SATA features enabled
    
    // Word 80-86
    uint16_t major_version;     // Major version number
    uint16_t minor_version;     // Minor version number
    uint16_t commands_supported[3]; // Command sets supported
    uint16_t commands_enabled[2]; // Command sets enabled
    
    // Word 87-92
    uint16_t ultra_dma;         // Ultra DMA support and status
    uint16_t time_erase_normal; // Time for security erase completion
    uint16_t time_erase_enhanced; // Time for enhanced security erase
    uint16_t current_apm;       // Current APM level
    uint16_t master_password;   // Master password revision
    uint16_t hw_reset_result;   // Result of hardware reset
    
    // Word 93
    uint16_t acoustic;          // Acoustic management
    
    // Word 94-99
    uint16_t stream_min_size;   // Stream minimum request size
    uint16_t stream_transfer_time; // Stream transfer time
    uint16_t stream_access_latency; // Stream access latency
    uint32_t stream_performance; // Stream performance granularity
    
    // Word 100-103
    uint64_t max_lba;           // Max user LBA (48-bit)
    
    // Word 104-105
    uint16_t stream_transfer_time_pio; // PIO stream transfer time
    uint16_t reserved104;       // Reserved
    
    // Word 106
    uint16_t sector_size;       // Physical/Logical sector size
    
    // Word 107-116
    uint16_t inter_seek_delay;  // Interseek delay for ISO 7779
    uint16_t world_wide_name[4]; // World wide name
    uint16_t reserved112[5];    // Reserved
    
    // Word 117-118
    uint32_t words_per_sector;  // Words per logical sector
    
    // Word 119-126
    uint16_t reserved119[8];    // Reserved
    
    // Word 127
    uint16_t removable_status;  // Removable media status
    
    // Word 128
    uint16_t security_status;   // Security status
    
    // Word 129-159
    uint16_t vendor_specific[31]; // Vendor specific
    
    // Word 160
    uint16_t cfa_power_mode;    // CFA power mode 1
    
    // Word 161-167
    uint16_t reserved_for_compactflash[7]; // Reserved for CompactFlash
    
    // Word 168-173
    uint16_t form_factor;       // Device form factor
    uint16_t data_set_mgmt;     // Data set management support
    uint16_t additional_product_id[4]; // Additional product identifier
    
    // Word 174-175
    uint16_t reserved174[2];    // Reserved
    
    // Word 176-195
    uint16_t current_media[20]; // Current media serial number
    
    // Word 196-205
    uint16_t sct_command;       // SCT command transport
    uint16_t reserved197[9];    // Reserved
    
    // Word 206-254
    uint16_t reserved206[49];   // Reserved
    
    // Word 255
    uint16_t integrity;         // Integrity word
} __attribute__((packed)) ata_identify_t;

// SATA port structure
typedef struct {
    uint8_t status;             // Port status (0=none, 1=present, 2=active)
    uint8_t type;               // Device type (ATA, ATAPI, etc.)
    uint16_t reserved;          // Reserved for alignment
    
    uint32_t sector_size;       // Sector size in bytes
    uint64_t sector_count;      // Total sectors on device
    
    char model[41];             // Model name (40 chars + null)
    char serial[21];            // Serial number (20 chars + null)
    char firmware[9];           // Firmware revision (8 chars + null)
    
    // Command list and received FIS pointers
    hba_cmd_header_t *cmd_list;       // 1K aligned
    hba_fis_t *fis;                  // 256B aligned
    hba_cmd_tbl_t *cmd_tables[32];    // Command tables, 128B aligned
    
    // Port control
    hba_port_t *port_base;      // Port register base address
    uint8_t port_num;           // Port number within controller
    uint8_t controller_id;      // Controller ID
    
    // DMA buffer space (used for data transfers)
    void *dma_buffer;           // Buffer for data transfers
    uintptr_t dma_buffer_phys;  // Physical address of buffer
    size_t dma_buffer_size;     // Size of buffer
    
    // Port access lock
    spinlock_t lock;            // Spinlock for port access
} sata_port_t;

// SATA controller structure
typedef struct {
    bool initialized;           // Controller initialized flag
    
    // PCI device information
    uint8_t pci_bus;            // PCI bus number
    uint8_t pci_device;         // PCI device number
    uint8_t pci_function;       // PCI function number
    uint16_t vendor_id;         // PCI vendor ID
    uint16_t device_id;         // PCI device ID
    
    // AHCI host controller information
    hba_mem_t *abar;            // AHCI base address register (memory mapped)
    uintptr_t abar_phys;        // Physical address of ABAR
    
    // Port information
    uint32_t ports_impl;        // Implemented ports bitmask
    uint32_t ports_active;      // Active ports bitmask
    uint32_t slot_count;        // Number of command slots per port
    
    // Port structures
    sata_port_t ports[32];      // Port structures (max 32 ports per controller)
} sata_controller_t;

// SATA driver state
typedef struct {
    sata_controller_t controllers[SATA_MAX_CONTROLLERS];  // Array of controllers
    uint32_t controller_count;                           // Number of controllers
    uint32_t total_ports;                                // Total number of ports
    spinlock_t global_lock;                              // Driver global lock
    bool initialized;                                    // Driver initialized flag
} sata_driver_t;

// Public function declarations

// Initialize the SATA driver
bool sata_init(void);

// Shutdown the SATA driver
void sata_shutdown(void);

// Read data from SATA device
bool sata_read(uint32_t port_id, uint64_t lba, void* buffer, uint32_t sector_count);

// Write data to SATA device
bool sata_write(uint32_t port_id, uint64_t lba, const void* buffer, uint32_t sector_count);

// Flush SATA device cache
bool sata_flush(uint32_t port_id);

// Get device information
bool sata_get_port_info(uint32_t port_id, char* buffer, size_t buffer_size);

// Get number of SATA ports
uint32_t sata_get_port_count(void);

// Diagnostics function - Dump information about all SATA devices
void sata_debug_dump_info(void);

#endif // _SYNCOS_SATA_H