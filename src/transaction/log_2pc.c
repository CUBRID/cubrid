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
 * log_2pc.c -
 */

#ident "$Id$"

#include "config.h"

#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

/* The following two are for getpid */
#include <sys/types.h>

#include "log_manager.h"
#include "log_impl.h"
#include "log_comm.h"
#include "lock_manager.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "page_buffer.h"
#include "error_manager.h"
#include "system_parameter.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#include "thread.h"
#endif /* SERVER_MODE */
#if !defined(WINDOWS)
#include "tcp.h"		/* for css_gethostid */
#else /* !WINDOWS */
#include "wintcp.h"
#include "porting.h"
#endif /* !WINDOWS */


#if !defined(SERVER_MODE)
#define	CSS_ENABLE_INTERRUPTS
#endif /* !SERVER_MODE */

/* Variables */
struct log_2pc_global_data
{
  int (*get_participants) (int *particp_id_length, void **block_particps_ids);
  int (*lookup_participant) (void *particp_id, int num_particps, void *block_particps_ids);
  char *(*sprintf_participant) (void *particp_id);
  void (*dump_participants) (FILE * fp, int block_length, void *block_particps_id);
  int (*send_prepare) (int gtrid, int num_particps, void *block_particps_ids);
    bool (*send_commit) (int gtrid, int num_particps, int *particp_indices, void *block_particps_ids);
    bool (*send_abort) (int gtrid, int num_particps, int *particp_indices, void *block_particps_ids, int collect);
};
struct log_2pc_global_data log_2pc_Userfun = { NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static int log_2pc_get_num_participants (int *partid_len, void **block_particps_ids);
#if defined (ENABLE_UNUSED_FUNCTION)
static int log_2pc_lookup_particp (void *look_particp_id, int num_particps, void *block_particps_ids);
#endif
static int log_2pc_make_global_tran_id (TRANID tranid);
static bool log_2pc_check_duplicate_global_tran_id (int gtrid);
static int log_2pc_commit_first_phase (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_2PC_EXECUTE execute_2pc_type,
				       bool * decision);
static TRAN_STATE log_2pc_commit_second_phase (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool * decision);
static void log_2pc_append_start (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_2pc_append_decision (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_RECTYPE decsion);
static LOG_TDES *log_2pc_find_tran_descriptor (int gtrid);
static int log_2pc_attach_client (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_TDES * client_tdes);
#if defined (ENABLE_UNUSED_FUNCTION)
static int log_2pc_broadcast_decision_participant (THREAD_ENTRY * thread_p, LOG_TDES * tdes, int particp_index);
#endif
static void log_2pc_recovery_prepare (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * log_lsa,
				      LOG_PAGE * log_page_p);
static int log_2pc_recovery_start (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
				   int *ack_list, int *ack_count);
static int *log_2pc_expand_ack_list (THREAD_ENTRY * thread_p, int *ack_list, int *ack_count, int *size_ack_list);
static void log_2pc_recovery_recv_ack (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p, int *ack_list,
				       int *ack_count);
static int log_2pc_recovery_analysis_record (THREAD_ENTRY * thread_p, LOG_RECTYPE record_type, LOG_TDES * tdes,
					     LOG_LSA * log_lsa, LOG_PAGE * log_page_p, int **ack_list, int *ack_count,
					     int *size_ack_list, bool * search_2pc_prepare, bool * search_2pc_start);
static void log_2pc_recovery_collecting_participant_votes (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_2pc_recovery_abort_decision (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_2pc_recovery_commit_decision (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_2pc_recovery_committed_informing_participants (THREAD_ENTRY * thread_p, LOG_TDES * tdes);
static void log_2pc_recovery_aborted_informing_participants (THREAD_ENTRY * thread_p, LOG_TDES * tdes);

/*
 *     FUNCTIONS RELATED TO COMMUNICATION BETWEEN THIS MODULE AND YOUR
 *      APPLICATION (I.E, BETWEEN COORDINATOR AND PARTICIPANTS OR VICE VERSA)
 */

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_2pc_define_funs - Define the user 2pc functions
 *
 * return:
 *
 *   get_participants(in):
 *   lookup_participant(in):
 *   sprintf_participant(in):
 *   dump_participants(in):
 *   send_prepare(in):
 *   send_commit(in):
 *   send_abort(in):
 *      functions for 2PC communication (See prototype.. too many to describe
 *                                      here)
 *
 * NOTE:Define the functions used for communication between the
 *              coordinator (this code or your application) and the
 *              participants (your application or this code).
 *              See description blocks of each function for it uses.
 */
void
log_2pc_define_funs (int (*get_participants) (int *particp_id_length, void **block_particps_ids),
		     int (*lookup_participant) (void *particp_id, int num_particps, void *block_particps_ids),
		     char *(*sprintf_participant) (void *particp_id),
		     void (*dump_participants) (FILE * fp, int block_length, void *block_particps_id),
		     int (*send_prepare) (int gtrid, int num_particps, void *block_particps_ids),
		     bool (*send_commit) (int gtrid, int num_particps, int *particp_indices, void *block_particps_ids),
		     bool (*send_abort) (int gtrid, int num_particps, int *particp_indices, void *block_particps_ids,
					 int collect))
{
  log_2pc_Userfun.get_participants = get_participants;
  log_2pc_Userfun.lookup_participant = lookup_participant;
  log_2pc_Userfun.sprintf_participant = sprintf_participant;
  log_2pc_Userfun.dump_participants = dump_participants;
  log_2pc_Userfun.send_prepare = send_prepare;
  log_2pc_Userfun.send_commit = send_commit;
  log_2pc_Userfun.send_abort = send_abort;
}
#endif

/*
 * log_2pc_find_particps - Find all particpants
 *
 * return:  number of participants
 *
 *   partid_len(in): Length of each participant (Set as a side effect)
 *   block_particps_ids(in): An array of particpant ids where each particpant id
 *                        is of length "partid_len" (Set as a side effect)
 *
 * NOTE:Find the participants of the current transaction. If the
 *              the transaction was not distributed one, the number of
 *              participants is set to zero.
 */
static int
log_2pc_get_num_participants (int *partid_len, void **block_particps_ids)
{
  void *block;
  int num_particps;

  if (log_2pc_Userfun.get_participants == NULL)
    {
      partid_len = 0;
      block_particps_ids = NULL;
      return 0;
    }

  num_particps = (*log_2pc_Userfun.get_participants) (partid_len, &block);
  if (num_particps > 0)
    {
      *block_particps_ids = malloc (num_particps * *partid_len);
      if (*block_particps_ids == NULL)
	{
	  /* Failure */
	  return -1;
	}
      memcpy (*block_particps_ids, block, num_particps * *partid_len);
    }
  else
    {
      *block_particps_ids = NULL;
    }

  return num_particps;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_2pc_lookup_particp - LOOK UP FOR A PARTICIPANT
 *
 * return: index
 *
 *   look_particp_id(in): Desired participant identifier
 *   num_particps(in): Number of participants at block_particps_ids
 *   block_particps_ids(in): An array of particpant ids. The length of each
 *                        element should be known by the callee.
 *
 * NOTE:Locate the given participant identifier on the block. If it is
 *              not found (-1) is returned.
 */
static int
log_2pc_lookup_particp (void *look_particp_id, int num_particps, void *block_particps_ids)
{
  if (log_2pc_Userfun.lookup_participant == NULL)
    {
      return -1;
    }

  return (*log_2pc_Userfun.lookup_participant) (look_particp_id, num_particps, block_particps_ids);
}
#endif

/*
 * log_2pc_sprintf_particp - A STRING VERSION OF PARTICIPANT-ID
 *
 * return:
 *
 *   particp_id(in): Desired participant identifier
 *
 * NOTE:Return a string version of the given participant that can be
 *              printed using the quilifier %s of printf.
 */
char *
log_2pc_sprintf_particp (void *particp_id)
{
  if (log_2pc_Userfun.sprintf_participant == NULL)
    {
      return NULL;
    }
  return (*log_2pc_Userfun.sprintf_participant) (particp_id);
}

/*
 * log_2pc_dump_participants - Dump all participants
 *
 * return: nothing..
 *
 *   block_length(in): Length of each participant.
 *   block_particps_ids(in): An array of particpant ids. The length of each
 *                        element should be known by the callee.
 *
 * Note:Print all participants in a readable form. The particpant
 *              length is used to find out the number of participants.
 */
void
log_2pc_dump_participants (FILE * fp, int block_length, void *block_particps_ids)
{
  if (log_2pc_Userfun.dump_participants == NULL)
    {
      return;
    }
  (*log_2pc_Userfun.dump_participants) (fp, block_length, block_particps_ids);
}

/*
 * log_2pc_send_prepare - SEND A PREPARE MESSAGE TO PARTICIPANTS
 *
 * return:
 *
 *   gtrid(in): Global/distributed transaction identifier
 *   num_particps(in): Number of participants at block_particps_ids
 *   block_particps_ids(in): An array of particpant ids. The length of each
 *                        element should be known by the callee.
 *
 * NOTE: Send a prepare to commit message to all participants, then
 *              collects the votes and retruns true if all participants are
 *              willing to commit, otherwise, false is returned.
 *
 *              Currently, our communication subsystem does not provide an
 *              asynchronous capabilities for multicasting. Once this is
 *              provided, the jobs of this function will change. For example,
 *              the collecting of votes will be done through interrupts, and
 *              so on.
 */
bool
log_2pc_send_prepare (int gtrid, int num_particps, void *block_particps_ids)
{
  if (log_2pc_Userfun.send_prepare == NULL)
    {
      return true;
    }

  return (*log_2pc_Userfun.send_prepare) (gtrid, num_particps, block_particps_ids);
}

/*
 * log_2pc_send_commit_decision - SEND A COMMIT TO PARTICIPANTS
 *
 * return:
 *
 *   gtrid(in): Global/distributed transaction identifier
 *   num_particps(in): Number of participants at block_particps_ids
 *   particps_indices(in): Participant indices
 *   block_particps_ids(in): An array of particpant ids. The length of each
 *                        element should be known by the callee.
 *
 * NOTE:Send the commit decision to participants which have not
 *              received the commit decision. This is found by looking to the
 *              particps_indices array. If the ith element of the array is 0,
 *              the ith participant needs to be informed, otherwise, it does
 *              not have to. The particps_indices cannot be NULL. When a
 *              participant acknowledge for receiving the decisions, the
 *              function log_2pc_append_recv_ack must be called indicating the index
 *              number.
 *
 *              Currently, our communication subsystem does not provide an
 *              asynchronous capabilities for multicasting. Once this is
 *              provided, the jobs of this function will change. For example,
 *              the collecting of votes will be done through interrupts, and
 *              so on.
 */
bool
log_2pc_send_commit_decision (int gtrid, int num_particps, int *particps_indices, void *block_particps_ids)
{
  bool result = true;

  if (log_2pc_Userfun.send_commit != NULL)
    {
      result = (*log_2pc_Userfun.send_commit) (gtrid, num_particps, particps_indices, block_particps_ids);
    }

  return result;
}

/*
 * log_2pc_send_abort_decision - SEND AN ABORT TO PARTICIPANTS
 *
 * return:
 *
 *   gtrid(in): Global/distributed transaction identifier
 *   num_particps(in): Number of participants at block_particps_ids
 *   particps_indices(in):  Participant indices
 *   block_particps_ids(in): An array of particpant ids. The length of each
 *                        element should be known by the callee.
 *   collect(in): Wheater or not acks should be collected
 *
 * NOTE:Send the abort decision to participants which have not received
 *              the abort decision and that they were willing to commit. This
 *              is found by looking to the particps_indices array. If the ith
 *              element of the array is 0, the ith participant needs to be
 *              informed, otherwise, it does not have to. If the
 *              particps_indices is NULL, we must sent to all participants.
 *              If collect ack is used to indicate to participants if an ack
 *              is needed. An acks is not needed if the abort was decided
 *              before entering 2PC. When collect is true and when a
 *              participant acknowledge for receiving the decisonis received,
 *              the function log_2pc_append_recv_ack must be called indicating the
 *              index number.
 *
 *              Currently, our communication subsystem does not provide an
 *              asynchronous capabilities for multicasting. Once this is
 *              provided, the jobs of this function will change. For example,
 *              the collecting of votes will be done through interrupts, and
 *              so on.
 */
bool
log_2pc_send_abort_decision (int gtrid, int num_particps, int *particps_indices, void *block_particps_ids, bool collect)
{
  bool result = true;

  if (log_2pc_Userfun.send_abort != NULL)
    {
      result = (*log_2pc_Userfun.send_abort) (gtrid, num_particps, particps_indices, block_particps_ids, collect);
    }

  return result;
}

/*
 *
 *       	      THE REST OF SUPPORTING 2PC FUNCTIONS
 *
 */

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_get_global_tran_id - FIND CURRENT GLOBAL TRANID
 *
 * return: gtrid
 *
 * NOTE:Find current gloabl transaction identifier.
 */
int
log_get_global_tran_id (THREAD_ENTRY * thread_p)
{
  int tran_index;
  LOG_TDES *tdes;		/* Transaction descriptor */
  int gtrid = LOG_2PC_NULL_GTRID;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

  TR_TABLE_CS_ENTER (thread_p);

  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL)
    {
      if (tdes->gtrid == LOG_2PC_NULL_GTRID && log_is_tran_distributed (tdes) == true)
	{
	  tdes->gtrid = log_2pc_make_global_tran_id (tdes->trid);
	}

      gtrid = tdes->gtrid;
    }

  TR_TABLE_CS_EXIT (thread_p);

  return gtrid;
}
#endif

/*
 * log_2pc_make_global_tran_id - Make a global transaction identifier for
 *                       2pc purposes
 *
 * return:
 *
 *   tranid(in): Transaction identifier
 *
 * NOTE:Build a global transaction identifier based on the host
 *              identifier, the process identifier and the transaction
 *              identifier.
 */
static int
log_2pc_make_global_tran_id (TRANID tranid)
{
  unsigned char *ptr;
  unsigned int hash;
  unsigned int value;
  unsigned int unsig_gtrid;
  int gtrid;
  unsigned int i;

  unsig_gtrid = 0;

  /* HASH THE HOST IDENTIFIER INTO ONE BYTE */
  value = css_gethostid ();
  hash = 0;
  ptr = (unsigned char *) &value;
  for (i = 0; i < sizeof (value); i++)
    {
      hash = (hash << 5) - hash + *ptr++;
    }
  /* Don't use more than one byte */
  /* set the MSB to zero */
  unsig_gtrid = unsig_gtrid + ((hash % UCHAR_MAX) & 0x7F);

  /* HASH THE PROCESS IDENTIFIER INTO ONE BYTE */
  value = (unsigned int) getpid ();
  hash = 0;
  ptr = (unsigned char *) &value;
  for (i = 0; i < sizeof (value); i++)
    {
      hash = (hash << 5) - hash + *ptr++;
    }
  /* Don't use more than one byte */
  unsig_gtrid = (unsig_gtrid << 8) + (hash % UCHAR_MAX);


  /* FOLD the TRANSACTION IDENTIFIER INTO TWO */
  hash = *(unsigned short *) &tranid;
  hash = (hash << 5) - hash + *((unsigned short *) &tranid + 1);

  /* Don't use more than two byte */
  unsig_gtrid = (unsig_gtrid << 16) + (hash % SHRT_MAX);


  /* 
   * Make sure that another 2PC transaction does not have this identifier.
   * If there is one, subtract one and check again. Note that the identifier
   * may be duplicated in a remote site, we do not have control about this.
   */

  gtrid = (int) unsig_gtrid;

  value = 1;
  if (gtrid == LOG_2PC_NULL_GTRID)
    {
      gtrid--;
    }

  while (log_2pc_check_duplicate_global_tran_id (gtrid))
    {
      gtrid--;

      if (gtrid == LOG_2PC_NULL_GTRID)
	{
	  gtrid--;
	}
    }

  return (int) gtrid;
}

/*
 * log_2pc_check_duplicate_global_tran_id
 *
 * return:
 *
 *   gtrid(in): Global transaction identifier
 *
 * Note:
 */
static bool
log_2pc_check_duplicate_global_tran_id (int gtrid)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int i;

  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      if (i != LOG_SYSTEM_TRAN_INDEX)
	{
	  tdes = LOG_FIND_TDES (i);
	  if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->gtrid != LOG_2PC_NULL_GTRID && gtrid == tdes->gtrid)
	    {
	      return true;
	    }
	}
    }

  return false;
}

/*
 * log_2pc_commit_first_phase
 *
 * return:
 *
 *   tdes(in):
 *
 * Note:
 */
static int
log_2pc_commit_first_phase (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_2PC_EXECUTE execute_2pc_type, bool * decision)
{
  int i;

  /* Start the first phase of 2PC. Prepare to commit or voting phase */
  if (tdes->state == TRAN_ACTIVE)
    {
      /* 
       * Start prepare to commit at coordinator site
       * Note: that the transient classnames table entries should have
       * already been taken care of.
       */

      /* 
       * Release share locks types and record that the 2PC has started.
       * NOTE: that log_2pc_append_start flushes the log and change the state
       *       of the transaction 2PC collecting votes
       */

      /* 
       * Start the 2PC for this coordinator
       */
      log_2pc_append_start (thread_p, tdes);

      if (execute_2pc_type == LOG_2PC_EXECUTE_FULL)
	{
	  /* 
	   * This is the coordinatoor root
	   */
	  lock_unlock_all_shared_get_all_exclusive (thread_p, NULL);
	}

      /* Initialize the Acknowledgement vector to 0 */
      i = sizeof (int) * tdes->coord->num_particps;

      tdes->coord->ack_received = (int *) malloc (i);
      if (tdes->coord->ack_received == NULL)
	{
	  /* Out of memory */
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_commit");
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      for (i = 0; i < tdes->coord->num_particps; i++)
	{
	  tdes->coord->ack_received[i] = false;
	}

      *decision = log_2pc_send_prepare (tdes->gtrid, tdes->coord->num_particps, tdes->coord->block_particps_ids);
    }
  else
    {
      /* 
       * We are not in the right mode for the prepare to commit phase
       */
      *decision = false;

      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * log_2pc_commit_second_phase
 *
 * return:
 *
 *   tdes(in):
 *
 * Note:
 */
static TRAN_STATE
log_2pc_commit_second_phase (THREAD_ENTRY * thread_p, LOG_TDES * tdes, bool * decision)
{
  TRAN_STATE state;

  if (*decision == true)
    {
      /* 
       * DECISION is COMMIT
       * The coordinator and all participants of distributed transaction
       * have agreed to commit the transaction. The commit decision is
       * forced to the log to find out the decision in the case of a crash.
       */

      log_2pc_append_decision (thread_p, tdes, LOG_2PC_COMMIT_DECISION);

      /* 
       * The transaction has been declared as 2PC commit. We could execute the
       * LOCAL COMMIT AND THE REMOTE COMMITS IN PARALLEL, however our
       * communication subsystem does not support asynchronous communication
       * types. The commitment of the participants is done after the local
       * commitment is completed.
       */

      /* Save the state.. so it can be reverted to the 2pc state .. */
      state = tdes->state;
      /* 2PC protocol does not support RETAIN LOCK */
      (void) log_commit_local (thread_p, tdes, false, false);

      tdes->state = state;	/* Revert to 2PC state... */
      /* 
       * If the following function fails, the transaction will be dangling
       * and we need to retry sending the decision at another point.
       * We have already decided and log the decision in the log file.
       */
      (void) log_2pc_send_commit_decision (tdes->gtrid, tdes->coord->num_particps, tdes->coord->ack_received,
					   tdes->coord->block_particps_ids);
      /* Check if all the acknowledgments have been received */
      state = log_complete_for_2pc (thread_p, tdes, LOG_COMMIT, LOG_NEED_NEWTRID);
    }
  else
    {
      /* 
       * DECISION is ABORT
       * The coordinator and/or some of the participants of distributed
       * transaction could not agree to commit the transaction. The abort
       * decision is logged. We do not need to forced since the default is
       * abort. It does not matter whether this is a root coordinator or not
       * the current site has decide to abort.
       */

      /* 
       * If the transaction is active and there are not acknowledgments
       * needed, the abort for the distributed transaction was decided
       * without using the 2PC
       */

      if (tdes->state != TRAN_ACTIVE || tdes->coord->ack_received != NULL)
	{
	  log_2pc_append_decision (thread_p, tdes, LOG_2PC_ABORT_DECISION);
	}

      /* 
       * The transaction has been declared as 2PC abort. We could execute the
       * LOCAL ABORT AND THE REMOTE ABORTS IN PARALLEL, however our
       * communication subsystem does not support asynchronous communication
       * types. The abort of the participants is done after the local abort
       * is completed.
       */

      /* Save the state.. so it can be reverted to the 2pc state .. */
      state = tdes->state;
      /* 2PC protocol does not support RETAIN LOCK */
      (void) log_abort_local (thread_p, tdes, false);

      if (tdes->state == TRAN_UNACTIVE_ABORTED)
	{
	  tdes->state = state;	/* Revert to 2PC state... */
	}
      /* 
       * Execute the abort at participants sites at this time.
       */
      if (tdes->coord->ack_received)
	{
	  /* 
	   * Current site was also a coordinator site (of course not the root
	   * coordinator). Thus, we need to collect acknowledgments.
	   *
	   * If the following function fails, the transaction will be dangling
	   * and we need to retry sending the decision at another point.
	   * We have already decided and log the decision in the log file.
	   */
	  (void) log_2pc_send_abort_decision (tdes->gtrid, tdes->coord->num_particps, tdes->coord->ack_received,
					      tdes->coord->block_particps_ids, true);
	}
      else
	{
	  /* 
	   * Abort was decided without using the 2PC protocol at this site.
	   * That is, the participants are not prepare to commit). Therefore,
	   * there is no need to collect acknowledgments.
	   *
	   * If the following function fails, the transaction will be dangling
	   * and we need to retry sending the decision at another point.
	   * We have already decided and log the decision in the log file.
	   */
	  (void) log_2pc_send_abort_decision (tdes->gtrid, tdes->coord->num_particps, tdes->coord->ack_received,
					      tdes->coord->block_particps_ids, false);
	}
      /* Check if all the acknowledgments have been received */
      state = log_complete_for_2pc (thread_p, tdes, LOG_ABORT, LOG_NEED_NEWTRID);
    }

  return state;
}

/*
 * log_2pc_commit - Follow two phase commit protocol (coordinator's algorithm)
 *
 * return: TRAN_STATE
 *
 *   tdes(in): State structure of transaction of the log record
 *   execute_2pc_type(in): Phase of two phase commit to execute
 *   decision(in/out): Wheater coordinator and its participants agree to
 *                       commit or abort the transaction
 *
 */
TRAN_STATE
log_2pc_commit (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_2PC_EXECUTE execute_2pc_type, bool * decision)
{
  TRAN_STATE state;

  if (tdes->gtrid == LOG_2PC_NULL_GTRID)
    {
      TR_TABLE_CS_ENTER (thread_p);
      tdes->gtrid = log_2pc_make_global_tran_id (tdes->trid);
      TR_TABLE_CS_EXIT (thread_p);
    }

  /* 
   * PHASE I of 2PC: Guarantee commitment (i.e., coordinator and participants
   *                 are Voting)
   */

  /* If the transaction is ready to commit, the first phase of the 2PC has already been completed, so skip it here */

  if (execute_2pc_type == LOG_2PC_EXECUTE_FULL || execute_2pc_type == LOG_2PC_EXECUTE_PREPARE)
    {
      if (log_2pc_commit_first_phase (thread_p, tdes, execute_2pc_type, decision) != NO_ERROR)
	{
	  return tdes->state;
	}
    }
  else
    {
      /* 
       * We are currently not executing the first phase of 2PC. The decsion is
       * already known
       */
      if (execute_2pc_type == LOG_2PC_EXECUTE_COMMIT_DECISION)
	{
	  *decision = true;
	}
      else
	{
	  *decision = false;
	}
    }

  /* 
   * PHASE II of 2PC: Inform decsion to participants (i.e., either commit or
   *                  abort)
   */
  if (execute_2pc_type != LOG_2PC_EXECUTE_PREPARE || *decision == false)
    {
      state = log_2pc_commit_second_phase (thread_p, tdes, decision);
    }
  else
    {
      state = tdes->state;
    }

  return state;
}

/*
 * log_set_global_tran_info - SET GLOBAL TRANSACTION INFORMATION
 *
 * return:
 *
 *   gtrid(in): global transaction identifier
 *   info(in): pointer to the user information to be set
 *   size(in): size of the user information to be set
 *
 * NOTE:Set the user information related with the global transaction.
 *              The global transaction identified by the 'gtrid' should exist
 *              and should be the value returned by 'db_2pc_start_transaction'
 *              You can use this function to set the longer format of global
 *              transaction identifier such as XID of XA interface.
 */
int
log_set_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *info, int size)
{
  LOG_TDES *tdes;
  int i;

  if (gtrid != LOG_2PC_NULL_GTRID)
    {
      TR_TABLE_CS_ENTER (thread_p);

      for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
	{
	  tdes = LOG_FIND_TDES (i);
	  if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->gtrid == gtrid)
	    {
	      /* The transaction is in the middle of the 2PC protocol, we cannot set. */
	      if (LOG_ISTRAN_2PC (tdes))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_SET_GTRINFO, 2, gtrid,
			  log_state_string (tdes->state));

		  TR_TABLE_CS_EXIT (thread_p);
		  return ER_LOG_CANNOT_SET_GTRINFO;
		}

	      /* Set the global transaction information. If already set, overwrite it. */
	      if (tdes->gtrinfo.info_data != NULL)
		{
		  free_and_init (tdes->gtrinfo.info_data);
		}
	      tdes->gtrinfo.info_data = malloc (size);
	      if (tdes->gtrinfo.info_data == NULL)
		{
		  TR_TABLE_CS_EXIT (thread_p);
		  return ER_OUT_OF_VIRTUAL_MEMORY;
		}
	      tdes->gtrinfo.info_length = size;
	      (void) memcpy (tdes->gtrinfo.info_data, info, size);

	      TR_TABLE_CS_EXIT (thread_p);
	      return NO_ERROR;
	    }
	}

      TR_TABLE_CS_EXIT (thread_p);
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_2PC_UNKNOWN_GTID, 1, gtrid);
  return ER_LOG_2PC_UNKNOWN_GTID;
}

/*
 * log_get_global_tran_info - Get global transaction information
 *
 * return: NO_ERROR or error code
 *
 *   gtrid(in): global transaction identifier
 *   buffer(in): pointer to the buffer into which the user information is stored
 *   size(in): size of the buffer
 *
 * NOTE:Get the user information of the global transaction identified
 *              by the 'gtrid'.
 *              You can use this function to get the longer format of global
 *              transaction identifier such as XID of XA interface. This
 *              function is designed to use if you want to get XID after
 *              calling 'db_2pc_prepared_transactions' to support xa_recover()
 */
int
log_get_global_tran_info (THREAD_ENTRY * thread_p, int gtrid, void *buffer, int size)
{
  LOG_TDES *tdes;
  int i;

  assert (buffer != NULL);
  assert (size >= 0);

  if (gtrid != LOG_2PC_NULL_GTRID)
    {

      TR_TABLE_CS_ENTER (thread_p);

      for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
	{
	  tdes = LOG_FIND_TDES (i);
	  if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->gtrid == gtrid)
	    {
	      /* If the global transation information is not set, error. */
	      if (tdes->gtrinfo.info_data == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_CANNOT_SET_GTRINFO, 1, gtrid);

		  TR_TABLE_CS_EXIT (thread_p);
		  return ER_LOG_CANNOT_SET_GTRINFO;
		}

	      /* Copy the global transaction information to the buffer. */
	      if (size > tdes->gtrinfo.info_length)
		{
		  size = tdes->gtrinfo.info_length;
		}
	      (void) memcpy (buffer, tdes->gtrinfo.info_data, size);

	      TR_TABLE_CS_EXIT (thread_p);
	      return NO_ERROR;
	    }
	}

