/*
 * Atari ST RP2040 IKBD Emulator
 * Copyright (C) 2021 Roy Hopkins
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include "SerialPort.h"
#include "pico.h"  // Includes all platform definitions including __not_in_flash_func
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "config.h"

#define UART_ID uart1
#define UART_IRQ UART1_IRQ
// The HD6301 in the ST communicates at 7812 baud
#define BAUD_RATE 7812
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

// IRQ-based ring buffer for received data (matching logronoid's implementation)
#define RX_BUFFER_SIZE 256
static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

// Cached UART hardware pointer - set at initialization, used in critical path
// This avoids calling uart_get_hw() which might access flash
static uart_hw_t* g_uart_hw = nullptr;

// Put byte into ring buffer (called from ISR)
static inline void rx_buffer_put(uint8_t data) {
    uint16_t next_head = (rx_head + 1) & 0xFF;
    if (next_head != rx_tail) {  // Buffer not full
        rx_buffer[rx_head] = data;
        rx_head = next_head;
    }
    // If buffer is full, byte is silently dropped (matches logronoid's behavior)
}

// Get byte from ring buffer (called from main loop)
static bool rx_buffer_get(uint8_t* data) {
    if (rx_head == rx_tail) {
        return false;  // Buffer empty
    }
    *data = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) & 0xFF;
    return true;
}

// Get number of bytes available in buffer
static uint16_t rx_buffer_available(void) {
    return (rx_head - rx_tail) & 0xFF;
}

// ISR for UART receive (IRQ handler)
static void on_uart_irq(void) {
    // Check if this is an RX interrupt
    if (g_uart_hw && (g_uart_hw->mis & UART_UARTMIS_RXMIS_BITS)) {
        // Clear RX interrupt
        g_uart_hw->icr = UART_UARTICR_RXIC_BITS;
        
        // Read all available data from UART and put into ring buffer
        while (uart_is_readable(UART_ID)) {
            uint8_t ch = uart_getc(UART_ID);
            rx_buffer_put(ch);
        }
    }
}

SerialPort::~SerialPort() {
    close();
}

SerialPort& SerialPort::instance() {
    static SerialPort serial;
    return serial;
}

void SerialPort::open() {
    // Initialize UART
    uart_init(UART_DEVICE, BAUD_RATE);
    gpio_set_function(UART_TX, GPIO_FUNC_UART);
    gpio_set_function(UART_RX, GPIO_FUNC_UART);
    
    // Set pull-up on RX line (matching logronoid's code)
    gpio_pull_up(UART_RX);
    gpio_disable_pulls(UART_TX);
    
    // Set drive strength for TX
    gpio_set_drive_strength(UART_TX, GPIO_DRIVE_STRENGTH_12MA);
    
    int actual = uart_set_baudrate(UART_ID, BAUD_RATE);
    printf("Serial port opened at %d baud (target: %d)\n", actual, BAUD_RATE);

    // No hardware flow control
    uart_set_hw_flow(UART_ID, false, false);

    // Data format: data bits, stop bits, parity
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    
    // Use raw mode (no CRLF translation)
    uart_set_translate_crlf(UART_ID, false);

    // Disable FIFO (matching logronoid's implementation)
    // IRQ-based approach doesn't need FIFO - bytes are captured immediately
    uart_set_fifo_enabled(UART_ID, false);
    
    // Cache the UART hardware pointer for use in critical path (RAM functions)
    // This avoids calling uart_get_hw() which might access flash
    g_uart_hw = uart_get_hw(UART_ID);
    
    // Reset ring buffer
    rx_head = 0;
    rx_tail = 0;
    
    // Set up interrupt handler for UART RX
    irq_set_exclusive_handler(UART_IRQ, on_uart_irq);
    irq_set_priority(UART_IRQ, 0);  // 0 = highest priority
    
    // Enable UART RX interrupt (but not TX)
    uart_set_irq_enables(UART_ID, true, false);
    
    // Enable the IRQ at NVIC level
    irq_set_enabled(UART_IRQ, true);
    
    printf("UART IRQ enabled - RX interrupt handler active\n");
    
    // Verify UART is enabled for transmission
    if (g_uart_hw) {
        uint32_t cr = g_uart_hw->cr;
        if (!(cr & UART_UARTCR_UARTEN_BITS)) {
            printf("WARNING: UART not enabled! CR=0x%08lx\n", cr);
        }
        if (!(cr & UART_UARTCR_TXE_BITS)) {
            // TX not enabled - enable it using direct register write
            g_uart_hw->cr |= UART_UARTCR_TXE_BITS;
        }
    }
}

void SerialPort::set_ui(UserInterface& ui) {
    this->ui = &ui;
}

void SerialPort::close() {
    // Disable interrupts
    irq_set_enabled(UART_IRQ, false);
    uart_set_irq_enables(UART_ID, false, false);
    uart_deinit(UART_ID);
}

void SerialPort::send(const unsigned char data) {
    uart_putc(UART_ID, data);
    if (ui) {
        ui->serial(true, data);
    }
}

bool SerialPort::recv(unsigned char& data) const {
    // Read from IRQ-based ring buffer instead of polling UART hardware
    // This is much more efficient - bytes are captured immediately by ISR
    if (rx_buffer_get(&data)) {
        if (ui) {
            ui->serial(false, data);
        }
        return true;
    }
    return false;
}

void SerialPort::configure() {
}

bool SerialPort::send_buf_empty() const {
    return uart_is_writable(UART_ID);
}

uint16_t SerialPort::rx_available() const {
    return rx_buffer_available();
}

// C wrapper functions marked for RAM - called from 6301 emulator
// These bypass the C++ SerialPort class AND Pico SDK functions to avoid flash latency
// Using direct hardware register access for maximum speed
extern "C" {
void __not_in_flash_func(serial_send)(unsigned char data) {
    // Direct hardware register access - CRITICAL: bypasses all SDK functions in flash
    // Use cached hardware pointer to avoid any flash access
    if (!g_uart_hw) {
        // Fallback if not initialized (shouldn't happen, but be safe)
        return;
    }
    
    // Verify UART is enabled before sending
    if (!(g_uart_hw->cr & UART_UARTCR_UARTEN_BITS)) {
        // UART not enabled - can't send
        return;
    }
    if (!(g_uart_hw->cr & UART_UARTCR_TXE_BITS)) {
        // TX not enabled - enable it
        g_uart_hw->cr |= UART_UARTCR_TXE_BITS;
    }
    
    // Wait for TX FIFO to have space (should be very fast, usually immediate)
    // TXFF bit is 1 when FIFO is full, 0 when there's space
    // BUSY bit is 1 when UART is transmitting, but we can still write to FIFO
    while (g_uart_hw->fr & UART_UARTFR_TXFF_BITS) {
        // FIFO full, wait (should be rare with 32-byte FIFO)
        // This is a tight loop in RAM, so it's fast
    }
    
    // Write byte to data register - this starts transmission
    // The UART hardware will automatically transmit when data is written to DR
    g_uart_hw->dr = data;
    
    // Note: UI update removed from critical path - can be done elsewhere if needed
    // The byte is now in the UART FIFO and will be transmitted automatically
}

int __not_in_flash_func(serial_send_buf_empty)(void) {
    // Direct hardware register access - CRITICAL: bypasses all SDK functions in flash
    // Check UARTFR register TXFF (Transmit FIFO Full) bit - if not set, we can write
    // Use cached hardware pointer to avoid any flash access
    if (!g_uart_hw) {
        return 0;  // Not initialized, assume not ready
    }
    return (g_uart_hw->fr & UART_UARTFR_TXFF_BITS) ? 0 : 1;
}
}
