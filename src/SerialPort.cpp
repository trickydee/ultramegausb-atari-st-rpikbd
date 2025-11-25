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
#include "config.h"

#define UART_ID uart1
// The HD6301 in the ST communicates at 7812 baud
#define BAUD_RATE 7812
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE

// We flag the buffer as 'empty' if it has less than this amount of bytes queued. This
// provides a small queue that optimises the serial port performance. Important for
// smooth mouse handling.
#define BUFFER_SIZE 8

// Cached UART hardware pointer - set at initialization, used in critical path
// This avoids calling uart_get_hw() which might access flash
static uart_hw_t* g_uart_hw = nullptr;

SerialPort::~SerialPort() {
    close();
}

SerialPort& SerialPort::instance() {
    static SerialPort serial;
    return serial;
}

void SerialPort::open() {
    uart_init(UART_DEVICE, 2400);
    gpio_set_function(UART_TX, GPIO_FUNC_UART);
    gpio_set_function(UART_RX, GPIO_FUNC_UART);
    int actual = uart_set_baudrate(UART_ID, BAUD_RATE);

    // No hardware flow control
    uart_set_hw_flow(UART_ID, false, false);

    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);

    // Enable UART FIFO to buffer incoming bytes and prevent data loss
    // The RP2040 UART has 32-byte FIFOs which help with timing tolerance
    uart_set_fifo_enabled(UART_ID, true);
    
    // Set RX FIFO watermark to trigger earlier (1/8 full = 4 bytes)
    // This gives us more time to read bytes before FIFO fills up
    // Default is 1/2 (16 bytes) - we use 1/8 for ultra-low latency
    hw_write_masked(&uart_get_hw(UART_ID)->ifls,
                    0 << UART_UARTIFLS_RXIFLSEL_LSB,  // 1/8 full (4 bytes)
                    UART_UARTIFLS_RXIFLSEL_BITS);
    
    // Cache the UART hardware pointer for use in critical path (RAM functions)
    // This avoids calling uart_get_hw() which might access flash
    g_uart_hw = uart_get_hw(UART_ID);
    
    // Verify UART is enabled for transmission
    // UARTEN (bit 0) and TXE (bit 8) must be set in UARTCR register
    if (g_uart_hw) {
        uint32_t cr = g_uart_hw->cr;
        if (!(cr & UART_UARTCR_UARTEN_BITS)) {
            // UART not enabled - this shouldn't happen if uart_init() worked
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
}

void SerialPort::send(const unsigned char data) {
    uart_putc(UART_ID, data);
    if (ui) {
        ui->serial(true, data);
    }
}

bool SerialPort::recv(unsigned char& data) const {
    if (uart_is_readable(UART_ID)) {
        // Read data register which includes error flags
        uint32_t dr = uart_get_hw(UART_ID)->dr;
        
        // Check for UART hardware errors
        if (dr & (UART_UARTDR_OE_BITS | UART_UARTDR_BE_BITS | UART_UARTDR_PE_BITS | UART_UARTDR_FE_BITS)) {
            static int hw_error_count = 0;
            if ((++hw_error_count % 100) == 1) {
                printf("UART HW ERROR: ");
                if (dr & UART_UARTDR_OE_BITS) printf("OVERRUN ");
                if (dr & UART_UARTDR_BE_BITS) printf("BREAK ");
                if (dr & UART_UARTDR_PE_BITS) printf("PARITY ");
                if (dr & UART_UARTDR_FE_BITS) printf("FRAMING ");
                printf("(count: %d)\n", hw_error_count);
            }
        }
        
        data = dr & 0xFF;  // Extract actual data byte
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
