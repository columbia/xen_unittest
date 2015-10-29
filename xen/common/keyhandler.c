/******************************************************************************
 * keyhandler.c
 */

#include <asm/regs.h>
#include <xen/keyhandler.h> 
#include <xen/shutdown.h>
#include <xen/event.h>
#include <xen/console.h>
#include <xen/serial.h>
#include <xen/sched.h>
#include <xen/tasklet.h>
#include <xen/domain.h>
#include <xen/rangeset.h>
#include <xen/compat.h>
#include <xen/ctype.h>
#include <xen/perfc.h>
#include <xen/mm.h>
#include <xen/watchdog.h>
#include <xen/init.h>
#include <asm/debugger.h>
#include <asm/div64.h>

#include <xen/exit.h>

static struct keyhandler *key_table[256];
static unsigned char keypress_key;
static bool_t alt_key_handling;

char keyhandler_scratch[1024];

static void keypress_action(unsigned long unused)
{
    handle_keypress(keypress_key, NULL);
}

static DECLARE_TASKLET(keypress_tasklet, keypress_action, 0);

void handle_keypress(unsigned char key, struct cpu_user_regs *regs)
{
    struct keyhandler *h;

    if ( (h = key_table[key]) == NULL )
        return;

    if ( !in_irq() || h->irq_callback )
    {
        console_start_log_everything();
        h->irq_callback ? (*h->u.irq_fn)(key, regs) : (*h->u.fn)(key);
        console_end_log_everything();
    }
    else
    {
        keypress_key = key;
        tasklet_schedule(&keypress_tasklet);
    }
}

void register_keyhandler(unsigned char key, struct keyhandler *handler)
{
    ASSERT(key_table[key] == NULL);
    key_table[key] = handler;
}

static void show_handlers(unsigned char key)
{
    int i;
    printk("'%c' pressed -> showing installed handlers\n", key);
    for ( i = 0; i < ARRAY_SIZE(key_table); i++ ) 
        if ( key_table[i] != NULL ) 
            printk(" key '%c' (ascii '%02x') => %s\n", 
                   isprint(i) ? i : ' ', i, key_table[i]->desc);
}

static struct keyhandler show_handlers_keyhandler = {
    .u.fn = show_handlers,
    .desc = "show this message"
};

static cpumask_t dump_execstate_mask;

void dump_execstate(struct cpu_user_regs *regs)
{
    unsigned int cpu = smp_processor_id();

    if ( !guest_mode(regs) )
    {
        printk("*** Dumping CPU%u host state: ***\n", cpu);
        show_execution_state(regs);
    }

    if ( !is_idle_vcpu(current) )
    {
        printk("*** Dumping CPU%u guest state (%pv): ***\n",
               smp_processor_id(), current);
        show_execution_state(guest_cpu_user_regs());
        printk("\n");
    }

    cpumask_clear_cpu(cpu, &dump_execstate_mask);
    if ( !alt_key_handling )
        return;

    cpu = cpumask_cycle(cpu, &dump_execstate_mask);
    if ( cpu < nr_cpu_ids )
    {
        smp_send_state_dump(cpu);
        return;
    }

    console_end_sync();
    watchdog_enable();
}

static void dump_registers(unsigned char key, struct cpu_user_regs *regs)
{
    unsigned int cpu;

    /* We want to get everything out that we possibly can. */
    watchdog_disable();
    console_start_sync();

    printk("'%c' pressed -> dumping registers\n\n", key);

    cpumask_copy(&dump_execstate_mask, &cpu_online_map);

    /* Get local execution state out immediately, in case we get stuck. */
    dump_execstate(regs);

    /* Alt. handling: remaining CPUs are dumped asynchronously one-by-one. */
    if ( alt_key_handling )
        return;

    /* Normal handling: synchronously dump the remaining CPUs' states. */
    for_each_cpu ( cpu, &dump_execstate_mask )
    {
        smp_send_state_dump(cpu);
        while ( cpumask_test_cpu(cpu, &dump_execstate_mask) )
            cpu_relax();
    }

    console_end_sync();
    watchdog_enable();
}

static struct keyhandler dump_registers_keyhandler = {
    .irq_callback = 1,
    .diagnostic = 1,
    .u.irq_fn = dump_registers,
    .desc = "dump registers"
};

static DECLARE_TASKLET(dump_hwdom_tasklet, NULL, 0);

