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

#ifndef	__ODBC_DIAG_HEADER	/* to avoid multiple inclusion */
#define	__ODBC_DIAG_HEADER

#include		"cas_cci.h"

#define		ODBC_DIAG_INIT			0
#define		ODBC_DIAG_RESET			1
#define		ODBC_DIAG_FREE_MEMBER	2
#define		ODBC_DIAG_FREE_ALL		3

#define		ODBC_DIAG_RECORD_INIT			0
#define		ODBC_DIAG_RECORD_RESET			1
#define		ODBC_DIAG_RECORD_FREE_MEMBER	2
#define		ODBC_DIAG_RECORD_FREE_ALL		3


/*-----------------------------------------------------------------------
			CUBRID ODBC specific error code
 *-----------------------------------------------------------------------*/

/* error code
 * -1 error, -2 waring, -3 unknown, ... ~ -99 reserved
 * -1000 ~ -4999 odbc error
 * -5000 ~ -9999  reserved
 * -10000 ~ -11999 CUBRID odbc error
 * -12000 ~ -12999 CAS CCI error
 * available error code is to -20000
 */

/* state
 *	"00"  - general
 *	"UN"  - CUBRID ODBC
 *	"CA"  - CAS CCI
 */

#define ODBC_OK				0
#define	ODBC_ERROR_OFFSET			(-10000)
#define	ODBC_GENERAL_ERROR			(-1 + ODBC_ERROR_OFFSET)
#define	ODBC_WARING					(-2 + ODBC_ERROR_OFFSET)
#define	ODBC_UNKNOWN				(-3 + ODBC_ERROR_OFFSET)
#define ODBC_NULL_VALUE				(-4 + ODBC_ERROR_OFFSET)
#define ODBC_CAS_ERROR				(-6 + ODBC_ERROR_OFFSET)
#define ODBC_NO_MORE_DATA			(-7 + ODBC_ERROR_OFFSET)
#define ODBC_MEMORY_ALLOC_ERROR		(-8 + ODBC_ERROR_OFFSET)
#define	ODBC_NOT_IMPLEMENTED		(-9 + ODBC_ERROR_OFFSET)
#define	ODBC_UNKNOWN_TYPE			(-10 + ODBC_ERROR_OFFSET)
#define	ODBC_INVALID_TYPE_CONVERSION (-11 + ODBC_ERROR_OFFSET)
#define ODBC_CAS_MOVE_CURSOR		(-50 + ODBC_ERROR_OFFSET)
#define ODBC_CAS_FETCH_CURSOR		(-51 + ODBC_ERROR_OFFSET)
#define ODBC_CAS_GET_DATA			(-52 + ODBC_ERROR_OFFSET)

/*--------------------------------------------------------------------
 *						CAS ERROR CODE
 *	item을 추가할 땐, diag.c의 ...에 message를 추가해줘야 한다.
 *-------------------------------------------------------------------*/


#define	ODBC_CC_ER_NO_ERROR			ODBC_OK
#define	ODBC_CC_ER_OFFSET			(-12000)
#define	ODBC_CC_ER_DBMS				(CCI_ER_DBMS + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_CON_HANDLE		(CCI_ER_CON_HANDLE + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_NO_MORE_MEMORY	(CCI_ER_NO_MORE_MEMORY + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_COMMUNICATION	(CCI_ER_COMMUNICATION + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_NO_MORE_DATA		(CCI_ER_NO_MORE_DATA + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_TRAN_TYPE		(CCI_ER_TRAN_TYPE + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_STR_PARAM		(CCI_ER_STR_PARAM + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_TYPE_CONVERSION	(CCI_ER_TYPE_CONVERSION + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_BIND_INDEX		(CCI_ER_BIND_INDEX + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_ATYPE			(CCI_ER_ATYPE + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_NOT_BIND			(CCI_ER_NOT_BIND + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_PARAM_NAME		(CCI_ER_PARAM_NAME + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_COLUMN_INDEX		(CCI_ER_COLUMN_INDEX + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_SCHEMA_TYPE		(CCI_ER_SCHEMA_TYPE + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_FILE				(CCI_ER_FILE + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_CONNECT			(CCI_ER_CONNECT + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_ALLOC_CON_HANDLE	(CCI_ER_ALLOC_CON_HANDLE + ODBC_CC_ER_OFFSET)
#define	ODBC_CC_ER_REQ_HANDLE		(CCI_ER_REQ_HANDLE + ODBC_CC_ER_OFFSET)
#define ODBC_CC_ER_INVALID_CURSOR_POS (CCI_ER_INVALID_CURSOR_POS + ODBC_CC_ER_OFFSET)
#define ODBC_CC_ER_OBJECT			(CCI_ER_OBJECT + ODBC_CC_ER_OFFSET)
#define ODBC_CC_ER_CAS				(CCI_ER_CAS + ODBC_CC_ER_OFFSET)
#define ODBC_CC_ER_HOSTNAME			(CCI_ER_HOSTNAME + ODBC_CC_ER_OFFSET)
#define ODBC_CC_ER_NOT_IMPLEMENTED	(CCI_ER_NOT_IMPLEMENTED + ODBC_CC_ER_OFFSET)

