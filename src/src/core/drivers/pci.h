#ifndef _SYNCOS_PCI_H
#define _SYNCOS_PCI_H

#include <stdint.h>
#include <stdbool.h>

// PCI Configuration space registers
#define PCI_CONFIG_VENDOR_ID      0x00
#define PCI_CONFIG_DEVICE_ID      0x02
#define PCI_CONFIG_COMMAND        0x04
#define PCI_CONFIG_STATUS         0x06
#define PCI_CONFIG_REVISION_ID    0x08
#define PCI_CONFIG_PROG_IF        0x09
#define PCI_CONFIG_SUBCLASS       0x0A
#define PCI_CONFIG_CLASS          0x0B
#define PCI_CONFIG_CACHE_LINE     0x0C
#define PCI_CONFIG_LATENCY        0x0D
#define PCI_CONFIG_HEADER_TYPE    0x0E
#define PCI_CONFIG_BIST           0x0F
#define PCI_CONFIG_BAR0           0x10
#define PCI_CONFIG_BAR1           0x14
#define PCI_CONFIG_BAR2           0x18
#define PCI_CONFIG_BAR3           0x1C
#define PCI_CONFIG_BAR4           0x20
#define PCI_CONFIG_BAR5           0x24
#define PCI_CONFIG_CARDBUS_CIS    0x28
#define PCI_CONFIG_SUBSYS_VENDOR  0x2C
#define PCI_CONFIG_SUBSYS_ID      0x2E
#define PCI_CONFIG_EXP_ROM_BASE   0x30
#define PCI_CONFIG_CAPABILITIES   0x34
#define PCI_CONFIG_INTERRUPT_LINE 0x3C
#define PCI_CONFIG_INTERRUPT_PIN  0x3D
#define PCI_CONFIG_MIN_GRANT      0x3E
#define PCI_CONFIG_MAX_LATENCY    0x3F

// PCI command register bits
#define PCI_COMMAND_IO            0x0001
#define PCI_COMMAND_MEMORY        0x0002
#define PCI_COMMAND_MASTER        0x0004
#define PCI_COMMAND_SPECIAL       0x0008
#define PCI_COMMAND_INVALIDATE    0x0010
#define PCI_COMMAND_VGA_PALETTE   0x0020
#define PCI_COMMAND_PARITY        0x0040
#define PCI_COMMAND_WAIT          0x0080
#define PCI_COMMAND_SERR          0x0100
#define PCI_COMMAND_FAST_BACK     0x0200
#define PCI_COMMAND_INTERRUPT     0x0400

// PCI status register bits
#define PCI_STATUS_INTERRUPT      0x0008
#define PCI_STATUS_CAPABILITIES   0x0010
#define PCI_STATUS_66MHZ          0x0020
#define PCI_STATUS_FAST_BACK      0x0080
#define PCI_STATUS_PARITY_ERROR   0x0100
#define PCI_STATUS_DEVSEL_MASK    0x0600
#define PCI_STATUS_SIG_TARGET_ABORT 0x0800
#define PCI_STATUS_REC_TARGET_ABORT 0x1000
#define PCI_STATUS_REC_MASTER_ABORT 0x2000
#define PCI_STATUS_SIG_SYSTEM_ERROR 0x4000
#define PCI_STATUS_PARITY_DETECT  0x8000

// PCI header types
#define PCI_HEADER_TYPE_NORMAL    0x00
#define PCI_HEADER_TYPE_BRIDGE    0x01
#define PCI_HEADER_TYPE_CARDBUS   0x02
#define PCI_HEADER_TYPE_MULTI_FUNC 0x80

// PCI BAR types
#define PCI_BAR_TYPE_MASK         0x01
#define PCI_BAR_TYPE_MEMORY       0x00
#define PCI_BAR_TYPE_IO           0x01
#define PCI_BAR_MEMORY_TYPE_MASK  0x06
#define PCI_BAR_MEMORY_TYPE_32    0x00
#define PCI_BAR_MEMORY_TYPE_1M    0x02
#define PCI_BAR_MEMORY_TYPE_64    0x04
#define PCI_BAR_MEMORY_PREFETCH   0x08
#define PCI_BAR_IO_MASK           0xFFFFFFFC
#define PCI_BAR_MEMORY_MASK       0xFFFFFFF0

// PCI device structure
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;
    uint8_t header_type;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
} pci_device_t;

extern pci_device_t pci_devices[];

// PCI function prototypes
void pci_init(void);
uint8_t pci_read_config_byte(pci_device_t *device, uint8_t offset);
uint16_t pci_read_config_word(pci_device_t *device, uint8_t offset);
uint32_t pci_read_config_dword(pci_device_t *device, uint8_t offset);
void pci_write_config_byte(pci_device_t *device, uint8_t offset, uint8_t value);
void pci_write_config_word(pci_device_t *device, uint8_t offset, uint16_t value);
void pci_write_config_dword(pci_device_t *device, uint8_t offset, uint32_t value);
pci_device_t* pci_find_devices(uint8_t class_code, uint8_t subclass, uint32_t* count);
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id);
uintptr_t pci_get_bar_address(pci_device_t *device, uint8_t bar);
uint32_t pci_get_bar_size(pci_device_t *device, uint8_t bar);
bool pci_enable_bus_mastering(pci_device_t *device);
void pci_enable_device(pci_device_t *device);
void pci_dump_device_info(pci_device_t *device);

#endif // _SYNCOS_PCI_H