static void dump_hwdom_action(unsigned long arg)
{
    struct vcpu *v = (void *)arg;

    for ( ; ; )
    {
        vcpu_show_execution_state(v);
        if ( (v = v->next_in_list) == NULL )
            break;
        if ( softirq_pending(smp_processor_id()) )
        {
            dump_hwdom_tasklet.data = (unsigned long)v;
            tasklet_schedule_on_cpu(&dump_hwdom_tasklet, v->processor);
            break;
        }
    }
}

static void dump_hwdom_registers(unsigned char key)
{
    struct vcpu *v;

    if ( hardware_domain == NULL )
        return;

    printk("'%c' pressed -> dumping Dom0's registers\n", key);

    for_each_vcpu ( hardware_domain, v )
    {
        if ( alt_key_handling && softirq_pending(smp_processor_id()) )
        {
            tasklet_kill(&dump_hwdom_tasklet);
            tasklet_init(&dump_hwdom_tasklet, dump_hwdom_action,
                         (unsigned long)v);
            tasklet_schedule_on_cpu(&dump_hwdom_tasklet, v->processor);
            return;
        }
        vcpu_show_execution_state(v);
    }
}

static struct keyhandler dump_hwdom_registers_keyhandler = {
    .diagnostic = 1,
    .u.fn = dump_hwdom_registers,
    .desc = "dump Dom0 registers"
};

static void reboot_machine(unsigned char key, struct cpu_user_regs *regs)
{
    printk("'%c' pressed -> rebooting machine\n", key);
    machine_restart(0);
}

static struct keyhandler reboot_machine_keyhandler = {
    .irq_callback = 1,
    .u.irq_fn = reboot_machine,
    .desc = "reboot machine"
};

static void cpuset_print(char *set, int size, const cpumask_t *mask)
{
    *set++ = '{';
    set += cpulist_scnprintf(set, size-2, mask);
    *set++ = '}';
    *set++ = '\0';
}

static void nodeset_print(char *set, int size, const nodemask_t *mask)
{
    *set++ = '[';
    set += nodelist_scnprintf(set, size-2, mask);
    *set++ = ']';
    *set++ = '\0';
}

static void periodic_timer_print(char *str, int size, uint64_t period)
{
    if ( period == 0 )
    {
        strlcpy(str, "No periodic timer", size);
        return;
    }

    snprintf(str, size,
             "%u Hz periodic timer (period %u ms)",
             1000000000/(int)period, (int)period/1000000);
}

