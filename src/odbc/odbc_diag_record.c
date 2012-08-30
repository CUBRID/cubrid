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

#include		"odbc_portable.h"
#include		"odbc_util.h"
#include		"sqlext.h"
#include		"odbc_diag_record.h"
#include		"odbc_env.h"
#include		"odbc_connection.h"
#include		"odbc_statement.h"

#define		ISO_CLASS_ORIGIN			"ISO 9075"
#define		ODBC_CLASS_ORIGIN		"ODBC 3.0"
#define		DIAG_PREFIX				"[CUBRID][ODBC CUBRID Driver]"

PRIVATE const char *get_diag_message (ODBC_DIAG_RECORD * record);
PRIVATE char *get_diag_subclass_origin (char *sql_state);
PRIVATE void odbc_init_diag (ODBC_DIAG * diag);
PRIVATE void odbc_clear_diag (ODBC_DIAG * diag);
PRIVATE void odbc_init_diag_record (ODBC_DIAG_RECORD * node);
PRIVATE void odbc_clear_diag_record (ODBC_DIAG_RECORD * node);
PRIVATE short is_header_field (short field_id);
PRIVATE ODBC_DIAG_RECORD *find_diag_record (ODBC_DIAG * diag, int rec_number);
PRIVATE char *get_diag_class_origin (char *sql_state);

