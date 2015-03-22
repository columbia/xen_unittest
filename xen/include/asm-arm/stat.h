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
    unsigned long ipi_cnt;
    unsigned long wfi_cnt;
    unsigned long mmio_cnt;
    unsigned long guest_fault_cnt;
};

#ifndef XEN_ARM_STAT
#define XEN_ARM_STAT
extern struct arm_virt_stat virt_stat;
extern void trap_hyp_cnt_incr(int dom_id);
extern void trap_irq_cnt_incr(int dom_id);
extern void trap_fiq_cnt_incr(int dom_id);
extern void timer_inj_cnt_incr(int dom_id);
extern void ipi_cnt_incr(int dom_id);
extern void do_irq_cnt_incr(int dom_id);
extern void do_sgi_cnt_incr(int dom_id);
extern void wfi_cnt_incr(int dom_id);
extern void mmio_cnt_incr(int dom_id);
extern void guest_fault_cnt_incr(int dom_id);
extern void virt_stat_reset(void);
extern void virt_stat_show(int dom_id);
#endif
