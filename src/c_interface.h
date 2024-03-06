#pragma once

#include "export.h"

#ifdef _cplusplus
extern "C" {
#endif

#if defined(__LP64__) || defined(_WIN64)
#define HEADER_ALIGN 16
#else
#define HEADER_ALIGN 8
#endif

typedef struct rambrain_ptr_t
{
	alignas(HEADER_ALIGN)
	
	unsigned	canary;
	void*		chunk;
} rambrain_ptr;
static_assert (sizeof(rambrain_ptr) == 16, "rambrain_ptr size is not correct");

extern rambrain_ptr rambrain_null_ptr;

RAMBRAINAPI rambrain_ptr rambrain_allocate(size_t s_size);
RAMBRAINAPI void		 rambrain_free(rambrain_ptr ptr);
RAMBRAINAPI void*		 rambrain_reference(rambrain_ptr ptr);
RAMBRAINAPI void		 rambrain_dereference(rambrain_ptr ptr);
RAMBRAINAPI rambrain_ptr rambrain_adopt(void* chunk);

#ifdef _cplusplus
}
#endif