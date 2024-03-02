/*   rambrain - a dynamical physical memory extender
 *   Copyright (C) 2015 M. Imgrund, A. Arth
 *   mimgrund (at) mpifr-bonn.mpg.de
 *   arth (at) usm.uni-muenchen.de
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MANAGEDFILESWAP_H
#define MANAGEDFILESWAP_H

#include "managedSwap.h"
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <stdint.h>
#endif
#include <map>
#include <queue>
#ifndef _WIN32
#include <libaio.h>
#endif
#include <signal.h>
#include <unordered_map>
#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
typedef int file_descriptor_t
#else
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
typedef HANDLE file_descriptor_t;
#endif

//Test classes
#ifdef BUILD_TESTS
class managedFileSwap_Unit_SwapAllocation_Test;
class managedFileSwap_Integration_RandomAccess_Test;
class managedFileSwap_Integration_RandomAccessVariousSize_Test;
class managedFileSwap_Unit_SwapPolicy_Test;
#endif

namespace rambrain
{
///@brief the status for pageFileLocations
enum pageChunkStatus {PAGE_FREE = 1 /** PageChunk is free space not occupied by a swapped chunk **/,
                      PAGE_PART = 2 /** PageChunk is part of an object **/,
                      PAGE_END = 4 /** PageChunk is last part of an object ( or whole object) **/,
                      PAGE_WASREAD = 8 /** PageChunk has been read by user**/,
                      PAGE_UNKNOWN_STATE = 16 /** PageChunk has unknown state ( temporary ) **/
                     };

typedef uint64_t global_offset;

class pageFileLocation;

///@brief structure to handle swap files
struct swapFileDesc {

    file_descriptor_t fileno;
    global_bytesize currentSize;
};

#ifdef _WIN32

struct iocb {
    OVERLAPPED oOverlap; //must be first
    file_descriptor_t fd;
    void* buf; 
    size_t count; 
    long long offset;
    int operation;
    int aio_reqprio;
};

typedef unsigned long io_context_t;


struct io_event {
    void* data;
    struct iocb* obj;
    long long res;
    long long res2;
};

#endif

///@brief datastructure for handling asynchronous events
struct aiotracker {
    struct iocb aio;
    int *tracker;
};

///@brief saves some storage in pageFileLocation
union glob_off_union {
    pageFileLocation *glob_off_next;
    managedMemoryChunk *chunk;
    glob_off_union() {
        chunk = NULL;
    };
};


/** @brief tracks page file allocations
 * while objects are preferably written continuous to page file, we may encounter the situation that the
 * pagefiles are nearly full. In this case we take fragments of free space and use these to break up the
 * consecutive memory of a chunk into parts, tracked by pageChunks.
 * */
class RAMBRAINAPI pageFileLocation
{
public:
    pageFileLocation ( unsigned int file, global_bytesize offset, global_bytesize size, pageChunkStatus status = PAGE_FREE ) :
        file ( file ), offset ( offset ), size ( size ), status ( status ), aio_ptr ( NULL ) {}

    unsigned int file /** The number of the file this pageFileLocation is resident in**/;
    global_bytesize offset /** Byte offset into the file**/;
    global_bytesize size /** size of the chunk (data, not including possible reserved bytes after data)**/;
    union glob_off_union glob_off_next/** This points if used to the next part, if free to the next free chunk, if PAGE_END points to memchunk. **/;//
    pageChunkStatus status /** the status of the page**/;
    struct aiotracker *aio_ptr = NULL /** pointer possibly pointing to any pending aio requests concerning this chunk**/;
    char aio_lock = 0 /** an elementary lock that lets us wait on a single pageFileLocation **/;
};



class managedFileSwap;

/** @brief An implementation of managedSwap that is capable of kernel asynchronousIO
 *
 *  @note we also support DMA, however this is not recommended as kernel caching&buffering will be circumvent. For our use case this turns out to slow down things more and we do not make best use of system resources.
 *  @note all public functions of managedFileSwap need to be called holding stateChangeMutex
 **/
class RAMBRAINAPI managedFileSwap : public managedSwap
{
public:

    managedFileSwap ( global_bytesize size, const char *filemask, global_bytesize oneFile = 0, bool enableDMA = false );
    virtual ~managedFileSwap();

    virtual void swapDelete ( managedMemoryChunk *chunk );
    virtual global_bytesize swapIn ( managedMemoryChunk **chunklist, unsigned int nchunks );
    virtual global_bytesize swapIn ( managedMemoryChunk *chunk );
    virtual global_bytesize swapOut ( managedMemoryChunk **chunklist, unsigned int nchunks );
    virtual global_bytesize swapOut ( managedMemoryChunk *chunk );
    virtual bool extendSwap ( global_bytesize size );
    virtual bool extendSwapByPolicy ( global_bytesize min_size );

    void setDMA ( bool arg1 );

    virtual void close();