PRIVATE ODBC_ERROR_MAP odbc_3_0_error_map[] = {
  {"01000", "General warning"},
  {"01001", "Cursor operation conflict"},
  {"01002", "Disconnect error"},
  {"01003", "NULL value eliminated in set function"},
  {"01004", "String data, right truncated"},
  {"01006", "Privilege not revoked"},
  {"01007", "Privilege not granted"},
  {"01S00", "Invalid connection string attribute"},
  {"01S01", "Error in row"},
  {"01S02", "Option value changed"},
  {"01S06", "Attempt to fetch before the result set retunred the first rowset"},
  {"01S07", "Fractional truncation"},
  {"01S08", "Error saving File DSN"},
  {"01S09", "Invalid keyword"},
  {"07001", "Wrong number of parameters"},
  {"07002", "COUNT field incorrect"},
  {"07005", "Prepared statement not a cursor-specification"},
  {"07006", "Restricted data type attribute violation"},
  {"07009", "Invalid descriptor index"},
  {"07S01", "Invalid use of default parameter"},
  {"08001", "Client unable to establish connection"},
  {"08002", "Connection name in use"},
  {"08003", "Connection does not exist"},
  {"08004", "Server rejected the connection"},
  {"08007", "Connection failure during transaction"},
  {"08S01", "Communication link failure"},
  {"21S01", "Insert value list does not match column list"},
  {"21S02", "Degree of derived table does not match column list"},
  {"22001", "String data, right truncated"},
  {"22002", "Indicator variable required but not supplied"},
  {"22003", "Numeric value out of range"},
  {"22007", "Invalid datetime format"},
  {"22008", "Datetime field overflow"},
  {"22012", "Division by zero"},
  {"22015", "Invalid field overflow"},
  {"22018", "Invalid charcter value for cast specification"},
  {"22019", "Invalid escape character"},
  {"22025", "Invalid escape sequence"},
  {"22026", "String data, length mismatch"},
  {"23000", "Integrity constraint violation"},
  {"24000", "Invalid cursor state"},
  {"25000", "Invalid transaction state"},
  {"25S01", "Transaction state"},
  {"25S02", "Transaction is still active"},
  {"25S03", "Transaction is rolled back"},
  {"28000", "Invalid authorization specification"},
  {"34000", "Invalid cursor name"},
  {"3C000", "Duplicate cursor name"},
  {"3D000", "Invalid catalog name"},
  {"3F000", "Invalid schema name"},
  {"40001", "Serialization failure"},
  {"40002", "Integrity constraint violation"},
  {"40003", "Statement completion unknown"},
  {"42000", "Syntax error or access violation"},
  {"42S01", "Base table or view already exists"},
  {"42S02", "Base table or view not found"},
  {"42S11", "Index already exists"},
  {"42S12", "Index not found"},
  {"42S21", "Column already exists"},
  {"42S22", "Column not found"},
  {"44000", "WITH CHECK OPTION violation"},
  {"HY000", "General error"},
  {"HY001", "Memory allocation error"},
  {"HY003", "Invalid application buffer type"},
  {"HY004", "Invalid SQL data type"},
  {"HY007", "Associated statement is not prepared"},
  {"HY008", "Operation canceled"},
  {"HY009", "Invalid use of null pointer"},
  {"HY010", "Function sequence error"},
  {"HY011", "Attribute cannot be set now"},
  {"HY012", "Invalid transaction operation code"},
  {"HY013", "Memory management error"},
  {"HY014", "Limit on the number of handles exceeded"},
  {"HY015", "No cursor name available"},
  {"HY016", "Cannot modify an implementation row descriptor"},
  {"HY017", "Invalid use of an automatically allocated descriptor handle"},
  {"HY018", "Server declined cancel request"},
  {"HY019", "Non-character and non-binary data sent in pieces"},
  {"HY020", "Attempt to concatenate a null value"},
  {"HY021", "Inconsistent descriptor information"},
  {"HY024", "Invalid attribute value"},
  {"HY090", "Invalid string or buffer length"},
  {"HY091", "Invalid descriptor field identifier"},
  {"HY092", "Invalid attribute/option identifier"},
  {"HY095", "Function type out of range"},
  {"HY096", "Invalid information type"},
  {"HY097", "Column type out of range"},
  {"HY098", "Scope type out of range"},
  {"HY099", "Nullable type out of range"},
  {"HY100", "Uniqueness option type out of range"},
  {"HY101", "Accuracy option type out of range"},
  {"HY103", "Invalid retrieval code"},
  {"HY104", "Invalid precision or scale value"},
  {"HY105", "Invalid parameter type"},
  {"HY106", "Fetch type out of range"},
  {"HY107", "Row value out of range"},
  {"HY109", "Invalid cursor position"},
  {"HY110", "Invalid driver completion"},
  {"HY111", "Invalid bookmark value"},
  {"HYC00", "Optional feature not implemented"},
  {"HYT00", "Timeout expired"},
  {"HYT01", "Connection timeout expired"},
  {"IM001", "Driver does not support this function"},
  {"IM002", "Data source name not found and no default driver specified"},
  {"IM003", "Specified driver could not be loaded"},
  {"IM004", "Driver's SQLAllocHandle on SQL_HANDLE_ENV failed"},
  {"IM005", "Driver's SQLAllocHandle on SQL_HANDLE_DBC failed"},
  {"IM006", "Driver's SQLSetConnectAttr failed"},
  {"IM007", "No data source or driver specified; dalo prohibited"},
  {"IM008", "Dialog failed"},
  {"IM009", "Unable to load translation DLL"},
  {"IM010", "Data source name too long"},
  {"IM011", "Driver name too long"},
  {"IM012", "DRIVER keyword syntax error"},
  {"IM013", "Trace file error"},
  {"IM014", "Invalid name of File DSN"},
  {"IM015", "Corrupt file data source"},
  {NULL, NULL}
};

