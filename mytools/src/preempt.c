#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <litmus.h>
#include <time.h>

struct thread_context {
	int id;				// id of the task
	int cpu;			// parition of the task
	int priority;			// priority of the task
};

void* rt_thread(void *tcontext);
int job(struct thread_context *ctx);

int lock_1;

int threads; 		// number of threads in the system
int interrupter; 	// the id of first interrupter thread
int cpus; 		// number of cpu we will use. From core 1.

int main(int argc, char** argv) {
	int i;
	struct thread_context *ctx;
	pthread_t *task;

	threads = atoi(argv[1]);
	interrupter = atoi(argv[2]);
	cpus = atoi(argv[3]);


	printf("hi: 1 \n");

	init_litmus();
	be_migrate_to_domain(1);

	ctx = malloc(sizeof(struct thread_context) * threads);
	task = malloc(sizeof(pthread_t) * threads);

	/* init and create tasks */
	for (i = 0; i < threads; i++) {
		ctx[i].id = i + 1;
		if (ctx[i].id < interrupter) {
			ctx[i].priority = 500;
			ctx[i].cpu = ctx[i].id;
		} else {
			ctx[i].priority = 5;
			ctx[i].cpu = ctx[i].id - cpus;
		}
		pthread_create(task + i, NULL, rt_thread, (void *) (ctx + i));
	}

	for (i = 0; i < threads; i++)
		pthread_join(task[i], NULL);

	printf("hi: 2 \n");

	free(ctx);
	free(task);

	return 0;
}

/************* CPU cycle consuming for 3 threads *************/
#define NUMS 500
static int num[NUMS];
static int loop_once(void) {
	int i, j = 0;
	for (i = 0; i < NUMS; i++)
		j += num[i]++;
	return j;
}
static int loop_for(double exec_time) {
	double last_loop = 0, loop_start;
	int tmp = 0;

	double start = cputime();
	double now = cputime();

	while (now + last_loop < start + exec_time) {
		loop_start = now;
		tmp += loop_once();
		now = cputime();
		last_loop = now - loop_start;
	}

	return tmp;
}
/************* CPU cycle cosuming END *************/

void* rt_thread(void *tcontext) {
	struct thread_context *ctx = (struct thread_context *) tcontext;
	struct rt_task param;

	/* set up litmus real-time task*/
	be_migrate_to_domain(ctx->cpu);
	init_rt_task_param(&param);
	param.priority = ctx->priority;
	param.cpu = ctx->cpu;
	param.exec_cost = ms2ns(3000);
	param.period = ms2ns(10000);
	param.relative_deadline = ms2ns(15000);
	param.budget_policy = NO_ENFORCEMENT;
	param.cls = RT_CLASS_HARD;

	init_rt_thread();
	if (set_rt_task_param(gettid(), &param) != 0)
		printf("set_rt_task_param error. \n ");
	task_mode(LITMUS_RT_TASK);

	lock_1 = litmus_open_lock(1, 1, "b", init_prio_per_cpu(4, 10, 10, 10, 10));

	do {
		job(ctx);
	} while (1);

	task_mode(BACKGROUND_TASK);
	return NULL;
}

int job(struct thread_context *ctx) {
	int ret;

	if (ctx->id < interrupter) {

		litmus_lock(lock_1);

		loop_for(0.003);

		ret = litmus_unlock(lock_1);
		if (ret != 0)
			printf("%s\n", "unlock error");

	} else {
		loop_for(0.008);
	}
	return 0;
}
