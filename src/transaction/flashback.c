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

/*
 * flashback.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#if defined(SOLARIS)
#include <netdb.h>
#endif /* SOLARIS */
#include <sys/stat.h>
#include <assert.h>
#if defined(WINDOWS)
#include <io.h>
#endif /* WINDOWS */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cstdint>

#include "flashback.h"

#include "log_impl.h"
#include "message_catalog.h"
#include "msgcat_set_log.hpp"
#include "memory_alloc.h"
#include "intl_support.h"
#include "log_record.hpp"
#include "heap_file.h"
#include "thread_entry.hpp"
#include "storage_common.h"
#include "system_parameter.h"
#include "object_representation.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static volatile LOG_PAGEID flashback_Min_log_pageid = NULL_LOG_PAGEID;	// Minumun log pageid to keep archive log volume from being removed

static CSS_CONN_ENTRY *flashback_Current_conn = NULL;	// the connection entry for a flashback request

static pthread_mutex_t flashback_Conn_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * flashback_is_duplicated_request - check if the caller is duplicated request for flashback
 *
 * need_reset (in) : if connection is lost and need_reset is true, then it reset the flashback variables
 *
 * return   : duplicated or not
 */

static bool
flashback_is_in_progress ()
{
  /* flashback_Current_conn indicates conn_entry in thread_p, and conn_entry can be reused by request handler.
   * So, status in flashback_Current_conn can be overwritten. */

  if (flashback_Current_conn == NULL)
    {
      /* previous flashback set flashback_Current_conn to NULL properly. (exited well) */
      return false;
    }
  else
    {
      if (flashback_Current_conn->in_flashback == true)
	{
	  /* previous flashback is still in progress */
	  return true;
	}
      else
	{
	  /* - flashback_Current_conn is overwritten with new connection, so in_flashback value is initialized to false.
	   * - previous flashback connection has been exited abnormally. */
	  return false;
	}
    }
}

/*
 * flashback_initialize - check request if is duplicated, 
 *                        then initialize variables and connection when flashback request is started
 */

int
flashback_initialize (THREAD_ENTRY * thread_p)
{
  /* If multiple requests come at the same time,
   * they all can be treated as non-duplicate requests.
   * So, latch for check and set flashback connection is required */

  pthread_mutex_lock (&flashback_Conn_lock);

  if (flashback_is_in_progress ())
    {
      pthread_mutex_unlock (&flashback_Conn_lock);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FLASHBACK_DUPLICATED_REQUEST, 0);
      return ER_FLASHBACK_DUPLICATED_REQUEST;
    }

  if (flashback_Current_conn != NULL)
    {
      flashback_reset ();
    }

  flashback_Current_conn = thread_p->conn_entry;
  flashback_Current_conn->in_flashback = true;

  pthread_mutex_unlock (&flashback_Conn_lock);

  flashback_Min_log_pageid = NULL_LOG_PAGEID;

  return NO_ERROR;
}

/*
 * flashback_set_min_log_pageid_to_keep - set flashback_Min_log_pageid
 */

void
flashback_set_min_log_pageid_to_keep (LOG_LSA * lsa)
{
  assert (lsa != NULL);

  flashback_Min_log_pageid = lsa->pageid;
}

/*
 * flashback_min_log_pageid_to_keep - returns minimum log pageid to keep
 *
 * return   : minimum pageid that flashback have to keep
 */

LOG_PAGEID
flashback_min_log_pageid_to_keep ()
{
  return flashback_Min_log_pageid;
}

/*
 * flashback_is_needed_to_keep_archive - check if archive log volume is required to be kept
 *
 * return   : true or false
 */

bool
flashback_is_needed_to_keep_archive ()
{
  bool is_needed = false;

  if (flashback_is_in_progress ())
    {
      is_needed = true;
    }
  else
    {
      is_needed = false;
    }

  return is_needed;
}

/*
 * flashback_reset - reset flashback global variables
 *
 */

void
flashback_reset ()
{
  flashback_Min_log_pageid = NULL_LOG_PAGEID;

  flashback_Current_conn->in_flashback = false;
  flashback_Current_conn = NULL;
}

/*
 * flashback_is_loginfo_generation_finished - check if it is finished to generate log infos
 *
 * return   : true or false
 */

