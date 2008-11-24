/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */


/*
 * network.h -Definitions for client/server network support.
 */

#ifndef _NETWORK_H_
#define _NETWORK_H_

#ident "$Id$"

#include "query_opfunc.h"
#include "perf_monitor.h"
#include "locator.h"
#include "log_comm.h"
#include "thread_impl.h"


/* Name sizes */
#define MAX_SERVER_HOST_NAME 256
#define MAX_SERVER_NAME      256


/* Server statistics structure size, used to make sure the pack/unpack
   routines follow the current structure definition.
   This must be the byte size of the structure
   as returned by sizeof().  Note that MEMORY_STAT_SIZE and PACKED_STAT_SIZE
   are not necesarily the same although they will be in most cases.
*/
#define STAT_SIZE_MEMORY 72
#define STAT_SIZE_PACKED OR_INT_SIZE * 18

#define NET_CLIENT_SERVER_HAND_SHAKE "$Id$"
#define NET_PINGBUF_SIZE 300


/* These define the reqests that the server will respond to */
#define SERVER_PING_WITH_HANDSHAKE_XXXXXXUNUSED               1
#define SERVER_BO_INIT_SERVER		                      2
#define SERVER_BO_RESTART_SERVER	                      3
#define SERVER_BO_REGISTER_CLIENT                             4
#define SERVER_BO_UNREGISTER_CLIENT                           5
#define SERVER_BO_SIMULATE_SERVER_CRASH                       6
#define SERVER_BO_BACKUP				      7
#define SERVER_BO_ADD_VOLEXT				      8
#define SERVER_BO_CHECK_DBCONSISTENCY			      9
#define SERVER_BO_FIND_NPERM_VOLS                            10

#define SERVER_TM_SERVER_COMMIT		                     11
#define SERVER_TM_SERVER_ABORT		                     12
#define SERVER_TM_SERVER_START_TOPOP                         13
#define SERVER_TM_SERVER_END_TOPOP                           14
#define SERVER_TM_SERVER_SAVEPOINT                           15
#define SERVER_TM_SERVER_PARTIAL_ABORT                       16
#define SERVER_TM_SERVER_HAS_UPDATED                         17
#define SERVER_TM_SERVER_ISACTIVE_AND_HAS_UPDATED            18
#define SERVER_TM_ISBLOCKED	                             19
#define SERVER_TM_WAIT_SERVER_ACTIVE_TRANS                   20

#define SERVER_LC_FETCH			                     22
#define SERVER_LC_FETCHALL		                     23
#define SERVER_LC_FETCH_LOCKSET  			     24
#define SERVER_LC_FETCH_ALLREFS_LOCKSET                      25
#define SERVER_LC_GET_CLASS		                     26
#define SERVER_LC_FIND_CLASSOID		                     27
#define SERVER_LC_DOESEXIST                                  28
#define SERVER_LC_FORCE			                     29
#define SERVER_LC_RESERVE_CLASSNAME	                     30
#define SERVER_LC_DELETE_CLASSNAME	                     31
#define SERVER_LC_RENAME_CLASSNAME	                     32
#define SERVER_LC_ASSIGN_OID		                     33
#define SERVER_LC_NOTIFY_ISOLATION_INCONS                    34
#define SERVER_LC_FIND_LOCKHINT_CLASSOIDS                    35
#define SERVER_LC_FETCH_LOCKHINT_CLASSES                     36

#define SERVER_HEAP_CREATE		                     37
#define SERVER_HEAP_DESTROY		                     39
#define SERVER_HEAP_DESTROY_WHEN_NEW                           40

#define SERVER_LARGEOBJMGR_CREATE 		                     41
#define SERVER_LARGEOBJMGR_READ 		                     42
#define SERVER_LARGEOBJMGR_WRITE 		                     43
#define SERVER_LARGEOBJMGR_INSERT 		                     44
#define SERVER_LARGEOBJMGR_DESTROY		                     45
#define SERVER_LARGEOBJMGR_DELETE		                     46
#define SERVER_LARGEOBJMGR_APPEND		                     47
#define SERVER_LARGEOBJMGR_TRUNCATE		                     48
#define SERVER_LARGEOBJMGR_COMPRESS		                     49
#define SERVER_LARGEOBJMGR_LENGTH		                     50

#define SERVER_LOG_RESET_WAITSECS                            51
#define SERVER_LOG_RESET_ISOLATION                           52
#define SERVER_LOG_SET_INTERRUPT                             53
#define SERVER_LOG_CLIENT_UNDO                               54
#define SERVER_LOG_CLIENT_POSTPONE                           55
#define SERVER_LOG_HAS_FINISHED_CLIENT_POSTPONE              56
#define SERVER_LOG_HAS_FINISHED_CLIENT_UNDO                  57
#define SERVER_LOG_CLIENT_GET_FIRST_POSTPONE                 58
#define SERVER_LOG_CLIENT_GET_FIRST_UNDO                     59
#define SERVER_LOG_CLIENT_GET_NEXT_POSTPONE                  60
#define SERVER_LOG_CLIENT_GET_NEXT_UNDO                      61
#define SERVER_LOG_CLIENT_UNKNOWN_STATE_ABORT_GET_FIRST_UNDO 62

#define SERVER_LK_DUMP                                       63

#define SERVER_BTREE_ADDINDEX                                   64
#define SERVER_BTREE_DELINDEX                                   65
#define SERVER_BTREE_LOADINDEX				     66

