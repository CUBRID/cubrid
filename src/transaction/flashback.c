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

#include "config.h"

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
#include "log_manager.h"
#include "message_catalog.h"
#include "msgcat_set_log.hpp"
#include "memory_alloc.h"
#include "intl_support.h"
#include "log_record.hpp"
#include "heap_file.h"
#include "thread_entry.hpp"
#include "oid.h"
#include "storage_common.h"

static int
flashback_create_summary_entry (THREAD_ENTRY * thread_p, FLASHBACK_SUMMARY_ENTRY ** summary)
{
  *summary = (FLASHBACK_SUMMARY_ENTRY *) db_private_alloc (thread_p, sizeof (FLASHBACK_SUMMARY_ENTRY));
  if (*summary == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (FLASHBACK_SUMMARY_ENTRY));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  (*summary)->trid = NULL_TRANID;
  (*summary)->start_time = -1;
  (*summary)->end_time = -1;
  (*summary)->num_insert = 0;
  (*summary)->num_update = 0;
  (*summary)->num_delete = 0;
  (*summary)->start_lsa = LSA_INITIALIZER;
  (*summary)->end_lsa = LSA_INITIALIZER;
  (*summary)->num_class = 0;
  (*summary)->classlist[0] = OID_INITIALIZER;

  return NO_ERROR;
}

static void
flashback_drop_summary_entry (THREAD_ENTRY * thread_p, FLASHBACK_SUMMARY_ENTRY ** summary)
{
  assert (*summary != NULL);

  db_private_free_and_init (thread_p, *summary);
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

  for (i = 0; i < summary_entry->num_class; i++)
    {
      if (OID_EQ (&summary_entry->classlist[i], &classoid))
	{
	  found = true;
	  break;
	}
    }

  if (!found)
    {
      summary_entry->num_class += 1;
      COPY_OID (&summary_entry->classlist[i], &classoid);
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
 * flashback_cleanup - dealloc memory of summary entries iterating summary list
 *
 * thread_p (in)         : thread entry
 * context (in/out)      : flashback summary context that contains summary list
 */

void
flashback_cleanup (THREAD_ENTRY * thread_p, FLASHBACK_SUMMARY_CONTEXT * context)
{
  /* *INDENT-OFF* */
  for (auto iter:context->summary_list)
    {
      if (iter.second != NULL)
        {
	  db_private_free_and_init (thread_p, iter.second);
        }
    }
  /* *INDENT-ON* */
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

  FLASHBACK_SUMMARY_ENTRY *summary_entry = NULL;

  int error = NO_ERROR;

  LOG_RECTYPE log_type;

  char *supplement_data = NULL;

  log_page_p = (LOG_PAGE *) PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  LSA_COPY (&process_lsa, &(context->end_lsa));

  /*fetch log page */
  if (logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_page_p) != NO_ERROR)
    {
      goto exit;
    }

begin:

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
	  summary_entry->start_time = current_time - 1;	// current time is obtained before accessing this log record.
	  LSA_COPY (&summary_entry->start_lsa, &cur_log_rec_lsa);
	}
    }

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*log_rec_header), &process_lsa, log_page_p);

  switch (log_type)
    {
    case LOG_COMMIT:
      {
	LOG_REC_DONETIME *donetime;

	if (context->num_summary == FLASHBACK_MAX_SUMMARY)
	  {
	    error = ER_FLASHBACK_TOO_MANY_SUMMARY;
	    goto exit;
	  }

	LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*donetime), &process_lsa, log_page_p);

	donetime = (LOG_REC_DONETIME *) (log_page_p->area + process_lsa.offset);

	/* create summary info entry, and register it to the summary list */
	if ((error = flashback_create_summary_entry (thread_p, &summary_entry)) != NO_ERROR)
	  {
	    goto exit;
	  }

	summary_entry->trid = trid;
	LSA_COPY (&summary_entry->end_lsa, &cur_log_rec_lsa);
	summary_entry->end_time = donetime->at_time;

        // *INDENT-OFF*
	context->summary_list.insert (std::make_pair (trid, summary_entry));
        // *INDENT-ON*
	current_time = donetime->at_time;
	context->num_summary += 1;
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

	LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*supplement), &process_lsa, log_page_p);

	supplement = (LOG_REC_SUPPLEMENT *) (log_page_p->area + process_lsa.offset);
	supplement_length = supplement->length;
	rec_type = supplement->rec_type;

	LOG_READ_ADD_ALIGN (thread_p, sizeof (*supplement), &process_lsa, log_page_p);

	if (cdc_get_undo_record (thread_p, log_page_p, cur_log_rec_lsa, &supp_recdes) != S_SUCCESS)
	  {
	    error = ER_FAILED;
	    goto exit;
	  }

	supplement_length = sizeof (supp_recdes.type) + supp_recdes.length;
	supplement_data = (char *) db_private_alloc (thread_p, supplement_length);
	if (supplement_data == NULL)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, supplement_length);
	    error = ER_OUT_OF_VIRTUAL_MEMORY;
	    goto exit;
	  }

	CDC_MAKE_SUPPLEMENT_DATA (supplement_data, supp_recdes);

	free_and_init (supp_recdes.data);

	switch (rec_type)
	  {
	  case LOG_SUPPLEMENT_TRAN_USER:
	    {
	      FLASHBACK_CHECK_AND_GET_SUMMARY (context->summary_list, trid, summary_entry);

	      /* user filtering */
	      if (strlen (context->user) != 0
		  && intl_identifier_ncasecmp (context->user, supplement_data, supplement_length) != 0)
		{
		  if (summary_entry != NULL)
		    {
		      flashback_drop_summary_entry (thread_p, &summary_entry);
                      // *INDENT-OFF*
		      context->summary_list.erase (trid);
                      // *INDENT-ON*
		      context->num_summary -= 1;
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
	      bool found = false;

	      memcpy (&classoid, supplement_data, sizeof (OID));

	      /* Check if it is extraction target */
	      for (int i = 0; i < context->num_class; i++)
		{
		  if (OID_EQ (&context->classlist[i], &classoid))
		    {
		      found = true;
		      break;
		    }
		}

	      if (!found)
		{
		  break;
		}

	      FLASHBACK_CHECK_AND_GET_SUMMARY (context->summary_list, trid, summary_entry);
	      if (summary_entry != NULL)
		{
		  flashback_fill_dml_summary (summary_entry, classoid, rec_type);
		}

	      break;
	    }
	  default:
	    break;
	  }
      }
    default:
      break;
    }

  if (LSA_LT (&next_log_rec_lsa, &context->start_lsa))
    {
      /* parse done */
      error = NO_ERROR;
      goto exit;
    }

  LSA_COPY (&process_lsa, &next_log_rec_lsa);

  if (process_lsa.pageid != log_page_p->hdr.logical_pageid)
    {
      if (logpb_fetch_page (thread_p, &process_lsa, LOG_CS_SAFE_READER, log_page_p) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto exit;
	}
    }

  if (supplement_data != NULL)
    {
      db_private_free_and_init (thread_p, supplement_data);
    }

  goto begin;

exit:
  if (supplement_data != NULL)
    {
      db_private_free_and_init (thread_p, supplement_data);
    }

  return error;
}
