/*
 * litmus/sched_pfp.c
 *
 * Implementation of partitioned fixed-priority scheduling.
 * Based on PSN-EDF.
 */

#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <litmus/litmus.h>
#include <litmus/wait.h>
#include <litmus/jobs.h>
#include <litmus/preempt.h>
#include <litmus/fp_common.h>
#include <litmus/sched_plugin.h>
#include <litmus/sched_trace.h>
#include <litmus/trace.h>
#include <litmus/budget.h>

/* to set up domain/cpu mappings */
#include <litmus/litmus_proc.h>
#include <linux/uaccess.h>

#include <litmus/fdso.h>
#include <litmus/srp.h>

/* **** MrsP Preemption Queue **** */
spinlock_t mrsp_migrate_lock;
struct task_struct** mrsp_tasks;
/* ******************************* */

/* ******* task queue ******* */
typedef struct task_list {
	struct list_head next;
	struct task_struct* task;
} tasks;
/* ************************** */

typedef struct {
	rt_domain_t domain;
	struct fp_prio_queue ready_queue;
	int cpu;
	struct task_struct* scheduled; /* only RT tasks */
	int queued_mrsp_tasks;
/*
 * scheduling lock slock
 * protects the domain and serializes scheduling decisions
 */
#define slock domain.ready_lock

} mrsp_domain_t;

DEFINE_PER_CPU(mrsp_domain_t, mrsp_domains);
DEFINE_PER_CPU(spinlock_t, mrsp_migrate_locks);

mrsp_domain_t* mrsp_doms[NR_CPUS];

#define local_pfp		(this_cpu_ptr(&mrsp_domains))
#define remote_dom(cpu)		(&per_cpu(mrsp_domains, cpu).domain)
#define remote_pfp(cpu)	(&per_cpu(mrsp_domains, cpu))
#define task_dom(task)		remote_dom(get_partition(task))
#define task_pfp(task)		remote_pfp(get_partition(task))

#ifdef CONFIG_LITMUS_LOCKING
DEFINE_PER_CPU(uint64_t, mrsp_fmlp_timestamp);
#endif

/* we assume the lock is being held */
static void preempt(mrsp_domain_t *pfp) {
	preempt_if_preemptable(pfp->scheduled, pfp->cpu);
}

static unsigned int priority_index(struct task_struct* t) {
#ifdef CONFIG_LITMUS_LOCKING
	if (unlikely(t->rt_param.inh_task))
		/* use effective priority */
		t = t->rt_param.inh_task;

	if (is_priority_boosted(t)) {
		/* zero is reserved for priority-boosted tasks */
		return 0;
	} else
#endif
		return get_priority(t);
}

static void mrsp_release_jobs(rt_domain_t* rt, struct bheap* tasks) {
	mrsp_domain_t *pfp = container_of(rt, mrsp_domain_t, domain);
	unsigned long flags;
	struct task_struct* t;
	struct bheap_node* hn;

	raw_spin_lock_irqsave(&pfp->slock, flags);

	while (!bheap_empty(tasks)) {
		hn = bheap_take(fp_ready_order, tasks);
		t = bheap2task(hn);
		TRACE_TASK(t, "take from ready queue. released (part:%d prio:%d)\n", get_partition(t), get_priority(t));
		fp_prio_add(&pfp->ready_queue, t, priority_index(t));
	}

	/* do we need to preempt? */
	if (fp_higher_prio(fp_prio_peek(&pfp->ready_queue), pfp->scheduled)) {
		TRACE_CUR("preempted by new release\n");
		preempt(pfp);
	}

	raw_spin_unlock_irqrestore(&pfp->slock, flags);
}

static void mrsp_preempt_check(mrsp_domain_t *pfp) {
	if (fp_higher_prio(fp_prio_peek(&pfp->ready_queue), pfp->scheduled))
		preempt(pfp);
}

static void mrsp_domain_init(mrsp_domain_t* pfp, int cpu) {
	fp_domain_init(&pfp->domain, NULL, mrsp_release_jobs);
	pfp->cpu = cpu;
	pfp->scheduled = NULL;
	pfp->queued_mrsp_tasks = 0;
	fp_prio_queue_init(&pfp->ready_queue);
}

static void requeue(struct task_struct* t, mrsp_domain_t *pfp) {
	tsk_rt(t)->completed = 0;
	if (is_released(t, litmus_clock()))
		fp_prio_add(&pfp->ready_queue, t, priority_index(t));
	else
		add_release(&pfp->domain, t); /* it has got to wait */
}

static void job_completion(struct task_struct* t, int forced) {
	sched_trace_task_completion(t, forced);
	TRACE_TASK(t, "job_completion(forced=%d) now prepare for next release.\n", forced);

	tsk_rt(t)->completed = 0;
	prepare_for_next_period(t);
	if (is_released(t, litmus_clock()))
		sched_trace_task_release(t);
}
struct task_list * task_remove(struct task_struct* task, struct list_head *head);

