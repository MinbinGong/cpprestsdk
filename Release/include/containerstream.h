/***
* ==++==
*
* Copyright (c) Microsoft Corporation. All rights reserved. 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* ==--==
* =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
*
* containerstream.h
*
* This file defines a basic STL-container-based stream buffer. Reading from the buffer will not remove any data
* from it and seeking is thus supported.
*
* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
****/
#pragma once

#include <vector>
#include <queue>
#include <algorithm>
#include <iterator>
#include "pplxtasks.h"
#include "astreambuf.h"
#include "streams.h"
#ifdef _MS_WINDOWS
#include <safeint.h>
#endif

#ifndef _CONCRT_H
#ifndef _LWRCASE_CNCRRNCY
#define _LWRCASE_CNCRRNCY
// Note to reader: we're using lower-case namespace names everywhere, but the 'Concurrency' namespace
// is capitalized for historical reasons. The alias let's us pretend that style issue doesn't exist.
namespace Concurrency { }
namespace concurrency = Concurrency;
#endif
#endif

// Suppress unreferenced formal parameter warning as they are required for documentation
#pragma warning(push)
#pragma warning(disable : 4100)

namespace Concurrency { namespace streams {

    // Forward declarations

    template<typename _CollectionType> class container_buffer;

    namespace details {

    /// <summary>
    /// The basic_container_buffer class serves as a memory-based steam buffer that supports both writing and reading
    /// sequences of characters.
    /// </summary>
    /// <remarks> When closed, neither writing nor reading is supported any longer. To separate the closing
    /// of read and write capabilities, use list_write_buffer to complement. </remarks>
    template<typename _CollectionType>
    class basic_container_buffer : public streams::details::streambuf_state_manager<typename _CollectionType::value_type>
    {
    public:
        typedef typename _CollectionType::value_type _CharType;
        typedef typename basic_streambuf<_CharType>::traits traits;
        typedef typename basic_streambuf<_CharType>::int_type int_type;
        typedef typename basic_streambuf<_CharType>::pos_type pos_type;
        typedef typename basic_streambuf<_CharType>::off_type off_type;

        /// <summary>
        /// Returns the underlying data container
        /// </summary>
        _CollectionType& collection()
        {
            return m_data;
        }

        /// <summary>
        /// Destructor
        /// </summary>
        virtual ~basic_container_buffer()
        { 
            // Invoke the synchronous versions since we need to
            // purge the request queue before deleting the buffer
            this->_close_read();
            this->_close_write();
        }


    protected:
        /// <summary>
        /// can_seek is used to determine whether a stream buffer supports seeking.
        /// </summary>
        virtual bool can_seek() const { return this->is_open(); }

        /// <summary>
        /// Get the stream buffer size, if one has been set.
        /// </summary>
        /// <param name="direction">The direction of buffering (in or out)</param>
        /// <remarks>An implementation that does not support buffering will always return '0'.</remarks>
        virtual size_t buffer_size(std::ios_base::openmode = std::ios_base::in) const
        {
            return 0;
        }

        /// <summary>
        /// Set the stream buffer implementation to buffer or not buffer.
        /// </summary>
        /// <param name="size">The size to use for internal buffering, 0 if no buffering should be done.</param>
        /// <param name="direction">The direction of buffering (in or out)</param>
        /// <remarks>An implementation that does not support buffering will silently ignore calls to this function and it will not have
        ///          any effect on what is returned by subsequent calls to buffer_size().</remarks>
        virtual void set_buffer_size(size_t , std::ios_base::openmode = std::ios_base::in) 
        {
            return;
        }

        /// <summary>
        /// For any input stream, in_avail returns the number of characters that are immediately available
        /// to be consumed without blocking. May be used in conjunction with sbumpc() and sgetn() to 
        /// read data without incurring the overhead of using tasks.
        /// </summary>
        virtual size_t in_avail() const
        {
            // See the comment in seek around the restiction that we do not allow read head to 
            // seek beyond the current write_end.
            _PPLX_ASSERT(m_current_position <= m_size);
#ifdef _MS_WINDOWS
            msl::utilities::SafeInt<size_t> readhead(m_current_position);
            msl::utilities::SafeInt<size_t> writeend(m_size);
            return (size_t)(writeend - readhead); 
#else
            return m_size - m_current_position;
#endif
        }

