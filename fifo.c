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

#include "fifo.h"
#include <sched.h>

#if defined(FIFO_DEBUG)
#include <assert.h>
#endif

struct queue_t queues[MAX_CORE_NUM];

inline uint64_t rdtsc_bare()
{
	uint64_t	time;
	uint32_t	msw, lsw;
	__asm__		__volatile__(
			"rdtsc\n\t"
			"mov %%edx, %0\n\t"
			"mov %%eax, %1\n\t"
			: "=r" (msw), "=r"(lsw)
			:
			: "%rax","%rdx");
	time = ((uint64_t) msw << 32) | lsw;
	return time;
}

inline void wait_ticks(uint64_t ticks)
{
	uint64_t	current_time;
	uint64_t	time = rdtsc_bare();
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
	q->info.queue_size = queue_size;
	q->traffic_full = 0;
	q->traffic_empty = 0;
	q->penalty = penalty;
	printf("===== EQueue starts ======\n");
	q->data = (ELEMENT_TYPE *) calloc (MAX_QUEUE_SIZE, sizeof(ELEMENT_TYPE));
	if (q->data == NULL) {
		printf("Error in allocating FIFO queue.\n");
		exit(-1);
	}
}

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
	int batch_head = MOD(q->info.head, batch_size,
			READ_ONCE(q->info.queue_size));

	while ( READ_ONCE(q->data[batch_head]) ) {
		wait_ticks(DEFAULT_PENALTY);
		if ( batch_size > BATCH_SLICE ) {
			batch_size = batch_size >> 1;
			batch_head = MOD(q->info.head, batch_size,
					READ_ONCE(q->info.queue_size));
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

	uint32_t lhead_t = q->local_head;
	uint64_t qsize_t = READ_ONCE(q->info.queue_size);
	q->local_head ++;
	if ( q->local_head >= qsize_t ) {
		long traffic_tmp = 
			READ_ONCE(q->traffic_full) - READ_ONCE(q->traffic_empty);
		if (traffic_tmp >= ENLARGE_THRESHOLD) {
			if ((qsize_t << 1) > MAX_QUEUE_SIZE) {
				q->local_head = 0;
				printf("(FAILURE: Queue %ld) Enlarging queue size failed \
					(reaching maximum queue size. Current value: %u)\n",
					(q - queues), q->info.queue_size);
			}
			else {
				WRITE_ONCE(q->info.queue_size, qsize_t << 1);
				WRITE_ONCE(q->traffic_full, 0);
				WRITE_ONCE(q->traffic_empty, 0);
				printf("(SUCCESS: Qeueue %ld) Enlarge queue size to %d\n",
						(q - queues), q->info.queue_size);
			}
		}
		else
			q->local_head = 0;
	}

	WRITE_ONCE(q->data[lhead_t], value);

	return SUCCESS;
}

int dequeue(struct queue_t * q, ELEMENT_TYPE * value)
{
	if ( !READ_ONCE(q->data[q->tail]) ) {
		return BUFFER_EMPTY;
	}

	uint32_t ltail_t = READ_ONCE(q->tail);
	WRITE_ONCE(q->tail, ltail_t + 1);
	if ( (ltail_t+1) >= READ_ONCE(q->info.queue_size) ) {
		long traffic_tmp = READ_ONCE(q->traffic_empty)
					- READ_ONCE(q->traffic_full);
		if (traffic_tmp >= SHRINK_THRESHOLD) { 
			struct info_t tmp;
			struct info_t tmp2;
			tmp2 = tmp = READ_ONCE(q->info);
			if (tmp.queue_size <= MIN_QUEUE_SIZE) {
				printf("(Queue %ld) Failed to shrink queue size \
						(queue size too small : %u)\n",
						(q-queues), q->info.queue_size);
			}
			else {
				if (tmp.head < (tmp.queue_size >> 1)) {
					tmp2.queue_size = tmp2.queue_size >> 1;
					if (__sync_bool_compare_and_swap((uint64_t *)&(q->info),
								*(uint64_t *)&tmp, *(uint64_t *)&tmp2)) {
						WRITE_ONCE(q->traffic_empty, 0);
						WRITE_ONCE(q->traffic_full, 0);
						printf("(SUCCESS: Queue %ld) Shrink queue size to %d\n",
								(q-queues), q->info.queue_size);
					} else {
						printf("(FAILURE: Queue %ld) CAS failed in dequeue\n", (q-queues));
					}
				}
			}
		}
		q->tail = 0;
	}
	*value = READ_ONCE(q->data[ltail_t]);
	WRITE_ONCE(q->data[ltail_t], ELEMENT_ZERO);

	return SUCCESS;
}

