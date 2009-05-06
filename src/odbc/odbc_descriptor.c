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

#include		<string.h>

#include		"odbc_portable.h"
#include		"odbc_descriptor.h"
#include		"odbc_diag_record.h"
#include		"odbc_util.h"
#include		"odbc_statement.h"
#include		"odbc_type.h"
#include		"sqlext.h"

#define			CUBRID_NUMERIC_PRECISION_DEFAULT	15
#define			CUBRID_NUMERIC_SCALE_DEFAULT		0

PRIVATE int is_header_field (short desc_field_id);
PRIVATE int odbc_consistency_check (ODBC_RECORD * record);
PRIVATE short is_read_only_field (short field_id);
PRIVATE void header_desc_field_copy (ODBC_DESC * source_desc,
				     ODBC_DESC * dest_desc);
PRIVATE void record_desc_field_copy (ODBC_DESC * source_desc,
				     ODBC_DESC * dest_desc);
PRIVATE short odbc_type_searchable (short type);

/************************************************************************
* name: odbc_alloc_desc
* arguments:
* returns/side-effects:
* description:
* NOTE:
*    con이 null이 아니면 con에 연결된 explicit desc로 간주한다.
************************************************************************/
PUBLIC RETCODE
odbc_alloc_desc (ODBC_CONNECTION * conn, ODBC_DESC ** desc_ptr)
{
  ODBC_DESC *desc_node;

  if (desc_ptr == NULL)
    {
      if (conn != NULL)
	odbc_set_diag (conn->diag, "HY090", 0, NULL);
      goto error;
    }

  desc_node = (ODBC_DESC *) UT_ALLOC (sizeof (ODBC_DESC));
  if (desc_node == NULL)
    {
      if (conn != NULL)
	odbc_set_diag (conn->diag, "HY001", 0, NULL);
      goto error;
    }

  /* init members */
  desc_node->handle_type = SQL_HANDLE_DESC;
  desc_node->diag = odbc_alloc_diag ();
  desc_node->stmt = NULL;	/* set in odbc_alloc_stmt */
  desc_node->conn = NULL;
  desc_node->records = NULL;

  desc_node->alloc_type = SQL_DESC_ALLOC_AUTO;
  desc_node->array_size = 1;
  desc_node->array_status_ptr = NULL;
  desc_node->bind_offset_ptr = NULL;
  desc_node->bind_type = SQL_BIND_BY_COLUMN;	/* equal to SQL_PARAM_BY_COLUMN */
  desc_node->max_count = 0;
  desc_node->rows_processed_ptr = NULL;

  /* for explicit */
  if (conn != NULL)
    {
      desc_node->alloc_type = SQL_DESC_ALLOC_USER;

      desc_node->next = conn->descriptors;
      conn->descriptors = desc_node;
    }

  *desc_ptr = desc_node;

  return ODBC_SUCCESS;

error:
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_free_desc
* arguments:
* returns/side-effects:
* description:
* NOTE:
*    desc->con이 null이 아니면 con에 연결된 explicit desc로 간주한다.
************************************************************************/
PUBLIC RETCODE
odbc_free_desc (ODBC_DESC * desc)
{
  ODBC_DESC *d, *prev;

  if (desc == NULL)
    {
      goto error;
    }

  // for explicit, remove link with connection handle
  if (desc->conn != NULL)
    {
      for (d = desc->conn->descriptors, prev = NULL;
	   d != NULL && d != desc; d = d->next)
	{
	  prev = d;
	}

      if (d == desc)
	{
	  if (prev != NULL)
	    prev->next = desc->next;
	  else
	    desc->conn->descriptors = desc->next;
	}
    }

  odbc_free_all_records (desc->records);
  desc->records = NULL;

  odbc_free_diag (desc->diag, FREE_ALL);
  desc->diag = NULL;

  UT_FREE (desc);

  return ODBC_SUCCESS;
error:
  return ODBC_ERROR;
}



/************************************************************************
* name: odbc_alloc_record
* arguments:
* returns/side-effects:
* description:
* NOTE:
*    record handle은 정의도 안 되었을뿐만 아니라, 외부에서 직접적으로 사용할
*    일이 없으므로 NULL을 허용한다.
*	- error messaging
************************************************************************/
PUBLIC RETCODE
odbc_alloc_record (ODBC_DESC * desc, ODBC_RECORD ** rec, int rec_number)
{
  ODBC_RECORD *rec_node;

  if (desc == NULL)
    {
      goto error;
    }
  if (rec_number < 0)
    {
      goto error;
    }

  if (find_record_from_desc (desc, rec_number) != NULL)
    {
      /* alreay existing record */
      goto error;
    }


  rec_node = (ODBC_RECORD *) UT_ALLOC (sizeof (ODBC_RECORD));
  if (rec_node == NULL)
    {
      goto error;
    }

  if (rec_number > desc->max_count)
    {
      desc->max_count = rec_number;
    }


  /* init record members */
  rec_node->auto_unique_value = SQL_FALSE;
  rec_node->base_column_name = NULL;
  rec_node->base_table_name = NULL;
  rec_node->case_sensitive = SQL_FALSE;
  rec_node->catalog_name = NULL;
  rec_node->concise_type = SQL_C_DEFAULT;
  rec_node->data_ptr = NULL;
  rec_node->datetime_interval_code = 0;
  rec_node->datetime_interval_precision = 0;
  rec_node->display_size = 0;
  rec_node->fixed_prec_scale = SQL_FALSE;
  rec_node->indicator_ptr = NULL;
  rec_node->length = 0;		/* ??? */
  rec_node->literal_prefix = NULL;
  rec_node->literal_suffix = NULL;
  rec_node->local_type_name = NULL;
  rec_node->name = NULL;
  rec_node->nullable = SQL_NULLABLE_UNKNOWN;
  rec_node->num_prec_radix = 0;
  rec_node->octet_length = 0;
  rec_node->octet_length_ptr = NULL;
  rec_node->parameter_type = SQL_PARAM_INPUT;
  rec_node->precision = 0;
  rec_node->rowver = SQL_FALSE;
  rec_node->schema_name = NULL;
  rec_node->searchable = SQL_PRED_NONE;
  rec_node->table_name = NULL;
  rec_node->type = SQL_C_DEFAULT;
  rec_node->type_name = NULL;
  rec_node->unnamed = SQL_UNNAMED;
  rec_node->unsigned_type = SQL_FALSE;
  rec_node->updatable = SQL_ATTR_READWRITE_UNKNOWN;

  rec_node->record_number = rec_number;
  rec_node->desc = desc;
  rec_node->next = desc->records;

  desc->records = rec_node;


  if (rec != NULL)
    {
      *rec = rec_node;
    }

  return ODBC_SUCCESS;
error:
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_free_record
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_free_record (ODBC_RECORD * record)
{
  ODBC_RECORD *r, *prev;


  if (record == NULL)
    {
      goto error;
    }


  NA_FREE (record->base_column_name);
  NA_FREE (record->base_table_name);
  NA_FREE (record->catalog_name);
  NA_FREE (record->literal_prefix);
  NA_FREE (record->literal_suffix);
  NA_FREE (record->local_type_name);
  NA_FREE (record->name);
  NA_FREE (record->schema_name);
  NA_FREE (record->table_name);
  NA_FREE (record->type_name);

  // remove link from descriptor
  if (record->desc != NULL)
    {
      for (r = record->desc->records, prev = NULL; r != NULL && r != record;
	   r = r->next)
	{
	  prev = r;
	}

      if (r == record)
	{
	  if (prev != NULL)
	    prev->next = record->next;
	  else
	    record->desc->records = record->next;
	}
    }

  UT_FREE (record);

  return ODBC_SUCCESS;
error:
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_free_all_records
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_free_all_records (ODBC_RECORD * head_record)
{
  ODBC_RECORD *now, *deleted_node;

  if (head_record == NULL)
    {
      goto error;
    }

  for (now = head_record; now != NULL;)
    {
      deleted_node = now;
      now = now->next;
      odbc_free_record (deleted_node);
      deleted_node = NULL;
    }

  return ODBC_SUCCESS;
error:
  return ODBC_ERROR;
}


/************************************************************************
* name: odbc_get_desc_field
* arguments:
* returns/side-effects:
* description:
* NOTE:
*	BASE_COLUMN_NAME, NAME, LABEL are same as SQL_DESC_NAME
************************************************************************/
PUBLIC RETCODE
odbc_get_desc_field (ODBC_DESC * desc,
		     SQLSMALLINT rec_number,
		     SQLSMALLINT field_id,
		     SQLPOINTER value_ptr,
		     SQLLEN buffer_length, SQLLEN *string_length_ptr)
{
  RETCODE status = ODBC_SUCCESS, rc;
  ODBC_RECORD *record;
  char *pt;
  char empty_str[1] = "";

  if (is_header_field (field_id) == 1)
    {
      /* header field */

      switch (field_id)
	{
	case SQL_DESC_COUNT:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = desc->max_count;

	  if (string_length_ptr != NULL)
	    *string_length_ptr = sizeof (desc->max_count);
	  break;

	case SQL_DESC_ALLOC_TYPE:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = desc->alloc_type;

	  if (string_length_ptr != NULL)
	    *string_length_ptr = sizeof (desc->alloc_type);
	  break;

	case SQL_DESC_ARRAY_SIZE:
	  if (value_ptr != NULL)
	    *(unsigned long *) value_ptr = desc->array_size;

	  if (string_length_ptr != NULL)
	    *string_length_ptr = sizeof (desc->array_status_ptr);
	  break;

	case SQL_DESC_ARRAY_STATUS_PTR:
	  if (value_ptr != NULL)
	    *(unsigned short **) value_ptr = desc->array_status_ptr;

	  if (string_length_ptr != NULL)
	    *string_length_ptr = sizeof (desc->array_status_ptr);
	  break;

	case SQL_DESC_BIND_OFFSET_PTR:
	  if (value_ptr != NULL)
	    *(long **) value_ptr = desc->bind_offset_ptr;

	  if (string_length_ptr != NULL)
	    *string_length_ptr = sizeof (desc->bind_offset_ptr);
	  break;

	case SQL_DESC_BIND_TYPE:
	  if (value_ptr != NULL)
	    *(long *) value_ptr = desc->bind_type;

	  if (string_length_ptr != NULL)
	    *string_length_ptr = sizeof (desc->bind_type);
	  break;

	case SQL_DESC_ROWS_PROCESSED_PTR:
	  if (value_ptr != NULL)
	    *(unsigned long **) value_ptr = desc->rows_processed_ptr;

	  if (string_length_ptr != NULL)
	    *string_length_ptr = sizeof (desc->rows_processed_ptr);
	  break;

	default:
	  /* unknown field */
	  odbc_set_diag (desc->diag, "HY091", 0, NULL);
	  goto error;
	}

    }
  else
    {
      /* record filed */

      if (rec_number < 0)
	{
	  odbc_set_diag (desc->diag, "07009", 0, NULL);
	  goto error;
	}

      record = find_record_from_desc (desc, rec_number);
      if (record == NULL)
	{
	  odbc_set_diag (desc->diag, "07009", 0, NULL);
	  goto error;
	}

      switch (field_id)
	{

	case SQL_DESC_BASE_COLUMN_NAME:
	case SQL_DESC_LABEL:
	case SQL_DESC_NAME:
	  if (record->name != NULL)
	    {
	      pt = record->name;
	    }
	  else
	    {
	      pt = empty_str;
	    }
	  rc =
	    str_value_assign (pt, value_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      odbc_set_diag (desc->diag, "01004", 0, NULL);
	    }
	  break;


	case SQL_DESC_CASE_SENSITIVE:
	  if (value_ptr != NULL)
	    *(long *) value_ptr = record->case_sensitive;

	  if (string_length_ptr != NULL)
	    *string_length_ptr = sizeof (record->case_sensitive);
	  break;


	case SQL_DESC_CONCISE_TYPE:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->concise_type;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->concise_type);
	    }
	  break;

	case SQL_DESC_DATA_PTR:
	  if (value_ptr != NULL)
	    *(void **) value_ptr = record->data_ptr;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->data_ptr);
	    }
	  break;

	case SQL_DESC_DATETIME_INTERVAL_CODE:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->datetime_interval_code;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->datetime_interval_code);
	    }
	  break;

	case SQL_DESC_DATETIME_INTERVAL_PRECISION:
	  if (value_ptr != NULL)
	    *(long *) value_ptr = record->datetime_interval_precision;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr =
		sizeof (record->datetime_interval_precision);
	    }
	  break;

	case SQL_DESC_DISPLAY_SIZE:
	  if (value_ptr != NULL)
	    *(long *) value_ptr = record->display_size;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->display_size);
	    }
	  break;

	case SQL_DESC_FIXED_PREC_SCALE:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->fixed_prec_scale;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->fixed_prec_scale);
	    }
	  break;

	case SQL_DESC_INDICATOR_PTR:
	  if (value_ptr != NULL)
	    *(long **) value_ptr = record->indicator_ptr;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->indicator_ptr);
	    }
	  break;


	case SQL_DESC_LENGTH:
	case SQL_COLUMN_LENGTH:	// for 2.x backward compatibility
	  if (value_ptr != NULL)
	    *(unsigned long *) value_ptr = record->length;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->length);
	    }
	  break;

	case SQL_DESC_LITERAL_PREFIX:
	  if (record->literal_prefix != NULL)
	    {
	      pt = record->literal_prefix;
	    }
	  else
	    {
	      pt = empty_str;
	    }

	  rc =
	    str_value_assign (pt, value_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      odbc_set_diag (desc->diag, "01004", 0, NULL);
	    }
	  break;

	case SQL_DESC_LITERAL_SUFFIX:
	  if (record->literal_suffix != NULL)
	    {
	      pt = record->literal_suffix;
	    }
	  else
	    {
	      pt = empty_str;
	    }
	  rc =
	    str_value_assign (pt, value_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      odbc_set_diag (desc->diag, "01004", 0, NULL);
	    }
	  break;

	case SQL_DESC_LOCAL_TYPE_NAME:
	  if (record->local_type_name != NULL)
	    {
	      pt = record->local_type_name;
	    }
	  else
	    {
	      pt = empty_str;
	    }

	  rc =
	    str_value_assign (pt, value_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      odbc_set_diag (desc->diag, "01004", 0, NULL);
	    }
	  break;


	case SQL_DESC_NULLABLE:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->nullable;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->nullable);
	    }
	  break;

	case SQL_DESC_NUM_PREC_RADIX:
	  if (value_ptr != NULL)
	    *(long *) value_ptr = record->num_prec_radix;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->num_prec_radix);
	    }
	  break;

	case SQL_DESC_OCTET_LENGTH:
	  if (value_ptr != NULL)
	    *(long *) value_ptr = record->octet_length;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->octet_length);
	    }
	  break;

	case SQL_DESC_OCTET_LENGTH_PTR:
	  if (value_ptr != NULL)
	    *(long **) value_ptr = record->octet_length_ptr;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->octet_length_ptr);
	    }
	  break;

	case SQL_DESC_PARAMETER_TYPE:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->parameter_type;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->parameter_type);
	    }
	  break;

	case SQL_DESC_PRECISION:
	case SQL_COLUMN_PRECISION:	// for 2.x backward compatibility
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->precision;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->precision);
	    }
	  break;

	case SQL_DESC_SCALE:
	case SQL_COLUMN_SCALE:	// for 2.x backward compatibility
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->scale;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->scale);
	    }
	  break;

	case SQL_DESC_SEARCHABLE:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->searchable;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->searchable);
	    }
	  break;

	case SQL_DESC_TABLE_NAME:
	case SQL_DESC_BASE_TABLE_NAME:	/* 따로 지원하지 않고 TABLE_NAME과 같이 쓴다. */
	  if (record->table_name != NULL)
	    {
	      pt = record->table_name;
	    }
	  else
	    {
	      pt = empty_str;
	    }

	  rc =
	    str_value_assign (pt, value_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      odbc_set_diag (desc->diag, "01004", 0, NULL);
	    }
	  break;


	case SQL_DESC_TYPE:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->type;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->type);
	    }
	  break;

	case SQL_DESC_TYPE_NAME:
	  if (record->type_name != NULL)
	    {
	      pt = record->type_name;
	    }
	  else
	    {
	      pt = empty_str;
	    }

	  rc =
	    str_value_assign (pt, value_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      odbc_set_diag (desc->diag, "01004", 0, NULL);
	    }
	  break;

	case SQL_DESC_UNNAMED:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->unnamed;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->unnamed);
	    }
	  break;

	case SQL_DESC_UNSIGNED:
	  if (value_ptr != NULL)
	    *(short *) value_ptr = record->unsigned_type;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (record->unsigned_type);
	    }
	  break;

	case SQL_DESC_AUTO_UNIQUE_VALUE:
	  // there is no auto unique attribute in CUBRID
	  if (value_ptr != NULL)
	    *(long *) value_ptr = SQL_FALSE;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (long);
	    }
	  break;

	case SQL_DESC_CATALOG_NAME:
	case SQL_DESC_SCHEMA_NAME:
	  // empty string
	  rc =
	    str_value_assign (empty_str, value_ptr, buffer_length,
			      string_length_ptr);
	  if (rc == ODBC_SUCCESS_WITH_INFO)
	    {
	      odbc_set_diag (desc->diag, "01004", 0, NULL);
	    }
	  break;

	case SQL_DESC_ROWVER:
	  // there is no rowver attribute in CUBRID
	  if (value_ptr != NULL)
	    *(short *) value_ptr = SQL_FALSE;

	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (short);
	    }
	  break;


	case SQL_DESC_UPDATABLE:
	  // always READWRITE_UNKNOWN
	  if (value_ptr != NULL)
	    *(short *) value_ptr = SQL_ATTR_READWRITE_UNKNOWN;


	  if (string_length_ptr != NULL)
	    {
	      *string_length_ptr = sizeof (short);
	    }
	  break;

	default:
	  odbc_set_diag (desc->diag, "HY091", 0, NULL);
	  goto error;
	}

    }

  return status;
