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
 * cas_error.h -
 */

#ifndef	_CAS_ERROR_H_
#define	_CAS_ERROR_H_

#ident "$Id$"

#ifdef __cplusplus
extern "C"
{
#endif


  typedef enum
  {
    CAS_ER_GLO = -999,
    CAS_ER_DBMS = -1000,
    CAS_ER_INTERNAL = -1001,
    CAS_ER_NO_MORE_MEMORY = -1002,
    CAS_ER_COMMUNICATION = -1003,
    CAS_ER_ARGS = -1004,
    CAS_ER_TRAN_TYPE = -1005,
    CAS_ER_SRV_HANDLE = -1006,
    CAS_ER_NUM_BIND = -1007,
    CAS_ER_UNKNOWN_U_TYPE = -1008,
    CAS_ER_DB_VALUE = -1009,
    CAS_ER_TYPE_CONVERSION = -1010,
    CAS_ER_PARAM_NAME = -1011,
    CAS_ER_NO_MORE_DATA = -1012,
    CAS_ER_OBJECT = -1013,
    CAS_ER_OPEN_FILE = -1014,
    CAS_ER_SCHEMA_TYPE = -1015,
    CAS_ER_VERSION = -1016,
    CAS_ER_FREE_SERVER = -1017,
    CAS_ER_NOT_AUTHORIZED_CLIENT = -1018,
    CAS_ER_QUERY_CANCEL = -1019,
    CAS_ER_NOT_COLLECTION = -1020,
    CAS_ER_COLLECTION_DOMAIN = -1021,

    CAS_ER_NO_MORE_RESULT_SET = -1022,
    CAS_ER_INVALID_CALL_STMT = -1023,
    CAS_ER_STMT_POOLING = -1024,
    CAS_ER_DBSERVER_DISCONNECTED = -1025,
    CAS_ER_GLO_CMD = -1026,
    CAS_ER_NOT_IMPLEMENTED = -1100,

    CAS_ER_IS = -1200,
  } T_CAS_ERROR_CODE;


#ifdef __cplusplus
}
#endif

#endif				/* _CAS_ERROR_H_ */