/************************************************************************
 * name: odbc_alloc_diag
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC ODBC_DIAG *
odbc_alloc_diag (void)
{
  ODBC_DIAG *diag = NULL;

  diag = UT_ALLOC (sizeof (ODBC_DIAG));
  odbc_init_diag (diag);

  return diag;
}

/************************************************************************
 * name: odbc_free_diag
 * arguments:
 *    option -
 *		INIT - 초기화
 *		RESET - member reset
 *		FREE_MEBER - member만 free
 *		FREE_ALL - member 및 node free
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC void
odbc_free_diag (ODBC_DIAG * diag, int option)
{
  if (diag == NULL)
    return;

  switch (option)
    {
    case INIT:
      odbc_init_diag (diag);
      break;
    case RESET:
      odbc_clear_diag (diag);
      odbc_init_diag (diag);
      break;
    case FREE_MEMBER:
      odbc_clear_diag (diag);
      break;
    case FREE_ALL:
      odbc_clear_diag (diag);
      UT_FREE (diag);
      break;
    }
}

/************************************************************************
 * name: odbc_alloc_diag_record
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC ODBC_DIAG_RECORD *
odbc_alloc_diag_record (void)
{
  ODBC_DIAG_RECORD *diag_record = NULL;

  diag_record = UT_ALLOC (sizeof (ODBC_DIAG_RECORD));
  odbc_init_diag_record (diag_record);

  return diag_record;
}

/************************************************************************
 * name: odbc_free_diag_record
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC void
odbc_free_diag_record (ODBC_DIAG_RECORD * diag_record, int option)
{
  if (diag_record == NULL)
    return;

  switch (option)
    {
    case INIT:
      odbc_init_diag_record (diag_record);
      break;
    case RESET:
      odbc_clear_diag_record (diag_record);
      odbc_init_diag_record (diag_record);
      break;
    case FREE_MEMBER:
      odbc_clear_diag_record (diag_record);
      break;
    case FREE_ALL:
      odbc_clear_diag_record (diag_record);
      UT_FREE (diag_record);
      break;
    }
}

PUBLIC void
odbc_free_diag_record_list (ODBC_DIAG_RECORD * diag_record)
{
  ODBC_DIAG_RECORD *current, *del;

  current = diag_record;

  while (current != NULL)
    {
      del = current;
      current = current->next;
      odbc_free_diag_record (del, ODBC_DIAG_RECORD_FREE_ALL);
    }
}

PUBLIC void
odbc_move_diag (ODBC_DIAG * target_diag, ODBC_DIAG * src_diag)
{
  ODBC_DIAG_RECORD *tmp;

  if (src_diag == NULL || target_diag == NULL)
    return;

  tmp = src_diag->record;

  if (tmp == NULL)
    return;
  else
    {
      while (tmp->next != NULL)
	tmp = tmp->next;
    }

  tmp->next = target_diag->record;
  target_diag->record = src_diag->record;
  src_diag->record = NULL;

  target_diag->rec_number += src_diag->rec_number;

  return;
}



/* recored added to head */
PUBLIC void
odbc_set_diag (ODBC_DIAG * diag, char *sql_state, int native_code,
	       char *message)
{
  ODBC_DIAG_RECORD *record = NULL;
	int i;

  if (diag == NULL)
    return;

  record = odbc_alloc_diag_record ();
  if (record == NULL)
    return;

  ++(diag->rec_number);

  record->number = diag->rec_number;
  record->sql_state = UT_MAKE_STRING (sql_state, -1);
  record->native_code = native_code;

	if (message) {
	  record->message = UT_MAKE_STRING (message, -1);
	} else {
		for (i = 0; ; i++) {
			if (strcmp(odbc_3_0_error_map[i].status, sql_state) == 0 ||
				odbc_3_0_error_map[i].status == NULL) {
					break;
			}
		}
		record->message = UT_MAKE_STRING (odbc_3_0_error_map[i].msg, -1);
	}

  record->next = diag->record;
  diag->record = record;
}

PUBLIC void
odbc_set_diag_by_cci (ODBC_DIAG * diag, int cci_retval, T_CCI_ERROR *error)
{
  ODBC_DIAG_RECORD *record = NULL;
	char err_msg[1024] = { 0 };

  if (diag == NULL)
    return;

  record = odbc_alloc_diag_record ();
  if (record == NULL)
    return;

  ++(diag->rec_number);

  record->number = diag->rec_number;
  record->sql_state = UT_MAKE_STRING ("HY000", -1);

	if (cci_retval == CCI_ER_DBMS) {
		if (error) {
			record->native_code = error->err_code;
		  record->message = UT_MAKE_STRING(error->err_msg, -1);
		} else {
			record->native_code = CCI_ER_DBMS;
			record->message = UT_MAKE_STRING("Unknown DBMS error", -1);
		}
	} else {
		if (cci_get_err_msg(cci_retval, err_msg, sizeof(err_msg)) < 0) {
			strncpy(err_msg, "Unknown error", sizeof(err_msg));
		}

		record->native_code = cci_retval;
		record->message = UT_MAKE_STRING(err_msg, -1);
	}

  record->next = diag->record;
  diag->record = record;
}