      TR_TABLE_CS_EXIT (thread_p);
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_2PC_UNKNOWN_GTID, 1, gtrid);
  return ER_LOG_2PC_UNKNOWN_GTID;
}

/*
 * log_2pc_start - START TRANSACTION AS A PART OF GLOBAL TRANSACTION
 *
 * return: return global transaction identifier
 *
 * NOTE:Make current transaction as a part of a global transaction by
 *              assigning a global transaction identifier(gtrid).
 *              It is recommended to call this function just after the end of
 *              a transaction(commit or abort) before executing other works.
 *              This function is one way of getting gtrid of the transaction.
 *              The other way is to use 'db_2pc_prepare_to_commit_transaction'
 *              The function 'db_2pc_prepare_transaction' should be used if
 *              this function is called.
 */
int
log_2pc_start (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return LOG_2PC_NULL_GTRID;
    }

  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_2PC_CANNOT_START, 1, log_state_string (tdes->state));
      return LOG_2PC_NULL_GTRID;
    }

  if (tdes->gtrid == LOG_2PC_NULL_GTRID)
    {
      TR_TABLE_CS_ENTER (thread_p);
      tdes->gtrid = log_2pc_make_global_tran_id (tdes->trid);
      TR_TABLE_CS_EXIT (thread_p);
    }

  return tdes->gtrid;
}

