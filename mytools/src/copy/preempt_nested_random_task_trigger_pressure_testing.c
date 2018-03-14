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
	
	do {
		if(ctx->id < interrupter)
			lt_sleep(600000);
		else
			lt_sleep(100000);
		job(ctx);

		/*finish the program */
		if(ctx->id == 1){
			count_first++;
			if(count_first >= executions)
				exit_program = 1;
		}
	} while (exit_program != 1);
	

	task_mode(BACKGROUND_TASK);
	return NULL;
}

/*CPU cycle cosuming*/
#define NUMS 4096
static int num[NUMS];
static int loop_once(void)
{
	int i, j = 0;
	for (i = 0; i < NUMS; i++)
		j += num[i]++;
	return j;
}

int job(struct thread_context *ctx)
{
	int x, ret;

	if(ctx->id < interrupter){

		/*Critical Section*/
		litmus_lock(lock_1);
    	
		for(x=0; x<1000;x++){
			loop_once();
		}

		ret = litmus_unlock(lock_1);
		if(ret != 0)
			printf("%s\n", "unlock error");
	}
	else{
        enter_np();
		for(x=0; x<1000;x++){
				loop_once();
		}
		exit_np();
	}

	return 0;
}
