#include <asm/stat.h>
#include <xen/domain_page.h>

void virt_stat_reset(void)
{
    virt_stat.trap_hyp_cnt = 0;
    virt_stat.trap_irq_cnt = 0;
    virt_stat.trap_fiq_cnt = 0;
    virt_stat.timer_inj_cnt = 0;
    virt_stat.do_irq_cnt = 0;
    virt_stat.do_sgi_cnt = 0;
    virt_stat.dispatch_sgi_cnt = 0;
    virt_stat.ipi_cnt = 0;
    virt_stat.wfi_cnt = 0;
    virt_stat.mmio_cnt = 0;
    virt_stat.guest_fault_cnt = 0;

    return;
}

void virt_stat_show(int dom_id)
{
    printk("total trap count %lu\n", virt_stat.trap_irq_cnt + 
            virt_stat.trap_hyp_cnt + virt_stat.trap_fiq_cnt);
    printk("----------------------\n"); 
    printk("dom %d trap fiq cnt %lu\n", dom_id, virt_stat.trap_fiq_cnt);
    printk("dom %d trap irq cnt %lu\n", dom_id, virt_stat.trap_irq_cnt);
    printk("dom %d do_IRQ cnt %lu\n", dom_id, virt_stat.do_irq_cnt);
    printk("dom %d timer injection cnt %lu\n", dom_id, virt_stat.timer_inj_cnt);
    printk("dom %d do_SGI cnt %lu\n", dom_id, virt_stat.do_sgi_cnt);
    printk("dom %d dispatch_SGI cnt %lu\n", dom_id, virt_stat.dispatch_sgi_cnt);
    printk("dom %d ipi cnt %lu\n", dom_id, virt_stat.ipi_cnt);
    printk("----------------------\n"); 
    printk("dom %d trap hyp cnt %lu\n", dom_id, virt_stat.trap_hyp_cnt);
    printk("dom %d wfi cnt %lu\n", dom_id, virt_stat.wfi_cnt);
    printk("dom %d mmio cnt %lu\n", dom_id, virt_stat.mmio_cnt);
    printk("dom %d guest fault cnt %lu\n", dom_id, virt_stat.guest_fault_cnt);
    printk("----------------------\n");
    return;
}
