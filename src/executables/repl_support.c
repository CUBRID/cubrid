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
 * repl_support.c - Common functions of replication processes
 */

#ident "$Id$"

#include "config.h"

#include <sys/stat.h>
#include <sys/ioctl.h>
#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/resource.h>
#include <unistd.h>
#include <limits.h>
#endif /* ! WINDOWS */
#include <netdb.h>
#include <netinet/tcp.h>

#include "broker_error.h"
#include "repl_tp.h"
#include "memory_alloc.h"
#include "environment_variable.h"
#include "connection_cl.h"
#include "system_parameter.h"
#include "repl_support.h"
#include "repl_agent.h"
#include "message_catalog.h"
#include "utility.h"


extern REPL_ERR *err_Head;
extern int debug_Dump_info;

int repl_Pipe_to_master = -1;

/*
 * repl_io_read() - read a page from the disk
 *   return: error code
 *     vdes(in): the volume descriptor of the target file
 *     io_pgptr(out): start pointer to be read
 *     pageid(in): page id to read
 *     pagesize(in): page size to wrea
 *
 * Note:
 *     reads a predefined size of page from the disk
 *
 *     called by RECV thread of repl_agent
 *       - to read end log
 *     called by APPLY thread of repl_agent
 *       - to read log page from the disk (using local buffer)
 *     called by SEND thread of repl_server
 *       - to read log header
 *       - to read final log page
 *     called by READ thread of repl_server
 *       - to read log page
 *
 *     Caller shoule consider "mutex lock"
 */
int
repl_io_read (int vdes, void *io_pgptr, PAGEID pageid, int pagesize)
{
  int remain_bytes = pagesize;
  int nbytes;
  off_t offset = REPL_GET_FILE_SIZE (pagesize, pageid);
  char errno_buffer[FILE_PATH_LENGTH];
  char *current_ptr = (char *) io_pgptr;

  if (lseek (vdes, offset, SEEK_SET) == -1)
    {
      snprintf (errno_buffer, FILE_PATH_LENGTH, "%d", errno);
      REPL_ERR_RETURN_ONE_ARG (REPL_FILE_COMM, REPL_COMMON_ERROR,
			       errno_buffer);
    }

  while (remain_bytes > 0)
    {
      nbytes = read (vdes, current_ptr, pagesize);
      /*
       * The nbytes is zero indicates end of file.
       * This case is not possible.
       */
      if (nbytes == 0)
	{
	  snprintf (errno_buffer, FILE_PATH_LENGTH, "EOF");
	  REPL_ERR_RETURN_ONE_ARG (REPL_FILE_COMM, REPL_COMMON_ERROR,
				   errno_buffer);
	}
      else if (nbytes == -1)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  snprintf (errno_buffer, FILE_PATH_LENGTH, "%d", errno);
	  REPL_ERR_RETURN_ONE_ARG (REPL_FILE_COMM, REPL_COMMON_ERROR,
				   errno_buffer);
	}
      remain_bytes -= nbytes;
      current_ptr += nbytes;
    }
  return NO_ERROR;
}

/*
 * repl_io_write() - write a page from the disk
 *   return:
 *     vdes(in): the volume descriptor of the target file
 *     io_pgptr(out): start pointer to be written
 *     pageid(in): page id to read
 *     pagesize(in): page size to write
 *
 * Note:
 *     writes a predefined size of page from the disk
 *
 *     called by MAIN thread of repl_agent
 *       - to write the trail log
 *     called by RECV thread of repl_agent
 *       - to write the log header
 *       - to write the end log
 *     called by APPLY thread of repl_agent
 *       - to write the trail log
 *     called by FLUSH thread of repl_agent
 *       - to write the log page
 *
 *     Caller shoule consider "mutex lock"
 */
int
repl_io_write (int vdes, void *op_pgptr, PAGEID pageid, int pagesize)
{
  int remain_bytes = pagesize;
  int nbytes;
  off_t offset = REPL_GET_FILE_SIZE (pagesize, pageid);
  char errno_buffer[FILE_PATH_LENGTH];
  char *current_ptr = (char *) op_pgptr;

  if (lseek (vdes, offset, SEEK_SET) == -1)
    {
      snprintf (errno_buffer, FILE_PATH_LENGTH, "%d", errno);
      REPL_ERR_RETURN_ONE_ARG (REPL_FILE_COMM, REPL_COMMON_ERROR,
			       errno_buffer);
    }

  while (remain_bytes > 0)
    {
      nbytes = write (vdes, current_ptr, remain_bytes);
      if (nbytes == -1)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  snprintf (errno_buffer, FILE_PATH_LENGTH, "%d", errno);
	  REPL_ERR_RETURN_ONE_ARG (REPL_FILE_COMM, REPL_COMMON_ERROR,
				   errno_buffer);
	}
      remain_bytes -= nbytes;
      current_ptr += nbytes;
    }

  return NO_ERROR;
}

