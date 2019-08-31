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

/*
 * stream_file.hpp
 */

#ident "$Id$"

#ifndef _STREAM_FILE_HPP_
#define _STREAM_FILE_HPP_

#include "multi_thread_stream.hpp"
#include "semaphore.hpp"
#include <map>
#include <queue>

namespace cubthread
{
  class daemon;
}

namespace cubstream
{

  // same as FILEIO_DISK_PROTECTION_MODE from file_io.c
  // better redefine another one here in order not to pull all dependencies from file_io module
  static const unsigned int RW_USR_MODE = 0600;

  /*
   * class for handling reading/writing to volumes from a stream
   * stream file consists of several physical files (volumes) on disk;
   * there is a base file name (considered the name of stream), suffixed by a sequence number, generated incrementally
   * For instance: $CUBRID_DATABASES/<dbname>/replica_0000, replica_0001, ... replica_0123
   * each volume file has a configured fixed size
   * The volume having the filename of base is not used; (maybe reserved for metainformation ?)
   *
   * Restrictions and usage:
   * - a range of data is written only if all previous ranges are written (we don't allow holes in written data)
   * - active range is considered between a dropped position and append position; the active range is contiguous
   * - read range must entirely be inside the active range
   *
   * TODOs:
   * - resizing of physical files (should be entirely off-line)
   */
  class stream_file
  {
    private:
      static const int DEFAULT_FILENAME_DIGITS = 4;
      static const bool REMOVE_PHYSICAL_FILE = true;

      static const int STRICT_APPEND_MODE = 1;
      static const int UNCONTIGUOUS_APPEND_MODE = 0;

      /* 100 MBytes */
      static const size_t DEFAULT_VOLUME_SIZE = 100 * 1024 * 1024;

      /* if enabled all writes must be in succession of append_position */
      bool m_strict_append_mode;

      multi_thread_stream &m_stream;

      /* largest stream position written to file */
      stream_position m_append_position;

      /* position of the last drop (no data before this is available) */
      stream_position m_drop_position;
      /* oldest avaiable volume (old volume may be removed to save disk space) */
      int m_start_vol_seqno;
      std::mutex m_drop_files_mutex;

      /* mapping of file sequence to OS file descriptor */
      std::map<int, int> m_file_descriptors;
      std::mutex m_file_descriptor_mutex;

      /* stream file is split into several physical files (volumes)
       * this is the desired size of such physical file;
       * a new volume should be created when the current volume (the one which is appended into) size approaches this
       * limit or already exceeded it;
       * the purpose is to avoid splitting stream entries among stream volumes
       */
      size_t m_desired_volume_size;

      /* base name of stream physical files */
      std::string m_base_filename;

      /* path for stream physical files location */
      std::string m_base_path;

      /* number of digits in the sequence number suffixing the base name
       * TODO : this should be used only as a nice priting of names, we should allow increasing sequence number
       * even if exceeds the digits in filename */
      int m_filename_digits_seqno;

      /* last position which must be flushed; this is the position imposed to flusher to be reached
       * (it may not be appended yet) */
      stream_position m_target_flush_position;

      stream_position m_req_start_flush_position;

      /* notify on fsync data */
      std::atomic<bool> m_notify_on_sync;
      std::queue<int> m_sync_seq_nrs;
      stream_position m_to_be_synced;
      stream::notify_send_stream_pos_func_t m_sync_notifier;
      stream::notify_func_t m_start_flush_handler;

      cubthread::daemon *m_write_daemon;
      std::mutex m_flush_mutex;
      std::condition_variable m_flush_cv;

      cubsync::event_semaphore m_writer_stop;
      bool m_is_stop_request;   // used with m_flush_mutex

      static const int FILE_CREATE_FLAG;

#if defined (WINDOWS)
      std::mutex m_io_mutex;
#endif

    protected:
      int get_file_desc_from_vol_seqno (const int vol_seqno);
      int get_vol_seqno_from_stream_pos (const stream_position &pos);
      int get_vol_seqno_from_stream_pos_ext (const stream_position &pos, size_t &amount, size_t &vol_offset);

      int create_volumes_in_range (const stream_position &start_pos, const stream_position &end_pos);

      int get_vol_filename_with_vol_seqno (char *filename, const size_t max_filename, const int vol_seqno);

      int open_vol_seqno (const int vol_seqno, int flags = 0);
      int close_vol_seqno (const int vol_seqno, bool remove_physical = false);

      int open_file (const char *file_path, int flags = 0);

      int create_file (const char *file_path, unsigned int mode);

      size_t read_buffer (const int vol_seqno, const size_t volume_offset, char *buf, const size_t amount);
      size_t write_buffer (const int vol_seqno, const size_t volume_offset, const char *buf, const size_t amount);
      int fsync_writes ();

    public:
      stream_file () = delete;

      stream_file (multi_thread_stream &stream_arg, const std::string &path,
		   const size_t file_size = DEFAULT_VOLUME_SIZE, const int print_digits = DEFAULT_FILENAME_DIGITS)
	: m_stream (stream_arg)
	, m_notify_on_sync (false)
	, m_to_be_synced (std::numeric_limits<stream_position>::min ())
      {
	init (path, 0, file_size, print_digits);
      };

      ~stream_file ()
      {
	stop ();
      };

      void init (const std::string &path,
		 const stream_position &start_append_pos = 0,
		 const size_t file_size = DEFAULT_VOLUME_SIZE,
		 const int print_digits = DEFAULT_FILENAME_DIGITS);

      void set_append_mode (int mode)
      {
	m_strict_append_mode = mode;
      }
      
      void stop ();

      int write (const stream_position &pos, const char *buf, const size_t amount);

      int read (const stream_position &pos, char *buf, const size_t amount);

      int drop_volumes_to_pos (const stream_position &drop_pos, bool force_set = false);

      size_t get_volume_size (void)
      {
	return m_desired_volume_size;
      }

      size_t get_max_available_from_pos (const stream_position &pos)
      {
	assert (m_append_position >= pos);
	if (m_append_position > pos)
	  {
	    return m_append_position - pos;
	  }
	else
	  {
	    return 0;
	  }
      }

      void update_sync_position (stream_position to_be_synced)
      {
	m_to_be_synced = to_be_synced;
      }

      stream_position get_last_flushed_position (void)
      {
	return m_append_position;
      }

      stream_position get_drop_position (void)
      {
	return m_drop_position;
      }

      void set_sync_notifier (const stream::notify_send_stream_pos_func_t &sync_done_notify)
      {
	m_sync_notifier = sync_done_notify;
	m_notify_on_sync = true;
      }

      void remove_sync_notifier ()
      {
	m_notify_on_sync = false;
	m_sync_notifier = [] (const stream_position &)
	{
	  assert (false);
	};
      }

      void start_flush (const stream_position &start_position, const size_t amount_to_flush);

      void force_start_flush (void);

      void wait_flush_signal (stream_position &start_position, stream_position &target_position);

      bool is_stop_requested (void)
      {
	return m_is_stop_request;
      }
      void stop_daemon (void)
      {
	m_is_stop_request = true;
      }

      void set_daemon_is_stopped (void)
      {
        m_writer_stop.set ();
      }

      stream_position get_min_available_pos (void) const;
  };

} /*  namespace cubstream */

#endif /* _STREAM_FILE_HPP_ */