error:
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_get_desc_rec
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_get_desc_rec (ODBC_DESC * desc,
		   SQLSMALLINT rec_number,
		   SQLCHAR *name,
		   SQLSMALLINT buffer_length,
		   SQLSMALLINT *string_length_ptr,
		   SQLSMALLINT *type_ptr,
		   SQLSMALLINT *subtype_ptr,
		   SQLLEN *length_ptr,
		   SQLSMALLINT *precision_ptr,
		   SQLSMALLINT *scale_ptr, SQLSMALLINT *nullable_ptr)
{
  ODBC_RECORD *record = NULL;
  SQLLEN tmp_length;

  record = find_record_from_desc (desc, rec_number);
  if (record == NULL)
    {
      odbc_set_diag (desc->diag, "07009", 0, NULL);
      return ODBC_NO_DATA;
    }


  /* WARN : type converting  string_length_ptr(short*) -> (long*) */
  odbc_get_desc_field (desc, rec_number, SQL_DESC_NAME,
		       (void *) name, buffer_length,
                       &tmp_length);
  *string_length_ptr = (SQLSMALLINT) tmp_length;

  odbc_get_desc_field (desc, rec_number, SQL_DESC_TYPE,
		       (void *) type_ptr, 0, NULL);

  odbc_get_desc_field (desc, rec_number, SQL_DESC_DATETIME_INTERVAL_CODE,
		       (void *) subtype_ptr, 0, NULL);

  odbc_get_desc_field (desc, rec_number, SQL_DESC_OCTET_LENGTH,
		       (void *) length_ptr, 0, NULL);

  odbc_get_desc_field (desc, rec_number, SQL_DESC_PRECISION,
		       (void *) precision_ptr, 0, NULL);

  odbc_get_desc_field (desc, rec_number, SQL_DESC_SCALE,
		       (void *) scale_ptr, 0, NULL);

  odbc_get_desc_field (desc, rec_number, SQL_DESC_NULLABLE,
		       (void *) nullable_ptr, 0, NULL);

  return ODBC_SUCCESS;
}


