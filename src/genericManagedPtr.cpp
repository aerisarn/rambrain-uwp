#include "genericManagedPtr.h"

using namespace rambrain;

genericManagedPtr::genericManagedPtr(const genericManagedPtr& ref) : chunk(ref.chunk), tracker(ref.tracker), n_elem(ref.n_elem), s_size(ref.s_size) {
    rambrain_atomic_add_fetch(tracker, 1);
}

genericManagedPtr::genericManagedPtr(size_t s_size, unsigned int n_elem) {
    this->n_elem = n_elem;
    this->s_size = s_size;
    tracker = new unsigned int;
    (*tracker) = 1;
    if (n_elem == 0) {
        chunk = NULL;
        return;
    }

#ifdef PARENTAL_CONTROL
    /**I admit, this is a bit complicated. as we do not want users to carry around information about object parenthood
    into their classes, we have to ensure that our 'memoryManager::parent' attribute is carried down the hierarchy correctly.
    As this code part is accessed recursively throughout creation, we have to make sure that
    * we do not lock twice (deadlock)
    * we only continue if we're called by the currently active creation process
    the following is not very elegant, but works. The idea behind this is that we admit one thread at a time to create
    an object class hierarchy. In this way, the parent argument from below is correct.
    This is synced by basically checking wether we should be a master or not based on a possibly setted threadID...
    **/

    bool iamSyncer;
    if (pthread_mutex_trylock(&managedMemory::parentalMutex) == 0) {
        //Could lock
        iamSyncer = true;
    }
    else {
        //Could not lock. Perhaps, I already have a lock:
        if (pthread_equal(pthread_self(), managedMemory::creatingThread)) {
            iamSyncer = false;//I was called by my parents!
        }
        else {
            //I need to gain the lock:
            rambrain_pthread_mutex_lock(&managedMemory::parentalMutex);
            iamSyncer = true;
        }
    }
    //iamSyncer tells me whether I am the parent thread (not object!)

    //Set our thread id as the one without locking:
    if (iamSyncer) {
        managedMemory::creatingThread = pthread_self();

    }
#endif

    chunk = managedMemory::defaultManager->mmalloc(s_size * n_elem);

#ifdef PARENTAL_CONTROL
    //Now call constructor and save possible children's sake:
    memoryID savedParent = managedMemory::parent;
    managedMemory::parent = chunk->id;
#endif

    setUse();
    //for (unsigned int n = 0; n < n_elem; n++) {
    //    new (((T*)chunk->locPtr) + n) T(Args...);
    //}
    unsetUse();
#ifdef PARENTAL_CONTROL
    managedMemory::parent = savedParent;
    if (iamSyncer) {
        managedMemory::creatingThread = 0;
        //Let others do the job:
        rambrain_pthread_mutex_unlock(&managedMemory::parentalMutex);
    }
#endif
}

genericManagedPtr::~genericManagedPtr() {
    int trackerold = rambrain_atomic_sub_fetch(tracker, 1);
    if (trackerold == 0) {//Ensure destructor to be called only once.
        mDelete();
    }
}

bool genericManagedPtr::prepareUse() const {
    managedMemory::defaultManager->prepareUse(*chunk, true);
    return true;
}

bool genericManagedPtr::setUse(bool writable, bool* tracker) const {
    if (tracker)
        if (!rambrain_atomic_bool_compare_and_swap(tracker, false, true)) {
            waitForSwapin();
            return false;
        }
    bool result = managedMemory::defaultManager->setUse(*chunk, writable);

    return result;
}

bool genericManagedPtr::unsetUse(unsigned int loaded) const {
    return managedMemory::defaultManager->unsetUse(*chunk, loaded);
}

genericManagedPtr& genericManagedPtr::operator= (const genericManagedPtr& ref) {
    if (chunk) {
        if (ref.chunk == chunk) {
            return *this;
        }
        if (rambrain_atomic_sub_fetch(tracker, 1) == 0) {
            mDelete();
        }
    }
    n_elem = ref.n_elem;
    s_size = ref.s_size;
    chunk = ref.chunk;
    tracker = ref.tracker;
    rambrain_atomic_add_fetch(tracker, 1);
    return *this;
}

void* genericManagedPtr::getLocPtr() const {
    if (chunk->status != MEM_ALLOCATED_INUSE_WRITE) {
        waitForSwapin();
    }
    return chunk->locPtr;
}

const void* genericManagedPtr::getConstLocPtr() const {
    if (!(chunk->status & MEM_ALLOCATED_INUSE_READ)) {
        waitForSwapin();
    }
    return chunk->locPtr;
}

void genericManagedPtr::mDelete() {
    if (s_size > 0) {
        managedMemory::defaultManager->mfree(chunk->id);
    }
    delete tracker;
}

void genericManagedPtr::waitForSwapin() const {
    //While in this case, we are not the guy who actually enforce the swapin, we never the less have to wait
    //for the chunk to become ready. This is done in the following way:
    if (!(chunk->status & MEM_ALLOCATED)) { // We may savely check against this as use will be set by other adhereTo thread and cannot be undone as long as calling adhereTo exists
        rambrain_pthread_mutex_lock(&managedMemory::defaultManager->stateChangeMutex);
        //We will burn a little bit of power here, eventually, but this is a very rare case.
        while (!managedMemory::defaultManager->waitForSwapin(*chunk, true)) {};
        rambrain_pthread_mutex_unlock(&managedMemory::defaultManager->stateChangeMutex);
    }
}

genericAdhereTo::genericAdhereTo(const genericAdhereTo& ref) : data(ref.data) {
    loadedReadable = ref.loadedReadable;
    loadedWritable = ref.loadedWritable;
    if (loadedWritable) {
        data->setUse(loadedWritable);
    }
    if (loadedReadable) {
        data->setUse(loadedReadable);
    }
};

genericAdhereTo::genericAdhereTo(const genericManagedPtr& data, bool loadImmediately) : data(&data) {
    if (loadImmediately && data.size() != 0) {
        data.prepareUse();
    }
}

genericAdhereTo& genericAdhereTo::operator= (const genericAdhereTo& ref) {
    if (loadedReadable) {
        data->unsetUse();
    }
    if (loadedWritable) {
        data->unsetUse();
    }
    this->data = ref.data;
    loadedReadable = ref.loadedReadable;
    loadedWritable = ref.loadedWritable;

    if (loadedWritable) {
        data->setUse(loadedWritable);
    }
    if (loadedReadable) {
        data->setUse(loadedReadable);
    }
    return *this;
}

genericAdhereTo::operator const void* () const {
    if (data->size() == 0) {
        return NULL;
    }
    if (!loadedReadable) {
        data->setUse(false, &loadedReadable);
    }
    return data->getConstLocPtr();
}

genericAdhereTo::operator void* () {
    if (data->size() == 0) {
        return NULL;
    }
    if (!loadedWritable) {
        data->setUse(true, &loadedWritable);
    }
    return data->getLocPtr();
}

genericAdhereTo::~genericAdhereTo() {
    unsigned char loaded = 0;
    loaded = (loadedReadable ? 1 : 0) + (loadedWritable ? 1 : 0);
    if (loaded > 0 && data->size() != 0) {
        data->unsetUse(loaded);
    }
}