bool
flashback_is_loginfo_generation_finished (LOG_LSA * start_lsa, LOG_LSA * end_lsa)
{
  /* The way to know whether log info generation is done depends on the direction of traversing the log record
   * forward : start_lsa increases as much as log infos are generated for every request.
   *           So, when start_lsa becomes the same as end_lsa, it is judged to be done.
   * backward : end_lsa decreases as much as log infos are generated for every request.
   *            So, when end_lsa meets the prev_tranlsa == NULL, then it is judged to be done.
   */

  return LSA_ISNULL (end_lsa) || LSA_GE (start_lsa, end_lsa);
}

/*
 * flashback_fill_dml_summary - fill the dml information into summary entry
 *
 * thread_p (in)             : thread entry
 * summary_entry (in/out)    : flashback summary entry
 * classoid (in)             : OID of DML target class
 * rec_type(in)              : record type that can be classified as DML types
 */

static void
flashback_fill_dml_summary (FLASHBACK_SUMMARY_ENTRY * summary_entry, OID classoid, SUPPLEMENT_REC_TYPE rec_type)
{
  assert (summary_entry != NULL);

  bool found = false;
  int i = 0;

  /* fill in the summary_entry info entry
   * 1. number of class and put the classoid into the classlist
   * 2. number of DML operation */

  if (summary_entry->classoid_set.find (classoid) == summary_entry->classoid_set.end ())
    {
      summary_entry->classoid_set.emplace (classoid);
    }

  switch (rec_type)
    {
    case LOG_SUPPLEMENT_INSERT:
    case LOG_SUPPLEMENT_TRIGGER_INSERT:
      summary_entry->num_insert++;
      break;
    case LOG_SUPPLEMENT_UPDATE:
    case LOG_SUPPLEMENT_TRIGGER_UPDATE:
      summary_entry->num_update++;
      break;
    case LOG_SUPPLEMENT_DELETE:
    case LOG_SUPPLEMENT_TRIGGER_DELETE:
      summary_entry->num_delete++;
      break;
    default:
      assert (false);
      break;
    }

  return;
}

/*
 * flashback_make_summary - read log volumes within the range of start_lsa and end_lsa then make summary information
 *
 * return                : error code
 * thread_p (in)         : thread entry
 * context (in/out)      : flashback summary context that contains range of travering logs, filter (user, class), and summary list
 */

