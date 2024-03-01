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

#include "managedFileSwap.h"
#include "common.h"
#ifndef _WIN32
#include <unistd.h>
#endif // !_WIN32
#include <string.h>
#ifndef _WIN32
#include <sys/signal.h>
#endif
#include <sys/stat.h>
#include "exceptions.h"
#include "managedMemory.h"
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#ifndef _WIN32
#include <sys/ioctl.h>
#include <mm_malloc.h>
#include <sys/statvfs.h>
#endif
#include <iostream>
#include <limits>
#ifndef OpenMP_NOT_FOUND
#include <omp.h>
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <chrono>
#include <stdio.h>
//has issues, TODO
//#include "LockFreeListFinal.h"
#include <mutex>
#include <process.h>
#include <atomic>

void dbgprint(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char cad[1024]; vsprintf(cad, fmt, args);  OutputDebugString(cad);
    va_end(args);
}

#define printf(...) dbgprint(__VA_ARGS__)


//std::atomic<uint64_t> io_key = 0;
std::mutex io_mutex;
std::list<struct rambrain::iocb*> io_queue;

//used for multiple context, we don't need this
static inline int io_setup(int maxevents, rambrain::io_context_t* ctxp) {
    *ctxp = 1;
    //io_queue = lockfree::list_init();
    if (!io_queue.empty())
        io_queue.clear();
    return 0;
}

//unused in single process context
static inline int io_destroy(rambrain::io_context_t ctx) {
    //io_queue->destructor(io_queue->head);
    //free(io_queue);
    return 0;
}


#define IO_CMD_PREAD 0
#define IO_CMD_PWRITE 1

//just prepare the context
inline void io_prep_pread(struct rambrain::iocb* iocb, file_descriptor_t fd, void* buf, size_t count, long long offset)
{
    memset(&iocb->oOverlap, 0, sizeof(OVERLAPPED));
    iocb->oOverlap.hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
    iocb->oOverlap.OffsetHigh = offset >> 32 & 0xFFFFFFFF;
    iocb->oOverlap.Offset = offset & 0xFFFFFFFF;
    iocb->fd = fd;
    iocb->buf = buf;
    iocb->count = count;
    iocb->offset = offset;
    iocb->operation = IO_CMD_PREAD;
}

//just prepare the context
inline void io_prep_pwrite(struct rambrain::iocb* iocb, file_descriptor_t fd, void* buf, size_t count, long long offset)
{
    memset(&iocb->oOverlap, 0, sizeof(OVERLAPPED));
    iocb->oOverlap.hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
    iocb->oOverlap.OffsetHigh = offset >> 32 & 0xFFFFFFFF;
    iocb->oOverlap.Offset = offset & 0xFFFFFFFF;
    iocb->fd = fd;
    iocb->buf = buf;
    iocb->count = count;
    iocb->offset = offset;
    iocb->operation = IO_CMD_PWRITE;
}

int io_submit(rambrain::io_context_t ctx, long nr, struct rambrain::iocb* iocbs[])
{
    DWORD Result = ERROR_SUCCESS;
    for (long i = 0; i < nr; ++i)
    {
        struct rambrain::iocb* request = iocbs[i];
        if (request->operation == IO_CMD_PREAD)
        {
            Result = ReadFile(request->fd, request->buf, request->count, NULL, (LPOVERLAPPED)request);
            if (Result != TRUE && GetLastError() != ERROR_IO_PENDING)
            {
                Result = GetLastError();
                return Result;
            }
        }
        else if (request->operation == IO_CMD_PWRITE)
        {
            Result = WriteFile(request->fd, request->buf, request->count, NULL, (LPOVERLAPPED)request);
            if (Result != TRUE && GetLastError() != ERROR_IO_PENDING)
            {
                Result = GetLastError();
                return Result;
            }
        }
        {
            const std::lock_guard<std::mutex> lock(io_mutex);
            io_queue.push_back(request);
        }
        //io_queue->insert(io_key++, io_queue->head, request);
    }
    return 1;
}

static inline int64_t
timespec_to_nsec(const struct timespec* a)
{
    return (int64_t)a->tv_sec * 1000000000 + a->tv_nsec;
}

template <
    class result_t = std::chrono::milliseconds,
    class clock_t = std::chrono::steady_clock,
    class duration_t = std::chrono::milliseconds
>
auto since(std::chrono::time_point<clock_t, duration_t> const& start)
{
    return std::chrono::duration_cast<result_t>(clock_t::now() - start);
}

