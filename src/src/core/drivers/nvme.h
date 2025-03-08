#ifndef _SYNCOS_NVME_H
#define _SYNCOS_NVME_H

#include <stdint.h>
#include <stdbool.h>
#include <syncos/spinlock.h>
#include <stddef.h>

// NVMe Controller Register Space Offsets
#define NVME_REG_CAP       0x0000  // Controller Capabilities
#define NVME_REG_VS        0x0008  // Version
#define NVME_REG_INTMS     0x000C  // Interrupt Mask Set
#define NVME_REG_INTMC     0x0010  // Interrupt Mask Clear
#define NVME_REG_CC        0x0014  // Controller Configuration
#define NVME_REG_CSTS      0x001C  // Controller Status
#define NVME_REG_NSSR      0x0020  // NVM Subsystem Reset
#define NVME_REG_AQA       0x0024  // Admin Queue Attributes
#define NVME_REG_ASQ       0x0028  // Admin Submission Queue Base Address
#define NVME_REG_ACQ       0x0030  // Admin Completion Queue Base Address
#define NVME_REG_CMBLOC    0x0038  // Controller Memory Buffer Location
#define NVME_REG_CMBSZ     0x003C  // Controller Memory Buffer Size
#define NVME_REG_BPINFO    0x0040  // Boot Partition Information
#define NVME_REG_BPRSEL    0x0044  // Boot Partition Read Select
#define NVME_REG_BPMBL     0x0048  // Boot Partition Memory Buffer Location
#define NVME_REG_CMBMSC    0x0050  // Controller Memory Buffer Memory Space Control
#define NVME_REG_PMRCAP    0x0E00  // Persistent Memory Region Capabilities
#define NVME_REG_PMRCTL    0x0E04  // Persistent Memory Region Control
#define NVME_REG_PMRSTS    0x0E08  // Persistent Memory Region Status
#define NVME_REG_PMREBS    0x0E0C  // Persistent Memory Region Elasticity Buffer Size
#define NVME_REG_PMRSWTP   0x0E10  // Persistent Memory Region Sustained Write Throughput
#define NVME_REG_DBS       0x1000  // Doorbell stride

// Controller Configuration Register (CC) bits
#define NVME_CC_EN        (1 << 0)  // Enable bit
#define NVME_CC_CSS_NVM   (0 << 4)  // I/O Command Set: NVM command set
#define NVME_CC_CSS_ADMIN (1 << 4)  // I/O Command Set: Admin command set
#define NVME_CC_MPS_SHIFT 7         // Memory Page Size shift
#define NVME_CC_AMS_RR    (0 << 11) // Arbitration Mechanism: Round Robin
#define NVME_CC_AMS_WRR   (1 << 11) // Arbitration Mechanism: Weighted Round Robin
#define NVME_CC_AMS_VS    (7 << 11) // Arbitration Mechanism: Vendor Specific
#define NVME_CC_SHN_NONE  (0 << 14) // Shutdown Notification: None
#define NVME_CC_SHN_NORM  (1 << 14) // Shutdown Notification: Normal
#define NVME_CC_SHN_ABRUPT (2 << 14) // Shutdown Notification: Abrupt
#define NVME_CC_IOSQES_SHIFT 16    // I/O SQ Entry Size shift
#define NVME_CC_IOCQES_SHIFT 20    // I/O CQ Entry Size shift

// Controller Status Register (CSTS) bits
#define NVME_CSTS_RDY     (1 << 0)  // Ready
#define NVME_CSTS_CFS     (1 << 1)  // Controller Fatal Status
#define NVME_CSTS_SHST_NORM (0 << 2) // Shutdown Status: Normal
#define NVME_CSTS_SHST_OCCUR (1 << 2) // Shutdown Status: Occurring
#define NVME_CSTS_SHST_COMP (2 << 2) // Shutdown Status: Complete
#define NVME_CSTS_NSSRO   (1 << 4)  // NVM Subsystem Reset Occurred

// NVMe command opcodes
#define NVME_CMD_ADMIN_DELETE_SQ        0x00
#define NVME_CMD_ADMIN_CREATE_SQ        0x01
#define NVME_CMD_ADMIN_GET_LOG_PAGE     0x02
#define NVME_CMD_ADMIN_DELETE_CQ        0x04
#define NVME_CMD_ADMIN_CREATE_CQ        0x05
#define NVME_CMD_ADMIN_IDENTIFY         0x06
#define NVME_CMD_ADMIN_ABORT            0x08
#define NVME_CMD_ADMIN_SET_FEATURES     0x09
#define NVME_CMD_ADMIN_GET_FEATURES     0x0A
#define NVME_CMD_ADMIN_ASYNC_EVENT      0x0C
#define NVME_CMD_ADMIN_NAMESPACE_MGMT   0x0D
#define NVME_CMD_ADMIN_FIRMWARE         0x10
#define NVME_CMD_ADMIN_FORMAT_NVM       0x80
#define NVME_CMD_ADMIN_SECURITY_SEND    0x81
#define NVME_CMD_ADMIN_SECURITY_RECV    0x82

