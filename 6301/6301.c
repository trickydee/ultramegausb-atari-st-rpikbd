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
DESCRIPTION: Code to interface Steem SSE with a modified version of
sim68xx (Copyright (c) 1994-1996 Arne Riiber) to emulate at a lower level
the HD6301V1 chip that was in the ST, as described in 'HD6301V1 on ST.txt'.
This emulation was supposed to run as terminal on a DOS PC. A precise
integration with another serial device (MC6850 ACIA in the ST) wasn't the 
focus.
The functions in original files have been modified where necessary, some
have been added, in 6301.c, and in ireg.c (most interesting ones). 
This is C, not C++. 6301.c includes other C files.
ASSERT will only work properly in the Visual Studio IDE (or equivalent).
I went out of my way to have TRACE working just like in the rest of Steem
SSE.
---------------------------------------------------------------------------*/

#include "config.h"
#include "6301.h"

#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"

// variables from Steem (declared as extern "C") there
extern double cpu_cycles_multiplier;

// our variables that Steem must see
COUNTER_VAR cycles_run=0; 

// additional variables for our module
unsigned int mouse_x_counter;
unsigned int mouse_y_counter;
int crashed = 0;

// Debug facilities
#if defined(TRACE_6301)
#define TRACE printf
#else
#define TRACE
#endif

#define ASSERT(x)
#define error printf
#define warning TRACE

#define abs_quick(i) ( (i>=0) ? (i) : -(i))

#if ENABLE_SERIAL_LOGGING
static uint32_t diag_instruction_pc;
static uint32_t diag_instruction_opcode = 0x100; // Interrupt entry / no opcode yet.
static hd6301_memory_fault_t diag_memory_fault;
static uint32_t diag_memory_fault_ready;
static uint32_t diag_memory_fault_count;
static uint32_t diag_rx_capture_requested;
static uint32_t diag_resets, diag_reset_pc, diag_ram_entries, diag_rom_returns;
static int diag_was_ram;
static hd6301_instruction_trace_t diag_trace[HD6301_DIAG_TRACE_SIZE];
static uint32_t diag_trace_head, diag_trace_count;
static uint8_t diag_consumed[512];
static uint32_t diag_consumed_total;
#endif

// base
#include "cpu.c"
#include "fprinthe.c"
#include "memsetl.c"
#include "symtab.c"  
//#include "tty.c"
// arch
// m68xx
#include "memory.c"
#include "opfunc.c" // will include alu.c (arithmetic/logic unit)
#include "reg.c"
// h6301
#include "instr.c"
#include "ireg.c"
#include "optab.c"
#include "sci.c"
#include "timer.c"
#include "callstac.c"

void __not_in_flash_func(hd6301_record_memory_fault)(uint32_t address, uint32_t value, uint32_t write) {
#if ENABLE_SERIAL_LOGGING
  // Single producer (emulator). Publish only after the complete snapshot is ready.
  uint32_t count = __atomic_load_n(&diag_memory_fault_count, __ATOMIC_RELAXED);
  if (write < HD6301_DIAG_RX_BLOCKED && count != UINT32_MAX)
    __atomic_store_n(&diag_memory_fault_count, count + 1, __ATOMIC_RELAXED);
  if (__atomic_load_n(&diag_memory_fault_ready, __ATOMIC_RELAXED)) return;
  diag_memory_fault.address = address;
  diag_memory_fault.value = value;
  diag_memory_fault.write = write;
  diag_memory_fault.instruction_pc = diag_instruction_pc;
  diag_memory_fault.opcode = diag_instruction_opcode;
  diag_memory_fault.pc = reg_getpc();
  diag_memory_fault.x = reg_getix();
  diag_memory_fault.sp = reg_getsp();
  diag_memory_fault.d = reg_getaccd();
  diag_memory_fault.ccr = reg_getccr();
  diag_memory_fault.trcsr = iram[TRCSR];
  diag_memory_fault.subroutine = callstack_peek_addr();
  // Raw instruction bytes without invoking memory-mapped register callbacks.
  for (unsigned i = 0; i < 3; ++i) {
    uint32_t a = (diag_instruction_pc + i) & 0xffff;
    diag_memory_fault.code[i] = !ram ? 0xff :
        a >= 0xf000 ? ram[a - 0xf000 + 256] :
        a >= 0x80 && a < 0x100 ? ram[a] : 0xff;
  }
  for (unsigned i = 0; i < 128; ++i)
    diag_memory_fault.controller_ram[i] = ram ? ram[0x80 + i] : 0xff;
  diag_memory_fault.consumed_total = diag_consumed_total;
  for (unsigned i = 0; i < 512; ++i)
    diag_memory_fault.consumed[i] = diag_consumed[i];
  diag_memory_fault.trace_count = diag_trace_count;
  for (unsigned i = 0; i < diag_trace_count; ++i)
    diag_memory_fault.trace[i] = diag_trace[(diag_trace_head + HD6301_DIAG_TRACE_SIZE - diag_trace_count + i) % HD6301_DIAG_TRACE_SIZE];
  __atomic_store_n(&diag_memory_fault_ready, 1, __ATOMIC_RELEASE);
#endif
}

void hd6301_request_rx_blocked_capture(void) {
#if ENABLE_SERIAL_LOGGING
  __atomic_store_n(&diag_rx_capture_requested, HD6301_DIAG_RX_BLOCKED, __ATOMIC_RELEASE);
#endif
}