// manually unrolled
inline static struct task_struct*
get_high_priority_mrsp_task(int start_index) {
	if (mrsp_tasks[start_index + 0] != NULL)
		return mrsp_tasks[start_index + 0];
	if (mrsp_tasks[start_index + 1] != NULL)
		return mrsp_tasks[start_index + 1];
	if (mrsp_tasks[start_index + 2] != NULL)
		return mrsp_tasks[start_index + 2];
	if (mrsp_tasks[start_index + 3] != NULL)
		return mrsp_tasks[start_index + 3];
	if (mrsp_tasks[start_index + 4] != NULL)
		return mrsp_tasks[start_index + 4];
	if (mrsp_tasks[start_index + 5] != NULL)
		return mrsp_tasks[start_index + 5];
	if (mrsp_tasks[start_index + 6] != NULL)
		return mrsp_tasks[start_index + 6];
	if (mrsp_tasks[start_index + 7] != NULL)
		return mrsp_tasks[start_index + 7];
	if (mrsp_tasks[start_index + 8] != NULL)
		return mrsp_tasks[start_index + 8];
	if (mrsp_tasks[start_index + 9] != NULL)
		return mrsp_tasks[start_index + 9];
	if (mrsp_tasks[start_index + 10] != NULL)
		return mrsp_tasks[start_index + 10];
	if (mrsp_tasks[start_index + 11] != NULL)
		return mrsp_tasks[start_index + 11];
	if (mrsp_tasks[start_index + 12] != NULL)
		return mrsp_tasks[start_index + 12];
	if (mrsp_tasks[start_index + 13] != NULL)
		return mrsp_tasks[start_index + 13];
	if (mrsp_tasks[start_index + 14] != NULL)
		return mrsp_tasks[start_index + 14];
	if (mrsp_tasks[start_index + 15] != NULL)
		return mrsp_tasks[start_index + 15];
	if (mrsp_tasks[start_index + 16] != NULL)
		return mrsp_tasks[start_index + 16];
	if (mrsp_tasks[start_index + 17] != NULL)
		return mrsp_tasks[start_index + 17];
	if (mrsp_tasks[start_index + 18] != NULL)
		return mrsp_tasks[start_index + 18];
	if (mrsp_tasks[start_index + 19] != NULL)
		return mrsp_tasks[start_index + 19];
	if (mrsp_tasks[start_index + 20] != NULL)
		return mrsp_tasks[start_index + 20];
	if (mrsp_tasks[start_index + 21] != NULL)
		return mrsp_tasks[start_index + 21];
	if (mrsp_tasks[start_index + 22] != NULL)
		return mrsp_tasks[start_index + 22];
	if (mrsp_tasks[start_index + 23] != NULL)
		return mrsp_tasks[start_index + 23];
	if (mrsp_tasks[start_index + 24] != NULL)
		return mrsp_tasks[start_index + 24];
	if (mrsp_tasks[start_index + 25] != NULL)
		return mrsp_tasks[start_index + 25];
	if (mrsp_tasks[start_index + 26] != NULL)
		return mrsp_tasks[start_index + 26];
	if (mrsp_tasks[start_index + 27] != NULL)
		return mrsp_tasks[start_index + 27];
	if (mrsp_tasks[start_index + 28] != NULL)
		return mrsp_tasks[start_index + 28];
	if (mrsp_tasks[start_index + 29] != NULL)
		return mrsp_tasks[start_index + 29];
	if (mrsp_tasks[start_index + 30] != NULL)
		return mrsp_tasks[start_index + 30];
	if (mrsp_tasks[start_index + 31] != NULL)
		return mrsp_tasks[start_index + 31];
	if (mrsp_tasks[start_index + 32] != NULL)
		return mrsp_tasks[start_index + 32];
	if (mrsp_tasks[start_index + 33] != NULL)
		return mrsp_tasks[start_index + 33];
	if (mrsp_tasks[start_index + 34] != NULL)
		return mrsp_tasks[start_index + 34];
	if (mrsp_tasks[start_index + 35] != NULL)
		return mrsp_tasks[start_index + 35];
	if (mrsp_tasks[start_index + 36] != NULL)
		return mrsp_tasks[start_index + 36];
	if (mrsp_tasks[start_index + 37] != NULL)
		return mrsp_tasks[start_index + 37];
	if (mrsp_tasks[start_index + 38] != NULL)
		return mrsp_tasks[start_index + 38];
	if (mrsp_tasks[start_index + 39] != NULL)
		return mrsp_tasks[start_index + 39];
	if (mrsp_tasks[start_index + 40] != NULL)
		return mrsp_tasks[start_index + 40];
	if (mrsp_tasks[start_index + 41] != NULL)
		return mrsp_tasks[start_index + 41];
	if (mrsp_tasks[start_index + 42] != NULL)
		return mrsp_tasks[start_index + 42];
	if (mrsp_tasks[start_index + 43] != NULL)
		return mrsp_tasks[start_index + 43];
	if (mrsp_tasks[start_index + 44] != NULL)
		return mrsp_tasks[start_index + 44];
	if (mrsp_tasks[start_index + 45] != NULL)
		return mrsp_tasks[start_index + 45];
	if (mrsp_tasks[start_index + 46] != NULL)
		return mrsp_tasks[start_index + 46];
	if (mrsp_tasks[start_index + 47] != NULL)
		return mrsp_tasks[start_index + 47];
	if (mrsp_tasks[start_index + 48] != NULL)
		return mrsp_tasks[start_index + 48];
	if (mrsp_tasks[start_index + 49] != NULL)
		return mrsp_tasks[start_index + 49];
	if (mrsp_tasks[start_index + 50] != NULL)
		return mrsp_tasks[start_index + 50];
	if (mrsp_tasks[start_index + 51] != NULL)
		return mrsp_tasks[start_index + 51];
	if (mrsp_tasks[start_index + 52] != NULL)
		return mrsp_tasks[start_index + 52];
	if (mrsp_tasks[start_index + 53] != NULL)
		return mrsp_tasks[start_index + 53];
	if (mrsp_tasks[start_index + 54] != NULL)
		return mrsp_tasks[start_index + 54];
	if (mrsp_tasks[start_index + 55] != NULL)
		return mrsp_tasks[start_index + 55];
	if (mrsp_tasks[start_index + 56] != NULL)
		return mrsp_tasks[start_index + 56];
	if (mrsp_tasks[start_index + 57] != NULL)
		return mrsp_tasks[start_index + 57];
	if (mrsp_tasks[start_index + 58] != NULL)
		return mrsp_tasks[start_index + 58];
	if (mrsp_tasks[start_index + 59] != NULL)
		return mrsp_tasks[start_index + 59];
	if (mrsp_tasks[start_index + 60] != NULL)
		return mrsp_tasks[start_index + 60];
	if (mrsp_tasks[start_index + 61] != NULL)
		return mrsp_tasks[start_index + 61];
	if (mrsp_tasks[start_index + 62] != NULL)
		return mrsp_tasks[start_index + 62];
	if (mrsp_tasks[start_index + 63] != NULL)
		return mrsp_tasks[start_index + 63];
	if (mrsp_tasks[start_index + 64] != NULL)
		return mrsp_tasks[start_index + 64];
	if (mrsp_tasks[start_index + 65] != NULL)
		return mrsp_tasks[start_index + 65];
	if (mrsp_tasks[start_index + 66] != NULL)
		return mrsp_tasks[start_index + 66];
	if (mrsp_tasks[start_index + 67] != NULL)
		return mrsp_tasks[start_index + 67];
	if (mrsp_tasks[start_index + 68] != NULL)
		return mrsp_tasks[start_index + 68];
	if (mrsp_tasks[start_index + 69] != NULL)
		return mrsp_tasks[start_index + 69];
	if (mrsp_tasks[start_index + 70] != NULL)
		return mrsp_tasks[start_index + 70];
	if (mrsp_tasks[start_index + 71] != NULL)
		return mrsp_tasks[start_index + 71];
	if (mrsp_tasks[start_index + 72] != NULL)
		return mrsp_tasks[start_index + 72];
	if (mrsp_tasks[start_index + 73] != NULL)
		return mrsp_tasks[start_index + 73];
	if (mrsp_tasks[start_index + 74] != NULL)
		return mrsp_tasks[start_index + 74];
	if (mrsp_tasks[start_index + 75] != NULL)
		return mrsp_tasks[start_index + 75];
	if (mrsp_tasks[start_index + 76] != NULL)
		return mrsp_tasks[start_index + 76];
	if (mrsp_tasks[start_index + 77] != NULL)
		return mrsp_tasks[start_index + 77];
	if (mrsp_tasks[start_index + 78] != NULL)
		return mrsp_tasks[start_index + 78];
	if (mrsp_tasks[start_index + 79] != NULL)
		return mrsp_tasks[start_index + 79];
	if (mrsp_tasks[start_index + 80] != NULL)
		return mrsp_tasks[start_index + 80];
	if (mrsp_tasks[start_index + 81] != NULL)
		return mrsp_tasks[start_index + 81];
	if (mrsp_tasks[start_index + 82] != NULL)
		return mrsp_tasks[start_index + 82];
	if (mrsp_tasks[start_index + 83] != NULL)
		return mrsp_tasks[start_index + 83];
	if (mrsp_tasks[start_index + 84] != NULL)
		return mrsp_tasks[start_index + 84];
	if (mrsp_tasks[start_index + 85] != NULL)
		return mrsp_tasks[start_index + 85];
	if (mrsp_tasks[start_index + 86] != NULL)
		return mrsp_tasks[start_index + 86];
	if (mrsp_tasks[start_index + 87] != NULL)
		return mrsp_tasks[start_index + 87];
	if (mrsp_tasks[start_index + 88] != NULL)
		return mrsp_tasks[start_index + 88];
	if (mrsp_tasks[start_index + 89] != NULL)
		return mrsp_tasks[start_index + 89];
	if (mrsp_tasks[start_index + 90] != NULL)
		return mrsp_tasks[start_index + 90];
	if (mrsp_tasks[start_index + 91] != NULL)
		return mrsp_tasks[start_index + 91];
	if (mrsp_tasks[start_index + 92] != NULL)
		return mrsp_tasks[start_index + 92];
	if (mrsp_tasks[start_index + 93] != NULL)
		return mrsp_tasks[start_index + 93];
	if (mrsp_tasks[start_index + 94] != NULL)
		return mrsp_tasks[start_index + 94];
	if (mrsp_tasks[start_index + 95] != NULL)
		return mrsp_tasks[start_index + 95];
	if (mrsp_tasks[start_index + 96] != NULL)
		return mrsp_tasks[start_index + 96];
	if (mrsp_tasks[start_index + 97] != NULL)
		return mrsp_tasks[start_index + 97];
	if (mrsp_tasks[start_index + 98] != NULL)
		return mrsp_tasks[start_index + 98];
	if (mrsp_tasks[start_index + 99] != NULL)
		return mrsp_tasks[start_index + 99];
	if (mrsp_tasks[start_index + 100] != NULL)
		return mrsp_tasks[start_index + 100];
	if (mrsp_tasks[start_index + 101] != NULL)
		return mrsp_tasks[start_index + 101];
	if (mrsp_tasks[start_index + 102] != NULL)
		return mrsp_tasks[start_index + 102];
	if (mrsp_tasks[start_index + 103] != NULL)
		return mrsp_tasks[start_index + 103];
	if (mrsp_tasks[start_index + 104] != NULL)
		return mrsp_tasks[start_index + 104];
	if (mrsp_tasks[start_index + 105] != NULL)
		return mrsp_tasks[start_index + 105];
	if (mrsp_tasks[start_index + 106] != NULL)
		return mrsp_tasks[start_index + 106];
	if (mrsp_tasks[start_index + 107] != NULL)
		return mrsp_tasks[start_index + 107];
	if (mrsp_tasks[start_index + 108] != NULL)
		return mrsp_tasks[start_index + 108];
	if (mrsp_tasks[start_index + 109] != NULL)
		return mrsp_tasks[start_index + 109];
	if (mrsp_tasks[start_index + 110] != NULL)
		return mrsp_tasks[start_index + 110];
	if (mrsp_tasks[start_index + 111] != NULL)
		return mrsp_tasks[start_index + 111];
	if (mrsp_tasks[start_index + 112] != NULL)
		return mrsp_tasks[start_index + 112];
	if (mrsp_tasks[start_index + 113] != NULL)
		return mrsp_tasks[start_index + 113];
	if (mrsp_tasks[start_index + 114] != NULL)
		return mrsp_tasks[start_index + 114];
	if (mrsp_tasks[start_index + 115] != NULL)
		return mrsp_tasks[start_index + 115];
	if (mrsp_tasks[start_index + 116] != NULL)
		return mrsp_tasks[start_index + 116];
	if (mrsp_tasks[start_index + 117] != NULL)
		return mrsp_tasks[start_index + 117];
	if (mrsp_tasks[start_index + 118] != NULL)
		return mrsp_tasks[start_index + 118];
	if (mrsp_tasks[start_index + 119] != NULL)
		return mrsp_tasks[start_index + 119];
	if (mrsp_tasks[start_index + 120] != NULL)
		return mrsp_tasks[start_index + 120];
	if (mrsp_tasks[start_index + 121] != NULL)
		return mrsp_tasks[start_index + 121];
	if (mrsp_tasks[start_index + 122] != NULL)
		return mrsp_tasks[start_index + 122];
	if (mrsp_tasks[start_index + 123] != NULL)
		return mrsp_tasks[start_index + 123];
	if (mrsp_tasks[start_index + 124] != NULL)
		return mrsp_tasks[start_index + 124];
	if (mrsp_tasks[start_index + 125] != NULL)
		return mrsp_tasks[start_index + 125];
	if (mrsp_tasks[start_index + 126] != NULL)
		return mrsp_tasks[start_index + 126];
	if (mrsp_tasks[start_index + 127] != NULL)
		return mrsp_tasks[start_index + 127];
	if (mrsp_tasks[start_index + 128] != NULL)
		return mrsp_tasks[start_index + 128];
	if (mrsp_tasks[start_index + 129] != NULL)
		return mrsp_tasks[start_index + 129];
	if (mrsp_tasks[start_index + 130] != NULL)
		return mrsp_tasks[start_index + 130];
	if (mrsp_tasks[start_index + 131] != NULL)
		return mrsp_tasks[start_index + 131];
	if (mrsp_tasks[start_index + 132] != NULL)
		return mrsp_tasks[start_index + 132];
	if (mrsp_tasks[start_index + 133] != NULL)
		return mrsp_tasks[start_index + 133];
	if (mrsp_tasks[start_index + 134] != NULL)
		return mrsp_tasks[start_index + 134];
	if (mrsp_tasks[start_index + 135] != NULL)
		return mrsp_tasks[start_index + 135];
	if (mrsp_tasks[start_index + 136] != NULL)
		return mrsp_tasks[start_index + 136];
	if (mrsp_tasks[start_index + 137] != NULL)
		return mrsp_tasks[start_index + 137];
	if (mrsp_tasks[start_index + 138] != NULL)
		return mrsp_tasks[start_index + 138];
	if (mrsp_tasks[start_index + 139] != NULL)
		return mrsp_tasks[start_index + 139];
	if (mrsp_tasks[start_index + 140] != NULL)
		return mrsp_tasks[start_index + 140];
	if (mrsp_tasks[start_index + 141] != NULL)
		return mrsp_tasks[start_index + 141];
	if (mrsp_tasks[start_index + 142] != NULL)
		return mrsp_tasks[start_index + 142];
	if (mrsp_tasks[start_index + 143] != NULL)
		return mrsp_tasks[start_index + 143];
	if (mrsp_tasks[start_index + 144] != NULL)
		return mrsp_tasks[start_index + 144];
	if (mrsp_tasks[start_index + 145] != NULL)
		return mrsp_tasks[start_index + 145];
	if (mrsp_tasks[start_index + 146] != NULL)
		return mrsp_tasks[start_index + 146];
	if (mrsp_tasks[start_index + 147] != NULL)
		return mrsp_tasks[start_index + 147];
	if (mrsp_tasks[start_index + 148] != NULL)
		return mrsp_tasks[start_index + 148];
	if (mrsp_tasks[start_index + 149] != NULL)
		return mrsp_tasks[start_index + 149];
	if (mrsp_tasks[start_index + 150] != NULL)
		return mrsp_tasks[start_index + 150];
	if (mrsp_tasks[start_index + 151] != NULL)
		return mrsp_tasks[start_index + 151];
	if (mrsp_tasks[start_index + 152] != NULL)
		return mrsp_tasks[start_index + 152];
	if (mrsp_tasks[start_index + 153] != NULL)
		return mrsp_tasks[start_index + 153];
	if (mrsp_tasks[start_index + 154] != NULL)
		return mrsp_tasks[start_index + 154];
	if (mrsp_tasks[start_index + 155] != NULL)
		return mrsp_tasks[start_index + 155];
	if (mrsp_tasks[start_index + 156] != NULL)
		return mrsp_tasks[start_index + 156];
	if (mrsp_tasks[start_index + 157] != NULL)
		return mrsp_tasks[start_index + 157];
	if (mrsp_tasks[start_index + 158] != NULL)
		return mrsp_tasks[start_index + 158];
	if (mrsp_tasks[start_index + 159] != NULL)
		return mrsp_tasks[start_index + 159];
	if (mrsp_tasks[start_index + 160] != NULL)
		return mrsp_tasks[start_index + 160];
	if (mrsp_tasks[start_index + 161] != NULL)
		return mrsp_tasks[start_index + 161];
	if (mrsp_tasks[start_index + 162] != NULL)
		return mrsp_tasks[start_index + 162];
	if (mrsp_tasks[start_index + 163] != NULL)
		return mrsp_tasks[start_index + 163];
	if (mrsp_tasks[start_index + 164] != NULL)
		return mrsp_tasks[start_index + 164];
	if (mrsp_tasks[start_index + 165] != NULL)
		return mrsp_tasks[start_index + 165];
	if (mrsp_tasks[start_index + 166] != NULL)
		return mrsp_tasks[start_index + 166];
	if (mrsp_tasks[start_index + 167] != NULL)
		return mrsp_tasks[start_index + 167];
	if (mrsp_tasks[start_index + 168] != NULL)
		return mrsp_tasks[start_index + 168];
	if (mrsp_tasks[start_index + 169] != NULL)
		return mrsp_tasks[start_index + 169];
	if (mrsp_tasks[start_index + 170] != NULL)
		return mrsp_tasks[start_index + 170];
	if (mrsp_tasks[start_index + 171] != NULL)
		return mrsp_tasks[start_index + 171];
	if (mrsp_tasks[start_index + 172] != NULL)
		return mrsp_tasks[start_index + 172];
	if (mrsp_tasks[start_index + 173] != NULL)
		return mrsp_tasks[start_index + 173];
	if (mrsp_tasks[start_index + 174] != NULL)
		return mrsp_tasks[start_index + 174];
	if (mrsp_tasks[start_index + 175] != NULL)
		return mrsp_tasks[start_index + 175];
	if (mrsp_tasks[start_index + 176] != NULL)
		return mrsp_tasks[start_index + 176];
	if (mrsp_tasks[start_index + 177] != NULL)
		return mrsp_tasks[start_index + 177];
	if (mrsp_tasks[start_index + 178] != NULL)
		return mrsp_tasks[start_index + 178];
	if (mrsp_tasks[start_index + 179] != NULL)
		return mrsp_tasks[start_index + 179];
	if (mrsp_tasks[start_index + 180] != NULL)
		return mrsp_tasks[start_index + 180];
	if (mrsp_tasks[start_index + 181] != NULL)
		return mrsp_tasks[start_index + 181];
	if (mrsp_tasks[start_index + 182] != NULL)
		return mrsp_tasks[start_index + 182];
	if (mrsp_tasks[start_index + 183] != NULL)
		return mrsp_tasks[start_index + 183];
	if (mrsp_tasks[start_index + 184] != NULL)
		return mrsp_tasks[start_index + 184];
	if (mrsp_tasks[start_index + 185] != NULL)
		return mrsp_tasks[start_index + 185];
	if (mrsp_tasks[start_index + 186] != NULL)
		return mrsp_tasks[start_index + 186];
	if (mrsp_tasks[start_index + 187] != NULL)
		return mrsp_tasks[start_index + 187];
	if (mrsp_tasks[start_index + 188] != NULL)
		return mrsp_tasks[start_index + 188];
	if (mrsp_tasks[start_index + 189] != NULL)
		return mrsp_tasks[start_index + 189];
	if (mrsp_tasks[start_index + 190] != NULL)
		return mrsp_tasks[start_index + 190];
	if (mrsp_tasks[start_index + 191] != NULL)
		return mrsp_tasks[start_index + 191];
	if (mrsp_tasks[start_index + 192] != NULL)
		return mrsp_tasks[start_index + 192];
	if (mrsp_tasks[start_index + 193] != NULL)
		return mrsp_tasks[start_index + 193];
	if (mrsp_tasks[start_index + 194] != NULL)
		return mrsp_tasks[start_index + 194];
	if (mrsp_tasks[start_index + 195] != NULL)
		return mrsp_tasks[start_index + 195];
	if (mrsp_tasks[start_index + 196] != NULL)
		return mrsp_tasks[start_index + 196];
	if (mrsp_tasks[start_index + 197] != NULL)
		return mrsp_tasks[start_index + 197];
	if (mrsp_tasks[start_index + 198] != NULL)
		return mrsp_tasks[start_index + 198];
	if (mrsp_tasks[start_index + 199] != NULL)
		return mrsp_tasks[start_index + 199];
	if (mrsp_tasks[start_index + 200] != NULL)
		return mrsp_tasks[start_index + 200];
	if (mrsp_tasks[start_index + 201] != NULL)
		return mrsp_tasks[start_index + 201];
	if (mrsp_tasks[start_index + 202] != NULL)
		return mrsp_tasks[start_index + 202];
	if (mrsp_tasks[start_index + 203] != NULL)
		return mrsp_tasks[start_index + 203];
	if (mrsp_tasks[start_index + 204] != NULL)
		return mrsp_tasks[start_index + 204];
	if (mrsp_tasks[start_index + 205] != NULL)
		return mrsp_tasks[start_index + 205];
	if (mrsp_tasks[start_index + 206] != NULL)
		return mrsp_tasks[start_index + 206];
	if (mrsp_tasks[start_index + 207] != NULL)
		return mrsp_tasks[start_index + 207];
	if (mrsp_tasks[start_index + 208] != NULL)
		return mrsp_tasks[start_index + 208];
	if (mrsp_tasks[start_index + 209] != NULL)
		return mrsp_tasks[start_index + 209];
	if (mrsp_tasks[start_index + 210] != NULL)
		return mrsp_tasks[start_index + 210];
	if (mrsp_tasks[start_index + 211] != NULL)
		return mrsp_tasks[start_index + 211];
	if (mrsp_tasks[start_index + 212] != NULL)
		return mrsp_tasks[start_index + 212];
	if (mrsp_tasks[start_index + 213] != NULL)
		return mrsp_tasks[start_index + 213];
	if (mrsp_tasks[start_index + 214] != NULL)
		return mrsp_tasks[start_index + 214];
	if (mrsp_tasks[start_index + 215] != NULL)
		return mrsp_tasks[start_index + 215];
	if (mrsp_tasks[start_index + 216] != NULL)
		return mrsp_tasks[start_index + 216];
	if (mrsp_tasks[start_index + 217] != NULL)
		return mrsp_tasks[start_index + 217];
	if (mrsp_tasks[start_index + 218] != NULL)
		return mrsp_tasks[start_index + 218];
	if (mrsp_tasks[start_index + 219] != NULL)
		return mrsp_tasks[start_index + 219];
	if (mrsp_tasks[start_index + 220] != NULL)
		return mrsp_tasks[start_index + 220];
	if (mrsp_tasks[start_index + 221] != NULL)
		return mrsp_tasks[start_index + 221];
	if (mrsp_tasks[start_index + 222] != NULL)
		return mrsp_tasks[start_index + 222];
	if (mrsp_tasks[start_index + 223] != NULL)
		return mrsp_tasks[start_index + 223];
	if (mrsp_tasks[start_index + 224] != NULL)
		return mrsp_tasks[start_index + 224];
	if (mrsp_tasks[start_index + 225] != NULL)
		return mrsp_tasks[start_index + 225];
	if (mrsp_tasks[start_index + 226] != NULL)
		return mrsp_tasks[start_index + 226];
	if (mrsp_tasks[start_index + 227] != NULL)
		return mrsp_tasks[start_index + 227];
	if (mrsp_tasks[start_index + 228] != NULL)
		return mrsp_tasks[start_index + 228];
	if (mrsp_tasks[start_index + 229] != NULL)
		return mrsp_tasks[start_index + 229];
	if (mrsp_tasks[start_index + 230] != NULL)
		return mrsp_tasks[start_index + 230];
	if (mrsp_tasks[start_index + 231] != NULL)
		return mrsp_tasks[start_index + 231];
	if (mrsp_tasks[start_index + 232] != NULL)
		return mrsp_tasks[start_index + 232];
	if (mrsp_tasks[start_index + 233] != NULL)
		return mrsp_tasks[start_index + 233];
	if (mrsp_tasks[start_index + 234] != NULL)
		return mrsp_tasks[start_index + 234];
	if (mrsp_tasks[start_index + 235] != NULL)
		return mrsp_tasks[start_index + 235];
	if (mrsp_tasks[start_index + 236] != NULL)
		return mrsp_tasks[start_index + 236];
	if (mrsp_tasks[start_index + 237] != NULL)
		return mrsp_tasks[start_index + 237];
	if (mrsp_tasks[start_index + 238] != NULL)
		return mrsp_tasks[start_index + 238];
	if (mrsp_tasks[start_index + 239] != NULL)
		return mrsp_tasks[start_index + 239];
	if (mrsp_tasks[start_index + 240] != NULL)
		return mrsp_tasks[start_index + 240];
	if (mrsp_tasks[start_index + 241] != NULL)
		return mrsp_tasks[start_index + 241];
	if (mrsp_tasks[start_index + 242] != NULL)
		return mrsp_tasks[start_index + 242];
	if (mrsp_tasks[start_index + 243] != NULL)
		return mrsp_tasks[start_index + 243];
	if (mrsp_tasks[start_index + 244] != NULL)
		return mrsp_tasks[start_index + 244];
	if (mrsp_tasks[start_index + 245] != NULL)
		return mrsp_tasks[start_index + 245];
	if (mrsp_tasks[start_index + 246] != NULL)
		return mrsp_tasks[start_index + 246];
	if (mrsp_tasks[start_index + 247] != NULL)
		return mrsp_tasks[start_index + 247];
	if (mrsp_tasks[start_index + 248] != NULL)
		return mrsp_tasks[start_index + 248];
	if (mrsp_tasks[start_index + 249] != NULL)
		return mrsp_tasks[start_index + 249];
	if (mrsp_tasks[start_index + 250] != NULL)
		return mrsp_tasks[start_index + 250];
	if (mrsp_tasks[start_index + 251] != NULL)
		return mrsp_tasks[start_index + 251];
	if (mrsp_tasks[start_index + 252] != NULL)
		return mrsp_tasks[start_index + 252];
	if (mrsp_tasks[start_index + 253] != NULL)
		return mrsp_tasks[start_index + 253];
	if (mrsp_tasks[start_index + 254] != NULL)
		return mrsp_tasks[start_index + 254];
	if (mrsp_tasks[start_index + 255] != NULL)
		return mrsp_tasks[start_index + 255];
	if (mrsp_tasks[start_index + 256] != NULL)
		return mrsp_tasks[start_index + 256];
	if (mrsp_tasks[start_index + 257] != NULL)
		return mrsp_tasks[start_index + 257];
	if (mrsp_tasks[start_index + 258] != NULL)
		return mrsp_tasks[start_index + 258];
	if (mrsp_tasks[start_index + 259] != NULL)
		return mrsp_tasks[start_index + 259];
	if (mrsp_tasks[start_index + 260] != NULL)
		return mrsp_tasks[start_index + 260];
	if (mrsp_tasks[start_index + 261] != NULL)
		return mrsp_tasks[start_index + 261];
	if (mrsp_tasks[start_index + 262] != NULL)
		return mrsp_tasks[start_index + 262];
	if (mrsp_tasks[start_index + 263] != NULL)
		return mrsp_tasks[start_index + 263];
	if (mrsp_tasks[start_index + 264] != NULL)
		return mrsp_tasks[start_index + 264];
	if (mrsp_tasks[start_index + 265] != NULL)
		return mrsp_tasks[start_index + 265];
	if (mrsp_tasks[start_index + 266] != NULL)
		return mrsp_tasks[start_index + 266];
	if (mrsp_tasks[start_index + 267] != NULL)
		return mrsp_tasks[start_index + 267];
	if (mrsp_tasks[start_index + 268] != NULL)
		return mrsp_tasks[start_index + 268];
	if (mrsp_tasks[start_index + 269] != NULL)
		return mrsp_tasks[start_index + 269];
	if (mrsp_tasks[start_index + 270] != NULL)
		return mrsp_tasks[start_index + 270];
	if (mrsp_tasks[start_index + 271] != NULL)
		return mrsp_tasks[start_index + 271];
	if (mrsp_tasks[start_index + 272] != NULL)
		return mrsp_tasks[start_index + 272];
	if (mrsp_tasks[start_index + 273] != NULL)
		return mrsp_tasks[start_index + 273];
	if (mrsp_tasks[start_index + 274] != NULL)
		return mrsp_tasks[start_index + 274];
	if (mrsp_tasks[start_index + 275] != NULL)
		return mrsp_tasks[start_index + 275];
	if (mrsp_tasks[start_index + 276] != NULL)
		return mrsp_tasks[start_index + 276];
	if (mrsp_tasks[start_index + 277] != NULL)
		return mrsp_tasks[start_index + 277];
	if (mrsp_tasks[start_index + 278] != NULL)
		return mrsp_tasks[start_index + 278];
	if (mrsp_tasks[start_index + 279] != NULL)
		return mrsp_tasks[start_index + 279];
	if (mrsp_tasks[start_index + 280] != NULL)
		return mrsp_tasks[start_index + 280];
	if (mrsp_tasks[start_index + 281] != NULL)
		return mrsp_tasks[start_index + 281];
	if (mrsp_tasks[start_index + 282] != NULL)
		return mrsp_tasks[start_index + 282];
	if (mrsp_tasks[start_index + 283] != NULL)
		return mrsp_tasks[start_index + 283];
	if (mrsp_tasks[start_index + 284] != NULL)
		return mrsp_tasks[start_index + 284];
	if (mrsp_tasks[start_index + 285] != NULL)
		return mrsp_tasks[start_index + 285];
	if (mrsp_tasks[start_index + 286] != NULL)
		return mrsp_tasks[start_index + 286];
	if (mrsp_tasks[start_index + 287] != NULL)
		return mrsp_tasks[start_index + 287];
	if (mrsp_tasks[start_index + 288] != NULL)
		return mrsp_tasks[start_index + 288];
	if (mrsp_tasks[start_index + 289] != NULL)
		return mrsp_tasks[start_index + 289];
	if (mrsp_tasks[start_index + 290] != NULL)
		return mrsp_tasks[start_index + 290];
	if (mrsp_tasks[start_index + 291] != NULL)
		return mrsp_tasks[start_index + 291];
	if (mrsp_tasks[start_index + 292] != NULL)
		return mrsp_tasks[start_index + 292];
	if (mrsp_tasks[start_index + 293] != NULL)
		return mrsp_tasks[start_index + 293];
	if (mrsp_tasks[start_index + 294] != NULL)
		return mrsp_tasks[start_index + 294];
	if (mrsp_tasks[start_index + 295] != NULL)
		return mrsp_tasks[start_index + 295];
	if (mrsp_tasks[start_index + 296] != NULL)
		return mrsp_tasks[start_index + 296];
	if (mrsp_tasks[start_index + 297] != NULL)
		return mrsp_tasks[start_index + 297];
	if (mrsp_tasks[start_index + 298] != NULL)
		return mrsp_tasks[start_index + 298];
	if (mrsp_tasks[start_index + 299] != NULL)
		return mrsp_tasks[start_index + 299];
	if (mrsp_tasks[start_index + 300] != NULL)
		return mrsp_tasks[start_index + 300];
	if (mrsp_tasks[start_index + 301] != NULL)
		return mrsp_tasks[start_index + 301];
	if (mrsp_tasks[start_index + 302] != NULL)
		return mrsp_tasks[start_index + 302];
	if (mrsp_tasks[start_index + 303] != NULL)
		return mrsp_tasks[start_index + 303];
	if (mrsp_tasks[start_index + 304] != NULL)
		return mrsp_tasks[start_index + 304];
	if (mrsp_tasks[start_index + 305] != NULL)
		return mrsp_tasks[start_index + 305];
	if (mrsp_tasks[start_index + 306] != NULL)
		return mrsp_tasks[start_index + 306];
	if (mrsp_tasks[start_index + 307] != NULL)
		return mrsp_tasks[start_index + 307];
	if (mrsp_tasks[start_index + 308] != NULL)
		return mrsp_tasks[start_index + 308];
	if (mrsp_tasks[start_index + 309] != NULL)
		return mrsp_tasks[start_index + 309];
	if (mrsp_tasks[start_index + 310] != NULL)
		return mrsp_tasks[start_index + 310];
	if (mrsp_tasks[start_index + 311] != NULL)
		return mrsp_tasks[start_index + 311];
	if (mrsp_tasks[start_index + 312] != NULL)
		return mrsp_tasks[start_index + 312];
	if (mrsp_tasks[start_index + 313] != NULL)
		return mrsp_tasks[start_index + 313];
	if (mrsp_tasks[start_index + 314] != NULL)
		return mrsp_tasks[start_index + 314];
	if (mrsp_tasks[start_index + 315] != NULL)
		return mrsp_tasks[start_index + 315];
	if (mrsp_tasks[start_index + 316] != NULL)
		return mrsp_tasks[start_index + 316];
	if (mrsp_tasks[start_index + 317] != NULL)
		return mrsp_tasks[start_index + 317];
	if (mrsp_tasks[start_index + 318] != NULL)
		return mrsp_tasks[start_index + 318];
	if (mrsp_tasks[start_index + 319] != NULL)
		return mrsp_tasks[start_index + 319];
	if (mrsp_tasks[start_index + 320] != NULL)
		return mrsp_tasks[start_index + 320];
	if (mrsp_tasks[start_index + 321] != NULL)
		return mrsp_tasks[start_index + 321];
	if (mrsp_tasks[start_index + 322] != NULL)
		return mrsp_tasks[start_index + 322];
	if (mrsp_tasks[start_index + 323] != NULL)
		return mrsp_tasks[start_index + 323];
	if (mrsp_tasks[start_index + 324] != NULL)
		return mrsp_tasks[start_index + 324];
	if (mrsp_tasks[start_index + 325] != NULL)
		return mrsp_tasks[start_index + 325];
	if (mrsp_tasks[start_index + 326] != NULL)
		return mrsp_tasks[start_index + 326];
	if (mrsp_tasks[start_index + 327] != NULL)
		return mrsp_tasks[start_index + 327];
	if (mrsp_tasks[start_index + 328] != NULL)
		return mrsp_tasks[start_index + 328];
	if (mrsp_tasks[start_index + 329] != NULL)
		return mrsp_tasks[start_index + 329];
	if (mrsp_tasks[start_index + 330] != NULL)
		return mrsp_tasks[start_index + 330];
	if (mrsp_tasks[start_index + 331] != NULL)
		return mrsp_tasks[start_index + 331];
	if (mrsp_tasks[start_index + 332] != NULL)
		return mrsp_tasks[start_index + 332];
	if (mrsp_tasks[start_index + 333] != NULL)
		return mrsp_tasks[start_index + 333];
	if (mrsp_tasks[start_index + 334] != NULL)
		return mrsp_tasks[start_index + 334];
	if (mrsp_tasks[start_index + 335] != NULL)
		return mrsp_tasks[start_index + 335];
	if (mrsp_tasks[start_index + 336] != NULL)
		return mrsp_tasks[start_index + 336];
	if (mrsp_tasks[start_index + 337] != NULL)
		return mrsp_tasks[start_index + 337];
	if (mrsp_tasks[start_index + 338] != NULL)
		return mrsp_tasks[start_index + 338];
	if (mrsp_tasks[start_index + 339] != NULL)
		return mrsp_tasks[start_index + 339];
	if (mrsp_tasks[start_index + 340] != NULL)
		return mrsp_tasks[start_index + 340];
	if (mrsp_tasks[start_index + 341] != NULL)
		return mrsp_tasks[start_index + 341];
	if (mrsp_tasks[start_index + 342] != NULL)
		return mrsp_tasks[start_index + 342];
	if (mrsp_tasks[start_index + 343] != NULL)
		return mrsp_tasks[start_index + 343];
	if (mrsp_tasks[start_index + 344] != NULL)
		return mrsp_tasks[start_index + 344];
	if (mrsp_tasks[start_index + 345] != NULL)
		return mrsp_tasks[start_index + 345];
	if (mrsp_tasks[start_index + 346] != NULL)
		return mrsp_tasks[start_index + 346];
	if (mrsp_tasks[start_index + 347] != NULL)
		return mrsp_tasks[start_index + 347];
	if (mrsp_tasks[start_index + 348] != NULL)
		return mrsp_tasks[start_index + 348];
	if (mrsp_tasks[start_index + 349] != NULL)
		return mrsp_tasks[start_index + 349];
	if (mrsp_tasks[start_index + 350] != NULL)
		return mrsp_tasks[start_index + 350];
	if (mrsp_tasks[start_index + 351] != NULL)
		return mrsp_tasks[start_index + 351];
	if (mrsp_tasks[start_index + 352] != NULL)
		return mrsp_tasks[start_index + 352];
	if (mrsp_tasks[start_index + 353] != NULL)
		return mrsp_tasks[start_index + 353];
	if (mrsp_tasks[start_index + 354] != NULL)
		return mrsp_tasks[start_index + 354];
	if (mrsp_tasks[start_index + 355] != NULL)
		return mrsp_tasks[start_index + 355];
	if (mrsp_tasks[start_index + 356] != NULL)
		return mrsp_tasks[start_index + 356];
	if (mrsp_tasks[start_index + 357] != NULL)
		return mrsp_tasks[start_index + 357];
	if (mrsp_tasks[start_index + 358] != NULL)
		return mrsp_tasks[start_index + 358];
	if (mrsp_tasks[start_index + 359] != NULL)
		return mrsp_tasks[start_index + 359];
	if (mrsp_tasks[start_index + 360] != NULL)
		return mrsp_tasks[start_index + 360];
	if (mrsp_tasks[start_index + 361] != NULL)
		return mrsp_tasks[start_index + 361];
	if (mrsp_tasks[start_index + 362] != NULL)
		return mrsp_tasks[start_index + 362];
	if (mrsp_tasks[start_index + 363] != NULL)
		return mrsp_tasks[start_index + 363];
	if (mrsp_tasks[start_index + 364] != NULL)
		return mrsp_tasks[start_index + 364];
	if (mrsp_tasks[start_index + 365] != NULL)
		return mrsp_tasks[start_index + 365];
	if (mrsp_tasks[start_index + 366] != NULL)
		return mrsp_tasks[start_index + 366];
	if (mrsp_tasks[start_index + 367] != NULL)
		return mrsp_tasks[start_index + 367];
	if (mrsp_tasks[start_index + 368] != NULL)
		return mrsp_tasks[start_index + 368];
	if (mrsp_tasks[start_index + 369] != NULL)
		return mrsp_tasks[start_index + 369];
	if (mrsp_tasks[start_index + 370] != NULL)
		return mrsp_tasks[start_index + 370];
	if (mrsp_tasks[start_index + 371] != NULL)
		return mrsp_tasks[start_index + 371];
	if (mrsp_tasks[start_index + 372] != NULL)
		return mrsp_tasks[start_index + 372];
	if (mrsp_tasks[start_index + 373] != NULL)
		return mrsp_tasks[start_index + 373];
	if (mrsp_tasks[start_index + 374] != NULL)
		return mrsp_tasks[start_index + 374];
	if (mrsp_tasks[start_index + 375] != NULL)
		return mrsp_tasks[start_index + 375];
	if (mrsp_tasks[start_index + 376] != NULL)
		return mrsp_tasks[start_index + 376];
	if (mrsp_tasks[start_index + 377] != NULL)
		return mrsp_tasks[start_index + 377];
	if (mrsp_tasks[start_index + 378] != NULL)
		return mrsp_tasks[start_index + 378];
	if (mrsp_tasks[start_index + 379] != NULL)
		return mrsp_tasks[start_index + 379];
	if (mrsp_tasks[start_index + 380] != NULL)
		return mrsp_tasks[start_index + 380];
	if (mrsp_tasks[start_index + 381] != NULL)
		return mrsp_tasks[start_index + 381];
	if (mrsp_tasks[start_index + 382] != NULL)
		return mrsp_tasks[start_index + 382];
	if (mrsp_tasks[start_index + 383] != NULL)
		return mrsp_tasks[start_index + 383];
	if (mrsp_tasks[start_index + 384] != NULL)
		return mrsp_tasks[start_index + 384];
	if (mrsp_tasks[start_index + 385] != NULL)
		return mrsp_tasks[start_index + 385];
	if (mrsp_tasks[start_index + 386] != NULL)
		return mrsp_tasks[start_index + 386];
	if (mrsp_tasks[start_index + 387] != NULL)
		return mrsp_tasks[start_index + 387];
	if (mrsp_tasks[start_index + 388] != NULL)
		return mrsp_tasks[start_index + 388];
	if (mrsp_tasks[start_index + 389] != NULL)
		return mrsp_tasks[start_index + 389];
	if (mrsp_tasks[start_index + 390] != NULL)
		return mrsp_tasks[start_index + 390];
	if (mrsp_tasks[start_index + 391] != NULL)
		return mrsp_tasks[start_index + 391];
	if (mrsp_tasks[start_index + 392] != NULL)
		return mrsp_tasks[start_index + 392];
	if (mrsp_tasks[start_index + 393] != NULL)
		return mrsp_tasks[start_index + 393];
	if (mrsp_tasks[start_index + 394] != NULL)
		return mrsp_tasks[start_index + 394];
	if (mrsp_tasks[start_index + 395] != NULL)
		return mrsp_tasks[start_index + 395];
	if (mrsp_tasks[start_index + 396] != NULL)
		return mrsp_tasks[start_index + 396];
	if (mrsp_tasks[start_index + 397] != NULL)
		return mrsp_tasks[start_index + 397];
	if (mrsp_tasks[start_index + 398] != NULL)
		return mrsp_tasks[start_index + 398];
	if (mrsp_tasks[start_index + 399] != NULL)
		return mrsp_tasks[start_index + 399];
	if (mrsp_tasks[start_index + 400] != NULL)
		return mrsp_tasks[start_index + 400];
	if (mrsp_tasks[start_index + 401] != NULL)
		return mrsp_tasks[start_index + 401];
	if (mrsp_tasks[start_index + 402] != NULL)
		return mrsp_tasks[start_index + 402];
	if (mrsp_tasks[start_index + 403] != NULL)
		return mrsp_tasks[start_index + 403];
	if (mrsp_tasks[start_index + 404] != NULL)
		return mrsp_tasks[start_index + 404];
	if (mrsp_tasks[start_index + 405] != NULL)
		return mrsp_tasks[start_index + 405];
	if (mrsp_tasks[start_index + 406] != NULL)
		return mrsp_tasks[start_index + 406];
	if (mrsp_tasks[start_index + 407] != NULL)
		return mrsp_tasks[start_index + 407];
	if (mrsp_tasks[start_index + 408] != NULL)
		return mrsp_tasks[start_index + 408];
	if (mrsp_tasks[start_index + 409] != NULL)
		return mrsp_tasks[start_index + 409];
	if (mrsp_tasks[start_index + 410] != NULL)
		return mrsp_tasks[start_index + 410];
	if (mrsp_tasks[start_index + 411] != NULL)
		return mrsp_tasks[start_index + 411];
	if (mrsp_tasks[start_index + 412] != NULL)
		return mrsp_tasks[start_index + 412];
	if (mrsp_tasks[start_index + 413] != NULL)
		return mrsp_tasks[start_index + 413];
	if (mrsp_tasks[start_index + 414] != NULL)
		return mrsp_tasks[start_index + 414];
	if (mrsp_tasks[start_index + 415] != NULL)
		return mrsp_tasks[start_index + 415];
	if (mrsp_tasks[start_index + 416] != NULL)
		return mrsp_tasks[start_index + 416];
	if (mrsp_tasks[start_index + 417] != NULL)
		return mrsp_tasks[start_index + 417];
	if (mrsp_tasks[start_index + 418] != NULL)
		return mrsp_tasks[start_index + 418];
	if (mrsp_tasks[start_index + 419] != NULL)
		return mrsp_tasks[start_index + 419];
	if (mrsp_tasks[start_index + 420] != NULL)
		return mrsp_tasks[start_index + 420];
	if (mrsp_tasks[start_index + 421] != NULL)
		return mrsp_tasks[start_index + 421];
	if (mrsp_tasks[start_index + 422] != NULL)
		return mrsp_tasks[start_index + 422];
	if (mrsp_tasks[start_index + 423] != NULL)
		return mrsp_tasks[start_index + 423];
	if (mrsp_tasks[start_index + 424] != NULL)
		return mrsp_tasks[start_index + 424];
	if (mrsp_tasks[start_index + 425] != NULL)
		return mrsp_tasks[start_index + 425];
	if (mrsp_tasks[start_index + 426] != NULL)
		return mrsp_tasks[start_index + 426];
	if (mrsp_tasks[start_index + 427] != NULL)
		return mrsp_tasks[start_index + 427];
	if (mrsp_tasks[start_index + 428] != NULL)
		return mrsp_tasks[start_index + 428];
	if (mrsp_tasks[start_index + 429] != NULL)
		return mrsp_tasks[start_index + 429];
	if (mrsp_tasks[start_index + 430] != NULL)
		return mrsp_tasks[start_index + 430];
	if (mrsp_tasks[start_index + 431] != NULL)
		return mrsp_tasks[start_index + 431];
	if (mrsp_tasks[start_index + 432] != NULL)
		return mrsp_tasks[start_index + 432];
	if (mrsp_tasks[start_index + 433] != NULL)
		return mrsp_tasks[start_index + 433];
	if (mrsp_tasks[start_index + 434] != NULL)
		return mrsp_tasks[start_index + 434];
	if (mrsp_tasks[start_index + 435] != NULL)
		return mrsp_tasks[start_index + 435];
	if (mrsp_tasks[start_index + 436] != NULL)
		return mrsp_tasks[start_index + 436];
	if (mrsp_tasks[start_index + 437] != NULL)
		return mrsp_tasks[start_index + 437];
	if (mrsp_tasks[start_index + 438] != NULL)
		return mrsp_tasks[start_index + 438];
	if (mrsp_tasks[start_index + 439] != NULL)
		return mrsp_tasks[start_index + 439];
	if (mrsp_tasks[start_index + 440] != NULL)
		return mrsp_tasks[start_index + 440];
	if (mrsp_tasks[start_index + 441] != NULL)
		return mrsp_tasks[start_index + 441];
	if (mrsp_tasks[start_index + 442] != NULL)
		return mrsp_tasks[start_index + 442];
	if (mrsp_tasks[start_index + 443] != NULL)
		return mrsp_tasks[start_index + 443];
	if (mrsp_tasks[start_index + 444] != NULL)
		return mrsp_tasks[start_index + 444];
	if (mrsp_tasks[start_index + 445] != NULL)
		return mrsp_tasks[start_index + 445];
	if (mrsp_tasks[start_index + 446] != NULL)
		return mrsp_tasks[start_index + 446];
	if (mrsp_tasks[start_index + 447] != NULL)
		return mrsp_tasks[start_index + 447];
	if (mrsp_tasks[start_index + 448] != NULL)
		return mrsp_tasks[start_index + 448];
	if (mrsp_tasks[start_index + 449] != NULL)
		return mrsp_tasks[start_index + 449];
	if (mrsp_tasks[start_index + 450] != NULL)
		return mrsp_tasks[start_index + 450];
	if (mrsp_tasks[start_index + 451] != NULL)
		return mrsp_tasks[start_index + 451];
	if (mrsp_tasks[start_index + 452] != NULL)
		return mrsp_tasks[start_index + 452];
	if (mrsp_tasks[start_index + 453] != NULL)
		return mrsp_tasks[start_index + 453];
	if (mrsp_tasks[start_index + 454] != NULL)
		return mrsp_tasks[start_index + 454];
	if (mrsp_tasks[start_index + 455] != NULL)
		return mrsp_tasks[start_index + 455];
	if (mrsp_tasks[start_index + 456] != NULL)
		return mrsp_tasks[start_index + 456];
	if (mrsp_tasks[start_index + 457] != NULL)
		return mrsp_tasks[start_index + 457];
	if (mrsp_tasks[start_index + 458] != NULL)
		return mrsp_tasks[start_index + 458];
	if (mrsp_tasks[start_index + 459] != NULL)
		return mrsp_tasks[start_index + 459];
	if (mrsp_tasks[start_index + 460] != NULL)
		return mrsp_tasks[start_index + 460];
	if (mrsp_tasks[start_index + 461] != NULL)
		return mrsp_tasks[start_index + 461];
	if (mrsp_tasks[start_index + 462] != NULL)
		return mrsp_tasks[start_index + 462];
	if (mrsp_tasks[start_index + 463] != NULL)
		return mrsp_tasks[start_index + 463];
	if (mrsp_tasks[start_index + 464] != NULL)
		return mrsp_tasks[start_index + 464];
	if (mrsp_tasks[start_index + 465] != NULL)
		return mrsp_tasks[start_index + 465];
	if (mrsp_tasks[start_index + 466] != NULL)
		return mrsp_tasks[start_index + 466];
	if (mrsp_tasks[start_index + 467] != NULL)
		return mrsp_tasks[start_index + 467];
	if (mrsp_tasks[start_index + 468] != NULL)
		return mrsp_tasks[start_index + 468];
	if (mrsp_tasks[start_index + 469] != NULL)
		return mrsp_tasks[start_index + 469];
	if (mrsp_tasks[start_index + 470] != NULL)
		return mrsp_tasks[start_index + 470];
	if (mrsp_tasks[start_index + 471] != NULL)
		return mrsp_tasks[start_index + 471];
	if (mrsp_tasks[start_index + 472] != NULL)
		return mrsp_tasks[start_index + 472];
	if (mrsp_tasks[start_index + 473] != NULL)
		return mrsp_tasks[start_index + 473];
	if (mrsp_tasks[start_index + 474] != NULL)
		return mrsp_tasks[start_index + 474];
	if (mrsp_tasks[start_index + 475] != NULL)
		return mrsp_tasks[start_index + 475];
	if (mrsp_tasks[start_index + 476] != NULL)
		return mrsp_tasks[start_index + 476];
	if (mrsp_tasks[start_index + 477] != NULL)
		return mrsp_tasks[start_index + 477];
	if (mrsp_tasks[start_index + 478] != NULL)
		return mrsp_tasks[start_index + 478];
	if (mrsp_tasks[start_index + 479] != NULL)
		return mrsp_tasks[start_index + 479];
	if (mrsp_tasks[start_index + 480] != NULL)
		return mrsp_tasks[start_index + 480];
	if (mrsp_tasks[start_index + 481] != NULL)
		return mrsp_tasks[start_index + 481];
	if (mrsp_tasks[start_index + 482] != NULL)
		return mrsp_tasks[start_index + 482];
	if (mrsp_tasks[start_index + 483] != NULL)
		return mrsp_tasks[start_index + 483];
	if (mrsp_tasks[start_index + 484] != NULL)
		return mrsp_tasks[start_index + 484];
	if (mrsp_tasks[start_index + 485] != NULL)
		return mrsp_tasks[start_index + 485];
	if (mrsp_tasks[start_index + 486] != NULL)
		return mrsp_tasks[start_index + 486];
	if (mrsp_tasks[start_index + 487] != NULL)
		return mrsp_tasks[start_index + 487];
	if (mrsp_tasks[start_index + 488] != NULL)
		return mrsp_tasks[start_index + 488];
	if (mrsp_tasks[start_index + 489] != NULL)
		return mrsp_tasks[start_index + 489];
	if (mrsp_tasks[start_index + 490] != NULL)
		return mrsp_tasks[start_index + 490];
	if (mrsp_tasks[start_index + 491] != NULL)
		return mrsp_tasks[start_index + 491];
	if (mrsp_tasks[start_index + 492] != NULL)
		return mrsp_tasks[start_index + 492];
	if (mrsp_tasks[start_index + 493] != NULL)
		return mrsp_tasks[start_index + 493];
	if (mrsp_tasks[start_index + 494] != NULL)
		return mrsp_tasks[start_index + 494];
	if (mrsp_tasks[start_index + 495] != NULL)
		return mrsp_tasks[start_index + 495];
	if (mrsp_tasks[start_index + 496] != NULL)
		return mrsp_tasks[start_index + 496];
	if (mrsp_tasks[start_index + 497] != NULL)
		return mrsp_tasks[start_index + 497];
	if (mrsp_tasks[start_index + 498] != NULL)
		return mrsp_tasks[start_index + 498];
	if (mrsp_tasks[start_index + 499] != NULL)
		return mrsp_tasks[start_index + 499];
	if (mrsp_tasks[start_index + 500] != NULL)
		return mrsp_tasks[start_index + 500];
	if (mrsp_tasks[start_index + 501] != NULL)
		return mrsp_tasks[start_index + 501];
	if (mrsp_tasks[start_index + 502] != NULL)
		return mrsp_tasks[start_index + 502];
	if (mrsp_tasks[start_index + 503] != NULL)
		return mrsp_tasks[start_index + 503];
	if (mrsp_tasks[start_index + 504] != NULL)
		return mrsp_tasks[start_index + 504];
	if (mrsp_tasks[start_index + 505] != NULL)
		return mrsp_tasks[start_index + 505];
	if (mrsp_tasks[start_index + 506] != NULL)
		return mrsp_tasks[start_index + 506];
	if (mrsp_tasks[start_index + 507] != NULL)
		return mrsp_tasks[start_index + 507];
	if (mrsp_tasks[start_index + 508] != NULL)
		return mrsp_tasks[start_index + 508];
	if (mrsp_tasks[start_index + 509] != NULL)
		return mrsp_tasks[start_index + 509];
	if (mrsp_tasks[start_index + 510] != NULL)
		return mrsp_tasks[start_index + 510];
	if (mrsp_tasks[start_index + 511] != NULL)
		return mrsp_tasks[start_index + 511];

	return NULL;

//	int i;
//	for (i = start_index; i < start_index + LITMUS_MAX_PRIORITY; i++) {
//		if (mrsp_tasks[i] != NULL)
//			return mrsp_tasks[i];
//	}
//	return NULL;
}