static void dump_domains(unsigned char key)
{
    struct domain *d;
    struct vcpu   *v;
    s_time_t       now = NOW();
#define tmpstr keyhandler_scratch

    printk("'%c' pressed -> dumping domain info (now=0x%X:%08X)\n", key,
           (u32)(now>>32), (u32)now);

    rcu_read_lock(&domlist_read_lock);

    for_each_domain ( d )
    {
        unsigned int i;

        process_pending_softirqs();

        printk("General information for domain %u:\n", d->domain_id);
        cpuset_print(tmpstr, sizeof(tmpstr), d->domain_dirty_cpumask);
        printk("    refcnt=%d dying=%d pause_count=%d\n",
               atomic_read(&d->refcnt), d->is_dying,
               atomic_read(&d->pause_count));
        printk("    nr_pages=%d xenheap_pages=%d shared_pages=%u paged_pages=%u "
               "dirty_cpus=%s max_pages=%u\n", d->tot_pages, d->xenheap_pages, 
                atomic_read(&d->shr_pages), atomic_read(&d->paged_pages), 
                tmpstr, d->max_pages);
        printk("    handle=%02x%02x%02x%02x-%02x%02x-%02x%02x-"
               "%02x%02x-%02x%02x%02x%02x%02x%02x vm_assist=%08lx\n",
               d->handle[ 0], d->handle[ 1], d->handle[ 2], d->handle[ 3],
               d->handle[ 4], d->handle[ 5], d->handle[ 6], d->handle[ 7],
               d->handle[ 8], d->handle[ 9], d->handle[10], d->handle[11],
               d->handle[12], d->handle[13], d->handle[14], d->handle[15],
               d->vm_assist);
        for ( i = 0 ; i < NR_DOMAIN_WATCHDOG_TIMERS; i++ )
            if ( test_bit(i, &d->watchdog_inuse_map) )
                printk("    watchdog %d expires in %d seconds\n",
                       i, (u32)((d->watchdog_timer[i].expires - NOW()) >> 30));

        arch_dump_domain_info(d);

        rangeset_domain_printk(d);

        dump_pageframe_info(d);
               
        nodeset_print(tmpstr, sizeof(tmpstr), &d->node_affinity);
        printk("NODE affinity for domain %d: %s\n", d->domain_id, tmpstr);

        printk("VCPU information and callbacks for domain %u:\n",
               d->domain_id);
        for_each_vcpu ( d, v )
        {
            if ( !(v->vcpu_id & 0x3f) )
                process_pending_softirqs();

            printk("    VCPU%d: CPU%d [has=%c] poll=%d "
                   "upcall_pend = %02x, upcall_mask = %02x ",
                   v->vcpu_id, v->processor,
                   v->is_running ? 'T':'F', v->poll_evtchn,
                   vcpu_info(v, evtchn_upcall_pending),
                   !vcpu_event_delivery_is_enabled(v));
            cpuset_print(tmpstr, sizeof(tmpstr), v->vcpu_dirty_cpumask);
            printk("dirty_cpus=%s ", tmpstr);
            cpuset_print(tmpstr, sizeof(tmpstr), v->cpu_hard_affinity);
            printk("cpu_affinity=%s\n", tmpstr);
            cpuset_print(tmpstr, sizeof(tmpstr), v->cpu_soft_affinity);
            printk("cpu_soft_affinity=%s\n", tmpstr);
            printk("    pause_count=%d pause_flags=%lx\n",
                   atomic_read(&v->pause_count), v->pause_flags);
            arch_dump_vcpu_info(v);
            periodic_timer_print(tmpstr, sizeof(tmpstr), v->periodic_period);
            printk("    %s\n", tmpstr);
        }
    }

    for_each_domain ( d )
    {
        for_each_vcpu ( d, v )
        {
            if ( !(v->vcpu_id & 0x3f) )
                process_pending_softirqs();

            printk("Notifying guest %d:%d (virq %d, port %d)\n",
                   d->domain_id, v->vcpu_id,
                   VIRQ_DEBUG, v->virq_to_evtchn[VIRQ_DEBUG]);
            send_guest_vcpu_virq(v, VIRQ_DEBUG);
        }
    }

    arch_dump_shared_mem_info();

    rcu_read_unlock(&domlist_read_lock);
#undef tmpstr
}

static struct keyhandler dump_domains_keyhandler = {
    .diagnostic = 1,
    .u.fn = dump_domains,
    .desc = "dump domain (and guest debug) info"
};

static cpumask_t read_clocks_cpumask;
static DEFINE_PER_CPU(s_time_t, read_clocks_time);
static DEFINE_PER_CPU(u64, read_cycles_time);

static void read_clocks_slave(void *unused)
{
    unsigned int cpu = smp_processor_id();
    local_irq_disable();
    while ( !cpumask_test_cpu(cpu, &read_clocks_cpumask) )
        cpu_relax();
    per_cpu(read_clocks_time, cpu) = NOW();
    per_cpu(read_cycles_time, cpu) = get_cycles();
    cpumask_clear_cpu(cpu, &read_clocks_cpumask);
    local_irq_enable();
}