// NVMe I/O command opcodes
#define NVME_CMD_IO_FLUSH               0x00
#define NVME_CMD_IO_WRITE               0x01
#define NVME_CMD_IO_READ                0x02
#define NVME_CMD_IO_WRITE_UNCORRECTABLE 0x04
#define NVME_CMD_IO_COMPARE             0x05
#define NVME_CMD_IO_WRITE_ZEROES        0x08
#define NVME_CMD_IO_DATASET_MANAGEMENT  0x09
#define NVME_CMD_IO_RESERVATION_REG     0x0D
#define NVME_CMD_IO_RESERVATION_REPORT  0x0E
#define NVME_CMD_IO_RESERVATION_ACQUIRE 0x11
#define NVME_CMD_IO_RESERVATION_RELEASE 0x15

// Identify command CNS values
#define NVME_IDENTIFY_CNS_NAMESPACE     0x00
#define NVME_IDENTIFY_CNS_CONTROLLER    0x01
#define NVME_IDENTIFY_CNS_NS_ACTIVE_LIST 0x02
#define NVME_IDENTIFY_CNS_NS_DESC_LIST  0x03
#define NVME_IDENTIFY_CNS_NS_PRESENT_LIST 0x10
#define NVME_IDENTIFY_CNS_CTRL_LIST     0x13

// NVMe Command completion status codes
#define NVME_SC_SUCCESS                 0x0
#define NVME_SC_INVALID_COMMAND         0x1
#define NVME_SC_INVALID_FIELD           0x2
#define NVME_SC_COMMAND_ID_CONFLICT     0x3
#define NVME_SC_DATA_TRANSFER_ERROR     0x4
#define NVME_SC_POWER_LOSS              0x5
#define NVME_SC_INTERNAL_ERROR          0x6
#define NVME_SC_ABORT_REQUESTED         0x7
#define NVME_SC_ABORT_FAILED            0x8
#define NVME_SC_INVALID_NAMESPACE       0x9
#define NVME_SC_FORMAT_IN_PROGRESS      0xA
#define NVME_SC_INVALID_FORMAT          0xB
#define NVME_SC_CAPACITY_EXCEEDED       0xC
#define NVME_SC_NS_NOT_READY            0xD

// Maximum number of NVMe devices supported
#define NVME_MAX_DEVICES                 8

// Maximum number of namespaces per controller
#define NVME_MAX_NAMESPACES              16

// Maximum queue entries
#define NVME_ADMIN_QUEUE_SIZE           64   // Admin queue size
#define NVME_IO_QUEUE_SIZE              1024 // I/O queue size

// Ring size for command tracking
#define NVME_CMD_RING_SIZE              256

// NVMe submission queue entry
typedef struct __attribute__((packed)) {
    // Command DWORD 0
    uint8_t  opcode;          // Opcode
    uint8_t  flags;           // Command flags
    uint16_t command_id;      // Command identifier
    
    // Command DWORD 1
    uint32_t nsid;            // Namespace identifier
    
    // Command DWORD 2-3
    uint64_t reserved;
    
    // Command DWORD 4-5
    uint64_t metadata;        // Metadata pointer
    
    // Command DWORD 6-9
    uint64_t prp1;            // PRP entry 1
    uint64_t prp2;            // PRP entry 2 or PRP list pointer
    
    // Command DWORD 10-15
    uint32_t cdw10;           // Command-specific
    uint32_t cdw11;           // Command-specific
    uint32_t cdw12;           // Command-specific
    uint32_t cdw13;           // Command-specific
    uint32_t cdw14;           // Command-specific
    uint32_t cdw15;           // Command-specific
} nvme_command_t;

// NVMe completion queue entry
typedef struct __attribute__((packed)) {
    uint32_t result;          // Command-specific result
    uint32_t rsvd;            // Reserved
    uint16_t sq_head;         // Submission queue head pointer
    uint16_t sq_id;           // Submission queue identifier
    uint16_t command_id;      // Command identifier from the associated submission queue entry
    uint16_t status;          // Status field
} nvme_completion_t;

// NVMe queue structure
typedef struct {
    void* sq_addr;            // Submission queue virtual address
    void* cq_addr;            // Completion queue virtual address
    uintptr_t sq_phys;        // Submission queue physical address
    uintptr_t cq_phys;        // Completion queue physical address
    uint32_t sq_size;         // Submission queue size
    uint32_t cq_size;         // Completion queue size
    uint16_t sq_head;         // Submission queue head pointer
    uint16_t sq_tail;         // Submission queue tail pointer
    uint16_t cq_head;         // Completion queue head pointer
    uint16_t sq_id;           // Submission queue ID
    uint16_t cq_id;           // Completion queue ID
    uint16_t phase;           // Phase bit for completion
    spinlock_t lock;          // Queue lock
} nvme_queue_t;

