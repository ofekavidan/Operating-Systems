# OSM Project: Measuring the Costs of Operations, Function Calls, and System Calls in Different Platforms
## Project Overview
This project explores the performance of various operations in different environments (Linux, Virtual Machine, and Container) by measuring the time it takes to execute:

A simple addition operation.
An empty function call.
A system call (syscall).
The project focuses on the overhead introduced by virtualization and containerization, which are becoming increasingly important in modern computing environments. The static library libosm.a provides functions to calculate the average time taken for these operations over multiple iterations.

Results
The execution times (in nanoseconds) for each operation on different platforms are summarized below:

Platform/Operation	Operation (ns)	Empty Function (ns)	SysCall (ns)
Linux	0.3333	3.6667	716.167
VM	6.1667	12.3333	694
Container	0.3333	1.3333	705.8333
From the results, we observe the following trends:

Linux performs the fastest in basic operations and function calls, though system calls are slightly slower compared to containers.
Virtual Machines (VM) introduce a significant overhead in both operations and function calls.
Containers show near-native performance for operations and function calls but have a slightly higher overhead for system calls than native Linux.
The graph above provides a visual comparison of the execution times on a logarithmic scale.

## How to Run
Clone the repository.
Compile the project using the provided Makefile by running make in the project root.
The compiled static library libosm.a will be generated, which can be used to run the performance tests.
Use the WhatIDo program with strace to analyze the system calls and behavior.

