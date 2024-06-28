# Dynamic Thread Pool in C

## Overview

This project implements a dynamic thread pool in C that manages multiple groups of threads. Each group contains a specified range of threads, and the pool oversees all groups collectively. The thread pool allows the creation and destruction of threads dynamically and includes a timer-based manager thread to handle periodic tasks.

## Precompiled Library

The thread pool is provided as a precompiled static library (`threadpool.a`). To integrate it into your project:

1. **Compile the Library**: First, compile the provided source files into the static library using `make`.

   Example:
   ```bash
   make

2. **Linking**: Include `threadpool.a` in your linker command.
   
   Example:
   ```bash
   gcc -o example example.c -L/path/to/threadpool -lthreadpool -pthread