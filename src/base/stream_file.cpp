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
 * stream_file.cpp
 */

#ident "$Id$"


#include <cstdio>
#include <cassert>
#if defined (WINDOWS)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include "stream_file.hpp"
#include "error_manager.h"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"
#include "thread_looper.hpp"
#include "porting.h"
#include "system_parameter.h" /* er_log_debug */

#include <cstdio>     /* for std::remove */
#include <limits>     /* for std::numeric_limits */

namespace cubstream
{

  const int stream_file::FILE_CREATE_FLAG = O_CREAT;

  /* daemon task for writing stream contents to stream file
   * The stream_file object has a m_target_flush_position; this is the target set by the stream
   * and is the position which desired to be saved to file (all data up to this position)
   * When 'execute' method starts will begin writting all data from 'last_dropped_pos" to m_target_flush_position
   * last_dropped_pos is retrieved from stream by get_last_dropable_pos()
   * To prevent contention on stream, a small buffer is used to read from stream into the buffer
   * and then save to stream_file the contents of buffer
   * while the loop in the execute is running the m_target_flush_position may be updated by the stream:
   * in such case, the execution is continued until the updated target is achieved.
   */
  class writer_daemon_task : public cubthread::entry_task
  {
    public:
      writer_daemon_task (multi_thread_stream &stream_arg)
	: m_stream (stream_arg),
	  m_stream_file (*stream_arg.get_stream_file ())
      {
	m_copy_to_buffer_func = std::bind (&writer_daemon_task::copy_to_buffer, std::ref (*this),
					   std::placeholders::_1, std::placeholders::_2);
      }

      void execute (cubthread::entry &thread_ref) override
      {
	int err = NO_ERROR;
	stream_position curr_pos;
	stream_position end_pos;

	m_stream_file.wait_flush_signal (curr_pos, end_pos);

	assert (m_stream.get_last_committed_pos () >= end_pos);

	while (curr_pos < end_pos)
	  {
	    if (m_stream_file.is_stopped ())
	      {
		return;
	      }

	    size_t amount_to_copy = MIN (end_pos - curr_pos, BUFFER_SIZE);

	    err = m_stream.read (curr_pos, amount_to_copy, m_copy_to_buffer_func);
	    if (err <= 0)
	      {
		ASSERT_ERROR ();
		return;
	      }

	    err = m_stream_file.write (curr_pos, m_buffer, amount_to_copy);
	    if (err != NO_ERROR)
	      {
		ASSERT_ERROR ();
		return;
	      }

	    curr_pos += amount_to_copy;
	    m_stream.set_last_recyclable_pos (curr_pos);

	  }
      }

      int copy_to_buffer (char *ptr, const size_t byte_count)
      {
	memcpy (m_buffer, ptr, byte_count);
	return (int) byte_count;
      };

    private:
      /* buffer of 256K */
      static const int BUFFER_SIZE = 256 * 1024;

      multi_thread_stream &m_stream;
      stream_file &m_stream_file;

      char m_buffer[BUFFER_SIZE];

      cubstream::stream::read_func_t m_copy_to_buffer_func;
  };


  void stream_file::init (const stream_position &start_append_pos, const size_t file_size, const int print_digits)
  {
    m_stream.set_stream_file (this);

    m_strict_append_mode = STRICT_APPEND_MODE;

    m_base_filename = m_stream.name ();
    m_desired_volume_size = file_size;
    m_filename_digits_seqno = print_digits;

    m_append_position = start_append_pos;
    m_drop_position = std::numeric_limits<stream_position>::max ();

    m_target_flush_position = start_append_pos;
    m_req_start_flush_position = start_append_pos;

    m_start_vol_seqno = 0;

    m_start_flush_handler = std::bind (&stream_file::start_flush, std::ref (*this), std::placeholders::_1,
				       std::placeholders::_2);
    m_stream.set_filled_stream_handler (m_start_flush_handler);

    m_is_stopped = false;

#if defined (SERVER_MODE)
    m_write_daemon = cubthread::get_manager ()->create_daemon (cubthread::delta_time (0),
		     new writer_daemon_task (m_stream),
		     "replication_stream_flush");
#endif
  }