        virtual pplx::task<bool> _sync()
        {
            return pplx::task_from_result(true);
        }

        virtual pplx::task<int_type> _putc(_CharType ch)
        {
            int_type retVal = (this->write(&ch, 1) == 1) ? static_cast<int_type>(ch) : traits::eof();
            return pplx::task_from_result<int_type>(retVal);
        }

        virtual pplx::task<size_t> _putn(const _CharType *ptr, size_t count)
        {
            return pplx::task_from_result<size_t>(this->write(ptr, count));
        }

        _CharType* alloc(size_t count)
        {
            if (!this->can_write()) return nullptr;

            // Allocate space
            resize_for_write(m_current_position+count);

            // Let the caller copy the data
            return (_CharType*)&m_data[m_current_position];
        }

        void commit(size_t actual )
        {
            // Update the write position and satisfy any pending reads
            update_current_position(m_current_position+actual);
        }

        virtual bool acquire(_CharType*& ptr, size_t& count)
        {
            if (!this->can_read()) return false;

            count = in_avail();

            if (count > 0)
            {
                ptr = (_CharType*)&m_data[m_current_position];
                return true;
            }
            else
            {
                ptr = nullptr;

                // If the in_avail is 0, we want to return 'false' if the stream buffer
                // is still open, to indicate that a subsequent attempt could
                // be successful. If we return true, it will indicate that the end
                // of the stream has been reached.
                return !this->is_open(); 
            }
        }

        virtual void release(_Out_writes_ (count) _CharType *, _In_ size_t count)
        {
            update_current_position(m_current_position + count);
        }

        virtual pplx::task<size_t> _getn(_Out_writes_ (count) _CharType *ptr, _In_ size_t count)
        {
            return pplx::task_from_result(this->read(ptr, count));
        }

        size_t _sgetn(_Out_writes_ (count) _CharType *ptr, _In_ size_t count)
        { 
            return this->read(ptr, count);
        }

        virtual size_t _scopy(_Out_writes_ (count) _CharType *ptr, _In_ size_t count)
        {
            return this->read(ptr, count, false);
        }

        virtual pplx::task<int_type> _bumpc()
        {
            return pplx::task_from_result(this->read_byte(true));
        }
        
        virtual int_type _sbumpc()
        {
            return this->read_byte(true);
        }

        virtual pplx::task<int_type> _getc()
        {
            return pplx::task_from_result(this->read_byte(false));
        }
        
        int_type _sgetc()
        {
            return this->read_byte(false);
        }

        virtual pplx::task<int_type> _nextc()
        {
            this->read_byte(true);
            return pplx::task_from_result(this->read_byte(false));
        }
        
        virtual pplx::task<int_type> _ungetc()
        {
            auto pos = seekoff(-1, std::ios_base::cur, std::ios_base::in);
            if ( pos == (pos_type)traits::eof())
                return pplx::task_from_result(traits::eof());
            return this->getc();
        }

        virtual pos_type seekpos(pos_type position, std::ios_base::openmode mode)
        {
            pos_type beg(0);

            // Inorder to support relative seeking from the end postion we need to fix an end position.
            // Technically, there is no end for the stream buffer as new writes would just expand the buffer.
            // For now, we assume that the current write_end is the end of the buffer. We use this aritifical
            // end to restrict the read head from seeking beyond what is available. 

            pos_type end(m_size);

            if (position >= beg)
            {
                auto pos = static_cast<size_t>(position);

                // Read head
                if ((mode & std::ios_base::in) && this->can_read()) 
                {
                    if (position <= end)
                    {
                        // We do not allow reads to seek beyond the end or before the start position.
                        update_current_position(pos);
                        return static_cast<pos_type>(m_current_position);
                    }
                }

                // Write head
                if ((mode & std::ios_base::out) && this->can_write()) 
                {
                    // Allocate space
                    resize_for_write(pos);

                    // Nothing to really copy

                    // Update write head and satisfy read requests if any
                    update_current_position(pos);

                    return static_cast<pos_type>(m_current_position);
                }
            }

            return static_cast<pos_type>(traits::eof());
        }
        
