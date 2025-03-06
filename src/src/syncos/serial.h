#ifndef SYNCOS_SERIAL_H
#define SYNCOS_SERIAL_H

#include <stdint.h>
#include <stdbool.h>

// Standard COM port addresses
#define COM1_PORT 0x3F8
#define COM2_PORT 0x2F8
#define COM3_PORT 0x3E8
#define COM4_PORT 0x2E8

// COM port register offsets
#define SERIAL_DATA_REG           0 // R/W
#define SERIAL_INT_ENABLE_REG     1 // W
#define SERIAL_INT_ID_FIFO_REG    2 // R/W
#define SERIAL_LINE_CONTROL_REG   3 // R/W
#define SERIAL_MODEM_CONTROL_REG  4 // R/W
#define SERIAL_LINE_STATUS_REG    5 // R
#define SERIAL_MODEM_STATUS_REG   6 // R
#define SERIAL_SCRATCH_REG        7 // R/W

// Line status register bits
#define SERIAL_LSR_DATA_READY        0x01
#define SERIAL_LSR_OVERRUN_ERROR     0x02
#define SERIAL_LSR_PARITY_ERROR      0x04
#define SERIAL_LSR_FRAMING_ERROR     0x08
#define SERIAL_LSR_BREAK_INDICATOR   0x10
#define SERIAL_LSR_TX_HOLDING_EMPTY  0x20
#define SERIAL_LSR_TX_EMPTY          0x40
#define SERIAL_LSR_FIFO_ERROR        0x80

// Line control register bits
#define SERIAL_LCR_5BITS            0x00
#define SERIAL_LCR_6BITS            0x01
#define SERIAL_LCR_7BITS            0x02
#define SERIAL_LCR_8BITS            0x03
#define SERIAL_LCR_STOP_BITS        0x04 // 0 = 1 stop bit, 1 = 1.5 or 2 stop bits
#define SERIAL_LCR_PARITY_NONE      0x00
#define SERIAL_LCR_PARITY_ODD       0x08
#define SERIAL_LCR_PARITY_EVEN      0x18
#define SERIAL_LCR_PARITY_MARK      0x28
#define SERIAL_LCR_PARITY_SPACE     0x38
#define SERIAL_LCR_BREAK_ENABLE     0x40
#define SERIAL_LCR_DLAB             0x80 // Divisor Latch Access Bit

// FIFO control register bits
#define SERIAL_FCR_ENABLE_FIFO      0x01
#define SERIAL_FCR_CLEAR_RX_FIFO    0x02
#define SERIAL_FCR_CLEAR_TX_FIFO    0x04
#define SERIAL_FCR_DMA_MODE_SELECT  0x08
#define SERIAL_FCR_ENABLE_64_BYTE   0x20 // 16750 only
#define SERIAL_FCR_INT_TRIGGER_1    0x00
#define SERIAL_FCR_INT_TRIGGER_4    0x40
#define SERIAL_FCR_INT_TRIGGER_8    0x80
#define SERIAL_FCR_INT_TRIGGER_14   0xC0

// Modem control register bits
#define SERIAL_MCR_DTR              0x01
#define SERIAL_MCR_RTS              0x02
#define SERIAL_MCR_OUT1             0x04
#define SERIAL_MCR_OUT2             0x08
#define SERIAL_MCR_LOOPBACK         0x10
#define SERIAL_MCR_AUTOFLOW_CTRL    0x20 // 16750 only

// Baud rate divisors (115200 / desired_baud)
#define SERIAL_BAUD_115200          1
#define SERIAL_BAUD_57600           2
#define SERIAL_BAUD_38400           3
#define SERIAL_BAUD_19200           6
#define SERIAL_BAUD_9600            12
#define SERIAL_BAUD_4800            24
#define SERIAL_BAUD_2400            48
#define SERIAL_BAUD_1200            96

// Function prototypes
void serial_init(uint16_t port, uint16_t baud_divisor);
bool serial_is_transmit_empty(uint16_t port);
void serial_write_char(uint16_t port, char c);
void serial_write_string(uint16_t port, const char* str);
void serial_write_bytes(uint16_t port, const uint8_t* data, uint32_t length);
bool serial_has_data(uint16_t port);
char serial_read_char(uint16_t port);
void serial_read_bytes(uint16_t port, uint8_t* buffer, uint32_t length);

// Standard I/O functions
void serial_init_stdio(void);
int serial_stdin_getchar(void);
int serial_stdout_putchar(int c);
int serial_stderr_putchar(int c);

// Printf-like functions
void serial_printf(uint16_t port, const char* format, ...);
void kprintf(const char* format, ...);
void dprintf(const char* format, ...);

#endif // SYNCOS_SERIAL_H