static struct task_struct* mrsp_schedule(struct task_struct * prev) {
	mrsp_domain_t* pfp = local_pfp;
	struct task_struct* next;
	struct task_struct* peak_next;
	spinlock_t *this_lock = &per_cpu(mrsp_migrate_locks, pfp->cpu), *prev_lock, *next_lock;

	int out_of_time, sleep, preempt, np, exists, blocks, resched, migrate;

	raw_spin_lock(&pfp->slock);

	/* sanity checking
	 * differently from gedf, when a task exits (dead)
	 * pfp->schedule may be null and prev _is_ realtime
	 */
	/* (0) Determine state */
	exists = pfp->scheduled != NULL;
	blocks = exists && !is_current_running();
	out_of_time = exists && budget_enforced(pfp->scheduled) && budget_exhausted(pfp->scheduled);
	np = exists && is_np(pfp->scheduled);
	sleep = exists && is_completed(pfp->scheduled);
	migrate = exists && get_partition(pfp->scheduled) != pfp->cpu;
	preempt = !blocks && (migrate || fp_preemption_needed(&pfp->ready_queue, prev) || (prev != NULL && prev->rt_param.task_params.requesting_lock != NULL));

	/* If we need to preempt do so.
	 * The following checks set resched to 1 in case of special
	 * circumstances.
	 */
	resched = preempt;

