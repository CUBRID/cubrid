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

#include "config.h"
#include "pgbuf_inspector.hpp"
#include "pgbuf_inspector_socket.hpp"
#include "system_parameter.h"
#include "disk_manager.h"
#include "file_io.h"
#include "page_buffer.h"
#include "boot_sr.h"
#include "tcp.h"
#include "thread_manager.hpp"
#include "thread_daemon.hpp"
#include "thread_entry_task.hpp"
#include "error_manager.h"

#include <openssl/sha.h>
#include <limits.h>
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

REGISTER_DAEMON (pgbuf_inspector);

namespace cubpgbuf
{
  namespace inspector
  {
    namespace
    {
      cubthread::daemon *inspector_daemon = nullptr;

      bool append_volume (VOLID volid, INT64 creation, INT64 volume_creation, void *argument)
      {
	auto &db = *static_cast<identity *> (argument);
	if (db.oversized)
	  {
	    return true;
	  }
	// Field names alone exceed 32 bytes per volume; this cap only rejects provably oversized proofs.
	if (db.volumes.size () >= 2048)
	  {
	    db.oversized = true;
	    return true;
	  }
	if (!db.volumes.empty () && db.database_creation != static_cast<std::uint64_t> (creation))
	  {
	    return false;
	  }
	db.database_creation = creation;
	db.volumes.push_back ({volid, static_cast<std::uint64_t> (volume_creation), 0, 0});
	return true;
      }
      bool read_identity (identity &db)
      {
	pgbuf_get_lru_counts (&db.shared_lru_count, &db.private_lru_count);
	if (!disk_map_cached_persistent_volumes (append_volume, &db))
	  {
	    return false;
	  }
	for (auto &volume : db.volumes)
	  {
	    struct stat st;
	    if (fstat (fileio_get_volume_descriptor (volume.volid), &st) != 0)
	      {
		return false;
	      }
	    volume.device = st.st_dev;
	    volume.inode = st.st_ino;
	  }
	return true;
      }
      class attachment_task : public cubthread::entry_task
      {
	public:
	  endpoint socket;
	  void execute (cubthread::entry &) override
	  {
	    socket.poll ();
	  }
      };
      void unavailable ()
      {
	// Local operator diagnostic only. Do not include these details in wire frames.
	const char *message = "PGBUF_INSPECTOR_UNAVAILABLE: startup activation failed; unavailable until server restart";
	fprintf (stderr, "%s\n", message);
	er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	_er_log_debug (ARG_FILE_LINE, "%s\n", message);
      }
    }
    void initialize ()
    {
      // Startup only. Disabled creates neither a task nor a socket and performs no identity collection.
      if (!prm_get_bool_value (PRM_ID_ENABLE_PGBUF_INSPECTOR))
	{
	  return;
	}
      identity db;
      char canonical[PATH_MAX];
      if (!read_identity (db) || realpath (boot_db_full_name (), canonical) == nullptr)
	{
	  unavailable ();
	  return;
	}
      std::string source = std::string (canonical) + "\n" + std::to_string (db.database_creation);
      unsigned char digest[SHA256_DIGEST_LENGTH];
      SHA256 (reinterpret_cast<const unsigned char *> (source.data ()), source.size (), digest);
      std::string key;
      const char *hex = "0123456789abcdef";
      for (unsigned i = 0; i < 16; ++i)
	{
	  key += hex[digest[i] >> 4];
	  key += hex[digest[i] & 15];
	}
      std::string master = css_get_master_domain_path ();
      std::string root = master.substr (0, master.rfind ('/'));
      auto *task = new attachment_task ();
      if (!task->socket.start (root, key, db, read_identity))
	{
	  delete task;
	  unavailable ();
	  return;
	}
      inspector_daemon = cubthread::get_manager ()->create_daemon (
				 cubthread::looper (std::chrono::milliseconds (5)), task, "pgbuf-inspector");
      if (inspector_daemon == nullptr)
	{
	  delete task;
	  unavailable ();
	  return;
	}
      fprintf (stderr, "PGBUF_INSPECTOR_READY: %s\n", task->socket.path ().c_str ());
    }
    void finalize ()
    {
      cubthread::get_manager ()->destroy_daemon (inspector_daemon);
    }
  }
}
