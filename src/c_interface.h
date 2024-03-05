#pragma once

#include "export.h"

#ifdef _cplusplus
extern "C" {
#endif

#pragma pack(push)
#pragma pack(16)
struct rambrain_ptr
{
	void* rambrain_descriptor;
	bool  rambrain_tracker;
};
static_assert (sizeof(rambrain_ptr) == 16, "rambrain_ptr size is not correct");
#pragma pack(pop)

RAMBRAINAPI rambrain_ptr rambrain_allocate(size_t s_size, unsigned int n_elem);
RAMBRAINAPI void		 rambrain_free(rambrain_ptr ptr);
RAMBRAINAPI void*		 rambrain_reference(rambrain_ptr ptr);
RAMBRAINAPI void		 rambrain_dereference(rambrain_ptr ptr);

#ifdef _cplusplus
}
#endif