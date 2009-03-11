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
 * cm_diag_client_request.h - 
 */

#ifndef _CM_DIAG_CLIENT_REQUEST_H_
#define _CM_DIAG_CLIENT_REQUEST_H_

#ident "$Id$"

/*****************************************
 * Client info MACRO
 *****************************************/
#define NEED_MON_CUB(X)  \
    (   NEED_MON_CUB_QUERY(X) \
     || NEED_MON_CUB_LOCK(X) \
     || NEED_MON_CUB_CONNECTION(X) \
     || NEED_MON_CUB_BUFFER(X) \
     || NEED_MON_CUB_LOG(X) \
     || NEED_MON_CUB_REPLICATION(X) \
     || NEED_MON_CUB_MASTER(X))

/*****************************************
 * 1. cas head byte
 *****************************************/
#define TCL_H_MON_CAS                       0x01
#define TCL_H_ACT_CAS                       0x02

#define SET_CLIENT_MONITOR_INFO_CAS(X)          (X).cas.head|=TCL_H_MON_CAS
#define SET_CLIENT_ACTINFO_CAS(X)             (X).cas.head|=TCL_H_ACT_CAS
#define NEED_MON_CAS(X)                       ((X).cas.head&TCL_H_MON_CAS)
#define NEED_ACT_CAS(X)                       ((X).cas.head&TCL_H_ACT_CAS)

/*****************************************
 * 2. cas body [0] byte
 *****************************************/
#define TCL_MON_CAS_REQ                     0x01
#define TCL_MON_CAS_ACTIVE_SESSION          0x02
#define TCL_MON_CAS_TRAN                    0x04

#define SET_CLIENT_MONITOR_INFO_CAS_REQ(X)    (X).cas.body[0]|=TCL_MON_CAS_REQ
#define SET_CLIENT_MONITOR_INFO_CAS_TRAN(X)   (X).cas.body[0]|=TCL_MON_CAS_TRAN
#define SET_CLIENT_MONITOR_INFO_CAS_ACTIVE_SESSION(X) \
                                    (X).cas.body[0]|=TCL_MON_CAS_ACTIVE_SESSION
#define NEED_MON_CAS_REQ(X)         ((X).cas.body[0]&TCL_MON_CAS_REQ)
#define NEED_MON_CAS_TRAN(X)        ((X).cas.body[0]&TCL_MON_CAS_TRAN)
#define NEED_MON_CAS_ACT_SESSION(X) ((X).cas.body[0]&TCL_MON_CAS_ACTIVE_SESSION)

/*****************************************
 * 3. cas body [1] byte
 *****************************************/
#define TCL_ACT_CAS_REQ                     0x01
#define TCL_ACT_CAS_TRAN                    0x02

#define SET_CLIENT_ACTINFO_CAS_REQ(X)        (X).cas.body[1]|=TCL_ACT_CAS_REQ
#define SET_CLIENT_ACTINFO_CAS_TRAN(X)       (X).cas.body[1]|=TCL_ACT_CAS_TRAN
#define NEED_ACT_CAS_REQ(X)             ((X).cas.body[1]&TCL_ACT_CAS_REQ)
#define NEED_ACT_CAS_TRAN(X)            ((X).cas.body[1]&TCL_ACT_CAS_TRAN)

/* this is used in cas.c */
#define NEED_ACT_REQ(X)                 ((X).body[1]&TCL_ACT_CAS_REQ)
#define NEED_ACT_TRAN(X)                ((X).body[1]&TCL_ACT_CAS_TRAN)

/*****************************************
 * 4. server head [0] byte
 *****************************************/
#define TCL_H_MON_CUB_INSTANCE              0x01
#define TCL_H_MON_CUB_QUERY                 0x02
#define TCL_H_MON_CUB_LOCK                  0x04
#define TCL_H_MON_CUB_CONNECTION            0x08
#define TCL_H_MON_CUB_BUFFER                0x10
#define TCL_H_MON_CUB_LOG                   0x20
#define TCL_H_MON_CUB_REPLICATION           0x40
#define TCL_H_MON_CUB_MASTER                0x80

#define SET_CLIENT_MONITOR_INFO_CUB_INSTANCE(X)   (X).server.head[0]|=TCL_H_MON_CUB_INSTANCE
#define SET_CLIENT_MONITOR_INFO_CUB_QUERY(X)      (X).server.head[0]|=TCL_H_MON_CUB_QUERY
#define SET_CLIENT_MONITOR_INFO_CUB_LOCK(X)       (X).server.head[0]|=TCL_H_MON_CUB_LOCK
#define SET_CLIENT_MONITOR_INFO_CUB_CONNECTION(X)\
                                            (X).server.head[0]|=TCL_H_MON_CUB_CONNECTION
#define SET_CLIENT_MONITOR_INFO_CUB_BUFFER(X)     (X).server.head[0]|=TCL_H_MON_CUB_BUFFER
#define SET_CLIENT_MONITOR_INFO_CUB_LOG(X)        (X).server.head[0]|=TCL_H_MON_CUB_LOG
#define SET_CLIENT_MONITOR_INFO_CUB_REPLICATION(X) \
                                            (X).server.head[0]|=TCL_H_MON_CUB_REPLICATION
