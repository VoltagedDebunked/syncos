#ifndef _SYNCOS_KEYBOARD_H
#define _SYNCOS_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

// Keyboard driver initialization function
void keyboard_init(void);

// Check if a key is available in the keyboard buffer
bool keyboard_has_key(void);

// Read a key from the keyboard buffer
uint8_t keyboard_read_key(void);

// Keyboard event handler types
typedef enum {
    KEY_PRESS,
    KEY_RELEASE
} keyboard_event_type_t;

// Keyboard event structure
typedef struct {
    uint8_t scancode;        // Raw scancode
    keyboard_event_type_t type;  // Event type (press/release)
    bool is_special;         // Is this a special key?
    bool is_modifier;        // Is this a modifier key (shift, ctrl, etc.)?
} keyboard_event_t;

// Keyboard callback function type
typedef void (*keyboard_callback_t)(keyboard_event_t *event);

// Register a keyboard event callback
bool keyboard_register_callback(keyboard_callback_t callback);

// Unregister a keyboard event callback
bool keyboard_unregister_callback(keyboard_callback_t callback);

// Convert scancode to ASCII character
int keyboard_scancode_to_ascii(uint8_t scancode, bool shift_pressed);

#endif // _SYNCOS_KEYBOARD_H