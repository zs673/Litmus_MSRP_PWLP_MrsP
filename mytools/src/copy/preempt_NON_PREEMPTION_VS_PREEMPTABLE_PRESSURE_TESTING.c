#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <litmus.h>
#include <time.h>

struct thread_context {
	int id;
	int cpu;
	int priority;
};

void* rt_thread(void *tcontext);
int job(struct thread_context *ctx);

int lock_1;
int exit_program;
int count_first;

int threads;
int interrupter;
int cpus;
int executions;
int helping;
int non_preemption_after_migrate;

struct timespec start, end;

int main(int argc, char** argv)
{
	int i;
	struct thread_context *ctx;
	pthread_t             *task;

	exit_program = 0;
	count_first = 0;

	threads 	= atoi(argv[1]);
	interrupter = atoi(argv[2]);
	cpus 		= atoi(argv[3]);

	executions 	= atoi(argv[4]);
	helping 	= atoi(argv[5]);
	non_preemption_after_migrate 	= atoi(argv[6]);

	init_litmus();
	be_migrate_to_domain(1);

	ctx = malloc(sizeof(struct thread_context) * threads );
	task = malloc(sizeof(pthread_t) * threads );

	for (i = 0; i < threads; i++) {
		/*Control the order of thread execution*/
		ctx[i].id = i+1;
		if(ctx[i].id < interrupter){
			ctx[i].priority = 500;
			ctx[i].cpu = ctx[i].id;
		}
		else{
			ctx[i].priority = 5;
			ctx[i].cpu = ctx[i].id - cpus;
		}
		pthread_create(task + i, NULL, rt_thread, (void *) (ctx + i));
	}

	for (i = 0; i < threads; i++)
		pthread_join(task[i], NULL);

	free(ctx);
	free(task);

	printf("thread 1 total time: %lld\n", (long long) (end.tv_sec * 1000000000 + end.tv_nsec) - (start.tv_sec * 1000000000 + start.tv_nsec));

	return 0;
}

void* rt_thread(void *tcontext)
{
	struct thread_context *ctx = (struct thread_context *) tcontext;
	struct rt_task param;

	be_migrate_to_domain(ctx->cpu);
	init_rt_task_param(&param);

	param.priority = ctx->priority;
	param.cpu = ctx->cpu;

	param.exec_cost = ms2ns(3000);
	param.period = ms2ns(10000);
	param.relative_deadline = ms2ns(15000);

	param.budget_policy = NO_ENFORCEMENT;
	param.cls = RT_CLASS_HARD;
	param.enable_help = helping;
	param.non_preemption_after_migrate = non_preemption_after_migrate;

	init_rt_thread();
	set_rt_task_param(gettid(), &param);
	task_mode(LITMUS_RT_TASK);

	lock_1 = litmus_open_lock(7, 1, "b", init_prio_per_cpu(4, 10, 10, 10 ,10));
	
	if(ctx->id == 1)
		clock_gettime(CLOCK_REALTIME, &start);

	do {
			lt_sleep(10);
		job(ctx);

		/*finish the program */
		if(ctx->id == 1){
			count_first++;
			if(count_first >= executions)
				exit_program = 1;
		}
	} while (exit_program != 1);

	if(ctx->id == 1)
		clock_gettime(CLOCK_REALTIME, &end);
	

	task_mode(BACKGROUND_TASK);
	return NULL;
}

/************* CPU cycle cosuming for 3 threads *************/
#define NUMS 500
#define NUMS1 500
#define NUMS2 500
#define NUMS3 500
static int num[NUMS];
static int num1[NUMS1];
static int num2[NUMS2];
static int num3[NUMS3];
static int loop_once(void) {
	int i, j = 0;
	for (i = 0; i < NUMS; i++)
		j += num[i]++;
	return j;
}
static int loop_once1(void) {
	int i, j = 0;
	for (i = 0; i < NUMS1; i++)
		j += num1[i]++;
	return j;
}
static int loop_once2(void) {
	int i, j = 0;
	for (i = 0; i < NUMS2; i++)
		j += num2[i]++;
	return j;
}
static int loop_once3(void) {
	int i, j = 0;
	for (i = 0; i < NUMS3; i++)
		j += num3[i]++;
	return j;
}
static int loop_for(int id, double exec_time)
{
	double last_loop = 0, loop_start;
	int tmp = 0;

	double start = cputime();
	double now = cputime();

	while (now + last_loop < start + exec_time) {
		loop_start = now;
		if(id == 1)
			tmp += loop_once();
		if(id == 2)
			tmp += loop_once1();
		if(id == 3)
			tmp += loop_once2();
		if(id == 4)
			tmp += loop_once3();
		now = cputime();
		last_loop = now - loop_start;
	}

	return tmp;
}
/************* CPU cycle cosuming END *************/

int job(struct thread_context *ctx)
{
	int ret;

	if(ctx->id < interrupter){

		/*Critical Section*/
		litmus_lock(lock_1);
    	
		loop_for(ctx->id, 0.002);

		ret = litmus_unlock(lock_1);
		if(ret != 0)
			printf("%s\n", "unlock error");
	}
	else{
        loop_for(ctx->id, 0.001);
	}

	return 0;
}