#define SET_CLIENT_MONITOR_INFO_CUB_MASTER(X)     (X).server.head[0]|=TCL_H_MON_CUB_MASTER

#define NEED_MON_CUB_INSTANCE(X)        ((X).server.head[0]&TCL_H_MON_CUB_INSTANCE)
#define NEED_MON_CUB_QUERY(X)           ((X).server.head[0]&TCL_H_MON_CUB_QUERY)
#define NEED_MON_CUB_LOCK(X)            ((X).server.head[0]&TCL_H_MON_CUB_LOCK)
#define NEED_MON_CUB_CONNECTION(X)      ((X).server.head[0]&TCL_H_MON_CUB_CONNECTION)
#define NEED_MON_CUB_BUFFER(X)          ((X).server.head[0]&TCL_H_MON_CUB_BUFFER)
#define NEED_MON_CUB_LOG(X)             ((X).server.head[0]&TCL_H_MON_CUB_LOG)
#define NEED_MON_CUB_REPLICATION(X)     ((X).server.head[0]&TCL_H_MON_CUB_REPLICATION)
#define NEED_MON_CUB_MASTER(X)          ((X).server.head[0]&TCL_H_MON_CUB_MASTER)

/*****************************************
 * 5. server head [1] byte
 *****************************************/
#define TCL_H_ACT_CUB                       0x01

#define SET_CLIENT_ACTINFO_CUB(X)             (X).server.head[1]|=TCL_H_ACT_CUB
#define NEED_ACT_CUB(X)                       ((X).server.head[1]&TCL_H_ACT_CUB)


/******************************************
 * 6. server body [0] byte
 ******************************************/
#define TCL_MON_CUB_QUERY_OPEN_PAGE         0x01
#define TCL_MON_CUB_QUERY_OPENED_PAGE       0x02
#define TCL_MON_CUB_QUERY_SLOW_QUERY        0x04
#define TCL_MON_CUB_QUERY_FULL_SCAN         0x08
#define TCL_MON_CUB_CONN_CLI_REQUEST        0x10
#define TCL_MON_CUB_CONN_ABORTED_CLIENTS    0x20
#define TCL_MON_CUB_CONN_CONN_REQ           0x40
#define TCL_MON_CUB_CONN_CONN_REJECT        0x80

#define SET_CLIENT_MONITOR_INFO_CUB_QUERY_OPEN_PAGE(X) \
                        (X).server.body[0]|=TCL_MON_CUB_QUERY_OPEN_PAGE
#define SET_CLIENT_MONITOR_INFO_CUB_QUERY_OPENED_PAGE(X)\
                        (X).server.body[0]|=TCL_MON_CUB_QUERY_OPENED_PAGE
#define SET_CLIENT_MONITOR_INFO_CUB_QUERY_SLOW_QUERY(X) \
                        (X).server.body[0]|=TCL_MON_CUB_QUERY_SLOW_QUERY
#define SET_CLIENT_MONITOR_INFO_CUB_QUERY_FULL_SCAN(X)  \
                        (X).server.body[0]|=TCL_MON_CUB_QUERY_FULL_SCAN
#define SET_CLIENT_MONITOR_INFO_CUB_CONN_CLI_REQUEST(X) \
                        (X).server.body[0]|=TCL_MON_CUB_CONN_CLI_REQUEST
#define SET_CLIENT_MONITOR_INFO_CUB_CONN_ABORTED_CLIENTS(X) \
                        (X).server.body[0]|=TCL_MON_CUB_CONN_ABORTED_CLIENTS
#define SET_CLIENT_MONITOR_INFO_CUB_CONN_CONN_REQ(X)   \
                        (X).server.body[0]|=TCL_MON_CUB_CONN_CONN_REQ
#define SET_CLIENT_MONITOR_INFO_CUB_CONN_CONN_REJECT(X)     \
                        (X).server.body[0]|=TCL_MON_CUB_CONN_CONN_REJECT

#define NEED_MON_CUB_QUERY_OPEN_PAGE(X) \
                        ((X).server.body[0]&TCL_MON_CUB_QUERY_OPEN_PAGE)
#define NEED_MON_CUB_QUERY_OPENED_PAGE(X) \
                        ((X).server.body[0]&TCL_MON_CUB_QUERY_OPENED_PAGE)
#define NEED_MON_CUB_QUERY_SLOW_QUERY(X) \
                        ((X).server.body[0]&TCL_MON_CUB_QUERY_SLOW_QUERY)
#define NEED_MON_CUB_QUERY_FULL_SCAN(X)  \
                        ((X).server.body[0]&TCL_MON_CUB_QUERY_FULL_SCAN)
#define NEED_MON_CUB_CONN_CLI_REQUEST(X) \
                        ((X).server.body[0]&TCL_MON_CUB_CONN_CLI_REQUEST)
#define NEED_MON_CUB_CONN_ABORTED_CLIENTS(X) \
                        ((X).server.body[0]&TCL_MON_CUB_CONN_ABORTED_CLIENTS)
