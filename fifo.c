/*
 *  tinyQueue: an efficient lock-free queue for pipeline parallelism 
 *  on multi-core architectures.
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
 *  Copyright (c) 2017 Junchang Wang, NUPT.
 *
*/

#include "fifo.h"
#include <sched.h>

#if defined(FIFO_DEBUG)
#include <assert.h>
#endif

struct queue_t queues[MAX_CORE_NUM];

inline uint64_t rdtsc_bare()
{
	uint64_t        time;
	uint32_t        msw, lsw;
	__asm__         __volatile__(
			"rdtsc\n\t"
			"mov %%edx, %0\n\t"
			"mov %%eax, %1\n\t"
			: "=r" (msw), "=r"(lsw)
			:   
			: "%rax","%rdx");
	time = ((uint64_t) msw << 32) | lsw;
	return time;
}

inline uint64_t rdtsc_barrier(void)
{
	uint64_t    time;
	uint32_t    cycles_high, cycles_low;
	__asm__     __volatile__(
			"cpuid\n\t"
			"rdtscp\n\t"
			"mov %%edx, %0\n\t"
			"mov %%eax, %1\n\t"
			"cpuid\n\t"
			: "=r"(cycles_high), "=r"(cycles_low)
			:
			: "%rax", "%rbx", "%rcx", "%rdx");
	time = ((uint64_t) cycles_high<< 32) | cycles_low;
	return time;
}

inline void wait_ticks(uint64_t ticks)
{
	uint64_t        current_time;
	uint64_t        time = rdtsc_bare();
	time += ticks;
	do {
		current_time = rdtsc_bare();
	} while (current_time < time);
}

static ELEMENT_TYPE ELEMENT_ZERO = 0x0UL;

/*************************************************/
/********** Queue Functions **********************/
/*************************************************/

void queue_init(struct queue_t *q, uint64_t queue_size, uint64_t penalty)
{
	memset(q, 0, sizeof(struct queue_t));
#if defined(TINYQUEUE)
	q->info.queue_size = queue_size;
#else
	q->queue_size = queue_size;
#endif
#if defined(TINYQUEUE)
	q->traffic_full = 0;
	q->traffic_empty = 0;
#endif
	q->penalty = penalty;
#if defined(TINYQUEUE)
	printf("===== TinyQueue starts ======\n");
	q->data = (ELEMENT_TYPE *) calloc (MAX_QUEUE_SIZE, sizeof(ELEMENT_TYPE));
#else
	q->data = (ELEMENT_TYPE *) calloc (q->queue_size, sizeof(ELEMENT_TYPE));
#endif

	if (q->data == NULL) {
		printf("Error in allocating FIFO queue.\n");
		exit(-1);
	}
}

#if defined(TINYQUEUE)

uint32_t MOD(uint32_t val, uint32_t inc, uint32_t mod)
{
	if ((val + inc) >= mod)
		return val + inc - mod;
	else
		return val + inc;
}

int enqueue_batching_detect(struct queue_t * q )
{
	int batch_size = DEFAULT_BATCH_SIZE;
	int batch_head = MOD(q->info.head, batch_size, q->info.queue_size);

	while ( q->data[batch_head] ) {
		wait_ticks(DEFAULT_PENALTY);
		if ( batch_size > BATCH_SLICE ) {
			batch_size = batch_size >> 1;
			batch_head = MOD(q->info.head, batch_size, q->info.queue_size);
		}
		else
			return BUFFER_FULL;
	}
	q->info.head = batch_head;

	return SUCCESS;
}

int enqueue(struct queue_t * q, ELEMENT_TYPE value)
{
#if defined(BATCHING)
	if ( q->local_head == q->info.head ) {
		if (enqueue_batching_detect(q) != SUCCESS)
			return BUFFER_FULL;
	}
#else
	if ( READ_ONCE(q->data[q->local_head]) ) {
		return BUFFER_FULL;
	}
#endif

	uint32_t t = q->local_head;
	q->local_head ++;
	if ( q->local_head >= q->info.queue_size ) {
		long traffic_tmp = q->traffic_full - q->traffic_empty;
		if (traffic_tmp >= ENLARGE_THRESHOLD) {
			if ((q->info.queue_size << 1) > MAX_QUEUE_SIZE) {
				q->local_head = 0;
				printf("(FAILURE: Queue %ld) Enlarging queue size failed (reaching maximum queue size. Current value: %u)\n", (q - queues), q->info.queue_size);
			}
			else {
				q->info.queue_size = q->info.queue_size << 1;
				q->traffic_full = q->traffic_empty = 0;
				printf("(SUCCESS: Qeueue %ld) Enlarge queue size to %d\n", (q - queues), q->info.queue_size); 
			}
		}
		else
			q->local_head = 0;
	}

	WRITE_ONCE(q->data[t], value);

	return SUCCESS;
}

int dequeue(struct queue_t * q, ELEMENT_TYPE * value)
{
	if ( !q->data[q->tail] ) {
		return BUFFER_EMPTY;
	}

	uint32_t t = READ_ONCE(q->tail);
	WRITE_ONCE(q->tail, t + 1);
	if ( (t+1) >= q->info.queue_size ) {
		long traffic_tmp = q->traffic_empty - q->traffic_full;
		if (traffic_tmp >= SHRINK_THRESHOLD) { 
			struct info_t tmp;
			struct info_t tmp2;
			tmp2 = tmp = READ_ONCE(q->info);
			if (tmp.queue_size <= MIN_QUEUE_SIZE) {
				printf("(Queue %ld) Failed to shrink queue size (queue size too small : %u)\n", (q-queues), q->info.queue_size);
			}
			else {
				if (tmp.head < (tmp.queue_size >> 1)) {
					tmp2.queue_size = tmp2.queue_size >> 1;
					if (__sync_bool_compare_and_swap((uint64_t *)&(q->info), *(uint64_t *)&tmp, *(uint64_t *)&tmp2)) {
						q->traffic_empty = q->traffic_full = 0;
						printf("(SUCCESS: Queue %ld) Shrink queue size to %d\n", (q-queues), q->info.queue_size);
					} else {
						printf("(FAILURE: Queue %ld) CAS failed in dequeue\n", (q-queues));
					}
				}
			}
		}
		WRITE_ONCE(q->tail, 0);
	}
	*value = READ_ONCE(q->data[t]);
	WRITE_ONCE(q->data[t], ELEMENT_ZERO);

	return SUCCESS;
}

#else

int enqueue(struct queue_t * q, ELEMENT_TYPE value)
{
	if ( q->data[q->head] ) {
		return BUFFER_FULL;
	}

	q->data[q->head] = value;
	q->head ++;
	if ( q->head >= q->queue_size ) {
		q->head = 0;
	}

	return SUCCESS;
}

int dequeue(struct queue_t * q, ELEMENT_TYPE * value)
{
	if ( !q->data[q->tail] ) {
		return BUFFER_EMPTY;
	}

	*value = q->data[q->tail];
	q->data[q->tail] = ELEMENT_ZERO;
	q->tail ++; 
	if ( q->tail >= q->queue_size )
		q->tail = 0;

	return SUCCESS;
}

#endif