int
flashback_make_summary_list (THREAD_ENTRY * thread_p, FLASHBACK_SUMMARY_CONTEXT * context)
{
  LOG_PAGE *log_page_p = NULL;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  LOG_LSA cur_log_rec_lsa = LSA_INITIALIZER;
  LOG_LSA next_log_rec_lsa = LSA_INITIALIZER;
  LOG_LSA process_lsa = LSA_INITIALIZER;

  LOG_RECORD_HEADER *log_rec_header;

  int trid;

  time_t current_time = 0;

  FLASHBACK_SUMMARY_ENTRY *summary_entry;
  bool found = false;

  int error = NO_ERROR;

  LOG_RECTYPE log_type;

  char *supplement_data = NULL;
  int supplement_alloc_length = 0;

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  assert (!LSA_ISNULL (&context->end_lsa));

  LSA_COPY (&process_lsa, &(context->end_lsa));

  /*fetch log page */
  error = logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_page_p);
  if (error != NO_ERROR)
    {
      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "flashback_make_summary_list");
      goto exit;
    }

  while (LSA_GE (&process_lsa, &context->start_lsa))
    {
      LSA_COPY (&cur_log_rec_lsa, &process_lsa);
      log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_page_p, &process_lsa);

      log_type = log_rec_header->type;
      trid = log_rec_header->trid;

      LSA_COPY (&next_log_rec_lsa, &log_rec_header->back_lsa);

      if (LSA_ISNULL (&log_rec_header->prev_tranlsa))
	{
	  FLASHBACK_CHECK_AND_GET_SUMMARY (context->summary_list, trid, summary_entry);

	  if (summary_entry != NULL)
	    {
	      /* current time is updated whenever a log record with a time value is encountered.
	       * this function reads the log records in reverse time order,
	       * so the current_time represents a time in the future than the log being read now
	       * Since LOG_DUMMY_HA_SERVER_STATE is logged every second, start_time is set to 1 second before the current_time */

	      summary_entry->start_time = current_time - 1;
	      LSA_COPY (&summary_entry->start_lsa, &cur_log_rec_lsa);
	    }
	}

      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_header), &process_lsa, log_page_p);

      switch (log_type)
	{
	case LOG_COMMIT:
	  {
	    LOG_REC_DONETIME *donetime;
	    FLASHBACK_SUMMARY_ENTRY tmp_summary_entry = { -1, "\0", 0, 0, 0, 0, 0, LSA_INITIALIZER, LSA_INITIALIZER, };

	    if (context->num_summary == FLASHBACK_MAX_NUM_TRAN_TO_SUMMARY)
	      {
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FLASHBACK_EXCEED_MAX_NUM_TRAN_TO_SUMMARY, 1,
			FLASHBACK_MAX_NUM_TRAN_TO_SUMMARY);
		error = ER_FLASHBACK_EXCEED_MAX_NUM_TRAN_TO_SUMMARY;
		goto exit;
	      }

	    LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*donetime), &process_lsa, log_page_p);

	    donetime = (LOG_REC_DONETIME *) (log_page_p->area + process_lsa.offset);

	    tmp_summary_entry.trid = trid;
	    LSA_COPY (&tmp_summary_entry.end_lsa, &cur_log_rec_lsa);
	    tmp_summary_entry.end_time = donetime->at_time;

	    context->summary_list.emplace (trid, tmp_summary_entry);
	    current_time = donetime->at_time;
	    context->num_summary++;
	    break;
	  }
	case LOG_DUMMY_HA_SERVER_STATE:
	  {
	    LOG_REC_HA_SERVER_STATE *ha_dummy;

	    LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*ha_dummy), &process_lsa, log_page_p);

	    ha_dummy = (LOG_REC_HA_SERVER_STATE *) (log_page_p->area + process_lsa.offset);

	    current_time = ha_dummy->at_time;

	    break;
	  }
	case LOG_SUPPLEMENTAL_INFO:
	  {
	    LOG_REC_SUPPLEMENT *supplement;
	    int supplement_length;
	    SUPPLEMENT_REC_TYPE rec_type;
	    RECDES supp_recdes = RECDES_INITIALIZER;

	    OID classoid;

	    FLASHBACK_CHECK_AND_GET_SUMMARY (context->summary_list, trid, summary_entry);
	    if (summary_entry == NULL)
	      {
		break;
	      }

	    LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*supplement), &process_lsa, log_page_p);

	    supplement = (LOG_REC_SUPPLEMENT *) (log_page_p->area + process_lsa.offset);
	    supplement_length = supplement->length;
	    rec_type = supplement->rec_type;

	    LOG_READ_ADD_ALIGN (thread_p, sizeof (*supplement), &process_lsa, log_page_p);

	    if (cdc_get_undo_record (thread_p, log_page_p, cur_log_rec_lsa, &supp_recdes) != S_SUCCESS)
	      {
		/* er_set */
		error = ER_FAILED;
		goto exit;
	      }

	    supplement_length = sizeof (supp_recdes.type) + supp_recdes.length;
	    if (supplement_length > supplement_alloc_length)
	      {
		char *tmp = (char *) db_private_realloc (thread_p, supplement_data, supplement_length);
		if (tmp == NULL)
		  {
		    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, supplement_length);
		    error = ER_OUT_OF_VIRTUAL_MEMORY;
		    goto exit;
		  }
		else
		  {
		    supplement_data = tmp;
		    supplement_alloc_length = supplement_length;
		  }
	      }

	    CDC_MAKE_SUPPLEMENT_DATA (supplement_data, supp_recdes);

	    free_and_init (supp_recdes.data);

	    switch (rec_type)
	      {
	      case LOG_SUPPLEMENT_TRAN_USER:
		{
		  /* user filtering */
		  if (strlen (context->user) != 0
		      && intl_identifier_ncasecmp (context->user, supplement_data, supplement_length) != 0)
		    {
		      context->summary_list.erase (trid);
		      context->num_summary--;
		    }
		  else
		    {
		      if (strlen (summary_entry->user) == 0)
			{
			  strncpy (summary_entry->user, supplement_data, supplement_length);
			}
		    }

		  break;
		}
	      case LOG_SUPPLEMENT_INSERT:
	      case LOG_SUPPLEMENT_TRIGGER_INSERT:
	      case LOG_SUPPLEMENT_UPDATE:
	      case LOG_SUPPLEMENT_TRIGGER_UPDATE:
	      case LOG_SUPPLEMENT_DELETE:
	      case LOG_SUPPLEMENT_TRIGGER_DELETE:
		{
		  memcpy (&classoid, supplement_data, sizeof (OID));

		  assert (oid_is_system_class (&classoid) == false);

                  // *INDENT-OFF*
		  if (std::find (context->classoids.begin(), context->classoids.end(), classoid) == context->classoids.end())
		    {
		      break;
		    }
                  // *INDENT-ON*

		  flashback_fill_dml_summary (summary_entry, classoid, rec_type);

		  break;
		}
	      default:
		break;
	      }
	  }
	default:
	  break;
	}

      LSA_COPY (&process_lsa, &next_log_rec_lsa);

      if (process_lsa.pageid != log_page_p->hdr.logical_pageid)
	{
	  error = logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_page_p);
	  if (error != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "flashback_make_summary_list");
	      goto exit;
	    }
	}
    }

