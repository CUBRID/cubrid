/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * logcp.h -
 * 									       
 * 	Overview: LOG AND RECOVERY MANAGER (AT CLIENT & SERVER)		       
 * 									       
 * This file is used for communication purposes between the client and server  
 * log and recovery manager. A small portion of the log server interface was   
 * exposed to the client to allow multimedia recovery at the client machine.   
 * For a complete overview of the log and recovery manger see the file log.c   
 * 
 */

#ifndef _LOGCP_HEADER_
#define _LOGCP_HEADER_

#ident "$Id$"

#include <stdio.h>
#include "common.h"
#include "dbdef.h"		/* DB_TRAN_ISOLATION */
#include "object_representation.h"


#if defined(L_cuserid)
#define LOG_USERNAME_MAX L_cuserid
#else /* L_cuserid */
#define LOG_USERNAME_MAX 9
#endif /* L_cuserid */

#define TRAN_LOCK_INFINITE_WAIT (-1)

#define LOG_ONELOG_PACKED_SIZE (OR_INT_SIZE * 3)

#define LOG_MANYLOGS_PTR_IN_LOGAREA(log_areaptr)                         \
  ((struct manylogs *) ((char *)log_areaptr->mem + log_areaptr->length - \
                                  DB_SIZEOF(struct manylogs)))

/*
 * STATES OF TRANSACTIONS                            
 */
typedef enum
{
  TRAN_RECOVERY,		/* State of a system 
				 *  transaction which is
				 *  used for recovery
				 *  purposes. For example
				 *  , set lock for
				 *  damaged pages.
				 */
  TRAN_ACTIVE,			/* Active transaction */
  TRAN_UNACTIVE_COMMITTED,	/* Transaction is in 
				   the commit process
				   or has been comitted
				 */
  TRAN_UNACTIVE_WILL_COMMIT,	/* Transaction will be
				   committed
				 */
  TRAN_UNACTIVE_COMMITTED_WITH_POSTPONE,	/* Transaction has been
						   committed, but it is 
						   still executing
						   postpone operations
						 */
  TRAN_UNACTIVE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS,	/* Transaction has being
							   committed, but it is
							   still executing
							   postpone operations
							   on the client machine
							   (e.g., mutimedia
							   external files)
							 */
  TRAN_UNACTIVE_TOPOPE_COMMITTED_WITH_POSTPONE,	/* In the process of
						   executing postpone
						   top system operations
						 */
  TRAN_UNACTIVE_XTOPOPE_COMMITTED_WITH_CLIENT_USER_LOOSE_ENDS,
  /* In the process of
     executing postpone
     client top system
     operations
   */
  TRAN_UNACTIVE_ABORTED,	/* Transaction is in the
				   abort process or has
				   been aborted
				 */
  TRAN_UNACTIVE_UNILATERALLY_ABORTED,	/* Transaction was
					   active a the time of
					   a system crash. The
					   transaction is
					   unilaterally aborted
					   by the system
					 */
  TRAN_UNACTIVE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS,	/* Transaction has being
							   aborted, but it is
							   still executing undo 
							   operations on the
							   client machine (e.g.,
							   mutimedia external
							   files)
							 */
  TRAN_UNACTIVE_TOPOPE_ABORTED_WITH_CLIENT_USER_LOOSE_ENDS,
  /* In the process of
     executing undo
     client top system
     operations
   */
  TRAN_UNACTIVE_2PC_PREPARE,	/* Local part of the
				   distributed
				   transaction is ready
				   to commit. (It will
				   not be unilaterally
				   aborted by the
				   system)
				 */

  TRAN_UNACTIVE_2PC_COLLECTING_PARTICIPANT_VOTES,	/* First phase of 2PC
							   protocol. Transaction
							   is collecting votes
							   from participants
							 */

  TRAN_UNACTIVE_2PC_ABORT_DECISION,	/* Second phase of 2PC
					   protocol. Transaction
					   needs to be aborted
					   both locally and
					   globally.
					 */

  TRAN_UNACTIVE_2PC_COMMIT_DECISION,	/* Second phase of 2PC 
					   protocol. Transaction
					   needs to be committed
					   both locally and
					   globally.
					 */

  TRAN_UNACTIVE_COMMITTED_INFORMING_PARTICIPANTS,	/* Transaction has been 
							   committed, and it is 
							   informing 
							   participants about
							   the decision.
							 */
  TRAN_UNACTIVE_ABORTED_INFORMING_PARTICIPANTS,	/* Transaction has been 
						   aborted, and it is 
						   informing 
						   participants about
						   the decision.
						 */

  TRAN_UNACTIVE_UNKNOWN		/* Unknown state. */
} TRAN_STATE;

/*
 * RESULT OF NESTED TOP OPERATION                        
 */

typedef enum
{
  LOG_RESULT_TOPOP_COMMIT,
  LOG_RESULT_TOPOP_ABORT,
  LOG_RESULT_TOPOP_ATTACH_TO_OUTER
} LOG_RESULT_TOPOP;