	/* If a task blocks we have no choice but to reschedule.
	 */
	if (blocks || pfp->queued_mrsp_tasks > 0)
		resched = 1;

	/* Request a sys_exit_np() call if we would like to preempt but cannot.
	 * Multiple calls to request_exit_np() don't hurt.
	 */
	if (np && (out_of_time || preempt || sleep))
		request_exit_np(pfp->scheduled);

	/* Any task that is preemptable and either exhausts its execution
	 * budget or wants to sleep completes. We may have to reschedule after
	 * this.
	 */
	if (!np && (out_of_time || sleep)) {
		job_completion(pfp->scheduled, !sleep);
		resched = 1;
	}

	/* The final scheduling decision. Do we need to switch for some reason?
	 * Switch if we are in RT mode and have no task or if we need to
	 * resched.
	 */
	next = NULL;

	if ((!np || blocks) && (resched || !exists)) {
		/* When preempting a task that does not block, then
		 * re-insert it into either the ready queue or the
		 * release queue (if it completed). requeue() picks
		 * the appropriate queue.
		 */

		/** Before taking the next task from the run queue, we check whether a MrsP task can resume.**/
		if (prev && prev->rt_param.task_params.num_mrsp_locks > 0 && !blocks && !migrate && fp_preemption_needed(&pfp->ready_queue, prev)) {
			int leaving_assigned = 0;
			prev_lock = &per_cpu(mrsp_migrate_locks, prev->rt_param.task_params.original_cpu);
			spin_lock(prev_lock);

			if (prev->rt_param.task_params.leaving_priority == -1) {
				prev->rt_param.task_params.leaving_priority = get_priority(prev);
				leaving_assigned = 1;
			}

			if (prev->rt_param.task_params.np_len > 0) {
				prev->rt_param.task_params.migrated_time = -1;
				cancel_non_preemption_budget_timer(prev);
				if (get_priority(prev) == 0) {
					if (prev->rt_param.task_params.saved_current_priority != -1 && prev->rt_param.task_params.saved_current_priority != 0)
						prev->rt_param.task_params.priority = prev->rt_param.task_params.saved_current_priority;
					else if (leaving_assigned == 0)
						prev->rt_param.task_params.priority = prev->rt_param.task_params.leaving_priority;
				}
			}

			if (mrsp_tasks[prev->rt_param.task_params.leaving_priority + prev->rt_param.task_params.original_cpu * LITMUS_MAX_PRIORITY] != NULL) {
				if (leaving_assigned)
					prev->rt_param.task_params.leaving_priority = -1;
				prev->rt_param.task_params.preempted = 0;

				if (pfp->scheduled && !blocks && !migrate)
					requeue(pfp->scheduled, pfp);

			} else {
				mrsp_domain_t *ori = remote_pfp(prev->rt_param.task_params.original_cpu);
				prev->rt_param.task_params.helper = NULL;

				mrsp_tasks[prev->rt_param.task_params.leaving_priority + prev->rt_param.task_params.original_cpu * LITMUS_MAX_PRIORITY] = prev;
				prev->rt_param.task_params.preempted = 1;

				ori->queued_mrsp_tasks = ori->queued_mrsp_tasks + 1;
			}

			spin_unlock(prev_lock);

		} else if (prev && prev->rt_param.task_params.fifop_requesting_lock != NULL && !blocks && !migrate && fp_preemption_needed(&pfp->ready_queue, prev)) {
			spin_lock(&prev->rt_param.task_params.fifop_requesting_lock->lock);

			prev->rt_param.task_params.need_re_request = 1;
			list_del(prev->rt_param.task_params.next);
			prev->rt_param.task_params.next = NULL;

			spin_unlock(&prev->rt_param.task_params.fifop_requesting_lock->lock);
			prev->rt_param.task_params.fifop_requesting_lock = NULL;
			requeue(pfp->scheduled, pfp);

		} else if (pfp->scheduled && !blocks && !migrate) {
			requeue(pfp->scheduled, pfp);
		}

		peak_next = fp_prio_peek(&pfp->ready_queue);
		if (pfp->queued_mrsp_tasks > 0) {
			struct task_struct* head;
			int start_index = pfp->cpu * LITMUS_MAX_PRIORITY;

			spin_lock(this_lock);

			head = get_high_priority_mrsp_task(start_index);
			if (head != NULL) {
				/**
				 * if the MrsP task has a same priority as that of the first task in run queue, we schedule the MrsP task first according to FIFO.
				 */
				if (peak_next == NULL || head->rt_param.task_params.leaving_priority <= get_priority(peak_next)) {
					next = head;
					mrsp_tasks[pfp->cpu * LITMUS_MAX_PRIORITY + head->rt_param.task_params.leaving_priority] = NULL;
					pfp->queued_mrsp_tasks = pfp->queued_mrsp_tasks - 1;

					if (next->rt_param.task_params.np_len > 0) {
						next->rt_param.task_params.migrated_time = -1;
						cancel_non_preemption_budget_timer(next);
					}

					next->rt_param.task_params.cpu = pfp->cpu;
					next->rt_param.task_params.priority = head->rt_param.task_params.leaving_priority;
					next->rt_param.task_params.preempted = 0;
					next->rt_param.task_params.leaving_priority = -1;
				}
			}
			spin_unlock(this_lock);
		}

		/* The helping mechanism starts here. we need to fight with race condition here. */
		if (next == NULL && peak_next != NULL && peak_next->rt_param.task_params.enable_help == 1 && peak_next->rt_param.task_params.requesting_lock != NULL) {
			struct task_list *lock_next;
			struct task_struct* head;
//			int i, numOfLocks, vaild = 0;

			lock_next = list_entry((peak_next->rt_param.task_params.requesting_lock->tasks_queue->next.next), struct task_list, next);
			if (lock_next == NULL || lock_next->task == NULL) {
				head = NULL;
			} else {
				head = lock_next->task;
			}

			if (head != NULL) {
				next_lock = &per_cpu(mrsp_migrate_locks, head->rt_param.task_params.original_cpu);
				spin_lock(next_lock);

				// many things can happen after we get mrsp queue lock. we need to check now whether the task still has the requesting lock before help.
//				numOfLocks = head->rt_param.task_params.num_mrsp_locks;
//				for (i = 0; i < numOfLocks; i++) {
//					if (head->rt_param.task_params.mrsp_locks[i] == peak_next->rt_param.task_params.requesting_lock)
//						vaild = 1;
//				}
//				if (peak_next->rt_param.task_params.requesting_lock == head->rt_param.task_params.requesting_lock)
//					vaild = 1;

				if (/*vaild == 1 &&*/head->rt_param.task_params.preempted == 1) {
					mrsp_domain_t *ori = remote_pfp(head->rt_param.task_params.original_cpu);
					/**
					 * If we gets into here successfully, there will be no races conditions. the head is still preempted and in the MrsP queue.
					 */
					mrsp_tasks[head->rt_param.task_params.original_cpu * LITMUS_MAX_PRIORITY + head->rt_param.task_params.leaving_priority] = NULL;
					ori->queued_mrsp_tasks = ori->queued_mrsp_tasks - 1;

					next = head;
					if (next->rt_param.task_params.np_len > 0) {
						next->rt_param.task_params.priority = 0;
						next->rt_param.task_params.migrated_time = get_exec_time(next);
						next->rt_param.task_params.saved_current_priority = get_priority(peak_next);
					} else
						next->rt_param.task_params.priority = get_priority(peak_next);

					next->rt_param.task_params.cpu = pfp->cpu;
					next->rt_param.task_params.preempted = 0;
					next->rt_param.task_params.helper = peak_next;
				}

				spin_unlock(next_lock);

			}

		}

		if (next == NULL) {
			next = fp_prio_take(&pfp->ready_queue);
		}
	} else
	/* Only override Linux scheduler if we have a real-time task
	 * scheduled that needs to continue.
	 */
	if (exists)
		next = prev;

	pfp->scheduled = next;
	sched_state_task_picked();

	raw_spin_unlock(&pfp->slock);
	return next;
}

#ifdef CONFIG_LITMUS_LOCKING

/* prev is no longer scheduled --- see if it needs to migrate */
static void mrsp_finish_switch(struct task_struct *prev) {
	mrsp_domain_t *to;

	if (prev->rt_param.task_params.mrsp_task == 0) {
		if (is_realtime(prev) && prev->state == TASK_RUNNING &&
		get_partition(prev) != smp_processor_id()) {

			to = task_pfp(prev);
			raw_spin_lock(&to->slock);
			requeue(prev, to);
			if (fp_preemption_needed(&to->ready_queue, to->scheduled))
				preempt(to);
			raw_spin_unlock(&to->slock);
		}
	} else {
		if (is_realtime(prev) && prev->state == TASK_RUNNING &&
		get_partition(prev) != smp_processor_id()) {
			spinlock_t *prev_lock = &per_cpu(mrsp_migrate_locks, prev->rt_param.task_params.original_cpu);
			to = task_pfp(prev);

			raw_spin_lock(&to->slock);
			spin_lock(prev_lock);

			if (prev->rt_param.task_params.migrate_back == 0) {
				spin_unlock(prev_lock);
				raw_spin_unlock(&to->slock);
				return;
			}
			prev->rt_param.task_params.migrate_back = 0;

			if (prev->rt_param.task_params.num_mrsp_locks == 0) {
				prev->rt_param.task_params.leaving_priority = -1;
				prev->rt_param.task_params.helper = NULL;

				prev->rt_param.task_params.preempted = 0;
				prev->rt_param.task_params.cpu = prev->rt_param.task_params.original_cpu;
				prev->rt_param.task_params.priority = prev->rt_param.task_params.original_priority;

				requeue(prev, to);
				if (fp_preemption_needed(&to->ready_queue, to->scheduled))
					preempt(to);
			} else {
				if (mrsp_tasks[prev->rt_param.task_params.original_cpu * LITMUS_MAX_PRIORITY + prev->rt_param.task_params.leaving_priority] != NULL) {

					prev->rt_param.task_params.cpu = prev->rt_param.task_params.original_cpu;
					prev->rt_param.task_params.priority = prev->rt_param.task_params.leaving_priority;
					prev->rt_param.task_params.leaving_priority = -1;
					prev->rt_param.task_params.preempted = 0;
					prev->rt_param.task_params.helper = NULL;

					requeue(prev, to);
					if (fp_preemption_needed(&to->ready_queue, to->scheduled))
						preempt(to);

				} else {
					mrsp_domain_t *ori = remote_pfp(prev->rt_param.task_params.original_cpu);
					mrsp_tasks[prev->rt_param.task_params.original_cpu * LITMUS_MAX_PRIORITY + prev->rt_param.task_params.leaving_priority] = prev;
					ori->queued_mrsp_tasks = ori->queued_mrsp_tasks + 1;

					prev->rt_param.task_params.preempted = 1;
					prev->rt_param.task_params.helper = NULL;
				}

			}

			spin_unlock(prev_lock);
			raw_spin_unlock(&to->slock);
		}
	}

}

#endif

/*	Prepare a task for running in RT mode
 */
static void mrsp_task_new(struct task_struct * t, int on_rq, int is_scheduled) {
	mrsp_domain_t* pfp = task_pfp(t);
	unsigned long flags;

	TRACE_TASK(t, "P-FP: task new, cpu = %d\n", t->rt_param.task_params.cpu);

	/* setup job parameters */
	release_at(t, litmus_clock());

	raw_spin_lock_irqsave(&pfp->slock, flags);
	if (is_scheduled) {
		/* there shouldn't be anything else running at the time */
		BUG_ON(pfp->scheduled);
		pfp->scheduled = t;
	} else if (on_rq) {
		requeue(t, pfp);
		/* maybe we have to reschedule */
		mrsp_preempt_check(pfp);
	}
	raw_spin_unlock_irqrestore(&pfp->slock, flags);
}