exit:
  if (supplement_data != NULL)
    {
      db_private_free_and_init (thread_p, supplement_data);
    }

  return error;
}

/*
 * flashback_verify_time () - verify the availablity of log records around the 'start_time' and 'end_time'
 *
 * return           : error_code
 * thread_p (in)    : thread entry
 * start_time (in)  : start point of flashback.
 * end_time (in)    : end point of flashback
 * start_lsa (out)  : LSA near the 'start_time'
 * end_lsa (out)    : LSA near the 'end_time'
 */

int
flashback_verify_time (THREAD_ENTRY * thread_p, time_t * start_time, time_t * end_time, LOG_LSA * start_lsa,
		       LOG_LSA * end_lsa)
{
  int error_code = NO_ERROR;
  time_t ret_time = 0;

  time_t current_time = time (NULL);

  /* 1. Check time value statically */

  if (*start_time > current_time || *end_time <= log_Gl.hdr.db_creation)
    {
      char start_date[20];
      char db_creation_date[20];
      char cur_date[20];

      strftime (start_date, 20, "%d-%m-%Y:%H:%M:%S", localtime (start_time));
      strftime (db_creation_date, 20, "%d-%m-%Y:%H:%M:%S", localtime (&log_Gl.hdr.db_creation));
      strftime (cur_date, 20, "%d-%m-%Y:%H:%M:%S", localtime (&current_time));

      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_FLASHBACK_INVALID_TIME, 3, start_date, db_creation_date,
	      cur_date);

      return ER_FLASHBACK_INVALID_TIME;
    }

  if (*start_time < log_Gl.hdr.db_creation)
    {
      /* If the start time received from the user is earlier than the db_creation,
       * set the start time to log_Gl.hdr.db_creation.
       * Since it was checked above that end_time must be greater than db_creation, even if start_time is set to db_creation,
       * it can be confirmed that start_time is greater than end_time.
       */

      *start_time = log_Gl.hdr.db_creation;
    }

  /* 2. Check if there is log record at start_time */

  ret_time = *start_time;

  error_code = cdc_find_lsa (thread_p, &ret_time, start_lsa);
  if (error_code == NO_ERROR || error_code == ER_CDC_ADJUSTED_LSA)
    {
      /* find log record at the time
       * ret_time : time of the commit record or dummy record that is found to be greater than or equal to the start_time at first */

      if (ret_time >= *end_time)
	{
	  char start_date[20];
	  char db_creation_date[20];

	  strftime (start_date, 20, "%d-%m-%Y:%H:%M:%S", localtime (start_time));
	  strftime (db_creation_date, 20, "%d-%m-%Y:%H:%M:%S", localtime (&log_Gl.hdr.db_creation));

	  /* out of range : start_time (ret_time) can not be greater than end_time */
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_FLASHBACK_INVALID_TIME, 3, start_date,
		  db_creation_date, start_date);

	  return ER_FLASHBACK_INVALID_TIME;
	}
    }
  else
    {
      /* failed to find a log at the time due to failure while reading log page or volume (ER_FAILED, ER_LOG_READ) */
      return error_code;
    }

  *start_time = ret_time;

  /* 3. If start_time is valid, then get end_lsa with end_time */

  error_code = cdc_find_lsa (thread_p, end_time, end_lsa);
  if (!(error_code == NO_ERROR || error_code == ER_CDC_ADJUSTED_LSA))
    {
      /* failed to find a log record */
      return error_code;
    }

  return NO_ERROR;
}