#define NEED_MON_CUB_CONN_CONN_REQ(X) \
                        ((X).server.body[0]&TCL_MON_CUB_CONN_CONN_REQ)
#define NEED_MON_CUB_CONN_CONN_REJECT(X) \
                        ((X).server.body[0]&TCL_MON_CUB_CONN_CONN_REJECT)

/******************************************
 * 7. server body [1] byte
 ******************************************/
#define TCL_MON_CUB_BUFFER_PAGE_WRITE       0x01
#define TCL_MON_CUB_BUFFER_PAGE_READ        0x02
#define TCL_MON_CUB_LOCK_DEADLOCK           0x04
#define TCL_MON_CUB_LOCK_REQUEST            0x08

#define SET_CLIENT_MONITOR_INFO_CUB_BUFFER_PAGE_WRITE(X) \
                        (X).server.body[1]|=TCL_MON_CUB_BUFFER_PAGE_WRITE
#define SET_CLIENT_MONITOR_INFO_CUB_BUFFER_PAGE_READ(X)  \
                        (X).server.body[1]|=TCL_MON_CUB_BUFFER_PAGE_READ
#define SET_CLIENT_MONITOR_INFO_CUB_LOCK_DEADLOCK(X)     \
                        (X).server.body[1]|=TCL_MON_CUB_LOCK_DEADLOCK
#define SET_CLIENT_MONITOR_INFO_CUB_LOCK_REQUEST(X)      \
                        (X).server.body[1]|=TCL_MON_CUB_LOCK_REQUEST

#define NEED_MON_CUB_BUFFER_PAGE_WRITE(X)     \
                        ((X).server.body[1]&TCL_MON_CUB_BUFFER_PAGE_WRITE)
#define NEED_MON_CUB_BUFFER_PAGE_READ(X)      \
                        ((X).server.body[1]&TCL_MON_CUB_BUFFER_PAGE_READ)
#define NEED_MON_CUB_LOCK_DEADLOCK(X)         \
                        ((X).server.body[1]&TCL_MON_CUB_LOCK_DEADLOCK)
#define NEED_MON_CUB_LOCK_REQUEST(X)          \
                        ((X).server.body[1]&TCL_MON_CUB_LOCK_REQUEST)


/******************************************
 * 8. server body [4] byte - act info
 ******************************************/
#define TCL_ACT_CUB_QUERY_FULLSCAN          0x01
#define TCL_ACT_CUB_LOCK_DEADLOCK           0x02
#define TCL_ACT_CUB_BUFFER_PAGE_READ        0x04
#define TCL_ACT_CUB_BUFFER_PAGE_WRITE       0x08

#define SET_CLIENT_ACTINFO_CUB_QUERY_FULLSCAN(X)    \
                               (X).server.body[4]|=TCL_ACT_CUB_QUERY_FULLSCAN
#define SET_CLIENT_ACTINFO_CUB_LOCK_DEADLOCK(X)     \
                               (X).server.body[4]|=TCL_ACT_CUB_LOCK_DEADLOCK
#define SET_CLIENT_ACTINFO_CUB_BUFFER_PAGE_READ(X)  \
                               (X).server.body[4]|=TCL_ACT_CUB_BUFFER_PAGE_READ
#define SET_CLIENT_ACTINFO_CUB_BUFFER_PAGE_WRITE(X) \
                               (X).server.body[4]|=TCL_ACT_CUB_BUFFER_PAGE_WRITE
#define NEED_ACT_CUB_QUERY_FULLSCAN(X)         \
                               ((X).server.body[4]&TCL_ACT_CUB_QUERY_FULLSCAN)
#define NEED_ACT_CUB_LOCK_DEADLOCK(X)          \
                               ((X).server.body[4]&TCL_ACT_CUB_LOCK_DEADLOCK)
#define NEED_ACT_CUB_BUFFER_PAGE_READ(X)       \
                               ((X).server.body[4]&TCL_ACT_CUB_BUFFER_PAGE_READ)
#define NEED_ACT_CUB_BUFFER_PAGE_WRITE(X)      \
                               ((X).server.body[4]&TCL_ACT_CUB_BUFFER_PAGE_WRITE)

/* this is used in server only */
#define NEED_ACT_QUERY_FULLSCAN(X)    ((X).body[4]&TCL_ACT_CUB_QUERY_FULLSCAN)
#define NEED_ACT_LOCK_DEADLOCK(X)     ((X).body[4]&TCL_ACT_CUB_LOCK_DEADLOCK)
#define NEED_ACT_BUFFER_PAGE_READ(X)  ((X).body[4]&TCL_ACT_CUB_BUFFER_PAGE_READ)
#define NEED_ACT_BUFFER_PAGE_WRITE(X) ((X).body[4]&TCL_ACT_CUB_BUFFER_PAGE_WRITE)

int init_monitor_config (T_CLIENT_MONITOR_CONFIG * c_config);
int init_cas_monitor_config (MONITOR_CAS_CONFIG * c_cas);
int init_server_monitor_config (MONITOR_SERVER_CONFIG * c_server);

#endif /* _CM_DIAG_CLIENT_REQUEST_H_ */