static void read_clocks(unsigned char key)
{
    unsigned int cpu = smp_processor_id(), min_stime_cpu, max_stime_cpu;
    unsigned int min_cycles_cpu, max_cycles_cpu;
    u64 min_stime, max_stime, dif_stime;
    u64 min_cycles, max_cycles, dif_cycles;
    static u64 sumdif_stime = 0, maxdif_stime = 0;
    static u64 sumdif_cycles = 0, maxdif_cycles = 0;
    static u32 count = 0;
    static DEFINE_SPINLOCK(lock);

    spin_lock(&lock);

    smp_call_function(read_clocks_slave, NULL, 0);

    local_irq_disable();
    cpumask_andnot(&read_clocks_cpumask, &cpu_online_map, cpumask_of(cpu));
    per_cpu(read_clocks_time, cpu) = NOW();
    per_cpu(read_cycles_time, cpu) = get_cycles();
    local_irq_enable();

    while ( !cpumask_empty(&read_clocks_cpumask) )
        cpu_relax();

    min_stime_cpu = max_stime_cpu = min_cycles_cpu = max_cycles_cpu = cpu;
    for_each_online_cpu ( cpu )
    {
        if ( per_cpu(read_clocks_time, cpu) <
             per_cpu(read_clocks_time, min_stime_cpu) )
            min_stime_cpu = cpu;
        if ( per_cpu(read_clocks_time, cpu) >
             per_cpu(read_clocks_time, max_stime_cpu) )
            max_stime_cpu = cpu;
        if ( per_cpu(read_cycles_time, cpu) <
             per_cpu(read_cycles_time, min_cycles_cpu) )
            min_cycles_cpu = cpu;
        if ( per_cpu(read_cycles_time, cpu) >
             per_cpu(read_cycles_time, max_cycles_cpu) )
            max_cycles_cpu = cpu;
    }

    min_stime = per_cpu(read_clocks_time, min_stime_cpu);
    max_stime = per_cpu(read_clocks_time, max_stime_cpu);
    min_cycles = per_cpu(read_cycles_time, min_cycles_cpu);
    max_cycles = per_cpu(read_cycles_time, max_cycles_cpu);

    spin_unlock(&lock);

    dif_stime = max_stime - min_stime;
    if ( dif_stime > maxdif_stime )
        maxdif_stime = dif_stime;
    sumdif_stime += dif_stime;
    dif_cycles = max_cycles - min_cycles;
    if ( dif_cycles > maxdif_cycles )
        maxdif_cycles = dif_cycles;
    sumdif_cycles += dif_cycles;
    count++;
    printk("Synced stime skew: max=%"PRIu64"ns avg=%"PRIu64"ns "
           "samples=%"PRIu32" current=%"PRIu64"ns\n",
           maxdif_stime, sumdif_stime/count, count, dif_stime);
    printk("Synced cycles skew: max=%"PRIu64" avg=%"PRIu64" "
           "samples=%"PRIu32" current=%"PRIu64"\n",
           maxdif_cycles, sumdif_cycles/count, count, dif_cycles);
}

static struct keyhandler read_clocks_keyhandler = {
    .diagnostic = 1,
    .u.fn = read_clocks,
    .desc = "display multi-cpu clock info"
};

static struct keyhandler dump_runq_keyhandler = {
    .diagnostic = 1,
    .u.fn = dump_runq,
    .desc = "dump run queues"
};

#ifdef PERF_COUNTERS
static struct keyhandler perfc_printall_keyhandler = {
    .diagnostic = 1,
    .u.fn = perfc_printall,
    .desc = "print performance counters"
};
static struct keyhandler perfc_reset_keyhandler = {
    .u.fn = perfc_reset,
    .desc = "reset performance counters"
};
#endif

#ifdef LOCK_PROFILE
static struct keyhandler spinlock_printall_keyhandler = {
    .diagnostic = 1,
    .u.fn = spinlock_profile_printall,
    .desc = "print lock profile info"
};
static struct keyhandler spinlock_reset_keyhandler = {
    .u.fn = spinlock_profile_reset,
    .desc = "reset lock profile info"
};
#endif

static void run_all_nonirq_keyhandlers(unsigned long unused)
{
    /* Fire all the non-IRQ-context diagnostic keyhandlers */
    struct keyhandler *h;
    int k;

    console_start_log_everything();

    for ( k = 0; k < ARRAY_SIZE(key_table); k++ )
    {
        process_pending_softirqs();
        h = key_table[k];
        if ( (h == NULL) || !h->diagnostic || h->irq_callback )
            continue;
        printk("[%c: %s]\n", k, h->desc);
        (*h->u.fn)(k);
    }

    console_end_log_everything();
}

static DECLARE_TASKLET(run_all_keyhandlers_tasklet,
                       run_all_nonirq_keyhandlers, 0);

static void run_all_keyhandlers(unsigned char key, struct cpu_user_regs *regs)
{
    struct keyhandler *h;
    int k;

    watchdog_disable();

    printk("'%c' pressed -> firing all diagnostic keyhandlers\n", key);

    /* Fire all the IRQ-context diangostic keyhandlers now */
    for ( k = 0; k < ARRAY_SIZE(key_table); k++ )
    {
        h = key_table[k];
        if ( (h == NULL) || !h->diagnostic || !h->irq_callback )
            continue;
        printk("[%c: %s]\n", k, h->desc);
        (*h->u.irq_fn)(k, regs);
    }

    watchdog_enable();

    /* Trigger the others from a tasklet in non-IRQ context */
    tasklet_schedule(&run_all_keyhandlers_tasklet);
}

