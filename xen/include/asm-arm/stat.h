#include <xen/sched.h>

#define HVC_EVT_START		0x4b000040
#define HVC_EVT_END		0x4b000050

extern volatile int xen_stat_en;

struct arm_virt_stat
{
    unsigned long trap_hyp_cnt;
    unsigned long trap_irq_cnt;
    unsigned long trap_fiq_cnt;
    unsigned long timer_inj_cnt;
    unsigned long do_irq_cnt;
    unsigned long do_sgi_cnt;
    unsigned long dispatch_sgi_cnt;
    unsigned long ipi_cnt;
    unsigned long wfi_cnt;
    unsigned long mmio_cnt;
    unsigned long guest_fault_cnt;
};

#ifndef XEN_ARM_STAT
#define XEN_ARM_STAT

void inline __evt_cnt_incr(unsigned long *cnt)
{
    int dom_id = current->domain->domain_id;

    if (!cnt)
        return;

    if (xen_stat_en && dom_id != 0)
        (*cnt)++;
    return;
}
#define evt_cnt_incr(_evt) __evt_cnt_incr( \
        &virt_stat._evt ## _cnt)

extern struct arm_virt_stat virt_stat;
extern void virt_stat_reset(void);
extern void virt_stat_show(int dom_id);
#endif