static void mrsp_task_wake_up(struct task_struct *task) {
	unsigned long flags;
	mrsp_domain_t* pfp = task_pfp(task);
	lt_t now;

	TRACE_TASK(task, "wake_up at %llu\n", litmus_clock());
	raw_spin_lock_irqsave(&pfp->slock, flags);

#ifdef CONFIG_LITMUS_LOCKING
	/* Should only be queued when processing a fake-wake up due to a
	 * migration-related state change. */
	if (unlikely(is_queued(task))) {
		TRACE_TASK(task, "WARNING: waking task still queued. Is this right?\n");
		goto out_unlock;
	}
#else
	BUG_ON(is_queued(task));
#endif
	now = litmus_clock();
	if (is_sporadic(task) && is_tardy(task, now)
#ifdef CONFIG_LITMUS_LOCKING
		/* We need to take suspensions because of semaphores into
		 * account! If a job resumes after being suspended due to acquiring
		 * a semaphore, it should never be treated as a new job release.
		 */
		&& !is_priority_boosted(task)
#endif
	) {
		/* new sporadic release */
		release_at(task, now);
		sched_trace_task_release(task);
	}

	/* Only add to ready queue if it is not the currently-scheduled
	 * task. This could be the case if a task was woken up concurrently
	 * on a remote CPU before the executing CPU got around to actually
	 * de-scheduling the task, i.e., wake_up() raced with schedule()
	 * and won. Also, don't requeue if it is still queued, which can
	 * happen under the DPCP due wake-ups racing with migrations.
	 */
	if (pfp->scheduled != task) {
		requeue(task, pfp);
		mrsp_preempt_check(pfp);
	}

#ifdef CONFIG_LITMUS_LOCKING
	out_unlock:
#endif
	raw_spin_unlock_irqrestore(&pfp->slock, flags);TRACE_TASK(task, "wake up done\n");
}

static void mrsp_task_block(struct task_struct *t) {
	/* only running tasks can block, thus t is in no queue */
	TRACE_TASK(t, "block at %llu, state=%d\n", litmus_clock(), t->state);

	BUG_ON(!is_realtime(t));

	/* If this task blocked normally, it shouldn't be queued. The exception is
	 * if this is a simulated block()/wakeup() pair from the pull-migration code path.
	 * This should only happen if the DPCP is being used.
	 */
#ifdef CONFIG_LITMUS_LOCKING
	if (unlikely(is_queued(t)))
		TRACE_TASK(t, "WARNING: blocking task still queued. Is this right?\n");
#else
	BUG_ON(is_queued(t));
#endif
}

static void mrsp_task_exit(struct task_struct * t) {
	unsigned long flags;
	mrsp_domain_t* pfp = task_pfp(t);
	rt_domain_t* dom;

	raw_spin_lock_irqsave(&pfp->slock, flags);
	if (is_queued(t)) {
		BUG()
		; /* This currently doesn't work. */
		/* dequeue */
		dom = task_dom(t);
		remove(dom, t);
	}
	if (pfp->scheduled == t) {
		pfp->scheduled = NULL;
		preempt(pfp);
	}TRACE_TASK(t, "RIP, now reschedule\n");

	raw_spin_unlock_irqrestore(&pfp->slock, flags);
}

#ifdef CONFIG_LITMUS_LOCKING

#include <litmus/fdso.h>
#include <litmus/srp.h>

static void fp_dequeue(mrsp_domain_t* pfp, struct task_struct* t) {
	BUG_ON(pfp->scheduled == t && is_queued(t));
	if (is_queued(t))
		fp_prio_remove(&pfp->ready_queue, t, priority_index(t));
}

static void fp_set_prio_inh(mrsp_domain_t* pfp, struct task_struct* t, struct task_struct* prio_inh) {
	int requeue;

	if (!t || t->rt_param.inh_task == prio_inh) {
		/* no update  required */
		if (t)
			TRACE_TASK(t, "no prio-inh update required\n");
		return;
	}

	requeue = is_queued(t);
	TRACE_TASK(t, "prio-inh: is_queued:%d\n", requeue);

	if (requeue)
		/* first remove */
		fp_dequeue(pfp, t);

	t->rt_param.inh_task = prio_inh;

	if (requeue)
		/* add again to the right queue */
		fp_prio_add(&pfp->ready_queue, t, priority_index(t));
}

static int effective_agent_priority(int prio) {
	/* make sure agents have higher priority */
	return prio - LITMUS_MAX_PRIORITY;
}

static lt_t prio_point(int eprio) {
	/* make sure we have non-negative prio points */
	return eprio + LITMUS_MAX_PRIORITY;
}

static void boost_priority(struct task_struct* t, lt_t priority_point) {
	unsigned long flags;
	mrsp_domain_t* pfp = task_pfp(t);

	raw_spin_lock_irqsave(&pfp->slock, flags);

	TRACE_TASK(t, "priority boosted at %llu\n", litmus_clock());

	tsk_rt(t)->priority_boosted = 1;
	/* tie-break by protocol-specific priority point */
	tsk_rt(t)->boost_start_time = priority_point;

	/* Priority boosting currently only takes effect for already-scheduled
	 * tasks. This is sufficient since priority boosting only kicks in as
	 * part of lock acquisitions. */
	BUG_ON(pfp->scheduled != t);

	raw_spin_unlock_irqrestore(&pfp->slock, flags);
}

static void unboost_priority(struct task_struct* t) {
	unsigned long flags;
	mrsp_domain_t* pfp = task_pfp(t);

	raw_spin_lock_irqsave(&pfp->slock, flags);

	/* Assumption: this only happens when the job is scheduled.
	 * Exception: If t transitioned to non-real-time mode, we no longer
	 * care abou tit. */
	BUG_ON(pfp->scheduled != t && is_realtime(t));

	TRACE_TASK(t, "priority restored at %llu\n", litmus_clock());

	tsk_rt(t)->priority_boosted = 0;
	tsk_rt(t)->boost_start_time = 0;

	/* check if this changes anything */
	if (fp_preemption_needed(&pfp->ready_queue, pfp->scheduled))
		preempt(pfp);

	raw_spin_unlock_irqrestore(&pfp->slock, flags);
}

/* ******************** SRP support ************************ */

static unsigned int mrsp_get_srp_prio(struct task_struct* t) {
	return get_priority(t);
}

/* ******************** FMLP support ********************** */

struct fmlp_semaphore {
	struct litmus_lock litmus_lock;

	/* current resource holder */
	struct task_struct *owner;

	/* FIFO queue of waiting tasks */
	wait_queue_head_t wait;
};

static inline struct fmlp_semaphore* fmlp_from_lock(struct litmus_lock* lock) {
	return container_of(lock, struct fmlp_semaphore, litmus_lock);
}

static inline lt_t fmlp_clock(void) {
	return (lt_t) this_cpu_inc_return(mrsp_fmlp_timestamp);
}

int mrsp_fmlp_lock(struct litmus_lock* l) {
	struct task_struct* t = current;
	struct fmlp_semaphore *sem = fmlp_from_lock(l);
	wait_queue_t wait;
	unsigned long flags;
	lt_t time_of_request;

	if (!is_realtime(t))
		return -EPERM;

	/* prevent nested lock acquisition --- not supported by FMLP */
	if (tsk_rt(t)->num_locks_held ||
	tsk_rt(t)->num_local_locks_held)
		return -EBUSY;

	spin_lock_irqsave(&sem->wait.lock, flags);

	/* tie-break by this point in time */
	time_of_request = fmlp_clock();

	/* Priority-boost ourself *before* we suspend so that
	 * our priority is boosted when we resume. */
	boost_priority(t, time_of_request);

	if (sem->owner) {
		/* resource is not free => must suspend and wait */

		init_waitqueue_entry(&wait, t);

		/* FIXME: interruptible would be nice some day */
		set_task_state(t, TASK_UNINTERRUPTIBLE);

		__add_wait_queue_tail_exclusive(&sem->wait, &wait);

		TS_LOCK_SUSPEND
		;

		/* release lock before sleeping */
		spin_unlock_irqrestore(&sem->wait.lock, flags);

		/* We depend on the FIFO order.  Thus, we don't need to recheck
		 * when we wake up; we are guaranteed to have the lock since
		 * there is only one wake up per release.
		 */

		schedule();

		TS_LOCK_RESUME
		;

		/* Since we hold the lock, no other task will change
		 * ->owner. We can thus check it without acquiring the spin
		 * lock. */
		BUG_ON(sem->owner != t);
	} else {
		/* it's ours now */
		sem->owner = t;

		spin_unlock_irqrestore(&sem->wait.lock, flags);
	}

	tsk_rt(t)->num_locks_held++;

	return 0;
}

int mrsp_fmlp_unlock(struct litmus_lock* l) {
	struct task_struct *t = current, *next = NULL;
	struct fmlp_semaphore *sem = fmlp_from_lock(l);
	unsigned long flags;
	int err = 0;

	preempt_disable()
	;

	spin_lock_irqsave(&sem->wait.lock, flags);

	if (sem->owner != t) {
		err = -EINVAL;
		goto out;
	}

	tsk_rt(t)->num_locks_held--;

	/* we lose the benefit of priority boosting */

	unboost_priority(t);

	/* check if there are jobs waiting for this resource */
	next = __waitqueue_remove_first(&sem->wait);
	sem->owner = next;

	out: spin_unlock_irqrestore(&sem->wait.lock, flags);

	/* Wake up next. The waiting job is already priority-boosted. */
	if (next) {
		wake_up_process(next);
	}

	preempt_enable()
	;

	return err;
}

int mrsp_fmlp_close(struct litmus_lock* l) {
	struct task_struct *t = current;
	struct fmlp_semaphore *sem = fmlp_from_lock(l);
	unsigned long flags;

	int owner;

	spin_lock_irqsave(&sem->wait.lock, flags);

	owner = sem->owner == t;

	spin_unlock_irqrestore(&sem->wait.lock, flags);

	if (owner)
		mrsp_fmlp_unlock(l);

	return 0;
}

void mrsp_fmlp_free(struct litmus_lock* lock) {
	kfree(fmlp_from_lock(lock));
}

static struct litmus_lock_ops mrsp_fmlp_lock_ops =
	{ .close = mrsp_fmlp_close, .lock = mrsp_fmlp_lock, .unlock = mrsp_fmlp_unlock, .deallocate = mrsp_fmlp_free, };

static struct litmus_lock* mrsp_new_fmlp(void) {
	struct fmlp_semaphore* sem;

	sem = kmalloc(sizeof(*sem), GFP_KERNEL);
	if (!sem)
		return NULL;

	sem->owner = NULL;
	init_waitqueue_head(&sem->wait);
	sem->litmus_lock.ops = &mrsp_fmlp_lock_ops;

	return &sem->litmus_lock;
}

/* ******************** MPCP support ********************** */

struct mpcp_semaphore {
	struct litmus_lock litmus_lock;

	/* current resource holder */
	struct task_struct *owner;

	/* priority queue of waiting tasks */
	wait_queue_head_t wait;

	/* priority ceiling per cpu */
	unsigned int prio_ceiling[NR_CPUS];

	/* should jobs spin "virtually" for this resource? */
	int vspin;
};

#define OMEGA_CEILING UINT_MAX

/* Since jobs spin "virtually" while waiting to acquire a lock,
 * they first must aquire a local per-cpu resource.
 */
static DEFINE_PER_CPU(wait_queue_head_t, mpcpvs_vspin_wait);
static DEFINE_PER_CPU(struct task_struct*, mpcpvs_vspin);

/* called with preemptions off <=> no local modifications */
static void mpcp_vspin_enter(void) {
	struct task_struct* t = current;

	while (1) {
		if (this_cpu_read(mpcpvs_vspin) == NULL) {
			/* good, we get to issue our request */
			this_cpu_write(mpcpvs_vspin, t);
			break;
		} else {
			/* some job is spinning => enqueue in request queue */
			prio_wait_queue_t wait;
			wait_queue_head_t* vspin = this_cpu_ptr(&mpcpvs_vspin_wait);
			unsigned long flags;

			/* ordered by regular priority */
			init_prio_waitqueue_entry(&wait, t, prio_point(get_priority(t)));

			spin_lock_irqsave(&vspin->lock, flags);

			set_task_state(t, TASK_UNINTERRUPTIBLE);

			__add_wait_queue_prio_exclusive(vspin, &wait);

			spin_unlock_irqrestore(&vspin->lock, flags);

			TS_LOCK_SUSPEND
			;

			preempt_enable_no_resched()
			;

			schedule();

			preempt_disable()
			;

			TS_LOCK_RESUME
			;
			/* Recheck if we got it --- some higher-priority process might
			 * have swooped in. */
		}
	}
	/* ok, now it is ours */
}

/* called with preemptions off */
static void mpcp_vspin_exit(void) {
	struct task_struct* t = current, *next;
	unsigned long flags;
	wait_queue_head_t* vspin = this_cpu_ptr(&mpcpvs_vspin_wait);

	BUG_ON(this_cpu_read(mpcpvs_vspin) != t);

	/* no spinning job */
	this_cpu_write(mpcpvs_vspin, NULL);

	/* see if anyone is waiting for us to stop "spinning" */
	spin_lock_irqsave(&vspin->lock, flags);
	next = __waitqueue_remove_first(vspin);

	if (next)
		wake_up_process(next);

	spin_unlock_irqrestore(&vspin->lock, flags);
}

static inline struct mpcp_semaphore* mpcp_from_lock(struct litmus_lock* lock) {
	return container_of(lock, struct mpcp_semaphore, litmus_lock);
}

int mrsp_mpcp_lock(struct litmus_lock* l) {
	struct task_struct* t = current;
	struct mpcp_semaphore *sem = mpcp_from_lock(l);
	prio_wait_queue_t wait;
	unsigned long flags;

	if (!is_realtime(t))
		return -EPERM;

	/* prevent nested lock acquisition */
	if (tsk_rt(t)->num_locks_held ||
	tsk_rt(t)->num_local_locks_held)
		return -EBUSY;

	preempt_disable()
	;

	if (sem->vspin)
		mpcp_vspin_enter();

	/* Priority-boost ourself *before* we suspend so that
	 * our priority is boosted when we resume. Use the priority
	 * ceiling for the local partition. */
	boost_priority(t, sem->prio_ceiling[get_partition(t)]);

	spin_lock_irqsave(&sem->wait.lock, flags);

	preempt_enable_no_resched()
	;

	if (sem->owner) {
		/* resource is not free => must suspend and wait */

		/* ordered by regular priority */
		init_prio_waitqueue_entry(&wait, t, prio_point(get_priority(t)));

		/* FIXME: interruptible would be nice some day */
		set_task_state(t, TASK_UNINTERRUPTIBLE);

		__add_wait_queue_prio_exclusive(&sem->wait, &wait);

		TS_LOCK_SUSPEND
		;

		/* release lock before sleeping */
		spin_unlock_irqrestore(&sem->wait.lock, flags);

		/* We depend on the FIFO order.  Thus, we don't need to recheck
		 * when we wake up; we are guaranteed to have the lock since
		 * there is only one wake up per release.
		 */

		schedule();

		TS_LOCK_RESUME
		;

		/* Since we hold the lock, no other task will change
		 * ->owner. We can thus check it without acquiring the spin
		 * lock. */
		BUG_ON(sem->owner != t);
	} else {
		/* it's ours now */
		sem->owner = t;

		spin_unlock_irqrestore(&sem->wait.lock, flags);
	}

	tsk_rt(t)->num_locks_held++;

	return 0;
}

int mrsp_mpcp_unlock(struct litmus_lock* l) {
	struct task_struct *t = current, *next = NULL;
	struct mpcp_semaphore *sem = mpcp_from_lock(l);
	unsigned long flags;
	int err = 0;

	preempt_disable()
	;

	spin_lock_irqsave(&sem->wait.lock, flags);

	if (sem->owner != t) {
		err = -EINVAL;
		goto out;
	}

	tsk_rt(t)->num_locks_held--;

	/* we lose the benefit of priority boosting */
	unboost_priority(t);

	/* check if there are jobs waiting for this resource */
	next = __waitqueue_remove_first(&sem->wait);
	sem->owner = next;

	out: spin_unlock_irqrestore(&sem->wait.lock, flags);

	/* Wake up next. The waiting job is already priority-boosted. */
	if (next) {
		wake_up_process(next);
	}

	if (sem->vspin && err == 0) {
		mpcp_vspin_exit();
	}

	preempt_enable()
	;

	return err;
}

int mrsp_mpcp_open(struct litmus_lock* l, void* config) {
	struct task_struct *t = current;
	int cpu, local_cpu;
	struct mpcp_semaphore *sem = mpcp_from_lock(l);
	unsigned long flags;

	if (!is_realtime(t))
		/* we need to know the real-time priority */
		return -EPERM;

	local_cpu = get_partition(t);

	spin_lock_irqsave(&sem->wait.lock, flags);
	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (cpu != local_cpu) {
			sem->prio_ceiling[cpu] = min(sem->prio_ceiling[cpu], get_priority(t));
			TRACE_CUR("priority ceiling for sem %p is now %d on cpu %d\n", sem, sem->prio_ceiling[cpu], cpu);
		}
	}
	spin_unlock_irqrestore(&sem->wait.lock, flags);

	return 0;
}

int mrsp_mpcp_close(struct litmus_lock* l) {
	struct task_struct *t = current;
	struct mpcp_semaphore *sem = mpcp_from_lock(l);
	unsigned long flags;

	int owner;

	spin_lock_irqsave(&sem->wait.lock, flags);

	owner = sem->owner == t;

	spin_unlock_irqrestore(&sem->wait.lock, flags);

	if (owner)
		mrsp_mpcp_unlock(l);

	return 0;
}

void mrsp_mpcp_free(struct litmus_lock* lock) {
	kfree(mpcp_from_lock(lock));
}

static struct litmus_lock_ops mrsp_mpcp_lock_ops = { .close = mrsp_mpcp_close, .lock = mrsp_mpcp_lock, .open = mrsp_mpcp_open, .unlock = mrsp_mpcp_unlock,
	.deallocate = mrsp_mpcp_free, };

static struct litmus_lock* mrsp_new_mpcp(int vspin) {
	struct mpcp_semaphore* sem;
	int cpu;

	sem = kmalloc(sizeof(*sem), GFP_KERNEL);
	if (!sem)
		return NULL;

	sem->owner = NULL;
	init_waitqueue_head(&sem->wait);
	sem->litmus_lock.ops = &mrsp_mpcp_lock_ops;

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		sem->prio_ceiling[cpu] = OMEGA_CEILING;

	/* mark as virtual spinning */
	sem->vspin = vspin;

	return &sem->litmus_lock;
}

/* ******************** PCP support ********************** */

struct pcp_semaphore {
	struct litmus_lock litmus_lock;

	struct list_head ceiling;

	/* current resource holder */
	struct task_struct *owner;

	/* priority ceiling --- can be negative due to DPCP support */
	int prio_ceiling;

