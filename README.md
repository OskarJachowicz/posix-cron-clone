<div align="center">

# Memory Manager

**A custom C memory manager with heap validation, block-boundary protection, and dynamic page management.**

![C](https://img.shields.io/badge/C-C11-A8B9CC?style=for-the-badge&logo=c&logoColor=white)
![POSIX](https://img.shields.io/badge/POSIX-Compatible-333333?style=for-the-badge&logo=linux&logoColor=white)
![Memory Management](https://img.shields.io/badge/Memory_Management-Custom-2E8B57?style=for-the-badge)

</div>

## Overview

Memory Manager is a lightweight allocator implemented in C11. It manages its own
memory pages instead of delegating allocation decisions to the standard `malloc`
implementation. Each allocated block is tracked by control metadata, protected
by boundary fences, and checked before memory-management operations.

The project is intended for learning and experimentation with low-level memory
management, heap diagnostics, and defensive programming in POSIX environments.

## Features

- Heap allocation and deallocation with `heap_malloc`, `heap_calloc`,
  `heap_realloc`, and `heap_free`.
- Heap consistency validation and pointer classification.
- Fence protection for detecting writes outside allocated blocks.
- Control-block checksums for detecting heap metadata corruption.
- Dynamic memory-page management with support for finding the largest used block.
- Detection of invalid pointers, double frees, and corrupted heap structures.
- A small API that can be integrated into C programs without changing their
  normal allocation workflow.

## Public API

```c
void *heap_malloc(size_t size);
void *heap_calloc(size_t number, size_t size);
void *heap_realloc(void *ptr, size_t size);
void heap_free(void *ptr);

int heap_validate(void);
enum pointer_type_t get_pointer_type(const void *const pointer);
size_t heap_get_largest_used_block_size(void);
```

`heap_validate()` can be called during development or in tests to verify the
heap state. Pointer classification makes it possible to distinguish valid
allocations from freed pointers, control-block addresses, fence addresses, and
other invalid locations.

## Building

The project is designed for a C11-compatible compiler on a POSIX-compatible
system. A typical build with GCC is:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -g *.c -o memory_manager
```

Use the resulting allocator in a program by including its public header and
linking the implementation files into the target.