//Attempts to read up to nr events from the completion queue for the aio_context specified by ctx.
int io_getevents(rambrain::io_context_t ctx, long min_nr, long nr, struct rambrain::io_event* events, struct timespec* timeout)
{
    long completed = 0;
    //lockfree::node_lf* node = io_queue->head->next;
    auto start = std::chrono::steady_clock::now();
    int64_t deadline = 0;
    if (NULL != timeout)
        deadline = timespec_to_nsec(timeout);
    while (completed < min_nr)
    {
        {
            const std::lock_guard<std::mutex> lock(io_mutex);
            auto it = io_queue.begin();
            while (it != io_queue.end())
            {
                auto request = *it;
                DWORD transferred = 0;
                BOOL result = GetOverlappedResult(request->fd, &request->oOverlap, &transferred, FALSE);
                if (TRUE == result)
                {
                    if (transferred == request->count)
                    {
                        CloseHandle(request->oOverlap.hEvent);
                        events[completed].res = transferred;
                        events[completed].res2 = ERROR_SUCCESS;
                        events[completed].obj = request;
                        completed++;
                    }
                    //remove request
                    it = io_queue.erase(it);
                    if (completed >= nr)
                        return completed;
                }
                else {
                    if (ERROR_IO_PENDING != GetLastError() && ERROR_IO_INCOMPLETE != GetLastError())
                    {
                        int error = GetLastError();
                        throw std::runtime_error("");
                    }
                    ++it;
                }
            }
        }

        //while (node)
        //{
        //    struct rambrain::iocb* request = (struct rambrain::iocb*)node->value;
        //    if (request == NULL)
        //    {
        //        node = node->next;
        //        continue;
        //    }
        //    DWORD transferred = 0;
        //    BOOL result = GetOverlappedResult(request->fd, &request->oOverlap, &transferred, FALSE);
        //    if (TRUE == result)
        //    {
        //        if (transferred == request->count)
        //        {
        //            CloseHandle(request->oOverlap.hEvent);
        //            events[completed].res = transferred;
        //            events[completed].res2 = ERROR_SUCCESS;
        //            events[completed].obj = request;
        //            completed++;
        //            if (completed >= nr)
        //                return completed;
        //        }
        //        //remove request
        //        int delete_key = node->key;
        //        lockfree::node_lf* delete_node = node;
        //        node = node->next;
        //        io_queue->delete_node(delete_key, delete_node);
        //    }
        //    else {
        //        if (ERROR_IO_PENDING != GetLastError() && ERROR_IO_INCOMPLETE != GetLastError())
        //        {
        //            int error = GetLastError();
        //            throw std::runtime_error("");
        //        }
        //        node = node->next;
        //    }
        //}
        if (completed >= min_nr)
        {
            break;
        }
        if (completed < nr)
        {
            SleepEx(0, TRUE);
        }
        auto elapsed = since<std::chrono::nanoseconds>(start).count();
        if (elapsed >= deadline)
        {
            if (deadline != 0)
                errno = ETIMEDOUT;
            break;
        }
    }
    //for (int i = 0; i < nr; ++i)
    //{
    //    if (completed == min_nr && io_queue-> <= 0)
    //        break;
    //    if (io_queue->take<std::chrono::milliseconds>(events[i], timespec_to_msec(timeout) * 1ms))
    //    {
    //        if (events[i].res != ERROR_SUCCESS)
    //        {
    //            return -events[i].res;
    //        }
    //        completed++;
    //    }
    //    else {
    //        errno = ETIMEDOUT;
    //        break;
    //    }
    //}
    return completed;
}

//Implementation

//VOID WINAPI CompletedWriteRoutine(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
//{
//    struct rambrain::io_event this_event;
//    this_event.res = dwErrorCode;
//    this_event.res2 = dwNumberOfBytesTransfered;
//    this_event.obj = (struct rambrain::iocb*)lpOverlapped;
//    this_event.data = NULL;
//    io_queue->put(this_event);
//}
//
//VOID WINAPI CompletedReadRoutine(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
//{
//    struct rambrain::io_event this_event;
//    this_event.res = dwErrorCode;
//    this_event.res2 = dwNumberOfBytesTransfered;
//    this_event.obj = (struct rambrain::iocb*)lpOverlapped;
//    this_event.data = NULL;
//    io_queue->put(this_event);
//}

//Utilities

#define _SC_PAGE_SIZE 0

DWORD get_page_size()
{
    SYSTEM_INFO sysInfo;

    GetSystemInfo(&sysInfo);

    return sysInfo.dwPageSize;
}

unsigned int sysconf(int type)
{
    switch (type)
    {
    case _SC_PAGE_SIZE: return get_page_size();
    default:break;
    }
    return 0;
}

void usleep(__int64 usec)
{
    HANDLE timer;
    LARGE_INTEGER ft;

    ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObjectEx(timer, INFINITE, TRUE);
    CloseHandle(timer);
}

#define O_DIRECT 100
#define S_IRUSR  200
#define S_IWUSR  300

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM | WINAPI_PARTITION_GAMES)

#else
HANDLE WINAPI CreateFileW(LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    CREATEFILE2_EXTENDED_PARAMETERS createExParams;
    createExParams.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
    createExParams.dwFileAttributes = dwFlagsAndAttributes & 0xFFFF;
    createExParams.dwFileFlags = dwFlagsAndAttributes & 0xFFF00000;
    createExParams.dwSecurityQosFlags = dwFlagsAndAttributes & 0x000F0000;
    createExParams.lpSecurityAttributes = lpSecurityAttributes;
    createExParams.hTemplateFile = hTemplateFile;
    return CreateFile2(lpFileName, dwDesiredAccess, dwShareMode, dwCreationDisposition, &createExParams);
}

int getpid(void)
{
    //there's only one process running that will ever need this, so
    return 666;
}

#endif

HANDLE open(const char* szFileName, int mode, int permissions)
{
    // The file must be opened for asynchronous I/O by using the 
// FILE_FLAG_OVERLAPPED flag.
    DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
    if ((mode & O_DIRECT) == O_DIRECT)
        flags |= FILE_FLAG_NO_BUFFERING;

    std::wstring wsTmp(szFileName, szFileName+strlen(szFileName));

    HANDLE hFile = CreateFileW(wsTmp.c_str(),		// Name of the file
        (GENERIC_READ | GENERIC_WRITE),	// Open for writing
        (FILE_SHARE_READ | FILE_SHARE_WRITE),								// Do not share
        NULL,							// Default security
        CREATE_ALWAYS,					// Overwrite existing
        flags,
        NULL);
    return hFile;
}

BOOL close(HANDLE hndl)
{
    return CloseHandle(hndl);
}




