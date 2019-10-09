/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

//
// record_descriptor - RECDES extended functionality
//

#ifndef _RECORD_DESCRIPTOR_HPP_
#define _RECORD_DESCRIPTOR_HPP_

#include "mem_block.hpp"
#include "memory_alloc.h"
#include "memory_private_allocator.hpp"
#include "packable_object.hpp"
#include "storage_common.h"

#include <cinttypes>

// forward definitions
namespace cubthread
{
  class entry;
};

//  record_descriptor extends functionality for recdes:
//
//  typedef struct recdes RECDES;	/* RECORD DESCRIPTOR */
//  struct recdes
//  {
//    int area_size;		/* Length of the allocated area. It includes only the data field. The value is negative
//				 * if data is inside buffer. For example, peeking in a slotted page. */
//    int length;			/* Length of the data. Does not include the length and type fields */
//    INT16 type;			/* Type of record */
//    char *data;			/* The data */
//  };
//

// explicit aliases for PEEK/COPY
enum class record_get_mode
{
  PEEK_RECORD = PEEK,
  COPY_RECORD = COPY
};

class record_descriptor : public cubpacking::packable_object
{
  public:

    // constructors

    // default
    record_descriptor (const cubmem::block_allocator &alloc = cubmem::PRIVATE_BLOCK_ALLOCATOR);
    ~record_descriptor (void);

    record_descriptor (const char *data, std::size_t size);

    // based on recdes
    record_descriptor (const recdes &rec, const cubmem::block_allocator &alloc = cubmem::PRIVATE_BLOCK_ALLOCATOR);

    record_descriptor (record_descriptor &&other);

    void set_recdes (const recdes &rec);

    // peek record from page; changes into record data will not be permitted
    int peek (cubthread::entry *thread_p, PAGE_PTR page, PGSLOTID slotid);

    // copy record from page
    int copy (cubthread::entry *thread_p, PAGE_PTR page, PGSLOTID slotid);

    // get record from page with peek or copy mode
    int get (cubthread::entry *thread_p, PAGE_PTR page, PGSLOTID slotid, record_get_mode mode);

    // getters
    const recdes &get_recdes (void) const;  // get recdes

    const char *get_data (void) const;      // get record data
    std::size_t get_size (void) const;      // get record size
    char *get_data_for_modify (void);

    // setters
    void set_data (const char *data, std::size_t size);      // set record data to byte array
    template <typename T>
    void set_data_to_object (const T &t);               // set record data to object

    void set_record_length (std::size_t length);
    void set_type (std::int16_t type);

    //
    // manipulate record data
    //

    // replace old_size bytes at offset with new_size bytes from new_data
    void modify_data (std::size_t offset, std::size_t old_size, std::size_t new_size, const char *new_data);

    // delete data_size bytes from offset
    void delete_data (std::size_t offset, std::size_t data_size);

    // insert new_size bytes from new_data at offset
    void insert_data (std::size_t offset, std::size_t new_size, const char *new_data);

    // move record data starting from source_offset to dest_offset
    void move_data (std::size_t dest_offset, std::size_t source_offset);

    void pack (cubpacking::packer &packer) const override;
    void unpack (cubpacking::unpacker &unpacker) override;
    std::size_t get_packed_size (cubpacking::packer &packer, std::size_t curr_offset) const override;

    //
    // manipulate record memory buffer
    //

    // resize record buffer
    void resize_buffer (std::size_t size);
    // set external buffer; record type is set to new automatically
    void set_external_buffer (char *buf, std::size_t buf_size);
    template <std::size_t S>
    void set_external_buffer (cubmem::stack_block<S> &membuf);
    void release_buffer (char *&data, std::size_t &size);

  private:

    // debug function to check if data changes are permitted; e.g. changes into peeked records are not permitted
    void check_changes_are_permitted (void) const;
    bool is_mutable () const;

    void update_source_after_get (record_get_mode mode);

    // source of record data
    enum class data_source
    {
      INVALID,          // invalid data
      PEEKED,           // record data peeked from page
      COPIED,           // record data copied from page or another record
      NEW,              // record data is new
      IMMUTABLE         // record data is a constant buffer or object
    };

    recdes m_recdes;                  // underlaying recdes
    cubmem::extensible_block m_own_data;
    // destruction
    data_source m_data_source;        // source of record data
};

//////////////////////////////////////////////////////////////////////////
// template/inline
//////////////////////////////////////////////////////////////////////////

template <std::size_t S>
void
record_descriptor::set_external_buffer (cubmem::stack_block<S> &membuf)
{
  m_own_data.freemem ();
  m_recdes.area_size = membuf.SIZE;
  m_recdes.data = membuf.get_ptr ();
  m_data_source = data_source::NEW;
}

template <typename T>
void
record_descriptor::set_data_to_object (const T &t)
{
  set_data (reinterpret_cast<const char *> (&t), sizeof (t));
}

#endif // !_RECORD_DESCRIPTOR_HPP_
