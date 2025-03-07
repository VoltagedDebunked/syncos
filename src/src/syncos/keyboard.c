#include <syncos/keyboard.h>
#include <syncos/irq.h>
#include <syncos/pic.h>
#include <kstd/io.h>
#include <kstd/stdio.h>

// Keyboard I/O Ports
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64

// Keyboard buffer size
#define KEYBOARD_BUFFER_SIZE  256

// Modifier key flags
#define MODIFIER_SHIFT    0x01
#define MODIFIER_CTRL     0x02
#define MODIFIER_ALT      0x04
#define MODIFIER_CAPSLOCK 0x08

// Keyboard state
typedef struct {
    uint8_t buffer[KEYBOARD_BUFFER_SIZE];
    volatile uint16_t read_index;
    volatile uint16_t write_index;
    volatile uint8_t modifiers;
    
    // Callback system
    keyboard_callback_t callbacks[8];
    int callback_count;
} keyboard_state_t;

// Static keyboard state
static keyboard_state_t keyboard_state = {0};

// Scancode to ASCII mapping (no shift)
static const char scancode_to_ascii_normal[] = {
    0,   // 0x00
    27,  // ESC
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',  // 0x01-0x0E
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',  // 0x0F-0x1C
    0,   // Left Ctrl
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',  // 0x1D-0x28
    0,   // Left Shift
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',  // 0x29-0x35
    0,   // Right Shift
    '*', // Keypad *
    0,   // Left Alt
    ' ', // Space
    0,   // CapsLock
    // Function keys, etc. would follow
};

// Scancode to ASCII mapping (with shift)
static const char scancode_to_ascii_shift[] = {
    0,   // 0x00
    27,  // ESC
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',  // 0x01-0x0E
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',  // 0x0F-0x1C
    0,   // Left Ctrl
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',  // 0x1D-0x28
    0,   // Left Shift
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',  // 0x29-0x35
    0,   // Right Shift
    '*', // Keypad *
    0,   // Left Alt
    ' ', // Space
    0,   // CapsLock
    // Function keys, etc. would follow
};

// Forward declarations
static bool keyboard_irq_handler(uint8_t irq, void *context);

// Convert scancode to ASCII
int keyboard_scancode_to_ascii(uint8_t scancode, bool shift_pressed) {
    // Ignore extended scancodes and release events (high bit set)
    if (scancode & 0x80 || scancode >= sizeof(scancode_to_ascii_normal)) {
        return -1;
    }
    
    // Select appropriate mapping based on shift state
    const char *mapping = shift_pressed ? scancode_to_ascii_shift : scancode_to_ascii_normal;
    
    return mapping[scancode];
}

// Keyboard IRQ handler
static bool keyboard_irq_handler(uint8_t irq, void *context) {
    // Read the scancode
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Create keyboard event
    keyboard_event_t event = {
        .scancode = scancode & 0x7F,  // Clear release bit
        .type = (scancode & 0x80) ? KEY_RELEASE : KEY_PRESS,
        .is_special = false,
        .is_modifier = false
    };
    
    // Check for modifier keys
    switch (event.scancode) {
        case 0x2A:  // Left Shift
        case 0x36:  // Right Shift
            event.is_modifier = true;
            if (event.type == KEY_PRESS) {
                keyboard_state.modifiers |= MODIFIER_SHIFT;
            } else {
                keyboard_state.modifiers &= ~MODIFIER_SHIFT;
            }
            break;
        
        case 0x1D:  // Left Ctrl
            event.is_modifier = true;
            if (event.type == KEY_PRESS) {
                keyboard_state.modifiers |= MODIFIER_CTRL;
            } else {
                keyboard_state.modifiers &= ~MODIFIER_CTRL;
            }
            break;
        
        case 0x38:  // Left Alt
            event.is_modifier = true;
            if (event.type == KEY_PRESS) {
                keyboard_state.modifiers |= MODIFIER_ALT;
            } else {
                keyboard_state.modifiers &= ~MODIFIER_ALT;
            }
            break;
    }
    
    // If this is a key press of a printable key, add to buffer
    if (event.type == KEY_PRESS && !event.is_modifier) {
        // Convert scancode to ASCII, respecting shift state
        int ascii = keyboard_scancode_to_ascii(event.scancode, 
            keyboard_state.modifiers & MODIFIER_SHIFT);
        
        if (ascii > 0) {
            // Calculate next write index
            uint16_t next_write = (keyboard_state.write_index + 1) % KEYBOARD_BUFFER_SIZE;
            
            // Only add if buffer is not full
            if (next_write != keyboard_state.read_index) {
                keyboard_state.buffer[keyboard_state.write_index] = (uint8_t)ascii;
                keyboard_state.write_index = next_write;
            }
        }
    }
    
    // Call registered callbacks
    for (int i = 0; i < keyboard_state.callback_count; i++) {
        if (keyboard_state.callbacks[i]) {
            keyboard_state.callbacks[i](&event);
        }
    }
    
    return true;
}

// Check if a key is available
bool keyboard_has_key(void) {
    return keyboard_state.read_index != keyboard_state.write_index;
}

// Read a key from the buffer
uint8_t keyboard_read_key(void) {
    // If no key available, return 0
    if (!keyboard_has_key()) {
        return 0;
    }
    
    // Read the key and update read index
    uint8_t key = keyboard_state.buffer[keyboard_state.read_index];
    keyboard_state.read_index = (keyboard_state.read_index + 1) % KEYBOARD_BUFFER_SIZE;
    
    return key;
}

// Register a keyboard event callback
bool keyboard_register_callback(keyboard_callback_t callback) {
    if (!callback) {
        return false;
    }
    
    // Find an empty slot
    for (int i = 0; i < 8; i++) {
        if (keyboard_state.callbacks[i] == NULL) {
            keyboard_state.callbacks[i] = callback;
            keyboard_state.callback_count++;
            return true;
        }
    }
    
    return false;
}

// Unregister a keyboard event callback
bool keyboard_unregister_callback(keyboard_callback_t callback) {
    for (int i = 0; i < 8; i++) {
        if (keyboard_state.callbacks[i] == callback) {
            keyboard_state.callbacks[i] = NULL;
            keyboard_state.callback_count--;
            return true;
        }
    }
    
    return false;
}

// Initialize the keyboard driver
void keyboard_init(void) {
    // Clear keyboard state
    keyboard_state.read_index = 0;
    keyboard_state.write_index = 0;
    keyboard_state.modifiers = 0;
    keyboard_state.callback_count = 0;
    
    // Register IRQ handler for keyboard (IRQ 1)
    if (!irq_register_handler(IRQ_KEYBOARD, keyboard_irq_handler, NULL)) {
        printf("Keyboard: Failed to register IRQ handler\n");
        return;
    }
    
    printf("Keyboard: Driver initialized\n");
}