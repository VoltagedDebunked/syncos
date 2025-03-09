#include <core/drivers/net/e1000.h>
#include <core/drivers/pci.h>
#include <syncos/idt.h>
#include <kstd/io.h>
#include <kstd/string.h>
#include <syncos/vmm.h>

#define E1000_REG_CTRL     0x0000
#define E1000_REG_STATUS   0x0008
#define E1000_REG_ICR      0x00C0
#define E1000_REG_IMS      0x00D0
#define E1000_REG_IMC      0x00D8
#define E1000_REG_RCTL     0x0100
#define E1000_REG_TCTL     0x0400

#define E1000_CTRL_RESET   0x04000000
#define E1000_CTRL_SLU     0x00000040

volatile uint8_t* e1000_base = NULL;

#define MMU_PROT_NONE  0x00    // No access
#define MMU_PROT_READ  0x01    // Read permission
#define MMU_PROT_WRITE 0x02    // Write permission
#define MMU_PROT_EXEC  0x04    // Execute permission

// Globals
static uint16_t iobase;
static uint8_t irq;
extern pci_device_t pci_devices[];

static uint8_t tx_buffer[8192];
static uint8_t rx_buffer[8192];

static uint8_t* tx_buffers[8];
static uint32_t tx_buffer_indices[8];
static struct e1000_tx_desc* tx_descs[8];

static uint8_t* rx_buffers[32];
static uint32_t rx_buffer_indices[32];  
static struct e1000_rx_desc* rx_descs[32];

// Helper to read from E1000 registers
static inline uint32_t e1000_read(uint16_t index) {
    return inl(iobase + index);
}

// Helper to write to E1000 registers  
static inline void e1000_write(uint16_t index, uint32_t value) {
    outl(iobase + index, value);
}

void e1000_init(void) {
    pci_device_t* dev = pci_find_device(0x8086, 0x100E);
    if (!dev) return;
    
    uint16_t cmd = pci_read_config_word(dev, 0x04);
    pci_write_config_word(dev, 0x04, cmd | 0x0007);
    
    uint32_t bar0 = pci_read_config_dword(dev, 0x10) & ~0xF;
    e1000_base = vmm_map_physical(bar0, 0x20000, MMU_PROT_WRITE);
    
    // Disable interrupts
    e1000_write(E1000_REG_IMC, 0xFFFFFFFF);
    e1000_read(E1000_REG_ICR);
    
    // Reset
    e1000_write(E1000_REG_CTRL, e1000_read(E1000_REG_CTRL) | E1000_CTRL_RESET);
    
    // Wait for reset completion
    int timeout = 10000;
    while ((e1000_read(E1000_REG_CTRL) & E1000_CTRL_RESET) && --timeout) {}
    if (!timeout) return;
    
    // Link up
    e1000_write(E1000_REG_CTRL, e1000_read(E1000_REG_CTRL) | E1000_CTRL_SLU);
    
    // Set MAC address if needed
    // Setup transmit control
    e1000_write(E1000_REG_TCTL, 0x0003A007); // TCTL.EN, TCTL.PSP, TCTL.CT=0x0F, TCTL.COLD=0x3F
    
    // Setup receive control
    e1000_write(E1000_REG_RCTL, 0x00000004); // RCTL.EN
}

// Read the MAC address from the EEPROM  
void e1000_read_mac(uint8_t mac[6]) {
    uint16_t data;
    
    data = e1000_read(E1000_REG_RAH); 
    mac[0] = (data >> 8) & 0xFF;
    mac[1] = data & 0xFF;

    data = e1000_read(E1000_REG_RAL);
    mac[2] = (data >> 24) & 0xFF;  
    mac[3] = (data >> 16) & 0xFF;
    mac[4] = (data >> 8) & 0xFF;
    mac[5] = data & 0xFF;  
}

// Transmit a frame
void e1000_transmit(uint8_t* data, uint32_t len) {
    static uint8_t tx_index = 0;  
    
    // Copy the data to the next available transmit buffer
    memcpy(tx_buffers[tx_index], data, len);
    
    // Set the length in the descriptor
    tx_descs[tx_index]->length = len;

    // Move to next transmit descriptor  
    tx_index = (tx_index + 1) % 8;

    // Update tail pointer 
    e1000_write(E1000_REG_TDT, tx_index);
}

// Check for received packet and copy to buffer  
// Returns number of bytes received
uint32_t e1000_receive(uint8_t* buffer) {
    static uint8_t rx_index = 0;
    
    // Check if a packet has been received
    if(!(rx_descs[rx_index]->status & 0x01)) {
        return 0;  
    }

    // Get the length 
    uint16_t len = rx_descs[rx_index]->length;

    // Copy into the receive buffer
    memcpy(buffer, rx_buffers[rx_index], len);

    // Clear the status bits  
    rx_descs[rx_index]->status = 0;  

    // Move to next receive descriptor
    rx_index = (rx_index + 1) % 32;

    // Update tail pointer
    e1000_write(E1000_REG_RDT, rx_index);

    return len;
}