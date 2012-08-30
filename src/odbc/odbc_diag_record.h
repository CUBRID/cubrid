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


/* CUBRID ODBC specific error code */
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

typedef struct st_odbc_error_map
{
  const char *status;
  const char *msg;
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
PUBLIC RETCODE odbc_get_diag_field (SQLSMALLINT handle_type,
                                    SQLHANDLE handle,
                                    SQLSMALLINT rec_number,
                                    SQLSMALLINT diag_identifier,
                                    SQLPOINTER diag_info_ptr,
                                    SQLSMALLINT buffer_length, SQLLEN *string_length_ptr);
PUBLIC RETCODE odbc_get_diag_rec (SQLSMALLINT handle_type,
                                  SQLHANDLE *handle,
                                  SQLSMALLINT rec_number,
                                  SQLCHAR *sqlstate,
                                  SQLINTEGER *native_error_ptr,
                                  SQLCHAR *message_text,
                                  SQLSMALLINT buffer_length, SQLLEN *text_length_ptr);
PUBLIC void odbc_set_diag_by_cci (ODBC_DIAG * diag, int cci_err_code,
                                  T_CCI_ERROR * error);
PUBLIC void odbc_move_diag (ODBC_DIAG * target_diag, ODBC_DIAG * src_diag);

#endif /* ! __ODBC_DIAG_HEADER */
