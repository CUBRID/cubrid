/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
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
    CAS_ER_MAX_PREPARED_STMT_COUNT_EXCEEDED = -1026,
    CAS_ER_NOT_IMPLEMENTED = -1100,

    CAS_ER_IS = -1200,
  } T_CAS_ERROR_CODE;


#ifdef __cplusplus
}
#endif

#endif				/* _CAS_ERROR_H_ */
