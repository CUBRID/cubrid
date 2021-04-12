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

#ifndef _LOG_META_HPP_
#define _LOG_META_HPP_

#include "log_checkpoint_info.hpp"
#include "log_lsa.hpp"
#include "packable_object.hpp"

#include <cstdio>
#include <map>

namespace cublog
{
  // todo: replace with real checkpoint info

  class meta : cubpacking::packable_object
  {
    public:
      meta () = default;
      ~meta () = default;

      void load_from_file (std::FILE *stream);       // load meta from meta log file
      void flush_to_file (std::FILE *stream) const;  // write meta to disk

      size_t get_packed_size (cubpacking::packer &serializator, std::size_t start_offset = 0) const;
      void pack (cubpacking::packer &serializator) const;
      void unpack (cubpacking::unpacker &deserializator);

      const checkpoint_info *get_checkpoint_info (const log_lsa &checkpoint_lsa) const;
      void add_checkpoint_info (const log_lsa &chkt_lsa, checkpoint_info &&chkpt_info);
      void add_checkpoint_info (const log_lsa &chkpt_lsa, const checkpoint_info &chkpt_info);
      size_t remove_checkpoint_info_before_lsa (const log_lsa &target_lsa);
      size_t get_checkpoint_info_size () const;

    private:
      using checkpoint_container_t = std::map<log_lsa, checkpoint_info>;   // todo: replace unsigned with checkpoint_info

      checkpoint_container_t m_checkpoints;
  };
}

#endif // !_LOG_META_HPP_
