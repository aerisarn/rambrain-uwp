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
    managedMemoryChunk* chunk = AllocatorAccessor::mmalloc(s_size);
    if (AllocatorAccessor::setUse(chunk, true))
    {
        result.canary = CANARY;
        result.chunk = chunk;
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

void rambrain_dereference(rambrain_ptr ptr)
{
    assert(ptr.canary == CANARY);
    AllocatorAccessor::unsetUse(static_cast<managedMemoryChunk*>(ptr.chunk), true);
}

rambrain_ptr rambrain_adopt(void* chunk)
{
    return { CANARY, chunk };
}