int ftruncate(HANDLE hndl, long long int size_to_reserve)
{

    if (size_to_reserve <= 0)
        return 0;

    DWORD pageSize = get_page_size();

    if (size_to_reserve < pageSize)
        size_to_reserve = pageSize;

    LARGE_INTEGER minus_one = { 0 }, zero = { 0 };
    minus_one.QuadPart = -(long long)pageSize;

    // Get the current file position
    LARGE_INTEGER old_pos = { 0 };
    if (!SetFilePointerEx(hndl, zero, &old_pos, FILE_CURRENT))
        return -1;

    // Move file position to the new end. These calls do NOT result in the actual allocation of
    // new blocks, but they must succeed.
    LARGE_INTEGER new_pos = { 0 };
    new_pos.QuadPart = size_to_reserve;
    if (!SetFilePointerEx(hndl, new_pos, NULL, FILE_END))
        return -1;
    if (!SetEndOfFile(hndl))
        return -1;
    if (!SetFilePointerEx(hndl, minus_one, NULL, FILE_END))
        return -1;
    //char  initializer_buf[1] = { 1 };
    LARGE_INTEGER new_pos_last_page = { 0 };
    new_pos.QuadPart = size_to_reserve - pageSize;
    DWORD written = 0;
    OVERLAPPED ov = { 0 };
    ov.OffsetHigh = new_pos.HighPart;
    ov.Offset = new_pos.LowPart;
    unsigned char* initializer_buf = (unsigned char*)_aligned_malloc(pageSize, pageSize);
    ZeroMemory(initializer_buf, pageSize);

    ov.hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
    if (FALSE != WriteFile(hndl, initializer_buf, pageSize, 0, &ov) ||
        GetLastError() != ERROR_IO_PENDING)
    {
        int last_error = GetLastError();
        return last_error;
    }
    if (TRUE != GetOverlappedResult(hndl, &ov, &written, TRUE) || written != pageSize)
    {
        int last_error = GetLastError();
        return last_error;
    }
    CloseHandle(ov.hEvent);
    return 0;
}

#define FILE_INVALID INVALID_HANDLE_VALUE
#else
#define FILE_INVALID -1
#endif

