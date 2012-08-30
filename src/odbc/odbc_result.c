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

#include		<stdlib.h>
#include		"odbc_portable.h"
#include		"odbc_statement.h"
#include		"odbc_diag_record.h"
#include		"odbc_descriptor.h"
#include		"odbc_type.h"
#include		"odbc_result.h"
#include		"odbc_catalog.h"

PRIVATE void get_bind_info (ODBC_STATEMENT * stmt,
			    short row_index,
			    short col_index,
			    short *type,
			    void **bound_ptr,
			    long *buffer_length, SQLLEN ** strlen_ind_ptr);
PRIVATE RETCODE move_catalog_rs (ODBC_STATEMENT * stmt,
				 unsigned long *current_tpl_pos);
PRIVATE RETCODE get_catalog_data (ODBC_STATEMENT * stmt,
				  short row_index, short col_index);
PRIVATE RETCODE c_value_to_bound_ptr (void *bound_ptr,
				      SQLLEN buffer_length,
				      VALUE_CONTAINER * c_value);

/************************************************************************
* name: odbc_bind_col
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_bind_col (ODBC_STATEMENT * stmt,
	       SQLUSMALLINT column_num,
	       SQLSMALLINT target_type,
	       SQLPOINTER target_value_ptr,
	       SQLLEN buffer_len, SQLLEN * strlen_indicator)
{

  ODBC_DESC *ard;
  long size;
  short odbc_type;
  RETCODE rc;
  ODBC_RECORD *record;
  short odbc_subcode;

  ard = stmt->ard;

  record = find_record_from_desc (ard, column_num);
  if (record == NULL)
    {
      odbc_alloc_record (ard, NULL, column_num);
    }

  rc = odbc_set_desc_field (ard, column_num, SQL_DESC_CONCISE_TYPE,
			    (void *) target_type, 0, 1);
  ERROR_GOTO (rc, error);

  /* setting verbose type */
  odbc_type = odbc_concise_to_verbose_type (target_type);
  rc =
    odbc_set_desc_field (ard, column_num, SQL_DESC_TYPE, (void *) odbc_type,
			 0, 1);
  ERROR_GOTO (rc, error);

  /* setting subcode */
  if (odbc_type == SQL_DATETIME)
    {
      odbc_subcode = odbc_subcode_type (target_type);
      rc =
	odbc_set_desc_field (ard, column_num, SQL_DESC_DATETIME_INTERVAL_CODE,
			     (void *) odbc_subcode, 0, 1);
      ERROR_GOTO (rc, error);
    }


  size = odbc_size_of_by_type_id (target_type);

  if (size > 0)
    {				/* target_type is the fixed type */
      rc = odbc_set_desc_field (ard, column_num, SQL_DESC_LENGTH,
				(void *) size, 0, 1);
      ERROR_GOTO (rc, error);
    }

  rc = odbc_set_desc_field (ard, column_num, SQL_DESC_OCTET_LENGTH,
			    (void *) buffer_len, 0, 1);
  ERROR_GOTO (rc, error);
  rc = odbc_set_desc_field (ard, column_num, SQL_DESC_DATA_PTR,
			    (void *) target_value_ptr, 0, 1);
  ERROR_GOTO (rc, error);
  rc = odbc_set_desc_field (ard, column_num, SQL_DESC_INDICATOR_PTR,
			    (void *) strlen_indicator, 0, 1);
  ERROR_GOTO (rc, error);
  rc = odbc_set_desc_field (ard, column_num, SQL_DESC_OCTET_LENGTH_PTR,
			    (void *) strlen_indicator, 0, 1);
  ERROR_GOTO (rc, error);

  return ODBC_SUCCESS;