/*
 * log_2pc_prepare - PREPARE TRANSACTION TO COMMIT
 *
 * return: TRAN_STATE
 *
 * NOTE: Prepare the current transaction for commitment in 2PC. The
 *              transaction should be made as a part of a global transaction
 *              before by 'db_2pc_start_transaction', a pair one of this
 *              function.
 *              The system promises not to unilaterally abort the transaction.
 *              After this function call, the only API functions that should
 *              be executed are 'db_commit_transaction' &
 *              'db_abort_transaction'.
 */
TRAN_STATE
log_2pc_prepare (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (tdes->gtrid == LOG_2PC_NULL_GTRID)
    {
      /* Transaction is not started by 'log_2pc_start', cannot be prepared. */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_2PC_NOT_STARTED, 1, tdes->trid);
      return tdes->state;
    }

  return log_2pc_prepare_global_tran (thread_p, tdes->gtrid);
}

/*
 * log_2pc_recovery_prepared - OBTAIN LIST OF PREPARED TRANSACTIONS
 *
 * return:  the number of ids copied into 'gtrids[]'
 *
 *   gtrids(in): array into which global transaction identifiers are copied
 *   size(in): size of 'gtrids[]' array
 *
 * NOTE:For restart recovery of global transactions, this function
 *              returns gtrids of transactions in prepared state, which was
 *              a part of a global transaction.
 *              If the return value is less than the 'size', there's no more
 *              transactions to recover.
 */
int
log_2pc_recovery_prepared (THREAD_ENTRY * thread_p, int gtrids[], int size)
{
  LOG_TDES *tdes;
  int i, count = 0;

  assert (size >= 0);

  TR_TABLE_CS_ENTER (thread_p);

  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = LOG_FIND_TDES (i);
      if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->gtrid != LOG_2PC_NULL_GTRID
	  && LOG_ISTRAN_2PC_PREPARE (tdes))
	{
	  if (size <= count)
	    {
	      break;
	    }
	  gtrids[count++] = tdes->gtrid;
	}
    }

  TR_TABLE_CS_EXIT (thread_p);
  return count;
}

/*
 * log_2pc_find_tran_descriptor -
 *
 * return:
 *
 *   gtrid(in): global transaction identifier
 *
 * Note:
 */
static LOG_TDES *
log_2pc_find_tran_descriptor (int gtrid)
{
  LOG_TDES *tdes;
  int i;

  /* 
   * Check if the client_user has a 2PC prepare index. If it does, attach
   * this index to the given user.
   */
  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = LOG_FIND_TDES (i);
      if (tdes != NULL && tdes->trid != NULL_TRANID && tdes->gtrid != LOG_2PC_NULL_GTRID
	  && LOG_ISTRAN_2PC_PREPARE (tdes) && (tdes->gtrid == gtrid))
	{
	  return tdes;
	}
    }

  return NULL;
}

