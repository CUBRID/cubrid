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
  meta::get_packed_size (cubpacking::packer &serializer, std::size_t start_offset) const
  {
    size_t end_offset = start_offset;

    end_offset += serializer.get_packed_bool_size (end_offset); // is shutdown
    end_offset += serializer.get_packed_int_size (end_offset); // m_checkpoints.size ()
    for (const auto &chkinfo : m_checkpoints)
      {
	end_offset += serializer.get_packed_bigint_size (end_offset);
	end_offset += serializer.get_packed_int_size (end_offset);
	end_offset += serializer.get_packed_size_overloaded (chkinfo.second, end_offset);
      }
    return end_offset - start_offset;
  }

  void
  meta::pack (cubpacking::packer &serializer) const
  {
    serializer.pack_bool (m_clean_shutdown);
    serializer.pack_to_int (m_checkpoints.size ());
    for (const auto &chkinfo : m_checkpoints)
      {
	serializer.pack_bigint (chkinfo.first.pageid);
	serializer.pack_int (chkinfo.first.offset);
	serializer.pack_overloaded (chkinfo.second);
      }
  }

  void
  meta::unpack (cubpacking::unpacker &deserializer)
  {
    assert (m_checkpoints.empty ());
    size_t size;
    deserializer.unpack_bool (m_clean_shutdown);
    deserializer.unpack_from_int (size);
    for (size_t i = 0; i < size; ++i)
      {
	log_lsa chkpt_lsa;
	std::int64_t upk_bigint;
	int upk_int;
	deserializer.unpack_bigint (upk_bigint);
	deserializer.unpack_int (upk_int);
	assert (upk_int <= INT16_MAX);
	chkpt_lsa = { upk_bigint, static_cast<std::int16_t> (upk_int) };

	checkpoint_info chkinfo;
	deserializer.unpack_overloaded (chkinfo);

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
    cubpacking::unpacker deserializer (charbuf.get (), size);
    unpack (deserializer);
    m_loaded_from_file = true;
  }

  void
  meta::flush_to_file (std::FILE *stream) const
  {
    cubpacking::packer serializer;
    size_t size = static_cast<unsigned> (get_packed_size (serializer, 0));
    std::unique_ptr<char[]> charbuf (new char[size]);
    serializer.set_buffer (charbuf.get (), size);
    pack (serializer);

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

  void
  meta::set_clean_shutdown (bool a_clean_shutdown)
  {
    m_clean_shutdown = a_clean_shutdown;
  }

  bool
  meta::is_loaded_from_file () const
  {
    return m_loaded_from_file;
  }

  const checkpoint_info *
  meta::get_checkpoint_info (const log_lsa &checkpoint_lsa) const
  {
    auto find_it = m_checkpoints.find (checkpoint_lsa);
    if (find_it != m_checkpoints.cend ())
      {
	return &find_it->second;
      }
    else
      {
	return nullptr;
      }
  }

  std::tuple<log_lsa, const checkpoint_info *>
  meta::get_highest_lsa_checkpoint_info () const
  {
    if (m_checkpoints.empty ())
      {
	return { NULL_LSA, nullptr };
      }
    else
      {
	const auto &last_tuple = m_checkpoints.rbegin ();
	return { last_tuple->first, &last_tuple->second };
      }
  }

  void
  meta::add_checkpoint_info (const log_lsa &chkpt_lsa, checkpoint_info &&chkpt_info)
  {
    const checkpoint_container_t::const_iterator found_it = m_checkpoints.find (chkpt_lsa);
    if (found_it != m_checkpoints.cend ())
      {
	// if same LSA checkpoint already exists, replace with new one
	// to be in line with what happens in calling code where the checkpoint is also
	// added to the log for the purpose of being transferred to passive transaction servers
	m_checkpoints.erase (found_it);
      }
    m_checkpoints.insert ({chkpt_lsa, std::move (chkpt_info)});
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
  meta::get_checkpoint_count () const
  {
    return m_checkpoints.size ();
  }

  void
  meta::clear ()
  {
    m_loaded_from_file = false;
    m_clean_shutdown = false;
    m_checkpoints.clear ();
  }
}