  void stream_file::finalize (void)
  {
    std::map<int,int>::iterator it;

    m_is_stopped = true;

    er_log_debug_replication (ARG_FILE_LINE, "stream_file::finalize file_path:%s, stream:%s\n",
			      m_base_path.c_str (), m_stream.name ().c_str ());

    force_start_flush ();

    cubthread::get_manager ()->destroy_daemon (m_write_daemon);

    std::unique_lock<std::mutex> ulock (m_file_descriptor_mutex);
    for (it = m_file_descriptors.begin (); it != m_file_descriptors.end (); it ++)
      {
	assert (it->second > 0);
	close (it->second);
      }
    m_file_descriptors.clear ();
  }

  int stream_file::get_file_desc_from_vol_seqno (const int vol_seqno)
  {
    std::unique_lock<std::mutex> ulock (m_file_descriptor_mutex);
    std::map<int,int>::iterator it  = m_file_descriptors.find (vol_seqno);
    if (it != m_file_descriptors.end ())
      {
	return it->second;
      }

    return -1;
  }

  /*
   * get_vol_seqno_from_stream_pos
   * get volume sequence for a position;
   * it does not acquire mutex, so the caller is responsible to hold it, if logic requires it
   */
  int stream_file::get_vol_seqno_from_stream_pos (const stream_position &pos)
  {
    stream_position start_available_pos;
    int vol_seqno;

    start_available_pos = m_start_vol_seqno * m_desired_volume_size;

    if (pos < start_available_pos)
      {
	/* not on disk anymore */
	return -1;
      }

    vol_seqno = (int) (pos / m_desired_volume_size);

    return vol_seqno;
  }

  int stream_file::get_vol_seqno_from_stream_pos_ext (const stream_position &pos, size_t &amount, size_t &vol_offset)
  {
    std::unique_lock<std::mutex> ulock (m_drop_files_mutex);
    if (pos < m_drop_position || m_drop_position == std::numeric_limits<stream_position>::max ())
      {
	return -1;
      }

    int vol_seqno = get_vol_seqno_from_stream_pos (pos);
    ulock.unlock ();

    if (vol_seqno >= 0)
      {
	vol_offset = pos - vol_seqno * m_desired_volume_size;

	amount = (vol_seqno + 1) * m_desired_volume_size - pos;
      }

    return vol_seqno;
  }

  /*
   * create_volumes_in_range
   *
   * this is an utility function to create "zeroed" content (including missing volumes) for a range
   * of stream positions; it should not be needed in normal usage since writting should be done by appending
   * previously written range
   *
   */
  int stream_file::create_volumes_in_range (const stream_position &start_pos, const stream_position &end_pos)
  {
    stream_position curr_pos;
    size_t volume_offset, hole_amount, available_amount_in_volume;
    int vol_seqno;
    int err = NO_ERROR;
    const size_t BUFFER_SIZE = 4 * 1024;
    char zero_buffer[BUFFER_SIZE] = { 0 };

    curr_pos = start_pos;
    hole_amount = start_pos - m_append_position;

    if (hole_amount == 0)
      {
	/* append in a new volume(s) : volumes are created as empty (without filling with zero) */
	while (curr_pos < end_pos)
	  {
	    vol_seqno = get_vol_seqno_from_stream_pos_ext (curr_pos, available_amount_in_volume, volume_offset);
	    if (vol_seqno < 0)
	      {
		err = ER_STREAM_FILE_INVALID_WRITE;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, m_stream.name ().c_str (), curr_pos, 0);
		return err;
	      }

	    /* volume_offset == 0 ==> this is a new volume (we didn't written in it)
	     * volume_offset > 0  ==> volume is already created, make sure it may be opened */
	    int dummy_fd = open_vol_seqno (vol_seqno, (volume_offset == 0) ? FILE_CREATE_FLAG : 0);
	    if (dummy_fd < 0)
	      {
		ASSERT_ERROR_AND_SET (err);
		return err;
	      }

	    /* advance to next volume */
	    curr_pos += available_amount_in_volume;
	  }

	return err;
      }