/************************************************************************
* name: odbc_set_desc_field
* arguments:
* returns/side-effects:
* description:
* NOTE:
*	CHECK : consistency
*	일부 consistency check가 odbc_set_desc_rec에서 이루어진다. 참고..
************************************************************************/
PUBLIC RETCODE
odbc_set_desc_field (ODBC_DESC * desc,
		     short rec_number,
		     short field_id,
		     void *value_ptr, long buffer_length, short is_driver)
{
  ODBC_RECORD *record;
  RETCODE status = ODBC_SUCCESS;
  int i;

  if (is_driver != 1 && is_read_only_field (field_id) == _TRUE_)
    {
      odbc_set_diag (desc->diag, "HY091", 0, NULL);
      goto error;
    }

  if (is_header_field (field_id) == 1)
    {
      /* header field */
      switch (field_id)
	{
	case SQL_DESC_ALLOC_TYPE:
	  desc->alloc_type = (short) value_ptr;
	  break;
	case SQL_DESC_ARRAY_SIZE:
	  desc->array_size = (unsigned long) value_ptr;
	  break;
	case SQL_DESC_ARRAY_STATUS_PTR:
	  desc->array_status_ptr = value_ptr;
	  break;
	case SQL_DESC_BIND_OFFSET_PTR:
	  desc->bind_offset_ptr = value_ptr;
	  break;
	case SQL_DESC_BIND_TYPE:
	  desc->bind_type = (long) value_ptr;
	  break;
	case SQL_DESC_COUNT:
	  if ((short) value_ptr < desc->max_count)
	    {
	      for (i = desc->max_count; i > (short) value_ptr && i > 0; --i)
		{
		  record = find_record_from_desc (desc, i);
		  if (record != NULL)
		    {
		      odbc_free_record (record);
		    }
		}
	    }
	  desc->max_count = (short) value_ptr;
	  break;
	case SQL_DESC_ROWS_PROCESSED_PTR:
	  desc->rows_processed_ptr = value_ptr;
	  break;
	default:
	  odbc_set_diag (desc->diag, "HY091", 0, NULL);
	  goto error;
	}

    }
  else
    {
      /* record filed */
      if (rec_number < 0)
	{
	  odbc_set_diag (desc->diag, "07009", 0, NULL);
	  goto error;
	}

      record = find_record_from_desc (desc, rec_number);
      if (record == NULL)
	{
	  odbc_alloc_record (desc, &record, rec_number);
	}

      switch (field_id)
	{

	case SQL_DESC_BASE_COLUMN_NAME:
	case SQL_DESC_LABEL:
	case SQL_DESC_NAME:
	  NC_FREE (record->name);
	  record->name = UT_MAKE_STRING (value_ptr, buffer_length);
	  break;

	case SQL_DESC_CASE_SENSITIVE:
	  record->case_sensitive = (long) value_ptr;
	  break;

	case SQL_DESC_DISPLAY_SIZE:
	  record->display_size = (long) value_ptr;
	  break;
	case SQL_DESC_FIXED_PREC_SCALE:
	  record->fixed_prec_scale = (short) value_ptr;
	  break;
	case SQL_DESC_LITERAL_PREFIX:
	  NC_FREE (record->literal_prefix);
	  record->literal_prefix = UT_MAKE_STRING (value_ptr, buffer_length);
	  break;
	case SQL_DESC_LITERAL_SUFFIX:
	  NC_FREE (record->literal_suffix);
	  record->literal_suffix = UT_MAKE_STRING (value_ptr, buffer_length);
	  break;
	case SQL_DESC_LOCAL_TYPE_NAME:
	  NC_FREE (record->local_type_name);
	  record->local_type_name = UT_MAKE_STRING (value_ptr, buffer_length);
	  break;
	case SQL_DESC_NULLABLE:
	  record->nullable = (short) value_ptr;
	  break;

	case SQL_DESC_SEARCHABLE:
	  record->searchable = (short) value_ptr;
	  break;
	  /* BASE_TABLE_NAME과 TABLE_NAME과의 구분은 없다. */
	case SQL_DESC_TABLE_NAME:
	case SQL_DESC_BASE_TABLE_NAME:
	  NC_FREE (record->table_name);
	  record->table_name = UT_MAKE_STRING (value_ptr, buffer_length);
	  break;
	case SQL_DESC_TYPE_NAME:
	  NC_FREE (record->type_name);
	  record->type_name = UT_MAKE_STRING (value_ptr, buffer_length);
	  break;
	case SQL_DESC_UNSIGNED:
	  record->unsigned_type = (short) value_ptr;
	  break;

	case SQL_DESC_CONCISE_TYPE:
	  if (!odbc_is_valid_type ((short) value_ptr))
	    {
	      odbc_set_diag (desc->diag, "HY021", 0, NULL);
	      goto error;
	    }
	  record->concise_type = (short) value_ptr;
	  break;

	case SQL_DESC_DATA_PTR:
	  if (!odbc_consistency_check (record))
	    {
	      odbc_set_diag (desc->diag, "HY021", 0, NULL);
	      goto error;
	    }
	  record->data_ptr = value_ptr;
	  break;

	case SQL_DESC_DATETIME_INTERVAL_CODE:
	  if (!odbc_is_valid_code ((short) value_ptr))
	    {
	      odbc_set_diag (desc->diag, "HY021", 0, NULL);
	      goto error;
	    }
	  record->datetime_interval_code = (short) value_ptr;
	  break;

	case SQL_DESC_DATETIME_INTERVAL_PRECISION:
	  record->datetime_interval_precision = (short) value_ptr;
	  break;

	case SQL_DESC_INDICATOR_PTR:
	  record->indicator_ptr = value_ptr;
	  break;

	case SQL_DESC_LENGTH:
	  record->length = (unsigned long) value_ptr;
	  break;

	case SQL_DESC_NUM_PREC_RADIX:
	  record->num_prec_radix = (long) value_ptr;
	  break;

	case SQL_DESC_OCTET_LENGTH:
	  record->octet_length = (long) value_ptr;
	  break;

	case SQL_DESC_OCTET_LENGTH_PTR:
	  record->octet_length_ptr = value_ptr;
	  break;

	case SQL_DESC_PARAMETER_TYPE:
	  if ((short) value_ptr == SQL_PARAM_INPUT)
	    {
	      record->parameter_type = (short) value_ptr;
	    }
	  else
	    {
	      odbc_set_diag (desc->diag, "HY091", 0, NULL);
	      goto error;
	    }
	  break;

	case SQL_DESC_PRECISION:
	  record->precision = (short) value_ptr;
	  break;

	case SQL_DESC_SCALE:
	  record->scale = (short) value_ptr;
	  break;

	case SQL_DESC_TYPE:
	  switch ((short) value_ptr)
	    {
	    case SQL_CHAR:	/* SQL_C_CHAR */
	    case SQL_VARCHAR:
	      record->type = (short) value_ptr;
	      record->length = 1;
	      record->precision = 0;
	      break;

	    case SQL_DATETIME:
	      record->type = (short) value_ptr;
	      switch (record->datetime_interval_code)
		{
		case SQL_CODE_DATE:
		case SQL_CODE_TIME:
		  record->precision = 0;
		  break;
		case SQL_CODE_TIMESTAMP:
		  record->precision = 6;
		  break;
		}
	      break;

	    case SQL_DECIMAL:
	    case SQL_NUMERIC:	/* SQL_C_NUMERIC */
	      record->type = (short) value_ptr;
	      record->precision = CUBRID_NUMERIC_PRECISION_DEFAULT;
	      record->scale = CUBRID_NUMERIC_SCALE_DEFAULT;
	      break;

	    case SQL_FLOAT:
	    case SQL_C_FLOAT:
	      record->type = (short) value_ptr;
	      /* WARN : NEED precision setting as default value */
	      break;

	    case SQL_INTERVAL:
	      odbc_set_diag (desc->diag, "HYC00", 0, NULL);
	      goto error;


	    case SQL_C_UBIGINT:
	    case SQL_C_SBIGINT:
	    case SQL_C_STINYINT:
	    case SQL_C_UTINYINT:
	    case SQL_C_TINYINT:	// for 2.x backward compatibility
	    case SQL_C_SHORT:	// for 2.x backward compatibility
	    case SQL_C_SSHORT:
	    case SQL_C_USHORT:
	    case SQL_C_LONG:
	    case SQL_C_SLONG:
	    case SQL_C_ULONG:
	    case SQL_C_GUID:
	    case SQL_BIT:
	    case SQL_BIGINT:
	    case SQL_LONGVARBINARY:
	    case SQL_VARBINARY:
	    case SQL_BINARY:
	    case SQL_LONGVARCHAR:
	    case SQL_C_DOUBLE:
	    case SQL_C_DEFAULT:
	    case SQL_TYPE_TIME:
	    case SQL_TYPE_TIMESTAMP:
	    case SQL_C_UNI_OBJECT:
	    case SQL_C_UNI_SET:
	      record->type = (short) value_ptr;
	      break;

	    default:
	      odbc_set_diag (desc->diag, "HY024", 0, NULL);
	      goto error;
	    }
	  break;

	case SQL_DESC_UNNAMED:
	  switch ((short) value_ptr)
	    {
	    case SQL_NAMED:
	    case SQL_UNNAMED:
	      record->unnamed = (short) value_ptr;
	      break;
	    default:
	      odbc_set_diag (desc->diag, "HY024", 0, NULL);
	      goto error;
	    }
	  break;

	case SQL_DESC_UPDATABLE:
	  record->updatable = (short) value_ptr;
	  break;

	case SQL_DESC_AUTO_UNIQUE_VALUE:
	case SQL_DESC_CATALOG_NAME:
	case SQL_DESC_ROWVER:
	case SQL_DESC_SCHEMA_NAME:
	  odbc_set_diag (desc->diag, "HYC00", 0, NULL);
	  goto error;

	default:
	  /* unknown field */
	  odbc_set_diag (desc->diag, "HY091", 0, NULL);
	  goto error;
	}

    }

  return status;
error:
  return ODBC_ERROR;
}