        virtual pos_type seekoff(off_type offset, std::ios_base::seekdir way, std::ios_base::openmode mode) 
        {
            pos_type beg = 0;
            pos_type cur = static_cast<pos_type>(m_current_position);
            pos_type end = static_cast<pos_type>(m_size);

            switch ( way )
            {
            case std::ios_base::beg:
                return seekpos(beg + offset, mode);

            case std::ios_base::cur:
                return seekpos(cur + offset, mode);

            case std::ios_base::end:
                return seekpos(end + offset, mode);

            default:
                return static_cast<pos_type>(traits::eof());
            }
        }

    private:
        template<typename _CollectionType1> friend class container_buffer;

        /// <summary>
        /// Constructor
        /// </summary>
        basic_container_buffer(std::ios_base::openmode mode) 
            : streambuf_state_manager<typename _CollectionType::value_type>(mode),
              m_current_position(0),
              m_size(0)
        {
        }

        /// <summary>
        /// Constructor
        /// </summary>
        basic_container_buffer(_CollectionType data, std::ios_base::openmode mode) 
            : streambuf_state_manager<typename _CollectionType::value_type>(mode),
              m_data(std::move(data)),
              m_current_position((mode & std::ios_base::in) ? 0 : m_data.size()),
              m_size(m_data.size())
        {
            validate_mode(mode);
        }

        static void validate_mode(std::ios_base::openmode mode)
        {
            // Disallow simultaneous use of the stream buffer for writing and reading.
            if ((mode & std::ios_base::in) && (mode & std::ios_base::out))
                throw std::invalid_argument("this combination of modes on container stream not supported");
        }

        /// <summary>
        /// Determine if the request can be satisfied.
        /// </summary>
        bool can_satisfy(size_t)
        {
            // We can always satisfy a read, at least partially, unless the
            // read position is at the very end of the buffer.
            return (in_avail() > 0);
        }

        /// <summary>
        /// Reads a byte from the stream and returns it as int_type.
        /// Note: This routine shall only be called if can_satisfy() returned true.
        /// </summary>
        int_type read_byte(bool advance = true)
        {
            _CharType value;
            auto read_size = this->read(&value, 1, advance);
            return read_size == 1 ? static_cast<int_type>(value) : traits::eof();
        }

        /// <summary>
        /// Reads up to count characters into ptr and returns the count of characters copied.
        /// The return value (actual characters copied) could be <= count.
        /// Note: This routine shall only be called if can_satisfy() returned true.
        /// </summary>
        size_t read(_Out_writes_ (count) _CharType *ptr, _In_ size_t count, bool advance = true)
        {
            if (!can_satisfy(count))
                return 0;

            SafeSize request_size(count);
            SafeSize read_size = request_size.Min(in_avail());

            size_t newPos = m_current_position + read_size;

            auto readBegin = begin(m_data) + m_current_position;
            auto readEnd = begin(m_data) + newPos;
            
#ifdef _MS_WINDOWS
            // Avoid warning C4996: Use checked iterators under SECURE_SCL
            std::copy(readBegin, readEnd, stdext::checked_array_iterator<_CharType *>(ptr, count));
#else
            std::copy(readBegin, readEnd, ptr);
#endif // _MS_WINDOWS

            if (advance)
            {
                update_current_position(newPos);
            }

            return (size_t) read_size;
        }

        /// <summary>
        /// Write count characters from the ptr into the stream buffer
        /// </summary>
        size_t write(const _CharType *ptr, size_t count)
        {
            if (!this->can_write() || (count == 0)) return 0;

            auto newSize = m_current_position + count;
            
            // Allocate space
            resize_for_write(newSize);

            // Copy the data
            std::copy(ptr, ptr + count, begin(m_data) + m_current_position);

            // Update write head and satisfy pending reads if any
            update_current_position(newSize);

            return count;
        }

        /// <summary>
        /// Resize the underlying container to match the new write head
        /// </summary>
        void resize_for_write(size_t newPos)
        {
            _PPLX_ASSERT(m_size <= m_data.size());

            // Resize the container if required
            if (newPos > m_size)
            {
                m_data.resize(newPos);
            }
        }