/************************************************************************
* name: odbc_get_diag_field
* arguments:
* returns/side-effects:
* description:
* NOTE:
*	record field의 SQL_DIAG_COLUMN_NUMBER와 SQL_DIAG_ROW_NUMBER는
*	각 data cell에 대한 fetch시 일어나는 error에 관한 정보이다.
*	현재 지원하지 않는다.
************************************************************************/
PUBLIC RETCODE
odbc_get_diag_field (SQLSMALLINT handle_type,
		     SQLHANDLE handle,
		     SQLSMALLINT rec_number,
		     SQLSMALLINT diag_identifier,
		     SQLPOINTER diag_info_ptr,
		     SQLSMALLINT buffer_length, SQLLEN *string_length_ptr)
{
  ODBC_ENV *env;
  ODBC_DIAG_RECORD *record;

  RETCODE status = ODBC_SUCCESS, rc;
  char *pt;
  char empty_str[1] = "";

  env = (ODBC_ENV *) handle;

  if ((diag_identifier == SQL_DIAG_CURSOR_ROW_COUNT ||
       diag_identifier == SQL_DIAG_DYNAMIC_FUNCTION ||
       diag_identifier == SQL_DIAG_DYNAMIC_FUNCTION_CODE ||
       diag_identifier == SQL_DIAG_ROW_COUNT) &&
      handle_type != SQL_HANDLE_STMT)
    {
      return ODBC_ERROR;
    }

  if (is_header_field (diag_identifier) == _TRUE_)
    {
      switch (diag_identifier)
	{
	case SQL_DIAG_CURSOR_ROW_COUNT:
	case SQL_DIAG_ROW_COUNT:
	  if (diag_info_ptr != NULL)
	    *(long *) diag_info_ptr =
	      ((ODBC_STATEMENT *) handle)->current_tpl_pos;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (long);
	    }
	  break;

	case SQL_DIAG_DYNAMIC_FUNCTION:
	case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
	  /* Yet not implemented */
	  return ODBC_ERROR;

	case SQL_DIAG_NUMBER:
	  if (diag_info_ptr != NULL)
	    *(long *) diag_info_ptr = env->diag->rec_number;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (long);
	    }
	  break;

	case SQL_DIAG_RETURNCODE:
	  if (diag_info_ptr != NULL)
	    *(short *) diag_info_ptr = env->diag->retcode;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (short);
	    }
	  break;
	default:
	  return ODBC_ERROR;
	}
    }
  else
    {
      /* record field */
      if (rec_number <= 0 || rec_number > env->diag->rec_number)
	{
	  return ODBC_NO_DATA;
	}
      record = find_diag_record (env->diag, rec_number);
      if (record == NULL)
	{
	  return ODBC_NO_DATA;
	}

      switch (diag_identifier)
	{
	case SQL_DIAG_CLASS_ORIGIN:
	  pt = get_diag_class_origin (record->sql_state);
	  rc =
	    str_value_assign (pt, diag_info_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      status = rc;
	    }
	  break;

	case SQL_DIAG_COLUMN_NUMBER:	/* yet not implemeted */
	  if (diag_info_ptr != NULL)
	    *(long *) diag_info_ptr = SQL_COLUMN_NUMBER_UNKNOWN;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (long);
	    }
	  break;

	case SQL_DIAG_CONNECTION_NAME:	/* yet not implemeted */
	  return ODBC_NO_DATA;

	case SQL_DIAG_MESSAGE_TEXT:
	  {
	    char *mesg = NULL;
			char diag_prefix[256] = { 0 };

	    pt = (char *) get_diag_message (record);
	    if (pt == NULL)
	      pt = empty_str;

			_snprintf (diag_prefix, sizeof(diag_prefix), "%s[%d]", 
				DIAG_PREFIX, record->native_code);
	    mesg = UT_MAKE_STRING (diag_prefix, -1);
	    mesg = UT_APPEND_STRING (mesg, pt, -1);

	    rc =
	      str_value_assign (mesg, diag_info_ptr, buffer_length,
				string_length_ptr);
	    if (rc == ODBC_SUCCESS_WITH_INFO)
	      {
		status = rc;
	      }

	    NC_FREE (mesg);
	  }
	  break;

	case SQL_DIAG_NATIVE:
	  if (diag_info_ptr != NULL)
	    *(long *) diag_info_ptr = record->native_code;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (long);
	    }
	  break;


	case SQL_DIAG_ROW_NUMBER:	/* yet not implemeted */
	  if (diag_info_ptr != NULL)
	    *(long *) diag_info_ptr = SQL_ROW_NUMBER_UNKNOWN;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (long);
	    }
	  break;

	case SQL_DIAG_SERVER_NAME:
	  if (handle_type == SQL_HANDLE_ENV)
	    {
	      pt = empty_str;
	    }
	  else
	    {
	      if (handle_type == SQL_HANDLE_DBC)
		{
		  pt = ((ODBC_CONNECTION *) handle)->data_source;
		}
	      else if (handle_type == SQL_HANDLE_STMT)
		{
		  pt = ((ODBC_STATEMENT *) handle)->conn->data_source;
		}
	      else if (handle_type == SQL_HANDLE_DESC)
		{
		  pt = ((ODBC_DESC *) handle)->conn->data_source;
		}
	      rc =
		str_value_assign (pt, diag_info_ptr, buffer_length,
				  string_length_ptr);
	      if (rc == ODBC_SUCCESS_WITH_INFO)
		{
		  status = rc;
		}
	    }
	  break;

	case SQL_DIAG_SQLSTATE:
	  if (record->sql_state != NULL)
	    {
	      pt = record->sql_state;
	    }
	  else
	    {
	      pt = empty_str;
	    }
	  rc =
	    str_value_assign (pt, diag_info_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      status = rc;
	    }
	  break;

	case SQL_DIAG_SUBCLASS_ORIGIN:
	  pt = get_diag_subclass_origin (record->sql_state);
	  if (pt == NULL)
	    {
	      pt = empty_str;
	    }
	  rc =
	    str_value_assign (pt, diag_info_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      status = rc;
	    }
	  break;

	default:
	  return ODBC_ERROR;
	}

    }

  return status;
}