/*
 * log_2pc_attach_client -
 *
 * return:
 *
 *   gtrid(in): global transaction identifier
 *
 * Note:
 */
static int
log_2pc_attach_client (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_TDES * client_tdes)
{
  /* 
   * Abort the current client transaction, then attach to the desired
   * transaction.
   */
  (void) log_abort (thread_p, NULL_TRAN_INDEX);

  /* 
   * Copy the contents of the 2PC transaction descriptor over the
   * client transaction index.
   */
  tdes->isloose_end = false;
  tdes->isolation = client_tdes->isolation;
  tdes->wait_msecs = client_tdes->wait_msecs;
  /* 
   * The client identification remains the same. So there is not a need
   * to set clientids.
   */

  /* Return the table entry that is not going to be used anymore */
  logtb_free_tran_index (thread_p, client_tdes->tran_index);
  LOG_SET_CURRENT_TRAN_INDEX (thread_p, tdes->tran_index);

  /* Reduce the number of loose end transactions by one */
  log_Gl.trantable.num_prepared_loose_end_indices--;

  return NO_ERROR;
}

/*
 * log_2pc_attach_global_tran - Join coordinator to 2pc transaction
 *
 * return:  New transaction index..
 *
 *   gtrid(in): Global transaction identifier
 *
 * NOTE:The current client index is freed and the one with the given
 *              2PC loose end (i.e., transaction waiting for decision)
 *              transaction is returned. The old client transaction is aborted
 *              before the attachement, the old client transaction must not be
 *              in the middle of a 2PC.
 *              It is recommended to attach a client to a 2PC loose end
 *              transaction just after the client restart or after a commit
 *              or abort.
 *              The attachment is done by copying some information of the
 *              current transaction client index over the 2PC loose end index
 *              and the previous assigned client index is freed.
 *              The attachment may fail if the current client user is not the
 *              same that the original user due to recovery client issues.
 */
int
log_2pc_attach_global_tran (THREAD_ENTRY * thread_p, int gtrid)
{
  LOG_TDES *client_tdes;	/* The current (client) transaction descriptor */
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  assert (gtrid != LOG_2PC_NULL_GTRID);

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  client_tdes = LOG_FIND_TDES (tran_index);
  if (client_tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return NULL_TRAN_INDEX;
    }

  if (LOG_ISTRAN_2PC (client_tdes))
    {
      /* 
       * The current transaction is in the middle of the 2PC protocol, we
       * cannot attach at this moment
       */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_2PC_CANNOT_ATTACH, 2, client_tdes->trid, gtrid);
      return NULL_TRAN_INDEX;
    }

  TR_TABLE_CS_ENTER (thread_p);

  if (log_Gl.trantable.num_prepared_loose_end_indices > 0)
    {
      tdes = log_2pc_find_tran_descriptor (gtrid);
      if (tdes == NULL)
	{
	  goto error;
	}

      if (log_2pc_attach_client (thread_p, tdes, client_tdes) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_2PC_CANNOT_ATTACH, 2, gtrid, client_tdes->trid);

	  TR_TABLE_CS_EXIT (thread_p);
	  return NULL_TRAN_INDEX;
	}

      TR_TABLE_CS_EXIT (thread_p);
      return (tdes->tran_index);
    }

error:

  TR_TABLE_CS_EXIT (thread_p);

  /* There is no such transaction to attach to */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_2PC_UNKNOWN_GTID, 1, gtrid);
  return NULL_TRAN_INDEX;

}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_2pc_append_recv_ack - Acknowledgement received from a participant
 *
 * return:
 *
 *   particp_index(in): index of the participant that sent the acknowledgement
 *
 * NOTE:This function is invoked when an acknowledgement of a commit
 *              or abort decision is received for the current distributed
 *              transaction. A log record for this effect is logged to make
 *              the acknowledgement as known permanently.
 */
int
log_2pc_append_recv_ack (THREAD_ENTRY * thread_p, int particp_index)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  LOG_REC_2PC_PARTICP_ACK *received_ack;
  int tran_index;
  LOG_LSA start_lsa;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return ER_LOG_UNKNOWN_TRANINDEX;
    }

  if ((tdes->coord == NULL) || (tdes->coord->ack_received == NULL)
      || (!LOG_ISTRAN_2PC_INFORMING_PARTICIPANTS (tdes) && (tdes->state != TRAN_UNACTIVE_2PC_COMMIT_DECISION)
	  && (tdes->state != TRAN_UNACTIVE_2PC_ABORT_DECISION)))
    {
      /* 
       * May be a system error since transaction is not collecting
       * acknowledgement from participants. No participant is expected to send
       * acknowledgement.
       */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_2pc_recvack: Transaction %d " "(index = %d) is not expecting acknowledgement."
		    " Its state is %s\n", tdes->trid, tdes->tran_index, log_state_string (tdes->state));
#endif /* CUBRID_DEBUG */
      return ER_FAILED;
    }

  if (tdes->coord->num_particps < particp_index || particp_index < 0)
    {
      /* 
       * It is a system error since the given participant index is greater than
       * the expected range (which is from 0 to num_particps - 1).
       */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_2pc_recvack: Wrong " " Participant index = %d for distributed transaction = %d"
		    " (index = %d). Valid participant indices are from = %d" " to %d", particp_index, tdes->trid,
		    tdes->tran_index, 0, tdes->coord->num_particps - 1);
#endif /* CUBRID_DEBUG */
      return ER_FAILED;
    }

  if (tdes->coord->ack_received[particp_index] == true)
    {
      /* May be a system error since this participant has already sent its acknowledgement. */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_2pc_recvack: Participant %d" " of transaction %d (index = %d) has already sent its"
		    " acknowledgement. It may be a system error. Operation is" " ignored.\n", tdes->trid,
		    tdes->tran_index);
#endif /* CUBRID_DEBUG */
      return ER_FAILED;
    }

  /* Set the ack_received flag for this participant */
  tdes->coord->ack_received[particp_index] = true;

  LOG_CS_ENTER (thread_p);

  /* Enter Log record for this acknowledgement */
  start_lsa = logpb_start_append (thread_p, LOG_2PC_RECV_ACK, tdes);

  /* ADD the data header */
  LOG_APPEND_ADVANCE_WHEN_DOESNOT_FIT (thread_p, sizeof (*received_ack));
  received_ack = (LOG_REC_2PC_PARTICP_ACK *) LOG_APPEND_PTR ();

  received_ack->particp_index = particp_index;
  LOG_APPEND_SETDIRTY_ADD_ALIGN (thread_p, sizeof (*received_ack));
  /* 
   * END append
   * Don't need to flush, if there is a need the participant will be contacted
   * again.
   */
  logpb_end_append (thread_p);
  logpb_flush_pages (thread_p, &start_lsa);
  assert (LOG_CS_OWN (thread_p));

  LOG_CS_EXIT (thread_p);

  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * log_2pc_prepare_global_tran - Prepare the transaction to commit
 *
 * return: TRAN_STATE
 *
 *   gtrid(in): Identifier of the global transaction. The coordinator is
 *             responsible for generating the global transaction identifier.
 *
 * NOTE:This function prepares the transaction identified by "gtrid"
 *              for commitment. Any objects and data that the transaction held
 *              or modified are placed in a state that can be guarantee the
 *              the commintment of the transaction by coordinator request
 *              regardless of failures. The shared type locks (IS, S) acquired
 *              by the transaction are released (SIX is demoted to IX lock)
 *              and the exclusive type locks (IX, X) acquired by the
 *              transaction are saved in the log as part of the prepare to
 *              commit log record. This is needed since, we must guarantee the
 *              consistency of the updated data until the transaction is
 *              either committed or aborted by the coordinator regardless of
 *              failures. If the transaction cannot be committed, it was
 *              previously aborted, and the coordinator is notified of such
 *              state.
 */
TRAN_STATE
log_2pc_prepare_global_tran (THREAD_ENTRY * thread_p, int gtrid)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  LOG_TDES *other_tdes;		/* Transaction descriptor */
  LOG_REC_2PC_PREPCOMMIT *prepared;	/* A prepare to commit log record */
  LK_ACQUIRED_LOCKS acq_locks;	/* List of acquired locks */
  bool decision;		/* The decision: success or failure */
  TRAN_STATE state;		/* The state of the transaction */
  int size;
  int i;
  int tran_index;
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return TRAN_UNACTIVE_UNKNOWN;
    }

  if (!LOG_ISTRAN_ACTIVE (tdes))
    {
      /* 
       * May be a system error since transaction is not active.. cannot be
       * prepared to committed
       */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_2pc_prepare: Transaction %d " "(index = %d) is not active for prepare to commit."
		    " Its state is %s\n", tdes->trid, tdes->tran_index, log_state_string (tdes->state));
#endif /* CUBRID_DEBUG */
      return tdes->state;
    }

  if (tdes->topops.last >= 0)
    {
      /* 
       * This is likely a system error since the transaction is being prepared
       * to commit when there are system permanent operations attached to it.
       * We assume that the transaction finished those top actions and that a
       * commit is required on them.
       */

#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_2pc_prepare: May be a system error.\n" "Prepare to commit requested on the transaction = %d"
		    " (index = %d) which has permanent operations attached"
		    " to it.\n Will attach those system operations to the" " transaction\n", tdes->trid,
		    tdes->tran_index);