namespace rambrain
{


//#define DBG_AIO
managedFileSwap::managedFileSwap ( global_bytesize size, const char *filemask, global_bytesize oneFile, bool enableDMA ) : managedSwap ( size ), pageSize ( sysconf ( _SC_PAGE_SIZE ) )
{
    setDMA ( enableDMA );
    if ( oneFile == 0 ) { // Layout this on your own:

        global_bytesize myg = size / 16;
        oneFile = min ( 4 * gig, myg );
        oneFile = max ( mib, oneFile );
    }

    oneFile += oneFile % memoryAlignment == 0 ? 0 : memoryAlignment - ( oneFile % memoryAlignment );
    pageFileSize = oneFile;
    if ( size % oneFile != 0 ) {
        pageFileNumber = size / oneFile + 1;
    } else {
        pageFileNumber = size / oneFile;
    }

    //initialize inherited members:
    swapSize = pageFileNumber * oneFile;
    swapUsed = 0;
    swapFree = swapSize;

    //copy filemask:
    this->filemask = ( char * ) malloc ( sizeof ( char ) * ( strlen ( filemask ) + 1 ) );
    strcpy ( ( char * ) this->filemask, filemask );


    swapFiles = NULL;
    if ( !openSwapFiles() ) {
        throw memoryException ( "Could not create swap files" );
    }

    //Initialize swapmalloc:
    for ( unsigned int n = 0; n < pageFileNumber; n++ ) {
        pageFileLocation *pfloc = new pageFileLocation ( n, 0, pageFileSize );
        free_space[n * pageFileSize] = pfloc;
        all_space[n * pageFileSize] = pfloc;
    }

    instance = this;
#if defined SWAPSTATS && !defined _WIN32
    signal ( SIGUSR2, managedFileSwap::sigStat );
#endif

    aio_eventarr = ( struct io_event * ) malloc ( sizeof ( struct io_event ) * aio_max_transactions );
    memset ( aio_eventarr, 0, sizeof ( struct io_event ) *aio_max_transactions );
    int ioSetupErr = io_setup ( aio_max_transactions, &aio_context );
    if ( 0 != ioSetupErr ) {
        throw ( memoryException ( "Could not initialize aio!" ) );
    }
    memset ( &aio_template, 0, sizeof ( aio_template ) );
    aio_template.aio_reqprio = 0;

#ifndef OpenMP_NOT_FOUND
    io_submit_num_threads = omp_get_max_threads() / 2; //chosen by fair dice roll...
#endif



    io_submit_threads = ( pthread_t * ) malloc ( sizeof ( pthread_t ) * io_submit_num_threads );
    for ( unsigned int n = 0; n < io_submit_num_threads; ++n )
        if ( pthread_create ( io_submit_threads + n, NULL, &io_submit_worker, this ) ) {
            throw memoryException ( "Could not create worker threads for aio" );
        }

    pthread_create ( &io_arrive_thread, NULL, &io_arrrive_worker, this );
}

managedFileSwap::~managedFileSwap()
{
    close();
}


void managedFileSwap::closeSwapFiles()
{
    if ( swapFiles ) {
        for ( unsigned int n = 0; n < pageFileNumber; ++n ) {
            ::close ( swapFiles[n].fileno );
        }
        free ( swapFiles );
    }


    for ( unsigned int n = 0; n < pageFileNumber; ++n ) {
        char fname[1024];
        snprintf ( fname, 1024, filemask, getpid(), n );
#pragma warning(suppress : 4996)
        unlink ( fname );
    }

}

void managedFileSwap::setDMA ( bool arg1 )
{
    enableDMA = arg1;
#ifdef _WIN32
    memoryAlignment = arg1 ? get_page_size() : 1;
#else
    memoryAlignment = arg1 ? 512 : 1; //Dynamical detection is tricky if not impossible to solve in general
#endif
}

void managedFileSwap::close()
{
    if ( !closed ) {
        free ( aio_eventarr );
        closeSwapFiles();
        free ( ( void * ) filemask );
        if ( all_space.size() > 0 ) {
            std::map<global_offset, pageFileLocation *>::iterator it = all_space.begin();
            do {
                delete it->second;
            } while ( ++it != all_space.end() );
        }
        //Kill worker threads by issing suicidal command:
        for ( unsigned int n = 0; n < io_submit_num_threads; ++n ) {
            my_io_submit ( NULL );
        }
        io_arrive_work = false;
        for ( unsigned int n = 0; n < io_submit_num_threads; ++n ) {
            pthread_join ( io_submit_threads[n], NULL );
        }
        pthread_join ( io_arrive_thread, NULL );
        free ( io_submit_threads );
        io_destroy ( aio_context );
    }

    closed = true;
}

bool managedFileSwap::openSwapFileRange ( unsigned int start, unsigned int stop )
{
    for ( unsigned int n = start; n < stop; ++n ) {
        char fname[1024];
        snprintf ( fname, 1024, filemask, getpid(), n );
        swapFiles[n].fileno = open(fname, O_RDWR | O_TRUNC | O_CREAT| ( enableDMA ? O_DIRECT : 0 << 0 ), S_IRUSR | S_IWUSR );
        if ( swapFiles[n].fileno == FILE_INVALID) {
            if ( errno == EINVAL && n == 0 && enableDMA ) { //Probably happens because we have O_DIRECT set even though file system does not support this...
                warnmsg ( "Could not open first swapfile. Probably DMA is not supported on underlying filesystem. Trying again without dma" )
                setDMA ( false );
                return false;
            }
            int error = GetLastError();
            errmsgf ( "Encountered error code %d when opening file %s\n", errno, fname );
            throw memoryException ( "Could not open swap file." );
            return false;
        }
        swapFiles[n].currentSize = 0;
    }
    return true;
}

bool managedFileSwap::openSwapFiles()
{
    if ( swapFiles ) {
        throw memoryException ( "Swap files already opened. Close first" );
        return false;
    }
    swapFiles = ( struct swapFileDesc * ) malloc ( sizeof ( struct swapFileDesc ) * pageFileNumber );

    if ( !openSwapFileRange ( 0, pageFileNumber ) ) // DMA may fail on first / we recover in openSwapFileRange
        if ( !openSwapFileRange ( 0, pageFileNumber ) ) { // if we did not recover, fail...
            return false;
        }
    return true;
}

bool managedFileSwap::extendSwapByPolicy ( global_bytesize min_size )
{

    global_bytesize extendby = 0;
    global_bytesize freeondisk = 0;
    global_bytesize needed = min_size + ( min_size % pageFileSize == 0 ? 0 : ( pageFileSize - min_size % pageFileSize ) );
    switch ( policy ) {
    case swapPolicy::fixed:
        return false;
        break;
    case swapPolicy::autoextendable:
        //Check for still free space at swapfile location:
        freeondisk = getFreeDiskSpace();
        if ( freeondisk < needed ) {
            return false;
        }

        warnmsgf ( "Extending possible swap space by %lu MB ( %lu MB left on hdd)\n", pageFileSize / mib, freeondisk / mib );
        extendby = needed;
        break;
    case swapPolicy::interactive:
        freeondisk = getFreeDiskSpace();
        while ( ( extendby < needed || extendby > freeondisk ) ) {

            warnmsgf ( "I am out of swap.\n\tI can increase in steps of ~%luMB\n\tWe need at least %lu steps, possible steps until disk is full: %lu \n\t please type in an integer number and press enter", pageFileSize / mib, needed / pageFileSize, freeondisk / pageFileSize );
            do {
                std::cin >> extendby;
                if ( !std::cin.fail() ) {
                    break;
                }
                errmsg ( "I don't feel like this being an integer." );
                std::cin.clear();
                std::cin.ignore ( std::numeric_limits<streamsize>::max(), '\n' );
            } while ( true );

            //again check disk free space as this may have changed due to user interaction:
            freeondisk = getFreeDiskSpace();
            extendby *= pageFileSize;
            if ( extendby > freeondisk ) {
                errmsg ( "You want to assign more disk space than you have." );
            }
            if ( extendby < needed ) {
                errmsg ( "You want to assign less than we need." );
            }
        }
        break;

    }
    //Extend by extendby:
    if ( !extendSwap ( extendby ) ) {
        return false;
    }
    return true;
}

bool managedFileSwap::extendSwap ( global_bytesize size )
{
    size += size % pageFileSize == 0 ? 0 : pageFileSize - ( size % pageFileSize );
    if ( size > getFreeDiskSpace() ) {
        return false;
    }
    unsigned int oldpn = pageFileNumber;
    pageFileNumber += size / pageFileSize;
    struct swapFileDesc *oldList = swapFiles;
    swapFiles = ( struct swapFileDesc * ) malloc ( sizeof ( struct swapFileDesc ) * pageFileNumber );
    memcpy ( swapFiles, oldList, sizeof ( swapFileDesc ) *oldpn );
    if ( !openSwapFileRange ( oldpn, pageFileNumber ) ) {
        return false;
    }
    for ( unsigned int n = oldpn; n < pageFileNumber; n++ ) {
        pageFileLocation *pfloc = new pageFileLocation ( n, 0, pageFileSize );
        free_space[n * pageFileSize] = pfloc;
        all_space[n * pageFileSize] = pfloc;
    }

    swapFree += size;
    swapSize += size;
    free ( oldList );
    return true;
}

global_bytesize managedFileSwap::getFreeDiskSpace()
{
#ifdef _WIN32
    ULARGE_INTEGER FreeBytesAvailableToCaller,
        TotalNumberOfBytes,
        TotalNumberOfFreeBytes;

    GetDiskFreeSpaceExA(/*letter.c_str()*/NULL, &FreeBytesAvailableToCaller, &TotalNumberOfBytes, &TotalNumberOfFreeBytes);

    return TotalNumberOfFreeBytes.QuadPart;
#else
    string directory ( filemask );
    std::size_t found  = directory.find_last_of ( "/" );
    if ( found == directory.npos ) {
        directory = ".";
    } else {
        directory = directory.substr ( 0, found );
    }
    struct statvfs stats;
    statvfs ( directory.c_str(), &stats );
    global_bytesize bytesfree =  stats.f_bfree * stats.f_bsize;
    return bytesfree;
#endif
}



pageFileLocation *managedFileSwap::pfmalloc ( global_bytesize size, managedMemoryChunk *chunk )
{
    /**Priority: -Use first free chunk that fits completely
     *          -Distribute over free locations
     *          -look for read-in memory that can be overwritten
     *          -delete cached files and look again
     **/

    pageFileLocation *found = NULL;
    std::map<global_offset, pageFileLocation *>::iterator it;
    if ( free_space.size() == 0 ) {
        return NULL;
    }
    for ( it = free_space.begin(); it != free_space.end(); ++it ) {
        if ( !it->second ) {
            printf ( "Tuuuuut!\n" );
        }
        if ( it->second->size >= size ) {
            found = it->second;
            break;
        }
    }
    pageFileLocation *res = NULL;
    pageFileLocation *former = NULL;
    if ( found ) {
        res = allocInFree ( found, size );
        res->status = PAGE_END;//Don't forget to set the status of the allocated memory.
        res->glob_off_next.chunk = chunk;
    } else { //We need to write out the data in parts.


        //check for enough space:
        global_bytesize total_space = 0;
        it = free_space.begin();
        do {
            total_space -= total_space % memoryAlignment; // We have to pad splitted chunks.
            total_space += it->second->size;
        } while ( total_space < size && ++it != free_space.end() );
        if ( total_space < size ) {
            //Try to free cached elements:
            global_bytesize bz = size - total_space;
            if ( cleanupCachedElements ( bz ) ) {
                total_space += bz;
            }
        }


        if ( total_space >= size ) { //We can concat enough free chunks to satisfy memory requirements
            it = free_space.begin();
            while ( true ) {
                global_bytesize avail_space = it->second->size;
                global_bytesize alloc_here = min ( avail_space, size );
                if ( size > alloc_here ) {
                    alloc_here -= alloc_here % memoryAlignment;
                }
                auto it2 = it;
                ++it2;
                global_offset nextFreeOffset = it2->first;
                pageFileLocation *neu = allocInFree ( it->second, alloc_here );

                size -= alloc_here;
                neu->status = ( size == 0 ? PAGE_END : PAGE_PART );

                if ( !res ) {
                    res = neu;
                }
                if ( former ) {
                    former->glob_off_next.glob_off_next = neu;
                }
                if ( size == 0 ) {
                    neu->glob_off_next.chunk = chunk;
                    break;
                }
                former = neu;
                it = free_space.find ( nextFreeOffset );
                if ( it == free_space.end() ) {
                    neu->status = PAGE_END;
                    break;
                }
            };
            if ( size != 0 ) {
                pffree ( res );
                return NULL;
            }
        } else {
            throw memoryException ( "Out of swap space" );

        }

    }
    return res;
}

pageFileLocation *managedFileSwap::allocInFree ( pageFileLocation *freeChunk, global_bytesize size )
{
    //Hook out the block of free space:
    global_offset formerfree_off = determineGlobalOffset ( *freeChunk );
    free_space.erase ( formerfree_off );

    //We want to allocate a new chunk or use the chunk at hand.
    global_bytesize padded_size = ( size / memoryAlignment + ( size % memoryAlignment == 0 ? 0 : 1 ) ) * memoryAlignment;
    if ( padded_size != size ) {
        if ( padded_size > freeChunk->size ) {
            return NULL;
        }
        swapFree -= padded_size - size;
    }

    if ( freeChunk->size - padded_size < sizeof ( pageFileLocation ) ) { //Memory to manage free space exceeds free space (actually more than)
        //Thus, use the free chunk for your data.
        swapFree -= freeChunk->size - padded_size; //Account for not mallocable overhead, rest is done by claimUse
        freeChunk->size = size;
        return freeChunk;
    } else {
        pageFileLocation *neu = new pageFileLocation ( *freeChunk );
        freeChunk->offset += padded_size;
        freeChunk->size -= padded_size;
        freeChunk->glob_off_next.glob_off_next = NULL;
        global_offset newfreeloc = determineGlobalOffset ( *freeChunk );
        free_space[newfreeloc] = freeChunk;
        neu->size = size;
        all_space[newfreeloc] = freeChunk; //inserts.
        all_space[formerfree_off] = neu;//overwrites.
        return neu;
    }

}

void managedFileSwap::pffree ( pageFileLocation *pagePtr )
{

    bool endIsReached = false;
    do { //We possibly need multiple frees.
        pageFileLocation *next = pagePtr->glob_off_next.glob_off_next;
        endIsReached = ( pagePtr->status == PAGE_END );
        global_offset goff = determineGlobalOffset ( *pagePtr );
        auto it = all_space.find ( goff );


        //Delete possible pending aio_requests
        //Check whether we're about to be deleted

        if ( pagePtr->aio_ptr ) { //Pending aio
            while ( pagePtr->aio_lock != 0 )
                if ( !checkForAIO() ) {
                    pthread_cond_wait ( &managedMemory::swappingCond, &managedMemory::defaultManager->stateChangeMutex );
                };
        }




        //Check if we have free space before us to merge with:
        if ( pagePtr->offset != 0 && it != all_space.begin() )
            if ( ( --it )->second->status == PAGE_FREE ) {
                //Merge previous free space with this chunk
                pageFileLocation *prev = it->second;
                prev->size += pagePtr->size;
                delete pagePtr;
                all_space.erase ( goff );
                pagePtr = prev;
            }
        goff = determineGlobalOffset ( *pagePtr );
        it = all_space.find ( goff );
        ++it;
        //Check if we have unusable space after us to reuse:
        global_offset nextoff = ( it == all_space.end() ? swapSize : determineGlobalOffset ( * ( it->second ) ) );
        global_bytesize size = nextoff - goff;
        if ( pagePtr->size != size ) {
            swapFree += size - pagePtr->size;
            pagePtr->size = size;
        };

        //Check if we have free space after us to merge with;

        if ( it != all_space.end() && it->second->offset != 0 && it->second->status == PAGE_FREE ) {
            //We may merge the pageFileLocation after ourselve:
            global_offset gofffree = determineGlobalOffset ( * ( it->second ) );
            pagePtr->size += it->second->size;
            //The second one may go completely:
            delete it->second;
            free_space.erase ( gofffree );
            all_space.erase ( gofffree );

        }

        //We are left with our free chunk, lets mark it free (possibly redundant.)
        pagePtr->status = PAGE_FREE;
        free_space[goff] = pagePtr;
        pagePtr = next;

    } while ( !endIsReached );


}


//Actual interface:
void managedFileSwap::swapDelete ( managedMemoryChunk *chunk )
{
    if ( chunk->swapBuf ) { //Must not be swapped, as read-only access should lead to keeping the swapped out locs for the moment.
        pageFileLocation *loc = ( pageFileLocation * ) chunk->swapBuf;
        pffree ( loc );
        chunk->swapBuf = NULL;
        if ( !chunk->locPtr ) { //Check if this was a cached element...
            claimUsageof ( chunk->size, false, false );
        }
    }
}

global_bytesize managedFileSwap::swapIn ( managedMemoryChunk *chunk )
{
#ifdef DBG_AIO
    printf ( "swapping in chunk %lu\n", chunk->id );
#endif
    void *buf = _mm_malloc ( chunk->size, memoryAlignment );
    if ( !chunk->swapBuf || chunk->status == MEM_SWAPIN ) {
        return 0;
    }
    if ( buf ) {
        if ( chunk->status & MEM_ALLOCATED || chunk->status == MEM_SWAPIN ) {
            return 0;    //chunk is available or will become available
        }
        chunk->locPtr = buf;
        claimUsageof ( chunk->size, true, true );
        chunk->status = MEM_SWAPIN;
        copyMem ( chunk->locPtr, * ( ( pageFileLocation * ) chunk->swapBuf ) );
        return chunk->size;
    } else {
        return 0;
    }
}

global_bytesize managedFileSwap::swapIn ( managedMemoryChunk **chunklist, unsigned int nchunks )
{
    global_bytesize n_swapped = 0;
    for ( unsigned int n = 0; n < nchunks; ++n ) {
        n_swapped += swapIn ( chunklist[n] ) ;
    }
    return n_swapped;
}

global_bytesize managedFileSwap::swapOut ( managedMemoryChunk *chunk )
{
#ifdef DBG_AIO
    printf ( "swapping out chunk %lu\n", chunk->id );
#endif
    if ( chunk->size > swapFree ) {
        return 0;
    }
    if ( chunk->status == MEM_SWAPPED || chunk->status == MEM_SWAPOUT ) {
        return chunk->size;    //chunk is or will be swapped
    }
    if ( chunk->swapBuf ) { //We already have a position to store to! (happens when read-only was triggered)
        //Nothing to do here, we have read the element and swapOut is trivial from our point of view

#ifdef DBG_AIO
        printf ( "chunks is cached, we have no need to schedule: %lu\n", chunk->id );
#endif
        //We may just mark the chunk as swapped out.
        _mm_free ( chunk->locPtr );
        chunk->locPtr = NULL;
        chunk->status = MEM_SWAPPED;
        claimUsageof ( chunk->size, true, false );//Double booking :-)
        claimUsageof ( chunk->size, false, true );
        managedMemory::signalSwappingCond();
        return chunk->size;

    } else {
#ifdef DBG_AIO
        printf ( "chunks will be scheduled: %lu\n", chunk->id );
#endif
        pageFileLocation *newAlloced = pfmalloc ( chunk->size, chunk );
        if ( newAlloced ) {
            chunk->swapBuf = newAlloced;
            claimUsageof ( chunk->size, false, true );
            managedMemory::defaultManager->claimTobefreed ( chunk->size, true );
            chunk->status = MEM_SWAPOUT;
            copyMem ( *newAlloced, chunk->locPtr );
            return chunk->size;
        } else {
            return 0;
        }
    }
    return 0;

}

global_bytesize managedFileSwap::swapOut ( managedMemoryChunk **chunklist, unsigned int nchunks )
{
    global_bytesize n_swapped = 0;
    for ( unsigned int n = 0; n < nchunks; ++n ) {
        n_swapped += swapOut ( chunklist[n] );
    }
    return n_swapped;
}


global_offset managedFileSwap::determineGlobalOffset ( const pageFileLocation &ref ) const
{
    return ref.file * pageFileSize + ref.offset;
}
pageFileLocation managedFileSwap::determinePFLoc ( global_offset g_offset, global_bytesize length ) const
{
    unsigned int file = g_offset / pageFileSize;
    pageFileLocation pfLoc ( file, g_offset - file * pageFileSize, length, PAGE_UNKNOWN_STATE );
    return pfLoc;
}



void managedFileSwap::scheduleCopy ( pageFileLocation &ref, void *ramBuf, int *tracker, bool reverse )
{

    //We possibly need to resize swap file:
    global_bytesize neededSize = ref.size + ref.offset;
    if ( neededSize > swapFiles[ref.file].currentSize ) { // We need to resize swapFileDesc
        global_bytesize resizeStep = pageFileSize * swapFileResizeFrac;
        neededSize = neededSize % ( resizeStep ) == 0 ? neededSize : resizeStep * ( neededSize / resizeStep + 1 );

        int errcode;
        if ( 0 != ( errcode = ftruncate ( swapFiles[ref.file].fileno, neededSize ) ) ) {
            errmsgf ( "Could not resize swap file with error code %d", errcode );
            throw memoryException ( "Could not resize swap file" );
        };
        swapFiles[ref.file].currentSize = neededSize;
    }
    ++ ( *tracker );
    ++totalSwapActionsQueued;
    ref.aio_ptr = new struct aiotracker;

    ref.aio_lock = 1;

    struct iocb *aio = & ( ref.aio_ptr->aio );

    ref.aio_ptr->tracker = tracker;
#ifdef DBG_AIO
    reverse ? printf ( "scheduling read\n" ) : printf ( "scheduling write\n" );
#endif

    global_bytesize length = ref.size + ( ref.size % memoryAlignment == 0 ? 0 : memoryAlignment - ref.size % memoryAlignment );
    file_descriptor_t fd = swapFiles[ref.file].fileno;
    reverse ? io_prep_pread ( aio, fd, ramBuf, length, ref.offset ) : io_prep_pwrite ( aio, fd, ramBuf, length, ref.offset );

    pendingAios[aio] = &ref;
    my_io_submit ( aio );

}

void *managedFileSwap::io_submit_worker ( void *ptr )
{
    managedFileSwap *dhis = ( managedFileSwap * ) ptr;
    do {
        rambrain_pthread_mutex_lock ( & ( dhis->io_submit_lock ) );
        while ( dhis->io_submit_requests.size() == 0 ) {
            pthread_cond_wait ( & ( dhis->io_submit_cond ), & ( dhis->io_submit_lock ) );
        }
        struct iocb *aio = dhis->io_submit_requests.front();

        dhis->io_submit_requests.pop();
        rambrain_pthread_mutex_unlock ( & ( dhis->io_submit_lock ) );
        if ( aio == 0 ) {
            break;
        }
        int retcode = -EAGAIN;
        while ( 1 != ( retcode = io_submit ( dhis->aio_context, 1, &aio ) ) ) {
            if ( retcode != -EAGAIN ) {
                throw memoryException ( "Could not enqueue request" );
            }
            usleep ( 10 );
        }
    } while ( true );

    return NULL;
}

void *managedFileSwap::io_arrrive_worker ( void *ptr )
{
    managedFileSwap *dhis = ( managedFileSwap * ) ptr;
    while ( dhis->io_arrive_work ) {
        if ( dhis->totalSwapActionsQueued > 0 ) {
            pthread_mutex_lock ( &managedMemory::stateChangeMutex );
            dhis->checkForAIO();
            pthread_mutex_unlock ( &managedMemory::stateChangeMutex );
        }
        usleep ( 1000 );
    }
    return NULL;

}



void managedFileSwap::my_io_submit ( struct iocb *aio )
{
    rambrain_pthread_mutex_lock ( &io_submit_lock );
    io_submit_requests.push ( aio );
    pthread_cond_signal ( &io_submit_cond );
    rambrain_pthread_mutex_unlock ( &io_submit_lock );
}


void managedFileSwap::completeTransactionOn ( pageFileLocation *ref, bool lock )
{
#ifdef DBG_AIO
    printf ( "got a call\n" );
#endif
    while ( ref->status != PAGE_END ) {
        ref = ref->glob_off_next.glob_off_next;
    }
    managedMemoryChunk *chunk = ref->glob_off_next.chunk;
#ifdef DBG_AIO
    printf ( "Working on chunk %lu\n", chunk->id );
#endif
    switch ( chunk->status ) {
    case MEM_SWAPIN:
#ifdef DBG_AIO
        printf ( "Accounting for a swapin of chunk %lu\n", chunk->id );
#endif
        if ( lock ) {
            rambrain_pthread_mutex_lock ( &managedMemory::stateChangeMutex );
        }
        //if we have a user for this object, protect it from being swapped out again
        chunk->status = chunk->useCnt == 0 ? MEM_ALLOCATED : MEM_ALLOCATED_INUSE_READ;
        claimUsageof ( chunk->size, false, false );
        managedMemory::signalSwappingCond();
#ifdef SWAPSTATS
        managedMemory::defaultManager->swap_in_bytes += chunk->size;
#endif
        if ( lock ) {
            rambrain_pthread_mutex_unlock ( &managedMemory::stateChangeMutex );
        }
        break;
    case MEM_SWAPOUT:
#ifdef DBG_AIO
        printf ( "Accounting for a swapout\n" );
#endif
        if ( lock ) {
            rambrain_pthread_mutex_lock ( &managedMemory::stateChangeMutex );
        }
        _mm_free ( chunk->locPtr );
        chunk->locPtr = NULL; // not strictly required.
        chunk->status = MEM_SWAPPED;
        claimUsageof ( chunk->size, true, false );
#ifdef SWAPSTATS
        managedMemory::defaultManager->swap_out_bytes += chunk->size;
#endif
        managedMemory::defaultManager->claimTobefreed ( chunk->size, false );
        managedMemory::signalSwappingCond();
        if ( lock ) {
            rambrain_pthread_mutex_unlock ( &managedMemory::stateChangeMutex );
        }
        break;
    default:
        throw memoryException ( "AIO Synchronization broken!" );
        break;
    }

}


bool managedFileSwap::checkForAIO()
{
    //This may be called by different threads. We only want one waiting for aio-arrivals, the others may continue.
    if ( 0 != pthread_mutex_trylock ( &aioWaiterLock ) ) {
        return false;
    }

    //We're the only thread here. Check if there's still pending requests to wait for:
    if ( totalSwapActionsQueued == 0 ) { //We do not need to wait as nothing is coming.
        rambrain_pthread_mutex_unlock ( &aioWaiterLock );
        return true;    //Do not wait in caller for something to arrive.
    }

#ifdef DBG_AIO
    printf ( "checkForAIO called, we'll wait for an event, got %d in queue\n", totalSwapActionsQueued );
#endif
    //As there's at least one pending transaction, we may wait blocking indefinitely:

    int no_arrived;
tryagain:
    no_arrived = io_getevents ( aio_context, 0, aio_max_transactions, aio_eventarr, NULL );
    struct timespec timeout = {0, 100000};
    if ( no_arrived == 0 ) {
        rambrain_pthread_mutex_unlock ( & ( managedMemory::stateChangeMutex ) );
        no_arrived = io_getevents ( aio_context, 1, aio_max_transactions, aio_eventarr, &timeout );
        rambrain_pthread_mutex_lock ( & ( managedMemory::stateChangeMutex ) );
    }

    if ( no_arrived < 0 ) {
        if ( no_arrived == -EINTR ) { //We've been interrupted by a system call
            goto tryagain;
        }
        rambrain_pthread_mutex_unlock ( &aioWaiterLock );
        printf ( "We got an error back: %d\n", -no_arrived );
        throw memoryException ( "AIO Error" );

    }
#ifdef DBG_AIO
    printf ( "we got %d events\n", no_arrived );
#endif
    for ( int n = 0; n < no_arrived; ++n ) {
        //Try to find mapping:
#ifdef DBG_AIO
        printf ( "Processing event %d \n", n );
#endif
        auto found = pendingAios.find ( aio_eventarr[n].obj );
        if ( found != pendingAios.end() ) {
            pageFileLocation *ref = found->second;
            pendingAios.erase ( found );
            //Deal with element:
            asyncIoArrived ( ref, aio_eventarr + n );
        }

    }


    rambrain_pthread_mutex_unlock ( &aioWaiterLock );
    return true;

};

void managedFileSwap::asyncIoArrived ( rambrain::pageFileLocation *ref, io_event *event )
{

#ifdef DBG_AIO
    printf ( "aio: async io arrived, chunk of size %lu\n", ref->size );
#endif

    //Check whether we're about to be deleted, if so, don't touch the element
    int *tracker = ref->aio_ptr->tracker;
    //Check if aio was completed:


    int err = event->res2; //Seems to be that a value of zero here indicates success.
    if ( err == 0 && event->res == ref->size + ( ref->size % memoryAlignment == 0 ? 0 : memoryAlignment - ref->size % memoryAlignment ) ) { //This part arrived successfully
        delete ref->aio_ptr;
        ref->aio_ptr = NULL;
        ref->aio_lock = 0;

        int lastval = ( *tracker )--;
        if ( lastval == 1 ) {
            completeTransactionOn ( ref , false );
            delete tracker;
        }

        --totalSwapActionsQueued; //Do this at the very last line, as completeTransactionOn() has to be done beforehands.

    } else {
#ifndef _WIN32
        errmsgf ( "We have trouble in chunk %lu, %d ; aio_size %lu, size %lu, transfer size %lu", ref->glob_off_next.chunk->id, err, event->res, ref->size, event->obj->u.c.nbytes );
        errmsgf ( "file-align %lu, err %d, sizeWritten = %lu", ref->offset % memoryAlignment, err, event->res );
#endif
        throw ( memoryException ( "unknown aio error" ) );
    }
}



void managedFileSwap::copyMem (  pageFileLocation &ref, void *ramBuf , bool reverse )
{
    pageFileLocation *cur = &ref;
    char *cramBuf = ( char * ) ramBuf;
    global_bytesize offset = 0;
    int *tracker = new int ( 1 );
    ++totalSwapActionsQueued;
    while ( true ) { //Sift through all pageChunks that have to be read
        scheduleCopy ( *cur, ( void * ) ( cramBuf + offset ), tracker, reverse );
        if ( cur->status == PAGE_END ) {//I have completely written this pageChunk.
            break;
        }
        offset += cur->size;
        cur = cur->glob_off_next.glob_off_next;
    };
    unsigned int trval = ( *tracker )--;
    --totalSwapActionsQueued;
    if ( trval == 1 ) {
        completeTransactionOn ( cur , false ); //We already call having aquired the lock and know that nothing fatal happens
        delete tracker;
    }
}

managedFileSwap *managedFileSwap::instance = NULL;

void managedFileSwap::sigStat ( int signum )
{
    global_bytesize total_space = instance->swapSize;
    global_bytesize free_space = 0;
    global_bytesize free_space2 = 0;
    global_bytesize fractured = 0;
    global_bytesize partend = 0;
    auto it = instance->free_space.begin();
    do {
        free_space += it->second->size;
    } while ( ++it != instance->free_space.end() );

    it = instance->all_space.begin();
    do {
        switch ( it->second->status ) {
        case PAGE_END:
            partend += it->second->size;
            break;
        case PAGE_FREE:
            free_space2 += it->second->size;
            break;
        case PAGE_PART:
            fractured += it->second->size;
            break;
        default:
            break;
        }
    } while ( ++it != instance->all_space.end() );

    printf ( "%ld\t%ld\t%ld\t%e\t%e\t%s\n", free_space, partend, fractured, ( ( double ) free_space ) / ( partend + fractured + free_space ), ( ( ( double ) ( total_space ) - ( partend + fractured + free_space ) ) / ( total_space ) ), ( free_space == instance->swapFree ? "sane" : "insane" ) );


}

bool managedFileSwap::cleanupCachedElements ( global_bytesize minimum_size )
{
    //It would be way nicer to do this entirely within the knowledge of managedFileSwap,
    //However, this is not performant. We will search for chunks that are allocated but have still cached swap space.

    global_bytesize cleanedUp = 0;
    auto it = managedMemory::defaultManager->memChunks.begin();
    while ( ( minimum_size == 0 || cleanedUp < minimum_size ) && it != managedMemory::defaultManager->memChunks.end() ) {
        managedMemoryChunk *chunk = it->second;
        if ( chunk->status & MEM_ALLOCATED && chunk->swapBuf != NULL ) { // We may safely delete the pageFileLocation
            cleanedUp += chunk->size;
            pffree ( ( pageFileLocation * ) chunk->swapBuf );
            chunk->swapBuf = NULL;
        }
        ++it;

    }
    if ( cleanedUp > minimum_size ) {
        return true;
    }
    return false;
}

void managedFileSwap::invalidateCacheFor ( managedMemoryChunk &chunk )
{
    if ( chunk.swapBuf ) {
        pffree ( ( pageFileLocation * ) chunk.swapBuf );
        chunk.swapBuf = NULL;
    }
}


}