// NVMe controller structure
typedef struct {
    uintptr_t regs_phys;       // Physical address of registers
    volatile void* regs;       // Virtual address of mapped registers
    uint32_t doorbell_stride;  // Doorbell stride
    uint32_t db_stride;        // Doorbell stride in bytes
    uint32_t max_transfer_shift; // Maximum data transfer size (log2)
    uint32_t max_namespaces;   // Maximum number of namespaces supported
    uint8_t acq_size;          // Admin completion queue size (log2)
    uint8_t asq_size;          // Admin submission queue size (log2)
    
    // PCI device information
    uint8_t pci_bus;           // PCI bus number
    uint8_t pci_device;        // PCI device number
    uint8_t pci_function;      // PCI function number
    uint16_t vendor_id;        // PCI vendor ID
    uint16_t device_id;        // PCI device ID
    uint8_t irq_vector;        // IRQ vector assigned to the device
    
    spinlock_t lock;           // Controller lock
    
    // Admin & I/O queues
    nvme_queue_t admin_queue;
    nvme_queue_t io_queue;
    
    // Command tracking - ring buffer of command IDs
    uint16_t cmd_id;           // Next command ID to use
    uint16_t active_cmds[NVME_CMD_RING_SIZE]; // Ring of active command IDs
    uint16_t cmd_ring_head;
    uint16_t cmd_ring_tail;
    
    // Controller information
    char model_number[41];     // Model name (40 chars + null)
    char serial_number[21];    // Serial number (20 chars + null)
    char firmware_rev[9];      // Firmware revision (8 chars + null)

    bool use_admin_for_io;  // Flag to indicate we're using Admin queue for I/O
    
    // Namespace information
    uint32_t ns_count;         // Number of active namespaces
    struct {
        uint32_t id;           // Namespace ID
        uint64_t size;         // Size in logical blocks
        uint32_t lba_size;     // LBA size in bytes
    } namespaces[NVME_MAX_NAMESPACES];
    
    bool initialized;          // Controller initialization flag
} nvme_controller_t;

// NVMe driver state
typedef struct {
    nvme_controller_t controllers[NVME_MAX_DEVICES];
    uint32_t device_count;
    spinlock_t global_lock;
} nvme_driver_t;

// NVMe command structures

// Identify controller data structure
typedef struct __attribute__((packed)) {
    // Controller capabilities and features (bytes 0-127)
    uint16_t vid;               // PCI Vendor ID
    uint16_t ssvid;             // PCI Subsystem Vendor ID
    char     sn[20];            // Serial Number
    char     mn[40];            // Model Number
    char     fr[8];             // Firmware Revision
    uint8_t  rab;               // Recommended Arbitration Burst
    uint8_t  ieee[3];           // IEEE OUI Identifier
    uint8_t  mic;               // Multi-Interface Capabilities
    uint8_t  mdts;              // Maximum Data Transfer Size
    uint16_t cntlid;            // Controller ID
    uint32_t ver;               // Version
    uint32_t rtd3r;             // RTD3 Resume Latency
    uint32_t rtd3e;             // RTD3 Entry Latency
    uint32_t oaes;              // Optional Asynchronous Events Supported
    uint32_t ctratt;            // Controller Attributes
    uint16_t rrls;              // Read Recovery Levels Supported
    uint8_t  reserved1[9];      // Reserved
    
    // Admin command set attributes (bytes 128-255)
    uint8_t  sqes;              // Submission Queue Entry Size
    uint8_t  cqes;              // Completion Queue Entry Size
    uint16_t maxcmd;            // Maximum Outstanding Commands
    uint32_t nn;                // Number of Namespaces
    uint16_t oncs;              // Optional NVM Command Support
    uint16_t fuses;             // Fused Operation Support
    uint8_t  fna;               // Format NVM Attributes
    uint8_t  vwc;               // Volatile Write Cache
    uint16_t awun;              // Atomic Write Unit Normal
    uint16_t awupf;             // Atomic Write Unit Power Fail
    uint8_t  nvscc;             // NVM Vendor Specific Command Configuration
    uint8_t  nwpc;              // Namespace Write Protection Capabilities
    uint16_t acwu;              // Atomic Compare & Write Unit
    uint16_t reserved2;         // Reserved
    uint32_t sgls;              // SGL Support
    uint32_t mnan;              // Maximum Number of Allowed Namespaces
    uint8_t  reserved3[224-168]; // Reserved
    
    // NVM command set attributes (bytes 256-511)
    uint8_t  subnqn[256];       // NVM Subsystem NVMe Qualified Name
    
    // Power state descriptors (bytes 512-1023)
    struct {
        uint16_t max_power;     // Maximum Power
        uint8_t  rsvd2;         // Reserved
        uint8_t  flags;         // Flags
        uint32_t entry_lat;     // Entry Latency
        uint32_t exit_lat;      // Exit Latency
        uint8_t  read_tput;     // Relative Read Throughput
        uint8_t  read_lat;      // Relative Read Latency
        uint8_t  write_tput;    // Relative Write Throughput
        uint8_t  write_lat;     // Relative Write Latency
        uint16_t idle_power;    // Idle Power
        uint8_t  idle_scale;    // Idle Power Scale
        uint8_t  rsvd3;         // Reserved
        uint16_t active_power;  // Active Power
        uint8_t  active_work_scale; // Active Power Scale and Workload
        uint8_t  rsvd4[9];      // Reserved
    } psd[32];                  // Power State Descriptors
    
    // Vendor specific (bytes 1024-4095)
    uint8_t  vendor_specific[3072]; // Vendor Specific
} nvme_identify_controller_t;