#endif /* CUBRID_DEBUG */
      assert (false);
      while (tdes->topops.last >= 0)
	{
	  log_sysop_attach_to_outer (thread_p);
	}
    }

  /* Check if the given global transaction identifier is unique. Perform a linear search on the transaction table and
   * make sure that the given identifier is not being used by another transaction. Since the number of entries in the
   * transaction table is expected to be reasonably small, there is no need to use a hashing mechanism here. */

  TR_TABLE_CS_ENTER (thread_p);

  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      other_tdes = LOG_FIND_TDES (i);
      if (other_tdes == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1,
		  LOG_FIND_THREAD_TRAN_INDEX (thread_p));
	  TR_TABLE_CS_EXIT (thread_p);
	  return TRAN_UNACTIVE_UNKNOWN;
	}

      if (other_tdes != tdes && other_tdes->trid != NULL_TRANID && other_tdes->gtrid != LOG_2PC_NULL_GTRID
	  && other_tdes->gtrid == gtrid)
	{
	  /* This gtrid is not unique; It is already been used */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_2PC_NON_UNIQUE_GTID, 1, gtrid);
	  TR_TABLE_CS_EXIT (thread_p);
	  return tdes->state;
	}
    }
  TR_TABLE_CS_EXIT (thread_p);

  /* 
   * Check if the current site is not only a participant but also a
   * coordinator for some other participnats. If the current site is a
   * coordinator of the transaction,its participants must prepare to commit
   * before we can proceed with the prepare to commit. If not all the
   * participants are willing to commit, the prepare to commit cannot be
   * guaranteed; thus, the transaction is aborted at this site and its
   * participants that were willing to commit teh transaction.
   */

  tdes->gtrid = gtrid;
  if (log_is_tran_distributed (tdes))
    {
      /* 
       * Site is also a coordinator, so we need to execute a nested 2PC
       */
      state = log_2pc_commit (thread_p, tdes, LOG_2PC_EXECUTE_PREPARE, &decision);
      if (decision == false)
	{
	  return state;
	}

      /* Now proceed as participant of the distributed transaction */
    }

  lock_unlock_all_shared_get_all_exclusive (thread_p, &acq_locks);

  /* 
   * Indicate that we are willing to commit the transaction
   */

  size = 0;
  if (acq_locks.obj != NULL)
    {
      size = acq_locks.nobj_locks * sizeof (LK_ACQOBJ_LOCK);
    }

  node =
    prior_lsa_alloc_and_copy_data (thread_p, LOG_2PC_PREPARE, RV_NOT_DEFINED, NULL, tdes->gtrinfo.info_length,
				   (char *) tdes->gtrinfo.info_data, size, (char *) acq_locks.obj);
  if (node == NULL)
    {
      if (acq_locks.obj != NULL)
	{
	  free_and_init (acq_locks.obj);
	}

      return TRAN_UNACTIVE_UNKNOWN;
    }

  prepared = (LOG_REC_2PC_PREPCOMMIT *) node->data_header;

  memcpy (prepared->user_name, tdes->client.db_user, DB_MAX_USER_LENGTH);
  prepared->gtrid = gtrid;
  prepared->gtrinfo_length = tdes->gtrinfo.info_length;
  prepared->num_object_locks = acq_locks.nobj_locks;
  /* ignore num_page_locks */
  prepared->num_page_locks = 0;

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  if (acq_locks.obj != NULL)
    {
      free_and_init (acq_locks.obj);
    }

  /* 
   * END append. The log need to be flushed since we need to guarantee
   * the commitment of the transaction if the coordinator requests commit
   */

  tdes->state = TRAN_UNACTIVE_2PC_PREPARE;
  logpb_flush_pages (thread_p, &start_lsa);

  return tdes->state;
}

/*
 * log_2pc_read_prepare - READ PREPARE_TO_COMMIT LOG RECORD
 *
 * return: nothing
 *
 *   acquire_locks(in): specify if list of locks needs to be read from the log
 *                record and the listed locks needs to be acquired.
 *   tdes(in): Transaction descriptor
 *   log_lsa(in): Log address identifier containing the log record
 *   log_pgptr(in): the buffer containing the log page
 *
 * NOTE:This function is used to read the prepared log record from the
 *              system log at the specified location. If "acquire_locks"
 *              parameter is off only the global transaction identifier is
 *              read. If this parameter is on, on the other hand, the list of
 *              update-type locks that the transaction had acquired at the
 *              time of crash is also read from the log record, and the listed
 *              locks aqcuired.
 *              Note that the parameters specifying the location of the log
 *              record to be read are updated to point to the end of the
 *              record.
 */
void
log_2pc_read_prepare (THREAD_ENTRY * thread_p, int acquire_locks, LOG_TDES * tdes, LOG_LSA * log_lsa,
		      LOG_PAGE * log_page_p)
{
  LOG_REC_2PC_PREPCOMMIT *prepared;	/* A 2PC prepare to commit log record */
  LK_ACQUIRED_LOCKS acq_locks;	/* List of acquired locks before the system crash */
  int size;

  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*prepared), log_lsa, log_page_p);

  prepared = (LOG_REC_2PC_PREPCOMMIT *) ((char *) log_page_p->area + log_lsa->offset);

  logtb_set_client_ids_all (&tdes->client, 0, NULL, prepared->user_name, NULL, NULL, NULL, -1);

  tdes->gtrid = prepared->gtrid;
  tdes->gtrinfo.info_length = prepared->gtrinfo_length;

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*prepared), log_lsa, log_page_p);

  if (tdes->gtrinfo.info_length > 0)
    {
      tdes->gtrinfo.info_data = malloc (tdes->gtrinfo.info_length);
      if (tdes->gtrinfo.info_data == NULL)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_read_prepare");

	  return;
	}

      /* Read the global transaction user information data */
      LOG_READ_ALIGN (thread_p, log_lsa, log_page_p);

      logpb_copy_from_log (thread_p, (char *) tdes->gtrinfo.info_data, tdes->gtrinfo.info_length, log_lsa, log_page_p);
    }

  /* If the update-type locks that the transaction had obtained before the crash needs to be aqcuired, read them from
   * the log record and obtain the locks at this time. */

  if (acquire_locks != false)
    {
      /* Read in the list of locks to acquire */

      LOG_READ_ALIGN (thread_p, log_lsa, log_page_p);

      acq_locks.nobj_locks = prepared->num_object_locks;
      acq_locks.obj = NULL;

      if (acq_locks.nobj_locks > 0)
	{
	  /* obtain the list of locks to acquire on objects */
	  size = acq_locks.nobj_locks * sizeof (LK_ACQOBJ_LOCK);
	  acq_locks.obj = (LK_ACQOBJ_LOCK *) malloc (size);
	  if (acq_locks.obj == NULL)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_read_prepare");
	      return;
	    }

	  logpb_copy_from_log (thread_p, (char *) acq_locks.obj, size, log_lsa, log_page_p);
	  LOG_READ_ALIGN (thread_p, log_lsa, log_page_p);
	}

      if (acq_locks.nobj_locks > 0)
	{
	  /* Acquire the locks */
	  if (lock_reacquire_crash_locks (thread_p, &acq_locks, tdes->tran_index) != LK_GRANTED)
	    {
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_read_prepare");
	      return;
	    }

	  free_and_init (acq_locks.obj);
	}
    }
}

/*
 * log_2pc_dump_gtrinfo - DUMP GLOBAL TRANSACTION USER INFORMATION
 *
 * return: nothing
 *
 *   length(in): Length to dump in bytes
 *   data(in): The data being logged
 *
 * NOTE:Dump global transaction user information
 */
void
log_2pc_dump_gtrinfo (FILE * fp, int length, void *data)
{
}

/*
 * log_2pc_dump_acqobj_locks - DUMP THE ACQUIRED OBJECT LOCKS
 *
 * return: nothing
 *
 *   length(in): Length to dump in bytes
 *   data(in): The data being logged
 *
 * NOTE: Dump the acquired object lock structure.
 */
