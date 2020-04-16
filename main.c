/*
 *  EQueue: an robust and efficient lock-free queue
 *  working as the communication scheme for parallelizing
 *  applications on multi-core architectures.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Copyright (c) 2019 Junchang Wang, NUPT.
 *
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <poll.h>
#include "fifo.h"

#if defined(FIFO_DEBUG)
#include <assert.h>
#endif

#define DEFAULT_TEST_SIZE 20000000

int producerAffinity[MAX_CORE_NUM];
int consumerAffinity[MAX_CORE_NUM];
extern struct queue_t queues[];

static uint64_t test_size;
uint64_t workload = 170;
uint64_t burst = 1024UL;

struct init_info {
	uint32_t cpu_id;
	pthread_barrier_t * barrier;
};

struct init_info info_producer[MAX_CORE_NUM];
struct init_info info_consumer[MAX_CORE_NUM];

#if defined(E2ELATENCY)
struct e2e_info {
	uint64_t tsc;
	uint32_t distance;
};
struct e2e_info * e2e_output_p;
struct e2e_info * e2e_output_c;

static uint64_t e2e_sample_rate = 10000000UL;
static uint64_t e2e_sample_set_size;
static uint32_t e2e_sample_power_2;

uint32_t distance(struct queue_t * q)
{
	uint32_t head = READ_ONCE(q->info.head);
	uint32_t tail = READ_ONCE(q->tail);
	uint32_t qsize = READ_ONCE(q->info.queue_size);

	uint32_t rst = head >= tail? (head - tail) : (head + qsize - tail);
	if (rst != 0)
		printf("distance: %d. head: %d, tail: %ld\n",
				rst, q->info.head, q->tail);
	return rst;
}

#endif

inline uint64_t max(uint64_t a, uint64_t b)
{
	return (a > b) ? a : b;
}

void * consumer(void *arg)
{
	uint32_t     cpu_id;
	ELEMENT_TYPE value;
	cpu_set_t    cur_mask;
	uint64_t     i;

#if defined(FIFO_DEBUG)
	ELEMENT_TYPE	old_value = 0; 
#endif

	struct init_info * init = (struct init_info *) arg;
	cpu_id = init->cpu_id;
	pthread_barrier_t *barrier = init->barrier;

	CPU_ZERO(&cur_mask);
	CPU_SET(consumerAffinity[cpu_id], &cur_mask);
	printf("consumer %d:  ---%d----\n", cpu_id, consumerAffinity[cpu_id]);
	if (sched_setaffinity(0, sizeof(cur_mask), &cur_mask) < 0) {
		printf("Error: sched_setaffinity for consumer %d\n", cpu_id);
		return NULL;
	}

#if defined(RT_SCHEDULE)
	struct sched_param param;
	param.sched_priority = 99;
	int err;
	if ( (err = sched_setscheduler(0, SCHED_FIFO, &param)) != 0) {
		printf("Error: sched_setscheduler. %d, %s\n", err, strerror(err));
		return NULL;
	}
#endif

	printf("Consumer %d created...\n", cpu_id);
	//pthread_barrier_wait(barrier);

	queues[cpu_id].start_c = rdtsc_bare();

	for (i = 1; i <= test_size; i++) {
		int flag = 0;
		while( dequeue(&queues[cpu_id], &value) != 0 ) {
			if (flag == 0) {
				queues[cpu_id].empty_counter ++;
				queues[cpu_id].traffic_empty ++;
				flag = 1;
			}
		}

#if defined(E2ELATENCY)
		if (cpu_id == 0) {
			if ((i & (e2e_sample_rate - 1)) == 0) {
				uint32_t pos = (i >> e2e_sample_power_2) - 1;
				e2e_output_c[ pos ].tsc = rdtsc_bare();
				//printf("iteration %ld, output_c[%u], tsc: %ld\n",
				//		i, pos, e2e_output_c[pos].tsc);
			}
		}
#endif

#if defined(SIMULATE_BURST)
		wait_ticks(workload);
#endif

#if defined(FIFO_DEBUG)
		if((old_value + 1) != value) {
			printf("!!!ERROR!!! in queue internal \
					(old_value: %lu, value: %lu)\n",
					old_value, value);
		}

		old_value = value;
#endif
	}
	queues[cpu_id].stop_c = rdtsc_bare();

	printf("[Queue: %d: Buffer full: %u (ratio: %f).\
			Buffer empty: %u (ration: %f)\n", 
			cpu_id, queues[cpu_id].full_counter, 
			(double)(queues[cpu_id].full_counter)/test_size, 
			queues[cpu_id].empty_counter, 
			(double)(queues[cpu_id].empty_counter)/test_size);

	pthread_exit("consumer exit!");
}

void * producer(void *arg)
{
	uint64_t start_p;
	uint64_t stop_p;
	//pthread_barrier_t *barrier = (pthread_barrier_t *)arg;
	uint64_t	i;
	cpu_set_t	cur_mask;
	struct init_info * init = (struct init_info *) arg;
	uint32_t cpu_id = init->cpu_id;
	pthread_barrier_t *barrier = init->barrier;

	CPU_ZERO(&cur_mask);
	CPU_SET(producerAffinity[cpu_id], &cur_mask);
	printf("producer %d:  ---%d----\n", cpu_id, producerAffinity[cpu_id]);
	if (sched_setaffinity(0, sizeof(cur_mask), &cur_mask) < 0) {
		printf("Error: sched_setaffinity for producer %d\n", cpu_id);
		return NULL;
	}

#if defined(RT_SCHEDULE)
	struct sched_param param;
	param.sched_priority = 99;
	int err;
	if ( (err = sched_setscheduler(0, SCHED_FIFO, &param)) != 0) {
		printf("Error: sched_setscheduleri. %d, %s\n", err, strerror(err));
		return ;
	}
#endif

	printf("Producer %d created...\n", cpu_id);
	//pthread_barrier_wait(barrier);

	start_p = rdtsc_bare();

	for (i = 1; i <= test_size + BATCH_SLICE + 1; i++) {
		int flag = 0;
		while ( enqueue(&queues[cpu_id], (ELEMENT_TYPE)i) != 0) {
			if (flag == 0) {
				queues[cpu_id].full_counter ++;
				queues[cpu_id].traffic_full ++;
				flag = 1;
			}
			wait_ticks(queues[cpu_id].penalty);
		}

#if defined(INSERT_BUG)
		if(i==(test_size >> 1)) {
			printf("Duplicating data to incur bugs\n");
			enqueue(&queues[cpu_id], (ELEMENT_TYPE)i);
		}
#endif

#if defined(E2ELATENCY)
		if( (i & (e2e_sample_rate - 1)) == 0) {
			uint32_t pos = (i >> e2e_sample_power_2) - 1;
			e2e_output_p[ pos ].distance = distance(&queues[1]);
			e2e_output_p[ pos ].tsc = rdtsc_bare();
			//printf("iteration %ld, output_p[%u], tsc: %lu, distance: %u\n",
			//		i, pos, e2e_output_p[pos].tsc, 
			//		e2e_output_p[pos].distance);
		}
#endif
#if defined(SIMULATE_BURST)
		if ( (i & (burst - 1)) == 0)
			//wait_ticks(workload * burst * (num -1));
			wait_ticks((workload + 20) * burst);
#endif
	}

	stop_p = rdtsc_bare();
#if defined(SIMULATE_BURST)
	printf("producer %ld cycles/op\n", (stop_p - start_p) / ((test_size + 1)) - workload);
#else
	printf("producer %ld cycles/op\n", (stop_p - start_p) / ((test_size + 1)));
#endif

	pthread_exit("producer exit!");
}

int processAffinity(FILE * fp)
{
	int i;

	printf("Start processing affinity setting\n");
	for (i = 0; i < MAX_CORE_NUM; i++) {
		if (EOF == fscanf(fp, "%d", &producerAffinity[i]))
			return -1;
		printf("%d:  %4d ", i, producerAffinity[i]);
		if (EOF == fscanf(fp, "%d", &consumerAffinity[i]))
			return -1;
		printf("%4d ", consumerAffinity[i]);
		printf("\n");
	}
	printf("\n");
	printf("End processing affinity setting\n");

	return 0;
}

int main(int argc, char *argv[])
{
	pthread_t	producer_thread[MAX_CORE_NUM], consumer_thread[MAX_CORE_NUM];
	void * thread_result[MAX_CORE_NUM];
	pthread_barrier_t barrier;
	int		error, opt, i, max_th;
	uint64_t queue_size, penalty;

	queue_size = DEFAULT_QUEUE_SIZE;
	test_size = DEFAULT_TEST_SIZE;
	penalty = DEFAULT_PENALTY;
	max_th = 1;
	FILE *output = NULL;
	FILE *affinity_fp = NULL;

	char * usage = 
		"Usage: fifo [-c consumers  (default: 1)] \n\
		[-t test_size   (default:  20,000,000)]\n\
		[-s sample once (default:  10,000,000)]\n\
		[-q queue_size  (default: 1024*2 )]\n\
		[-p penalty     (default: 1000 cycles)]\n\
		[-o output      (default: terminal)]\n\
		[-w workload    (default: 170)]\n\
		[-r burst rate  (default: 1024)]\n\
		[-a affinity conf. (default: affinity.tree.conf)]\n\
		[-h help ]";

	while ((opt = getopt(argc, argv, "hc:t:s:q:p:o:w:r:a:")) != -1) {
		switch (opt) {
			case 'c':
				max_th = atoi(optarg);
				break;
			case 't':
				test_size = atoll(optarg);
				printf("===== Number of items to produce: %ld. =====\n", test_size);
				break;
			case 's':
#if defined(E2ELATENCY)
				e2e_sample_rate = atoll(optarg); 
#else
				printf("===== E2ELATENCY is not specified. Argument -s is not usable. =====\n");
#endif
				break;
			case 'q':
				queue_size = atoll(optarg);
				printf("===== queue size %ld. =====\n", queue_size);
				break;
			case 'w':
				workload = atoll(optarg);
				printf("===== workload for consumer: %ld. =====\n", workload);
				break;
			case 'r':
				burst = atoll(optarg);
				printf("===== burst rate for producer: %ld. =====\n", burst);
				break;
			case 'p':
				penalty = atoll(optarg);
				printf("===== Penalty (cycles) %ld. =====\n", penalty);
				break;
			case 'o':
				output = fopen(optarg, "w");
				if (output == NULL) {
					printf("Error in creating output file %s\n", optarg);
					return -1;
				}
				break;
			case 'h':
				printf("%s\n", usage);
				exit(0);
			case 'a':
				printf("affinity file: %s\n", optarg);
				affinity_fp = fopen(optarg, "r");
				if (affinity_fp == NULL) {
					fprintf(stdout, "Incorrect affinity file parameter.\n");
					printf("%s\n", usage);
					return -1;
				}
				if (processAffinity(affinity_fp) != 0 ) {
					fprintf(stdout, "Incorrect affinity file format1.\n");
					return -1;
				}
				break;
			default:
				printf("%s\n", usage);
				exit(-1);
		}
	}

#if defined(E2ELATENCY)
	e2e_sample_set_size = (uint64_t) test_size / e2e_sample_rate;
	uint64_t sample_rate_t = e2e_sample_rate;
	uint64_t power_t;
	for (power_t = 0; sample_rate_t > 1; power_t++) {
		sample_rate_t /= 2;
	}
	e2e_sample_power_2 = power_t;

	printf("===== End-to-end latency sample rate: %ld (2^%d). Set size: %ld =====\n", e2e_sample_rate, e2e_sample_power_2, e2e_sample_set_size);

	if (e2e_sample_set_size < 1) {
		printf("Error: The result of test_size/sample_rate must be larger than 1.\n");
		exit(-1);
	}
#endif

	if (affinity_fp == NULL) {
		char affinity_file[64] = "affinity.tree.conf";

		printf("affinity file: %s\n", affinity_file);
		affinity_fp = fopen(affinity_file, "r");
		if (affinity_fp == NULL) {
			fprintf(stdout, "Incorrect affinity file parameter.\n");
			printf("%s\n", usage);
			return -1;
		}
		if (processAffinity(affinity_fp) != 0 ) {
			fprintf(stdout, "Incorrect affinity file format2.\n");
			return -1;
		}
	}

	if (max_th < 1) {
		max_th = 1;
		printf("Minimum thread (consumer) number is 1\n");
	}
	if (max_th > MAX_CORE_NUM) {
		max_th = MAX_CORE_NUM;
		printf("Maximum core number is %d\n", max_th);
	}

	printf("Test ready to run. Parameters: penalty: %ld, workload: %ld, burst rate: %ld\n",
			penalty, workload, burst);

#if defined(E2ELATENCY)
	e2e_output_c = (struct e2e_info *) calloc(e2e_sample_set_size , sizeof(struct e2e_info));
	e2e_output_p = (struct e2e_info *) calloc(e2e_sample_set_size , sizeof(struct e2e_info));
#endif

	srand((unsigned int)rdtsc_bare());

	for (i=0; i<max_th; i++) {
		queue_init(&queues[i], queue_size, penalty);
	}

	error = pthread_barrier_init(&barrier, NULL, max_th * 2);
	if (error != 0) {
		perror("BW");
		return 1;
	}

	for (i=0; i<max_th; i++) {
		info_consumer[i].cpu_id = i;
		info_consumer[i].barrier = &barrier;
		error = pthread_create(&consumer_thread[i], NULL, 
				consumer, &info_consumer[i]);
	}
	if (error != 0) {
		perror("cannot create thread for consumer");
		return 1;
	}

	for (i=0; i < max_th; i++) {
		info_producer[i].cpu_id = i;
		info_producer[i].barrier = &barrier;
		error = pthread_create(&producer_thread[i], NULL, 
				producer, &info_producer[i]);
		poll(NULL, 0, 1);	
	}
	if (error != 0) {
		perror("cannot create thread for producer");
		return 1;
	}

	for (i = 0; i < max_th; i++) {
		error = pthread_join(consumer_thread[i], &thread_result[i]);
		if (error !=0) {
			perror("Thread join failed");
			return -1;
		}
	}

#if defined(E2ELATENCY)
	if (output != NULL) {
		fprintf(output, "tsc_p\t\t tsc_c\t\t tsc_diff    distance_p_c      \n"); 

		for (i=0; i<e2e_sample_set_size; i++) {
			fprintf(output, "%ld    %ld   %8ld   %6d \n", 
					e2e_output_p[i].tsc, e2e_output_c[i].tsc, 
					e2e_output_c[i].tsc - e2e_output_p[i].tsc,
					e2e_output_p[i].distance);
		}
	}
	else {
		for (i=0; i<e2e_sample_set_size; i++) {
			printf(" %ld  %ld, diff: %ld, Queue distance: %d \n", 
					e2e_output_p[i].tsc, e2e_output_c[i].tsc, 
					e2e_output_c[i].tsc - e2e_output_p[i].tsc,
					e2e_output_p[i].distance);
		}
	}
#endif

	for (i=1; i<max_th; i++) {
#if defined(SIMULATE_BURST)
		printf("consumer: %ld cycles/op\n", 
				((queues[i].stop_c - queues[i].start_c) / (test_size + 1)) - workload);
#else
		printf("consumer: %ld cycles/op\n", 
				((queues[i].stop_c - queues[i].start_c) / (test_size + 1)));
#endif
	}

	if (output != NULL)
		fclose(output);


	return 0;
}