void hd6301_request_upload_capture(void) {
#if ENABLE_SERIAL_LOGGING
  __atomic_store_n(&diag_rx_capture_requested, HD6301_DIAG_UPLOAD, __ATOMIC_RELEASE);
#endif
}

void hd6301_diag_lifecycle(uint32_t *resets, uint32_t *reset_pc,
                           uint32_t *ram_entries, uint32_t *rom_returns) {
#if ENABLE_SERIAL_LOGGING
  *resets = __atomic_load_n(&diag_resets, __ATOMIC_RELAXED);
  *reset_pc = __atomic_load_n(&diag_reset_pc, __ATOMIC_RELAXED);
  *ram_entries = __atomic_load_n(&diag_ram_entries, __ATOMIC_RELAXED);
  *rom_returns = __atomic_load_n(&diag_rom_returns, __ATOMIC_RELAXED);
#else
  *resets = *reset_pc = *ram_entries = *rom_returns = 0;
#endif
}

int hd6301_memory_fault_pending(void) {
#if ENABLE_SERIAL_LOGGING
  return __atomic_load_n(&diag_memory_fault_ready, __ATOMIC_ACQUIRE) != 0;
#else
  return 0;
#endif
}

int hd6301_memory_fault_snapshot(hd6301_memory_fault_t *out, uint32_t *count) {
#if ENABLE_SERIAL_LOGGING
  if (!__atomic_load_n(&diag_memory_fault_ready, __ATOMIC_ACQUIRE)) return 0;
  *out = diag_memory_fault;
  *count = __atomic_load_n(&diag_memory_fault_count, __ATOMIC_RELAXED);
  return 1;
#else
  return 0;
#endif
}


// Interface with Steem

// Global flag for external reset trigger (e.g. from keyboard shortcut)
static volatile int pending_reset = 0;

#include "pico/platform.h"

BYTE* __not_in_flash_func(hd6301_init)() {
  return mem_init();
}


int __not_in_flash_func(hd6301_destroy)() {
  TRACE("6301: destroy object\n");
  if(ram) 
    free(ram);
  ram=NULL;
  return 0;
}


int __not_in_flash_func(hd6301_reset)(int Cold) {
#if ENABLE_SERIAL_LOGGING
  __atomic_store_n(&diag_reset_pc, reg_getpc(), __ATOMIC_RELAXED);
  __atomic_fetch_add(&diag_resets, 1, __ATOMIC_RELAXED);
#endif
  TRACE("6301 emu cpu reset (cold %d)\n",Cold);
  crashed = 0;
  sci_reset_receive_status();
  cpu_reset();
  if(Cold)
  {
    WORD rnd=rand()%16;
    memset(ram, 0, 0x80); 
    memset(iram,0,NIREGS);// also all internal registers
    mouse_x_counter=MOUSE_MASK;
    mouse_y_counter=MOUSE_MASK;
    // internal mouse state is random: A239/Jumping Jackson, Droid SE WIP
    mouse_x_counter=_rotl(mouse_x_counter,rnd);
    mouse_y_counter=_rotl(mouse_y_counter,rnd);
    //TRACE("Mouse mask %X\n",mouse_x_counter); // 333... 666 999 CCC
  }
  // Clear serial state to prevent stale data from previous run
  iram[TRCSR]=0x20;  // TDRE=1 (transmit ready), all flags clear
  iram[RDR]=0x00;     // Clear receive data register
  iram[TDR]=0x00;     // Clear transmit data register
  mem_putw (OCR, 0xFFFF);
  TRACE("6301: Serial registers cleared (TRCSR=0x20, RDR=0x00, TDR=0x00)\n");
}

void __not_in_flash_func(hd6301_trigger_reset)() {
  // Called from external source (e.g. keyboard handler) to trigger a reset
  TRACE("6301: Reset triggered externally\n");
  pending_reset = 1;
}

 
void __not_in_flash_func(hd6301_run_clocks)(COUNTER_VAR clocks) {
  clocks *= HD6301_OVERCLOCK_NUM;
  // Called by Steem to run some cycles (per scanline or to update before IO)
  int pc;
  COUNTER_VAR starting_cycles=cpu.ncycles;

  // Check for pending reset request (e.g. from keyboard shortcut)
  if (pending_reset) {
    TRACE("6301: Executing pending reset\n");
    pending_reset = 0;
    hd6301_reset(1);  // Cold reset
    return;  // Don't run cycles this iteration
  }

  // make sure our 6301 is running OK
  if(!cpu_isrunning())
  {
    TRACE("6301 starting cpu\n");
    cpu_start();
  }
  if(iram[TRCSR]&1)
  {
    TRACE("6301 waking up (PC %X cycles %lu)\n",reg_getpc(),cpu.ncycles);
    iram[TRCSR]&=~1;
  }
  pc=reg_getpc();

  while(!crashed && ((cpu.ncycles-starting_cycles) < clocks))
  {
    instr_exec (); // execute one instruction
  }
}

int __not_in_flash_func(hd6301_receive_byte)(u_char byte_in) {
  return sci_in(&byte_in,1);
}

void __not_in_flash_func(hd6301_tx_empty)(int empty) {
  // Once our serial port TX buffer has space then set the register
  // to show the CPU another byte can be sent
  if (empty) {
    iram[TRCSR] |= TDRE;
  }
}

int __not_in_flash_func(hd6301_sci_busy)() {
  return (iram[TRCSR] & RDRF) ? 1 : 0;
}

uint16_t __not_in_flash_func(hd6301_get_pc)(void) {
  return reg_getpc();
}
