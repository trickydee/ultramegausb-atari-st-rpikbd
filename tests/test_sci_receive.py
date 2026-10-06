#!/usr/bin/env python3
"""Exercise the actual SCI register handlers with controlled receive interleavings."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "6301/sci.c").read_text()
functions = []
for name in ("sci_reset_receive_status", "trcsr_getb", "rdr_getb"):
    pos = source.index("(" + name + ")")
    start = source.rfind("\n", 0, pos) + 1
    end = source.index("\n}", pos) + 2
    functions.append(source[start:end])
preamble = '#include <stdint.h>\n#include <assert.h>\n#include <stdio.h>\ntypedef unsigned char u_char; typedef unsigned int u_int;\n#define __not_in_flash_func(x) x\n#define ENABLE_SERIAL_LOGGING 1\n#define RDR 18\n#define TRCSR 17\n#define RDRF 128\n#define ORFE 64\nstatic u_char iram[32],sci_rx_status_seen,diag_consumed[512];\nstatic unsigned diag_consumed_total,diag_memory_fault_ready;\nstatic int running=1;\nstatic int cpu_isrunning(void){return running;}\nstatic u_char ireg_getb(unsigned x){return iram[x];}\nstatic void arrive(u_char b){assert(!(iram[TRCSR]&RDRF));iram[RDR]=b;iram[TRCSR]|=RDRF;}\n'
checks = 'int main(void){\n // Status says empty; byte arrives before the data half of LDD $11.\n assert(!(trcsr_getb(0)&RDRF)); arrive(0x54); rdr_getb(0);\n assert(iram[TRCSR]&RDRF); assert(diag_consumed_total==0);\n assert(trcsr_getb(0)&RDRF); assert(rdr_getb(0)==0x54);\n assert(!(iram[TRCSR]&RDRF)); assert(diag_consumed_total==1);\n // Identical consecutive bytes must each be acknowledged once.\n for(int i=0;i<3;i++){arrive(0x54);assert(trcsr_getb(0)&RDRF);assert(rdr_getb(0)==0x54);}\n assert(diag_consumed_total==4);\n // Data-only reads do not acknowledge a fresh byte.\n arrive(0x2a);rdr_getb(0);assert(iram[TRCSR]&RDRF);\n running=0;trcsr_getb(0);rdr_getb(0);running=1;assert(!sci_rx_status_seen);\n trcsr_getb(0);sci_reset_receive_status();assert(!sci_rx_status_seen);\n trcsr_getb(0);rdr_getb(0);assert(!(iram[TRCSR]&RDRF));\n iram[TRCSR]=ORFE;trcsr_getb(0);rdr_getb(0);assert(!(iram[TRCSR]&ORFE));\n puts("SCI late-arrival, repeated-byte, status acknowledgement, reset and debugger-read tests passed");\n}\n'
with tempfile.TemporaryDirectory() as tmp:
    cfile = Path(tmp) / "sci_test.c"
    binary = Path(tmp) / "sci_test"
    cfile.write_text(preamble + "\n".join(functions) + checks)
    subprocess.run(["cc", str(cfile), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