void
log_2pc_dump_acqobj_locks (FILE * fp, int length, void *data)
{
  LK_ACQUIRED_LOCKS acq_locks;

  acq_locks.nobj_locks = length / sizeof (LK_ACQOBJ_LOCK);
  acq_locks.obj = (LK_ACQOBJ_LOCK *) data;
  lock_dump_acquired (fp, &acq_locks);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_2pc_dump_acqpage_locks - DUMP THE ACQUIRED PAGE LOCKS
 *
 * return: nothing
 *
 *   length(in): Length to dump in bytes
 *   data(in): The data being logged
 *
 * NOTE:Dump the acquired page lock structure.
 */
void
log_2pc_dump_acqpage_locks (FILE * fp, int length, void *data)
{
  LK_ACQUIRED_LOCKS acq_locks;

  acq_locks.nobj_locks = 0;
  acq_locks.obj = NULL;
  lock_dump_acquired (fp, &acq_locks);
}
#endif

/*
 * log_2pc_append_start - APPEND A VOTING LOG RECORD FOR THE 2PC PROTOCOL
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction
 *
 * NOTE:A LOG_2PC_START log record is appended to the log to start
 *              the 2PC protocol (i.e., its atomic commitment). The
 *              transaction is declared as collecting votes. This function is
 *              used by the coordinator site of a distributed transaction.
 */
static void
log_2pc_append_start (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  LOG_REC_2PC_START *start_2pc;	/* Start 2PC log record */
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node =
    prior_lsa_alloc_and_copy_data (thread_p, LOG_2PC_START, RV_NOT_DEFINED, NULL,
				   (tdes->coord->particp_id_length * tdes->coord->num_particps),
				   (char *) tdes->coord->block_particps_ids, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  start_2pc = (LOG_REC_2PC_START *) node->data_header;

  memcpy (start_2pc->user_name, tdes->client.db_user, DB_MAX_USER_LENGTH);
  start_2pc->gtrid = tdes->gtrid;
  start_2pc->num_particps = tdes->coord->num_particps;
  start_2pc->particp_id_length = tdes->coord->particp_id_length;

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  /* 
   * END append
   * We need to flush the log so that we can find the participants of the
   * 2PC in the event of a crash. This is needed since the participants do
   * not know about the coordinator or other participants. Participants will
   * always wait for the coordinators. We do not have a full 2PC in which
   * particpants know about each other and the coordiantor.
   */
  tdes->state = TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES;
  logpb_flush_pages (thread_p, &start_lsa);
}

/*
 * log_2pc_append_decision - THE DECISION FOR THE DISTRIBUTED TRANSACTION HAS
 *                          BEEN TAKEN
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction
 *   decision(in): Either LOG_2PC_COMMIT_DECISION or LOG_2PC_ABORT_DECISION
 *
 * NOTE:A decision was taken to either commit or abort the distributed
 *              transaction. If a commit decsion was taken all participants
 *              and the coordinator have agreed to commit the transaction. On
 *              the other hand, if an abort decsion wasd taken, the
 *              coordinator and all participants did not reach a agreement to
 *              commit the transaction. It is likely that the distributed
 *              transaction was aborted at a remote site for circunstances
 *              beyond our control. A LOG_2PC_ABORT_DECISION log record is
 *              appended to the log. The second phase of the 2PC starts after
 *              the function finishes.
 */
static void
log_2pc_append_decision (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_RECTYPE decision)
{
  LOG_PRIOR_NODE *node;
  LOG_LSA start_lsa;

  node = prior_lsa_alloc_and_copy_data (thread_p, decision, RV_NOT_DEFINED, NULL, 0, NULL, 0, NULL);
  if (node == NULL)
    {
      return;
    }

  start_lsa = prior_lsa_next_record (thread_p, node, tdes);

  if (decision == LOG_2PC_COMMIT_DECISION)
    {
      tdes->state = TRAN_UNACTIVE_2PC_COMMIT_DECISION;

      /* 
       * END append
       * We need to flush the log so that we can find the decision if a
       * participant needed in the event of a crash. If the decision is not
       * found in the log, we will assume abort
       */
      logpb_flush_pages (thread_p, &start_lsa);
    }
  else
    {
      tdes->state = TRAN_UNACTIVE_2PC_ABORT_DECISION;

      /* 
       * END append
       * We do not need to flush the log since if the decision is not found in
       * the log, abort is assumed.
       */
    }

}

/*
 * log_2pc_alloc_coord_info - ALLOCATE COORDINATOR RELATED INFORMATION
 *
 * return: tdes or NULL
 *
 *   tdes(in): Transaction descriptor
 *   num_particps(in): Number of participating sites
 *   particp_id_length(in): Length of particp_ids block
 *   block_particps_ids(in): A block of information about the participants
 *
 * NOTE:This function is used to allocate and initialize coordinator
 *              related information about participants.
 */
LOG_TDES *
log_2pc_alloc_coord_info (LOG_TDES * tdes, int num_particps, int particp_id_length, void *block_particps_ids)
{
  /* Initialize the coordinator information */
  tdes->coord = (LOG_2PC_COORDINATOR *) malloc (sizeof (LOG_2PC_COORDINATOR));
  if (tdes->coord == NULL)
    {
      return NULL;
    }
  else
    {
      tdes->coord->num_particps = num_particps;
      tdes->coord->particp_id_length = particp_id_length;
      tdes->coord->block_particps_ids = block_particps_ids;
      tdes->coord->ack_received = NULL;
    }

  return tdes;
}

/*
 * log_2pc_free_coord_info -  FREE COORDINATOR RELATED INFORMATION
 *
 * return: nothing
 *
 *   tdes(in): Transaction descriptor
 *
 * NOTE:This function is used to free coordinator related information
 *              about participants.
 */
void
log_2pc_free_coord_info (LOG_TDES * tdes)
{
  if (tdes->coord != NULL)
    {
      if (tdes->coord->ack_received != NULL)
	{
	  free_and_init (tdes->coord->ack_received);
	}

      if (tdes->coord->block_particps_ids != NULL)
	{
	  free_and_init (tdes->coord->block_particps_ids);
	}

      free_and_init (tdes->coord);
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_2pc_crash_participant - A participant aborted its local part of the global
 *                          transaction, or went down
 *
 * return: nothing
 *
 * NOTE:This function informs the transaction manager that one or
 *              more of the participanting sites of the current global
 *              transaction (whose coordinator is this site) has either went
 *              down, or decided to unilaterally abort their local part of
 *              this global transaction. As a result, this global transaction
 *              is aborted by this function, if it was in active state. If the
 *              transaction was already in two phase commit protocol, then
 *              this function does not take any action here as this condition
 *              will be handled naturally by the two phase commit protocol.
 */
void
log_2pc_crash_participant (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  char *client_prog_name;	/* Client program name for transaction */
  char *client_user_name;	/* Client user name for transaction */
  char *client_host_name;	/* Client host for transaction */
  int client_pid;		/* Client process identifier for transaction */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return;
    }

  if (log_is_tran_distributed (tdes) == false)
    {
      /* It is a system error since transaction is not a coordinator. */
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_2pc_particp_crash: Transaction %d " "(index = %d) is not a global one; and thus has no "
		    "participants that could abort. It is in state: %s\n", tdes->trid, tdes->tran_index,
		    log_state_string (tdes->state));
#endif /* CUBRID_DEBUG */
      return;
    }

  tdes->coord->ack_received = NULL;

  /* If the coordinator info has not been recorded in the tdes, do it now */

  /* If the transaction is active it needs to be aborted; Otherwise (i.e. the transaction is in 2PC protocol), no
   * action needs to be taken here; 2PC protocol will consider this condition */

  (void) log_abort (thread_p, tran_index);

  /* Put an error code recording this situation */
  (void) logtb_find_client_name_host_pid (tdes->tran_index, &client_prog_name, &client_user_name, &client_host_name,
					  &client_pid);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LK_UNILATERALLY_ABORTED, 4, tdes->tran_index, client_user_name,
	  client_host_name, client_pid);
}

/*
 * log_2pc_broadcast_decision_participant -
 *
 * return:
 *
 *   tdes(in):
 *   particp_index(in):
 *
 * Note:
 */
static int
log_2pc_broadcast_decision_participant (THREAD_ENTRY * thread_p, LOG_TDES * tdes, int particp_index)
{
  int *temp;
  int local_tran_index;
  int i;

  if (tdes->coord->ack_received[particp_index] == false)
    {
      temp = (int *) calloc (sizeof (int), tdes->coord->num_particps);
      if (temp == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      for (i = 0; i < tdes->coord->num_particps; i++)
	{
	  temp[i] = true;
	}

      temp[particp_index] = false;

      local_tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
      LOG_SET_CURRENT_TRAN_INDEX (thread_p, tdes->tran_index);

      if (LOG_ISTRAN_COMMITTED (tdes))
	{
	  /* 
	   * Decision was commit. Broadcast the commit to the participant
	   */

	  /* 
	   * Note that this communication needs to be synchronous (i.e. we
	   * should wait for the return of the function call
	   *
	   * If the following function fails, the transaction will be
	   * dangling and we need to retry sending the decision at another
	   * point.
	   * We have already decided and log the decision in the log file.
	   */
	  (void) log_2pc_send_commit_decision (tdes->gtrid, tdes->coord->num_particps, tdes->coord->ack_received,
					       tdes->coord->block_particps_ids);
	  if (tdes->coord->ack_received[particp_index] == true)
	    {
	      (void) log_complete_for_2pc (thread_p, tdes, LOG_COMMIT, LOG_DONT_NEED_NEWTRID);
	    }
	}
      else
	{
	  /* 
	   * Decsion was abort. Broadcast the abort to the participant
	   */

	  /* 
	   * Note that this communication needs to be syncronous (i.e. we
	   * should wait for the return of the function call
	   *
	   * If the following function fails, the transaction will be
	   * dangling and we need to retry sending the decision at another
	   * point.
	   * We have already decided and log the decision in the log file.
	   */
	  (void) log_2pc_send_abort_decision (tdes->gtrid, tdes->coord->num_particps, temp,
					      tdes->coord->block_particps_ids, true);
	  if (tdes->coord->ack_received[particp_index] == true)
	    {
	      (void) log_complete_for_2pc (thread_p, tdes, LOG_ABORT, LOG_DONT_NEED_NEWTRID);
	    }
	}			/* else */

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, local_tran_index);
      free_and_init (temp);
    }

  return ER_FAILED;
}

/*
 * log_2pc_send_decision_participant - Send decision of blocked transactions to just
 *                               reconnected participant
 *
 * return: nothing
 *
 *   particp_id(in): info to identify the participant to reconnect
 *
 * NOTE:For all distributed loose end transactions which have
 *              "partic-id" as a participant, the decisions are sent to this
 *              participant.
 *              This function is used when a participant is restarted.
 */
void
log_2pc_send_decision_participant (THREAD_ENTRY * thread_p, void *particp_id)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int particp_index;
  int i;

  for (i = 0;; i++)
    {

      TR_TABLE_CS_ENTER (thread_p);

      if ((i >= log_Gl.trantable.num_total_indices || log_Gl.trantable.num_coord_loose_end_indices <= 0))
	{
	  TR_TABLE_CS_EXIT (thread_p);
	  break;
	}

      TR_TABLE_CS_EXIT (thread_p);

      tdes = LOG_FIND_TDES (i);
      if (tdes != NULL && LOG_ISTRAN_2PC_INFORMING_PARTICIPANTS (tdes) && tdes->isloose_end)
	{
	  particp_index =
	    log_2pc_lookup_particp (particp_id, tdes->coord->num_particps, tdes->coord->block_particps_ids);
	  if (particp_index != -1)
	    {
	      (void) log_2pc_broadcast_decision_participant (thread_p, tdes, particp_index);
	    }
	}
    }
}
#endif

/*
 * log_2pc_recovery_prepare -
 *
 * return:
 *
 *   tdes(in/out):
 *   lsa(in/out):
 *   log_page_p(in/out):
 *
 * Note:
 */
static void
log_2pc_recovery_prepare (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * log_lsa, LOG_PAGE * log_page_p)
{
  /* 
   * This is a particpant of the distributed transaction. We
   * need to continue looking since this participant may be
   * a non root coordinator
   */

  /* Get the DATA HEADER */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);

  /* The transaction was in prepared_to_commit state at the time of crash. So, read the global transaction identifier
   * and list of locks from the log record, and acquire all of the locks. */
  if (tdes->state == TRAN_UNACTIVE_2PC_PREPARE)
    {
      log_2pc_read_prepare (thread_p, LOG_2PC_OBTAIN_LOCKS, tdes, log_lsa, log_page_p);
    }
  else
    {
      log_2pc_read_prepare (thread_p, LOG_2PC_DONT_OBTAIN_LOCKS, tdes, log_lsa, log_page_p);
    }
}

/*
 * log_2pc_recovery_start -
 *
 * return:
 *
 *   tdes(in/out):
 *   lsa(in/out):
 *   log_page_p(in/out):
 *   ack_list(in/out):
 *   ack_count(in/out):
 *
 * Note:
 */
static int
log_2pc_recovery_start (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * log_lsa, LOG_PAGE * log_page_p,
			int *ack_list, int *ack_count)
{
  LOG_REC_2PC_START *start_2pc;	/* A 2PC start log record */
  void *block_particps_ids;	/* A block of participant identifiers */
  int num_particps;
  int particp_id_length;
  int i;

  /* Obtain the coordinator information */
  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*start_2pc), log_lsa, log_page_p);

  start_2pc = ((LOG_REC_2PC_START *) ((char *) log_page_p->area + log_lsa->offset));
  /* 
   * Obtain the participant information for this coordinator
   */
  logtb_set_client_ids_all (&tdes->client, 0, NULL, start_2pc->user_name, NULL, NULL, NULL, -1);
  tdes->gtrid = start_2pc->gtrid;

  num_particps = start_2pc->num_particps;
  particp_id_length = start_2pc->particp_id_length;

  block_particps_ids = malloc (particp_id_length * num_particps);
  if (block_particps_ids == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_recovery_analysis_info");
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  LOG_READ_ADD_ALIGN (thread_p, sizeof (*start_2pc), log_lsa, log_page_p);
  LOG_READ_ALIGN (thread_p, log_lsa, log_page_p);

  /* Read in the participants info. block from the log */
  logpb_copy_from_log (thread_p, (char *) block_particps_ids, particp_id_length * num_particps, log_lsa, log_page_p);

  /* Initialize the coordinator information */
  if (log_2pc_alloc_coord_info (tdes, num_particps, particp_id_length, block_particps_ids) == NULL)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_recovery_analysis_info");
      return ER_FAILED;
    }

  /* Initialize the Acknowledgement vector to false since we do not know what acknowledgments have already been
   * received. we need to continue reading the log */

  i = sizeof (int) * tdes->coord->num_particps;
  tdes->coord->ack_received = (int *) malloc (i);
  if (tdes->coord->ack_received == NULL)
    {
      log_2pc_free_coord_info (tdes);
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_recovery_analysis_info");
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (i = 0; i < tdes->coord->num_particps; i++)
    {
      tdes->coord->ack_received[i] = false;
    }

  if (*ack_count > 0 && ack_list != NULL)
    {
      /* 
       * Some participant acknowledgemnts have already been
       * received. Copy this acknowledgment into the transaction
       * descriptor.
       */
      for (i = 0; i < *ack_count; i++)
	{
	  if (ack_list[i] > tdes->coord->num_particps)
	    {
	      er_log_debug (ARG_FILE_LINE, "log_2pc_recovery_analysis_info:" " SYSTEM ERROR for log located at %lld|%d",
			    log_lsa->pageid, log_lsa->offset);
	    }
	  else
	    {
	      tdes->coord->ack_received[ack_list[i]] = true;
	    }
	}
      free_and_init (ack_list);
      *ack_count = 0;
    }

  return NO_ERROR;
}

