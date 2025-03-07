#include <syncos/mouse.h>
#include <syncos/irq.h>
#include <syncos/pic.h>
#include <kstd/io.h>
#include <kstd/stdio.h>

// Mouse buffer configuration
#define MOUSE_BUFFER_SIZE 256

// Mouse state structure
typedef struct {
    // Packet assembly state
    uint8_t  packet_bytes[4];     // Bytes of current packet
    uint8_t  packet_stage;        // Current stage of packet assembly
    bool     have_first_byte;     // Have we received first byte of packet?
    
    // Event buffer
    mouse_packet_t buffer[MOUSE_BUFFER_SIZE];
    volatile uint16_t read_index;
    volatile uint16_t write_index;
    
    // Callback system
    mouse_callback_t callbacks[8];
    int callback_count;
} mouse_state_t;

// Static mouse state
static mouse_state_t mouse_state = {0};

// Wait for PS/2 controller to be ready
static void mouse_wait(bool is_write) {
    // Wait for controller to be ready
    uint32_t timeout = 100000;
    if (is_write) {
        while (timeout-- && (inb(MOUSE_STATUS_PORT) & 0x2)) {
            __asm__ volatile("pause");
        }
    } else {
        while (timeout-- && !(inb(MOUSE_STATUS_PORT) & 0x1)) {
            __asm__ volatile("pause");
        }
    }
}

// Send a command to the mouse
static void mouse_send_command(uint8_t cmd) {
    // Wait for write
    mouse_wait(true);
    
    // Tell controller we're sending a mouse command
    outb(MOUSE_COMMAND_PORT, 0xD4);
    
    // Wait for write again
    mouse_wait(true);
    
    // Send the actual command
    outb(MOUSE_DATA_PORT, cmd);
}

// Read a byte from the mouse
static uint8_t mouse_read(void) {
    // Wait for data
    mouse_wait(false);
    
    // Read and return the byte
    return inb(MOUSE_DATA_PORT);
}

// Initialize the mouse
void mouse_init(void) {
    // Disable interrupts during setup
    __asm__ volatile("cli");
    
    // Reset mouse state
    mouse_state.read_index = 0;
    mouse_state.write_index = 0;
    mouse_state.packet_stage = 0;
    mouse_state.have_first_byte = false;
    mouse_state.callback_count = 0;
    
    // Enable the auxiliary device (mouse)
    mouse_wait(true);
    outb(MOUSE_COMMAND_PORT, 0xA8);
    
    // Enable interrupts
    mouse_wait(true);
    outb(MOUSE_COMMAND_PORT, 0x20);
    mouse_wait(false);
    uint8_t status = inb(MOUSE_DATA_PORT);
    
    // Set compaq status bit
    mouse_wait(true);
    outb(MOUSE_COMMAND_PORT, 0x60);
    mouse_wait(true);
    outb(MOUSE_DATA_PORT, status | 0x2);
    
    // Default device initialization
    mouse_send_command(0xF6);  // Set default settings
    mouse_read();  // Acknowledge
    
    // Enable data reporting
    mouse_send_command(0xF4);
    mouse_read();  // Acknowledge
    
    // Re-enable interrupts
    __asm__ volatile("sti");

    // Register PS/2 mouse interrupt
    if (!irq_register_handler(IRQ_PS2_MOUSE, mouse_irq_handler, NULL)) {
        printf("Timer: Failed to register PS/2 mouse IRQ handler\n");
        return;
    }
    
    printf("PS/2 Mouse: Initialized\n");
}

// PS/2 Mouse IRQ handler
bool mouse_irq_handler(uint8_t irq, void *context) {
    // Read the mouse data byte
    uint8_t mouse_byte = inb(MOUSE_DATA_PORT);
    
    // Assemble the full packet
    switch (mouse_state.packet_stage) {
        case 0: {
            // First byte of the packet
            // Bit 3 must be set in the first byte
            if ((mouse_byte & 0x08) == 0) {
                // Invalid first byte
                return false;
            }
            
            mouse_state.packet_bytes[0] = mouse_byte;
            mouse_state.packet_stage = 1;
            mouse_state.have_first_byte = true;
            break;
        }
        
        case 1: {
            // Second byte: X movement
            mouse_state.packet_bytes[1] = mouse_byte;
            mouse_state.packet_stage = 2;
            break;
        }
        
        case 2: {
            // Third byte: Y movement and complete the packet
            mouse_state.packet_bytes[2] = mouse_byte;
            
            // Construct the mouse packet
            mouse_packet_t packet = {0};
            
            // Button states
            packet.button_state = mouse_state.packet_bytes[0] & 0x07;
            
            // X movement (sign extended)
            packet.x_movement = mouse_state.packet_bytes[1];
            if (mouse_state.packet_bytes[0] & 0x10) {
                // Negative x movement
                packet.x_movement |= 0xFFFFFF00;
            }
            
            // Y movement (sign extended and inverted due to screen coordinates)
            packet.y_movement = -(int8_t)mouse_state.packet_bytes[2];
            if (mouse_state.packet_bytes[0] & 0x20) {
                // Negative y movement
                packet.y_movement |= 0xFFFFFF00;
            }
            
            // Calculate next write index
            uint16_t next_write = (mouse_state.write_index + 1) % MOUSE_BUFFER_SIZE;
            
            // Only add if buffer is not full
            if (next_write != mouse_state.read_index) {
                mouse_state.buffer[mouse_state.write_index] = packet;
                mouse_state.write_index = next_write;
            }
            
            // Call registered callbacks
            for (int i = 0; i < mouse_state.callback_count; i++) {
                if (mouse_state.callbacks[i]) {
                    mouse_state.callbacks[i](&packet);
                }
            }
            
            // Reset for next packet
            mouse_state.packet_stage = 0;
            mouse_state.have_first_byte = false;
            break;
        }
    }
    
    return true;
}

// Check if a mouse event is available
bool mouse_has_event(void) {
    return mouse_state.read_index != mouse_state.write_index;
}

// Read a mouse event from the buffer
mouse_packet_t mouse_read_event(void) {
    // If no event available, return empty packet
    if (!mouse_has_event()) {
        return (mouse_packet_t){0};
    }
    
    // Read the next event
    mouse_packet_t event = mouse_state.buffer[mouse_state.read_index];
    
    // Update read index
    mouse_state.read_index = (mouse_state.read_index + 1) % MOUSE_BUFFER_SIZE;
    
    return event;
}

// Register a mouse event callback
bool mouse_register_callback(mouse_callback_t callback) {
    if (!callback) {
        return false;
    }
    
    // Find an empty slot
    for (int i = 0; i < 8; i++) {
        if (mouse_state.callbacks[i] == NULL) {
            mouse_state.callbacks[i] = callback;
            mouse_state.callback_count++;
            return true;
        }
    }
    
    return false;
}

// Unregister a mouse event callback
bool mouse_unregister_callback(mouse_callback_t callback) {
    for (int i = 0; i < 8; i++) {
        if (mouse_state.callbacks[i] == callback) {
            mouse_state.callbacks[i] = NULL;
            mouse_state.callback_count--;
            return true;
        }
    }
    
    return false;
}