// Identify namespace data structure
typedef struct __attribute__((packed)) {
    // Namespace Size and Capacity (bytes 0-31)
    uint64_t nsze;              // Namespace Size (total size in logical blocks)
    uint64_t ncap;              // Namespace Capacity
    uint64_t nuse;              // Namespace Utilization
    
    // Features and Capabilities (bytes 32-63)
    uint8_t  nsfeat;            // Namespace Features
    uint8_t  nlbaf;             // Number of LBA Formats
    uint8_t  flbas;             // Formatted LBA Size
    uint8_t  mc;                // Metadata Capabilities
    uint8_t  dpc;               // End-to-end Data Protection Capabilities
    uint8_t  dps;               // End-to-end Data Protection Type Settings
    uint8_t  nmic;              // Namespace Multi-path I/O and Namespace Sharing Capabilities
    uint8_t  rescap;            // Reservation Capabilities
    uint8_t  fpi;               // Format Progress Indicator
    uint8_t  dlfeat;            // Deallocate Logical Block Features
    uint16_t nawun;             // Namespace Atomic Write Unit Normal
    uint16_t nawupf;            // Namespace Atomic Write Unit Power Fail
    uint16_t nacwu;             // Namespace Atomic Compare & Write Unit
    uint16_t nabsn;             // Namespace Atomic Boundary Size Normal
    uint16_t nabo;              // Namespace Atomic Boundary Offset
    uint16_t nabspf;            // Namespace Atomic Boundary Size Power Fail
    uint16_t noiob;             // Namespace Optimal I/O Boundary
    uint64_t nvmcap[2];         // NVM Capacity
    uint8_t  reserved1[40];     // Reserved
    
    // Namespace Globally Unique Identifier (bytes 104-119)
    uint8_t  nguid[16];         // Namespace Globally Unique Identifier
    
    // EUI-64 (bytes 120-127)
    uint64_t eui64;             // IEEE Extended Unique Identifier
    
    // LBA Format Support (bytes 128-191)
    struct {
        uint16_t ms;            // Metadata Size
        uint8_t  lbads;         // LBA Data Size (log2)
        uint8_t  rp;            // Relative Performance
    } lbaf[16];
    
    // Reserved (bytes 192-383)
    uint8_t  reserved2[192];    // Reserved
    
    // Vendor specific (bytes 384-4095)
    uint8_t  vendor_specific[3712]; // Vendor Specific
} nvme_identify_namespace_t;

// Public function declarations

// Initialize the NVMe driver
bool nvme_init(void);

// Shutdown the NVMe driver
void nvme_shutdown(void);

// Read data from NVMe device
bool nvme_read(uint32_t device_id, uint32_t namespace_id, uint64_t lba, void* buffer, uint32_t blocks);

// Write data to NVMe device
bool nvme_write(uint32_t device_id, uint32_t namespace_id, uint64_t lba, const void* buffer, uint32_t blocks);

// Flush NVMe device cache
bool nvme_flush(uint32_t device_id, uint32_t namespace_id);

// Get device information (fills provided buffer with device info string)
bool nvme_get_device_info(uint32_t device_id, char* buffer, size_t buffer_size);

// Get namespace information
bool nvme_get_namespace_info(uint32_t device_id, uint32_t namespace_id, uint64_t* size_blocks, 
                            uint32_t* block_size);

// Get the number of NVMe devices
uint32_t nvme_get_device_count(void);

// Get the number of namespaces for a device
uint32_t nvme_get_namespace_count(uint32_t device_id);

// Debug function to dump NVMe controller information
void nvme_debug_dump_controller(uint32_t device_id);

#endif // _SYNCOS_NVME_H