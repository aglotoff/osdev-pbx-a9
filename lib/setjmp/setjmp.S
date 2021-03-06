/*
 * ----------------------------------------------------------------------------
 * int setjmp(jmp_buf env);
 * ----------------------------------------------------------------------------
 */

.globl setjmp
setjmp:
  // Save the required registers in `env`
  vstmia  r0!, {s0-s31}           // save FPU registers
  vmrs    r1, fpscr               // FPSCR -> R1
  str     r1, [r0], #4            // save FPSCR
  stmia   r0!, {r4-r14}           // save R4-R14

  // Return 0
  mov     r0, #0                  
  bx      lr
