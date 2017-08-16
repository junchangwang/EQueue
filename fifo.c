/*
 *  tinyQueue: an efficient lock-free queue for pipeline parallelism 
 *  on multi-core architectures.
 *
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

void queue_init(struct queue_t *q, uint64_t queue_size, uint64_t batch_size_p, uint64_t batch_size_c, uint64_t penalty)
{
	memset(q, 0, sizeof(struct queue_t));
#if defined(TINYQUEUE)
	q->info.queue_size = queue_size;
#else
    q->queue_size = queue_size;
#endif
#if defined(CONS_BATCH) || defined(PROD_BATCH)
    q->batch_size_p = batch_size_p;
    q->batch_size_c = batch_size_c;
    q->batch_history_p = q->batch_history_c = 1;
#endif
#if defined(TINYQUEUE)
	q->traffic = 0;
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

#if defined(PROD_BATCH) || defined(CONS_BATCH)
inline int leqthan(volatile ELEMENT_TYPE point, volatile ELEMENT_TYPE batch_point)
{
    return (point == batch_point);
}
#endif

#if defined(TINYQUEUE)
int enqueue(struct queue_t * q, ELEMENT_TYPE value)
{
    if ( q->data[q->info.head] ) {
        return BUFFER_FULL;
    }

    uint32_t t = q->info.head;
    q->info.head ++;
    if ( q->info.head >= q->info.queue_size ) {
		int resize_threshold = 30;
		//printf("traffic value: %ld, resize threshhold: %d\n", 
		//		q->traffic, resize_threshold);
		if (q->traffic >= resize_threshold) {
			q->info.queue_size = q->info.queue_size * 2;
			q->traffic = 0;
			printf("Double queue size to %d\n", q->info.queue_size);
		}
		else
			q->info.head = 0;
    }

    q->data[t] = value;

    return SUCCESS;
}

#elif defined(PROD_BATCH)

int enqueue(struct queue_t * q, ELEMENT_TYPE value)
{
    uint32_t tmp_head;
    unsigned long bt_old, bt = 1;

    if( q->head == q->batch_head ) {
        if ( q->data[q->batch_head] ) {
            wait_ticks(q->penalty);
            return BUFFER_FULL;
        }

        bt_old = bt = q->batch_history_p;
        tmp_head = q->batch_head + bt;
        if (tmp_head >= q->queue_size)
           tmp_head -= q->queue_size;

        if ( !q->data[tmp_head] ) {
            // Incremental
            while( !q->data[tmp_head] && (bt <= q->batch_size_p) ) {
                bt_old = bt;
                bt = bt << 1;
                tmp_head = q->batch_head + bt;
                if ( tmp_head >= q->queue_size )
                    tmp_head -= q->queue_size;
            }
        }
        else {
           // Backtracking
           while ( q->data[tmp_head] && (bt > 1) ) {
                bt = bt >> 1;
                tmp_head = q->batch_head + bt;
                if (tmp_head >= q->queue_size)
                    tmp_head -= q->queue_size;
            }
           bt_old = bt;
        }

        q->batch_history_p = bt_old;
        tmp_head = q->batch_head + bt_old;
        if (tmp_head >= q->queue_size)
            tmp_head -= q->queue_size;
        q->batch_head = tmp_head;
    }

    q->data[q->head] = value;
    q->head ++;
    if ( q->head >= q->queue_size ) {
        q->head = 0;
    }

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

#endif

#if defined(TINYQUEUE)

int dequeue(struct queue_t * q, ELEMENT_TYPE * value)
{
	if ( !q->data[q->tail] ) {
		return BUFFER_EMPTY;
	}

	uint32_t t = q->tail;
	q->tail++;
	if ( q->tail >= q->info.queue_size ) {
		int count = 0;
		int resize_threshold = -30;
		struct info_t tmp;
		struct info_t tmp2;
		if (q->traffic <= resize_threshold) { 
tag1:
			tmp = tmp2 = q->info;
			if (count >= 10)
				goto tag2;
			if (tmp.head < tmp.queue_size / 2) {
				tmp2.queue_size /= 2;
			}
			else
				goto tag2;

			if (__sync_bool_compare_and_swap((uint64_t *)&q->info, *(uint64_t *)&tmp, *(uint64_t *)&tmp2)) {
				q->traffic = 0;
				printf("Half queue size to %d\n", q->info.queue_size);
				goto tag2;
			}
			else {
				printf("CAS failed in dequeue\n");
				count ++;
				goto tag1;
			}
		}
tag2:
		q->tail = 0;
	}
	*value = q->data[t];
	q->data[t] = ELEMENT_ZERO;

	return SUCCESS;
}

#elif defined(CONS_BATCH)

int dequeue(struct queue_t * q, ELEMENT_TYPE * value)
{
    uint32_t tmp_tail;
    unsigned long bt_old, bt;

    if( q->tail == q->batch_tail ) {

        if ( !q->data[q->batch_tail] ) {
            q->batch_history_c = q->batch_history_c >> 1;
            return BUFFER_EMPTY;
        }

		// search batching size incrementally	
        bt_old = bt = q->batch_history_c;
        tmp_tail = q->batch_tail + bt;
        if (tmp_tail >= q->queue_size)
            tmp_tail -= q->queue_size;
        // Incremental   
        while ( q->data[tmp_tail] && (bt <= q->batch_size_c) ) {
            bt_old = bt;
            bt = bt << 1;
            //bt = bt + 16;
            tmp_tail = q->batch_tail + bt;
            if (tmp_tail >= q->queue_size)
                tmp_tail -= q->queue_size;
        }

        q->batch_history_c = bt_old;
        tmp_tail = q->batch_tail + bt_old;
        if (tmp_tail >= q->queue_size)
            tmp_tail -= q->queue_size;
        q->batch_tail = tmp_tail;
    }

    *value = q->data[q->tail];
    q->data[q->tail] = ELEMENT_ZERO;
    q->tail ++;
    if ( q->tail >= q->queue_size )
        q->tail = 0;

    return SUCCESS;
}

#else

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
