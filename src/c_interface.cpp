#include "c_interface.h"

#include "genericManagedPtr.h"

using namespace rambrain;

rambrain_ptr rambrain_allocate(size_t s_size, unsigned int n_elem)
{
	return { (void*)new genericManagedPtr(s_size, n_elem) };
}

void rambrain_free(rambrain_ptr ptr)
{
	delete ptr.rambrain_descriptor;
}

void* rambrain_reference(rambrain_ptr ptr)
{
	genericManagedPtr* real_ptr = (genericManagedPtr*)(ptr.rambrain_descriptor);
    if (real_ptr->size() == 0) {
        return NULL;
    }
    if (!ptr.rambrain_tracker) {
        real_ptr->prepareUse();
    }    
    real_ptr->setUse(true, &ptr.rambrain_tracker);
    return real_ptr->getLocPtr();
}

void rambrain_dereference(rambrain_ptr ptr)
{
    genericManagedPtr* real_ptr = (genericManagedPtr*)(ptr.rambrain_descriptor);
    if (ptr.rambrain_tracker && real_ptr->size() != 0) {
        real_ptr->unsetUse(ptr.rambrain_tracker);
    }
}