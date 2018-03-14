#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <litmus.h>
#include <time.h>
#include <signal.h>

struct thread_context {
	int id;
	int cpu;
};

void* rt_thread(void *tcontext);


int main(int argc, char** argv) {
	int i;
	struct thread_context *ctx;
	pthread_t *task;

	ctx = malloc(sizeof(struct thread_context) * 1);
	task = malloc(sizeof(pthread_t) * 1);

	be_migrate_to_domain(0);
	init_litmus();

	for (i = 0; i < 1; i++) {
		ctx[i].id = 1;
		ctx[i].cpu = 1;
		pthread_create(task + i, NULL, rt_thread, (void *) (ctx + i));
	}

	for (i = 0; i < 1; i++)
		pthread_join(task[i], NULL);

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

/**
 * loop for a given seconds.
 */
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
/************* CPU cycle consuming END *************/

void* rt_thread(void *tcontext) {
	struct thread_context *ctx = (struct thread_context *) tcontext;
	struct rt_task param;
	int count = 0;

	be_migrate_to_domain(ctx->cpu);

	/* Set up task parameters */
	init_rt_task_param(&param);
	param.exec_cost = ms2ns(5000);
	param.period = ms2ns(10000);
	param.relative_deadline = ms2ns(10000);
	param.budget_policy = NO_ENFORCEMENT;
	param.cls = RT_CLASS_HARD;
	param.cpu = ctx->cpu;
	param.priority = 500;

	/** MrsP Relative **/
	param.max_nested_degree = 100;
	param.msrp_max_nested_degree = 100;
	param.fifop_max_nested_degree = 100;

	param.enable_help = 0;
	param.np_len = 0;
	param.mrsp_task = 0;

	init_rt_thread();

	set_rt_task_param(gettid(), &param);

	if (task_mode(LITMUS_RT_TASK) != 0) {
		printf("task mode error id: %d.! \n", ctx->id);
	}

	while (count < 10) {

		loop_for(3);
		sleep_next_period();
		count++;
	}

	task_mode(BACKGROUND_TASK);
	return NULL;
}