/************************************************************************
* name: odbc_get_diag_rec
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_get_diag_rec (SQLSMALLINT handle_type,
		   SQLHANDLE *handle,
		   SQLSMALLINT rec_number,
		   SQLCHAR *sqlstate,
		   SQLINTEGER *native_error_ptr,
		   SQLCHAR *message_text,
		   SQLSMALLINT buffer_length, SQLLEN *text_length_ptr)
{
  RETCODE status = ODBC_SUCCESS, rc;
  ODBC_DIAG_RECORD *record;
  ODBC_ENV *env;
  char *pt;
  char *mesg = NULL;
  char empty_str[1] = "";
	char diag_prefix[256] = { 0 };

  env = (ODBC_ENV *) handle;

  if (rec_number <= 0)
    return ODBC_ERROR;

  /* Sequence error record is not implemented */
  if (rec_number > env->diag->rec_number)
    return ODBC_NO_DATA;
  if ((record = find_diag_record (env->diag, rec_number)) == NULL)
    {
      return ODBC_NO_DATA;
    }

  if (sqlstate != NULL)
    {
      strcpy (sqlstate, record->sql_state);
    }
  if (native_error_ptr != NULL)
    {
      *native_error_ptr = record->native_code;
    }

  pt = (char *) get_diag_message (record);
  if (pt == NULL)
    pt = empty_str;

	_snprintf (diag_prefix, sizeof(diag_prefix), "%s[%d]", 
		DIAG_PREFIX, record->native_code);
  mesg = UT_MAKE_STRING (diag_prefix, -1);
  mesg = UT_APPEND_STRING (mesg, pt, -1);

  rc = str_value_assign (mesg, message_text, buffer_length, text_length_ptr);
  if (rc == ODBC_SUCCESS_WITH_INFO)
    {
      status = rc;
    }
  NC_FREE (mesg);

  return status;
}

PRIVATE const char *
get_diag_message (ODBC_DIAG_RECORD * record)
{
  if (record->message != NULL && record->message[0] != '\0')
    {
      return record->message;
    }
	else
		{
			return NULL;
		}
}