    /* create volumes and fill them with zero : for range: m_append_position < start_pos */
    curr_pos = m_append_position;
    while (hole_amount > 0)
      {
	vol_seqno = get_vol_seqno_from_stream_pos_ext (curr_pos, available_amount_in_volume, volume_offset);
	if (vol_seqno < 0)
	  {
	    err = ER_STREAM_FILE_INVALID_WRITE;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, m_stream.name ().c_str (), curr_pos, 0);
	    return err;
	  }

	int dummy_fd = open_vol_seqno (vol_seqno, (volume_offset == 0) ? FILE_CREATE_FLAG : 0);
	if (dummy_fd < 0)
	  {
	    ASSERT_ERROR_AND_SET (err);
	    return err;
	  }

	size_t rem_amount_this_volume = std::min (hole_amount, available_amount_in_volume);

	while (rem_amount_this_volume > 0)
	  {
	    size_t amount_to_write = std::min (BUFFER_SIZE, rem_amount_this_volume);
	    size_t written_bytes = write_buffer (vol_seqno, volume_offset, zero_buffer, amount_to_write);
	    if (written_bytes != amount_to_write)
	      {
		ASSERT_ERROR_AND_SET (err);
		return err;
	      }
	    rem_amount_this_volume -= written_bytes;
	    volume_offset += written_bytes;
	  }

	rem_amount_this_volume = std::min (hole_amount, available_amount_in_volume);