	/* on which processor is this PCP semaphore allocated? */
	int on_cpu;
};

static inline struct pcp_semaphore* pcp_from_lock(struct litmus_lock* lock) {
	return container_of(lock, struct pcp_semaphore, litmus_lock);
}

struct pcp_state {
	struct list_head system_ceiling;

	/* highest-priority waiting task */
	struct task_struct* hp_waiter;

	/* list of jobs waiting to get past the system ceiling */
	wait_queue_head_t ceiling_blocked;
};

static void pcp_init_state(struct pcp_state* s) {
	INIT_LIST_HEAD(&s->system_ceiling);
	s->hp_waiter = NULL;
	init_waitqueue_head(&s->ceiling_blocked);
}

static DEFINE_PER_CPU(struct pcp_state, pcp_state);

/* assumes preemptions are off */
static struct pcp_semaphore* pcp_get_ceiling(void) {
	struct list_head* top = &(this_cpu_ptr(&pcp_state)->system_ceiling);
	return list_first_entry_or_null(top, struct pcp_semaphore, ceiling);
}

/* assumes preempt off */
static void pcp_add_ceiling(struct pcp_semaphore* sem) {
	struct list_head *pos;
	struct list_head *in_use = &(this_cpu_ptr(&pcp_state)->system_ceiling);
	struct pcp_semaphore* held;

	BUG_ON(sem->on_cpu != smp_processor_id());
	BUG_ON(in_list(&sem->ceiling));

	list_for_each(pos, in_use)
	{
		held = list_entry(pos, struct pcp_semaphore, ceiling);
		if (held->prio_ceiling >= sem->prio_ceiling) {
			__list_add(&sem->ceiling, pos->prev, pos);
			return;
		}
	}

	/* we hit the end of the list */

	list_add_tail(&sem->ceiling, in_use);
}

/* assumes preempt off */
static int pcp_exceeds_ceiling(struct pcp_semaphore* ceiling, struct task_struct* task, int effective_prio) {
	return ceiling == NULL || ceiling->prio_ceiling > effective_prio || ceiling->owner == task;
}

/* assumes preempt off */
static void pcp_priority_inheritance(void) {
	unsigned long flags;
	mrsp_domain_t* pfp = local_pfp;

	struct pcp_semaphore* ceiling = pcp_get_ceiling();
	struct task_struct *blocker, *blocked;

	blocker = ceiling ? ceiling->owner : NULL;
	blocked = this_cpu_ptr(&pcp_state)->hp_waiter;

	raw_spin_lock_irqsave(&pfp->slock, flags);

	/* Current is no longer inheriting anything by default.  This should be
	 * the currently scheduled job, and hence not currently queued.
	 * Special case: if current stopped being a real-time task, it will no longer
	 * be registered as pfp->scheduled. */
	BUG_ON(current != pfp->scheduled && is_realtime(current));

	fp_set_prio_inh(pfp, current, NULL);
	fp_set_prio_inh(pfp, blocked, NULL);
	fp_set_prio_inh(pfp, blocker, NULL);

	/* Let blocking job inherit priority of blocked job, if required. */
	if (blocker && blocked && fp_higher_prio(blocked, blocker)) {
		TRACE_TASK(blocker, "PCP inherits from %s/%d (prio %u -> %u) \n", blocked->comm, blocked->pid, get_priority(blocker), get_priority(blocked));
		fp_set_prio_inh(pfp, blocker, blocked);
	}

	/* Check if anything changed. If the blocked job is current, then it is
	 * just blocking and hence is going to call the scheduler anyway. */
	if (blocked != current && fp_higher_prio(fp_prio_peek(&pfp->ready_queue), pfp->scheduled))
		preempt(pfp);

	raw_spin_unlock_irqrestore(&pfp->slock, flags);
}

/* called with preemptions off */
static void pcp_raise_ceiling(struct pcp_semaphore* sem, int effective_prio) {
	struct task_struct* t = current;
	struct pcp_semaphore* ceiling;
	prio_wait_queue_t wait;
	unsigned int waiting_higher_prio;

	while (1) {
		ceiling = pcp_get_ceiling();
		if (pcp_exceeds_ceiling(ceiling, t, effective_prio))
			break;

		TRACE_CUR("PCP ceiling-blocked, wanted sem %p, but %s/%d has the ceiling \n", sem, ceiling->owner->comm, ceiling->owner->pid);

		/* we need to wait until the ceiling is lowered */

		/* enqueue in priority order */
		init_prio_waitqueue_entry(&wait, t, effective_prio);
		set_task_state(t, TASK_UNINTERRUPTIBLE);
		waiting_higher_prio = add_wait_queue_prio_exclusive(&(this_cpu_ptr(&pcp_state)->ceiling_blocked), &wait);

		if (waiting_higher_prio == 0) {
			TRACE_CUR("PCP new highest-prio waiter => prio inheritance\n");

			/* we are the new highest-priority waiting job
			 * => update inheritance */
			this_cpu_ptr(&pcp_state)->hp_waiter = t;
			pcp_priority_inheritance();
		}

		TS_LOCK_SUSPEND
		;

		preempt_enable_no_resched()
		;
		schedule();
		preempt_disable()
		;

		/* pcp_resume_unblocked() removed us from wait queue */

		TS_LOCK_RESUME
		;
	}

	TRACE_CUR("PCP got the ceiling and sem %p\n", sem);

	/* We are good to go. The semaphore should be available. */
	BUG_ON(sem->owner != NULL);

	sem->owner = t;

	pcp_add_ceiling(sem);
}

static void pcp_resume_unblocked(void) {
	wait_queue_head_t *blocked = &(this_cpu_ptr(&pcp_state)->ceiling_blocked);
	unsigned long flags;
	prio_wait_queue_t* q;
	struct task_struct* t = NULL;

	struct pcp_semaphore* ceiling = pcp_get_ceiling();

	spin_lock_irqsave(&blocked->lock, flags);

	while (waitqueue_active(blocked)) {
		/* check first == highest-priority waiting job */
		q = list_entry(blocked->task_list.next, prio_wait_queue_t, wq.task_list);
		t = (struct task_struct*) q->wq.private;

		/* can it proceed now? => let it go */
		if (pcp_exceeds_ceiling(ceiling, t, q->priority)) {
			__remove_wait_queue(blocked, &q->wq);
			wake_up_process(t);
		} else {
			/* We are done. Update highest-priority waiter. */
			this_cpu_ptr(&pcp_state)->hp_waiter = t;
			goto out;
		}
	}
	/* If we get here, then there are no more waiting
	 * jobs. */
	this_cpu_ptr(&pcp_state)->hp_waiter = NULL;
	out: spin_unlock_irqrestore(&blocked->lock, flags);
}

/* assumes preempt off */
static void pcp_lower_ceiling(struct pcp_semaphore* sem) {
	BUG_ON(!in_list(&sem->ceiling));
	BUG_ON(sem->owner != current);
	BUG_ON(sem->on_cpu != smp_processor_id());

	/* remove from ceiling list */
	list_del(&sem->ceiling);

	/* release */
	sem->owner = NULL;

	TRACE_CUR("PCP released sem %p\n", sem);

	/* Wake up all ceiling-blocked jobs that now pass the ceiling. */
	pcp_resume_unblocked();

	pcp_priority_inheritance();
}

static void pcp_update_prio_ceiling(struct pcp_semaphore* sem, int effective_prio) {
	/* This needs to be synchronized on something.
	 * Might as well use waitqueue lock for the processor.
	 * We assume this happens only before the task set starts execution,
	 * (i.e., during initialization), but it may happen on multiple processors
	 * at the same time.
	 */
	unsigned long flags;

	struct pcp_state* s = &per_cpu(pcp_state, sem->on_cpu);

	spin_lock_irqsave(&s->ceiling_blocked.lock, flags);

	sem->prio_ceiling = min(sem->prio_ceiling, effective_prio);

	spin_unlock_irqrestore(&s->ceiling_blocked.lock, flags);
}

static void pcp_init_semaphore(struct pcp_semaphore* sem, int cpu) {
	sem->owner = NULL;
	INIT_LIST_HEAD(&sem->ceiling);
	sem->prio_ceiling = INT_MAX;
	sem->on_cpu = cpu;
}

int mrsp_pcp_lock(struct litmus_lock* l) {
	struct task_struct* t = current;
	struct pcp_semaphore *sem = pcp_from_lock(l);

	/* The regular PCP uses the regular task priorities, not agent
	 * priorities. */
	int eprio = get_priority(t);
	int from = get_partition(t);
	int to = sem->on_cpu;

	if (!is_realtime(t) || from != to)
		return -EPERM;

	/* prevent nested lock acquisition in global critical section */
	if (tsk_rt(t)->num_locks_held)
		return -EBUSY;

	preempt_disable()
	;

	pcp_raise_ceiling(sem, eprio);

	preempt_enable()
	;

	tsk_rt(t)->num_local_locks_held++;

	return 0;
}

int mrsp_pcp_unlock(struct litmus_lock* l) {
	struct task_struct *t = current;
	struct pcp_semaphore *sem = pcp_from_lock(l);

	int err = 0;

	preempt_disable()
	;

	if (sem->owner != t) {
		err = -EINVAL;
		goto out;
	}

	/* The current owner should be executing on the correct CPU.
	 *
	 * If the owner transitioned out of RT mode or is exiting, then
	 * we it might have already been migrated away by the best-effort
	 * scheduler and we just have to deal with it. */
	if (unlikely(!is_realtime(t) && sem->on_cpu != smp_processor_id())) {
		TRACE_TASK(t, "PCP unlock cpu=%d, sem->on_cpu=%d\n", smp_processor_id(), sem->on_cpu);
		preempt_enable()
		;
		err = litmus_be_migrate_to(sem->on_cpu);
		preempt_disable()
		;TRACE_TASK(t, "post-migrate: cpu=%d, sem->on_cpu=%d err=%d\n", smp_processor_id(), sem->on_cpu, err);
	}
	BUG_ON(sem->on_cpu != smp_processor_id());
	err = 0;

	tsk_rt(t)->num_local_locks_held--;

	/* give it back */
	pcp_lower_ceiling(sem);

	out:
	preempt_enable()
	;

	return err;
}

int mrsp_pcp_open(struct litmus_lock* l, void* __user config) {
	struct task_struct *t = current;
	struct pcp_semaphore *sem = pcp_from_lock(l);

	int cpu, eprio;

	if (!is_realtime(t))
		/* we need to know the real-time priority */
		return -EPERM;

	if (!config)
		cpu = get_partition(t);
	else if (get_user(cpu, (int* ) config))
		return -EFAULT;

	/* make sure the resource location matches */
	if (cpu != sem->on_cpu)
		return -EINVAL;

	/* The regular PCP uses regular task priorites, not agent
	 * priorities. */
	eprio = get_priority(t);

	pcp_update_prio_ceiling(sem, eprio);

	return 0;
}

int mrsp_pcp_close(struct litmus_lock* l) {
	struct task_struct *t = current;
	struct pcp_semaphore *sem = pcp_from_lock(l);

	int owner = 0;

	preempt_disable()
	;

	if (sem->on_cpu == smp_processor_id())
		owner = sem->owner == t;

	preempt_enable()
	;

	if (owner)
		mrsp_pcp_unlock(l);

	return 0;
}

void mrsp_pcp_free(struct litmus_lock* lock) {
	kfree(pcp_from_lock(lock));
}

static struct litmus_lock_ops mrsp_pcp_lock_ops = { .close = mrsp_pcp_close, .lock = mrsp_pcp_lock, .open = mrsp_pcp_open, .unlock = mrsp_pcp_unlock,
	.deallocate = mrsp_pcp_free, };

static struct litmus_lock* mrsp_new_pcp(int on_cpu) {
	struct pcp_semaphore* sem;

	sem = kmalloc(sizeof(*sem), GFP_KERNEL);
	if (!sem)
		return NULL;

	sem->litmus_lock.ops = &mrsp_pcp_lock_ops;
	pcp_init_semaphore(sem, on_cpu);

	return &sem->litmus_lock;
}

/* ******************** DPCP support ********************** */

struct dpcp_semaphore {
	struct litmus_lock litmus_lock;
	struct pcp_semaphore pcp;
	int owner_cpu;
};

static inline struct dpcp_semaphore* dpcp_from_lock(struct litmus_lock* lock) {
	return container_of(lock, struct dpcp_semaphore, litmus_lock);
}

/* called with preemptions disabled */
static void mrsp_migrate_to(int target_cpu) {
	struct task_struct* t = current;
	mrsp_domain_t *from;

	if (get_partition(t) == target_cpu)
		return;

	if (!is_realtime(t)) {
		TRACE_TASK(t, "not migrating, not a RT task (anymore?)\n");
		return;
	}

	/* make sure target_cpu makes sense */
	BUG_ON(target_cpu >= NR_CPUS || !cpu_online(target_cpu));

	local_irq_disable()
	;

	from = task_pfp(t);
	raw_spin_lock(&from->slock);

	/* Scheduled task should not be in any ready or release queue.  Check
	 * this while holding the lock to avoid RT mode transitions.*/
	BUG_ON(is_realtime(t) && is_queued(t));

	/* switch partitions */
	tsk_rt(t)->task_params.cpu = target_cpu;

	raw_spin_unlock(&from->slock);

	/* Don't trace scheduler costs as part of
	 * locking overhead. Scheduling costs are accounted for
	 * explicitly. */
	TS_LOCK_SUSPEND
	;

	local_irq_enable()
	;
	preempt_enable_no_resched()
	;

	/* deschedule to be migrated */
	schedule();

	/* we are now on the target processor */
	preempt_disable()
	;

	/* start recording costs again */
	TS_LOCK_RESUME
	;

	BUG_ON(smp_processor_id() != target_cpu && is_realtime(t));
}

int mrsp_dpcp_lock(struct litmus_lock* l) {
	struct task_struct* t = current;
	struct dpcp_semaphore *sem = dpcp_from_lock(l);
	int eprio = effective_agent_priority(get_priority(t));
	int from = get_partition(t);
	int to = sem->pcp.on_cpu;

	if (!is_realtime(t))
		return -EPERM;

	/* prevent nested lock accquisition */
	if (tsk_rt(t)->num_locks_held ||
	tsk_rt(t)->num_local_locks_held)
		return -EBUSY;

	preempt_disable()
	;

	/* Priority-boost ourself *before* we suspend so that
	 * our priority is boosted when we resume. */

	boost_priority(t, get_priority(t));

	mrsp_migrate_to(to);

	pcp_raise_ceiling(&sem->pcp, eprio);

	/* yep, we got it => execute request */
	sem->owner_cpu = from;

	preempt_enable()
	;

	tsk_rt(t)->num_locks_held++;

	return 0;
}

int mrsp_dpcp_unlock(struct litmus_lock* l) {
	struct task_struct *t = current;
	struct dpcp_semaphore *sem = dpcp_from_lock(l);
	int err = 0;
	int home;

	preempt_disable()
	;

	if (sem->pcp.owner != t) {
		err = -EINVAL;
		goto out;
	}

	/* The current owner should be executing on the correct CPU.
	 *
	 * If the owner transitioned out of RT mode or is exiting, then
	 * we it might have already been migrated away by the best-effort
	 * scheduler and we just have to deal with it. */
	if (unlikely(!is_realtime(t) && sem->pcp.on_cpu != smp_processor_id())) {
		TRACE_TASK(t, "DPCP unlock cpu=%d, sem->pcp.on_cpu=%d\n", smp_processor_id(), sem->pcp.on_cpu);
		preempt_enable()
		;
		err = litmus_be_migrate_to(sem->pcp.on_cpu);
		preempt_disable()
		;TRACE_TASK(t, "post-migrate: cpu=%d, sem->pcp.on_cpu=%d err=%d\n", smp_processor_id(), sem->pcp.on_cpu, err);
	}
	BUG_ON(sem->pcp.on_cpu != smp_processor_id());
	err = 0;

	tsk_rt(t)->num_locks_held--;

	home = sem->owner_cpu;

	/* give it back */
	pcp_lower_ceiling(&sem->pcp);

	/* we lose the benefit of priority boosting */
	unboost_priority(t);

	mrsp_migrate_to(home);

	out:
	preempt_enable()
	;

	return err;
}

int mrsp_dpcp_open(struct litmus_lock* l, void* __user config) {
	struct task_struct *t = current;
	struct dpcp_semaphore *sem = dpcp_from_lock(l);
	int cpu, eprio;

	if (!is_realtime(t))
		/* we need to know the real-time priority */
		return -EPERM;

	if (get_user(cpu, (int* ) config))
		return -EFAULT;

	/* make sure the resource location matches */
	if (cpu != sem->pcp.on_cpu)
		return -EINVAL;

	eprio = effective_agent_priority(get_priority(t));

	pcp_update_prio_ceiling(&sem->pcp, eprio);

	return 0;
}

int mrsp_dpcp_close(struct litmus_lock* l) {
	struct task_struct *t = current;
	struct dpcp_semaphore *sem = dpcp_from_lock(l);
	int owner = 0;

	preempt_disable()
	;

	if (sem->pcp.on_cpu == smp_processor_id())
		owner = sem->pcp.owner == t;

	preempt_enable()
	;

	if (owner)
		mrsp_dpcp_unlock(l);

	return 0;
}

void mrsp_dpcp_free(struct litmus_lock* lock) {
	kfree(dpcp_from_lock(lock));
}

static struct litmus_lock_ops mrsp_dpcp_lock_ops = { .close = mrsp_dpcp_close, .lock = mrsp_dpcp_lock, .open = mrsp_dpcp_open, .unlock = mrsp_dpcp_unlock,
	.deallocate = mrsp_dpcp_free, };

static struct litmus_lock* mrsp_new_dpcp(int on_cpu) {
	struct dpcp_semaphore* sem;

	sem = kmalloc(sizeof(*sem), GFP_KERNEL);
	if (!sem)
		return NULL;

	sem->litmus_lock.ops = &mrsp_dpcp_lock_ops;
	sem->owner_cpu = NO_CPU;
	pcp_init_semaphore(&sem->pcp, on_cpu);

	return &sem->litmus_lock;
}

/* ******************** DFLP support ********************** */

struct dflp_semaphore {
	struct litmus_lock litmus_lock;

	/* current resource holder */
	struct task_struct *owner;
	int owner_cpu;

	/* FIFO queue of waiting tasks */
	wait_queue_head_t wait;

	/* where is the resource assigned to */
	int on_cpu;
};

