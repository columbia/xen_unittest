#ifndef __EXIT_H__
#define __EXIT_H__

enum trap_reason {
	TRAP_HVC = 0,
	TRAP_HVC_GRANT,
	TRAP_WFE,
	TRAP_WFI,
	TRAP_IO_KERNEL, 
	TRAP_SGI,
	TRAP_IRQ,
	TRAP_IRQ_HYP,
	TRAP_OTHER,
	TRAP_ABORT,
	TRAP_MAX
};

#endif
