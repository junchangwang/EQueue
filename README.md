
# TinyQueue

This project contains source code of TinyQueue, an efficient lock-free queue for pipeline parallelism on multi-core architectures.

TinyQueue is released under GPL v3.

# Organization

* fifo.c: Source code of TinyQueue.
* fifo.h: header file of fifo.c.
* main.c: main file of the project.
* CAS_range.c: Sample code to use the Less-Than Compare-And-Swap primitive.
* affinity.xxx.conf: Affinity configuration file which tries to allocate enqueue and dequeue threads to different CPU cores.

# Compile and Run

On most 64-bits machines running Linux, command "make" is enough to build sample code and tinyqueue executable file.

Usage:

	./fifo --help
	./fifo -t 10000000 -a affinity.tree.conf -c 4 -w 170 -r 32768

# Affinity setting files

Upon start, TinyQueue first loads the specified affinity setting file, and then binds reader threads and updater threads to specified CPU cores, accordingly.

# Contact

If you have any questions or suggestions regarding TinyQueue, please send email to junchangwang@gmail.com.


