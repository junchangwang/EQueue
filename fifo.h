/*
 *  tinyQueue: an efficient lock-free queue on pipeline parallelism 
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


#ifndef _FIFO_B_QUQUQ_H_
#define _FIFO_B_QUQUQ_H_

#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/* Reading/Writing aligned 64-bit memory is atomic on x64 servers. *
 * This argument must be changed to uint32_t when the FIFO is      *
 * used on 32-bit servers. */
#define ELEMENT_TYPE uint64_t

/* Return values of deq() and enq() */
#define SUCCESS 0
#define BUFFER_FULL -1
#define BUFFER_EMPTY -2

//#define DEFAULT_QUEUE_SIZE (1024 * 8) 
#define MAX_QUEUE_SIZE (1024 * 1024)
#define DEFAULT_QUEUE_SIZE (MAX_QUEUE_SIZE/512)

#define MAX_BATCH_SIZE (DEFAULT_QUEUE_SIZE/16)
#define BATCH_INCREAMENT (DEFAULT_BATCH_SIZE/2)
#define DEFAULT_PENALTY (1000) /* cycles */

#if defined(CONS_BATCH) || defined(PROD_BATCH)

struct queue_t {
	/* Mostly accessed by producer. */
	volatile	uint32_t	head;
	volatile	uint32_t	batch_head;
	uint32_t  full_counter;
	unsigned long batch_history_p;

	/* Mostly accessed by consumer. */
	volatile	uint32_t	tail __attribute__ ((aligned(64)));
	volatile	uint32_t	batch_tail;
	uint32_t  empty_counter;
	unsigned long batch_history_c;

	/* readonly data */
	uint64_t	start_c __attribute__ ((aligned(64)));
	uint64_t	stop_c;
	uint64_t  queue_size;
	uint64_t  batch_size_p;
	uint64_t  batch_size_c;
	uint64_t  penalty;

	/* accessed by both producer and comsumer */
	ELEMENT_TYPE * data __attribute__ ((aligned(64)));
} __attribute__ ((aligned(64)));

#elif defined(TINYQUEUE)

struct info_t {
	uint32_t head       : 32;
	uint64_t queue_size : 32;
};

struct queue_t {
	/* Mostly accessed by producer. */
	//volatile	uint32_t	head;
	uint32_t  full_counter;

	/* Mostly accessed by consumer. */
	volatile 	uint32_t	tail __attribute__ ((aligned(64)));
	uint32_t  empty_counter;

	/* readonly data */
	uint64_t	start_c __attribute__ ((aligned(64)));
	uint64_t	stop_c;
	//uint64_t  queue_size;
	uint64_t  penalty;
	struct info_t info __attribute__ ((aligned(64)));

	/* accessed by both producer and comsumer */
	//ELEMENT_TYPE	data[QUEUE_SIZE] __attribute__ ((aligned(64)));
	long traffic __attribute__ ((aligned(64)));
	ELEMENT_TYPE * data __attribute__ ((aligned(64)));
} __attribute__ ((aligned(64)));

#else

struct queue_t {
	/* Mostly accessed by producer. */
	volatile	uint32_t	head;
	uint32_t  full_counter;

	/* Mostly accessed by consumer. */
	volatile 	uint32_t	tail __attribute__ ((aligned(64)));
	uint32_t  empty_counter;

	/* readonly data */
	uint64_t	start_c __attribute__ ((aligned(64)));
	uint64_t	stop_c;
	uint64_t  queue_size;
	uint64_t  penalty;

	/* accessed by both producer and comsumer */
	//ELEMENT_TYPE	data[QUEUE_SIZE] __attribute__ ((aligned(64)));
	ELEMENT_TYPE * data __attribute__ ((aligned(64)));
} __attribute__ ((aligned(64)));

#endif

void queue_init(struct queue_t *, uint64_t, uint64_t, uint64_t, uint64_t);
int enqueue(struct queue_t *, ELEMENT_TYPE);
int dequeue(struct queue_t *, ELEMENT_TYPE *);
uint32_t distance(struct queue_t *);

uint64_t rdtsc_bare(void);
uint64_t rdtscp(void);
uint64_t rdtsc_barrier(void);
#if 0
static uint64_t rdtsc_barrier_begin(void);
static uint64_t rdtsc_barrier_end(void);
#endif
void wait_ticks(uint64_t);

#endif