/************************************************************************
* name: odbc_set_desc_rec
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_set_desc_rec (ODBC_DESC * desc,
		   SQLSMALLINT rec_number,
		   SQLSMALLINT type,
		   SQLSMALLINT subtype,
		   SQLLEN length,
		   SQLSMALLINT precision,
		   SQLSMALLINT scale,
		   SQLPOINTER data_ptr,
		   SQLLEN *string_length_ptr, SQLLEN *indicator_ptr)
{
  /* ODBC_RECORD *record; */
  short concise_type;
  ODBC_RECORD *record;


  record = find_record_from_desc (desc, rec_number);
  if (record == NULL)
    {
      odbc_set_diag (desc->diag, "07009", 0, NULL);
      return ODBC_NO_DATA;
    }

  odbc_set_desc_field (desc, rec_number, SQL_DESC_TYPE,
		       (void *) type, sizeof (type), 1);

  if (subtype == SQL_DATETIME || subtype == SQL_INTERVAL)
    {
      odbc_set_desc_field (desc, rec_number, SQL_DESC_DATETIME_INTERVAL_CODE,
			   (void *) subtype, sizeof (subtype), 1);
    }

  concise_type = odbc_verbose_to_concise_type (type, subtype);
  odbc_set_desc_field (desc, rec_number, SQL_DESC_CONCISE_TYPE,
		       (void *) concise_type, sizeof (concise_type), 1);

  odbc_set_desc_field (desc, rec_number, SQL_DESC_OCTET_LENGTH,
		       (void *) length, sizeof (length), 1);

  odbc_set_desc_field (desc, rec_number, SQL_DESC_PRECISION,
		       (void *) precision, sizeof (precision), 1);

  odbc_set_desc_field (desc, rec_number, SQL_DESC_SCALE,
		       (void *) scale, sizeof (scale), 1);

  odbc_set_desc_field (desc, rec_number, SQL_DESC_DATA_PTR,
		       (void *) data_ptr, sizeof (data_ptr), 1);

  odbc_set_desc_field (desc, rec_number, SQL_DESC_OCTET_LENGTH_PTR,
		       (void *) string_length_ptr, sizeof (string_length_ptr),
		       1);

  odbc_set_desc_field (desc, rec_number, SQL_DESC_INDICATOR_PTR,
		       (void *) indicator_ptr, sizeof (indicator_ptr), 1);

  return ODBC_SUCCESS;
}



