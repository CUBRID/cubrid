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
  class meta : cubpacking::packable_object
  {
    public:
      meta () = default;
      ~meta () = default;

      void load_from_file (std::FILE *stream);       // load meta from meta log file
      void flush_to_file (std::FILE *stream);        // write meta to disk

      bool is_loaded_from_file () const;

      inline bool get_clean_shutdown () const
      {
	return m_clean_shutdown;
      }
      void set_clean_shutdown (bool a_clean_shutdown);

      const checkpoint_info *get_checkpoint_info (const log_lsa &checkpoint_lsa) const;
      std::tuple<log_lsa,  const checkpoint_info *> get_highest_lsa_checkpoint_info () const;
      void add_checkpoint_info (const log_lsa &chkpt_lsa, checkpoint_info &&chkpt_info);
      void add_checkpoint_info (const log_lsa &chkpt_lsa, const checkpoint_info &chkpt_info);
      size_t remove_checkpoint_info_before_lsa (const log_lsa &target_lsa);
      size_t get_checkpoint_count () const;

      void clear ();

    private:
      size_t get_packed_size (cubpacking::packer &serializer, std::size_t start_offset = 0) const override;
      void pack (cubpacking::packer &serializer) const override;
      void unpack (cubpacking::unpacker &deserializer) override;

    private:
      using checkpoint_container_t = std::map<log_lsa, checkpoint_info>;

    private:
      bool m_loaded_from_file = false;

      /* flag parallel to 'log global header is shutdown':
       *  - false: server has not been clean shut down
       *  - true: has been clean shut down
       * after start, flag is set to 'false' and the meta log is saved to ensure state is persisted
       */
      bool m_clean_shutdown = false;

      /* as the system is designed, it is not needed to hold a map of checkpoints since there should
       * be, at most, 2 checkpoints:
       *  - the current checkpoint: the one to be used in case of crash
       *  - the new - in progress - checkpoint: at the moment the new checkpoint is commited to disk, the
       *      previous - current - checkpoint is discarded and the new checkpoint becomes the current one
       * but the implementation is simpler with a container
       */
      checkpoint_container_t m_checkpoints;
  };
}

#endif // !_LOG_META_HPP_