/*
 * log_2pc_expand_ack_list -
 *
 * return:
 *
 *   ack_list(in/out):
 *   ack_count(in/out):
 *   size_ack_list(in/out):
 *
 * Note:
 */
static int *
log_2pc_expand_ack_list (THREAD_ENTRY * thread_p, int *ack_list, int *ack_count, int *size_ack_list)
{
  int size;

  if ((*ack_count + 1) > (*size_ack_list))
    {
      /* allocate space */
      if (*size_ack_list == 0)
	{
	  /* 
	   * Initialize the temporary area. Assume no more than 10
	   * participants
	   */
	  *ack_count = 0;
	  *size_ack_list = 10;

	  size = (*size_ack_list) * sizeof (int);
	  ack_list = (int *) malloc (size);
	  if (ack_list == NULL)
	    {
	      /* Out of memory */
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_recovery_analysis_info");
	      return NULL;
	    }
	}
      else
	{
	  /* expand ack list by 30% */
	  *size_ack_list = ((int) (((float) (*size_ack_list) * 1.30) + 0.5));
	  size = (*size_ack_list) * sizeof (int);
	  ack_list = (int *) realloc (ack_list, size);
	  if (ack_list == NULL)
	    {
	      /* Out of memory */
	      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_recovery_analysis_info");
	      return NULL;
	    }
	}
    }

  return ack_list;
}

/*
 * log_2pc_recovery_recv_ack -
 *
 * return:
 *
 *   lsa(in/out):
 *   log_page_p(in/out):
 *   ack_list(in/out):
 *   ack_count(in/out):
 *
 * Note:
 */
static void
log_2pc_recovery_recv_ack (THREAD_ENTRY * thread_p, LOG_LSA * log_lsa, LOG_PAGE * log_page_p, int *ack_list,
			   int *ack_count)
{
  LOG_REC_2PC_PARTICP_ACK *received_ack;	/* A 2PC recv decision ack */

  LOG_READ_ADD_ALIGN (thread_p, sizeof (LOG_RECORD_HEADER), log_lsa, log_page_p);
  LOG_READ_ADVANCE_WHEN_DOESNT_FIT (thread_p, sizeof (*received_ack), log_lsa, log_page_p);
  received_ack = ((LOG_REC_2PC_PARTICP_ACK *) ((char *) log_page_p->area + log_lsa->offset));

  ack_list[*ack_count] = received_ack->particp_index;
  (*ack_count)++;
}

/*
 * log_2pc_recovery_analysis_record -
 *
 * return:
 *
 *   record_type(in):
 *   tdes(in/out):
 *   lsa(in/out):
 *   log_page_p(in/out):
 *   ack_list(in/out):
 *   ack_count(in/out):
 *   size_ack_list(in/out):
 *   search_2pc_prepare(in/out):
 *   search_2pc_start(in/out):
 *
 * Note:
 */
static int
log_2pc_recovery_analysis_record (THREAD_ENTRY * thread_p, LOG_RECTYPE record_type, LOG_TDES * tdes, LOG_LSA * log_lsa,
				  LOG_PAGE * log_page_p, int **ack_list, int *ack_count, int *size_ack_list,
				  bool * search_2pc_prepare, bool * search_2pc_start)
{
  switch (record_type)
    {
    case LOG_2PC_PREPARE:
      if (*search_2pc_prepare)
	{
	  log_2pc_recovery_prepare (thread_p, tdes, log_lsa, log_page_p);
	  *search_2pc_prepare = false;
	}
      break;

    case LOG_2PC_START:
      if (*search_2pc_start)
	{
	  if (log_2pc_recovery_start (thread_p, tdes, log_lsa, log_page_p, *ack_list, ack_count) == NO_ERROR)
	    {
	      *search_2pc_start = false;
	    }
	}
      break;

    case LOG_2PC_RECV_ACK:
      /* 
       * Coordiantor site: The distributed transaction is in the
       * second phase of the 2PC, that is, coordinator has notfied
       * the decision to participants and some of them as acknowledge
       * the execution of the decsion.
       */
      if (*search_2pc_start && LOG_ISTRAN_2PC_IN_SECOND_PHASE (tdes))
	{
	  *ack_list = log_2pc_expand_ack_list (thread_p, *ack_list, ack_count, size_ack_list);
	  log_2pc_recovery_recv_ack (thread_p, log_lsa, log_page_p, *ack_list, ack_count);
	}
      break;

    case LOG_COMPENSATE:
    case LOG_RUN_POSTPONE:
    case LOG_WILL_COMMIT:
    case LOG_COMMIT_WITH_POSTPONE:
    case LOG_2PC_COMMIT_DECISION:
    case LOG_2PC_ABORT_DECISION:
    case LOG_2PC_COMMIT_INFORM_PARTICPS:
    case LOG_2PC_ABORT_INFORM_PARTICPS:
      /* Skip over this log record types */
      break;

    case LOG_UNDOREDO_DATA:
    case LOG_DIFF_UNDOREDO_DATA:
    case LOG_UNDO_DATA:
    case LOG_REDO_DATA:
    case LOG_MVCC_UNDOREDO_DATA:
    case LOG_MVCC_DIFF_UNDOREDO_DATA:
    case LOG_MVCC_UNDO_DATA:
    case LOG_MVCC_REDO_DATA:
    case LOG_DBEXTERN_REDO_DATA:
    case LOG_DUMMY_HEAD_POSTPONE:
    case LOG_POSTPONE:
    case LOG_SAVEPOINT:
    case LOG_COMMIT:
    case LOG_ABORT:
    case LOG_SYSOP_START_POSTPONE:
    case LOG_SYSOP_END:
    case LOG_START_CHKPT:
    case LOG_END_CHKPT:
    case LOG_DUMMY_CRASH_RECOVERY:
    case LOG_REPLICATION_DATA:
    case LOG_REPLICATION_STATEMENT:
    case LOG_END_OF_LOG:
      /* 
       * Either the prepare to commit or start 2PC record should
       * have already been found by now. Otherwise, it is likely that the
       * transaction is not a distributed transaction that has loose end
       * client actions
       */
      if (*search_2pc_start == false)
	{
	  *search_2pc_prepare = false;
	}
      else if (*search_2pc_prepare == false)
	{
	  *search_2pc_start = false;
	}

      break;

    case LOG_SMALLER_LOGREC_TYPE:
    case LOG_LARGER_LOGREC_TYPE:
    default:
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE, "log_2pc_recovery_analysis_info:" " Unknown record type = %d May be a system error",
		    log_rec->type);
#endif /* CUBRID_DEBUG */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_PAGE_CORRUPTED, 1, log_lsa->pageid);
      return ER_LOG_PAGE_CORRUPTED;
    }

  return NO_ERROR;
}

/*
 * log_2pc_recovery_analysis_info - FIND 2PC information of given transaction
 *                                 upto the given lsa address of transaction
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction of the log record
 *   upto_chain_lsa(in): Stop at this lsa of the transaction (This lsa MUST
 *                  be of the given transaction
 *
 * NOTE:Obtain 2PC information of the given transaction up to the
 *              given lsa address. This function is needed at the end of the
 *              recovery analysis phase for transaction that were active at
 *              the time of the crash and at the time of a checkpoint log
 *              record. The upto_chain_lsa address is the undo_tail address of
 *              the transaction at the moment of the checkpoint log record.
 *              We should point out that a checkpoint log record does not
 *              contain all information related to 2PC due to the big space
 *              overhead (e.g., locks) and the closenest to the end of the
 *              transaction. The rest of the 2PC information of this
 *              transaction is read by the redo phase of the recovery process.
 */
void
log_2pc_recovery_analysis_info (THREAD_ENTRY * thread_p, LOG_TDES * tdes, LOG_LSA * upto_chain_lsa)
{
  LOG_RECORD_HEADER *log_rec;	/* Pointer to log record */
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT], *aligned_log_pgbuf;
  LOG_PAGE *log_page_p = NULL;	/* Log page pointer where LSA is located */
  LOG_LSA lsa;
  LOG_LSA prev_tranlsa;		/* prev LSA of transaction */
  bool search_2pc_prepare = false;
  bool search_2pc_start = false;
  int ack_count = 0;
  int *ack_list = NULL;
  int size_ack_list = 0;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  if (!LOG_ISTRAN_2PC (tdes))
    {
      return;
    }

  /* For a transaction that was prepared to commit at the time of the crash, make sure that its global transaction
   * identifier is obtained from the log and that the update_type locks that were acquired before the time of the crash 
   * are reacquired. */

  if (tdes->gtrid == LOG_2PC_NULL_GTRID)
    {
      search_2pc_prepare = true;
    }

  /* If this is a coordinator transaction performing 2PC and voting record has not been read from the log in the
   * recovery redo phase, read the voting record and any acknowledgement records logged for this transaction */

  if (tdes->coord == NULL)
    {
      search_2pc_start = true;
    }

  /* 
   * Follow the undo tail chain starting at upto_chain_tail finding all
   * 2PC related information
   */
  log_page_p = (LOG_PAGE *) aligned_log_pgbuf;

  LSA_COPY (&prev_tranlsa, upto_chain_lsa);
  while (!LSA_ISNULL (&prev_tranlsa) && (search_2pc_prepare || search_2pc_start))
    {
      LSA_COPY (&lsa, &prev_tranlsa);
      if ((logpb_fetch_page (thread_p, &lsa, LOG_CS_FORCE_USE, log_page_p)) != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_2pc_recovery_analysis_info");
	  break;
	}

      while (prev_tranlsa.pageid == lsa.pageid && (search_2pc_prepare || search_2pc_start))
	{
	  lsa.offset = prev_tranlsa.offset;

	  log_rec = LOG_GET_LOG_RECORD_HEADER (log_page_p, &lsa);
	  LSA_COPY (&prev_tranlsa, &log_rec->prev_tranlsa);

	  if (log_2pc_recovery_analysis_record
	      (thread_p, log_rec->type, tdes, &lsa, log_page_p, &ack_list, &ack_count, &size_ack_list,
	       &search_2pc_prepare, &search_2pc_start) != NO_ERROR)
	    {
	      LSA_SET_NULL (&prev_tranlsa);
	    }
	  free_and_init (ack_list);
	}			/* while */
    }				/* while */

  /* Check for error conditions */
  if (tdes->state == TRAN_UNACTIVE_2PC_PREPARE && tdes->gtrid == LOG_2PC_NULL_GTRID)
    {
#if defined(CUBRID_DEBUG)
      er_log_debug (ARG_FILE_LINE,
		    "log_2pc_recovery_analysis_info:" " SYSTEM ERROR... Either the LOG_2PC_PREPARE/LOG_2PC_START\n"
		    " log record was not found for participant of distributed" " trid = %d with state = %s", tdes->trid,
		    log_state_string (tdes->state));
#endif /* CUBRID_DEBUG */
    }

  /* 
   * Now the client should attach to this prepared transaction and
   * provide the decision (commit/abort). Until then this thread
   * is suspended.
   */

  if (search_2pc_start)
    {
      /* 
       * A 2PC start log record was not found for the coordinator
       */
      if (tdes->state != TRAN_UNACTIVE_2PC_PREPARE)
	{
#if defined(CUBRID_DEBUG)
	  er_log_debug (ARG_FILE_LINE,
			"log_2pc_recovery_analysis_info:" " SYSTEM ERROR... The LOG_2PC_START log record was"
			" not found for coordinator of distributed trid = %d" " with state = %s", tdes->trid,
			log_state_string (tdes->state));
#endif /* CUBRID_DEBUG */
	}
    }
}

