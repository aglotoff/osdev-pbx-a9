#ifndef KERNEL_ARMV7_H
#define KERNEL_ARMV7_H

/*
 *
 * Part 1. Program Status Registers
 * 
 */

#define PSR_M_MASK    0x1F          ///< Mode field bitmask
#define PSR_M_USR     0x10          ///<   User
#define PSR_M_FIQ     0x11          ///<   FIQ
#define PSR_M_IRQ     0x12          ///<   IRQ
#define PSR_M_SVC     0x13          ///<   Supervicor
#define PSR_M_MON     0x16          ///<   Monitor
#define PSR_M_ABT     0x17          ///<   Abort
#define PSR_M_UND     0x1B          ///<   Undefined
#define PSR_M_SYS     0x1F          ///<   System
#define PSR_T         (1 << 5)      ///< Thumb execution state bit
#define PSR_F         (1 << 6)      ///< Fast interrupt disable bit
#define PSR_I         (1 << 7)      ///< Interrupt disable bit
#define PSR_A         (1 << 8)      ///< Asynchronous abort disable bit
#define PSR_E         (1 << 9)      ///< Endianness execution state bit
#define PSR_GE_MASK   (0xF << 16)   ///< Greater than or Equal flags bitmask
#define PSR_J         (1 << 24)     ///< Jazelle bit
#define PSR_Q         (1 << 27)     ///< Cumulative saturation flag
#define PSR_V         (1 << 28)     ///< Overflow condition code flag
#define PSR_C         (1 << 29)     ///< Carry condition code flag
#define PSR_Z         (1 << 30)     ///< Zero condition code flag
#define PSR_N         (1 << 31)     ///< Negative condition code flag

/*
 *
 * Part 2. CP15 Registers
 * 
 */

// System Control Register bits
#define SCTLR_M         (1 << 0)      ///< MMU enable
#define SCTLR_A         (1 << 1)      ///< Alignment
#define SCTLR_C         (1 << 2)      ///< Cache enable
#define SCTLR_SW        (1 << 10)     ///< SWP/SWPB Enable
#define SCTLR_Z         (1 << 11)     ///< Branch prediction enable
#define SCTLR_I         (1 << 12)     ///< Instruction cache enable
#define SCTLR_V         (1 << 13)     ///< High exception vectors
#define SCTLR_RR        (1 << 14)     ///< Round Robin
#define SCTLR_HA        (1 << 17)     ///< Hardware Access Flag Enable
#define SCTLR_FI        (1 << 21)     ///< Fast Interupts configuration enable
#define SCTLR_VE        (1 << 24)     ///< Interrupt Vectors Enable
#define SCTLR_EE        (1 << 25)     ///< Exception Endianness
#define SCTLR_NMFI      (1 << 27)     ///< Non-maskable Fast Interrupts enable
#define SCTLR_TRE       (1 << 28)     ///< TX Remap Enable
#define SCTLR_AFE       (1 << 29)     ///< Acces Flag Enable
#define SCTLR_TE        (1 << 30)     ///< Thumb Exception enable

// Domain access permission bits
#define DA_MASK         0x3           ///< Domain access permissions bitmask
#define DA_NO           0x0           ///<   No access
#define DA_CLIENT       0x1           ///<   Client
#define DA_MANAGER      0x3           ///<   Manager

/** Domain n access permission bits */
#define DACR_D(n, x)    ((x) << (n * 2))

#ifndef __ASSEMBLER__

#include <stdint.h>

/**
 * Read the value of the MPIDR register
 */
static inline uint32_t
read_mpidr(void)
{
  uint32_t val;

  asm volatile ("mrc p15, 0, %0, c0, c0, 5" : "=r" (val));
  return val;
}

/**
 * Read the value of the DFSR register
 */
static inline uint32_t
read_dfsr(void)
{
  uint32_t val;

  asm volatile ("mrc p15, 0, %0, c5, c0, 0" : "=r" (val));
  return val;
}

/**
 * Read the value of the IFSR register
 */
static inline uint32_t
read_ifsr(void)
{
  uint32_t val;

  asm volatile ("mrc p15, 0, %0, c5, c0, 1" : "=r" (val));
  return val;
}

/**
 * Read the value of the DFAR register
 */
static inline uint32_t
read_dfar(void)
{
  uint32_t val;

  asm volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r" (val));
  return val;
}

/**
 * Read the value of the IFAR register
 */
static inline uint32_t
read_ifar(void)
{
  uint32_t val;

  asm volatile ("mrc p15, 0, %0, c6, c0, 1" : "=r" (val));
  return val;
}


#endif  // !__ASSEMBLER__

#endif  // !KERNEL_ARMV7_H