	hole_amount -= rem_amount_this_volume;
	curr_pos += rem_amount_this_volume;
      }

    return NO_ERROR;
  }

  /*
   * drop_volumes_to_pos
   *
   * physically removes files exclusively used by range 0 -> drop_pos
   * a file containing data after drop_pos is not removed
   *
   */
  int stream_file::drop_volumes_to_pos (const stream_position &drop_pos, bool force_set)
  {
    stream_position curr_pos;
    int err = NO_ERROR;

    std::unique_lock<std::mutex> drop_lock (m_drop_files_mutex);
    curr_pos = (m_drop_position == std::numeric_limits<stream_position>::max ()) ? 0 : m_drop_position;
    drop_lock.unlock ();

    std::unique_lock<std::mutex> u_flush_lock (m_flush_mutex);
    stream_position actual_drop_pos = std::min (drop_pos, m_append_position);
    u_flush_lock.unlock ();

    drop_lock.lock ();
    stream_position last_full_vol_drop_pos = m_start_vol_seqno * m_desired_volume_size;
    drop_lock.unlock ();

    while (curr_pos < actual_drop_pos)
      {
	size_t volume_offset;
	size_t amount_to_end_volume;

	int vol_seqno = get_vol_seqno_from_stream_pos_ext (curr_pos, amount_to_end_volume, volume_offset);

	if (volume_offset == 0 && amount_to_end_volume <= actual_drop_pos - curr_pos)
	  {
	    drop_lock.lock ();
	    if (vol_seqno >= m_start_vol_seqno)
	      {
		m_start_vol_seqno = vol_seqno + 1;
		last_full_vol_drop_pos = m_start_vol_seqno * m_desired_volume_size;
	      }
	    drop_lock.unlock ();

	    err = close_vol_seqno (vol_seqno, REMOVE_PHYSICAL_FILE);
	    if (err != NO_ERROR)
	      {
		return err;
	      }
	  }
	curr_pos += amount_to_end_volume;
      }

    drop_lock.lock ();
    if (last_full_vol_drop_pos > m_drop_position)
      {
	m_drop_position = last_full_vol_drop_pos;
      }
    if (force_set == true)
      {
	m_drop_position = actual_drop_pos;
      }
    drop_lock.unlock ();

    return err;
  }

  int stream_file::get_vol_filename_with_vol_seqno (char *filename, const size_t max_filename, const int vol_seqno)
  {
    assert (vol_seqno >= 0);

    snprintf (filename, max_filename, "%s%c%s_%0*d",
	      m_base_path.c_str (),
	      PATH_SEPARATOR,
	      m_base_filename.c_str (),
	      m_filename_digits_seqno,
	      vol_seqno);
    return NO_ERROR;
  }

  /*
   * open_vol_seqno
   *
   * returns the file description of volume or opens it, if not already open and caches the descriptor
   */
  int stream_file::open_vol_seqno (const int vol_seqno, int flags)
  {
    int fd;
    char file_name [PATH_MAX];
    int err = NO_ERROR;

    std::unique_lock<std::mutex> ulock (m_file_descriptor_mutex);
    auto it = m_file_descriptors.find (vol_seqno);

    if (it != m_file_descriptors.end ())
      {
	assert ((flags & FILE_CREATE_FLAG) == 0);
	return it->second;
      }

    ulock.unlock ();

    get_vol_filename_with_vol_seqno (file_name, sizeof (file_name) - 1, vol_seqno);

    fd = open_file (file_name, flags);
    if (fd < 0)
      {
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_VOL, 2, file_name, "");
	assert ((flags & FILE_CREATE_FLAG) != FILE_CREATE_FLAG);
	return -1;
      }

    ulock.lock ();
    m_file_descriptors[vol_seqno] = fd;
    ulock.unlock ();

    return fd;
  }

  /*
   * close_vol_seqno
   *
   * Closes the file descriptor associated to a file sequence
   * if remove_physical flag is set it also physical removes the file
   *
   */
  int stream_file::close_vol_seqno (const int vol_seqno, bool remove_physical)
  {
    char file_name[PATH_MAX];
    int fd = -1;
    int err = NO_ERROR;

    std::unique_lock<std::mutex> ulock (m_file_descriptor_mutex);
    auto it = m_file_descriptors.find (vol_seqno);
    if (it != m_file_descriptors.end ())
      {
	fd = it->second;
      }

    if (fd == -1)
      {
	ulock.unlock ();
	/* already closed */
	if (remove_physical)
	  {
	    /* reopen for the purpose of physical remove (to make sure it still exists) */
	    fd = open_vol_seqno (vol_seqno);
	    if (fd < 0)
	      {
		ASSERT_ERROR_AND_SET (err);
		return err;
	      }

	    assert (fd != -1);
	  }
      }

    if (fd == -1)
      {
	/* already closed, nothing to do */
	assert (remove_physical == false);
	return err;
      }

    if (remove_physical)
      {
	get_vol_filename_with_vol_seqno (file_name, sizeof (file_name) - 1, vol_seqno);
      }

    if (!ulock.owns_lock ())
      {
	ulock.lock ();
	it = m_file_descriptors.find (vol_seqno);
      }

    assert (it != m_file_descriptors.end ());
    m_file_descriptors.erase (it);
    ulock.unlock ();

    if (close (fd) != 0)
      {
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME, 1, file_name);
	err = ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME;
	return err;
      }

    if (remove_physical)
      {
	if (std::remove (file_name) != 0)
	  {
	    er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME, 1,
				 file_name);
	    err = ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME;
	    return err;
	  }
      }

    return err;
  }

  int stream_file::open_file (const char *file_path, int flags)
  {
    int fd;

    if ((flags & FILE_CREATE_FLAG) == FILE_CREATE_FLAG)
      {
	fd = create_file (file_path);
	if (fd < 0 && errno == EACCES)
	  {
	    if (std::remove (file_path) != 0)
	      {
		er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME, 1,
				     file_path);
		return ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME;
	      }

	    fd = create_file (file_path);
	    if (fd < 0)
	      {
		/* TODO[replication] : error */
		assert (false);
		return -1;
	      }
	  }

	close (fd);
	/* closed and fall through to reopen */
      }
#if defined (WINDOWS)
    fd = open (file_path, O_RDWR | O_BINARY | flags);
#else
    fd = open (file_path, O_RDWR);