int
repl_io_write_copy_log_info (int vdes, void *op_pgptr, PAGEID pageid,
			     int pagesize)
{
  int remain_bytes = DB_SIZEOF (COPY_LOG);
  int nbytes;
  off_t offset = REPL_GET_FILE_SIZE (pagesize, pageid);
  char errno_buffer[FILE_PATH_LENGTH];
  char *current_ptr = (char *) op_pgptr;

  if (lseek (vdes, offset, SEEK_SET) == -1)
    {
      snprintf (errno_buffer, FILE_PATH_LENGTH, "%d", errno);
      REPL_ERR_RETURN_ONE_ARG (REPL_FILE_COMM, REPL_COMMON_ERROR,
			       errno_buffer);
    }

  while (remain_bytes > 0)
    {
      nbytes = write (vdes, current_ptr, remain_bytes);
      if (nbytes == -1)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  snprintf (errno_buffer, FILE_PATH_LENGTH, "%d", errno);
	  REPL_ERR_RETURN_ONE_ARG (REPL_FILE_COMM, REPL_COMMON_ERROR,
				   errno_buffer);
	}
      remain_bytes -= nbytes;
      current_ptr += nbytes;
    }

  return NO_ERROR;
}

/*
 * repl_io_open() - open a file
 *   return: File descriptor
 *
 * Note:
 *     called by MAIN thread of repl_agent
 *       - to open the trail log file ( <-repl_ag_open_repl_trail_file())
 *     called by RECV thread of repl_agent
 *       - to open the copy log file
 *     called by READ thread of repl_server
 *       - to open the transaction log file ( <-repl_svr_tr_read())
 */
int
repl_io_open (const char *vlabel, int flags, int mode)
{
  int vdes;

  do
    {
      vdes = open (vlabel, flags, mode);
    }
  while (vdes == NULL_VOLDES && errno == EINTR);

  return vdes;
}

/*
 * repl_io_truncate() - truncate the active copy log after making archives
 *   return: error code
 *   vdes(in)
 *   pagesize(in)
 *   paggeid(in)
 *
 * Note:
 */
int
repl_io_truncate (int vdes, int pagesize, PAGEID pageid)
{
  int retry = true;
  off_t length = REPL_GET_FILE_SIZE (pagesize, pageid);

  while (retry == true)
    {
      retry = false;

      if (ftruncate (vdes, length))
	{
	  if (errno == EINTR)
	    retry = true;
	  else
	    {
	      REPL_ERR_RETURN (REPL_FILE_COMM, REPL_COMMON_ERROR);
	    }
	}
    }
  return NO_ERROR;
}

/*
 * repl_io_rename() - rename a file
 *   return:
 *   active_copy_log(in)
 *   active_vol(out)
 *   archive_copy_log(in)
 *   archive_vol(out)
 *
 * Note:
 *      to increase the performance of archiving the copy log,
 *      1. rename the active copy log to archive log name
 *      2. create the active log file
 *      3. copy the active portion to the new active log file
 *      4. truncate the active portion of the archive
 *
 *    This function does step 1 & 2
 */