static inline struct dflp_semaphore* dflp_from_lock(struct litmus_lock* lock) {
	return container_of(lock, struct dflp_semaphore, litmus_lock);
}

int mrsp_dflp_lock(struct litmus_lock* l) {
	struct task_struct* t = current;
	struct dflp_semaphore *sem = dflp_from_lock(l);
	int from = get_partition(t);
	int to = sem->on_cpu;
	unsigned long flags;
	wait_queue_t wait;
	lt_t time_of_request;

	if (!is_realtime(t))
		return -EPERM;

	/* prevent nested lock accquisition */
	if (tsk_rt(t)->num_locks_held ||
	tsk_rt(t)->num_local_locks_held)
		return -EBUSY;

	preempt_disable()
	;

	/* tie-break by this point in time */
	time_of_request = litmus_clock();

	/* Priority-boost ourself *before* we suspend so that
	 * our priority is boosted when we resume. */
	boost_priority(t, time_of_request);

	mrsp_migrate_to(to);

	/* Now on the right CPU, preemptions still disabled. */

	spin_lock_irqsave(&sem->wait.lock, flags);

	if (sem->owner) {
		/* resource is not free => must suspend and wait */

		init_waitqueue_entry(&wait, t);

		/* FIXME: interruptible would be nice some day */
		set_task_state(t, TASK_UNINTERRUPTIBLE);

		__add_wait_queue_tail_exclusive(&sem->wait, &wait);

		TS_LOCK_SUSPEND
		;

		/* release lock before sleeping */
		spin_unlock_irqrestore(&sem->wait.lock, flags);

		/* We depend on the FIFO order.  Thus, we don't need to recheck
		 * when we wake up; we are guaranteed to have the lock since
		 * there is only one wake up per release.
		 */

		preempt_enable_no_resched()
		;

		schedule();

		preempt_disable()
		;

		TS_LOCK_RESUME
		;

		/* Since we hold the lock, no other task will change
		 * ->owner. We can thus check it without acquiring the spin
		 * lock. */
		BUG_ON(sem->owner != t);
	} else {
		/* it's ours now */
		sem->owner = t;

		spin_unlock_irqrestore(&sem->wait.lock, flags);
	}

	sem->owner_cpu = from;

	preempt_enable()
	;

	tsk_rt(t)->num_locks_held++;

	return 0;
}

int mrsp_dflp_unlock(struct litmus_lock* l) {
	struct task_struct *t = current, *next;
	struct dflp_semaphore *sem = dflp_from_lock(l);
	int err = 0;
	int home;
	unsigned long flags;

	preempt_disable()
	;

	spin_lock_irqsave(&sem->wait.lock, flags);

	if (sem->owner != t) {
		err = -EINVAL;
		spin_unlock_irqrestore(&sem->wait.lock, flags);
		goto out;
	}

	/* check if there are jobs waiting for this resource */
	next = __waitqueue_remove_first(&sem->wait);
	if (next) {
		/* next becomes the resouce holder */
		sem->owner = next;

		/* Wake up next. The waiting job is already priority-boosted. */
		wake_up_process(next);
	} else
		/* resource becomes available */
		sem->owner = NULL;

	tsk_rt(t)->num_locks_held--;

	home = sem->owner_cpu;

	spin_unlock_irqrestore(&sem->wait.lock, flags);

	/* we lose the benefit of priority boosting */
	unboost_priority(t);

	mrsp_migrate_to(home);

	out:
	preempt_enable()
	;

	return err;
}

int mrsp_dflp_open(struct litmus_lock* l, void* __user config) {
	struct dflp_semaphore *sem = dflp_from_lock(l);
	int cpu;

	if (get_user(cpu, (int* ) config))
		return -EFAULT;

	/* make sure the resource location matches */
	if (cpu != sem->on_cpu)
		return -EINVAL;

	return 0;
}

int mrsp_dflp_close(struct litmus_lock* l) {
	struct task_struct *t = current;
	struct dflp_semaphore *sem = dflp_from_lock(l);
	int owner = 0;

	preempt_disable()
	;

	if (sem->on_cpu == smp_processor_id())
		owner = sem->owner == t;

	preempt_enable()
	;

	if (owner)
		mrsp_dflp_unlock(l);

	return 0;
}

void mrsp_dflp_free(struct litmus_lock* lock) {
	kfree(dflp_from_lock(lock));
}

static struct litmus_lock_ops mrsp_dflp_lock_ops = { .close = mrsp_dflp_close, .lock = mrsp_dflp_lock, .open = mrsp_dflp_open, .unlock = mrsp_dflp_unlock,
	.deallocate = mrsp_dflp_free, };

static struct litmus_lock* mrsp_new_dflp(int on_cpu) {
	struct dflp_semaphore* sem;

	sem = kmalloc(sizeof(*sem), GFP_KERNEL);
	if (!sem)
		return NULL;

	sem->litmus_lock.ops = &mrsp_dflp_lock_ops;
	sem->owner_cpu = NO_CPU;
	sem->owner = NULL;
	sem->on_cpu = on_cpu;
	init_waitqueue_head(&sem->wait);

	return &sem->litmus_lock;
}

/*  ---------------------------------------------------------------
 -------------------------- MrsP Support --------------------------
 --------------------------------------------------------------- */
void add_task(struct task_list *taskPtr, struct task_struct* task, struct list_head *head) {
//	BUG_ON(taskPtr == NULL);

	INIT_LIST_HEAD(&taskPtr->next);
	taskPtr->task = task;
	list_add_tail(&taskPtr->next, head);
}

void tasks_deleteAll(struct list_head *head) {
	struct list_head *iter;
	struct task_list *objPtr;

	redo:
	list_for_each(iter, head)
	{
		objPtr = list_entry(iter, struct task_list, next);
		list_del(&objPtr->next);
		kfree(objPtr);
		goto redo;
	}
}

struct task_list * task_remove(struct task_struct* task, struct list_head *head) {
	struct list_head *iter;
	struct task_list *objPtr;

	list_for_each(iter, head)
	{
		objPtr = list_entry(iter, struct task_list, next);
		if (objPtr->task == task) {
			list_del(&objPtr->next);
			return objPtr;
		}
	}
	return NULL;
}
/* ******* task queue end ******* */

/**
 * this method should only be called either by task t itself or by others (other tasks, schedulers) with the protection with sem->lock.
 */
int get_ceiling_priority(struct task_struct* t) {
	int ceiling = t->rt_param.task_params.original_priority;
	int numLocks = t->rt_param.task_params.num_mrsp_locks;

	if (t->rt_param.task_params.requesting_lock != NULL)
		ceiling = t->rt_param.task_params.requesting_lock->prio_per_cpu[t->rt_param.task_params.original_cpu];
	else if (numLocks > 0)
		ceiling = t->rt_param.task_params.mrsp_locks[numLocks - 1]->prio_per_cpu[t->rt_param.task_params.original_cpu];

	return ceiling;
}

static inline struct mrsp_semaphore*
mrsp_from_lock(struct litmus_lock* lock) {
	return container_of(lock, struct mrsp_semaphore, litmus_lock);
}

int mrsp_lock(struct litmus_lock* l) {
	struct task_struct* t = current;
	struct mrsp_semaphore *sem = mrsp_from_lock(l);
	unsigned long flags;
	unsigned int ticket;
	struct task_list *next, *next_check;
	struct task_list *taskPtr = (struct task_list *) kmalloc(sizeof(struct task_list), GFP_KERNEL);

	/* Nested Degree and Order Check*/
	if (t->rt_param.task_params.num_mrsp_locks >= t->rt_param.task_params.max_nested_degree)
		return -EINVAL;
	else if (t->rt_param.task_params.num_mrsp_locks > 0 && t->rt_param.task_params.mrsp_locks[t->rt_param.task_params.num_mrsp_locks - 1]->order <= sem->order)
		return -EINVAL;

	/* add to the request queue and hold to reference of the requesting lock. */
	preempt_disable()
	;
	spin_lock_irqsave(&sem->lock, flags);

	if (t->rt_param.task_params.helper == NULL) {
		t->rt_param.task_params.priority = sem->prio_per_cpu[get_partition(t)] < get_priority(t) ? sem->prio_per_cpu[get_partition(t)] : get_priority(t);
		t->rt_param.task_params.migrated_time = -1;
	}

	ticket = atomic_read(&sem->next_ticket);
	atomic_inc(&sem->next_ticket);
	add_task(taskPtr, t, &(sem->tasks_queue->next));
	t->rt_param.task_params.requesting_lock = sem;

	spin_unlock_irqrestore(&sem->lock, flags);
	preempt_enable()
	;

	/*Spinning for the lock.*/
	while (1) {
		if (atomic_read(&sem->owner_ticket) == ticket) {
			t->rt_param.task_params.requesting_lock = NULL;
			t->rt_param.task_params.num_mrsp_locks++;
			t->rt_param.task_params.mrsp_locks[t->rt_param.task_params.num_mrsp_locks - 1] = sem;
			tsk_rt(t)->num_locks_held++;
			break;
		}

		preempt_disable()
		;

		spin_lock_irqsave(&sem->lock, flags);

		next = list_entry((sem->tasks_queue->next.next), struct task_list, next);

		if (next != NULL && next->task != NULL && next->task != t) {
			volatile struct task_struct * next_head_copy;
			volatile mrsp_domain_t *next_head_domain_copy;
			struct task_struct *next_head_check, *next_head = next->task;
			mrsp_domain_t *next_head_domain_check, *next_head_domain;
			spinlock_t * next_lock = &per_cpu(mrsp_migrate_locks, next_head->rt_param.task_params.original_cpu);

			next_head_domain = task_pfp(next_head);
			next_head_copy = next_head;
			next_head_domain_copy = next_head_domain;
			raw_spin_lock(&next_head_domain->slock);
			spin_lock(next_lock);

			next_check = list_entry((sem->tasks_queue->next.next), struct task_list, next);
			next_head_check = next_check->task;
			next_head_domain_check = task_pfp(next_head_check);

			if (next_head == next_head_check && next_head_domain_check == next_head_domain && is_queued(next_head)
				&& get_partition(next_head) == next_head_domain->cpu && next_head != t /*&& next_head->rt_param.task_params.preempted == 0*/) {

				int leaving_assigned = 0;
				if (next_head->rt_param.task_params.leaving_priority == -1) {
					next_head->rt_param.task_params.leaving_priority = get_priority(next_head);
					leaving_assigned = 1;
				}

				if (next_head->rt_param.task_params.np_len > 0) {
					next_head->rt_param.task_params.migrated_time = -1;
					cancel_non_preemption_budget_timer(next_head);
				}

				if (fp_prio_remove(&next_head_domain->ready_queue, next_head, get_priority(next_head)) == -1
					&& fp_prio_remove(&next_head_domain->ready_queue, next_head, 0) == -1)
					BUG_ON(1);

				if (mrsp_tasks[next_head->rt_param.task_params.original_cpu * LITMUS_MAX_PRIORITY + next_head->rt_param.task_params.leaving_priority] != NULL) {
					if (leaving_assigned == 1)
						next_head->rt_param.task_params.leaving_priority = -1;

					if (get_priority(next_head) == 0) {
						if (next_head->rt_param.task_params.saved_current_priority != -1 && next_head->rt_param.task_params.saved_current_priority != 0)
							next_head->rt_param.task_params.priority = next_head->rt_param.task_params.saved_current_priority;
						else if (next_head->rt_param.task_params.leaving_priority != -1)
							next_head->rt_param.task_params.priority = next_head->rt_param.task_params.leaving_priority;
						else
							next_head->rt_param.task_params.priority = get_ceiling_priority(next_head);
					}

					requeue(next_head, next_head_domain);
					next_head->rt_param.task_params.preempted = 0;

					spin_unlock(next_lock);
					raw_spin_unlock(&next_head_domain->slock);
					spin_unlock_irqrestore(&sem->lock, flags);
					preempt_enable()
					;
				} else {
					mrsp_domain_t *ori = remote_pfp(next_head->rt_param.task_params.original_cpu);

					next_head->rt_param.task_params.helper = NULL;
					mrsp_tasks[next_head->rt_param.task_params.original_cpu * LITMUS_MAX_PRIORITY + next_head->rt_param.task_params.leaving_priority] =
						next_head;
					ori->queued_mrsp_tasks = ori->queued_mrsp_tasks + 1;
					next_head->rt_param.task_params.preempted = 1;

					spin_unlock(next_lock);
					raw_spin_unlock(&next_head_domain->slock);
					spin_unlock_irqrestore(&sem->lock, flags);
					preempt_enable()
					;
				}

			} else {
				spin_unlock(next_lock);
				raw_spin_unlock(&next_head_domain->slock);
				spin_unlock_irqrestore(&sem->lock, flags);
				preempt_enable()
				;
			}

			if (next_head != NULL && t->rt_param.task_params.enable_help && next_head->rt_param.task_params.preempted == 1) {
				schedule();
			}
		} else {
			spin_unlock_irqrestore(&sem->lock, flags);
			preempt_enable()
			;
		}
	}
	return 0;
}

int mrsp_unlock(struct litmus_lock* l) {
	struct task_struct *t = current;
	struct mrsp_semaphore *sem = mrsp_from_lock(l);
	int err = 0;
	unsigned long flags;
	struct task_list *next;
	struct task_list *my_obj = NULL;

	if (t->rt_param.task_params.num_mrsp_locks < 1 || t->rt_param.task_params.mrsp_locks[t->rt_param.task_params.num_mrsp_locks - 1] != sem) {
		err = -EINVAL;
		goto out;
	}

	/* set our status to "has nothing to do with the lock". */
	preempt_disable()
	;
	spin_lock_irqsave(&sem->lock, flags);

	next = list_entry((sem->tasks_queue->next.next), struct task_list, next);
	my_obj = task_remove(t, &(sem->tasks_queue->next));

	/* at this point the next task is able to get the lock unless being preempted or helping. */
	t->rt_param.task_params.mrsp_locks[t->rt_param.task_params.num_mrsp_locks - 1] = NULL;
	t->rt_param.task_params.num_mrsp_locks--;
	tsk_rt(t)->num_locks_held--;

	/* The helping session ends with the release of this lock */
	if (t->rt_param.task_params.helper != NULL && t->rt_param.task_params.helper->rt_param.task_params.requesting_lock == sem) {;
		t->rt_param.task_params.helper = NULL;

		if (t->rt_param.task_params.np_len > 0) {
			t->rt_param.task_params.migrated_time = -1;
			cancel_non_preemption_budget_timer(t);
		}

		if (t->rt_param.task_params.original_cpu == get_partition(t)) {
			if (t->rt_param.task_params.num_mrsp_locks == 0) {
				t->rt_param.task_params.priority = t->rt_param.task_params.original_priority;
				t->rt_param.task_params.leaving_priority = -1;
			} else {
				t->rt_param.task_params.priority = t->rt_param.task_params.leaving_priority;
				t->rt_param.task_params.leaving_priority = -1;
			}

		} else {
			t->rt_param.task_params.cpu = t->rt_param.task_params.original_cpu;
			t->rt_param.task_params.priority = get_ceiling_priority(t);
			t->rt_param.task_params.migrate_back = 1;
		}
		atomic_inc(&sem->owner_ticket);

		spin_unlock_irqrestore(&sem->lock, flags);
		preempt_enable()
		;
		schedule();
	}
	/* the task is not being helped at all */
	else if (t->rt_param.task_params.helper == NULL) {
		if (t->rt_param.task_params.np_len > 0) {
			t->rt_param.task_params.migrated_time = -1;
			cancel_non_preemption_budget_timer(t);
		}
		t->rt_param.task_params.priority = get_ceiling_priority(t);
		atomic_inc(&sem->owner_ticket);

		spin_unlock_irqrestore(&sem->lock, flags);
		preempt_enable()
		;
	} else {
		if (t->rt_param.task_params.num_mrsp_locks == 0) {
			int migrate = 0;

			printk(KERN_EMERG "task %d releases all locks but still under help. helper %d. helper wants %d. \n", t->pid, t->rt_param.task_params.helper->pid,
				t->rt_param.task_params.helper->rt_param.task_params.requesting_lock->id);

			if (t->rt_param.task_params.np_len > 0) {
				t->rt_param.task_params.migrated_time = -1;
				cancel_non_preemption_budget_timer(t);
			}

			t->rt_param.task_params.helper = NULL;
			migrate = t->rt_param.task_params.cpu != t->rt_param.task_params.original_cpu;
			t->rt_param.task_params.cpu = t->rt_param.task_params.original_cpu;
			t->rt_param.task_params.priority = t->rt_param.task_params.original_priority;

			if (migrate)
				t->rt_param.task_params.migrate_back = 1;

			atomic_inc(&sem->owner_ticket);

			spin_unlock_irqrestore(&sem->lock, flags);
			preempt_enable()
			;
			schedule();
		} else {
			atomic_inc(&sem->owner_ticket);
			spin_unlock_irqrestore(&sem->lock, flags);
			preempt_enable()
			;
		}

	}

	kfree(my_obj);

	out: return err;
}

int mrsp_open(struct litmus_lock* l, void *config) {
	struct task_struct *t = current;
	if (!is_realtime(t))
		return -EPERM;

	return 0;
}

int mrsp_close(struct litmus_lock* l) {
	struct task_struct *t = current;
	if (!is_realtime(t))
		return -EPERM;
	return 0;
}

void mrsp_free(struct litmus_lock* lock) {
	struct mrsp_semaphore *sem = mrsp_from_lock(lock);
	tasks_deleteAll(&(sem->tasks_queue->next));
	kfree(sem->prio_per_cpu);
	kfree(sem->usr_info);
	kfree(sem->tasks_queue);
	kfree(sem);
}

static struct litmus_lock_ops mrsp_lock_ops = { .close = mrsp_close, .lock = mrsp_lock, .open = mrsp_open, .unlock = mrsp_unlock, .deallocate = mrsp_free, };