static struct keyhandler run_all_keyhandlers_keyhandler = {
    .irq_callback = 1,
    .u.irq_fn = run_all_keyhandlers,
    .desc = "print all diagnostics"
};

static void do_debug_key(unsigned char key, struct cpu_user_regs *regs)
{
    printk("'%c' pressed -> trapping into debugger\n", key);
    (void)debugger_trap_fatal(0xf001, regs);
    nop(); /* Prevent the compiler doing tail call
                             optimisation, as that confuses xendbg a
                             bit. */
}

static struct keyhandler do_debug_key_keyhandler = {
    .irq_callback = 1,
    .u.irq_fn = do_debug_key,
    .desc = "trap to xendbg"
};

int profile_on = 0;
static inline unsigned long xen_arm_read_pcounter(void)
{
	unsigned long val;

	asm volatile(
			"isb\n"
			"mrs %0, CNTPCT_EL0\n"
			"isb\n"
			: [reg] "=r" (val));
	return val;
}


struct domain *g_idle_domain;
unsigned long acc_ctx[NR_CPUS];
unsigned long cnt_ctx[NR_CPUS];
int hyp_table_size = 13;

unsigned long sum_idle_time = 0;
unsigned long idle_switch_time = 0;
unsigned long total_switch = 0;
unsigned long total_dom0_switch_cnt = 0;
unsigned long total_domU_switch_cnt = 0;
unsigned long arch_timer_rate = 50;

static const char* trap_stat_names[TRAP_MAX] = {
	[TRAP_HVC]		= "--HVC",
	[TRAP_HVC_GRANT]	= "--HVC_GRANT_CPY",
	[TRAP_WFE]		= "--WFE",
	[TRAP_WFI]		= "--WFI",
	[TRAP_IO_KERNEL]	= "--IO_KERNEL",
	[TRAP_IRQ]		= "--IRQ",
	[TRAP_IRQ_HYP]		= "--IRQ_HYP",
	[TRAP_SGI]		= "--SGI",
	[TRAP_ABORT]		= "--ABORT",
	[TRAP_OTHER]		= "--OTHER",
};

/* This function is called with rcu_read_lock held */
static void update_stat(unsigned long stop_time)
{
	struct domain *d;
	struct vcpu   *v;
	struct vcpu_runstate_info runstate;
        unsigned int i = 0;

	for_each_domain ( d )
	{
		d->acc_domain_time = 0;
		d->acc_guest_time = 0;
		d->acc_do_trap_time = 0;
		d->acc_switch_to_xen = 0;
		d->acc_switch_to_dom = 0;
		d->acc_ctx_vcpu = 0;
		d->cnt_switch_to_xen = 0;
		d->acc_ctx = 0;
		d->cnt_ctx = 0;

		for (i = 0 ; i < TRAP_MAX; i++) {
			d->trap_breakdown_time[i] = 0;
			d->trap_breakdown_cnt[i] = 0;
		}

		for_each_vcpu ( d, v )
		{
			vcpu_runstate_get(v, &runstate);
			d->acc_domain_time += runstate.time[RUNSTATE_running] \
					      - v->init_running_time;

			if (v->ts_xen_exit > v->ts_xen_entry)  /* vcpu NOT in EL2 */
				v->acc_guest_time += stop_time - v->ts_xen_exit;
			d->acc_guest_time += v->acc_guest_time;

			d->acc_do_trap_time += v->acc_do_trap_time;
			d->acc_switch_to_xen += v->acc_switch_to_xen;
			d->acc_switch_to_dom += v->acc_switch_to_dom;
			d->cnt_switch_to_xen += v->cnt_switch_to_xen;
			d->acc_ctx_vcpu += v->acc_ctx_vcpu;
			d->acc_ctx += v->acc_ctx;
			d->cnt_ctx += v->cnt_ctx;
			for (i = 0 ; i < TRAP_MAX; i++) {
				d->trap_breakdown_time[i] += v->trap_breakdown_time[i];
				d->trap_breakdown_cnt[i] += v->trap_breakdown_cnt[i];
			}
		}
	}

        sum_idle_time = 0;
        idle_switch_time = 0;
        total_switch = 0;
        total_dom0_switch_cnt = 0;
        total_domU_switch_cnt = 0;
        for (i = 0; i < nr_cpu_ids; i++) {
                sum_idle_time += get_cpu_idle_time(i);
                idle_switch_time += get_idle_switch_time(i);
                total_switch += acc_ctx[i];
		if ( i < 4)
                	total_dom0_switch_cnt += cnt_ctx[i];
		else
                	total_domU_switch_cnt += cnt_ctx[i];
        }
}