int
repl_io_rename (char *active_copy_log, int *active_vol,
		char *archive_copy_log, int *archive_vol)
{
  close (*active_vol);
  rename (active_copy_log, archive_copy_log);

  *archive_vol = repl_io_open (archive_copy_log, O_RDWR, 0);

  if (*archive_vol == NULL_VOLDES)
    return REPL_COMMON_ERROR;

  /* create the active log */
  umask (S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  *active_vol = creat (active_copy_log, FILE_CREATE_MODE);

  /* TO DO : verify why we need to re-open after close */
  close (*active_vol);
  *active_vol = repl_io_open (active_copy_log, O_RDWR, 0);

  if (*active_vol == NULL_VOLDES)
    REPL_ERR_RETURN (REPL_FILE_COMM, REPL_COMMON_ERROR);

  return NO_ERROR;
}

/*
 * repl_io_file_size() - returns the file size
 *   return: file size
 *   vdes(in)
 */
off_t
repl_io_file_size (int vdes)
{
  off_t seek_result;

  seek_result = lseek (vdes, 0, SEEK_END);

  return seek_result;
}

/*
 * repl_pack_server_name() - make a "server_name" string
 *   return: packed name
 *   serveryn(in)      : true if repl_server
 *   server_name(in)   : server name
 *   name_length(out)   : length of packed name
 *
 * Note:
 *         make a "server_name" string to connect to master
 *         server_name = server_type ( + : repl_server, - : repl_agent) +
 *                       server_name +
 *                       release_string +
 *                       env_name +   ($CUBRID path)
 *                       pid_string   (process id)
 */
static char *
repl_pack_server_name (bool serveryn, const char *server_name,
		       int *name_length)
{
  char *packed_name = NULL;
  const char *env_name = NULL;
  char pid_string[10];
  int n_len, r_len, e_len, p_len;

  if (server_name != NULL)
    {
      env_name = envvar_root ();

      /* here we changed the 2nd string in packed_name from
       * rel_release_string() to rel_major_release_string()
       * solely for the purpose of matching the name of the CUBRID driver.
       */

      sprintf (pid_string, "%d", getpid ());
      n_len = strlen (server_name) + 1;
      r_len = strlen (rel_major_release_string ()) + 1;
      e_len = strlen (env_name) + 1;
      p_len = strlen (pid_string) + 1;
      *name_length = n_len + r_len + e_len + p_len + 5;

      packed_name = malloc (*name_length);
      if (serveryn == true)
	packed_name[0] = '+';
      else
	packed_name[0] = '&';
      memcpy (packed_name + 1, server_name, n_len);
      memcpy (packed_name + 1 + n_len, rel_major_release_string (), r_len);
      memcpy (packed_name + 1 + n_len + r_len, env_name, e_len);
      memcpy (packed_name + 1 + n_len + r_len + e_len, pid_string, p_len);
    }
  return (packed_name);
}

/*
 * repl_set_socket_tcp_nodelay ()
 */
int
repl_set_socket_tcp_nodelay (int sock_fd)
{
  struct protoent *proto;
  int tcp_level;
  int one = 1;

  proto = getprotobyname ("TCP");
  if (proto == NULL)
    {
      tcp_level = IPPROTO_TCP;
    }
  else
    {
      tcp_level = proto->p_proto;
    }
  return setsockopt (sock_fd, tcp_level, TCP_NODELAY, (char *) &one,
		     sizeof (int));
}

/*
 * repl_connect_to_master() - connect to the master server
 *   return: error code
 *   serveryn(in) : true if repl_server
 *   server_name(in): server name
 */
int
repl_connect_to_master (bool serveryn, const char *server_name)
{
  CSS_CONN_ENTRY *conn;
  int error = NO_ERROR;
  char *packed_name;
  int name_length = 0;

  packed_name = repl_pack_server_name (serveryn, server_name, &name_length);
  conn = css_connect_to_master_server (PRM_TCP_PORT_ID, packed_name,
				       name_length);
  if (conn == NULL)
    return REPL_COMMON_ERROR;

  repl_Pipe_to_master = conn->fd;

  free_and_init (packed_name);
  return error;
}

/*
 * repl_start_daemon() - make a daemon process
 *   return: 0 if success, -1 if error
 */
int
repl_start_daemon (void)
{
  int childpid, fd;
#if defined (HAVE_GETRLIMIT)
  struct rlimit rlp;
#endif /* HAVE_GETRLIMIT */
  int fd_max;
  int ppid = getpid ();

  if (getppid () == 1)
    goto out;

  if ((childpid = fork ()) < 0)
    {
      return ER_FAILED;
    }
  else if (childpid > 0)
    {
      exit (0);			/* parent goes bye-bye */
    }
  else
    {
      /*
       * Wait until the parent process has finished. Coded with polling since
       * the parent should finish immediately. SO, it is unlikely that we are
       * going to loop at all.
       */
      while (getppid () == ppid)
	{
	  sleep (1);
	}
    }

  setsid ();

out:

#if 0
  /*
   * Close unneeded file descriptors which prevent the daemon from holding
   * open any descriptors that it may have inherited from its parent which
   * could be a shell. For now, leave in/out/err open
   */

  fd_max = css_get_max_socket_fds();
  for (fd = 3; fd < fd_max; fd++)
    {
      close (fd);
    }
#endif

  /*
   * The file mode creation mask that is inherited could be set to deny
   * certain permissions. Therefore, clear the file mode creation mask.
   */

  umask (0);

  return (0);
}

/*
 * repl_signale_process() - process signals
 *   return: none
 *   routine  : signal handler
 */
void
repl_signal_process (SIGNAL_HANDLER_FUNCTION routine)
{
#if defined(WINDOWS)
  (void) os_set_signal_handler (SIGABRT, routine);
  (void) os_set_signal_handler (SIGINT, routine);
  (void) os_set_signal_handler (SIGTERM, routine);
#else /* ! WINDOWS */
  (void) os_set_signal_handler (SIGSTOP, routine);
  (void) os_set_signal_handler (SIGTERM, routine);
  (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
#endif /* ! WINDOWS */
}

/*
 * repl_error_push() - push the error code
 *   return: none
 *   file_id(in)
 *   line_num(in)
 *   code(in)
 *   arg(in)
 */
void
repl_error_push (int file_id, int line_num, int code, char *arg)
{
  REPL_ERR *temp = (REPL_ERR *) malloc (DB_SIZEOF (REPL_ERR));
  if (temp == NULL)
    return;

  PTHREAD_MUTEX_LOCK (error_Mutex);

  if (err_Head == NULL)
    {
      temp->next = NULL;
      err_Head = temp;
    }
  else
    {
      temp->next = err_Head;
      err_Head = temp;
    }

  /* internal error code generation :
   * eg: line = 34502 --> file id = 2, linenum = 345
   *     line = 123611 --> file id = 11, linenum = 1236
   */

  /* For repl_server :
   *     repl_server.c   - 10
   *     repl_svr_tp.c   - 11
   *     repl_svr_sock.c - 12
   * For repl_server :
   *     repl_agent.c    - 20
   *     repl_ag_tp.c    - 21
   *     repl_ag_sock.c  - 22
   * For common:
   *     repl_support.c     - 1
   */
  err_Head->line = line_num * 100 + file_id;
  /* escalate the previous error code, if input code is less than 1 */
  if (code < 1 && err_Head->next)
    err_Head->code = err_Head->next->code;
  else
    err_Head->code = code;

  if (arg)
    {
      strncpy (err_Head->arg, arg, sizeof (err_Head->arg));
    }
  else
    {
      err_Head->arg[0] = '\0';
    }

  PTHREAD_MUTEX_UNLOCK (error_Mutex);
}

/*
 * repl_error_flush() - flush the error message
 *   return: none
 *   fp(in)
 *   serveryn(in)
 */
void
repl_error_flush (FILE * fp, bool serveryn)
{
  bool is_first = true;
  REPL_ERR *temp;
  REPL_ERR final_error;
  time_t er_time;
  struct tm er_tm;
  struct tm *er_tm_p = &er_tm;
  char time_array[256];

  PTHREAD_MUTEX_LOCK (error_Mutex);

  memset (&final_error, 0, DB_SIZEOF (REPL_ERR));
  if (err_Head)
    {
      er_time = time (NULL);
      er_tm_p = localtime (&er_time);
      strftime (time_array, 256, "%c", er_tm_p);
      fprintf (fp, "[%s] :  ", time_array);
    }

  while (err_Head)
    {
      if (is_first == true)
	{			/* final messgae */
	  final_error.code = err_Head->code;
	  if (err_Head->arg[0] != '\0')
	    strncpy (final_error.arg, err_Head->arg,
		     sizeof (final_error.arg));
	  final_error.line = err_Head->line;
	  is_first = false;
	}
      if (debug_Dump_info & REPL_DEBUG_ERROR_DETAIL)
	{
	  if (err_Head)
	    {			/* first message */
	      if (err_Head->arg[0] != '\0')
		fprintf (fp, msgcat_message (MSGCAT_CATALOG_UTILS,
					     serveryn ?
					     MSGCAT_UTIL_SET_REPLSERVER :
					     MSGCAT_UTIL_SET_REPLAGENT,
					     err_Head->code), err_Head->line,
			 err_Head->arg);
	      else
		fprintf (fp, msgcat_message (MSGCAT_CATALOG_UTILS,
					     serveryn ?
					     MSGCAT_UTIL_SET_REPLSERVER :
					     MSGCAT_UTIL_SET_REPLAGENT,
					     err_Head->code), err_Head->line);
	      if (serveryn == false && er_errid () < 0)
		fprintf (fp, ", MSGCODE From slavedb: %d", er_errid ());
	      fprintf (fp, "\n");
	    }
	}
      temp = err_Head;
      err_Head = err_Head->next;
      free_and_init (temp);
    }

  if (is_first == false && final_error.code != 0)
    {
      if (final_error.arg[0] != '\0')
	fprintf (fp, msgcat_message (MSGCAT_CATALOG_UTILS,
				     serveryn ? MSGCAT_UTIL_SET_REPLSERVER
				     : MSGCAT_UTIL_SET_REPLAGENT,
				     final_error.code),
		 final_error.line, final_error.arg);
      else
	fprintf (fp, msgcat_message (MSGCAT_CATALOG_UTILS,
				     serveryn ? MSGCAT_UTIL_SET_REPLSERVER
				     : MSGCAT_UTIL_SET_REPLAGENT,
				     final_error.code), final_error.line);
      fprintf (fp, "\n");
    }

  fflush (fp);
  err_Head = NULL;

  PTHREAD_MUTEX_UNLOCK (error_Mutex);
}