/*
 *                                                                             
 *            TRANSACTION ISOLATION LEVELS (DEGREE OF CONSISTENCY)	       
 * 									       
 * The isolation level of a transaction is a measure of the degree of 	       
 * interference that the transaction is willing to tolerate from other 	       
 * concurrent transactions. The higher the isolation level, the less 	       
 * interference and the lower the concurrency. See lk.c for details..	       
 *                                                                             
 * REPEATABLE READ CLASS AND REPEATABLE READ INSTANCE:			       
 *      or  SERIALIZABLE:						       
 *      or  DEGREE 3 CONSISTENCY:					       
 * ---------------------------------------------------			       
 * This isolation level isolates the transaction from dirty relationships      
 * among objects (i.e., from inconsistencies due to concurrency).	       
 * 									       
 * REPEATABLE READ CLASS AND READ COMMITTED INSTANCES			       
 *      or CURSOR STABILITY						       
 * ---------------------------------------------------			       
 * This isolation level isolates the transaction from the dirty objects of     
 * other transactions and prevents other transactions (which follow rule a)    
 * from changing the classes (part of the schema) accessed by the interested   
 * transaction. However, the transaction may experience some non-repeatable    
 * reads on instances. That is, the transaction may read two different	       
 * (committed) values of the same object if it reads the same object twice.    
 * 									       
 * REPEATABLE READ CLASS AND READ UNCOMMITTED INSTANCES			       
 * ----------------------------------------------------			       
 * This isolation level isolates the transaction from the dirty classes of     
 * other transactions and prevents other transactions (which follow rule a)    
 * from changing the classes (part of the schema) accessed by the interested   
 * transaction. This isolation level allows a transaction reading dirty	       
 * (uncommitted) instances which may be subsequently updated or rolled back.   
 * 									       
 * READ COMMITTED CLASS AND READ COMMITTED INSTANCES			       
 *      or DEGREE_2_CONSISTENCY						       
 * -------------------------------------------------			       
 * This isolation level isolates the transaction from the dirty objects	       
 * (either classes or instances) of other transactions. It does not prevent    
 * other transactions from changing the classes and instances that have been   
 * read. The transaction may experience some non-repeatable reads on both      
 * classes (schema) and instances. 					       
 * 									       
 *   READ COMMITTED CLASS AND READ UNCOMMITTED INSTANCES		       
 *      or DEGREE_1_CONSISTENCY						       
 * -----------------------------------------------------		       
 * This isolation level isolates the transaction from the dirty classes of     
 * other transactions and allows the transaction reading dirty (uncommitted)   
 * instances which may be subsequently updated or rolled back. The transaction 
 * may experience some non-repeatable reads on both classes (schema) and       
 * instances.								       
 * 									       
 * DEGREE_0_CONSISTENCY: 						       
 * This degree of consistency prevents a transaction from been recoverable     
 * (i.e., it cannot be rolled back) because it commits data before the	       
 * transaction has finished. CUBRID does not support this level of 	       
 * isolation.								       
 * 									       
 * The following isolation levels are not possible:			       
 *   READ COMMITTED CLASS AND REPEATABLE READ INSTANCES			       
 *   READ UNCOMMITTED CLASS AND REPEATABLE READ INSTANCES		       
 *     If the structure of the class is changed, the instances cannot be       
 *     repeatable.							       
 *   READ UNCOMMITTED CLASS AND READ COMMITTED INSTANCES		       
 *   READ UNCOMMITTED CLASS AND READ UNCOMMITTED INSTANCES		       
 *     The behaviour of this isolation level is not well-understood and the    
 *     system may be probe to crashes. For example, in the middle of a query   
 *     the class is deleted and removed. Heap file may be gone and given to    
 *     someone else.							       
 * 									       
 * See lk.c for more details on isolation levels...			       
 * 									       
 * 
 * [GRAY75] Gray, J. et al. "Granularity of Locks and Degrees of Consistency   
 *          in a Shared Database," IBM Research Report RJ1654, September 1975. 
 * 									       
 * [PAPA86] Papadimitriou, C. The theory of Database Concurrency Control,      
 *          Computer Science Press, Rockville, Maryland, USA, 1986.	       
 * 									       
 */

/*
 * The definition of TRAN_ISOLATION has been moved to api/dbdef.h 
 * because it is visible through the API. 
 */

/* name used by the internal modules */
typedef DB_TRAN_ISOLATION TRAN_ISOLATION;
typedef int LOG_RCVCLIENT_INDEX;	/* Index to recovery functions */

typedef struct log_copy LOG_COPY;
struct log_copy
{				/* Copy area for flushing and fetching */
  char *mem;			/* Pointer to location of chunk of area */
  int length;			/* The size of the area                 */
};

struct onelog
{				/* One data record to recovery */
  LOG_RCVCLIENT_INDEX rcvindex;	/* Recovery function to apply */
  int length;			/* Length of the data         */
  int offset;			/* Offset to data             */
};

struct manylogs
{
  struct onelog onelog;
  int num_logs;			/* How many log recovery records are described */
};


extern const char *log_state_string (TRAN_STATE state);
extern const char *log_isolation_string (TRAN_ISOLATION isolation);
extern LOG_COPY *log_alloc_client_copy_area (int length);
extern void log_free_client_copy_area (LOG_COPY * copy_area);
extern char *log_pack_descriptors (int num_records, LOG_COPY * log_area,
				   char *descriptors);
extern char *log_unpack_descriptors (int num_records, LOG_COPY * log_area,
				     char *descriptors);
extern int
log_copy_area_send (LOG_COPY * log_area, char **contents_ptr,
		    int *contents_length, char **descriptors_ptr,
		    int *descriptors_length);
extern LOG_COPY *log_copy_area_malloc_recv (int num_records,
					    char **packed_descriptors,
					    int packed_descriptors_length,
					    char **contents_ptr,
					    int contents_length);
#endif
