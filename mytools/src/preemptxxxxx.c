#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <litmus.h>
#include <time.h>


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
		fprintf(f, "task %d access time:\n", i + 1);
		for (j = 0; j < executions; j++) {
			if (ctx[i].response_time[j] != 0) {
					printf("task %d on core %d executes %ld times, exec_avg1: %lld\n", i, ctx[i].cpu, j,
					ctx[i].response_time[j]);
					fprintf(f, "task %d on core %d executes %ld times, exec_avg1: %lld\n", i, ctx[i].cpu, j,
					ctx[i].response_time[j]);
			} else
				break;
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

	/* wait for t2 and t3 to start */
	if(ctx->id == 1){
		sleep(2);
	}

	/* get the lock */
	lock_1 = litmus_open_lock(7, 1, "b",init_prio_per_cpu(4, 10, 10, 10, 10));
	
	
	do {
		lt_sleep(10);

		job(ctx);

		/*finish the program */
		if (ctx->id == 1) {
			count_first++;
			if (count_first >= executions)
				exit_program = 1;
		}
	} while (exit_program != 1);

	/*notify the threads to exit*/
	if (ctx->id == 1) {
		sleep(2);

		pthread_mutex_lock(&mutex1);
		pthread_cond_signal(&cond1);
		pthread_mutex_unlock(&mutex1);
	}

	task_mode(BACKGROUND_TASK);
	return NULL;
}



int job(struct thread_context *ctx) {
	int ret;
	struct timespec start, end;

	if (ctx->id < interrupter) {
		/*t2 wait here*/
		if (ctx->id == 2) {
			pthread_mutex_lock(&mutex1);
			pthread_cond_wait(&cond1, &mutex1);
			pthread_mutex_unlock(&mutex1);
			
			loop_for(ctx->id, 0.001);
		}

		if (ctx->id == 1) {
			enter_np();
			/*wake up t2 to queue up*/
			pthread_mutex_lock(&mutex1);
			pthread_cond_signal(&cond1);
		}
		
		/* get the time when t1 is going to lock */		
		if (ctx->id == 1)

									/* record the time if we want lock time or response time*/
									if(measure != 0)
										clock_gettime(CLOCK_REALTIME, &start);

		litmus_lock(lock_1);
		
									/* record the time if we want lock time*/
									if(measure == 1)
										clock_gettime(CLOCK_REALTIME, &end);

									/* record the time if we want critical section time*/
									if(measure == 0)
										clock_gettime(CLOCK_REALTIME, &start);

		if (ctx->id == 1){
			/* let t2 run*/
			pthread_mutex_unlock(&mutex1);
			/* let t3 run here if we do not want push */
			if(push == 0)
				exit_np();
			loop_for(ctx->id, 0.002);
			/* let t3 run here if we want push */
			if(push == 1)
				exit_np();
			loop_for(ctx->id, 10);
		}
		else{
			// loop_for(ctx->id, 0.003);
		}

									/* record the time if we want critical section time or response time*/
									if(measure == 0)
										clock_gettime(CLOCK_REALTIME, &end);


		ret = litmus_unlock(lock_1);
									if(measure == 2)
										clock_gettime(CLOCK_REALTIME, &end);
		if (ret != 0)
			printf("%s\n", "unlock error");

	} else {
		/*High priority task computation*/
		clock_gettime(CLOCK_REALTIME, &start);
		loop_for(ctx->id, 0.000008);
		clock_gettime(CLOCK_REALTIME, &end);
	}

	ctx->response_time[ctx->execute_count] = (end.tv_sec * 1000000000 + end.tv_nsec) - (start.tv_sec * 1000000000 + start.tv_nsec);
	return 0;
}
