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

int cpu;
int mrsp_task_per_core;
int preemptor_per_core;

int enable_help;
int np_len;
int max_nested_degree;

int thread_count;
int preemptor;
int locking;

int* locks;
const char **lock_names;

int main(int argc, char** argv) {
	int i;
	struct thread_context *ctx;
	pthread_t *task;
	lock_names = malloc(sizeof(char *) * 100);
	lock_names[0] = "b1";
	lock_names[1] = "b2";
	lock_names[2] = "b3";
	lock_names[3] = "b4";
	lock_names[4] = "b5";
	lock_names[5] = "b6";
	lock_names[6] = "b7";
	lock_names[7] = "b8";
	lock_names[8] = "b9";
	lock_names[9] = "b10";

	lock_names[10] = "b11";
	lock_names[11] = "b12";
	lock_names[12] = "b13";
	lock_names[13] = "b14";
	lock_names[14] = "b15";
	lock_names[15] = "b16";
	lock_names[16] = "b17";
	lock_names[17] = "b18";
	lock_names[18] = "b19";
	lock_names[19] = "b20";

	lock_names[20] = "b21";
	lock_names[21] = "b22";
	lock_names[22] = "b23";
	lock_names[23] = "b24";
	lock_names[24] = "b25";
	lock_names[25] = "b26";
	lock_names[26] = "b27";
	lock_names[27] = "b28";
	lock_names[28] = "b29";
	lock_names[29] = "b30";

	lock_names[30] = "b31";
	lock_names[31] = "b32";
	lock_names[32] = "b33";
	lock_names[33] = "b34";
	lock_names[34] = "b35";
	lock_names[35] = "b36";
	lock_names[36] = "b37";
	lock_names[37] = "b38";
	lock_names[38] = "b39";
	lock_names[39] = "b40";

	lock_names[40] = "b41";
	lock_names[41] = "b42";
	lock_names[42] = "b43";
	lock_names[43] = "b44";
	lock_names[44] = "b45";
	lock_names[45] = "b46";
	lock_names[46] = "b47";
	lock_names[47] = "b48";
	lock_names[48] = "b49";
	lock_names[49] = "b50";

	lock_names[50] = "b51";
	lock_names[51] = "b52";
	lock_names[52] = "b53";
	lock_names[53] = "b54";
	lock_names[54] = "b55";
	lock_names[55] = "b56";
	lock_names[56] = "b57";
	lock_names[57] = "b58";
	lock_names[58] = "b59";
	lock_names[59] = "b60";

	lock_names[60] = "b61";
	lock_names[61] = "b62";
	lock_names[62] = "b63";
	lock_names[63] = "b64";
	lock_names[64] = "b65";
	lock_names[65] = "b66";
	lock_names[66] = "b67";
	lock_names[67] = "b68";
	lock_names[68] = "b69";
	lock_names[69] = "b70";

	lock_names[70] = "b71";
	lock_names[71] = "b72";
	lock_names[72] = "b73";
	lock_names[73] = "b74";
	lock_names[74] = "b75";
	lock_names[75] = "b76";
	lock_names[76] = "b77";
	lock_names[77] = "b78";
	lock_names[78] = "b79";
	lock_names[79] = "b80";

	lock_names[80] = "b81";
	lock_names[81] = "b82";
	lock_names[82] = "b83";
	lock_names[83] = "b84";
	lock_names[84] = "b85";
	lock_names[85] = "b86";
	lock_names[86] = "b87";
	lock_names[87] = "b88";
	lock_names[88] = "b89";
	lock_names[89] = "b90";

	lock_names[90] = "b91";
	lock_names[91] = "b92";
	lock_names[92] = "b93";
	lock_names[93] = "b94";
	lock_names[94] = "b95";
	lock_names[95] = "b96";
	lock_names[96] = "b97";
	lock_names[97] = "b98";
	lock_names[98] = "b99";
	lock_names[99] = "b100";

	//printf("fifo_test begin. \n");

	cpu = atoi(argv[1]);
	mrsp_task_per_core = atoi(argv[2]);
	preemptor_per_core = atoi(argv[3]);
	max_nested_degree = atoi(argv[4]);
	enable_help = atoi(argv[5]);
	np_len = atoi(argv[6]);
	locking = atoi(argv[7]);

	preemptor = preemptor_per_core == 0 ? 9999 : mrsp_task_per_core * cpu;
	thread_count = mrsp_task_per_core * cpu + preemptor_per_core * cpu;

	ctx = malloc(sizeof(struct thread_context) * thread_count);
	task = malloc(sizeof(pthread_t) * thread_count);
	locks = malloc(sizeof(int) * max_nested_degree);

	be_migrate_to_domain(0);
	init_litmus();

	for (i = 0; i < thread_count; i++) {
		if (i < preemptor) {
			ctx[i].id = i;
			ctx[i].cpu = i / mrsp_task_per_core + 1;
			pthread_create(task + i, NULL, rt_thread, (void *) (ctx + i));
		} else {
			ctx[i].id = i;
			ctx[i].cpu = (i - preemptor) / preemptor_per_core + 1;
			pthread_create(task + i, NULL, rt_thread, (void *) (ctx + i));
		}

	}

	for (i = 0; i < thread_count; i++)
		pthread_join(task[i], NULL);

	//printf("fifo_test finished. \n");

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
	int i;

	be_migrate_to_domain(ctx->cpu);

	if (ctx->id < preemptor) {
		/* Set up task parameters */
		init_rt_task_param(&param);
		param.exec_cost = ms2ns(500);
		param.period = ms2ns(1000);
		param.relative_deadline = ms2ns(1000);
		param.budget_policy = NO_ENFORCEMENT;
		param.cls = RT_CLASS_HARD;
		param.cpu = ctx->cpu;
		param.priority = 500;

		/** MrsP Relative **/
		param.max_nested_degree = max_nested_degree;
		param.msrp_max_nested_degree = max_nested_degree;
		param.fifop_max_nested_degree = max_nested_degree;

		param.enable_help = enable_help;
		param.np_len = np_len;
		param.mrsp_task = 1;

		init_rt_thread();

		set_rt_task_param(gettid(), &param);

		if (task_mode(LITMUS_RT_TASK) != 0) {
			printf("task mode error id: %d.! \n", ctx->id);
		}

		for (i = 0; i < max_nested_degree; i++) {

			if (locking == 7) {
				locks[i] = litmus_open_lock(locking, i + 1, lock_names[i], init_prio_per_cpu(6, i + 1, max_nested_degree - i,
				 490 - (i * 2), 490 - (i * 2), 490 - (i * 2), 490 - (i * 2)/*,
				 490 - (i * 2), 490 - (i * 2), 490 - (i * 2), 490 - (i * 2),
				 490 - (i * 2), 490 - (i * 2), 490 - (i * 2), 490 - (i * 2),
				 490 - (i * 2), 490 - (i * 2), 490 - (i * 2), 490 - (i * 2)*/));
			}

			if(locking == 8 || locking == 9){
				locks[i] = litmus_open_lock(locking, i + 1, lock_names[i], init_prio_per_cpu(2, i + 1, max_nested_degree - i));
			}

//			printf("task %d say: lock %d 's id: %d\n",ctx->id, i, locks[i]);
		}

		while (count < 1000) {

			for (i = 0; i < max_nested_degree; i++) {
				if (litmus_lock(locks[i]) != 0)
					printf("lock %d false: task %d. \n", i + 1, ctx->id);
			}

			loop_for(0.00001);

			for (i = max_nested_degree - 1; i > -1; i--) {
				if (litmus_unlock(locks[i]) != 0)
					printf("unlock %d false: task %d. \n", i + 1, ctx->id);
			}

			lt_sleep(1000000);
			count++;
		}
	} else {
		/* Pre-emptors Set up task parameters */
		init_rt_task_param(&param);
		param.exec_cost = ms2ns(1);
		param.period = ms2ns(10);
		param.relative_deadline = ms2ns(10);
		param.budget_policy = NO_ENFORCEMENT;
		param.cls = RT_CLASS_HARD;
		param.cpu = ctx->cpu;
		param.priority = 10;

		param.max_nested_degree = 0;
		param.msrp_max_nested_degree = 0;
		param.fifop_max_nested_degree = 0;

		param.enable_help = 0;
		param.np_len = 0;
		param.mrsp_task = 0;

		init_rt_thread();

		set_rt_task_param(gettid(), &param);

		if (task_mode(LITMUS_RT_TASK) != 0) {
			printf("task mode error id: %d.! \n", ctx->id);
		}

		while (count < 5000) {
			loop_for(0.00001);
			lt_sleep(100000);
			count++;
		}
	}

	task_mode(BACKGROUND_TASK);
	return NULL;
}