#endif

    if (fd < 0)
      {
	/* TODO[replication] : error */
	assert (false);
      }

    return fd;
  }

  int stream_file::create_file (const char *file_path)
  {
    int fd;

#if defined (WINDOWS)
    fd = _sopen (file_path, O_RDWR | O_CREAT | O_BINARY, _SH_DENYWR, 0600);
#else
    fd = open (file_path, O_RDWR | O_CREAT | O_EXCL);
#endif

    if (fd < 0)
      {
	/* TODO[replication] : error */
      }

    return fd;
  }

  /*
   * read_buffer : reads from volume identified by "vol_seqno" from offset "volume_offset" into the buffer "buf"
   *               an amount of "amount" bytes
   * return : number of bytes actually read
   */
  size_t stream_file::read_buffer (const int vol_seqno, const size_t volume_offset, char *buf, const size_t amount)
  {
    size_t actual_read;
    int fd;

    fd = open_vol_seqno (vol_seqno);

    if (fd <= 0)
      {
	return 0;
      }

#if defined (WINDOWS)
    /* TODO : use Windows API for paralel reads */
    m_io_mutex.lock ();
    off_t actual_offset = lseek (fd, (long) volume_offset, SEEK_SET);
    if (actual_offset != volume_offset)
      {
	actual_read = 0;
      }
    else
      {
	actual_read = ::read (fd, buf, amount);
      }
    m_io_mutex.unlock ();
#else
    actual_read = pread (fd, (void *) buf, amount, volume_offset);
#endif

    if (actual_read < amount)
      {
	char filename[PATH_MAX] = {0};
	get_vol_filename_with_vol_seqno (filename, sizeof (filename), vol_seqno);
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_FILE_CANNOT_READ, 3, filename,
			     volume_offset, amount);
      }

    return actual_read;
  }

  /*
   * write_buffer : write in volume identified by "vol_seqno" at offset "volume_offset" the buffer "buf" of size "amount"
   * return : number of bytes actually written
   */
  size_t stream_file::write_buffer (const int vol_seqno, const size_t volume_offset, const char *buf, const size_t amount)
  {
    size_t actual_write;
    int fd;

    fd = open_vol_seqno (vol_seqno);

    if (fd <= 0)
      {
	return 0;
      }

#if defined (WINDOWS)
    m_io_mutex.lock ();
    lseek (fd, (long ) volume_offset, SEEK_SET);
    actual_write = ::write (fd, buf, amount);
    m_io_mutex.unlock ();
#else
    actual_write = pwrite (fd, buf, amount, volume_offset);
#endif

    if (actual_write < amount)
      {
	char filename[PATH_MAX] = {0};
	get_vol_filename_with_vol_seqno (filename, sizeof (filename), vol_seqno);
	er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_FILE_CANNOT_WRITE, 3, filename,
			     volume_offset, amount);
      }
    return actual_write;
  }

  /*
   * write
   *
   * writes the contents of buf of size amount into the stream file starting from position pos
   *
   */
  int stream_file::write (const stream_position &pos, const char *buf, const size_t amount)
  {
    stream_position curr_pos;
    stream_position force_drop_pos = 0;
    size_t vol_offset, rem_amount, available_amount_in_volume;
    int vol_seqno;
    int err = NO_ERROR;

    std::unique_lock<std::mutex> drop_lock (m_drop_files_mutex);
    if (m_drop_position != std::numeric_limits<stream_position>::max ()
	&& pos < m_drop_position)
      {
	assert (false);
	err = ER_STREAM_FILE_INVALID_WRITE;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, m_stream.name ().c_str (), pos, amount);
	return err;
      }
    else if (m_drop_position == std::numeric_limits<stream_position>::max ())
      {
	/* this is the first flush in stream;
	 * we update the append_pos after the successful write, so readers cannot actually read yet */
	m_drop_position = pos;
      }
    drop_lock.unlock ();

    std::unique_lock<std::mutex> flush_lock (m_flush_mutex);
    if (m_strict_append_mode == STRICT_APPEND_MODE && pos != m_append_position)
      {
	assert (false);
	err = ER_STREAM_FILE_INVALID_WRITE;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, m_stream.name ().c_str (), pos, amount);
	return err;
      }

    if (pos > m_append_position && m_append_position > 0)
      {
	/* writing to a position larger than append,
	 * we need to drop all up to new position since we cannot guarantee reading valid data */
	assert (m_strict_append_mode == UNCONTIGUOUS_APPEND_MODE);
	m_append_position = pos;
	force_drop_pos = m_append_position;
      }
    assert (m_append_position + amount <= m_target_flush_position);
    flush_lock.unlock ();

    if (force_drop_pos > 0)
      {
	drop_volumes_to_pos (force_drop_pos);
      }

    curr_pos = pos;
    rem_amount = amount;

    if (curr_pos + amount > m_append_position)
      {
	err = create_volumes_in_range (curr_pos, curr_pos + amount);
	if (err != NO_ERROR)
	  {
	    return err;
	  }
      }

    while (rem_amount > 0)
      {
	size_t current_to_write;
	size_t actual_write;

	vol_seqno = get_vol_seqno_from_stream_pos_ext (curr_pos, available_amount_in_volume, vol_offset);
	if (vol_seqno < 0)
	  {
	    err = ER_STREAM_FILE_INVALID_WRITE;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, m_stream.name ().c_str (), curr_pos, 0);
	    return err;
	  }

	current_to_write = std::min (available_amount_in_volume, rem_amount);

	actual_write = write_buffer (vol_seqno, vol_offset, buf, current_to_write);
	if (actual_write < current_to_write)
	  {
	    ASSERT_ERROR_AND_SET (err);
	    return err;
	  }

	rem_amount -= actual_write;
	curr_pos += current_to_write;
	buf += current_to_write;
      }

    /* get flush mutex while incrementing append_position:
     * this is not mandatory, but we only need to protect the assertions from start_flush and wait_flush_signal
     * if we give up the mutex here, we also need to give up assertions involving m_append_position in those methods */
    flush_lock.lock ();
    m_append_position += amount;
    flush_lock.unlock ();

    return NO_ERROR;
  }

  /*
   * read
   *
   * reads into buffer buf an amount of size amount from the stream file starting from position pos
   * return : error code
   *
   */
  int stream_file::read (const stream_position &pos, char *buf, const size_t amount)
  {
    stream_position curr_pos;
    size_t available_amount_in_volume, volume_offset, rem_amount;
    int vol_seqno;
    int err = NO_ERROR;

    curr_pos = pos;
    rem_amount = amount;

    if (pos + amount > m_append_position)
      {
	assert (false);
	err = ER_STREAM_FILE_INVALID_READ;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, m_stream.name ().c_str (), pos, amount);
	return err;
      }

    while (rem_amount > 0)
      {
	size_t current_to_read;
	size_t actual_read;

	vol_seqno = get_vol_seqno_from_stream_pos_ext (curr_pos, available_amount_in_volume, volume_offset);
	if (vol_seqno < 0)
	  {
	    err = ER_STREAM_FILE_INVALID_READ;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, err, 3, m_stream.name ().c_str (), curr_pos, 0);
	    return err;
	  }

	current_to_read = MIN (available_amount_in_volume, rem_amount);

	actual_read = read_buffer (vol_seqno, volume_offset, buf, current_to_read);
	if (actual_read < current_to_read)
	  {
	    ASSERT_ERROR_AND_SET (err);
	    return err;
	  }

	rem_amount -= current_to_read;
	curr_pos += current_to_read;
	buf += current_to_read;
      }

    return NO_ERROR;
  }

  void stream_file::start_flush (const stream_position &start_position, const size_t amount_to_flush)
  {
    if (start_position < get_last_flushed_position ())
      {
	return;
      }
    std::unique_lock<std::mutex> ulock (m_flush_mutex);
    assert (start_position + amount_to_flush <= m_stream.get_last_committed_pos ());
    if (start_position > m_req_start_flush_position
	|| m_req_start_flush_position == 0
	|| (start_position + amount_to_flush > m_target_flush_position))
      {
	if (start_position > m_req_start_flush_position)
	  {
	    m_req_start_flush_position = start_position;
	  }

	if (start_position + amount_to_flush > m_target_flush_position)
	  {
	    m_target_flush_position = start_position + amount_to_flush;
	  }

	assert (m_req_start_flush_position < m_target_flush_position);
	assert (m_target_flush_position <= m_stream.get_last_committed_pos ());

	m_flush_cv.notify_one ();
      }
  }

  void stream_file::force_start_flush (void)
  {
    std::unique_lock<std::mutex> ulock (m_flush_mutex);
    m_flush_cv.notify_one ();
  }

  void stream_file::wait_flush_signal (stream_position &start_position, stream_position &target_position)
  {
    std::unique_lock<std::mutex> ulock (m_flush_mutex);
    /* check again against m_append_position : it may change after the previous loop of flush daemon  */
    m_flush_cv.wait (ulock, [&] { return m_is_stopped ||
					 (m_append_position < m_target_flush_position);
				});

    start_position = m_append_position;
    target_position = m_target_flush_position;
    assert (target_position >= m_append_position);
  }
} /* namespace cubstream */
