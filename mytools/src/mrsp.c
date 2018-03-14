
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <litmus.h>
#include <string.h>

/* MrsP primitives*/
static pthread_spinlock_t spin_lock;
static cpu_set_t resource_affinity;

static volatile pthread_t head;
static volatile int owner_ticket;
static volatile int next_ticket;

int *prio_per_cpu;
int online_cpu;

void new_mrsp_semaphore(int number_of_cpu) {
	int i, ret;

	/* initialize resource ceiling */
	prio_per_cpu = malloc(sizeof(int) * number_of_cpu);
	for (i = 0; i < number_of_cpu; i++) {
		prio_per_cpu[i] = 10;
	}

	/* init spin lock*/
	ret = pthread_spin_init(&spin_lock, PTHREAD_PROCESS_PRIVATE);
	if (ret != 0)
		printf("pthread_spin_init created failed");

	CPU_ZERO(&resource_affinity);
	online_cpu = number_of_cpu;

	head = 0;
	owner_ticket = 0;
	next_ticket = 0;
}

void destory_mrsp_semaphore() {
	free(prio_per_cpu);
}


int mrsp_lock(int cpu, int helping) {
	int ret, ticket;

	/* Set Ceiling priority */
	ret = pthread_setschedprio(pthread_self(), prio_per_cpu[cpu]);
	if (ret != 0)
		printf("pthread set param fails %d %d. \n", ret, errno);

	ret = pthread_spin_lock(&spin_lock);
	if (ret != 0)
		printf("thread spin lock fails %d. \n", errno);

	/* add processor to resource affinity */
	CPU_SET(cpu, &resource_affinity);

	/* update the affinity head (if not null)*/
	if (head != 0) {
		if (helping == 1) {
			ret = pthread_setaffinity_np(head, sizeof(cpu_set_t),
					&resource_affinity);
			if (ret != 0)
				printf("head set affinity fails %d. \n", errno);
		}
	} else
		head = pthread_self();

	/* get a ticket*/
	ticket = next_ticket;
	next_ticket++;

	ret = pthread_spin_unlock(&spin_lock);
	if (ret != 0)
		printf("thread spin unlock fails %d. \n", errno);

	while (1) {
		/* we now become the header */
		if (owner_ticket == ticket) {
			/* we set the header and update the resource affinity in case that it is updated*/
			ret = pthread_spin_lock(&spin_lock);
			if (ret != 0)
				printf("unlock thread spin lock fails %d. \n", errno);

			head = pthread_self();
			if (helping == 1) {
				ret = pthread_setaffinity_np(head, sizeof(cpu_set_t),
						&resource_affinity);
				if (ret != 0)
					printf("lock head set affinity fails %d. \n", errno);
			}

			/* Raise the priority by 1 in case of helping */
			ret = pthread_setschedprio(head, prio_per_cpu[cpu] + 1);
			if (ret != 0)
				printf("lock head raise priority fails %d %d. \n", ret, errno);

			ret = pthread_spin_unlock(&spin_lock);
			if (ret != 0)
				printf("lock thread spin unlock fails %d. \n", errno);

			break;
		}
		lt_sleep(1);
	}

	return 0;
}

int mrsp_unlock(int cpu, int priority, int helping) {
	int ret;
	cpu_set_t mask;

	/* the calling thread should hold the lock */
	if (pthread_self() != head) {
		printf("you are not the owner task: %ld. \n", pthread_self());
		return -1;
	}

	/* remove the processor from resource ceiling and increment the owner_ticket */
	ret = pthread_spin_lock(&spin_lock);
	if (ret != 0)
		printf("unlock thread spin lock fails %d. \n", errno);

	CPU_CLR(cpu, &resource_affinity);
	owner_ticket++;
	head = 0;

	ret = pthread_spin_unlock(&spin_lock);
	if (ret != 0)
		printf("unlock thread spin unlock fails %d. \n", errno);

	/* restore task affinity and priority*/
	if (helping == 1) {
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &mask);
		if (ret != 0)
			printf("unlock thread restore affinity fails %d. \n", errno);
	}

	ret = pthread_setschedprio(pthread_self(), priority);
	if (ret != 0)
		printf("unlock thread restore priority fails %d %d. \n", ret, errno);

	return 0;
}












pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;

struct thread_context {
	int id;				// id of the task
	int cpu;			// parition of the task
	int priority;			// priority of the task
	int execute_count;		// the current number of executions
	long long *response_time;	// store the time
};

void* run(void *tcontext);