static struct litmus_lock*
new_mrsp(int* config) {
	struct mrsp_semaphore* sem;
	int i = 0;
	sem = kmalloc(sizeof(*sem), GFP_KERNEL);

	if (!sem)
		return NULL;

	BUG_ON(!config);
	sem->usr_info = kmalloc(sizeof(int) * (num_possible_cpus() + 2),
	GFP_KERNEL);

	copy_from_user(sem->usr_info, config, (sizeof(int) * ( num_possible_cpus() + 2)));
	sem->prio_per_cpu = kmalloc(sizeof(int) * num_possible_cpus(), GFP_KERNEL);

	sem->id = sem->usr_info[0];
	sem->order = sem->usr_info[1];
	for (i = 0; i < num_possible_cpus(); i++) {
		sem->prio_per_cpu[i] = sem->usr_info[i + 2];
	}

	spin_lock_init(&sem->lock);
	sem->litmus_lock.ops = &mrsp_lock_ops;

	sem->tasks_queue = (struct task_list *) kmalloc(sizeof(struct task_list),
	GFP_KERNEL);
	sem->tasks_queue->task = NULL;
	INIT_LIST_HEAD(&(sem->tasks_queue->next));

	atomic_set(&sem->next_ticket, 0);
	atomic_set(&sem->owner_ticket, 0);

	return &sem->litmus_lock;
}

/*  ---------------------------------------------------------------
 -------------------------- MrsP Support End--------------------------
 --------------------------------------------------------------- */

/*  ---------------------------------------------------------------
 -------------------------- MSRP Support --------------------------
 --------------------------------------------------------------- */

static inline struct msrp_semaphore*
msrp_from_lock(struct litmus_lock* lock) {
	return container_of(lock, struct msrp_semaphore, litmus_lock);
}

int msrp_lock(struct litmus_lock* l) {
	struct task_struct* t = current;
	struct msrp_semaphore *sem = msrp_from_lock(l);
	unsigned long flags;
	unsigned int ticket;

	/* Nested Degree and Order Check*/
	if (t->rt_param.task_params.num_msrp_locks >= t->rt_param.task_params.msrp_max_nested_degree)
		return -EINVAL;
	else if (t->rt_param.task_params.num_msrp_locks > 0 && t->rt_param.task_params.msrp_locks[t->rt_param.task_params.num_msrp_locks - 1]->order <= sem->order)
		return -EINVAL;

	/* add to the request queue and hold to reference of the requesting lock. */
	preempt_disable()
	;
	spin_lock_irqsave(&sem->lock, flags);

	ticket = atomic_read(&sem->next_ticket);
	atomic_inc(&sem->next_ticket);
	t->rt_param.task_params.priority = 0;

	spin_unlock_irqrestore(&sem->lock, flags);
	preempt_enable();

	//t->rt_param.task_params.original_cpu = get_partition(t);
	//t->rt_param.task_params.cpu = 15;
	//if (sem->id == 1)
	//	schedule();

	/*Spinning for the lock.*/
	while (1) {
		if (atomic_read(&sem->owner_ticket) == ticket) {
			tsk_rt(t)->num_locks_held++;
			t->rt_param.task_params.num_msrp_locks++;
			t->rt_param.task_params.msrp_locks[t->rt_param.task_params.num_msrp_locks - 1] = sem;
			break;
		}
	}
	return 0;
}

int msrp_unlock(struct litmus_lock* l) {
	int err = 0;
	struct task_struct *t = current;
	struct msrp_semaphore *sem = msrp_from_lock(l);
	unsigned long flags;

	if (t->rt_param.task_params.num_msrp_locks < 1 || t->rt_param.task_params.msrp_locks[t->rt_param.task_params.num_msrp_locks - 1] != sem) {
		err = -EINVAL;
		goto out;
	}

	/* set our status to "has nothing to do with the lock". */
	preempt_disable()
	;
	spin_lock_irqsave(&sem->lock, flags);

	/* at this point the next task is able to get the lock unless being preempted or helping. */
	t->rt_param.task_params.msrp_locks[t->rt_param.task_params.num_msrp_locks - 1] = NULL;
	t->rt_param.task_params.num_msrp_locks--;
	tsk_rt(t)->num_locks_held--;

	if (t->rt_param.task_params.num_msrp_locks == 0)
		t->rt_param.task_params.priority = t->rt_param.task_params.original_priority;

	atomic_inc(&sem->owner_ticket);

	spin_unlock_irqrestore(&sem->lock, flags);
	preempt_enable()
	;

	out: return err;

	//t->rt_param.task_params.cpu = t->rt_param.task_params.original_cpu;
	//if (sem->id == 1)
	//	schedule();
	//return 0;
}

int msrp_open(struct litmus_lock* l, void *config) {
	struct task_struct *t = current;
	if (!is_realtime(t))
		return -EPERM;

	return 0;
}

int msrp_close(struct litmus_lock* l) {
	struct task_struct *t = current;
	if (!is_realtime(t))
		return -EPERM;
	return 0;
}

void msrp_free(struct litmus_lock* lock) {
	struct msrp_semaphore *sem = msrp_from_lock(lock);
	kfree(sem->usr_info);
	kfree(sem);
}

static struct litmus_lock_ops msrp_lock_ops = { .close = msrp_close, .lock = msrp_lock, .open = msrp_open, .unlock = msrp_unlock, .deallocate = msrp_free, };

static struct litmus_lock*
new_msrp(int* config) {
	struct msrp_semaphore* sem;
	sem = kmalloc(sizeof(*sem), GFP_KERNEL);

	if (!sem)
		return NULL;

	BUG_ON(!config);
	sem->usr_info = kmalloc(sizeof(int) * 2, GFP_KERNEL);
	copy_from_user(sem->usr_info, config, (sizeof(int) * 2));

	sem->id = sem->usr_info[0];
	sem->order = sem->usr_info[1];

	spin_lock_init(&sem->lock);
	sem->litmus_lock.ops = &msrp_lock_ops;

	atomic_set(&sem->next_ticket, 0);
	atomic_set(&sem->owner_ticket, 0);

	return &sem->litmus_lock;
}

/*  ---------------------------------------------------------------
 -------------------------- MSRP Support End--------------------------
 --------------------------------------------------------------- */

/*  ---------------------------------------------------------------
 -------------------------- FIFOP Support --------------------------
 --------------------------------------------------------------- */
static inline struct fifop_semaphore*
fifop_from_lock(struct litmus_lock* lock) {
	return container_of(lock, struct fifop_semaphore, litmus_lock);
}

int fifop_lock(struct litmus_lock* l) {
	struct task_struct* t = current;
	struct fifop_semaphore *sem = fifop_from_lock(l);
	unsigned long flags;
	struct task_list *taskPtr = (struct task_list *) kmalloc(sizeof(struct task_list), GFP_KERNEL);
	struct task_list *next;
	int goout = 0;

	/* Nested Degree and Order Check*/
	if (t->rt_param.task_params.num_fifop_locks >= t->rt_param.task_params.fifop_max_nested_degree)
		return -EINVAL;
	else if (t->rt_param.task_params.num_fifop_locks > 0
		&& t->rt_param.task_params.fifop_locks[t->rt_param.task_params.num_fifop_locks - 1]->order <= sem->order)
		return -EINVAL;

	enter: t->rt_param.task_params.need_re_request = 0;
	/* add to the request queue and hold to reference of the requesting lock. */
	preempt_disable()
	;
	spin_lock_irqsave(&sem->lock, flags);

	add_task(taskPtr, t, &(sem->tasks_queue->next));
	t->rt_param.task_params.fifop_requesting_lock = sem;
	t->rt_param.task_params.next = &taskPtr->next;

	spin_unlock_irqrestore(&sem->lock, flags);
	preempt_enable()
	;

	/*Spinning for the lock.*/
	while (!goout) {
		if (t->rt_param.task_params.need_re_request) {
			goto enter;
		}

		preempt_disable()
		;
		spin_lock_irqsave(&sem->lock, flags);

		next = list_entry((sem->tasks_queue->next.next), struct task_list, next);

		if (next->task == t) {
			tsk_rt(t)->num_locks_held++;
			t->rt_param.task_params.num_fifop_locks++;
			t->rt_param.task_params.fifop_locks[t->rt_param.task_params.num_fifop_locks - 1] = sem;
			t->rt_param.task_params.fifop_requesting_lock = NULL;
			t->rt_param.task_params.next = NULL;
			t->rt_param.task_params.priority = 0;
			goout = 1;
		}

		spin_unlock_irqrestore(&sem->lock, flags);
		preempt_enable()
		;

	}
	return 0;
}

int fifop_unlock(struct litmus_lock* l) {
	int err = 0;
	struct task_struct *t = current;
	struct fifop_semaphore *sem = fifop_from_lock(l);
	unsigned long flags;
	struct task_list *my_obj = NULL;

	if (t->rt_param.task_params.num_fifop_locks < 1 || t->rt_param.task_params.fifop_locks[t->rt_param.task_params.num_fifop_locks - 1] != sem) {
		err = -EINVAL;
		goto out;
	}

	/* set our status to "has nothing to do with the lock". */
	preempt_disable()
	;
	spin_lock_irqsave(&sem->lock, flags);

	/* at this point the next task is able to get the lock unless being preempted or helping. */
	t->rt_param.task_params.fifop_locks[t->rt_param.task_params.num_fifop_locks - 1] = NULL;
	t->rt_param.task_params.num_fifop_locks--;
	tsk_rt(t)->num_locks_held--;

	if (t->rt_param.task_params.num_fifop_locks == 0)
		t->rt_param.task_params.priority = t->rt_param.task_params.original_priority;

	my_obj = task_remove(t, &(sem->tasks_queue->next));

	spin_unlock_irqrestore(&sem->lock, flags);
	preempt_enable()
	;
	kfree(my_obj);

	out: return err;
}

int fifop_open(struct litmus_lock* l, void *config) {
	struct task_struct *t = current;
	if (!is_realtime(t))
		return -EPERM;

	return 0;
}

int fifop_close(struct litmus_lock* l) {
	struct task_struct *t = current;
	if (!is_realtime(t))
		return -EPERM;
	return 0;
}

void fifop_free(struct litmus_lock* lock) {
	struct fifop_semaphore *sem = fifop_from_lock(lock);

	tasks_deleteAll(&(sem->tasks_queue->next));
	kfree(sem->tasks_queue);

	kfree(sem->usr_info);
	kfree(sem);

}

static struct litmus_lock_ops fifop_lock_ops =
	{ .close = fifop_close, .lock = fifop_lock, .open = fifop_open, .unlock = fifop_unlock, .deallocate = fifop_free, };

static struct litmus_lock*
new_fifop(int* config) {
	struct fifop_semaphore* sem;
	sem = kmalloc(sizeof(*sem), GFP_KERNEL);

	if (!sem)
		return NULL;

	BUG_ON(!config);
	sem->usr_info = kmalloc(sizeof(int) * 2, GFP_KERNEL);
	copy_from_user(sem->usr_info, config, (sizeof(int) * 2));

	sem->id = sem->usr_info[0];
	sem->order = sem->usr_info[1];

	spin_lock_init(&sem->lock);
	sem->litmus_lock.ops = &fifop_lock_ops;

	atomic_set(&sem->next_ticket, 0);
	atomic_set(&sem->owner_ticket, 0);

	sem->tasks_queue = (struct task_list *) kmalloc(sizeof(struct task_list),
	GFP_KERNEL);
	sem->tasks_queue->task = NULL;
	INIT_LIST_HEAD(&(sem->tasks_queue->next));

	return &sem->litmus_lock;
}

/*  ---------------------------------------------------------------
 -------------------------- FIFOP Support End--------------------------
 --------------------------------------------------------------- */

/* **** lock constructor **** */

static long mrsp_allocate_lock(struct litmus_lock **lock, int type, void* __user config) {
	int err = -ENXIO, cpu;
	struct srp_semaphore* srp;
	int * prio_per_cpu = NULL;

	/* P-FP currently supports the SRP for local resources and the FMLP
	 * for global resources. */
	switch (type) {
	case FMLP_SEM:
		/* FIFO Mutex Locking Protocol */
		*lock = mrsp_new_fmlp();
		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;

	case MPCP_SEM:
		/* Multiprocesor Priority Ceiling Protocol */
		*lock = mrsp_new_mpcp(0);
		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;

	case MPCP_VS_SEM:
		/* Multiprocesor Priority Ceiling Protocol with virtual spinning */
		*lock = mrsp_new_mpcp(1);
		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;

	case DPCP_SEM:
		/* Distributed Priority Ceiling Protocol */
		if (get_user(cpu, (int* ) config))
			return -EFAULT;

		TRACE("DPCP_SEM: provided cpu=%d\n", cpu);

		if (cpu >= NR_CPUS || !cpu_online(cpu))
			return -EINVAL;

		*lock = mrsp_new_dpcp(cpu);
		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;

	case DFLP_SEM:
		/* Distributed FIFO Locking Protocol */
		if (get_user(cpu, (int* ) config))
			return -EFAULT;

		TRACE("DPCP_SEM: provided cpu=%d\n", cpu);

		if (cpu >= NR_CPUS || !cpu_online(cpu))
			return -EINVAL;

		*lock = mrsp_new_dflp(cpu);
		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;

	case SRP_SEM:
		/* Baker's Stack Resource Policy */
		srp = allocate_srp_semaphore();
		if (srp) {
			*lock = &srp->litmus_lock;
			err = 0;
		} else
			err = -ENOMEM;
		break;

	case PCP_SEM:
		/* Priority Ceiling Protocol */
		if (!config)
			cpu = get_partition(current);
		else if (get_user(cpu, (int* ) config))
			return -EFAULT;

		if (cpu >= NR_CPUS || !cpu_online(cpu))
			return -EINVAL;

		*lock = mrsp_new_pcp(cpu);
		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;

	case MRSP_SEM:

		if (!config)
			return -EFAULT;

		prio_per_cpu = (int*) config;

		if (!prio_per_cpu)
			return -EFAULT;

		*lock = new_mrsp(prio_per_cpu);

		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;

	case MSRP_SEM:
		if (!config)
			return -EFAULT;

		*lock = new_msrp((int*) config);

		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;

	case FIFOP_SEM:
		if (!config)
			return -EFAULT;

		*lock = new_fifop((int*) config);

		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;

	};

	return err;
}

#endif

static long mrsp_admit_task(struct task_struct* tsk) {
	if (task_cpu(tsk) == tsk->rt_param.task_params.cpu &&
#ifdef CONFIG_RELEASE_MASTER
		/* don't allow tasks on release master CPU */
		task_cpu(tsk) != remote_dom(task_cpu(tsk))->release_master &&
#endif
		litmus_is_valid_fixed_prio(get_priority(tsk)))
		return 0;
	else
		return -EINVAL;
}

static struct domain_proc_info mrsp_domain_proc_info;
static long mrsp_get_domain_proc_info(struct domain_proc_info **ret) {
	*ret = &mrsp_domain_proc_info;
	return 0;
}

static void mrsp_setup_domain_proc(void) {
	int i, cpu;
	int release_master =
#ifdef CONFIG_RELEASE_MASTER
		atomic_read(&release_master_cpu);
#else
		NO_CPU;
#endif
	int num_rt_cpus = num_online_cpus() - (release_master != NO_CPU);
	struct cd_mapping *cpu_map, *domain_map;

	memset(&mrsp_domain_proc_info, 0, sizeof(mrsp_domain_proc_info));
	init_domain_proc_info(&mrsp_domain_proc_info, num_rt_cpus, num_rt_cpus);
	mrsp_domain_proc_info.num_cpus = num_rt_cpus;
	mrsp_domain_proc_info.num_domains = num_rt_cpus;
	for (cpu = 0, i = 0; cpu < num_online_cpus(); ++cpu) {
		if (cpu == release_master)
			continue;
		cpu_map = &mrsp_domain_proc_info.cpu_to_domains[i];
		domain_map = &mrsp_domain_proc_info.domain_to_cpus[i];

		cpu_map->id = cpu;
		domain_map->id = i; /* enumerate w/o counting the release master */
		cpumask_set_cpu(i, cpu_map->mask);
		cpumask_set_cpu(cpu, domain_map->mask);
		++i;
	}
}

static long mrsp_activate_plugin(void) {
#if defined(CONFIG_RELEASE_MASTER) || defined(CONFIG_LITMUS_LOCKING)
	int cpu;
#endif

#ifdef CONFIG_RELEASE_MASTER
	for_each_online_cpu(cpu) {
		remote_dom(cpu)->release_master = atomic_read(&release_master_cpu);
	}
#endif

#ifdef CONFIG_LITMUS_LOCKING
	get_srp_prio = mrsp_get_srp_prio;

	for_each_online_cpu(cpu)
	{
		init_waitqueue_head(&per_cpu(mpcpvs_vspin_wait, cpu));
		per_cpu(mpcpvs_vspin, cpu) = NULL;

		pcp_init_state(&per_cpu(pcp_state, cpu));
		mrsp_doms[cpu] = remote_pfp(cpu);
		per_cpu(mrsp_fmlp_timestamp,cpu) = 0;
	}

#endif
	mrsp_setup_domain_proc();

	return 0;
}

static long mrsp_deactivate_plugin(void) {
	destroy_domain_proc_info(&mrsp_domain_proc_info);
	return 0;
}

/*	Plugin object	*/
static struct sched_plugin mrsp_plugin __cacheline_aligned_in_smp = { .plugin_name = "MRSP", .task_new = mrsp_task_new, .complete_job = complete_job,
	.task_exit = mrsp_task_exit, .schedule = mrsp_schedule, .task_wake_up = mrsp_task_wake_up, .task_block = mrsp_task_block, .admit_task = mrsp_admit_task,
	.activate_plugin = mrsp_activate_plugin, .deactivate_plugin = mrsp_deactivate_plugin, .get_domain_proc_info = mrsp_get_domain_proc_info,
#ifdef CONFIG_LITMUS_LOCKING
	.allocate_lock = mrsp_allocate_lock, .finish_switch = mrsp_finish_switch,
#endif
	};

static int __init init_pfp(void) {
	int i;

	mrsp_tasks = kmalloc(sizeof(struct task_struct*) * num_possible_cpus() * LITMUS_MAX_PRIORITY,
	GFP_KERNEL);
	for (i = 0; i < num_possible_cpus() * LITMUS_MAX_PRIORITY; i++) {
		mrsp_tasks[i] = NULL;
	}

	for (i = 0; i < num_possible_cpus(); i++) {
		spin_lock_init(&per_cpu(mrsp_migrate_locks, i));
	}

	spin_lock_init(&mrsp_migrate_lock);

	/* We do not really want to support cpu hotplug, do we? ;)
	 * However, if we are so crazy to do so,
	 * we cannot use num_online_cpu()
	 */
	for (i = 0; i < num_online_cpus(); i++) {
		mrsp_domain_init(remote_pfp(i), i);
	}

	return register_sched_plugin(&mrsp_plugin);
}

module_init(init_pfp);