#define msec(x) ((x) * 20 / 1000 / 1000)
#define print_title(st1, st2, st3, st4) \
	printk("%20s %12s %12s %12s\n", \
		st1, st2, st3, st4)
#define print_rec(lbl, d1, d2) \
	printk("%20s %12"PRIu64" %12"PRIu64"\n", \
		lbl, d1, msec(d2))
#define print_rec4(lbl, d1, d2) \
	printk("%20s %12"PRIu64" %12"PRIu64" %12d\n", \
		lbl, d1, msec(d1), d2)
#define print_raw(lbl, d1, d2) \
	printk("%20s %12"PRIu64" %12"PRIu64"\n", \
		lbl, d1, msec(d2))
static void print_per_domain_stat(unsigned long duration)
{
	struct domain *d;
	unsigned long xen_time;
	int i = 0;

        printk("<---------- Per Domain Stat (Unit: arch counter) ---------->\n");
	printk("Elapsed:       \t%12"PRIu64" msec\n", msec(duration));
	for_each_domain ( d )
	{
		/*
		for_each_vcpu ( d, v )
		{
			printk("Domain: %u VCPU: %u\n", d->domain_id , v->vcpu_id);
			printk("Acc dom:\t %12"PRIu64"\n", v->acc_guest_time);
			printk("Acc switch to Xen:\t %12"PRIu64"\n", v->acc_switch_to_xen);
			printk("Acc switch to Dom:\t %12"PRIu64"\n", v->acc_switch_to_dom);
			printk("Acc do_trap:\t %12"PRIu64"\n", v->acc_do_trap_time);
		}
		*/
		printk("<Domain %u Summary>\n", d->domain_id);
		print_title("", "arch counter", "msec", "nr");
		print_raw("Total:",  (long unsigned int)0, (long unsigned int)d->acc_domain_time/20);
		print_rec("VM (EL0, EL1):", d->acc_guest_time, d->acc_guest_time);
		xen_time = d->acc_domain_time/20 - d->acc_guest_time;
		print_rec("Xen (EL2):", xen_time, xen_time);
		print_rec("-Do_trap:", d->acc_do_trap_time, d->acc_do_trap_time);
		for (i = 0 ; i < TRAP_MAX; i++)
			print_rec4(trap_stat_names[i],
					d->trap_breakdown_time[i],
					d->trap_breakdown_cnt[i]);
		print_rec4("-EL2 Switch:", d->acc_switch_to_xen + d->acc_switch_to_dom, (int)d->cnt_switch_to_xen);
		print_rec4("-VCPU Switch:", d->acc_ctx, (int)d->cnt_ctx);
		print_rec4("-Rest:", xen_time - d->acc_do_trap_time - \
			d->acc_switch_to_xen - d->acc_switch_to_dom - d->acc_ctx, 0);
		
                /*
		printk("--to_Xen:\t%12"PRIu64"\n", d->acc_switch_to_xen);
		printk("--to_Dom:\t%12"PRIu64"\n", d->acc_switch_to_dom);
                */
                /*
		printk("Swi_Xen_cnt:\t%12"PRIu64"\n", d->cnt_switch_to_xen);
		printk("to_xen:\t%9"PRIu64"/10 per switch\n", d->acc_switch_to_xen*10 / d->cnt_switch_to_xen);
		printk("to_dom:\t%9"PRIu64"/10 per switch\n", d->acc_switch_to_dom*10 / d->cnt_switch_to_xen);
                */
	}
/*
	printk("Dom0 VCPU Switch cnt: \t%12"PRIu64"\n", total_dom0_switch_cnt);
	printk("DomU VCPU Switch cnt: \t%12"PRIu64"\n", total_domU_switch_cnt);
*/
}