int threads; 		// number of threads in the system
int interrupter;	// the id of first interrupter thread
int cpus;		// number of cpu we will use. From core 1.
int executions;		// number of times we will execute.
int helping;		// whether the tasks will help preempted head.
int locking;		// the locking protocol we will use.
int measure; 		// which section of we will measure

int lock_1;
int exit_program;
int count_first;
long long sum, sum100;
double avg;

int main(int argc, char** argv) {
	cpu_set_t cs;
	struct sched_param param;
	int i, ret;
	long j;
	FILE *f;
	pthread_t *task;
	struct thread_context *ctx;
	char path[200];
   	pthread_attr_t t1_attr;
    	int policy = SCHED_FIFO;

    	pthread_attr_init(&t1_attr);
	exit_program = 0;
	count_first = 0;

	threads 	= atoi(argv[1]);
	interrupter 	= atoi(argv[2]);
	cpus 		= atoi(argv[3]);
	executions 	= atoi(argv[4]);
	helping 	= atoi(argv[5]);
	locking 	= atoi(argv[6]);
	measure		= atoi(argv[7]);

	task = malloc(sizeof(pthread_t) * threads);
	ctx = malloc(sizeof(struct thread_context) * threads);

	/* init arrays */
	for (i = 0; i < threads; i++) {
		ctx[i].response_time = malloc(sizeof(long long) * executions);
		for (j = 0; j < executions; j++) {
			ctx[i].response_time[j] = 0;
		}
	}

	/*Set thread attrs*/
	param.sched_priority = 1;
	ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	if (ret != 0)
		printf("main thread set priority fails %d %d. \n", ret, errno);

	CPU_ZERO(&cs);
	CPU_SET(0, &cs);
	ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cs);
	if (ret != 0)
		printf("main thread set affinity fails. \n");

	new_mrsp_semaphore(8);

	/* init and create tasks */
	for (i = 0; i < threads; i++) {
		ctx[i].id = i + 1;
		ctx[i].execute_count = 0;
		if (ctx[i].id < interrupter) {
			ctx[i].priority = 5;
			ctx[i].cpu = ctx[i].id;
		} else {
			ctx[i].priority = 50;
			ctx[i].cpu = ctx[i].id - cpus;
		}

		param.sched_priority = ctx[i].priority;
		CPU_ZERO(&cs);
		CPU_SET(ctx[i].cpu, &cs);

		pthread_attr_setaffinity_np(&t1_attr, sizeof(cs), &cs);
		pthread_attr_setschedpolicy(&t1_attr, policy);
		pthread_attr_setschedparam(&t1_attr, &param);
		pthread_attr_setinheritsched(&t1_attr, PTHREAD_EXPLICIT_SCHED);

		pthread_create(task + i, &t1_attr, run, (void *) (ctx + i));
	}

	for (i = 0; i < threads; i++)
		pthread_join(task[i], NULL);

	destory_mrsp_semaphore();

	/* open the right file */
	if(measure == 0)
		strcpy(path, "../cs/");
	if(measure == 1)
		strcpy(path, "../ml/");
	if(measure == 2)
		strcpy(path, "../rt/");

	if (threads == 1 && locking == 7)
		strcat(path, "result_generic/mrsp1.txt");
	if (threads == 2 && locking == 7)
		strcat(path, "result_generic/mrsp2.txt");
	if (threads == 3 && helping == 1 && locking == 7)
		strcat(path, "result_generic/mrsp31.txt");
	if (threads == 3 && helping == 0 && locking == 7)
		strcat(path, "result_generic/mrsp30.txt");
	if (threads == 4 && helping == 1 && locking == 7)
		strcat(path, "result_generic/mrsp41.txt");
	if (threads == 4 && helping == 0 && locking == 7)
		strcat(path, "result_generic/mrsp40.txt");

	f = fopen(path, "w");
	if (f == NULL) {
		printf("Error opening result file!\n");
		exit(1);
	}

	/* write time to the file */
	printf("threads %d, helping %d, measure %d, goes to file %s.\n", threads, helping, measure, path);
	printf("Linux Generic. \n");
	for (i = 0; i < threads; i++) {
		sum = 0;
		sum100 = 0;
		avg = 0;
		fprintf(f, "task %d access time:\n", i + 1);
		for (j = 0; j < executions; j++) {
			if (ctx[i].response_time[j] != 0) {
				sum += ctx[i].response_time[j];
				sum100 += ctx[i].response_time[j];
				if(j % 100 == 99){
					avg = sum100/100;
					fprintf(f, "%20.5f\n", avg);
					sum100 = 0;
				}
			} else
				break;
		}
		avg = sum / j;
		if (avg != 0) {
			printf("task %d on core %d executes %ld times, exec_avg1: %20.5f\n", i, ctx[i].cpu, j,
					avg);
			fprintf(f, "task %d on core %d executes %ld times, exec_avg1: %20.5f\n", i, ctx[i].cpu, j,
					avg);
		}

		free(ctx[i].response_time);
	}

	free(task);
	fclose(f);
	free(ctx);

	return 0;
}