/*
 * flashback_pack_summary_entry ()
 *
 * return        : memory pointer after packing summary entries
 * ptr (in)      : memory pointer where to pack summary entries
 * context (in)  : context which contains summary entry list
 */

char *
flashback_pack_summary_entry (char *ptr, FLASHBACK_SUMMARY_CONTEXT context, int *num_summary)
{
  FLASHBACK_SUMMARY_ENTRY entry;

// *INDENT-OFF*
  for (auto iter : context.summary_list)
    {
      entry = iter.second;
      if (entry.num_insert + entry.num_update + entry.num_delete > 0)
      {
      ptr = or_pack_int (ptr, entry.trid);
      ptr = or_pack_string (ptr, entry.user);
      ptr = or_pack_int64 (ptr, entry.start_time);
      ptr = or_pack_int64 (ptr, entry.end_time);
      ptr = or_pack_int (ptr, entry.num_insert);
      ptr = or_pack_int (ptr, entry.num_update);
      ptr = or_pack_int (ptr, entry.num_delete);
      ptr = or_pack_log_lsa (ptr, &entry.start_lsa);
      ptr = or_pack_log_lsa (ptr, &entry.end_lsa);
      ptr = or_pack_int (ptr, entry.classoid_set.size());

      for (auto item : entry.classoid_set)
        {
          ptr = or_pack_oid (ptr, &item);
        }

      (*num_summary)++;
      }
    }
// *INDENT-ON*

  return ptr;
}


/*
 * flashback_pack_loginfo () -
 *
 * return: memory pointer after putting log info
 *
 *   thread_p(in):
 *   ptr(in): memory pointer where to pack sequence of log info
 *   context(in): context contains log infos to pack
 *
 */

char *
flashback_pack_loginfo (THREAD_ENTRY * thread_p, char *ptr, FLASHBACK_LOGINFO_CONTEXT context)
{
  CDC_LOGINFO_ENTRY *entry;

  for (int i = 0; i < context.num_loginfo; i++)
    {
      ptr = PTR_ALIGN (ptr, MAX_ALIGNMENT);
      // *INDENT-OFF*
      entry = context.loginfo_queue.front ();
      // *INDENT-ON*
      memcpy (ptr, PTR_ALIGN (entry->log_info, MAX_ALIGNMENT), entry->length);

      ptr = ptr + entry->length;

      free_and_init (entry->log_info);
      db_private_free_and_init (thread_p, entry);

      // *INDENT-OFF*
      context.loginfo_queue.pop ();
      // *INDENT-ON*
    }

  return ptr;
}

static int
flashback_find_start_lsa (THREAD_ENTRY * thread_p, FLASHBACK_LOGINFO_CONTEXT * context)
{
  LOG_PAGE *log_page_p = NULL;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  int error = NO_ERROR;

  LOG_LSA process_lsa = LSA_INITIALIZER;
  LOG_LSA cur_log_rec_lsa = LSA_INITIALIZER;

  LOG_RECORD_HEADER *log_rec_header;

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  log_page_p->hdr.logical_pageid = NULL_LOG_PAGEID;

  assert (!LSA_ISNULL (&context->end_lsa));
  LSA_COPY (&process_lsa, &context->end_lsa);

  while (!LSA_ISNULL (&process_lsa))
    {
      /* fetch page from archive or active, if previous page is needed */
      if (log_page_p->hdr.logical_pageid != process_lsa.pageid)
	{
	  if (logpb_is_page_in_archive (process_lsa.pageid))
	    {
	      LOG_CS_ENTER_READ_MODE (thread_p);

	      if (logpb_fetch_from_archive (thread_p, process_lsa.pageid, log_page_p, 0, NULL, false) == NULL)
		{
		  /* archive log volume has been removed */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FLASHBACK_LOG_NOT_EXIST, 2, LSA_AS_ARGS (&process_lsa));
		  error = ER_FLASHBACK_LOG_NOT_EXIST;

		  LOG_CS_EXIT (thread_p);
		  goto error;
		}

	      LOG_CS_EXIT (thread_p);
	    }
	  else
	    {
	      error = logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_page_p);
	      if (error != NO_ERROR)
		{
		  logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "flashback_find_start_lsa");
		  goto error;
		}
	    }
	}

      log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_page_p, &process_lsa);

      LSA_COPY (&cur_log_rec_lsa, &process_lsa);
      LSA_COPY (&process_lsa, &log_rec_header->prev_tranlsa);
    }

  LSA_COPY (&context->start_lsa, &cur_log_rec_lsa);

  return error;

