# Dynamic Thread Pool in C

## Overview
This project is not production ready.

This project implements a dynamic thread pool in C that manages multiple groups of threads. Each group contains a specified range of threads, and the pool oversees all groups collectively. The thread pool allows the creation and destruction of threads dynamically and includes a timer-based manager thread to handle periodic tasks.

## Precompiled Library

The thread pool is provided as a precompiled static library (`threadpool.a`). To integrate it into your project:

1. **Compile the Library**: First, compile the provided source files into the static library using `make`.

   ```bash
   make

2. **Linking**: Include `threadpool.a` in your linker command.
   
   Example:
   ```bash
   gcc -o example example.c -L/path/to/threadpool -lthreadpool -pthread

## Functions

- `int init_pool(TPool **tp, unsigned int maxThrds);`
Initializes a thread pool.

- `void destroy_pool(TPool *tp);`
Destroys the thread pool.

- `TGroup *add_group(TPool *tp, unsigned int min, unsigned int max, int flags);`
Adds a group to the thread pool.

- `void destroy_group(TGroup *tg);`
Destroys a thread group.

- `void init_work(Work **work);`
Initializes a work item.

- `void add_work(Work *work, work_func func, void *arg);`
Adds a work function to the work item.

- `int do_work(TGroup *tg, Work *work);`
Executes work in a thread group.

## Bench

The bench marks are a work in progress. The number of threads used correspond to the number of cores I have on my machine.
To run the benchmark:

   ```bash
   make bench
