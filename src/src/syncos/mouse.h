#ifndef _SYNCOS_MOUSE_H
#define _SYNCOS_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

// PS/2 Mouse I/O Ports
#define MOUSE_DATA_PORT    0x60
#define MOUSE_STATUS_PORT  0x64
#define MOUSE_COMMAND_PORT 0x64

// Mouse packet structure
typedef struct {
    int8_t x_movement;     // Signed x-axis movement
    int8_t y_movement;     // Signed y-axis movement
    uint8_t button_state;  // Button state
} mouse_packet_t;

// Mouse button bit masks
#define MOUSE_LEFT_BUTTON    0x01
#define MOUSE_RIGHT_BUTTON   0x02
#define MOUSE_MIDDLE_BUTTON  0x04

// Mouse callback function type
typedef void (*mouse_callback_t)(mouse_packet_t *packet);

// Public function prototypes
void mouse_init(void);
bool mouse_has_event(void);
mouse_packet_t mouse_read_event(void);
bool mouse_register_callback(mouse_callback_t callback);
bool mouse_unregister_callback(mouse_callback_t callback);
bool mouse_irq_handler(uint8_t irq, void *context);

#endif // _SYNCOS_MOUSE_H