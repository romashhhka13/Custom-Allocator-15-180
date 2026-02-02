#ifndef CUSTOM_ALLOC_H
#define CUSTOM_ALLOC_H

#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);

#ifdef CUSTOM_ALLOC_DEBUG
void custom_alloc_debug_print(void);
#endif

#endif