PRIVATE char *
get_diag_subclass_origin (char *sql_state)
{
  if (sql_state == NULL || sql_state[0] == '\0')
    {
      return NULL;
    }

  if (strcmp (sql_state, "01S00") == 0 ||
      strcmp (sql_state, "01S01") == 0 ||
      strcmp (sql_state, "01S02") == 0 ||
      strcmp (sql_state, "01S06") == 0 ||
      strcmp (sql_state, "01S07") == 0 ||
      strcmp (sql_state, "07S01") == 0 ||
      strcmp (sql_state, "08S01") == 0 ||
      strcmp (sql_state, "21S01") == 0 ||
      strcmp (sql_state, "21S02") == 0 ||
      strcmp (sql_state, "25S01") == 0 ||
      strcmp (sql_state, "25S02") == 0 ||
      strcmp (sql_state, "25S03") == 0 ||
      strcmp (sql_state, "42S01") == 0 ||
      strcmp (sql_state, "42S02") == 0 ||
      strcmp (sql_state, "42S11") == 0 ||
      strcmp (sql_state, "42S12") == 0 ||
      strcmp (sql_state, "42S21") == 0 ||
      strcmp (sql_state, "42S22") == 0 ||
      strcmp (sql_state, "HY095") == 0 ||
      strcmp (sql_state, "HY097") == 0 ||
      strcmp (sql_state, "HY098") == 0 ||
      strcmp (sql_state, "HY099") == 0 ||
      strcmp (sql_state, "HY100") == 0 ||
      strcmp (sql_state, "HY101") == 0 ||
      strcmp (sql_state, "HY105") == 0 ||
      strcmp (sql_state, "HY107") == 0 ||
      strcmp (sql_state, "HY109") == 0 ||
      strcmp (sql_state, "HY110") == 0 ||
      strcmp (sql_state, "HY111") == 0 ||
      strcmp (sql_state, "HYT00") == 0 ||
      strcmp (sql_state, "HYT01") == 0 ||
      strcmp (sql_state, "IM001") == 0 ||
      strcmp (sql_state, "IM002") == 0 ||
      strcmp (sql_state, "IM003") == 0 ||
      strcmp (sql_state, "IM004") == 0 ||
      strcmp (sql_state, "IM005") == 0 ||
      strcmp (sql_state, "IM006") == 0 ||
      strcmp (sql_state, "IM007") == 0 ||
      strcmp (sql_state, "IM008") == 0 ||
      strcmp (sql_state, "IM010") == 0 ||
      strcmp (sql_state, "IM011") == 0 || strcmp (sql_state, "IM012") == 0)
    {
      return ODBC_CLASS_ORIGIN;
    }
  return NULL;
}

PRIVATE ODBC_DIAG_RECORD *
find_diag_record (ODBC_DIAG * diag, int rec_number)
{
  ODBC_DIAG_RECORD *record = NULL;

  record = diag->record;

  while (record != NULL)
    if (record->number == rec_number)
      break;
    else
      record = record->next;

  return record;
}



PRIVATE void
odbc_init_diag (ODBC_DIAG * diag)
{
  if (diag == NULL)
    return;

  memset (diag, 0, sizeof (ODBC_DIAG));
}

PRIVATE void
odbc_clear_diag (ODBC_DIAG * diag)
{
  if (diag == NULL)
    return;

  odbc_free_diag_record_list (diag->record);
}

PRIVATE void
odbc_init_diag_record (ODBC_DIAG_RECORD * node)
{
  if (node != NULL)
    {
      memset (node, 0, sizeof (ODBC_DIAG_RECORD));
    }
}

PRIVATE void
odbc_clear_diag_record (ODBC_DIAG_RECORD * node)
{
  if (node == NULL)
    return;
  NC_FREE (node->message);
  NC_FREE (node->sql_state);
}

PRIVATE short
is_header_field (short field_id)
{
  switch (field_id)
    {
    case SQL_DIAG_CURSOR_ROW_COUNT:
    case SQL_DIAG_DYNAMIC_FUNCTION:
    case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
    case SQL_DIAG_NUMBER:
    case SQL_DIAG_RETURNCODE:
    case SQL_DIAG_ROW_COUNT:
      return _TRUE_;
    default:
      return _FALSE_;
    }
}

PRIVATE char *
get_diag_class_origin (char *sql_state)
{
  if (strncmp (sql_state, "IM", 2) == 0)
    return ODBC_CLASS_ORIGIN;
  else
    return ISO_CLASS_ORIGIN;
}
