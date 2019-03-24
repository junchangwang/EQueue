/*
 *  CAS_range.c: Sample code to demonstrate the Less-Than Compare-And-Swap
 *  primitive (LT-CAS) presented in the paper.
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

#include <stdint.h>
#include <stdio.h>

int main(void)
{
	uint16_t target = 0x01FF;
	uint8_t value;

	value = *((char *)&target + 1);
	*(char *)&target = 0xFE;
	printf("%X, (should be 0x01FE)\n", target);

	if (__sync_bool_compare_and_swap(((char *)&target)+1, value, 2)) {
		printf("First CAS success!\n");
	} else {
		printf("ERROR: First CAS failed!\n");
	}

	printf("%X, (should be 0x02FE)\n", target);
	target += 3;
	printf("%X, (should be 0x0301)\n", target);

	if ( ! __sync_bool_compare_and_swap(((char *)&target)+1, 2, 2)) {
		printf("Second CAS success!\n");
	} else {
		printf("ERROR: Second CAS failed!\n");
	}

	if ( __sync_bool_compare_and_swap(((char *)&target)+1, 3, 4)) {
		printf("Third CAS success!\n");
	} else {
		printf("ERROR: Third CAS failed!\n");
	}

	printf("%X, (should be 0x0401)\n", target);

	return 1;
}