error:
  odbc_move_diag (stmt->diag, ard->diag);
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_describe_col
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_describe_col (ODBC_STATEMENT * stmt,
		   SQLUSMALLINT column_number,
		   SQLCHAR * column_name,
		   SQLSMALLINT buffer_length,
		   SQLSMALLINT * name_length_ptr,
		   SQLSMALLINT * data_type_ptr,
		   SQLULEN * column_size_ptr,
		   SQLSMALLINT * decimal_digits_ptr,
		   SQLSMALLINT * nullable_ptr)
{
  ODBC_DESC *ird = NULL;
  ODBC_RECORD *record = NULL;
  short scale;
  SQLLEN int_name_length;
  RETCODE rc;

  ird = stmt->ird;

  if (column_name != NULL)
    {
      rc =
	odbc_get_desc_field (ird, column_number, SQL_DESC_NAME, column_name,
			     buffer_length, &int_name_length);
      ERROR_GOTO (rc, error);

      if (name_length_ptr != NULL)
	{
	  *name_length_ptr = (short) int_name_length;
	}
    }
  odbc_get_desc_field (ird, column_number, SQL_DESC_CONCISE_TYPE,
		       data_type_ptr, 0, NULL);

  odbc_get_desc_field (ird, column_number, SQL_DESC_SCALE, &scale, 0, NULL);

  if (column_size_ptr != NULL)
    {
      if (*data_type_ptr == SQL_NUMERIC)
	{
	  odbc_get_desc_field (ird, column_number, SQL_DESC_PRECISION,
			       column_size_ptr, 0, NULL);
	  *(unsigned long *) column_size_ptr =
	    *(unsigned short *) column_size_ptr;
	}
      else
	{
	  odbc_get_desc_field (ird, column_number, SQL_DESC_LENGTH,
			       column_size_ptr, 0, NULL);
	}
      //odbc_get_desc_field(ird, column_number, SQL_DESC_LENGTH, column_size_ptr, 0, NULL);
    }

  if (decimal_digits_ptr != NULL)
    {
      *decimal_digits_ptr = odbc_decimal_digits (*data_type_ptr, scale);
    }
  if (nullable_ptr != NULL)
    {
      odbc_get_desc_field (ird, column_number, SQL_DESC_NULLABLE,
			   nullable_ptr, 0, NULL);
    }

  return ODBC_SUCCESS;
error:
  odbc_move_diag (stmt->diag, ird->diag);
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_col_attribute
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_col_attribute (ODBC_STATEMENT * stmt,
		    unsigned short column_number,
		    unsigned short field_identifier,
		    void *str_value_ptr,
		    short buffer_length,
		    short *string_length_ptr, void *num_value_ptr)
{
  SQLLEN int_length;
  ODBC_DESC *ird;
  RETCODE rc;
  long temp_value = 0;


  ird = stmt->ird;

  switch (field_identifier)
    {
      /* Numeric Attribute */
    case SQL_DESC_AUTO_UNIQUE_VALUE:
    case SQL_DESC_CASE_SENSITIVE:
    case SQL_DESC_CONCISE_TYPE:
    case SQL_DESC_COUNT:
    case SQL_DESC_DISPLAY_SIZE:
    case SQL_DESC_FIXED_PREC_SCALE:
    case SQL_DESC_LENGTH:
    case SQL_COLUMN_LENGTH:	// for 2.x backward compatibility
    case SQL_DESC_NULLABLE:
    case SQL_DESC_NUM_PREC_RADIX:
    case SQL_DESC_OCTET_LENGTH:
    case SQL_DESC_PRECISION:
    case SQL_COLUMN_PRECISION:	// for 2.x backward compatibility
    case SQL_DESC_SCALE:
    case SQL_COLUMN_SCALE:	// for 2.x backward compatibility
    case SQL_DESC_SEARCHABLE:
    case SQL_DESC_TYPE:
    case SQL_DESC_UNNAMED:
    case SQL_DESC_UNSIGNED:
    case SQL_DESC_UPDATABLE:
      if (field_identifier == SQL_COLUMN_LENGTH)
	rc =
	  odbc_get_desc_field (ird, column_number, SQL_DESC_DISPLAY_SIZE,
			       &temp_value, 0, NULL);
      else
	rc =
	  odbc_get_desc_field (ird, column_number, field_identifier,
			       &temp_value, 0, NULL);

      *(long *) num_value_ptr = temp_value;
      if (string_length_ptr != NULL)
	{
	  *string_length_ptr = sizeof (long);
	}
      ERROR_GOTO (rc, error);
      break;

      /* Character Attribute */
    case SQL_DESC_BASE_COLUMN_NAME:
    case SQL_DESC_BASE_TABLE_NAME:
    case SQL_DESC_CATALOG_NAME:
    case SQL_DESC_LABEL:
    case SQL_DESC_LITERAL_PREFIX:
    case SQL_DESC_LITERAL_SUFFIX:
    case SQL_DESC_LOCAL_TYPE_NAME:
    case SQL_DESC_NAME:
    case SQL_DESC_SCHEMA_NAME:
    case SQL_DESC_TABLE_NAME:
      rc = odbc_get_desc_field (ird, column_number,
				field_identifier, str_value_ptr,
				buffer_length, &int_length);
      ERROR_GOTO (rc, error);
      *string_length_ptr = (short) int_length;
      break;

    }

  return ODBC_SUCCESS;
error:
  odbc_move_diag (stmt->diag, ird->diag);
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_row_count
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_row_count (ODBC_STATEMENT * stmt, SQLLEN * row_count)
{
  ODBC_DESC *ipd = NULL;

  if (row_count != NULL)
    *row_count = stmt->tpl_number;

  return ODBC_SUCCESS;
}

/************************************************************************
* name: odbc_num_result_cols
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_num_result_cols (ODBC_STATEMENT * stmt, short *column_count)
{
  ODBC_DESC *ird = NULL;

  ird = stmt->ird;

  if (column_count != NULL)
    {
      if (ird != NULL)
	{
	  *column_count = ird->max_count;
	}
      else
	{
	  *column_count = 0;
	}
    }

  return ODBC_SUCCESS;
}


/************************************************************************
* name: odbc_fetch
* arguments:
*	bind_offset - SQLBulkOperation with SQL_FETCH_BY_BOOKMARK를
*	위해서 고안된 것으로, 설정된 값만큼 ard array index를 이동한다.
*	flag_cursor_move - 0 - move, 1 - just value fetch
* returns/side-effects:
* description:
* NOTE:
*	SQLExtendedFetch는 SQLFetchScroll 등과 다른 error handling 방식을
*	갖는다.  이 때 모든 exceptions가 고려된 건 아니고, SQLSTATE 01S01만
*	적용되었다.  그러나 이 경우에도 fetch시 status record 구성 방식이
*	spec과 다르므로 정확한 적용방식이라고 말하기 힘들다.
*	참조 : Error handling in SQLFetchScroll
************************************************************************/
PUBLIC RETCODE
odbc_fetch (ODBC_STATEMENT * stmt,
	    SQLSMALLINT fetch_orientation,
	    SQLLEN fetch_offset, long bind_offset, short flag_cursor_move)
{
  unsigned long i;
  short j;
  long fetched_rows = 0;	/* all fetched rows number, except
				 * SQL_ROW_NO_ROWS */
  SQLLEN *strlen_ind_ptr = NULL;
  void *bound_ptr;
  long buffer_length;
  short type;
  long *fetched_rows_ptr = NULL;
  ODBC_RECORD *record = NULL;
  ODBC_RECORD *bookmark_record = NULL;
  unsigned long current_tpl_pos, init_current_tpl_pos;

  T_CCI_ERROR cci_err_buf;

  RETCODE rc;

  // reset column_data
  free_column_data (&stmt->column_data, RESET);

  init_current_tpl_pos = stmt->current_tpl_pos;

  if (stmt->result_type == 0)
    return ODBC_NO_DATA;

  else if (stmt->result_type == QUERY)
    {
      rc =
	move_advanced_cursor (stmt, &stmt->current_tpl_pos, fetch_orientation,
			      (long) fetch_offset);
    }
  else
    {
      rc = move_catalog_rs (stmt, &stmt->current_tpl_pos);
    }


  if (rc < 0)
    {
      if (rc == ODBC_NO_MORE_DATA)
	return ODBC_NO_DATA;
      else
	goto error;
    }
  current_tpl_pos = stmt->current_tpl_pos;

  for (i = bind_offset;
       (unsigned long) i < (stmt->ard->array_size + bind_offset); ++i)
    {
      if (i != bind_offset)
	{
	  if (stmt->result_type == QUERY)
	    {
	      rc = move_cursor (stmt->stmthd, &current_tpl_pos, stmt->diag);
	    }
	  else
	    {
	      rc = move_catalog_rs (stmt, &stmt->current_tpl_pos);
	    }
	  if (rc == ODBC_NO_MORE_DATA)
	    {
	      if (stmt->ird->array_status_ptr != NULL)
		{
		  stmt->ird->array_status_ptr[i] = SQL_ROW_NOROW;
		}
	      continue;
	    }
	}
      ++fetched_rows;

      if (stmt->result_type == QUERY)
	{
	  rc =
	    fetch_tuple (stmt->stmthd, stmt->diag,
			 stmt->attr_cursor_sensitivity);
	  if (rc < 0)
	    {
	      if (rc == ODBC_ROW_DELETED)
		{
		  if (stmt->ird->array_status_ptr != NULL)
		    {
		      stmt->ird->array_status_ptr[i] = SQL_ROW_DELETED;
		    }
		}
	      else
		{
		  if (stmt->ird->array_status_ptr != NULL)
		    {
		      stmt->ird->array_status_ptr[i] = SQL_ROW_ERROR;
		    }
		}
	      continue;
	    }
	}
      // catalog result set의 경우 fetch과정이 필요없다.

      // bind BOOKMARK
      if (stmt->attr_use_bookmark == SQL_UB_VARIABLE)
	{
	  bookmark_record = find_record_from_desc (stmt->ard, 0);
	  if (bookmark_record != NULL)
	    {
	      get_bind_info (stmt, (short) i, 0, &type, &bound_ptr,
			     &buffer_length, &strlen_ind_ptr);
	      *((long *) bound_ptr) = current_tpl_pos;
	      *((long *) strlen_ind_ptr) = 4;
	    }
	}

      // get each column data per row
      stmt->column_data.column_no = 0;	// for avoiding confliction with SQLGetData
      for (j = 1; j <= stmt->ard->max_count; ++j)
	{
	  record = find_record_from_desc (stmt->ard, j);
	  if (record == NULL)
	    continue;

	  get_bind_info (stmt, (short) i, j, &type, &bound_ptr,
			 &buffer_length, &strlen_ind_ptr);
	  if (bound_ptr == NULL)
	    continue;
	  rc =
	    odbc_get_data (stmt, j, type, bound_ptr, buffer_length,
			   strlen_ind_ptr);
	  if (rc < 0)
	    goto error;
	}
      stmt->column_data.column_no = 0;	// for avoiding confliction with SQLGetData

      if (stmt->ird->array_status_ptr != NULL)
	{
	  stmt->ird->array_status_ptr[i] = SQL_ROW_SUCCESS;
	}
    }

  // array fetch 동안 cursor movement 보정
  if (stmt->result_type == QUERY)
    {
      if (flag_cursor_move == 1)
	{
	  stmt->current_tpl_pos = init_current_tpl_pos;
	}
      cci_cursor (stmt->stmthd, stmt->current_tpl_pos, CCI_CURSOR_FIRST,
		  &cci_err_buf);
    }


  odbc_get_stmt_attr (stmt, SQL_ATTR_ROWS_FETCHED_PTR, &fetched_rows_ptr, 0,
		      NULL);
  if (fetched_rows_ptr != NULL)
    *fetched_rows_ptr = fetched_rows;

  return ODBC_SUCCESS;

// CHECK : error, get_date, move_cursor, fetch_tuple에서 cci_error가
// 발생한다. SQL_ROW_ERROR 보다 심각한 error의 경우 ODBC_ERROR를
// 뿌려야 한다.

error:
  if (stmt->conn->env->attr_odbc_version == SQL_OV_ODBC2)	// for 2.x backward compatibility
    {
      odbc_set_diag (stmt->diag, "01S01", 0, NULL);
      return ODBC_ERROR;
    }

  odbc_set_diag (stmt->diag, "01000", 0, "Fetch error");
  return ODBC_ERROR;

}

/************************************************************************
 * name: odbc_get_data
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
odbc_get_data (ODBC_STATEMENT * stmt,
	       SQLUSMALLINT col_number,
	       SQLSMALLINT target_type,
	       SQLPOINTER bound_ptr, SQLLEN buffer_length,
	       SQLLEN * str_ind_ptr)
{
  RETCODE status = ODBC_SUCCESS, rc;
  short precision, scale;
  T_CCI_A_TYPE a_type;
  UNI_CCI_A_TYPE cci_value;
  SQLLEN length;
  long offset;
  short sql_type;

  int cci_rc;
  int cci_ind;
  VALUE_CONTAINER c_value;

  if (col_number > stmt->ird->max_count)
    {
      odbc_set_diag (stmt->diag, "07009", 0, NULL);
      goto error;
    }

  if (bound_ptr == NULL)
    {
      odbc_set_diag (stmt->diag, "01004", 0,
		     "TargetValuePtr argument is a null pointer");
      return ODBC_SUCCESS_WITH_INFO;
    }


  if (col_number != stmt->column_data.column_no)
    {
      free_column_data (&stmt->column_data, RESET);
      stmt->column_data.column_no = col_number;

      if (stmt->result_type == QUERY)
	{
	  if (target_type == SQL_ARD_TYPE)
	    {
	      odbc_get_desc_field (stmt->ard, col_number, SQL_DESC_TYPE,
				   &target_type, 0, NULL);
	    }

	  if (target_type == SQL_C_DEFAULT)
	    {
	      odbc_get_desc_field (stmt->ird, col_number,
				   SQL_DESC_CONCISE_TYPE, &sql_type, 0, NULL);
	      target_type = odbc_default_c_type (sql_type);
	    }
	  a_type = odbc_type_to_cci_a_type (target_type);

	  cci_rc =
	    cci_get_data (stmt->stmthd, col_number, a_type, &cci_value,
			  &cci_ind);
	  if (cci_rc < 0)
	    {
	      odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	      goto error;
	    }
	  if (cci_ind == -1)
	    {			// NULL value
	      if (str_ind_ptr == NULL)
		{
		  odbc_set_diag (stmt->diag, "22002", 0, NULL);
		  goto error;
		}
	      *str_ind_ptr = SQL_NULL_DATA;
	    }
	  else
	    {
	      // object, set type은 string으로 match
	      if (target_type == SQL_C_CHAR ||
		  target_type == SQL_C_UNI_SET
		  || target_type == SQL_C_UNI_OBJECT)
		{
		  rc =
		    str_value_assign (cci_value.str, bound_ptr,
				      (int) buffer_length, str_ind_ptr);
		  if (rc == ODBC_SUCCESS_WITH_INFO)
		    {
		      if (buffer_length > 0)
			{
			  offset = (long) buffer_length - 1;
			}
		      else
			{
			  offset = 0;
			}
		      stmt->column_data.current_pt = cci_value.str + offset;
		      stmt->column_data.remain_length =
			strlen (cci_value.str) - offset;
		      // 1 is for '\0'

		      odbc_set_diag (stmt->diag, "01004", 0, NULL);
		      status = rc;
		    }
		  else
		    {
		      stmt->column_data.remain_length = 0;
		    }
		}
	      else if (target_type == SQL_C_BINARY)
		{
		  rc =
		    bin_value_assign (cci_value.bit.buf, cci_value.bit.size,
				      bound_ptr, buffer_length, str_ind_ptr);
		  if (rc == ODBC_SUCCESS_WITH_INFO)
		    {
		      if (buffer_length > 0)
			{
			  offset = (long) buffer_length;
			}
		      else
			{
			  offset = 0;
			}
		      stmt->column_data.current_pt =
			cci_value.bit.buf + offset;
		      stmt->column_data.remain_length =
			cci_value.bit.size - offset;

		      odbc_set_diag (stmt->diag, "01004", 0, NULL);
		      status = rc;
		    }
		  else
		    {
		      stmt->column_data.remain_length = 0;
		    }
		}
	      else
		{		// for static type
		  odbc_get_desc_field (stmt->ard, (short) col_number,
				       SQL_DESC_PRECISION,
				       (void *) &precision, 0, NULL);
		  odbc_get_desc_field (stmt->ard, (short) col_number,
				       SQL_DESC_SCALE, (void *) &scale, 0,
				       NULL);
		  length =
		    cci_value_to_odbc (bound_ptr, target_type, precision,
				       scale, buffer_length, &cci_value,
				       a_type);
		  if (str_ind_ptr != NULL)
		    *str_ind_ptr = length;
		}
	    }
	}
      else
	{			// catalog result set
	  rc = odbc_get_catalog_data (stmt, col_number, &c_value);
	  // pointer assign to c_value
	  if (rc < 0)
	    {
	      odbc_set_diag (stmt->diag, "HY000", 0,
			     "Data retrieving from catalog result set failed.");
	      goto error;
	    }

	  if (c_value.length == 0)
	    {			// NULL value
	      if (str_ind_ptr == NULL)
		{
		  odbc_set_diag (stmt->diag, "22002", 0, NULL);
		  goto error;
		}
	      *str_ind_ptr = SQL_NULL_DATA;
	    }
	  else
	    {
	      VALUE_CONTAINER target_value;

	      memset (&target_value, 0, sizeof (target_value));
	      target_value.type = target_type;
	      odbc_value_converter (&target_value, &c_value);

	      // catalog result set에는 object, set type이 없다.
	      if (target_type == SQL_C_CHAR)
		{
		  rc =
		    str_value_assign (target_value.value.str, bound_ptr,
				      buffer_length, str_ind_ptr);
		  if (rc == ODBC_SUCCESS_WITH_INFO)
		    {
		      if (buffer_length > 0)
			{
			  offset = (long) buffer_length - 1;
			}
		      else
			{
			  offset = 0;
			}
		      stmt->column_data.current_pt =
			target_value.value.str + offset;
		      stmt->column_data.remain_length = strlen (target_value.value.str) - offset;	// 1 is for '\0'

		      odbc_set_diag (stmt->diag, "01004", 0, NULL);
		      status = SQL_SUCCESS_WITH_INFO;
		    }
		  else
		    {
		      stmt->column_data.remain_length = 0;
		    }
		}
	      else if (target_type == SQL_C_BINARY)
		{
		  rc =
		    bin_value_assign (target_value.value.bin,
				      target_value.length, bound_ptr,
				      buffer_length, str_ind_ptr);
		  if (rc == ODBC_SUCCESS_WITH_INFO)
		    {
		      if (buffer_length > 0)
			{
			  offset = (long) buffer_length;
			}
		      else
			{
			  offset = 0;
			}
		      stmt->column_data.current_pt =
			target_value.value.bin + offset;
		      stmt->column_data.remain_length =
			(int) target_value.length - offset;

		      odbc_set_diag (stmt->diag, "01004", 0, NULL);
		      status = rc;
		    }
		  else
		    {
		      stmt->column_data.remain_length = 0;
		    }
		}
	      else
		{		// static data type
		  c_value_to_bound_ptr (bound_ptr, buffer_length,
					&target_value);
		  if (str_ind_ptr != NULL)
		    *str_ind_ptr = target_value.length;
		}
	      clear_value_container (&target_value);
	    }
	}
    }
  else
    {				// sequential function call
      if (stmt->column_data.prev_return_status == ODBC_SUCCESS &&
	  stmt->column_data.remain_length == 0)
	{
	  return ODBC_NO_DATA;
	}
      if (target_type == SQL_C_CHAR)
	{
	  rc =
	    str_value_assign (stmt->column_data.current_pt, bound_ptr,
			      buffer_length, str_ind_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      //stmt->column_data.current_pt = stmt->column_data.current_pt + (buffer_length-1);
	      //stmt->column_data.remain_length = strlen(stmt->column_data.current_pt) - (buffer_length-1);
	      if (buffer_length > 0)
		{
		  offset = (long) buffer_length - 1;
		}
	      else
		{
		  offset = 0;
		}
	      stmt->column_data.current_pt += offset;
	      stmt->column_data.remain_length -= offset;
	      stmt->column_data.column_no = col_number;

	      odbc_set_diag (stmt->diag, "01004", 0, NULL);
	      status = rc;
	    }
	  else
	    {
	      stmt->column_data.remain_length = 0;
	    }

	}
      else if (target_type == SQL_C_BINARY)
	{
	  rc =
	    bin_value_assign (stmt->column_data.current_pt,
			      stmt->column_data.remain_length, bound_ptr,
			      buffer_length, str_ind_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      if (buffer_length > 0)
		{
		  offset = (long) buffer_length;
		}
	      else
		{
		  offset = 0;
		}
	      stmt->column_data.current_pt += offset;
	      stmt->column_data.remain_length -= offset;
	      stmt->column_data.column_no = col_number;

	      odbc_set_diag (stmt->diag, "01004", 0, NULL);
	      status = rc;
	    }
	  else
	    {
	      stmt->column_data.remain_length = 0;
	    }
	}

    }

  stmt->column_data.prev_return_status = status;
  return status;

error:
  stmt->column_data.prev_return_status = status;
  return ODBC_ERROR;
}

/************************************************************************
 * name: get_bind_info
 * arguments:
 *		type (OUT)
 *		bound_ptr (OUT)
 *		strlen_ind_ptr (OUT)
 *		buffer_length (OUT)
 * returns/side-effects:
 * description:
 *		ard로부터 bound_ptr과 strlen_ind_ptr을 얻어온다.
 * NOTE:
 ************************************************************************/
PRIVATE void
get_bind_info (ODBC_STATEMENT * stmt,
	       short row_index,
	       short col_index,
	       short *type,
	       void **bound_ptr, long *buffer_length,
	       SQLLEN ** strlen_ind_ptr)
{
  long element_size;
  long offset_size;

  element_size = stmt->ard->bind_type;	/* 0 means single or column wise, >0 means row wise */
  if (stmt->ard->bind_offset_ptr == NULL)
    {
      offset_size = 0;
    }
  else
    {
      offset_size = *(stmt->ard->bind_offset_ptr);
    }

  odbc_get_desc_field (stmt->ard, (short) col_index, SQL_DESC_DATA_PTR,
		       (void *) bound_ptr, 0, NULL);
  odbc_get_desc_field (stmt->ard, (short) col_index, SQL_DESC_INDICATOR_PTR,
		       (void *) strlen_ind_ptr, 0, NULL);

  // unbind column
//      if ( *bound_ptr == NULL || *strlen_ind_ptr == NULL ) return ;
  if (*bound_ptr == NULL)
    return;

  odbc_get_desc_field (stmt->ard, (short) col_index, SQL_DESC_CONCISE_TYPE,
		       (void *) type, 0, NULL);

  // set과 object type은 string으로 match되어 있다.
  if (*type == SQL_C_CHAR || *type == SQL_C_BINARY
      || *type == SQL_C_UNI_OBJECT || *type == SQL_C_UNI_SET
      || *type == SQL_C_DEFAULT)
    {
      odbc_get_desc_field (stmt->ard, (short) col_index,
			   SQL_DESC_OCTET_LENGTH, (void *) buffer_length, 0,
			   NULL);
    }
  else
    {
      odbc_get_desc_field (stmt->ard, (short) col_index, SQL_DESC_LENGTH,
			   (void *) buffer_length, 0, NULL);
    }

  /* recalculating bount_ptr & strlen_ind_ptr */
  if (stmt->ard->bind_type == SQL_BIND_BY_COLUMN)
    {
      (long) *bound_ptr += offset_size + row_index * (*buffer_length);
      (long) *strlen_ind_ptr += offset_size + row_index * sizeof (long);
    }
  else
    {
      (long) *bound_ptr += offset_size + row_index * element_size;
      (long) *strlen_ind_ptr += offset_size + row_index * element_size;
    }

  return;
}

/************************************************************************
 * name: move_advanced_cursor
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
move_advanced_cursor (ODBC_STATEMENT * stmt,
		      long *current_tpl_pos,
		      short fetch_orientation, long fetch_offset)
{
  RETCODE rc = ODBC_SUCCESS;
  int cci_rc;
  T_CCI_ERROR cci_err_buf;
  int tpl_pos;
  char bound_state = 0;		//0 unknown,  1 before start, 2 after end
  long result_set_size;
  long row_set_size;
  long bookmark;

  result_set_size = stmt->tpl_number;
  row_set_size = stmt->ard->array_size;

  switch (fetch_orientation)
    {
    case SQL_FETCH_NEXT:
      if (*current_tpl_pos == 0)
	{			// before start
	  cci_rc = cci_cursor (stmt->stmthd, 1, CCI_CURSOR_FIRST,
			       &cci_err_buf);
	  tpl_pos = 1;
	}
      else
	{
	  cci_rc = cci_cursor (stmt->stmthd, row_set_size, CCI_CURSOR_CURRENT,
			       &cci_err_buf);
	  tpl_pos = *current_tpl_pos + row_set_size;
	  bound_state = 2;
	}
      break;
    case SQL_FETCH_PRIOR:
      if (*current_tpl_pos > 1 && *current_tpl_pos <= row_set_size ||
	  *current_tpl_pos == -1 && result_set_size < row_set_size)
	{
	  cci_rc = cci_cursor (stmt->stmthd, 1, CCI_CURSOR_FIRST,
			       &cci_err_buf);
	  tpl_pos = 1;
	}
      else
	{
	  cci_rc =
	    cci_cursor (stmt->stmthd, -row_set_size, CCI_CURSOR_CURRENT,
			&cci_err_buf);
	  tpl_pos = *current_tpl_pos - row_set_size;
	  bound_state = 1;
	}
      break;
    case SQL_FETCH_RELATIVE:
      if (*current_tpl_pos > 1 && (*current_tpl_pos + fetch_offset) < 1 &&
	  (labs (fetch_offset) > row_set_size))
	{
	  cci_rc = cci_cursor (stmt->stmthd, 1, CCI_CURSOR_FIRST,
			       &cci_err_buf);
	  tpl_pos = 1;
	}
      else
	{
	  cci_rc = cci_cursor (stmt->stmthd, fetch_offset, CCI_CURSOR_CURRENT,
			       &cci_err_buf);
	  tpl_pos = *current_tpl_pos + fetch_offset;
	  if (fetch_offset > 0)
	    {
	      bound_state = 2;
	    }
	  else
	    {
	      bound_state = 1;
	    }
	}
      break;
    case SQL_FETCH_ABSOLUTE:
      if (fetch_offset > 0)
	{
	  cci_rc =
	    cci_cursor (stmt->stmthd, fetch_offset, CCI_CURSOR_FIRST,
			&cci_err_buf);
	  tpl_pos = fetch_offset;
	  bound_state = 2;
	}
      else if (fetch_offset < 0 && labs (fetch_offset) > result_set_size
	       && labs (fetch_offset) <= row_set_size)
	{
	  cci_rc =
	    cci_cursor (stmt->stmthd, 1, CCI_CURSOR_FIRST, &cci_err_buf);
	  tpl_pos = 1;
	}
      else
	{
	  cci_rc =
	    cci_cursor (stmt->stmthd, -fetch_offset, CCI_CURSOR_LAST,
			&cci_err_buf);
	  tpl_pos = result_set_size + fetch_offset + 1;
	  bound_state = 1;
	}
      break;
    case SQL_FETCH_FIRST:
      cci_rc = cci_cursor (stmt->stmthd, 1, CCI_CURSOR_FIRST, &cci_err_buf);
      tpl_pos = 1;
      break;
    case SQL_FETCH_LAST:
      if (row_set_size > result_set_size)
	{
	  cci_rc =
	    cci_cursor (stmt->stmthd, 1, CCI_CURSOR_LAST, &cci_err_buf);
	  tpl_pos = 1;
	}
      else
	{
	  cci_rc =
	    cci_cursor (stmt->stmthd, result_set_size - row_set_size + 1,
			CCI_CURSOR_LAST, &cci_err_buf);
	  tpl_pos = result_set_size - row_set_size + 1;
	}
      break;
    case SQL_FETCH_BOOKMARK:
      if (stmt->attr_use_bookmark == SQL_UB_VARIABLE
	  && stmt->attr_fetch_bookmark_ptr != NULL)
	{
	  bookmark = *((long *) stmt->attr_fetch_bookmark_ptr);
	  if ((bookmark + fetch_offset) > 0)
	    {
	      cci_rc =
		cci_cursor (stmt->stmthd, bookmark + fetch_offset,
			    CCI_CURSOR_FIRST, &cci_err_buf);
	      tpl_pos = bookmark + fetch_offset;
	      bound_state = 2;
	    }
	  else if ((bookmark + fetch_offset) < 0
		   && labs (bookmark + fetch_offset) > result_set_size
		   && labs (bookmark + fetch_offset) <= row_set_size)
	    {
	      cci_rc =
		cci_cursor (stmt->stmthd, 1, CCI_CURSOR_FIRST, &cci_err_buf);
	      tpl_pos = 1;
	    }
	  else
	    {
	      cci_rc =
		cci_cursor (stmt->stmthd, -(bookmark + fetch_offset),
			    CCI_CURSOR_LAST, &cci_err_buf);
	      tpl_pos = result_set_size + bookmark + fetch_offset + 1;
	      bound_state = 1;
	    }
	}
      else
	{
	  odbc_set_diag (stmt->diag, "HY111", 0, NULL);
	  return ODBC_ERROR;
	}
      break;

    default:
      odbc_set_diag (stmt->diag, "HYC00", 0, NULL);
      return ODBC_ERROR;
    }

  if (cci_rc < 0 && cci_rc != CCI_ER_NO_MORE_DATA)
    {
      odbc_set_diag_by_cci (stmt->diag, cci_rc, &cci_err_buf);
      return ODBC_ERROR;
    }
  else if (cci_rc == CCI_ER_NO_MORE_DATA)
    {
      if (bound_state == 1)
	{
	  *current_tpl_pos = 0;
	}
      else
	{
	  *current_tpl_pos = -1;
	}
      return ODBC_NO_MORE_DATA;
    }
  else
    {
      *current_tpl_pos = tpl_pos;
    }

  return ODBC_SUCCESS;
}


/************************************************************************
 * name: move_cursor
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
move_cursor (int req_handle, unsigned long *current_tpl_pos, ODBC_DIAG * diag)
{
  RETCODE rc = ODBC_SUCCESS;
  int cci_rc;
  T_CCI_ERROR cci_err_buf;

  if (*current_tpl_pos == 0)
    {
      cci_rc = cci_cursor (req_handle, 1, CCI_CURSOR_FIRST, &cci_err_buf);
    }
  else
    {
      cci_rc = cci_cursor (req_handle, 1, CCI_CURSOR_CURRENT, &cci_err_buf);
    }


  if (cci_rc < 0 && cci_rc != CCI_ER_NO_MORE_DATA && diag != NULL)
    {
      odbc_set_diag_by_cci (diag, cci_rc, &cci_err_buf);
      rc = ODBC_ERROR;
    }
  else if (cci_rc == CCI_ER_NO_MORE_DATA)
    {
      rc = ODBC_NO_MORE_DATA;
    }
  else
    {
      (*current_tpl_pos)++;
    }


  return rc;
}

PRIVATE RETCODE
move_catalog_rs (ODBC_STATEMENT * stmt, unsigned long *current_tpl_pos)
{
  if (*current_tpl_pos == 0)
    {
      stmt->catalog_result.current = HeadNode (stmt->catalog_result.value);
    }
  else
    {
      stmt->catalog_result.current = NextNode (stmt->catalog_result.current);
    }

  if (stmt->catalog_result.current == NULL)
    {
      return ODBC_NO_MORE_DATA;
    }

  ++(*current_tpl_pos);

  return ODBC_SUCCESS;
}


/************************************************************************
 * name:  fetch_data
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
fetch_tuple (int req_handle, ODBC_DIAG * diag, long sensitivity)
{
  int cci_rc;
  T_CCI_ERROR cci_err_buf;

  if (sensitivity == SQL_SENSITIVE)
    {
      cci_rc = cci_fetch_sensitive (req_handle, &cci_err_buf);
    }
  else
    {
      cci_rc = cci_fetch (req_handle, &cci_err_buf);
    }

  if (cci_rc < 0)
    {
      if (cci_rc == CCI_ER_DELETED_TUPLE)
	{
	  return ODBC_ROW_DELETED;
	}
      else
	{
	  if (diag != NULL)
	    {
	      odbc_set_diag_by_cci (diag, cci_rc, &cci_err_buf);
	    }
	  return ODBC_ERROR;
	}
    }

  return ODBC_SUCCESS;
}

/************************************************************************
 * name:  odbc_more_results
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
odbc_more_results (ODBC_STATEMENT * stmt)
{
  int cci_rc;
  T_CCI_ERROR cci_err_buf;
  T_CCI_COL_INFO *cci_col_info;
  int column_number;


  reset_result_set (stmt);
  reset_descriptor (stmt->ird);
  stmt->param_number = 0;

  cci_rc = cci_next_result (stmt->stmthd, &cci_err_buf);
  if (cci_rc < 0)
    {
      if (cci_rc == CAS_ER_NO_MORE_RESULT_SET)
	{
	  return ODBC_NO_DATA;
	}
      else if (cci_rc == CCI_ER_REQ_HANDLE)
	{
	  return ODBC_NO_DATA;
	}
      else
	{
	  goto error;
	}
    }
  stmt->tpl_number = cci_rc;

  stmt->result_type = QUERY;

  // get param number
  stmt->param_number =
    cci_get_bind_num (stmt->stmthd) - stmt->revised_sql.oid_param_num;

  // second argument is stmt->stmt_type
  cci_col_info =
    cci_get_result_info (stmt->stmthd, &stmt->stmt_type, &column_number);

  // check read-only mode
  if (stmt->conn->attr_access_mode == SQL_MODE_READ_ONLY &&
      !RESULTSET_STMT_TYPE (stmt->stmt_type))
    {
			odbc_set_diag (stmt->diag, "HY000", 0, "SQL Mode is read-only.");
      return ODBC_ERROR;
    }

  create_ird (stmt, cci_col_info, column_number);

  return ODBC_SUCCESS;

error:
  odbc_set_diag_by_cci (stmt->diag, cci_rc, &cci_err_buf);
  return ODBC_ERROR;
}


/*
 * check : TINYINT, BIGINT에 대해서 고려되지 않았다.
 * 현재 bound_ptr의 type은 c_value->type과 같다.
 */

PRIVATE RETCODE
c_value_to_bound_ptr (void *bound_ptr,
		      SQLLEN buffer_length, VALUE_CONTAINER * c_value)
{
  switch (c_value->type)
    {
    case SQL_C_SHORT:
    case SQL_C_USHORT:
    case SQL_C_SSHORT:
      *((short *) bound_ptr) = c_value->value.s;
      break;
    case SQL_C_LONG:
    case SQL_C_ULONG:
    case SQL_C_SLONG:
      *((long *) bound_ptr) = c_value->value.l;
      break;
    case SQL_C_UBIGINT:
    case SQL_C_SBIGINT:
      *((__int64 *) bound_ptr) = c_value->value.bi;
    case SQL_C_FLOAT:
      *((float *) bound_ptr) = c_value->value.f;
      break;
    case SQL_C_DOUBLE:
      *((double *) bound_ptr) = c_value->value.d;
      break;
    case SQL_C_CHAR:
      str_value_assign (c_value->value.str, bound_ptr, buffer_length, NULL);
      break;
    case SQL_C_BINARY:
      bin_value_assign (c_value->value.str, c_value->length, bound_ptr,
			buffer_length, NULL);
      break;
    case SQL_C_TYPE_DATE:
    case SQL_C_DATE:		// for 2.x backward compatibility
      ((SQL_DATE_STRUCT *) bound_ptr)->year = c_value->value.date.year;
      ((SQL_DATE_STRUCT *) bound_ptr)->month = c_value->value.date.month;
      ((SQL_DATE_STRUCT *) bound_ptr)->day = c_value->value.date.day;
      break;
    case SQL_C_TYPE_TIME:
    case SQL_C_TIME:		// for 2.x backward compatibility
      ((SQL_TIME_STRUCT *) bound_ptr)->hour = c_value->value.time.hour;
      ((SQL_TIME_STRUCT *) bound_ptr)->minute = c_value->value.time.minute;
      ((SQL_TIME_STRUCT *) bound_ptr)->second = c_value->value.time.second;
      break;
    case SQL_C_TYPE_TIMESTAMP:
    case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
      ((SQL_TIMESTAMP_STRUCT *) bound_ptr)->year = c_value->value.ts.year;
      ((SQL_TIMESTAMP_STRUCT *) bound_ptr)->month = c_value->value.ts.month;
      ((SQL_TIMESTAMP_STRUCT *) bound_ptr)->day = c_value->value.ts.day;
      ((SQL_TIMESTAMP_STRUCT *) bound_ptr)->hour = c_value->value.ts.hour;
      ((SQL_TIMESTAMP_STRUCT *) bound_ptr)->minute = c_value->value.ts.minute;
      ((SQL_TIMESTAMP_STRUCT *) bound_ptr)->second = c_value->value.ts.second;
      ((SQL_TIMESTAMP_STRUCT *) bound_ptr)->fraction =
	c_value->value.ts.fraction;
      break;
    case SQL_C_BIT:
    case SQL_C_NUMERIC:
      return ODBC_NOT_IMPLEMENTED;
    default:
      return ODBC_UNKNOWN_TYPE;
    }

  return ODBC_SUCCESS;

}
