#define HVC_EVT_START		0x4b000040
#define HVC_EVT_END		0x4b000050

extern volatile int xen_stat_en;

struct arm_virt_stat
{
    unsigned long trap_cnt;
};

struct arm_virt_stat virt_stat;
