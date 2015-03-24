#include <asm/stat.h>
#include <xen/domain_page.h>

void virt_stat_show(int dom_id)
{
    printk("total trap count %lu\n", virt_stat->trap_irq_cnt + 
            virt_stat->trap_hyp_cnt + virt_stat->trap_fiq_cnt);
    printk("----------------------\n"); 
    printk("dom %d trap fiq cnt %lu\n", dom_id, virt_stat->trap_fiq_cnt);
    printk("dom %d trap irq cnt %lu\n", dom_id, virt_stat->trap_irq_cnt);
    printk("dom %d do_IRQ cnt %lu\n", dom_id, virt_stat->do_irq_cnt);
    printk("dom %d ppi cnt %lu\n", dom_id, virt_stat->ppis_cnt);
    printk("dom %d spi cnt %lu\n", dom_id, virt_stat->spis_cnt);
    printk("dom %d guest irq cnt %lu\n", dom_id, virt_stat->guest_irqs_cnt);
    printk("dom %d timer injection cnt %lu\n", dom_id, virt_stat->timer_inj_cnt);
    printk("dom %d do_SGI cnt %lu\n", dom_id, virt_stat->do_sgi_cnt);
    printk("dom %d dispatch_SGI cnt %lu\n", dom_id, virt_stat->dispatch_sgi_cnt);
    printk("dom %d ipi cnt %lu\n", dom_id, virt_stat->ipi_cnt);
    
    printk("----------------------\n"); 
    printk("dom %d trap hyp cnt %lu\n", dom_id, virt_stat->trap_hyp_cnt);
    printk("dom %d wfi cnt %lu\n", dom_id, virt_stat->wfi_cnt);
    printk("dom %d wfe cnt %lu\n", dom_id, virt_stat->wfe_cnt);
    printk("dom %d mmio cnt %lu\n", dom_id, virt_stat->mmio_cnt);
    printk("dom %d guest fault cnt %lu\n", dom_id, virt_stat->guest_fault_cnt);
    printk("----------------------\n");
    return;
}

void virt_stat_init(unsigned char key)
{
    virt_stat = xmalloc(struct arm_virt_stat);
    BUG_ON(virt_stat == NULL);
    virt_stat->trap_hyp_cnt = 0;
    virt_stat->trap_irq_cnt = 0;
    virt_stat->trap_fiq_cnt = 0;
    virt_stat->timer_inj_cnt = 0;
    virt_stat->do_irq_cnt = 0;
    virt_stat->do_sgi_cnt = 0;
    virt_stat->dispatch_sgi_cnt = 0;
    virt_stat->ipi_cnt = 0;
    virt_stat->wfi_cnt = 0;
    virt_stat->wfe_cnt = 0;
    virt_stat->mmio_cnt = 0;
    virt_stat->guest_fault_cnt = 0;
    virt_stat->guest_irqs_cnt = 0;
    virt_stat->ppis_cnt = 0;
    virt_stat->spis_cnt= 0;
    xen_stat_en = 1;
}

void virt_stat_reset(unsigned char key)
{
    xen_stat_en = 0;
    virt_stat_show(current->domain->domain_id);
    xfree(virt_stat);
    virt_stat = NULL;
}