/*
 * log_2pc_recovery_collecting_participant_votes -
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction of the log record
 *
 * Note:
 */
static void
log_2pc_recovery_collecting_participant_votes (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  /* 
   * This is a participant which has not decided the fate of the
   * distributed transaction. Abort the transaction
   */
  log_2pc_append_decision (thread_p, tdes, LOG_2PC_ABORT_DECISION);

  /* Let it fall thru the TRAN_UNACTIVE_2PC_ABORT_DECISION case */
  log_2pc_recovery_abort_decision (thread_p, tdes);
}

/*
 * log_2pc_recovery_abort_decision -
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction of the log record
 *
 * Note:
 */
static void
log_2pc_recovery_abort_decision (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  TRAN_STATE state;

  /* 
   * An abort decision has already been taken and the system crash
   * during the local abort. Retry it
   */

  /* 
   * The transaction has been declared as 2PC abort. We can execute
   * the LOCAL ABORT AND THE REMOTE ABORTS IN PARALLEL, however our
   * communication subsystem does not support asynchronous communication
   * types. The abort of the participants is done after the local
   * abort is completed.
   */

  /* Save the state.. so it can be reverted to the 2pc state .. */
  state = tdes->state;

  /* 2PC protocol does not support RETAIN LOCK */
  (void) log_abort_local (thread_p, tdes, false);

  if (tdes->state == TRAN_UNACTIVE_ABORTED)
    {
      tdes->state = state;	/* Revert to 2PC state... */
    }

  /* Try to reconnect to participants that have not sent ACK. yet */

  /* 
   * If the following function fails, the transaction will be dangling and we
   * need to retry sending the decision at another point.
   * We have already decided and log the decision in the log file.
   */
  (void) log_2pc_send_abort_decision (tdes->gtrid, tdes->coord->num_particps, tdes->coord->ack_received,
				      tdes->coord->block_particps_ids, true);
  /* Check if all the acknowledgements have been received */
  (void) log_complete_for_2pc (thread_p, tdes, LOG_ABORT, LOG_DONT_NEED_NEWTRID);
}

/*
 * log_2pc_recovery_commit_decision -
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction of the log record
 *
 * Note:
 */
static void
log_2pc_recovery_commit_decision (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  TRAN_STATE state;


  /* Save the state.. so it can be reverted to the 2pc state .. */
  state = tdes->state;

  /* First perform local commit; 2PC protocol does not support RETAIN LOCK */
  (void) log_commit_local (thread_p, tdes, false, false);
  tdes->state = state;		/* Revert to 2PC state... */

  /* 
   * If the following function fails, the transaction will be dangling and we
   * need to retry sending the decision at another point.
   * We have already decided and log the decision in the log file.
   */

  (void) log_2pc_send_commit_decision (tdes->gtrid, tdes->coord->num_particps, tdes->coord->ack_received,
				       tdes->coord->block_particps_ids);
  /* Check if all the acknowledgments have been received */
  (void) log_complete_for_2pc (thread_p, tdes, LOG_COMMIT, LOG_DONT_NEED_NEWTRID);
}

/*
 * log_2pc_recovery_committed_informing_participants -
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction of the log record
 *
 * Note:
 */
static void
log_2pc_recovery_committed_informing_participants (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  /* 
   * Broadcast the commit to the participants that has not sent an
   * acknowledgement yet.
   *
   * If the following function fails, the transaction will be
   * dangling and we need to retry sending the decision at another
   * point.
   * We have already decided and log the decision in the log file.
   */
  (void) log_2pc_send_commit_decision (tdes->gtrid, tdes->coord->num_particps, tdes->coord->ack_received,
				       tdes->coord->block_particps_ids);
  (void) log_complete_for_2pc (thread_p, tdes, LOG_COMMIT, LOG_DONT_NEED_NEWTRID);
}

/*
 * log_2pc_recovery_aborted_informing_participants -
 *
 * return: nothing
 *
 *   tdes(in/out): State structure of transaction of the log record
 *
 * Note:
 */
static void
log_2pc_recovery_aborted_informing_participants (THREAD_ENTRY * thread_p, LOG_TDES * tdes)
{
  /* 
   * Broadcast the abort to the participants that has not sent an
   * acknowledgement yet.
   *
   * If the following function fails, the transaction will be
   * dangling and we need to retry sending the decision at another
   * point.
   * We have already decided and log the decision in the log file.
   */

  (void) log_2pc_send_abort_decision (tdes->gtrid, tdes->coord->num_particps, tdes->coord->ack_received,
				      tdes->coord->block_particps_ids, true);
  (void) log_complete_for_2pc (thread_p, tdes, LOG_ABORT, LOG_DONT_NEED_NEWTRID);
}

/*
 * log_2pc_recovery - TRY TO FINISH TRANSACTIONS THAT WERE IN THE 2PC PROTOCOL
 *                   AT THE TIME OF CRASH
 *
 * return: nothing
 *
 * NOTE:This function tries to finish up the transactions that were
 *              in the two phase commit protocol at the time of the crash.
 */
void
log_2pc_recovery (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int i;

  /* 
   * Try to finish distributed transaction that are in the uncertain phase
   * of the two phase commit
   */

  for (i = 0; i < log_Gl.trantable.num_total_indices; i++)
    {
      tdes = LOG_FIND_TDES (i);

      if (tdes == NULL || tdes->trid == NULL_TRANID || !LOG_ISTRAN_2PC (tdes))
	{
	  continue;
	}

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, i);

      switch (tdes->state)
	{
	case TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES:
	  log_2pc_recovery_collecting_participant_votes (thread_p, tdes);
	  break;

	case TRAN_UNACTIVE_2PC_ABORT_DECISION:
	  log_2pc_recovery_abort_decision (thread_p, tdes);
	  break;

	case TRAN_UNACTIVE_2PC_COMMIT_DECISION:
	  log_2pc_recovery_commit_decision (thread_p, tdes);
	  break;

	case TRAN_UNACTIVE_WILL_COMMIT:
	case TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE:
	  /* 
	   * All the local postpone actions had been completed; there are
	   * not any client postpone actions. Thus, we can inform the
	   * participants at this time.
	   */

	  tdes->state = TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS;
	  /* 
	   * Let it fall thru the
	   * TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS case
	   */

	case TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS:
	  log_2pc_recovery_committed_informing_participants (thread_p, tdes);
	  break;

	case TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS:
	  log_2pc_recovery_aborted_informing_participants (thread_p, tdes);
	  break;

	case TRAN_RECOVERY:
	case TRAN_ACTIVE:
	case TRAN_UNACTIVE_COMMITTED:
	case TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE:
	case TRAN_UNACTIVE_ABORTED:
	case TRAN_UNACTIVE_UNILATERALLY_ABORTED:
	case TRAN_UNACTIVE_2PC_PREPARE:
	case TRAN_UNACTIVE_UNKNOWN:
	  break;
	}
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * log_is_tran_in_2pc - IS TRANSACTION IN THE MIDDLE OF 2PC ?
 *
 * return:
 *
 * NOTE: This function finds if the transaction indicated by tran_index
 *              is in prepare to commit state (i.e., it is waiting for either
 *              commit or abort from its coordiantor).
 */
bool
log_is_tran_in_2pc (THREAD_ENTRY * thread_p)
{
  LOG_TDES *tdes;		/* Transaction descriptor */
  int tran_index;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_UNKNOWN_TRANINDEX, 1, tran_index);
      return false;
    }

  return (LOG_ISTRAN_2PC (tdes));
}
#endif

/*
 * log_is_tran_distributed - IS THIS A COORDINATOR OF A DISTRIBUTED TRANSACTION
 *
 * return:
 *
 *   tdes(in): Transaction descriptor
 *
 * NOTE:Is this the coordinator of a distributed transaction ? If it
 *              is, coordinator information is initialized by this function.
 */
bool
log_is_tran_distributed (LOG_TDES * tdes)
{
  int num_particps = 0;		/* Number of participating sites */
  int particp_id_length;	/* Length of a particp_id */
  void *block_particps_ids;	/* A block of participant identifiers */

  if (tdes->coord != NULL)
    {
      return true;
    }

  num_particps = log_2pc_get_num_participants (&particp_id_length, &block_particps_ids);
  if (num_particps > 0)
    {
      /* This is a distributed transaction and our site is the coordinator */

      /* If the coordinator info has not been recorded in the tdes, do it now */
      (void) log_2pc_alloc_coord_info (tdes, num_particps, particp_id_length, block_particps_ids);
    }

  return (tdes->coord != NULL);
}

/*
 * log_clear_and_is_tran_distributed - FIND IF TRANSACTION IS DISTRIBUTED AFTER
 *                               CLEARING OLD COORDINATOR INFORMATION.
 *
 * return:
 *
 *   tdes(in): Transaction descriptor
 *
 * NOTE: Clear coordinator information about participants. Then, check
 *              if the transaction is distributed..and cache any related
 *              information.
 *              This function is used during commit/abort time to make sure
 *              we have all participants. This is needed since CUBRID does
 *              not inform me of new participants.
 */
bool
log_clear_and_is_tran_distributed (LOG_TDES * tdes)
{
  log_2pc_free_coord_info (tdes);
  return log_is_tran_distributed (tdes);
}
