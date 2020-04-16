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

#ifndef _FIFO_EQUEUE_H_
#define _FIFO_EQUEUE_H_

#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "api.h"

/* Reading/Writing aligned 64-bit memory is atomic on x64 servers. *
 * This argument must be changed to uint32_t when the FIFO is      *
 * used on 32-bit servers. */
#define ELEMENT_TYPE uint64_t

/* Return values of deq() and enq() */
#define SUCCESS 0
#define BUFFER_FULL -1
#define BUFFER_EMPTY -2

#define MAX_CORE_NUM 16

#define BATCH_SLICE (128UL)   //Must be power of two
#define DEFAULT_QUEUE_SIZE (16 * BATCH_SLICE)
#define DEFAULT_BATCH_SIZE (DEFAULT_QUEUE_SIZE/4)
#define MAX_QUEUE_SIZE (1024 * BATCH_SLICE)
#define MIN_QUEUE_SIZE (2 * BATCH_SLICE)

#define ENLARGE_THRESHOLD (1024)
#define SHRINK_THRESHOLD (128)

#define DEFAULT_PENALTY (1000) /* cycles */

struct info_t {
	uint32_t head;
	uint32_t queue_size;
};

struct queue_t {
	/* Mostly accessed by producer. */
	uint32_t full_counter __attribute__ ((aligned(128)));
	long traffic_full;
	struct info_t info;
#if defined(BATCHING)
	uint32_t  local_head;
#endif

	/* Mostly accessed by consumer. */
	uint32_t empty_counter __attribute__ ((aligned(128)));
	uint32_t tail;
	long traffic_empty;

	/* readonly data */
	uint64_t start_c __attribute__ ((aligned(128)));
	uint64_t stop_c;
	uint64_t penalty;

	/* accessed by both producer and comsumer */
	ELEMENT_TYPE * data __attribute__ ((aligned(128)));

};

void queue_init(struct queue_t *, uint64_t, uint64_t);
int enqueue(struct queue_t *, ELEMENT_TYPE);
int dequeue(struct queue_t *, ELEMENT_TYPE *);
uint32_t distance(struct queue_t *);

uint64_t rdtsc_bare(void);
uint64_t rdtscp(void);
uint64_t rdtsc_barrier(void);
void wait_ticks(uint64_t);

#endif