static void init_stat(unsigned long start_time)
{
	struct domain *d;
	struct vcpu   *v;
	struct vcpu_runstate_info runstate;
	int i = 0;

	for_each_domain ( d )
	{
		for_each_vcpu ( d, v )
		{
			v->ts_xen_exit = start_time;
			v->ts_xen_entry = start_time;
			v->ts_do_trap_xxx_entry= start_time;
			v->ts_do_trap_xxx_exit= start_time;

			v->acc_guest_time = 0;
			v->acc_do_trap_time = 0;
			v->acc_switch_to_xen = 0;
			v->acc_switch_to_dom = 0;
			v->acc_ctx_vcpu = 0;
			v->acc_ctx = 0;
			v->cnt_ctx = 0;
			v->cnt_switch_to_xen = 0;
			for (i = 0 ; i < TRAP_MAX; i++) {
				v->trap_breakdown_time[i] = 0;
				v->trap_breakdown_cnt[i] = 0;
			}

			vcpu_runstate_get(v, &runstate);
			v->init_running_time= runstate.time[RUNSTATE_running];
		}
	}


}

static void print_idle_domain_stat(unsigned long init_sum_idle_time, unsigned long duration)
{
        unsigned long total = (sum_idle_time - init_sum_idle_time)/20;
	printk("<Idle Domain Summary>\n");
	printk("Total:        \t%12"PRIu64"\n", total);
	printk("VCPU Switch:  \t%12"PRIu64"\n", idle_switch_time);
        printk("Idle:         \t%12"PRIu64"\n", total - idle_switch_time);
}

static void toggle_profile(unsigned char key, struct cpu_user_regs *regs)
{
	static unsigned long start_time;
	unsigned long stop_time;

	unsigned int i;
	static unsigned long init_sum_idle_time;

	if (profile_on) {
		profile_on = !profile_on;
		stop_time = xen_arm_read_pcounter();
	
		rcu_read_lock(&domlist_read_lock);
		update_stat(stop_time);
		print_per_domain_stat(stop_time - start_time);
		rcu_read_unlock(&domlist_read_lock);

                print_idle_domain_stat(init_sum_idle_time, stop_time - start_time);
	} else {
		start_time = xen_arm_read_pcounter();

		rcu_read_lock(&domlist_read_lock);
		init_stat(start_time);
		rcu_read_unlock(&domlist_read_lock);

		init_sum_idle_time = 0;
		for (i = 0; i < nr_cpu_ids; i++) {
			init_sum_idle_time += get_cpu_idle_time(i);
			reset_idle_switch_time(i);
			acc_ctx[i] = 0;
			cnt_ctx[i] = 0;
		}

		profile_on = !profile_on;
	}

	printk("Profile is %s\n", profile_on?"On":"Off");
}

static struct keyhandler toggle_profile_handler = {
	.irq_callback = 1,
	.u.irq_fn = toggle_profile,
	.desc = "Toggle Xen/Domain profiling"
};

static void do_toggle_alt_key(unsigned char key, struct cpu_user_regs *regs)
{
    alt_key_handling = !alt_key_handling;
    printk("'%c' pressed -> using %s key handling\n", key,
           alt_key_handling ? "alternative" : "normal");
}

static struct keyhandler toggle_alt_keyhandler = {
    .irq_callback = 1,
    .u.irq_fn = do_toggle_alt_key,
    .desc = "toggle alternative key handling"
};

void __init initialize_keytable(void)
{
    if ( num_present_cpus() > 16 )
    {
        alt_key_handling = 1;
        printk(XENLOG_INFO "Defaulting to alternative key handling; "
               "send 'A' to switch to normal mode.\n");
    }
    register_keyhandler('A', &toggle_alt_keyhandler);
    register_keyhandler('d', &dump_registers_keyhandler);
    register_keyhandler('h', &show_handlers_keyhandler);
    register_keyhandler('q', &dump_domains_keyhandler);
    register_keyhandler('r', &dump_runq_keyhandler);
    register_keyhandler('R', &reboot_machine_keyhandler);
    register_keyhandler('t', &read_clocks_keyhandler);
    register_keyhandler('0', &dump_hwdom_registers_keyhandler);
    register_keyhandler('%', &do_debug_key_keyhandler);
    register_keyhandler('*', &run_all_keyhandlers_keyhandler);
    register_keyhandler('C', &toggle_profile_handler);

#ifdef PERF_COUNTERS
    register_keyhandler('p', &perfc_printall_keyhandler);
    register_keyhandler('P', &perfc_reset_keyhandler);
#endif

#ifdef LOCK_PROFILE
    register_keyhandler('l', &spinlock_printall_keyhandler);
    register_keyhandler('L', &spinlock_reset_keyhandler);
#endif

}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
