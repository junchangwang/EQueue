
# EQueue

This project contains the source code of EQueue, an efficient and robust lock-free queue that works as a good candidate for the communication scheme for parallelizing applications  on multi-core architectures.

EQueue is released under GPL v3.

# Organization

* fifo.c: Source code of EQueue.
* fifo.h: header file of fifo.c.
* main.c: main file of the project.
* CAS_range.c: Sample code to use the Less-Than Compare-And-Swap primitive.
* affinity.xxx.conf: Affinity configuration file which tries to map the enqueue and dequeue threads to different CPU cores.

# Compile and Run

On most 64-bits machines running Linux, command "make" is enough to build the project.

Usage:

	./fifo --help
	./fifo -t 10000000 -a affinity.tree.conf -c 4 -w 170 -r 32768

# Affinity setting files

Upon start, EQueue first loads the specified affinity setting file, and then binds reader threads and writer threads to specified CPU cores, accordingly. The configuration files *affinity.distr.conf* and *affinity.tree.conf* are for Dell R730 server with two Intel Xeon processors (8*2 cores). If you are working with other hardware configuration, you may need to first adjust the settings in these two files.

# Contact

If you have any questions or suggestions regarding EQueue, please send email to junchangwang@gmail.com.


