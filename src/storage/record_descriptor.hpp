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

#include "mem_block.hpp"
#include "memory_alloc.h"
#include "storage_common.h"

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

class record_descriptor
{
  public:

    // constructors

    // default
    record_descriptor (void);
    ~record_descriptor (void);

    // based on an buffers
    template <size_t S>
    record_descriptor (cubmem::stack_block<S> &membuf);
    record_descriptor (const char *data, size_t size);

    // based on recdes
    record_descriptor (const recdes &rec);

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
    void set_data (const char *data, size_t size);      // set record data to byte array
    template <typename T>
    void set_data_to_object (const T &t);               // set record data to object

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

  private:

    // resize record buffer; copy_data is true if existing data must be preserved
    void resize (cubthread::entry *thread_p, std::size_t size, bool copy_data);

    // debug function to check if data changes are permitted; e.g. changes into peeked records are not permitted
    void check_changes_are_permitted (void) const;

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
    char *m_own_data;                 // non-nil value if record descriptor is owner of data buffer; is freed on
    // destruction
    data_source m_data_source;        // source of record data
};

//////////////////////////////////////////////////////////////////////////
// template/inline
//////////////////////////////////////////////////////////////////////////

template <size_t S>
record_descriptor::record_descriptor (cubmem::stack_block<S> &membuf)
{
  m_recdes.area_size = membuf.SIZE;
  m_recdes.length = 0;
  m_recdes.type = REC_HOME;
  m_recdes.data = membuf.get_ptr ();
  m_own_data = NULL;
  m_data_source = data_source::NEW;
}

template <typename T>
void
record_descriptor::set_data_to_object (const T &t)
{
  set_data (reinterpret_cast<const char *> (&t), sizeof (t));
}