#define ODBC_CAS_ER_DBMS			(CAS_ER_DBMS + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_INTERNAL				(CAS_ER_INTERNAL + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_NO_MORE_MEMORY				(CAS_ER_NO_MORE_MEMORY + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_COMMUNICATION				(CAS_ER_COMMUNICATION + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_ARGS				(CAS_ER_ARGS + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_TRAN_TYPE				(CAS_ER_TRAN_TYPE + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_SRV_HANDLE				(CAS_ER_SRV_HANDLE + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_NUM_BIND				(CAS_ER_NUM_BIND + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_UNKNOWN_U_TYPE				(CAS_ER_UNKNOWN_U_TYPE + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_DB_VALUE				(CAS_ER_DB_VALUE + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_TYPE_CONVERSION				(CAS_ER_TYPE_CONVERSION + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_PARAM_NAME				(CAS_ER_PARAM_NAME + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_NO_MORE_DATA				(CAS_ER_NO_MORE_DATA + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_OBJECT				(CAS_ER_OBJECT + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_OPEN_FILE				(CAS_ER_OPEN_FILE + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_SCHEMA_TYPE				(CAS_ER_SCHEMA_TYPE + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_VERSION				(CAS_ER_VERSION + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_FREE_SERVER			(CAS_ER_FREE_SERVER + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_NOT_AUTHORIZED_CLIENT (CAS_ER_NOT_AUTHORIZED_CLIENT + ODBC_CC_ER_OFFSET)
#define	ODBC_CAS_ER_QUERY_CANCEL		(CAS_ER_QUERY_CANCEL  + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_NOT_COLLECTION		(CAS_ER_NOT_COLLECTION + ODBC_CC_ER_OFFSET)
#define ODBC_CAS_ER_COLLECTION_DOMAIN	(CAS_ER_COLLECTION_DOMAIN + ODBC_CC_ER_OFFSET)

typedef struct st_odbc_error_map
{
  short code;
  const char *status;
  const char *msg;		/* status에 해당하는 error message */
} ODBC_ERROR_MAP;

typedef struct st_diag_record
{
  int native_code;
  long number;
  char *sql_state;
  char *message;
  struct st_diag_record *next;
} ODBC_DIAG_RECORD;

typedef struct st_diag
{
  long rec_number;
  RETCODE retcode;
  ODBC_DIAG_RECORD *record;
} ODBC_DIAG;

PUBLIC ODBC_DIAG *odbc_alloc_diag (void);
PUBLIC void odbc_free_diag (ODBC_DIAG * diag, int option);
PUBLIC ODBC_DIAG_RECORD *odbc_alloc_diag_record (void);
PUBLIC void odbc_free_diag_record (ODBC_DIAG_RECORD * diag_record,
				   int option);
PUBLIC void odbc_free_diag_record_list (ODBC_DIAG_RECORD * diag_record);
PUBLIC void odbc_set_diag (ODBC_DIAG * diag, char *sql_state, int native_code,
			   char *message);
PUBLIC RETCODE odbc_get_diag_field (short handle_type,
				    void *handle,
				    short rec_number,
				    short diag_identifier,
				    void *diag_info_ptr,
				    short buffer_length,
				    long *string_length_ptr);
PUBLIC RETCODE odbc_get_diag_rec (short handle_type,
				  void *handle,
				  short rec_number,
				  char *sqlstate,
				  long *native_error_ptr,
				  char *message_text,
				  short buffer_length, long *text_length_ptr);
PUBLIC void odbc_set_diag_by_icode (ODBC_DIAG * diag, int icode,
				    char *message);
PUBLIC void odbc_set_diag_by_cci (ODBC_DIAG * diag, int cci_err_code,
				  char *message);
PUBLIC void odbc_move_diag (ODBC_DIAG * target_diag, ODBC_DIAG * src_diag);

#endif /* ! __ODBC_DIAG_HEADER */