/************************************************************************
* name: odbc_copy_desc
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_copy_desc (ODBC_DESC * source_desc, ODBC_DESC * dest_desc)
{

  if (source_desc == NULL || dest_desc == NULL)
    {
      return SQL_INVALID_HANDLE;
    }

  // reset dest_desc
  odbc_set_desc_field (dest_desc, 0, SQL_DESC_COUNT, (void *) 0, 0, 1);

  header_desc_field_copy (source_desc, dest_desc);
  record_desc_field_copy (source_desc, dest_desc);

  return ODBC_SUCCESS;
}

/************************************************************************
* name:
* arguments:
* returns/side-effects:
* description:
* NOTE:
* if desc->stmt == NULL, desc is explicitly allocated descriptor and
* yet not assigned to any statement.
************************************************************************/
PUBLIC int
odbc_is_ird (ODBC_DESC * desc)
{
  return ((desc->stmt != NULL) && (desc == desc->stmt->ird));
}

/************************************************************************
* name: odbc_set_ird
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC void
odbc_set_ird (ODBC_STATEMENT * stmt,
	      short column_number,
	      short type,
	      char *table_name,
	      char *column_name,
	      long precision, short scale, short nullable, short updatable)
{
  short verbose_type;
  long display_size;
  long octet_length;
  ODBC_RECORD *record;
  short searchable;

  verbose_type = odbc_concise_to_verbose_type (type);
  display_size = odbc_display_size (type, precision);
  octet_length = odbc_octet_length (type, precision);


  // set ird field
  record = find_record_from_desc (stmt->ird, column_number);
  if (record == NULL)
    {
      odbc_alloc_record (stmt->ird, &record, column_number);
    }

  odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_TABLE_NAME,
		       (SQLPOINTER) table_name, SQL_NTS, 1);
  odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_NAME,
		       (SQLPOINTER) column_name, SQL_NTS, 1);
  odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_CONCISE_TYPE,
		       (SQLPOINTER) type, 0, 1);
  odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_TYPE_NAME,
		       (SQLPOINTER) odbc_type_name (type), SQL_NTS, 1);
  odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_TYPE,
		       (SQLPOINTER) verbose_type, 0, 1);

#ifdef DELPHI
  if (type == SQL_LONGVARCHAR)
    {
      odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_LOCAL_TYPE_NAME,
			   (SQLPOINTER) "string", SQL_NTS, 1);
    }
#endif

  if (IS_STRING_TYPE (type) || IS_BINARY_TYPE (type))
    {
      odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_PRECISION,
			   (SQLPOINTER) 0, 0, 1);
      // precision에 대해서 정의하고 있지 않다.
    }
  else if (odbc_is_valid_sql_date_type (type))
    {
      odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_PRECISION,
			   (SQLPOINTER) 0, 0, 1);
      // CUBRID는 date type에 대해서 precision(for second)은 0이다.
      // date type에 대한 length는 char형색의 display size와 같다.
    }
  else
    {
      odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_PRECISION,
			   (SQLPOINTER) precision, 0, 1);
    }

  if (type == SQL_NUMERIC)
    odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_LENGTH,
			 (SQLPOINTER) precision, 0, 1);
  else
    odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_LENGTH,
			 (SQLPOINTER) display_size, 0, 1);
  //odbc_set_desc_field(stmt->ird, column_number, SQL_DESC_LENGTH, (SQLPOINTER)display_size, 0, 1);
  odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_OCTET_LENGTH,
		       (SQLPOINTER) octet_length, 0, 1);
  odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_SCALE,
		       (SQLPOINTER) scale, 0, 1);
  odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_DISPLAY_SIZE,
		       (SQLPOINTER) display_size, 0, 1);

  searchable = odbc_type_searchable (type);
  odbc_set_desc_field (stmt->ird, column_number, SQL_DESC_SEARCHABLE,
		       (SQLPOINTER) searchable, 0, 1);
}

/************************************************************************
* name: find_record_from_desc
* arguments:
* returns/side-effects: record pointer
* description:
* NOTE:
************************************************************************/
PUBLIC ODBC_RECORD *
find_record_from_desc (ODBC_DESC * desc, int rec_number)
{
  ODBC_RECORD *rec = NULL;

  if (desc == NULL || desc->records == NULL)
    {
      return NULL;
    }

  if (rec_number > desc->max_count)
    {
      return NULL;
    }

  for (rec = desc->records; rec != NULL; rec = rec->next)
    {
      if (rec->record_number == rec_number)
	{
	  return rec;
	}
    }

  return NULL;
}

