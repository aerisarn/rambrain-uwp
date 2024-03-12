#include "c_interface.h"

#include <assert.h>
#include "managedMemory.h"

#define CANARY 0xED0A8D0

using namespace rambrain;

rambrain_ptr rambrain_null_ptr = { NULL, 0 };

namespace rambrain {

    class AllocatorAccessor
    {
    public:
        static managedMemoryChunk* mmalloc(size_t s_size)
        {
            return managedMemory::defaultManager->mmalloc(s_size);
        }

        static void mfree(rambrain::memoryID id, bool inCleanup = false)
        {
            return managedMemory::defaultManager->mfree(id, inCleanup);
        }

        static bool setUse(managedMemoryChunk* chunk, bool writeAccess) {
            return managedMemory::defaultManager->setUse(*chunk, writeAccess);
        }

        static bool unsetUse(managedMemoryChunk* chunk, unsigned int no_unsets = 1)
        {
            return managedMemory::defaultManager->unsetUse(*chunk, no_unsets);
        }
    };
}

rambrain_ptr rambrain_allocate(size_t s_size)
{
    rambrain_ptr result = rambrain_null_ptr;
    managedMemoryChunk* chunk = AllocatorAccessor::mmalloc(s_size + sizeof(rambrain_ptr));
    if (AllocatorAccessor::setUse(chunk, true))
    {
        rambrain_ptr* ptr = (rambrain_ptr*)chunk->locPtr;
        ptr->canary = CANARY;
        ptr->chunk = chunk;
        return *ptr;
    }
    return result;
}

void rambrain_free(rambrain_ptr ptr)
{
    assert(ptr.canary == CANARY);
    AllocatorAccessor::mfree(static_cast<managedMemoryChunk*>(ptr.chunk)->id);
}

void* rambrain_reference(rambrain_ptr ptr)
{
    assert(ptr.canary == CANARY);
    AllocatorAccessor::setUse(static_cast<managedMemoryChunk*>(ptr.chunk), true);
    return static_cast<managedMemoryChunk*>(ptr.chunk)->locPtr;
}

rambrain_ptr rambrain_dereference(rambrain_ptr ptr)
{
    assert(ptr.canary == CANARY);
    AllocatorAccessor::unsetUse(static_cast<managedMemoryChunk*>(ptr.chunk), true);
    return ptr;
}

rambrain_ptr rambrain_ptr_from_data(void* data) //chunk must be allocated on page
{
    rambrain_ptr* ptr = (rambrain_ptr*)((char*)data - sizeof(rambrain_ptr));
    assert(ptr->canary == CANARY);
    return *ptr;
}

void* rambrain_ptr_to_data(rambrain_ptr ptr)
{
    assert(ptr.canary == CANARY);
    managedMemoryChunk* chunk = (managedMemoryChunk *)(ptr.chunk);
    rambrain_ptr* ptr_on_chunk = (rambrain_ptr*)chunk->locPtr;
    assert(ptr_on_chunk->canary == CANARY);
    return (char*)ptr_on_chunk + sizeof(rambrain_ptr);
}

