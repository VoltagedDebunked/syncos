#include <syncos/serial.h>
#include <stdarg.h>
#include <kstd/io.h>
#include <kstd/string.h>
#include <kstd/stdio.h>

// Default serial port for standard I/O
static uint16_t stdio_port = COM1_PORT;

// Buffer for printf functions
#define PRINTF_BUFFER_SIZE 1024
static char printf_buffer[PRINTF_BUFFER_SIZE];

/**
 * Initialize a serial port with the specified parameters
 */
void serial_init(uint16_t port, uint16_t baud_divisor) {
    // Disable interrupts
    outb(port + SERIAL_INT_ENABLE_REG, 0x00);
    
    // Set DLAB to access baud rate divisor
    outb(port + SERIAL_LINE_CONTROL_REG, SERIAL_LCR_DLAB);
    
    // Set baud rate divisor
    outb(port + SERIAL_DATA_REG, baud_divisor & 0xFF);              // Low byte
    outb(port + SERIAL_DATA_REG + 1, (baud_divisor >> 8) & 0xFF);   // High byte
    
    // 8 bits, no parity, one stop bit
    outb(port + SERIAL_LINE_CONTROL_REG, SERIAL_LCR_8BITS);
    
    // Enable and clear FIFOs, set interrupt trigger level
    outb(port + SERIAL_INT_ID_FIFO_REG, 
         SERIAL_FCR_ENABLE_FIFO | SERIAL_FCR_CLEAR_RX_FIFO | 
         SERIAL_FCR_CLEAR_TX_FIFO | SERIAL_FCR_INT_TRIGGER_14);
    
    // Enable DTR, RTS, and OUT2 (required for interrupts)
    outb(port + SERIAL_MODEM_CONTROL_REG, SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT2);
}

/**
 * Check if the transmit buffer is empty
 */
bool serial_is_transmit_empty(uint16_t port) {
    return (inb(port + SERIAL_LINE_STATUS_REG) & SERIAL_LSR_TX_HOLDING_EMPTY) != 0;
}

/**
 * Write a character to the serial port
 */
void serial_write_char(uint16_t port, char c) {
    // Wait for the transmit buffer to be empty
    while (!serial_is_transmit_empty(port)) {
        // CPU pause to save power and reduce contention
        __asm__ volatile("pause");
    }
    
    // Handle newline by sending CR+LF for terminal compatibility
    if (c == '\n') {
        outb(port + SERIAL_DATA_REG, '\r');
        // Wait for the transmit buffer to be empty again
        while (!serial_is_transmit_empty(port)) {
            __asm__ volatile("pause");
        }
    }
    
    // Send the character
    outb(port + SERIAL_DATA_REG, c);
}

/**
 * Write a null-terminated string to the serial port
 */
void serial_write_string(uint16_t port, const char* str) {
    if (!str) {
        return;
    }
    
    while (*str) {
        serial_write_char(port, *str++);
    }
}

/**
 * Write raw bytes to the serial port
 */
void serial_write_bytes(uint16_t port, const uint8_t* data, uint32_t length) {
    if (!data) {
        return;
    }
    
    for (uint32_t i = 0; i < length; i++) {
        // Wait for the transmit buffer to be empty
        while (!serial_is_transmit_empty(port)) {
            __asm__ volatile("pause");
        }
        
        // Send the byte
        outb(port + SERIAL_DATA_REG, data[i]);
    }
}

/**
 * Check if there is data available to read
 */
bool serial_has_data(uint16_t port) {
    return (inb(port + SERIAL_LINE_STATUS_REG) & SERIAL_LSR_DATA_READY) != 0;
}

/**
 * Read a character from the serial port (blocking)
 */
char serial_read_char(uint16_t port) {
    // Wait for data to be available
    while (!serial_has_data(port)) {
        // CPU pause to save power and reduce contention
        __asm__ volatile("pause");
    }
    
    // Read and return the character
    return inb(port + SERIAL_DATA_REG);
}

/**
 * Read multiple bytes from the serial port (blocking)
 */
void serial_read_bytes(uint16_t port, uint8_t* buffer, uint32_t length) {
    if (!buffer) {
        return;
    }
    
    for (uint32_t i = 0; i < length; i++) {
        buffer[i] = serial_read_char(port);
    }
}

/**
 * Initialize all standard I/O to use serial port
 */
void serial_init_stdio(void) {
    // Initialize COM1 with 115200 baud
    serial_init(stdio_port, SERIAL_BAUD_115200);
    
    // Register the serial functions as stdio handlers
    stdio_set_stdin_getchar(serial_stdin_getchar);
    stdio_set_stdout_putchar(serial_stdout_putchar);
    stdio_set_stderr_putchar(serial_stderr_putchar);
    
    // Write a startup message
    serial_write_string(stdio_port, "Serial I/O initialized on COM1 (115200,8,N,1)\n");
}

/**
 * Standard input character read function (blocking)
 */
int serial_stdin_getchar(void) {
    return (int)serial_read_char(stdio_port);
}

/**
 * Standard output character write function
 */
int serial_stdout_putchar(int c) {
    serial_write_char(stdio_port, (char)c);
    return c;
}

/**
 * Standard error character write function
 */
int serial_stderr_putchar(int c) {
    // Use the same port but could be changed to a different port if needed
    serial_write_char(stdio_port, (char)c);
    return c;
}

/**
 * Format and print a string to the specified serial port
 */
void serial_printf(uint16_t port, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    int result = vsnprintf(printf_buffer, PRINTF_BUFFER_SIZE, format, args);
    
    va_end(args);
    
    if (result > 0) {
        // Ensure null-termination
        printf_buffer[PRINTF_BUFFER_SIZE - 1] = '\0';
        serial_write_string(port, printf_buffer);
    }
}

/**
 * Format and print a string to the kernel's standard output
 */
void kprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    int result = vsnprintf(printf_buffer, PRINTF_BUFFER_SIZE, format, args);
    
    va_end(args);
    
    if (result > 0) {
        // Ensure null-termination
        printf_buffer[PRINTF_BUFFER_SIZE - 1] = '\0';
        serial_write_string(stdio_port, printf_buffer);
    }
}

/**
 * Format and print a debug message (can be conditionally compiled out)
 */
void dprintf(const char* format, ...) {
    #ifdef DEBUG
    va_list args;
    va_start(args, format);
    
    int result = vsnprintf(printf_buffer, PRINTF_BUFFER_SIZE, format, args);
    
    va_end(args);
    
    if (result > 0) {
        // Ensure null-termination
        printf_buffer[PRINTF_BUFFER_SIZE - 1] = '\0';
        
        // Add a "DEBUG: " prefix
        serial_write_string(stdio_port, "DEBUG: ");
        serial_write_string(stdio_port, printf_buffer);
    }
    #endif
}