        /// <summary>
        /// Updates the write head to the new position
        /// </summary>
        void update_current_position(size_t newPos)
        {
            // The new write head
            m_current_position = newPos;

            if ( this->can_write() && m_size < m_current_position)
            {
                // Update the size of the buffer with valid data if required
                m_size = m_current_position;
            }

            _PPLX_ASSERT(m_current_position <= m_size);
            _PPLX_ASSERT(m_size <= m_data.size());
        }

        // The actual data store
        _CollectionType m_data;

        // Read/write head
        size_t m_current_position;
        size_t m_size;
    };

    } // namespace details

    /// <summary>
    /// The basic_container_buffer class serves as a memory-based steam buffer that supports both writing and reading
    /// sequences of characters. It can be used as a consumer/producer buffer.
    /// </summary>
    /// <remarks> 
    /// This is a reference-counted version of basic_container_buffer.
    /// </remarks>
    template<typename _CollectionType>
    class container_buffer : public streambuf<typename _CollectionType::value_type>
    {
    public:
        typedef typename _CollectionType::value_type char_type;

        /// <summary>
        /// Create a container_buffer given a collection, copying its data into the buffer.
        /// </summary>
        /// <param name="data">The collection that is the starting point for the buffer</param>
        /// <param name="mode">The I/O mode that the buffer should use (in / out)</param>
        container_buffer(_CollectionType data, std::ios_base::openmode mode = std::ios_base::in) 
            : streambuf<typename _CollectionType::value_type>(
                std::shared_ptr<details::basic_container_buffer<_CollectionType>>(new details::basic_container_buffer<_CollectionType>(std::move(data), mode)))
        {
        }

        /// <summary>
        /// Create a container_buffer starting from an empty collection.
        /// </summary>
        /// <param name="mode">The I/O mode that the buffer should use (in / out)</param>
        container_buffer(std::ios_base::openmode mode = std::ios_base::out) 
            : streambuf<typename _CollectionType::value_type>(
                std::shared_ptr<details::basic_container_buffer<_CollectionType>>(new details::basic_container_buffer<_CollectionType>(mode)))
        {
        }

        _CollectionType& collection() const
        {
            auto listBuf = static_cast<details::basic_container_buffer<_CollectionType> *>(this->get_base().get());
            return listBuf->collection();
        }
    };

    /// <summary>
    /// A static class to allow users to create input and out streams based off STL
    /// collections. The sole purpose of this class to avoid users from having to know
    /// anything about stream buffers.
    /// </summary>
    template<typename _CollectionType>
    class container_stream
    {
    public:

        typedef typename _CollectionType::value_type char_type;
        typedef container_buffer<_CollectionType> buffer_type;
       
        static concurrency::streams::basic_istream<char_type> open_istream(_CollectionType data)
        {
            return concurrency::streams::basic_istream<char_type>(buffer_type(std::move(data), std::ios_base::in));
        }

        static concurrency::streams::basic_ostream<char_type> open_ostream()
        {
            return concurrency::streams::basic_ostream<char_type>(buffer_type(std::ios_base::out));
        }
    };

    /// <summary>
    /// The stringstream allows an input stream to be constructed from std::string or std::wstring
    /// For outout streams the underlying string container could be retrieved using buf->collection()
    /// </summary>
    typedef container_stream<std::basic_string<char>> stringstream;
    typedef stringstream::buffer_type stringstreambuf;

    typedef container_stream<utility::string_t> wstringstream;
    typedef wstringstream::buffer_type wstringstreambuf;

    /// <summary>
    /// The byte allows an input stream to be constructed from any STL container
    /// </summary>
    class bytestream
    {
    public:
       
        template<typename _CollectionType>
        static concurrency::streams::istream open_istream(_CollectionType data)
        {
            return concurrency::streams::istream(streams::container_buffer<_CollectionType>(std::move(data), std::ios_base::in));
        }

        template<typename _CollectionType>
        static concurrency::streams::ostream open_ostream()
        {
            return concurrency::streams::ostream(streams::container_buffer<_CollectionType>());
        }
};

    
}} // namespaces

#pragma warning(pop) // 4100
