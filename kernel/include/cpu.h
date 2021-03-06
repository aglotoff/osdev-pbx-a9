#ifndef __KERNEL_CPU_H__
#define __KERNEL_CPU_H__

#ifndef __KERNEL__
#error "This is a kernel header; user programs should not #include it"
#endif

struct Context;
struct Task;

/**
 * Per-CPU state.
 */
struct Cpu {
  struct Context *scheduler;      ///< Saved scheduler context
  struct Task  *task;         ///< The currently running task   
  int             irq_save_count; ///< Depth of irq_save() nesting
  int             irq_flags;      ///< Were interupts enabled before IRQ save?
};

/**
 * Maximum number of CPUs.
 */
#define NCPU  4

extern struct Cpu cpus[];

unsigned     cpu_id(void);
struct Cpu  *my_cpu(void);
struct Task *my_task(void);

void         irq_disable(void);
void         irq_enable(void);
void         irq_save(void);
void         irq_restore(void);

#endif  // !__KERNEL_CPU_H__