/************************************************************************
* name: reset_descriptor
* arguments:
* returns/side-effects: record pointer
* description:
* NOTE:
*		SQL_DESC_ROWS_PROCESSED_PTR,
*		SQL_DESC_ARRAY_STATUS_PTR의 경우에는 사용자에 의해서 설정되므로
*		delete하지 말아야 한다.
************************************************************************/
PUBLIC void
reset_descriptor (ODBC_DESC * desc)
{
  if (desc == NULL)
    return;

  odbc_set_desc_field (desc, 0, SQL_DESC_ARRAY_SIZE, (void *) 1, 0, 1);

  odbc_set_desc_field (desc, 0, SQL_DESC_BIND_OFFSET_PTR, NULL, 0, 1);
  odbc_set_desc_field (desc, 0, SQL_DESC_BIND_TYPE,
		       (void *) SQL_BIND_BY_COLUMN, 0, 1);
  odbc_set_desc_field (desc, 0, SQL_DESC_COUNT, 0, 0, 1);
  // odbc_set_desc_field(desc, 0, SQL_DESC_ROWS_PROCESSED_PTR, NULL, 0, 1);
  // odbc_set_desc_field(desc, 0, SQL_DESC_ARRAY_STATUS_PTR, NULL, 0, 1);

}

/************************************************************************
* name: is_header_field
* arguments:
* returns/side-effects:
*  1 - header, 0 - record, else(-1) - unknown
* description:
* NOTE:
************************************************************************/
PRIVATE int
is_header_field (short desc_field_id)
{
  int rv = -1;

  switch (desc_field_id)
    {
    case SQL_DESC_ALLOC_TYPE:
    case SQL_DESC_ARRAY_SIZE:
    case SQL_DESC_ARRAY_STATUS_PTR:
    case SQL_DESC_BIND_OFFSET_PTR:
    case SQL_DESC_BIND_TYPE:
    case SQL_DESC_COUNT:
    case SQL_DESC_ROWS_PROCESSED_PTR:
      rv = 1;
      break;

    case SQL_DESC_AUTO_UNIQUE_VALUE:
    case SQL_DESC_BASE_COLUMN_NAME:
    case SQL_DESC_BASE_TABLE_NAME:
    case SQL_DESC_CASE_SENSITIVE:
    case SQL_DESC_CATALOG_NAME:
    case SQL_DESC_CONCISE_TYPE:
    case SQL_DESC_DATA_PTR:
    case SQL_DESC_DATETIME_INTERVAL_CODE:
    case SQL_DESC_DATETIME_INTERVAL_PRECISION:
    case SQL_DESC_DISPLAY_SIZE:
    case SQL_DESC_FIXED_PREC_SCALE:
    case SQL_DESC_INDICATOR_PTR:
    case SQL_DESC_LABEL:
    case SQL_DESC_LENGTH:
    case SQL_DESC_LITERAL_PREFIX:
    case SQL_DESC_LITERAL_SUFFIX:
    case SQL_DESC_LOCAL_TYPE_NAME:
    case SQL_DESC_NAME:
    case SQL_DESC_NULLABLE:
    case SQL_DESC_NUM_PREC_RADIX:
    case SQL_DESC_OCTET_LENGTH:
    case SQL_DESC_OCTET_LENGTH_PTR:
    case SQL_DESC_PARAMETER_TYPE:
    case SQL_DESC_PRECISION:
    case SQL_DESC_ROWVER:
    case SQL_DESC_SCALE:
    case SQL_DESC_SCHEMA_NAME:
    case SQL_DESC_SEARCHABLE:
    case SQL_DESC_TABLE_NAME:
    case SQL_DESC_TYPE:
    case SQL_DESC_TYPE_NAME:
    case SQL_DESC_UNNAMED:
    case SQL_DESC_UNSIGNED:
    case SQL_DESC_UPDATABLE:
      rv = 0;
      break;

    default:
      rv = -1;
      break;
    }

  return rv;
}