    const unsigned int pageSize;


private:
    /** @brief generate a pageFileLocation object given a global offset and a length of the data. This maps our "virtual" adress space to physical locations in a certain file**/
    pageFileLocation determinePFLoc ( global_offset g_offset, global_bytesize length ) const;
    /** @brief maps from physical location to "virtual" adress**/
    inline global_offset determineGlobalOffset ( const pageFileLocation &ref ) const;
    /** @brief opens swap files according to settings**/
    bool openSwapFiles();
    /** @brief opens certain range of swap files according to settings**/
    bool openSwapFileRange ( unsigned int start, unsigned int stop );
    /** @brief closes swap files**/
    void closeSwapFiles();

    const char *filemask;

    unsigned int pageFileNumber;
    global_bytesize pageFileSize;


    float swapFileResizeFrac = .1;

    struct swapFileDesc *swapFiles = NULL;

    /** @brief Schedules an elementary pageFileLocation chunk for copying (in or out)**/
    void scheduleCopy ( rambrain::pageFileLocation &ref, void *ramBuf, int *tracker, bool reverse = false ) ;
    /** @brief Schedules copying on level of whole managedMemoryChunks and calls scheduleCopy on the assigned parts
     * @param ref the pageFileLocation the chunks data should be copied to
     * @param ramBuf pointer to the managedMemoryChunk data buffer
     * @param reverse copy in the other direction
     **/
    void copyMem ( rambrain::pageFileLocation &ref, void *ramBuf, bool reverse = false  )  ;

    /** @brief Convenience function for reverse copying**/
    inline void copyMem ( void *ramBuf, rambrain::pageFileLocation &ref ) {
        copyMem ( ref, ramBuf, true );
    }
    /** @brief Convenience function for reverse scheduling a copy**/
    inline void scheduleCopy ( void *ramBuf, pageFileLocation &ref, int *parttracker ) {
        scheduleCopy ( ref, ramBuf, parttracker, true );
    }

    /** @brief If we have any restrictions regarding memory alignment of RAM buffers(DMA), this function tells us about it**/
    inline size_t getMemoryAlignment() const {
        return memoryAlignment;
    }


    //page file malloc:
    /** @brief Tries to find space in the swapFiles to write out an object of size size and returns first pageFileLocation to it **/
    pageFileLocation *pfmalloc ( rambrain::global_bytesize size, rambrain::managedMemoryChunk *chunk );
    void pffree ( pageFileLocation *pagePtr );
    ///Helper function for pfmalloc
    pageFileLocation *allocInFree ( pageFileLocation *freeChunk, global_bytesize size );

    std::map<global_offset, pageFileLocation *> free_space;
    std::map<global_offset, pageFileLocation *> all_space;


    bool enableDMA = false;
protected:
    bool deleteFilesOnExit = true;

    //sigEvent Handler:
    /** @brief deals with a single asynchronous IO event completion**/
    void asyncIoArrived ( rambrain::pageFileLocation *ref, struct io_event *aio );
    /** @brief called to finish a transaction when all pending aio on a managedMemoryChunk has completed**/
    void completeTransactionOn ( rambrain::pageFileLocation *ref, bool lock = true );

    /** @brief gives this class the chance to treat incoming aio events
     *  @return true if there is pending IO to wait for, false otherwise
     *  @warning if false is returned, immediate waiting for pending aio results in deadlock
     **/
    virtual bool checkForAIO();

    struct iocb aio_template;
    io_context_t aio_context = 0;
    unsigned int aio_max_transactions = 10240;
    struct io_event *aio_eventarr;
    pthread_mutex_t aioWaiterLock = PTHREAD_MUTEX_INITIALIZER;

    std::unordered_map<struct iocb *, pageFileLocation *> pendingAios;

    static managedFileSwap *instance;
    /** @brief returns some statistics. Typically, we will be sensitive to SIGUSR2 if compiled with -DSWAPSTATS=on**/
    static void sigStat ( int signum );

    //Thread pool for asynchronous io:
    pthread_mutex_t io_submit_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t io_submit_cond = PTHREAD_COND_INITIALIZER;
    unsigned int io_submit_num_threads = 1;
    pthread_t *io_submit_threads;
    pthread_t io_waiter_thread;
    pthread_t io_arrive_thread;

    std::queue<struct iocb *> io_submit_requests;

    void my_io_submit ( struct iocb *aio );
    static void *io_submit_worker ( void *ptr );
    static void *io_arrrive_worker ( void *ptr );

    bool io_arrive_work = true;

    /** @brief throws out cached elements still in ram but also resident on disk. This makes space in situations of low swap memory**/
    bool cleanupCachedElements ( rambrain::global_bytesize minimum_size = 0 );
    /** @brief tells managedFileSwap that the chunk under consideration might have been changed by user and needs to be copied out freshly**/
    virtual void invalidateCacheFor ( managedMemoryChunk &chunk );

    /** @brief returns free disk space at file system location specified by filemask **/
    global_bytesize getFreeDiskSpace();

    //Test classes
#ifdef BUILD_TESTS
    friend class ::managedFileSwap_Unit_SwapAllocation_Test;
    friend class ::managedFileSwap_Integration_RandomAccess_Test;
    friend class ::managedFileSwap_Integration_RandomAccessVariousSize_Test;
    friend class ::managedFileSwap_Unit_SwapPolicy_Test;
#endif
};

}


#endif