error:
  return error;
}

int
flashback_make_loginfo (THREAD_ENTRY * thread_p, FLASHBACK_LOGINFO_CONTEXT * context)
{
  LOG_LSA cur_log_rec_lsa = LSA_INITIALIZER;
  LOG_LSA next_log_rec_lsa = LSA_INITIALIZER;
  LOG_LSA process_lsa = LSA_INITIALIZER;

  LOG_PAGE *log_page_p = NULL;
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];

  LOG_RECORD_HEADER *log_rec_header;

  int trid;

  int error = NO_ERROR;

  LOG_RECTYPE log_type;

  char *supplement_data = NULL;
  int supplement_alloc_length = 0;

  RECDES supp_recdes = RECDES_INITIALIZER;
  RECDES undo_recdes = RECDES_INITIALIZER;
  RECDES redo_recdes = RECDES_INITIALIZER;

  int num_loginfo = 0;
  CDC_LOGINFO_ENTRY *log_info_entry = NULL;

  OID classoid;

  if (LSA_ISNULL (&context->start_lsa))
    {
      error = flashback_find_start_lsa (thread_p, context);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      /* if start_lsa was NULL at the caller, flashback_min_log_pageid was not set at the caller */
      flashback_set_min_log_pageid_to_keep (&context->start_lsa);
    }

  if (context->forward)
    {
      LSA_COPY (&process_lsa, &context->start_lsa);
    }
  else
    {
      LSA_COPY (&process_lsa, &context->end_lsa);
    }

  LSA_COPY (&cur_log_rec_lsa, &process_lsa);

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  /* fetch log page */
  error = logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_page_p);
  if (error != NO_ERROR)
    {
      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "flashback_make_loginfo");
      goto error;
    }

  /* backward : if prev_tranlsa is null, then finish the while loop (process_lsa = prev_tranlsa)
   * forward : if forw_lsa is greater than context->end_lsa then quit (process_lsa = forw_lsa)
   * num_loginfo will be generated less or equal than context->num_loginfo */

  while (!LSA_ISNULL (&process_lsa) && (LSA_LE (&process_lsa, &context->end_lsa) || num_loginfo < context->num_loginfo))
    {
      if (log_page_p->hdr.logical_pageid != process_lsa.pageid)
	{
	  error = logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_page_p);
	  if (error != NO_ERROR)
	    {
	      logpb_fatal_error (thread_p, false, ARG_FILE_LINE, "flashback_make_loginfo");
	      goto error;
	    }

	  if (process_lsa.offset == NULL_OFFSET && (process_lsa.offset = log_page_p->hdr.offset) == NULL_OFFSET)
	    {
	      /* can not find the first log record in the page
	       * goto next page, this occurs only when direction is forward */

	      process_lsa.pageid++;
	      continue;
	    }
	}

      log_rec_header = LOG_GET_LOG_RECORD_HEADER (log_page_p, &process_lsa);

      log_type = log_rec_header->type;
      trid = log_rec_header->trid;

      if (context->forward)
	{
	  LSA_COPY (&next_log_rec_lsa, &log_rec_header->forw_lsa);

	  if (LSA_ISNULL (&next_log_rec_lsa) && logpb_is_page_in_archive (cur_log_rec_lsa.pageid))
	    {
	      next_log_rec_lsa.pageid = cur_log_rec_lsa.pageid + 1;
	    }
	}
      else
	{
	  LSA_COPY (&next_log_rec_lsa, &log_rec_header->prev_tranlsa);
	}

      LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_header), &process_lsa, log_page_p);

      if (trid == context->trid && log_type == LOG_SUPPLEMENTAL_INFO)
	{
	  LOG_REC_SUPPLEMENT *supplement;
	  int supplement_length;
	  SUPPLEMENT_REC_TYPE rec_type;

	  LOG_LSA undo_lsa, redo_lsa;

	  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*supplement), &process_lsa, log_page_p);

	  supplement = (LOG_REC_SUPPLEMENT *) (log_page_p->area + process_lsa.offset);
	  supplement_length = supplement->length;
	  rec_type = supplement->rec_type;

	  LOG_READ_ADD_ALIGN (thread_p, sizeof (*supplement), &process_lsa, log_page_p);

	  if (cdc_get_undo_record (thread_p, log_page_p, cur_log_rec_lsa, &supp_recdes) != S_SUCCESS)
	    {
	      /* TODO:
	       * after refactor cdc_get_undo_record, then remove error setting
	       * modify cdc_get_undo_record() to return error, not scan code 
	       * */
	      error = ER_FAILED;
	      goto error;
	    }

	  supplement_length = sizeof (supp_recdes.type) + supp_recdes.length;

	  if (supplement_length > supplement_alloc_length)
	    {
	      char *tmp = (char *) db_private_realloc (thread_p, supplement_data, supplement_length);
	      if (tmp == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, supplement_length);
		  error = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto error;
		}
	      else
		{
		  supplement_data = tmp;
		  supplement_alloc_length = supplement_length;
		}
	    }

	  /* will be replaced with CDC_MAKE_SUPPLEMENT_DATA */
	  memcpy (supplement_data, &supp_recdes.type, sizeof (supp_recdes.type));
	  memcpy (supplement_data + sizeof (supp_recdes.type), supp_recdes.data, supp_recdes.length);

	  free_and_init (supp_recdes.data);

	  switch (rec_type)
	    {
	    case LOG_SUPPLEMENT_INSERT:
	    case LOG_SUPPLEMENT_TRIGGER_INSERT:
	      memcpy (&classoid, supplement_data, sizeof (OID));

	      /* classoid filter */
	      if (context->classoid_set.find (classoid) == context->classoid_set.end ())
		{
		  break;
		}

	      memcpy (&redo_lsa, supplement_data + sizeof (OID), sizeof (LOG_LSA));

	      error = cdc_get_recdes (thread_p, NULL, NULL, &redo_lsa, &redo_recdes, true);
	      if (error != NO_ERROR)
		{
		  goto error;
		}

	      log_info_entry = (CDC_LOGINFO_ENTRY *) db_private_alloc (thread_p, sizeof (CDC_LOGINFO_ENTRY));
	      if (log_info_entry == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (CDC_LOGINFO_ENTRY));
		  error = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto error;
		}

	      error =
		cdc_make_dml_loginfo (thread_p, trid, context->user,
				      rec_type == LOG_SUPPLEMENT_INSERT ? CDC_INSERT : CDC_TRIGGER_INSERT, classoid,
				      NULL, &redo_recdes, log_info_entry, true);

	      if (error != NO_ERROR)
		{
		  goto error;
		}

	      context->loginfo_queue.push (log_info_entry);
	      context->queue_size += log_info_entry->length;
	      num_loginfo++;
	      log_info_entry = NULL;

	      break;
	    case LOG_SUPPLEMENT_UPDATE:
	    case LOG_SUPPLEMENT_TRIGGER_UPDATE:
	      memcpy (&classoid, supplement_data, sizeof (OID));

	      /* classoid filter */
	      if (context->classoid_set.find (classoid) == context->classoid_set.end ())
		{
		  break;
		}

	      memcpy (&undo_lsa, supplement_data + sizeof (OID), sizeof (LOG_LSA));
	      memcpy (&redo_lsa, supplement_data + sizeof (OID) + sizeof (LOG_LSA), sizeof (LOG_LSA));

	      error = cdc_get_recdes (thread_p, &undo_lsa, &undo_recdes, &redo_lsa, &redo_recdes, true);
	      if (error != NO_ERROR)
		{
		  goto error;
		}

	      log_info_entry = (CDC_LOGINFO_ENTRY *) db_private_alloc (thread_p, sizeof (CDC_LOGINFO_ENTRY));
	      if (log_info_entry == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (CDC_LOGINFO_ENTRY));
		  error = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto error;
		}

	      if (undo_recdes.type == REC_ASSIGN_ADDRESS)
		{
		  /* This occurs when series of logs are appended like
		   * INSERT record for reserve OID (REC_ASSIGN_ADDRESS) then UPDATE to some record.
		   * And this is a sequence for INSERT a record with OID reservation.
		   * undo record with REC_ASSIGN_ADDRESS type has no undo image to extract, so this will be treated as INSERT
		   * CUBRID engine used to do INSERT a record like this way,
		   * for instance CREATE a class or INSERT a record by trigger execution */

		  assert (rec_type == LOG_SUPPLEMENT_TRIGGER_UPDATE);

		  error =
		    cdc_make_dml_loginfo (thread_p, trid, context->user, CDC_TRIGGER_INSERT, classoid, NULL,
					  &redo_recdes, log_info_entry, true);
		}
	      else
		{
		  error =
		    cdc_make_dml_loginfo (thread_p, trid, context->user,
					  rec_type == LOG_SUPPLEMENT_UPDATE ? CDC_UPDATE : CDC_TRIGGER_UPDATE, classoid,
					  &undo_recdes, &redo_recdes, log_info_entry, true);
		}

	      if (error != NO_ERROR)
		{
		  goto error;
		}

	      context->loginfo_queue.push (log_info_entry);
	      context->queue_size += log_info_entry->length;
	      num_loginfo++;
	      log_info_entry = NULL;

	      break;
	    case LOG_SUPPLEMENT_DELETE:
	    case LOG_SUPPLEMENT_TRIGGER_DELETE:
	      memcpy (&classoid, supplement_data, sizeof (OID));

	      /* classoid filter */
	      if (context->classoid_set.find (classoid) == context->classoid_set.end ())
		{
		  break;
		}

	      memcpy (&undo_lsa, supplement_data + sizeof (OID), sizeof (LOG_LSA));

	      error = cdc_get_recdes (thread_p, &undo_lsa, &undo_recdes, NULL, NULL, true);
	      if (error != NO_ERROR)
		{
		  goto error;
		}

	      log_info_entry = (CDC_LOGINFO_ENTRY *) db_private_alloc (thread_p, sizeof (CDC_LOGINFO_ENTRY));
	      if (log_info_entry == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (CDC_LOGINFO_ENTRY));
		  error = ER_OUT_OF_VIRTUAL_MEMORY;
		  goto error;
		}

	      error =
		cdc_make_dml_loginfo (thread_p, trid, context->user,
				      rec_type == LOG_SUPPLEMENT_DELETE ? CDC_DELETE : CDC_TRIGGER_DELETE, classoid,
				      &undo_recdes, NULL, log_info_entry, true);

	      if (error != NO_ERROR)
		{
		  goto error;
		}

	      context->loginfo_queue.push (log_info_entry);
	      context->queue_size += log_info_entry->length;
	      num_loginfo++;
	      log_info_entry = NULL;

	      break;
	    default:
	      break;
	    }
	}

      LSA_COPY (&process_lsa, &next_log_rec_lsa);
      LSA_COPY (&cur_log_rec_lsa, &process_lsa);

      if (undo_recdes.data != NULL)
	{
	  free_and_init (undo_recdes.data);
	}

      if (redo_recdes.data != NULL)
	{
	  free_and_init (redo_recdes.data);
	}
    }

  /* end of making log info */

  if (context->forward)
    {
      LSA_COPY (&context->start_lsa, &process_lsa);
    }
  else
    {
      LSA_COPY (&context->end_lsa, &process_lsa);
    }

  context->num_loginfo = num_loginfo;

  if (supplement_data != NULL)
    {
      db_private_free_and_init (thread_p, supplement_data);
    }

  return error;

error:

  if (supplement_data != NULL)
    {
      db_private_free_and_init (thread_p, supplement_data);
    }

  if (undo_recdes.data != NULL)
    {
      free_and_init (undo_recdes.data);
    }

  if (redo_recdes.data != NULL)
    {
      free_and_init (redo_recdes.data);
    }

  if (log_info_entry != NULL)
    {
      db_private_free_and_init (thread_p, log_info_entry);
    }

  if (error == ER_FLASHBACK_SCHEMA_CHANGED)
    {
      COPY_OID (&context->invalid_class, &classoid);
    }

  return error;
}
