/*
 * Atari ST RP2040 IKBD Emulator
 * Copyright (C) 2021 Roy Hopkins
 * 
 * This file has been copied and modified from the Steem SSE project.
 * The original copyright notice has been retained below this one.
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

/*---------------------------------------------------------------------------
PROJECT: Steem SSE
Atari ST emulator
Copyright (C) 2021 by Anthony Hayward and Russel Hayward + SSE

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see https://www.gnu.org/licenses/.

DOMAIN: Emu
FILE: 6301.c
DESCRIPTION: Declarations to interface Steem SSE with a modified version of
sim68xx (Copyright (c) 1994-1996 Arne Riiber) to emulate at a lower level
the HD6301V1 chip that was in the ST, as described in 'HD6301V1 on ST.txt'.
This emulation was supposed to run as terminal on a DOS PC. A precise
integration with another serial device (MC6850 ACIA in the ST) wasn't the 
focus.
The functions in original files have been modified where necessary, some
have been added, in 6301.c, and in ireg.c (most interesting ones). 
This is C, not C++. 6301.c includes other C files.
All functions and variables beginning with hd6301 are declared in this
6301 module and are meant to be visible to Steem (interface) by way of
inclusion of this file.
---------------------------------------------------------------------------*/

#pragma once
#ifndef HD6301_H
#define HD6301_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

unsigned int _rotr(unsigned int Data, unsigned int Bits);
unsigned int _rotl(unsigned int Data, unsigned int Bits);

typedef int64_t COUNTER_VAR;
typedef unsigned char BYTE;
typedef unsigned short WORD;

// variables that Steem must see
extern COUNTER_VAR cycles_run; 

// functions used by Steem
BYTE* hd6301_init();
int hd6301_destroy(); // like a C++ destructor
int hd6301_reset(int Cold); 
void hd6301_trigger_reset(); // Trigger reset from external source (e.g. keyboard)
void hd6301_run_clocks(COUNTER_VAR clocks);
int hd6301_receive_byte(u_char byte_in); // just passing through
void hd6301_tx_empty(int empty);
int hd6301_sci_busy();
uint16_t hd6301_get_pc(void);

#define HD6301_DIAG_TRACE_SIZE 16
#define HD6301_DIAG_RX_BLOCKED 2
#define HD6301_DIAG_UPLOAD 3
typedef struct {
    uint16_t pc, x, sp, d;
    uint8_t opcode, ccr, trcsr;
} hd6301_instruction_trace_t;

// First fault or blocked-RX capture is immutable; memory-fault count continues increasing.
typedef struct {
    // write: 0=read fault, 1=write fault, HD6301_DIAG_RX_BLOCKED=state capture.
    uint32_t address, value, write, instruction_pc, opcode, pc, x, sp, d, ccr;
    uint32_t trcsr, subroutine;
    uint8_t code[3];
    uint8_t controller_ram[128];
    uint32_t consumed_total;
    uint8_t consumed[512];
    uint32_t trace_count;
    hd6301_instruction_trace_t trace[HD6301_DIAG_TRACE_SIZE];
} hd6301_memory_fault_t;
int hd6301_memory_fault_snapshot(hd6301_memory_fault_t *out, uint32_t *count);
int hd6301_memory_fault_pending(void);
void hd6301_record_memory_fault(uint32_t address, uint32_t value, uint32_t write);
void hd6301_request_rx_blocked_capture(void);
void hd6301_request_upload_capture(void);
void hd6301_diag_lifecycle(uint32_t *resets, uint32_t *reset_pc,
                           uint32_t *ram_entries, uint32_t *rom_returns);


#define MOUSE_MASK 0x33333333 // 20bit on real HW?

extern unsigned int mouse_x_counter;
extern unsigned int mouse_y_counter;

extern int crashed;

#define USE_PROTOTYPES 

#ifdef __cplusplus
}                       // extern "C" {
#endif

#endif // HD6301_H
