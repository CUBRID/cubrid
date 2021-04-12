/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "log_meta.hpp"

#include "packer.hpp"

#include <memory>

namespace cublog
{
  size_t
  meta::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = 0;

    size += serializator.get_packed_int_size (start_offset + size); // m_checkpoints.size ()
    for (const auto chkinfo : m_checkpoints)
      {
	size += serializator.get_packed_bigint_size (start_offset + size);
	size += serializator.get_packed_int_size (start_offset + size);
	size += serializator.get_packed_size_overloaded (chkinfo.second, start_offset + size);
      }
    return size;
  }

  void
  meta::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_to_int (m_checkpoints.size ());
    for (const auto chkinfo : m_checkpoints)
      {
	serializator.pack_bigint (chkinfo.first.pageid);
	serializator.pack_int (chkinfo.first.offset);
	serializator.pack_overloaded (chkinfo.second);
      }
  }

  void
  meta::unpack (cubpacking::unpacker &deserializator)
  {
    size_t size;
    deserializator.unpack_from_int (size);
    for (size_t i = 0; i < size; ++i)
      {
	log_lsa chkpt_lsa;
	std::int64_t upk_bigint;
	int upk_int;
	deserializator.unpack_bigint (upk_bigint);
	deserializator.unpack_int (upk_int);
	assert (upk_int <= INT16_MAX);
	chkpt_lsa = { upk_bigint, static_cast<std::int16_t> (upk_int) };

	checkpoint_info chkinfo;
	deserializator.unpack_overloaded (chkinfo);

	m_checkpoints.insert ({ chkpt_lsa, std::move (chkinfo) });
      }
  }

  void
  meta::load_from_file (std::FILE *stream)
  {
    if (std::feof (stream))
      {
	// empty file
	return;
      }
    // read size of packed data
    size_t size;
    size_t ret = std::fread (&size, sizeof (size), 1, stream);
    if (ret < 1)
      {
	return;
      }
    std::unique_ptr<char[]> charbuf (new char[size]);
    ret = std::fread (charbuf.get (), 1, size, stream);
    if (ret < size)
      {
	return;
      }
    cubpacking::unpacker deserializator (charbuf.get (), size);
    unpack (deserializator);
  }

  void
  meta::flush_to_file (std::FILE *stream) const
  {
    cubpacking::packer serializator;
    size_t size = static_cast<unsigned> (get_packed_size (serializator, 0));
    std::unique_ptr<char[]> charbuf (new char[size]);
    serializator.set_buffer (charbuf.get (), size);
    pack (serializator);

    // write size
    size_t ret = std::fwrite (&size, sizeof (size), 1, stream);
    if (ret < 1)
      {
	assert (false);
	return;
      }
    // write packed data
    ret = std::fwrite (charbuf.get (), 1, size, stream);
    if (ret < size)
      {
	assert (false);
	return;
      }
    std::fflush (stream);
  }

  const checkpoint_info *
  meta::get_checkpoint_info (const log_lsa &checkpoint_lsa) const
  {
    auto find_it = m_checkpoints.find (checkpoint_lsa);
    if (find_it == m_checkpoints.cend ())
      {
	return nullptr;
      }
    else
      {
	return &find_it->second;
      }
  }

  void
  meta::add_checkpoint_info (const log_lsa &chkpt_lsa, checkpoint_info &&chkpt_info)
  {
    m_checkpoints.insert ({ chkpt_lsa, std::move (chkpt_info) });
  }

  void
  meta::add_checkpoint_info (const log_lsa &chkpt_lsa, const checkpoint_info &chkpt_info)
  {
    m_checkpoints.insert ({ chkpt_lsa, chkpt_info });
  }

  size_t
  meta::remove_checkpoint_info_before_lsa (const log_lsa &target_lsa)
  {
    size_t removed_count = 0;
    for (auto it = m_checkpoints.begin (); it != m_checkpoints.end ();)
      {
	if (it->first < target_lsa)
	  {
	    it = m_checkpoints.erase (it);
	    ++removed_count;
	  }
	else
	  {
	    ++it;
	  }
      }
    return removed_count;
  }

  size_t
  meta::get_checkpoint_info_size () const
  {
    return m_checkpoints.size ();
  }
}
