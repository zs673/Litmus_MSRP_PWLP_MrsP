#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <litmus.h>
#include <time.h>

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;


struct thread_context {
	int id;				// id of the task
	int cpu;			// parition of the task
	int priority;			// priority of the task
	int execute_count;		// the current number of executions
	long long *response_time;  	// store the time
};

void* rt_thread(void *tcontext);
int job(struct thread_context *ctx);

int lock_1;
int exit_program;
int count_first;
long long sum, sum100;
double avg;


int threads; 		// number of threads in the system
int interrupter; 	// the id of first interrupter thread
int cpus; 		// number of cpu we will use. From core 1.
int executions; 	// number of times we will execute.
int helping; 		// whether the tasks will help preempted head.
int locking; 		// the locking protocol we will use.
int scheduling; 	// the scheduler we will use. 
int measure; 		// which section of we will measure
int push;		// whehter the preempted head will push to remote cpu.
int non_preemption;

int main(int argc, char** argv) {
	int i;
	long j;
	struct thread_context *ctx;
	pthread_t *task;
	FILE *f;
	char path[200];

	exit_program = 0;
	count_first = 0;

	threads = atoi(argv[1]);
	interrupter = atoi(argv[2]);
	cpus = atoi(argv[3]);

	executions = atoi(argv[4]);
	helping = atoi(argv[5]);
	locking = atoi(argv[6]);
	scheduling = atoi(argv[7]);

	measure = atoi(argv[8]);
	push = atoi(argv[9]);
	non_preemption = atoi(argv[10]);

	init_litmus();
	be_migrate_to_domain(1);

	ctx = malloc(sizeof(struct thread_context) * threads);
	task = malloc(sizeof(pthread_t) * threads);

	/* init arrays */
	for (i = 0; i < threads; i++) {
		ctx[i].response_time = malloc(sizeof(long long) * executions);
		for (j = 0; j < executions; j++) {
			ctx[i].response_time[j] = 0;
		}
	}

	/* init and create tasks */
	for (i = 0; i < threads; i++) {
		ctx[i].id = i + 1;
		ctx[i].execute_count = 0;
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

	/* open the right file */
	if(measure == 0)
		strcpy(path, "../cs/");
	if(measure == 1)
		strcpy(path, "../ml/");
	if(measure == 2)
		strcpy(path, "../rt/");

	if (scheduling == 1)
		strcat(path, "result_pull/");
	if (scheduling == 3)
		strcat(path, "result_push/");
	if (scheduling == 2)
		strcat(path, "result_swap/");
	if (scheduling == 4)
		strcat(path, "result_challenge/");

	if (threads == 1 && locking == 7)
		strcat(path, "mrsp1.txt");
	if (threads == 2 && locking == 7)
		strcat(path, "mrsp2.txt");
	if (threads == 3 && helping == 1 && locking == 7)
		strcat(path, "mrsp31.txt");
	if (threads == 3 && helping == 0 && locking == 7)
		strcat(path, "mrsp30.txt");
	if (threads == 4 && helping == 1 && locking == 7)
		strcat(path, "mrsp41.txt");
	if (threads == 4 && helping == 0 && locking == 7)
		strcat(path, "mrsp40.txt");

	f = fopen(path, "w");
	if (f == NULL) {
		printf("Error opening result file!\n");
	}
	
	/* write time to the file */
	printf("threads %d, helping %d, measure %d, scheduling %d, goes to file %s.\n", threads, helping, measure, scheduling, path);
	for (i = 0; i < threads; i++) {
		sum = 0;

		fprintf(f, "task %d access time:\n", i + 1);
		for (j = 0; j < executions; j++) {
			if (ctx[i].response_time[j] != 0) {
					sum += ctx[i].response_time[j];
					fprintf(f, "%lld\n", ctx[i].response_time[j]);
			} else
				break;
		}
		avg = sum /j;
		if (avg != 0) {
			printf("task %d on core %d executes %ld times, exec_avg1: %20.5f\n", i, ctx[i].cpu, j,
					avg);
			fprintf(f, "task %d on core %d executes %ld times, exec_avg1: %20.5f\n", i, ctx[i].cpu, j,
					avg);
		}

		free(ctx[i].response_time);
	}

	fclose(f);
	free(ctx);
	free(task);

	return 0;
}



/************* CPU cycle cosuming for 3 threads *************/
#define NUMS 500
static int num[NUMS];
static int loop_once(void) {
	int i, j = 0;
	for (i = 0; i < NUMS; i++)
		j += num[i]++;
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
	param.enable_help = helping;
	param.non_preemption_after_migrate = non_preemption;

	init_rt_thread();
	if (set_rt_task_param(gettid(), &param) != 0)
		printf("set_rt_task_param error. \n ");
	task_mode(LITMUS_RT_TASK);


	lock_1 = litmus_open_lock(7, 1, "b", init_prio_per_cpu(4, 10, 10, 10, 10));
	
	do {
		lt_sleep(10);
		job(ctx);

		count_first++;
		if (count_first >= executions)
			exit_program = 1;
		
	} while (exit_program != 1);


	task_mode(BACKGROUND_TASK);
	return NULL;
}


int job(struct thread_context *ctx) {
	int ret;
	struct timespec start, end;

		litmus_lock(lock_1);
									
		clock_gettime(CLOCK_REALTIME, &start);

		
		loop_for(ctx->id, 0.001);

		if(non_preemption == 0)
			loop_for(ctx->id, 0.72);
		if(non_preemption == 1)
			loop_for(ctx->id, 0.000504);
		
		clock_gettime(CLOCK_REALTIME, &end);


		ret = litmus_unlock(lock_1);					
		if (ret != 0)
			printf("%s\n", "unlock error");

	ctx->response_time[ctx->execute_count] = (end.tv_sec * 1000000000 + end.tv_nsec) - (start.tv_sec * 1000000000 + start.tv_nsec);
	ctx->execute_count += 1;

	return 0;
}
