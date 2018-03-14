
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <litmus.h>
#include <time.h>
#include <sys/time.h>


void* run(void *tcontext);

int exit_program;
int count_first;

long long sum, sum100;
double avg,avg1;
long long **response_time;

int loop_times;

int main(int argc, char** argv) {
	cpu_set_t cs;
	struct sched_param param;
	int i, ret;
	long j;
	FILE *f;
	pthread_t task;

	

   	pthread_attr_t t1_attr;
    int policy = SCHED_FIFO;

    loop_times = atoi(argv[1]);
    pthread_attr_init(&t1_attr);

    response_time = malloc(sizeof(long long) * 2);
	/* init arrays */
	for (i = 0; i < 2; i++) {
		response_time[i] = malloc(sizeof(long long) * 10000);
		for (j = 0; j < 10000; j++) {
			response_time[i][j] = 0;
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


	/* init and create tasks */
	param.sched_priority = 99;
	CPU_ZERO(&cs);
	CPU_SET(1, &cs);

	pthread_attr_setaffinity_np(&t1_attr, sizeof(cs), &cs);
	pthread_attr_setschedpolicy(&t1_attr, policy);
	pthread_attr_setschedparam(&t1_attr, &param);
	pthread_attr_setinheritsched(&t1_attr, PTHREAD_EXPLICIT_SCHED);

	pthread_create(&task, &t1_attr, run, (void *) NULL);

	pthread_join(task, NULL);

	f = fopen("clock.txt", "w");
	if (f == NULL) {
		printf("Error opening result file!\n");
		exit(1);
	}

	/* write time to the file */
	for (i = 0; i < 2; i++) {
		sum = 0;
		sum100 = 0;
		avg = 0;
		fprintf(f, "clock %d:\n", i + 1);
		for (j = 0; j < 10000; j++) {
			if (response_time[i][j] != 0) {
				sum += response_time[i][j];
				sum100 += response_time[i][j];
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
			printf("clock %d executes %ld times, exec_avg1: %20.5f\n", i, j, avg);
			fprintf(f, "clock %d executes %ld times, exec_avg1: %20.5f\n", i, j, avg);
		}

		if(i == 0)
			avg1 = avg;

		free(response_time[i]);
	}

	printf("differneces: %20.5f\n", avg1 - avg);

	free(response_time);
	fclose(f);

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
/************* CPU cycle cosuming END *************/

void* run(void *tcontext) {
	struct timespec start, end, start1, end1;
	// struct timeval start, end;
	int i;

	exit_program = 0;
	

	do {
		lt_sleep(10);

		clock_gettime(CLOCK_REALTIME, &start);
		// gettimeofday(&start, NULL);

		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start1);


		for(i = 0; i < loop_times; i++)
			loop_once();


		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end1);
		
		clock_gettime(CLOCK_REALTIME, &end);	


		// gettimeofday(&end, NULL);	

									
		response_time[0][count_first] = (end.tv_sec * 1000000000 + end.tv_nsec) - (start.tv_sec * 1000000000 + start.tv_nsec);
		// response_time[0][count_first] = (end.tv_sec * 1000000000 + end.tv_usec * 1000) - (start.tv_sec * 1000000000 + start.tv_usec * 1000);

		response_time[1][count_first] = (end1.tv_sec * 1000000000 + end1.tv_nsec) - (start1.tv_sec * 1000000000 + start1.tv_nsec);


		/*finish the program */
		count_first++;
		if (count_first >= 10000)
			exit_program = 1;
		
	} while (exit_program != 1);

	return NULL;
}