/************************************************************************
* name:
* arguments:
* returns/side-effects:
* description:
* NOTE: whenever an application sets the SQL_DESC_DATA_PTR record field of an
* APD, ARD, or IPD. If there is incosistent information, returns SQLSTATE
* HY021.
* Refer to SQLSetDescRec .
************************************************************************/
PRIVATE int
odbc_consistency_check (ODBC_RECORD * record)
{
  if (!odbc_is_valid_verbose_type (record->type))
    {
      return FALSE;
    }
  if (!odbc_is_valid_concise_type (record->concise_type))
    {
      return FALSE;
    }
  if (odbc_is_valid_date_verbose_type (record->type) ||
      odbc_is_valid_interval_verbose_type (record->type))
    {
      if (!odbc_is_valid_code (record->datetime_interval_code))
	{
	  return FALSE;
	}
    }
  if (record->type == SQL_C_NUMERIC || record->type == SQL_NUMERIC)
    {
      if ((record->precision < 1 || record->precision > 38) ||
	  record->scale > record->precision)
	{
	  return FALSE;
	}
    }

  /* CHECK : check for date, interval type */

  return TRUE;
}


/************************************************************************
 * name: is_read_only_field
 * arguments:
 * returns/side-effects:
 * description:
 *	if (field_id == READ ONLY desc field) then
 *		return B_TURE
 *  else
 *		return _FALSE_
 *	endif
 * NOTE:
 ************************************************************************/
