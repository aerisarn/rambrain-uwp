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

#ifndef GENERICMANAGEDPTR_H
#define GENERICMANAGEDPTR_H

#include "managedMemory.h"
#include "rambrain_atomics.h"
#include "common.h"
#include "exceptions.h"
#include <type_traits>
#include <pthread.h>

namespace rambrain
{
    /**
     * \brief Main class to allocate memory that is managed by the rambrain memory defaultManager in a multidimensional way given by the second template parameter
     *
     * @warning _thread-safety_
     * * The object itself is not thread-safe
     * * Do not pass pointers/references to this object over thread boundaries
     * @note _thread-safety_
     * * The object itself may be passed over thread boundary
     *
     * **/
    class genericManagedPtr
    {
    public:
        ///@brief copy ctor
        genericManagedPtr(const genericManagedPtr& ref);

        ///@brief instantiates managedPtr containing n_elem elements and s_size
        genericManagedPtr(size_t s_size, unsigned int n_elem);

        ///@brief destructor
        ~genericManagedPtr();

        /// @brief Simple getter
        inline  size_t size() const {
            return n_elem * s_size;
        }

        inline  unsigned int count() const {
            return n_elem;
        }

        ///@brief tells the memory manager to possibly swap in chunk for near future use
        bool prepareUse() const;

        ///@brief Atomically sets use to a chunk if tracker is not already set to true. returns whether we set use or not.
        bool setUse(bool writable = true, bool* tracker = NULL) const;

        ///@brief unsets use count on memory chunk
        bool unsetUse(unsigned int loaded = 1) const;

        ///@brief assignment operator
        genericManagedPtr& operator= (const genericManagedPtr& ref);

        /** @brief returns local pointer to object
         *  @note when called, you have to care for setting use to the chunk, first
         * */
        void* getLocPtr() const;

        /** @brief returns const local pointer to object
         *  @note when called, you have to care for setting use to the chunk, first
         * */
        const void* getConstLocPtr() const;

    private:
        managedMemoryChunk* chunk;
        unsigned int* tracker;
        unsigned int n_elem;
        size_t s_size;

        ///@brief This function manages correct deallocation for array elements lacking a destructor
        void mDelete();

        /** @brief: indefinitely waits for swapin of the chunk
         *  While it would have been desirable to throw exceptions when chunk is not available,
         *  checking this in a save way would produce lots of overhead. Thus, it is better to wait
         *  indefinitely in this case, as under normal use, the chunk will become available.
        **/
        void waitForSwapin() const;

        friend class genericAdhereTo;
        friend class generidcAdhereToConst;
    };


    /**
     * @brief Main class to fetch memory that is managed by rambrain for actual usage.
     *
     * \warning _thread-safety_
     * * The object itself is not thread-safe
     * * Do not pass pointers/references to this object over thread boundaries
     *
     * \note _thread-safety_
     * * The object itself may be copied over thread boundaries
     * **/
    class genericAdhereTo
    {
    public:
        ///@brief copy constructor
        genericAdhereTo(const genericAdhereTo& ref);

        /**@brief constructor fetching data
         * \param loadImmediately set this to false if you want to load the element when pulling the pointer and not beforehands
         * \param data the managedPtr that is to be used in near future
        **/
        genericAdhereTo(const genericManagedPtr& data, bool loadImmediately = true);

        /// Provides the same functionality as the other constructor but accepts a managedPtr pointer as argument. @see adhereTo ( const managedPtr<T> &data, bool loadImmediately = true )
        genericAdhereTo(const genericManagedPtr* data, bool loadImmediately = true) : genericAdhereTo(*data, loadImmediately) {};

        ///Simple assignment operator
        genericAdhereTo& operator= (const genericAdhereTo& ref);

        ///@brief This operator can be used to pull the data to a const pointer. Use this whenever possible.
        operator const void* () { //This one is needed as c++ refuses to pick operator const T *() const as a default in this case
            return *((const genericAdhereTo*)this);
        }
        ///@brief This operator can be used to pull the data to a const pointer. Use this whenever possible.
        operator const void* () const;
        ///@brief This operator can be used to pull the data to a non-const pointer. If you only read the data, pull the const version, as this saves execution time.
        operator void* ();
        ///@brief destructor
        ~genericAdhereTo();
    private:
        const genericManagedPtr* data;

        mutable bool loadedWritable = false;
        mutable bool loadedReadable = false;

    };
}

#endif