#define SERVER_BTREE_FIND_UNIQUE				     69
#define SERVER_BTREE_CLASS_UNIQUE_TEST                          70

#define SERVER_DISK_TOTALPGS                                   71
#define SERVER_DISK_FREEPGS                                    72
#define SERVER_DISK_REMARKS                                    73
#define SERVER_DISK_PURPOSE                                    74
#define SERVER_DISK_PURPOSE_TOTALPGS_AND_FREEPGS               75
#define SERVER_DISK_VLABEL                                     76

#define SERVER_QST_SERVER_GET_STATISTICS                     77
#define SERVER_QST_UPDATE_CLASS_STATISTICS                   78
#define SERVER_QST_UPDATE_STATISTICS                         79

#define SERVER_QM_QUERY_PREPARE				     80
#define SERVER_QM_QUERY_EXECUTE				     81
#define SERVER_QM_QUERY_PREPARE_AND_EXECUTE		     82
#define SERVER_QM_QUERY_END				     83
#define SERVER_QM_QUERY_DROP_PLAN			     84
#define SERVER_QM_QUERY_DROP_ALL_PLANS			     85

#define SERVER_LS_GET_LIST_FILE_PAGE                         86

#define SERVER_MNT_SERVER_START_STATS			     87
#define SERVER_MNT_SERVER_STOP_STATS			     88
#define SERVER_MNT_SERVER_RESET_STATS			     89
#define SERVER_MNT_SERVER_COPY_STATS			     90

#define SERVER_CT_CAN_ACCEPT_NEW_REPR			     91

#define SERVER_BO_KILL_SERVER                                92
#define SERVER_BO_SIMULATE_SERVER                            93
#define SERVER_TEST_PERFORMANCE                              94

#define SERVER_SET_CLIENT_TIMEOUT                            95
#define SERVER_RESTART_EVENT_HANDLER                         96
#define SERVER_CSS_KILL_TRANSACTION                          97
#define SERVER_LOG_GETPACK_TRANTB			     98

#define SERVER_LC_ASSIGN_OID_BATCH                           99

#define SERVER_MSQL_START                                    100
#define SERVER_MSQL_END                                      200

#define SERVER_BO_FIND_NTEMP_VOLS                            201
#define SERVER_BO_FIND_LAST_TEMP                             202

#define SERVER_LC_REM_CLASS_FROM_INDEX                       203

#define SERVER_TM_SERVER_GET_GTRINFO                         210
#define SERVER_TM_SERVER_SET_GTRINFO                         211
#define SERVER_TM_SERVER_2PC_START                           212
#define SERVER_TM_SERVER_2PC_PREPARE                         213
#define SERVER_TM_SERVER_2PC_RECOVERY_PREPARED               214
#define SERVER_TM_SERVER_2PC_ATTACH_GT                       215
#define SERVER_TM_SERVER_2PC_PREPARE_GT                      216

/*
 * New codes for Streaming Queries
 */
#define SERVER_QM_QUERY_SYNC			     	     296
#define SERVER_QM_GET_QUERY_INFO			     297
#define SERVER_QM_QUERY_EXECUTE_ASYNC			     298
#define SERVER_QM_QUERY_PREPARE_AND_EXECUTE_ASYNC	     299

/*
 * New code for getting SYS_DATE, SYS_TIME, SYS_TIMESTAMP value
 */
#define SERVER_QPROC_GET_SYS_TIMESTAMP                          400
#define SERVER_QPROC_GET_CURRENT_VALUE                          401
#define SERVER_QPROC_GET_NEXT_VALUE                             402

#define SERVER_HEAP_GET_CLASS_NOBJS_AND_NPAGES                 410
#define SERVER_BTREE_GET_STATISTICS                             411
#define SERVER_HEAP_HAS_INSTANCE                               415

#define SERVER_QPROC_GET_SERVER_INFO			     420

#define	SERVER_TM_LOCAL_TRANSACTION_ID                       425

/*
 * for SET SYSTEM PARAMETERS statement
 */
#define SERVER_PRM_SET_PARAMETERS                            500
#define SERVER_PRM_GET_PARAMETERS                            501

#define SERVER_QM_QUERY_DUMP_PLANS                           502
#define SERVER_QM_QUERY_DUMP_CACHE                           503

/* AsyncCommit */
#define SERVER_LOG_DUMP_STAT                                 504


/*
 * for stored procedure
*/
#define SERVER_JSP_GET_SERVER_PORT                           600

/*
 * for replication
 */
#define SERVER_REPL_INFO                                     700
#define SERVER_REPL_LOG_GET_APPEND_LSA                       701

/* for FK */
#define SERVER_LC_BUILD_FK_OBJECT_CACHE                      800

#define SERVER_PING_WITH_HANDSHAKE                           999	/* NEW PING */

/* this is the last entry. It is also used for the end of an
 * array of statistics information on client/server communication.
 * Consequently, considerable care should be taken in changing this
 * value.
 */
#define SERVER_SHUTDOWN					     1000

/* Server capabilities */
#define NET_INTERRUPT_ENABLED_CAP 0x80000000

/* for the generated interface routines */
typedef const char CONSTCHAR;

extern unsigned int net_Interrupt_enabled;

extern char *net_pack_stats (char *buf, MNT_SERVER_EXEC_STATS * stats);
extern char *net_unpack_stats (char *buf, MNT_SERVER_EXEC_STATS * stats);

/* Server startup */
extern int net_server_start (const char *name);

#endif /* _NETWORK_H_ */
