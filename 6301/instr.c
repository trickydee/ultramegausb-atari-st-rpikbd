/* <<<                                  */
/* Copyright (c) 1994-1996 Arne Riiber. */
/* All rights reserved.                 */
/* >>>                                  */
#include <assert.h>
#include <stdio.h>

#include "defs.h"
#include "chip.h"
#include "cpu.h"
#include "ireg.h"
#include "memory.h"
#include "optab.h"
#include "reg.h"
#include "sci.h"
#include "timer.h"
#include "pico.h"

#ifdef USE_PROTOTYPES
#include "instr.h"
#endif


/*
 *  reset - jump to the reset vector
 */
reset () 
{
  reg_setpc (mem_getw (RESVECTOR));
  reg_setiflag (1);
}


/*
 * instr_exec - execute an instruction
 */
void __not_in_flash_func(instr_exec)() {

  /*
   * Get opcode from memory,
   * inc program counter to point to first operand,
   * Decode and execute the opcode.
   */
  struct opcode *opptr;
  int interrupted = 0;    /* 1 = HW interrupt occured */
#if ENABLE_SERIAL_LOGGING
  diag_instruction_pc = reg_getpc();
  diag_instruction_opcode = 0x100;
#endif

#ifndef M6800

  if (!reg_getiflag ()) 
  {
    /*
     * Check for interrupts in priority order
     * Optimization: Read registers once and cache the values
     */
    u_char tcsr = ireg_getb (TCSR);
    u_char trcsr = ireg_getb (TRCSR);
    
    if ((tcsr & OCF) && (tcsr & EOCI)) {
      int_addr (OCFVECTOR);
      interrupted = 1;
    } 
    else if (((trcsr & RDRF) && (trcsr & RIE)) || ((trcsr & TDRE) && (trcsr & TIE))) {
      int_addr (SCIVECTOR);
      interrupted = 1;
    }
  }
#if 0
  /*
   * 6301 Address check error: Trap if address is outside RAM/ROM
   * This must be modified to check mode (single-chip/extended) first
   */
  if ((reg_getpc() <= ramstart) || reg_getpc() >  ramend)
  {
    error ("instr_exec: Address error: %04x\n", reg_getpc());
    trap();   /* Highest pri vector after Reset */
  }
#endif /* 0 */
#endif /* M6800 */

  if (interrupted) /* Prepare cycle count for register stacking */
  {
    opptr = &opcodetab[0x3f]; /* SWI */
  }
  else
  {
#ifdef TRACE_6301
    // PC bounds check - only in debug mode for performance
    int pc=reg_getpc (); 
    if(!(pc>=0x80&&pc<0xFFFF)) // eg bad snapshot
    {
      TRACE("pc=%x, 6301 emu is hopelessly crashed!\n",pc);
      crashed = 1;
      return;
    }
#endif

    u_char opcode = mem_getb (reg_getpc ());
#if ENABLE_SERIAL_LOGGING
    diag_instruction_opcode = opcode;
    int in_ram = diag_instruction_pc >= 0x80 && diag_instruction_pc < 0x100;
    if (in_ram && !diag_was_ram)
      __atomic_fetch_add(&diag_ram_entries, 1, __ATOMIC_RELAXED);
    if (diag_was_ram && diag_instruction_pc >= 0xf000)
      __atomic_fetch_add(&diag_rom_returns, 1, __ATOMIC_RELAXED);
    diag_was_ram = in_ram;
    // Core 1 owns the snapshot; Core 0 only requests it.
    uint32_t capture_reason = __atomic_load_n(&diag_rx_capture_requested, __ATOMIC_ACQUIRE);
    if (capture_reason) {
      __atomic_store_n(&diag_rx_capture_requested, 0, __ATOMIC_RELAXED);
      hd6301_record_memory_fault(0, 0, capture_reason);
    }
    // Keep only custom-RAM instructions and the first byte beyond RAM.
    if (diag_instruction_pc >= 0x80 && diag_instruction_pc <= 0x100 &&
        !__atomic_load_n(&diag_memory_fault_ready, __ATOMIC_RELAXED)) {
      hd6301_instruction_trace_t *entry = &diag_trace[diag_trace_head];
      entry->pc = diag_instruction_pc;
      entry->opcode = opcode;
      entry->x = reg_getix();
      entry->sp = reg_getsp();
      entry->d = reg_getaccd();
      entry->ccr = reg_getccr();
      entry->trcsr = iram[TRCSR];
      diag_trace_head = (diag_trace_head + 1) % HD6301_DIAG_TRACE_SIZE;
      if (diag_trace_count < HD6301_DIAG_TRACE_SIZE) ++diag_trace_count;
    }
#endif
    opptr = &opcodetab [opcode];
    reg_incpc (1);
    (*opptr->op_func) ();
  }
  
  cpu_setncycles (cpu_getncycles () + opptr->op_n_cycles);
  timer_inc (opptr->op_n_cycles);
}