PRIVATE short
is_read_only_field (short field_id)
{
  switch (field_id)
    {
    case SQL_DESC_AUTO_UNIQUE_VALUE:
    case SQL_DESC_BASE_COLUMN_NAME:
    case SQL_DESC_BASE_TABLE_NAME:
    case SQL_DESC_CASE_SENSITIVE:
    case SQL_DESC_CATALOG_NAME:
    case SQL_DESC_DISPLAY_SIZE:
    case SQL_DESC_FIXED_PREC_SCALE:
    case SQL_DESC_LABEL:
    case SQL_DESC_LITERAL_PREFIX:
    case SQL_DESC_LITERAL_SUFFIX:
    case SQL_DESC_LOCAL_TYPE_NAME:
    case SQL_DESC_NULLABLE:
    case SQL_DESC_SCHEMA_NAME:
    case SQL_DESC_SEARCHABLE:
    case SQL_DESC_TABLE_NAME:
    case SQL_DESC_UNSIGNED:
    case SQL_DESC_UPDATABLE:
      return _TRUE_;
    default:
      return _FALSE_;
    }
}

/************************************************************************
* name: header_desc_field_copy
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PRIVATE void
header_desc_field_copy (ODBC_DESC * source_desc, ODBC_DESC * dest_desc)
{
  dest_desc->array_size = source_desc->array_size;
  dest_desc->array_status_ptr = source_desc->array_status_ptr;
  dest_desc->bind_offset_ptr = source_desc->bind_offset_ptr;
  dest_desc->bind_type = source_desc->bind_type;
  dest_desc->max_count = source_desc->max_count;
  dest_desc->rows_processed_ptr = source_desc->rows_processed_ptr;
}

/************************************************************************
* name: record_desc_field_copy
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PRIVATE void
record_desc_field_copy (ODBC_DESC * source_desc, ODBC_DESC * dest_desc)
{
  int i;
  ODBC_RECORD *src_record, *dest_record;

  for (i = 1; i <= source_desc->max_count; ++i)
    {
      src_record = NULL;
      dest_record = NULL;

      src_record = find_record_from_desc (source_desc, i);
      if (src_record == NULL)
	continue;

      odbc_alloc_record (dest_desc, &dest_record, i);

      dest_record->name = UT_MAKE_STRING (src_record->name, -1);
      dest_record->case_sensitive = src_record->case_sensitive;
      dest_record->display_size = src_record->display_size;
      dest_record->fixed_prec_scale = src_record->fixed_prec_scale;
      dest_record->literal_prefix =
	UT_MAKE_STRING (src_record->literal_prefix, -1);
      dest_record->literal_suffix =
	UT_MAKE_STRING (src_record->literal_suffix, -1);
      dest_record->local_type_name =
	UT_MAKE_STRING (src_record->local_type_name, -1);
      dest_record->nullable = src_record->nullable;
      dest_record->searchable = src_record->searchable;
      dest_record->table_name = UT_MAKE_STRING (src_record->table_name, -1);
      dest_record->type_name = UT_MAKE_STRING (src_record->type_name, -1);
      dest_record->unsigned_type = src_record->unsigned_type;

      dest_record->concise_type = src_record->concise_type;
      dest_record->data_ptr = src_record->data_ptr;
      dest_record->datetime_interval_code =
	src_record->datetime_interval_code;
      dest_record->datetime_interval_precision =
	src_record->datetime_interval_precision;
      dest_record->indicator_ptr = src_record->indicator_ptr;
      dest_record->length = src_record->length;
      dest_record->num_prec_radix = src_record->num_prec_radix;
      dest_record->octet_length = src_record->octet_length;
      dest_record->octet_length_ptr = src_record->octet_length_ptr;
      dest_record->parameter_type = src_record->parameter_type;
      dest_record->precision = src_record->precision;
      dest_record->scale = src_record->scale;
      dest_record->unnamed = src_record->unnamed;

    }
}

PRIVATE short
odbc_type_searchable (short type)
{
  short searchable;

  switch (type)
    {
    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARCHAR:
      searchable = SQL_SEARCHABLE;
      break;
    case SQL_NUMERIC:
    case SQL_DECIMAL:
    case SQL_INTEGER:
    case SQL_SMALLINT:
    case SQL_REAL:
    case SQL_FLOAT:
    case SQL_DOUBLE:
    case SQL_TYPE_DATE:
    case SQL_TYPE_TIME:
    case SQL_TYPE_TIMESTAMP:
      searchable = SQL_PRED_BASIC;
      break;
    default:
      searchable = 0;
    }

  return searchable;
}
