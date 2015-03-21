#define HVC_EVT_START		0x4b000040
#define HVC_EVT_END		0x4b000050

extern volatile int xen_stat_en;

struct arm_virt_stat
{
    unsigned long trap_hyp_cnt;
    unsigned long timer_inj_cnt;
};

#ifndef XEN_ARM_STAT
#define XEN_ARM_STAT
extern struct arm_virt_stat virt_stat;
extern void trap_hyp_cnt_incr(int dom_id);
extern void timer_inj_cnt_incr(int dom_id);
extern void virt_stat_reset(void);
extern void virt_stat_show(int dom_id);
#endif