/************* CPU cycle cosuming for 3 threads *************/
#define NUMS 500
#define NUMS1 500
#define NUMS2 500
static int num[NUMS];
static int num1[NUMS1];
static int num2[NUMS2];
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
		now = cputime();
		last_loop = now - loop_start;
	}

	return tmp;
}
/************* CPU cycle cosuming END *************/

struct timespec t2_start_time, t2_wake_up;

void* run(void *tcontext) {
	struct thread_context *ctx = (struct thread_context *) tcontext;
	struct timespec start, end;
	double t2exec;
	
	/* wait for t2 and t3 to start */
	if(ctx->id == 1)
		sleep(2);

	do {
		/* wait for t2 and t3 to finish */
		if(ctx->id == 1){
			lt_sleep(10);
			loop_for(ctx->id, 0.006);
		}

		if (ctx->id < interrupter) {
			/*t2 wait here*/
			if (ctx->id == 2) {
				pthread_mutex_lock(&mutex1);
				pthread_cond_wait(&cond1, &mutex1);
				pthread_mutex_unlock(&mutex1);

				/* t2 will release 1ms after t1 gets the lock*/
				clock_gettime(CLOCK_REALTIME, &t2_wake_up);
				t2exec = ( (double)((t2_start_time.tv_sec * 1000000000 + t2_start_time.tv_nsec) - (t2_wake_up.tv_sec * 1000000000 + t2_wake_up.tv_nsec)) + 1000000 ) / 1000000000;
				loop_for(ctx->id, t2exec);
			}
			
			if (ctx->id == 1) {
				enter_np();
				/*notify t3 to preempt*/
				pthread_mutex_lock(&mutex);
				pthread_cond_signal(&cond);
				
				/*wake up t2 to queue up*/
				pthread_mutex_lock(&mutex1);
				pthread_cond_signal(&cond1);
			}
			
			/* get the time when t1 is going to lock */	
			if (ctx->id == 1)
				clock_gettime(CLOCK_REALTIME, &t2_start_time);

									/* record the time if we want lock time or response time*/
									if(measure != 0)
										clock_gettime(CLOCK_REALTIME, &start);

			mrsp_lock(ctx->cpu, helping);

									/* record the time if we want lock time*/
									if(measure == 1)
										clock_gettime(CLOCK_REALTIME, &end);

									/* record the time if we want critical section time*/
									if(measure == 0)
										clock_gettime(CLOCK_REALTIME, &start);
			
			if (ctx->id == 1){
				/* let t2 run*/
				pthread_mutex_unlock(&mutex1);
				loop_for(ctx->id, 0.002);
				/* let t3 run */
				pthread_mutex_unlock(&mutex);
				exit_np();
				loop_for(ctx->id, 0.001);
			}
			else{
				loop_for(ctx->id, 0.003);
			}

									/* record the time if we want critical section time or response time*/
									if(measure == 0)
										clock_gettime(CLOCK_REALTIME, &end);			
									
			mrsp_unlock(ctx->cpu, ctx->priority, helping);
									if(measure == 2)
										clock_gettime(CLOCK_REALTIME, &end);
		} else {
			/*High priority task computation*/
			pthread_mutex_lock(&mutex);
			pthread_cond_wait(&cond, &mutex);
			pthread_mutex_unlock(&mutex);

			clock_gettime(CLOCK_REALTIME, &start);
			loop_for(ctx->id, 0.008);
			clock_gettime(CLOCK_REALTIME, &end);

		}

		/* store the time in the arrays */
		ctx->response_time[ctx->execute_count] = (end.tv_sec * 1000000000 + end.tv_nsec) - (start.tv_sec * 1000000000 + start.tv_nsec);
		ctx->execute_count += 1;

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

		pthread_mutex_lock(&mutex);
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&mutex);

		pthread_mutex_lock(&mutex1);
		pthread_cond_signal(&cond1);
		pthread_mutex_unlock(&mutex1);
	}

	return NULL;
}

