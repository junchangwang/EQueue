 #  EQueue
 #
 #  This program is free software: you can redistribute it and/or modify
 #  it under the terms of the GNU General Public License as published by
 #  the Free Software Foundation, either version 3 of the License, or
 #  (at your option) any later version.
 #
 #  This program is distributed in the hope that it will be useful,
 #  but WITHOUT ANY WARRANTY; without even the implied warranty of
 #  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 #  GNU General Public License for more details.
 #
 #  You should have received a copy of the GNU General Public License
 #  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 #
 #  Copyright (C) 2019 Junchang Wang
 #

INCLUDE = ../../include
CFLAGS = -g -O2 -D_M64_ -I$(INCLUDE) -D_GNU_SOURCE

CFLAGS += -DEQUEUE
CFLAGS += -DSIMULATE_BURST
CFLAGS += -DBATCHING
#CFLAGS += -DRT_SCHEDULE
#CFLAGS += -DE2ELATENCY
#CFLAGS += -DFIFO_DEBUG
#CFLAGS += -DINSERT_BUG

ORG = fifo.o main.o 

fifo: $(ORG) $(LIB) 
	gcc $(ORG) $(LIB) -o $@ -lpthread

$(ORG): fifo.h Makefile


clean:
	rm -f $(ORG) *.o *.a fifo test_cycle test_cycle.o cscope*

cscope:
	cscope -bqR
