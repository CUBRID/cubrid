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
#include		"sqlext.h"
#include		"odbc_util.h"
#include		"odbc_diag_record.h"
#include		"odbc_statement.h"
#include		"odbc_result.h"
#include		"odbc_type.h"
#include		"cas_cci.h"
#include		"odbc_catalog.h"

#define         TABLE_TYPE_TABLE        ( 0x1 )
#define         TABLE_TYPE_VIEW         ( 0x2 )
#define			TABLE_TYPE_SYSTEM		( 0x4 )
#define         TABLE_TYPE_ALL          ( TABLE_TYPE_TABLE | TABLE_TYPE_VIEW | \
										  TABLE_TYPE_SYSTEM )


#define         NC_CATALOG_TABLES               5	/* NC means num columns */
#define         NC_CATALOG_COLUMNS              18
#define         NC_CATALOG_SPECIALCOLUMNS       8
#define         NC_CATALOG_STATISTICS           13
#define         NC_CATALOG_TYPE_INFO            19

typedef struct tagODBC_SPC_INFO
{
  char *constraint_name;
  char *column_name;
  short is_non_null;
  int domain;
  int precision;
  int scale;
} ODBC_SPC_INFO;

PRIVATE RETCODE make_table_result_set (ODBC_STATEMENT * stmt,
				       int req_handle, int handle_type);
PRIVATE RETCODE column_result_set_by_id (ODBC_STATEMENT * stmt,
					 char *table_name, char *column_name);
PRIVATE RETCODE column_result_set_by_pv (ODBC_STATEMENT * stmt,
					 char *table_name_pattern,
					 int table_type,
					 char *column_name_pattern);
PRIVATE RETCODE make_column_result_set (ODBC_STATEMENT * stmt,
					int req_handle);
PRIVATE RETCODE get_sp_column_info (ODBC_STATEMENT * stmt,
				    char *table_name,
				    ODBC_SPC_INFO * spc_info_ptr);
PRIVATE RETCODE make_sp_column_result_set (ODBC_STATEMENT * stmt,
					   char *table_name,
					   ODBC_SPC_INFO * spc_info_ptr);
PRIVATE RETCODE odbc_get_table_data (ODBC_STATEMENT * stmt,
				     short col_index,
				     VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_column_data (ODBC_STATEMENT * stmt,
				      short col_index,
				      VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_stat_data (ODBC_STATEMENT * stmt,
				    short col_index,
				    VALUE_CONTAINER * c_value);

PRIVATE RETCODE odbc_get_sp_column_data (ODBC_STATEMENT * stmt,
					 short col_index,
					 VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_type_info_data (ODBC_STATEMENT * stmt,
					 short col_index,
					 VALUE_CONTAINER * c_value);
PRIVATE void free_table_node (ST_LIST * node);
PRIVATE void free_column_node (ST_LIST * node);
PRIVATE void free_stat_node (ST_LIST * node);
PRIVATE void free_sp_column_node (ST_LIST * node);
PRIVATE void free_type_info_node (ST_LIST * node);

PRIVATE ODBC_COL_INFO table_cinfo[] = {
  {"TABLE_CAT", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_SCHEM", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_TYPE", SQL_VARCHAR, 128, 128, 0},
  {"REMARKS", SQL_VARCHAR, 128, 128, 0},
  {NULL}
};

PRIVATE ODBC_COL_INFO column_cinfo[] = {
  {"TABLE_CAT", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_SCHEM", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"COLUMN_NAME", SQL_VARCHAR, 128, 128, 0},
  {"DATA_TYPE", SQL_SMALLINT, 5, 5, 0},

  {"TYPE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"COLUMN_SIZE", SQL_INTEGER, 10, 10, 0},
  {"BUFFER_LENGTH", SQL_INTEGER, 10, 10, 0},
  {"DECIMAL_DIGITS", SQL_SMALLINT, 5, 5, 0},
  {"NUM_PREC_RADIX", SQL_SMALLINT, 5, 5, 0},

  {"NULLABLE", SQL_SMALLINT, 5, 5, 0},
  {"REMARKS", SQL_VARCHAR, 128, 128, 0},
  {"COLUMN_DEF", SQL_VARCHAR, 128, 128, 0},
  {"SQL_DATA_TYPE", SQL_SMALLINT, 5, 5, 0},
  {"SQL_DATETIME_SUB", SQL_SMALLINT, 5, 5, 0},

  {"CHAR_OCTET_LENGTH", SQL_INTEGER, 10, 10, 0},
  {"ORDINAL_POSITION", SQL_INTEGER, 10, 10, 0},
  {"IS_NULLABLE", SQL_VARCHAR, 128, 128, 0},
  {NULL}
};

PRIVATE ODBC_COL_INFO special_column_cinfo[] = {
  {"SCOPE", SQL_SMALLINT, 5, 5, 0},
  {"COLUMN_NAME", SQL_VARCHAR, 128, 128, 0},
  {"DATA_TYPE", SQL_SMALLINT, 5, 5, 0},
  {"TYPE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"COLUMN_SIZE", SQL_INTEGER, 10, 10, 0},
  {"BUFFER_LENGTH", SQL_INTEGER, 10, 10, 0},
  {"DECIMAL_DIGITS", SQL_SMALLINT, 5, 5, 0},
  {"PSEUDO_COLUMN", SQL_SMALLINT, 5, 5, 0},
  {NULL}
};

PRIVATE ODBC_COL_INFO statistics_cinfo[] = {
  {"TABLE_CAT", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_SCHEM", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"NON_UNIQUE", SQL_SMALLINT, 5, 5, 0},
  {"INDEX_QUALIFIER", SQL_VARCHAR, 128, 128, 0},
  {"INDEX_NAME", SQL_VARCHAR, 128, 128, 0},
  {"TYPE", SQL_SMALLINT, 5, 5, 0},
  {"ORDINAL_POSITION", SQL_SMALLINT, 5, 5, 0},
  {"COLUMN_NAME", SQL_VARCHAR, 128, 128, 0},
  {"ASC_OR_DESC", SQL_CHAR, 1, 1, 0},
  {"CARDINALITY", SQL_INTEGER, 10, 10, 0},
  {"PAGES", SQL_INTEGER, 10, 10, 0},
  {"FILTER_CONDITION", SQL_VARCHAR, 128, 128, 0},
  {NULL}
};

PRIVATE ODBC_COL_INFO type_cinfo[] = {
  {"TYPE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"DATA_TYPE", SQL_SMALLINT, 5, 5, 0},
  {"COLUMN_SIZE", SQL_INTEGER, 10, 10, 0},
  {"LITERAL_PREFIX", SQL_VARCHAR, 128, 128, 0},
  {"LITERAL_SUFFIX", SQL_VARCHAR, 128, 128, 0},
  {"CREATE_PARAMS", SQL_VARCHAR, 128, 128, 0},
  {"NULLABLE", SQL_SMALLINT, 5, 5, 0},
  {"CASE_SENSITIVE", SQL_SMALLINT, 5, 5, 0},
  {"SEARCHABLE", SQL_SMALLINT, 5, 5, 0},
  {"UNSIGNED_ATTRIBUTE", SQL_SMALLINT, 5, 5, 0},
  {"FIXED_PREC_SCALE", SQL_SMALLINT, 5, 5, 0},
  {"AUTO_UNIQUE_VALUE", SQL_SMALLINT, 5, 5, 0},
  {"LOCAL_TYPE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"MINIMUM_SCALE", SQL_SMALLINT, 5, 5, 0},
  {"MAXIMUM_SCALE", SQL_SMALLINT, 5, 5, 0},
  {"SQL_DATA_TYPE", SQL_SMALLINT, 5, 5, 0},
  {"SQL_DATETIME_SUB", SQL_SMALLINT, 5, 5, 0},
  {"NUM_PREC_RADIX", SQL_INTEGER, 10, 10, 0},
  {"INTERVAL_PRECISION", SQL_SMALLINT, 5, 5, 0},
  {NULL}
};

static ODBC_TYPE_INFO_VALUE type_info[] = {
  {"CHAR", SQL_CHAR, MAX_CUBRID_CHAR_LEN, "'", "'", "length", SQL_NULLABLE,
   SQL_FALSE, SQL_SEARCHABLE, -1, SQL_FALSE, SQL_FALSE, "CHAR", -1, -1,
   SQL_CHAR, -1, -1, -1},
  {"VARCHAR", SQL_VARCHAR, MAX_CUBRID_CHAR_LEN, "'", "'", "length",
   SQL_NULLABLE,
   SQL_FALSE, SQL_SEARCHABLE, -1, SQL_FALSE, SQL_FALSE, "VARCHAR", -1, -1,
   SQL_VARCHAR, -1, -1, -1},
  {"BIT", SQL_BINARY, MAX_CUBRID_CHAR_LEN / 8, "X'", "'", "length",
   SQL_NULLABLE,
   SQL_FALSE, SQL_SEARCHABLE, -1, SQL_FALSE, SQL_FALSE, "BIT", -1, -1,
   SQL_BINARY, -1, -1, -1},
  {"BIT VARYING", SQL_VARBINARY, MAX_CUBRID_CHAR_LEN / 8, "X'", "'", "length",
   SQL_NULLABLE,
   SQL_FALSE, SQL_SEARCHABLE, -1, SQL_FALSE, SQL_FALSE, "BIT VARYING", -1, -1,
   SQL_VARBINARY, -1, -1, -1},
  {"NUMERIC", SQL_NUMERIC, 38, NULL, NULL, "precision,scale", SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "NUMERIC", 0,
   38,
   SQL_NUMERIC, -1, 10, -1},
  {"DECIMAL", SQL_DECIMAL, 38, NULL, NULL, "precision,scale", SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "DECIMAL", 0,
   38,
   SQL_DECIMAL, -1, 10, -1},
  {"INTEGER", SQL_INTEGER, 10, NULL, NULL, NULL, SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "INTEGER", -1,
   -1,
   SQL_INTEGER, -1, 10, -1},
  {"SMALLINT", SQL_SMALLINT, 5, NULL, NULL, NULL, SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "SMALLINT", -1,
   -1,
   SQL_SMALLINT, -1, 10, -1},
  {"REAL", SQL_REAL, 14, NULL, NULL, "precision", SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "REAL", -1, -1,
   SQL_REAL, -1, 10, -1},
  {"FLOAT", SQL_FLOAT, 14, NULL, NULL, "precision", SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "FLOAT", -1,
   -1,
   SQL_FLOAT, -1, 10, -1},
  {"DOUBLE", SQL_DOUBLE, 28, NULL, NULL, "precision", SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "DOUBLE", -1,
   -1,
   SQL_DOUBLE, -1, 10, -1},
  {"DATE", SQL_TYPE_DATE, 10, "DATE '", "'", NULL, SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "DATE", -1, -1,
   SQL_DATETIME, SQL_CODE_DATE, -1, -1},
  {"TIME", SQL_TYPE_TIME, 8, "TIME '", "'", NULL, SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "TIME", -1, -1,
   SQL_DATETIME, SQL_CODE_TIME, -1, -1},
  {"TIMESTAMP", SQL_TYPE_TIMESTAMP, 19, "TIMESTAMP '", "'", NULL,
   SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "TIMESTAMP",
   -1, -1,
   SQL_DATETIME, SQL_CODE_TIMESTAMP, -1, -1},
  {"BIGINT", SQL_BIGINT, 19, NULL, NULL, NULL, SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "BIGINT", -1,
   -1,
   SQL_INTEGER, -1, 10, -1},
  {"DATETIME", SQL_TYPE_TIMESTAMP, 23, "DATETIME '", "'", NULL,
   SQL_NULLABLE,
   SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "DATETIME",
   -1, -1,
   SQL_DATETIME, SQL_CODE_TIMESTAMP, -1, -1},
  /* XXX : oid와 set type은 char로 mapping된다.
     {"OBJECT", SQL_UNI_OBJECT, MAX_CUBRID_CHAR_LEN, "'", "'", "", SQL_NULLABLE,
     SQL_FALSE, SQL_SEARCHABLE, -1, SQL_FALSE, -1, "OBJECT", -1, -1,
     SQL_UNI_OBJECT, -1, -1, -1},
     {"SET", SQL_UNI_SET, MAX_CUBRID_CHAR_LEN, "'", "'", "", SQL_NULLABLE,
     SQL_FALSE, SQL_SEARCHABLE, -1, SQL_FALSE, -1, "SET", -1, -1,
     SQL_UNI_SET, -1, -1, -1},
     {"MONETARY", SQL_UNI_MONETARY, 28, NULL, NULL, "precision", SQL_NULLABLE,
     SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_FALSE, SQL_FALSE, "DOUBLE", -1, -1,
     SQL_UNI_MONETARY, -1, 10, -1},
   */
#ifdef DELPHI
  {"STRING", SQL_LONGVARCHAR, MAX_CUBRID_CHAR_LEN, "'", "'", "length",
   SQL_NULLABLE,
   SQL_FALSE, SQL_SEARCHABLE, -1, SQL_FALSE, SQL_FALSE, "STRING", -1, -1,
   SQL_LONGVARCHAR, -1, -1, -1},
#endif

  {NULL}
};

/************************************************************************
* name: odbc_tables
* arguments: 
*	table_type은 comma separated list value이다.
*	예) "'VIEW', 'TABLE'"  -> single comma로 구분된다.
*		실제로 구분 안해도 된다.  "VIEWTABLE"해도 된다.
* returns/side-effects: 
* description: 
* NOTE: 
*	patern search 지원하지 않음.
************************************************************************/
PUBLIC RETCODE
odbc_tables (ODBC_STATEMENT * stmt,
	     char *catalog_name,
	     char *schema_name, char *table_name, char *table_type)
{
  ODBC_TABLE_VALUE *table_node = NULL;
  int t_type = 0;
  char search_pattern_flag;
  int handle = -1;

  T_CCI_ERROR cci_err_buf;
  int cci_rc;

  short i;
  RETCODE rc;


  reset_result_set (stmt);	/* redundancy - cursor가 close상태에서만 prepare가 가능하다. */
  reset_descriptor (stmt->ird);
  stmt->param_number = 0;

  stmt->result_type = TABLES;

  ListCreate (&stmt->catalog_result.value);

  /* stmt->catalog_result.currnet = NULL
   * 의미없음.. current_tpl_pos에 의해서 결정됨
   * move_catalog_rs() 참조
   */
  stmt->catalog_result.current = NULL;

  // set IRD
  for (i = 1; i <= NC_CATALOG_TABLES; ++i)
    {
      // set ird field
      odbc_set_ird (stmt, i, table_cinfo[i - 1].type, "",
		    (char *) table_cinfo[i - 1].name,
		    table_cinfo[i - 1].precision, (short) 0,
		    SQL_NULLABLE_UNKNOWN, SQL_ATTR_READONLY);
    }

  if (table_type == NULL)
    {
      t_type = TABLE_TYPE_ALL;
    }
  else
    {
      if (strstr (table_type, "VIEW") != NULL)
	SET_OPTION (t_type, TABLE_TYPE_VIEW);

      if (strstr (table_type, "TABLE") != NULL)
	SET_OPTION (t_type, TABLE_TYPE_TABLE);

      if (strstr (table_type, "SYSTEM") != NULL)
	SET_OPTION (t_type, TABLE_TYPE_SYSTEM);
    }

  if (stmt->attr_metadata_id == SQL_TRUE)
    {
      search_pattern_flag = 0;
    }
  else
    {
      search_pattern_flag = CCI_CLASS_NAME_PATTERN_MATCH;

    }

  // return request handle or error code
  cci_rc = cci_schema_info (stmt->conn->connhd, CCI_SCH_CLASS,
			    table_name, NULL, search_pattern_flag,
			    &cci_err_buf);


  ERROR_GOTO (cci_rc, cci_error);

  handle = cci_rc;

  rc = make_table_result_set (stmt, handle, t_type);
  ERROR_GOTO (rc, error);

  cci_close_req_handle (handle);
  handle = -1;

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
error:
  // check : view나 class 둘 중 하나라도 error가 나면 모두 free
  // odbc_close_cursor(stmt);
  if (handle > 0)
    {
      cci_close_req_handle (handle);
    }
  return ODBC_ERROR;
}


/************************************************************************
 * name:  odbc_columns
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 *		ordinal_pos  - probably correct
 ************************************************************************/
PUBLIC RETCODE
odbc_columns (ODBC_STATEMENT * stmt,
	      char *catalog_name,
	      char *schema_name, char *table_name, char *column_name)
{
  RETCODE rc;
  short i;
  char search_pattern_flag;
  int handle = -1;

  int cci_rc;
  T_CCI_ERROR cci_err_buf;


  reset_result_set (stmt);
  reset_descriptor (stmt->ird);
  stmt->param_number = 0;

  stmt->result_type = COLUMNS;

  ListCreate (&stmt->catalog_result.value);
  stmt->catalog_result.current = NULL;

  for (i = 1; i <= NC_CATALOG_COLUMNS; ++i)
    {
      // set ird field
      odbc_set_ird (stmt, i, column_cinfo[i - 1].type, "",
		    (char *) column_cinfo[i - 1].name,
		    column_cinfo[i - 1].precision, (short) 0,
		    SQL_NULLABLE_UNKNOWN, SQL_ATTR_READONLY);
    }

  if (stmt->attr_metadata_id == SQL_TRUE)
    {
      search_pattern_flag = 0;
    }
  else
    {
      search_pattern_flag =
	CCI_CLASS_NAME_PATTERN_MATCH | CCI_ATTR_NAME_PATTERN_MATCH;
    }

  cci_rc = cci_schema_info (stmt->conn->connhd, CCI_SCH_ATTRIBUTE,
			    table_name, column_name, search_pattern_flag,
			    &cci_err_buf);
  ERROR_GOTO (cci_rc, cci_error);

  handle = cci_rc;

  rc = make_column_result_set (stmt, handle);
  ERROR_GOTO (rc, error);

  cci_close_req_handle (handle);


  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
error:
  if (handle > 0)
    cci_close_req_handle (handle);
  // check : close req_handle
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_statistics
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
*		ordinal_pos  - probably correct
************************************************************************/
PUBLIC RETCODE
odbc_statistics (ODBC_STATEMENT * stmt,
		 char *catalog_name,
		 char *schema_name,
		 char *table_name,
		 unsigned short unique, unsigned short reversed)
{
  RETCODE rc;
  short i;
  ODBC_STAT_VALUE *stat_node = NULL;
  char *cci_table_name = NULL, *cci_column_name = NULL;
  char *table_name_pattern = NULL, *column_name_pattern = NULL;
  int cci_rc;
  int cci_handle = -1;
  T_CCI_ERROR cci_err_buf;
  UNI_CCI_A_TYPE cci_value;
  int cci_ind;
  int current_tpl_pos;
  char *prev_index_name = NULL;
  int ordinal_pos = 0;

  reset_result_set (stmt);
  reset_descriptor (stmt->ird);
  stmt->param_number = 0;

  stmt->result_type = STATISTICS;

  ListCreate (&stmt->catalog_result.value);
  stmt->catalog_result.current = NULL;

  for (i = 1; i <= NC_CATALOG_STATISTICS; ++i)
    {
      // set ird field
      odbc_set_ird (stmt, i, statistics_cinfo[i - 1].type, "",
		    (char *) statistics_cinfo[i - 1].name,
		    statistics_cinfo[i - 1].precision, (short) 0,
		    SQL_NULLABLE_UNKNOWN, SQL_ATTR_READONLY);
    }

  cci_rc = cci_schema_info (stmt->conn->connhd, CCI_SCH_CONSTRAINT,
			    table_name, NULL, 0, &cci_err_buf);
  ERROR_GOTO (cci_rc, cci_error);

  cci_handle = cci_rc;

  // for SQL_TABLE_STAT
  if (table_name)
    {
      stat_node = create_stat_value ();
      if (stat_node == NULL)
	{
	  odbc_set_diag (stmt->diag, "UN008", 0, NULL);
	  goto error;
	}

      stat_node->table_name = UT_MAKE_STRING (table_name, -1);
      stat_node->type = SQL_TABLE_STAT;

      ListTailAdd (stmt->catalog_result.value, NULL, stat_node, NodeAssign);
      ++stmt->tpl_number;
    }

  current_tpl_pos = 0;
  while (1)
    {
      // move cursor
      rc = move_cursor (cci_handle, &current_tpl_pos, stmt->diag);
      if (rc < 0)
	{
	  if (rc == ODBC_NO_MORE_DATA)
	    break;
	  else
	    {
	      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_MOVE_CURSOR, NULL);
	      goto error;
	    }
	}

      // fetch data
      rc = fetch_tuple (cci_handle, stmt->diag, 0);
      if (rc < 0)
	{
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_FETCH_CURSOR, NULL);
	  goto error;
	}

      // create a tuple
      stat_node = create_stat_value ();
      if (stat_node == NULL)
	continue;

      // get constraint type , 0 unique, 1 index
      cci_rc = cci_get_data (cci_handle, 1, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}

      if (unique == SQL_INDEX_UNIQUE && cci_value.i == 1)
	continue;

      if (cci_value.i == 1)
	stat_node->non_unique = SQL_TRUE;
      else
	stat_node->non_unique = SQL_FALSE;



      stat_node->table_name = UT_MAKE_STRING (table_name, -1);

      stat_node->type = SQL_INDEX_OTHER;

      // get constraint name
      cci_rc = cci_get_data (cci_handle, 2, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}

      stat_node->index_name = UT_MAKE_STRING (cci_value.str, -1);

      if (prev_index_name == NULL ||
	  strcmp (prev_index_name, stat_node->index_name) != 0)
	{
	  prev_index_name = stat_node->index_name;
	  ordinal_pos = 1;
	}
      else
	{
	  ++ordinal_pos;
	}

      stat_node->ordinal_position = ordinal_pos;

      // get column name
      cci_rc = cci_get_data (cci_handle, 3, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}

      stat_node->column_name = UT_MAKE_STRING (cci_value.str, -1);

      ListTailAdd (stmt->catalog_result.value, NULL, stat_node, NodeAssign);
      ++stmt->tpl_number;
    }

  if (cci_handle > 0)
    {
      cci_close_req_handle (cci_handle);
    }
  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_rc, cci_err_buf.err_msg);
error:
  // check : close cursor
  if (cci_handle > 0)
    {
      cci_close_req_handle (cci_handle);
    }
  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_special_columns
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
* special column 선택 원칙
*		1. single unique constraint를 가지고 있으면서, 
*			non-null인 첫 column
*		2. single unique constraint를 가지고 있지만,
*			nullable인 가장 마지막 column
************************************************************************/
PUBLIC RETCODE
odbc_special_columns (ODBC_STATEMENT * stmt,
		      short identifier_type,
		      char *catalog_name,
		      char *schema_name,
		      char *table_name, short scope, short nullable)
{
  RETCODE rc;
  short i;
  char *prev_constraint_name = NULL;
  ODBC_SPC_INFO spc_info;

  char *cci_constraint_name, *cci_column_name;
  int cci_rc;
  int cci_handle = -1;
  T_CCI_ERROR cci_err_buf;
  UNI_CCI_A_TYPE cci_value;
  int cci_ind;
  int current_tpl_pos;

  reset_result_set (stmt);
  reset_descriptor (stmt->ird);
  stmt->param_number = 0;

  stmt->result_type = SPECIALCOLUMNS;

  // init spc_info
  spc_info.constraint_name = NULL;
  spc_info.column_name = NULL;

  ListCreate (&stmt->catalog_result.value);
  stmt->catalog_result.current = NULL;

  for (i = 1; i <= NC_CATALOG_SPECIALCOLUMNS; ++i)
    {
      // set ird field
      odbc_set_ird (stmt, i, special_column_cinfo[i - 1].type, "",
		    (char *) special_column_cinfo[i - 1].name,
		    special_column_cinfo[i - 1].precision, (short) 0,
		    SQL_NULLABLE_UNKNOWN, SQL_ATTR_READONLY);

    }

  if (identifier_type == SQL_ROWVER)
    {
      // CUBRID은 SQL_ROWVER에 해당하는 기능이 없으므로, 
      // empty result set을 return한다.
      return ODBC_SUCCESS;
    }

  cci_rc = cci_schema_info (stmt->conn->connhd, CCI_SCH_CONSTRAINT,
			    table_name, NULL, 0, &cci_err_buf);
  ERROR_GOTO (cci_rc, cci_error);

  cci_handle = cci_rc;

  current_tpl_pos = 0;
  while (1)
    {
      // move cursor
      rc = move_cursor (cci_handle, &current_tpl_pos, stmt->diag);
      if (rc < 0)
	{
	  if (rc == ODBC_NO_MORE_DATA)
	    break;
	  else
	    {
	      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_MOVE_CURSOR, NULL);
	      goto error;
	    }
	}

      // fetch data
      rc = fetch_tuple (cci_handle, stmt->diag, 0);
      if (rc < 0)
	{
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_FETCH_CURSOR, NULL);
	  goto error;
	}

      // get constraint type , 0 unique, 1 index
      cci_rc = cci_get_data (cci_handle, 1, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}

      // 0 means unqiue constraint
      if (cci_value.i != 0)
	{
	  continue;
	}

      // get constraint name
      cci_rc = cci_get_data (cci_handle, 2, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      cci_constraint_name = cci_value.str;

      // get column name
      cci_rc = cci_get_data (cci_handle, 3, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      cci_column_name = cci_value.str;


      if (spc_info.constraint_name == NULL)
	{
	  // reset candidate
	  spc_info.constraint_name = cci_constraint_name;
	  spc_info.column_name = cci_column_name;
	}
      else if (strcmp (spc_info.constraint_name, cci_value.str) != 0)
	{
	  rc = get_sp_column_info (stmt, table_name, &spc_info);
	  if (rc < 0)
	    goto error;

	  if (spc_info.is_non_null == _TRUE_)
	    {
	      make_sp_column_result_set (stmt, table_name, &spc_info);
	      spc_info.constraint_name = NULL;
	      spc_info.column_name = NULL;
	      break;
	    }

	}
      else
	{
	  spc_info.column_name = NULL;
	}
    }

  if (spc_info.constraint_name != NULL && spc_info.column_name != NULL)
    {
      rc = get_sp_column_info (stmt, table_name, &spc_info);
      if (rc < 0)
	goto error;
      make_sp_column_result_set (stmt, table_name, &spc_info);
    }

  if (cci_handle > 0)
    cci_close_req_handle (cci_handle);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
error:
  if (cci_handle > 0)
    cci_close_req_handle (cci_handle);

  return ODBC_ERROR;
}

/************************************************************************
* name: odbc_get_type_info
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC RETCODE
odbc_get_type_info (ODBC_STATEMENT * stmt, short data_type)
{
  ODBC_TYPE_INFO_VALUE *type_info_node;
  short i;

  reset_result_set (stmt);
  reset_descriptor (stmt->ird);
  stmt->param_number = 0;

  stmt->result_type = TYPE_INFO;

  ListCreate (&stmt->catalog_result.value);
  stmt->catalog_result.current = NULL;

  for (i = 1; i <= NC_CATALOG_TYPE_INFO; ++i)
    {
      // set ird field
      odbc_set_ird (stmt, i, type_cinfo[i - 1].type, "",
		    (char *) type_cinfo[i - 1].name,
		    type_cinfo[i - 1].precision, (short) 0,
		    SQL_NULLABLE_UNKNOWN, SQL_ATTR_READONLY);
    }

  if (data_type == SQL_ALL_TYPES)
    {
      for (i = 0; type_info[i].type_name != NULL; ++i)
	{
	  type_info_node = create_type_info_value ();
	  if (type_info_node == NULL)
	    continue;

	  memcpy (type_info_node, type_info + i,
		  sizeof (ODBC_TYPE_INFO_VALUE));
	  type_info_node->type_name =
	    UT_MAKE_STRING (type_info[i].type_name, -1);
	  type_info_node->create_params =
	    UT_MAKE_STRING (type_info[i].create_params, -1);
	  type_info_node->literal_prefix =
	    UT_MAKE_STRING (type_info[i].literal_prefix, -1);
	  type_info_node->literal_suffix =
	    UT_MAKE_STRING (type_info[i].literal_suffix, -1);
	  type_info_node->local_type_name =
	    UT_MAKE_STRING (type_info[i].local_type_name, -1);

	  // for 2.x backward compatibility
	  if (odbc_is_valid_sql_date_type (type_info_node->data_type))
	    {
	      if (stmt->conn->env->attr_odbc_version == SQL_OV_ODBC2)
		{
		  type_info_node->data_type =
		    odbc_date_type_backward (type_info_node->data_type);
		}
	    }

	  ListTailAdd (stmt->catalog_result.value, NULL, type_info_node,
		       NodeAssign);
	  ++stmt->tpl_number;
	}
    }
  else
    {
      // 2.x type에 대해서는 type_info를 구성하지 않았다.
      // 이유는 DM에서 mapping을 해주기 때문이다.
      for (i = 0; type_info[i].type_name != NULL; ++i)
	{
	  if (type_info[i].data_type == data_type)
	    {
	      type_info_node = create_type_info_value ();
	      if (type_info_node == NULL)
		continue;

	      memcpy (type_info_node, type_info + i,
		      sizeof (ODBC_TYPE_INFO_VALUE));
	      type_info_node->type_name =
		UT_MAKE_STRING (type_info[i].type_name, -1);
	      type_info_node->create_params =
		UT_MAKE_STRING (type_info[i].create_params, -1);
	      type_info_node->literal_prefix =
		UT_MAKE_STRING (type_info[i].literal_prefix, -1);
	      type_info_node->literal_suffix =
		UT_MAKE_STRING (type_info[i].literal_suffix, -1);
	      type_info_node->local_type_name =
		UT_MAKE_STRING (type_info[i].local_type_name, -1);

	      // for 2.x backward compatibility
	      if (odbc_is_valid_sql_date_type (type_info_node->data_type))
		{
		  if (stmt->conn->env->attr_odbc_version == SQL_OV_ODBC2)
		    {
		      type_info_node->data_type =
			odbc_date_type_backward (type_info_node->data_type);
		    }
		}

	      ListTailAdd (stmt->catalog_result.value, NULL, type_info_node,
			   NodeAssign);
	      ++stmt->tpl_number;
	    }

	}
    }

  return ODBC_SUCCESS;
}

/* c_value에는 pointer만 획득한다 */
PUBLIC RETCODE
odbc_get_catalog_data (ODBC_STATEMENT * stmt,
		       short col_index, VALUE_CONTAINER * c_value)
{
  RETCODE rc = ODBC_SUCCESS;

  if (stmt->result_type == TABLES)
    {
      rc = odbc_get_table_data (stmt, col_index, c_value);
    }
  else if (stmt->result_type == COLUMNS)
    {
      rc = odbc_get_column_data (stmt, col_index, c_value);
    }
  else if (stmt->result_type == STATISTICS)
    {
      rc = odbc_get_stat_data (stmt, col_index, c_value);
    }
  else if (stmt->result_type == SPECIALCOLUMNS)
    {
      rc = odbc_get_sp_column_data (stmt, col_index, c_value);
    }
  else if (stmt->result_type == TYPE_INFO)
    {
      rc = odbc_get_type_info_data (stmt, col_index, c_value);
    }

  // error check

  return rc;
}



/************************************************************************
 * IMPLEMENTATION OF PRIVATE FUNCTIONS                                                       *
 ************************************************************************/

/*	c_value에 대해서 memory alloc이 일어나지 않으므로 free해선 안된다. */
PRIVATE RETCODE
odbc_get_table_data (ODBC_STATEMENT * stmt,
		     short col_index, VALUE_CONTAINER * c_value)
{
  ODBC_TABLE_VALUE *table_tuple;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;

  table_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:			// catalog name , SQL_C_CHAR
    case 2:			// schema name, SQL_C_CHAR
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 3:			// table name, SQL_C_CHAR
      c_value->value.str = table_tuple->table_name;
      c_value->length = strlen (table_tuple->table_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 4:			// table type, SQL_C_CHAR
      c_value->value.str = table_tuple->table_type;
      c_value->length = strlen (table_tuple->table_type) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 5:			// remark, SQL_C_CHAR
      c_value->value.str = "";
      c_value->length = strlen ("") + 1;
      c_value->type = SQL_C_CHAR;
      break;
    default:
      odbc_set_diag (stmt->diag, "07009", 0, NULL);
      return ODBC_ERROR;
    }

  return ODBC_SUCCESS;
}

PRIVATE RETCODE
odbc_get_column_data (ODBC_STATEMENT * stmt,
		      short col_index, VALUE_CONTAINER * c_value)
{
  ODBC_COLUMN_VALUE *column_tuple;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;


  column_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:			// catalog name , SQL_C_CHAR
    case 2:			// schema name, SQL_C_CHAR
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 3:			// table name, SQL_C_CHAR
      c_value->value.str = column_tuple->table_name;
      c_value->length = strlen (column_tuple->table_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 4:			// column_name, SQL_C_CHAR
      c_value->value.str = column_tuple->column_name;
      c_value->length = strlen (column_tuple->column_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 5:			// data type, SQL_C_SHORT
      c_value->value.s = column_tuple->concise_data_type;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 6:			// type name, SQL_C_CHAR
      c_value->value.str = column_tuple->type_name;
      c_value->length = strlen (column_tuple->type_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 7:			// column size, SQL_C_LONG
      c_value->value.l = column_tuple->column_size;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 8:			// buffer length, SQL_C_LONG
      c_value->value.l = column_tuple->buffer_length;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 9:			// decimal disits, SQL_C_SHORT
      c_value->value.s = column_tuple->decimal_digits;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 10:			// num prec radix, SQL_C_SHORT
      c_value->value.s = column_tuple->num_prec_radix;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 11:			// nullable, SQL_C_SHORT
      c_value->value.s = column_tuple->nullable;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 12:			// remarks, SQL_C_CHAR
      c_value->value.str = "";
      c_value->length = strlen ("") + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 13:			// column default value, SQL_C_CHAR
      if (column_tuple->default_value == NULL)
	{
	  c_value->value.str = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_CHAR;
	}
      else
	{
	  c_value->value.str = column_tuple->default_value;
	  c_value->length = strlen (column_tuple->default_value) + 1;
	  c_value->type = SQL_C_CHAR;
	}
      break;
    case 14:			// sql data type. SQL_C_SHORT
      c_value->value.s = column_tuple->verbose_data_type;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 15:			// sql datetime subcode, SQL_C_SHORT
      c_value->value.s = column_tuple->subcode;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 16:			// char octet length, SQL_C_LONG
      c_value->value.l = column_tuple->octet_length;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 17:			// ordinal position, SQL_C_LONG
      c_value->value.l = column_tuple->ordinal_position;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 18:			// is nullable, SQL_C_CHAR
      c_value->value.str = "";
      c_value->length = strlen ("") + 1;
      c_value->type = SQL_C_CHAR;
      break;

    default:
      odbc_set_diag (stmt->diag, "07009", 0, NULL);
      return ODBC_ERROR;
    }

  return ODBC_SUCCESS;
}


PRIVATE RETCODE
odbc_get_stat_data (ODBC_STATEMENT * stmt,
		    short col_index, VALUE_CONTAINER * c_value)
{
  ODBC_STAT_VALUE *stat_tuple;
  short type;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;

  stat_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  type = stat_tuple->type;

  if (type == SQL_TABLE_STAT)
    {
      switch (col_index)
	{
	case 1:		// catalog name , SQL_C_CHAR
	case 2:		// schema name, SQL_C_CHAR
	  c_value->value.str = NULL;
	  c_value->type = SQL_C_CHAR;
	  c_value->length = 0;
	  break;
	case 3:		// table name, SQL_C_CHAR
	  c_value->value.str = stat_tuple->table_name;
	  c_value->length = strlen (stat_tuple->table_name) + 1;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 4:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_SHORT;
	  break;
	case 5:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 6:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 8:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_SHORT;
	  break;
	case 9:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 10:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 11:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_LONG;
	  break;
	case 12:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_LONG;
	  break;
	case 13:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 7:		// type
	  c_value->value.s = type;
	  c_value->length = sizeof (short);
	  c_value->type = SQL_C_SHORT;
	  break;

	default:
	  odbc_set_diag (stmt->diag, "07009", 0, NULL);
	  return ODBC_ERROR;
	}
    }
  else
    {
      switch (col_index)
	{
	case 1:		// catalog name , SQL_C_CHAR
	case 2:		// schema name, SQL_C_CHAR
	  c_value->value.str = NULL;
	  c_value->type = SQL_C_CHAR;
	  c_value->length = 0;
	  break;
	case 3:		// table name, SQL_C_CHAR
	  c_value->value.str = stat_tuple->table_name;
	  c_value->length = strlen (stat_tuple->table_name) + 1;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 4:		// non-unique , SQL_C_SHORT
	  c_value->value.s = stat_tuple->non_unique;
	  c_value->type = SQL_C_SHORT;
	  c_value->length = sizeof (short);
	  break;
	case 5:		// index qualifier, SQL_C_CHAR
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 6:		// index name, SQL_C_CHAR
	  c_value->value.str = stat_tuple->index_name;
	  c_value->length = strlen (stat_tuple->index_name) + 1;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 7:		// type, SQL_C_SHORT
	  c_value->value.s = type;
	  c_value->length = sizeof (short);
	  c_value->type = SQL_C_SHORT;
	  break;
	case 8:		// ordinal position, SQL_C_SHORT
	  c_value->value.s = stat_tuple->ordinal_position;
	  c_value->length = sizeof (short);
	  c_value->type = SQL_C_SHORT;
	  break;
	case 9:		// attr name, SQL_C_CHAR
	  c_value->value.str = stat_tuple->column_name;
	  c_value->length = strlen (stat_tuple->column_name) + 1;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 10:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_CHAR;
	  break;
	case 11:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_LONG;
	  break;
	case 12:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_LONG;
	  break;
	case 13:
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	  c_value->type = SQL_C_CHAR;
	  break;
	default:
	  odbc_set_diag (stmt->diag, "07009", 0, NULL);
	  return ODBC_ERROR;
	}
    }

  return ODBC_SUCCESS;
}


PRIVATE RETCODE
odbc_get_sp_column_data (ODBC_STATEMENT * stmt,
			 short col_index, VALUE_CONTAINER * c_value)
{
  ODBC_SP_COLUMN_VALUE *spc_tuple;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;


  spc_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:			// scope, SQL_C_SHORT
      c_value->value.s = SQL_SCOPE_CURROW;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 2:			// column name, SQL_C_CHAR
      c_value->value.str = spc_tuple->column_name;
      c_value->length = strlen (spc_tuple->column_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 3:			// data type, SQL_C_SHORT
      c_value->value.s = spc_tuple->concise_data_type;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 4:			// type name, SQL_C_CHAR
      c_value->value.str = spc_tuple->type_name;
      c_value->length = strlen (spc_tuple->type_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 5:			// column size, SQL_C_LONG
      c_value->value.l = spc_tuple->column_size;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 6:			// buffer length, SQL_C_LONG
      c_value->value.l = spc_tuple->buffer_length;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 7:			// decimal digits, SQL_C_SHORT
      c_value->value.s = spc_tuple->decimal_digits;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 8:			// pseudo column, SQL_C_SHORT
      c_value->value.s = SQL_PC_UNKNOWN;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    default:
      odbc_set_diag (stmt->diag, "07009", 0, NULL);
      return ODBC_ERROR;
    }

  return ODBC_SUCCESS;
}

PRIVATE RETCODE
odbc_get_type_info_data (ODBC_STATEMENT * stmt,
			 short col_index, VALUE_CONTAINER * c_value)
{
  ODBC_TYPE_INFO_VALUE *ti_tuple;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;

  ti_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:			// type name , SQL_C_CHAR
      c_value->value.str = ti_tuple->type_name;
      c_value->length = strlen (ti_tuple->type_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 2:			// data type, SQL_C_SHORT
      c_value->value.s = ti_tuple->data_type;
      c_value->type = SQL_C_SHORT;
      c_value->length = sizeof (short);
      break;
    case 3:			// column size, SQL_C_LONG
      if (c_value->value.s == SQL_CHAR || c_value->value.s == SQL_VARCHAR)
	{
	  if (ti_tuple->column_size >= stmt->conn->max_string_length)
	    c_value->value.l = stmt->conn->max_string_length;
	  else
	    c_value->value.l = stmt->conn->max_string_length;
	}
      else
	{
	  c_value->value.l = ti_tuple->column_size;
	}
      //c_value->value.l = ti_tuple->column_size;

      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 4:			// literal prefix
      if (ti_tuple->literal_prefix != NULL)
	{
	  c_value->value.str = ti_tuple->literal_prefix;
	  c_value->length = strlen (ti_tuple->literal_prefix) + 1;
	}
      else
	{
	  c_value->value.str = NULL;
	  c_value->length = 0;
	}
      c_value->type = SQL_C_CHAR;
      break;
    case 5:			// literal suffix
      if (ti_tuple->literal_suffix != NULL)
	{
	  c_value->value.str = ti_tuple->literal_suffix;
	  c_value->length = strlen (ti_tuple->literal_suffix) + 1;
	}
      else
	{
	  c_value->value.str = NULL;
	  c_value->length = 0;
	}
      c_value->type = SQL_C_CHAR;
      break;
    case 6:			// create params, SQL_C_CHAR
      if (ti_tuple->create_params != NULL)
	{
	  c_value->value.str = ti_tuple->create_params;
	  c_value->length = strlen (ti_tuple->create_params) + 1;
	}
      else
	{
	  c_value->value.str = NULL;
	  c_value->length = 0;
	}
      c_value->type = SQL_C_CHAR;
      break;
    case 7:			// nullable
      c_value->value.s = ti_tuple->nullable;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 8:			// case sensitive
      c_value->value.s = ti_tuple->case_sensitive;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 9:			// searchable
      c_value->value.s = ti_tuple->searchable;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 10:			// unsigned attribute
      if (ti_tuple->unsigned_attribute == -1)
	{
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	}
      else
	{
	  c_value->value.s = ti_tuple->unsigned_attribute;
	  c_value->length = sizeof (short);
	}
      c_value->type = SQL_C_SHORT;
      break;
    case 11:			// fixed prec scale
      c_value->value.s = ti_tuple->fixed_prec_scale;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 12:			// auto unique value
      if (ti_tuple->auto_unique_value == -1)
	{
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	}
      else
	{
	  c_value->value.s = ti_tuple->auto_unique_value;
	  c_value->length = sizeof (short);
	}
      c_value->type = SQL_C_SHORT;
      break;
    case 13:
      if (ti_tuple->local_type_name != NULL)
	{
	  c_value->value.str = ti_tuple->local_type_name;
	  c_value->length = strlen (ti_tuple->local_type_name) + 1;
	}
      else
	{
	  c_value->value.str = NULL;
	  c_value->length = 0;
	}
      c_value->type = SQL_C_CHAR;
      break;
    case 14:
      if (ti_tuple->minimum_scale == -1)
	{
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	}
      else
	{
	  c_value->value.s = ti_tuple->minimum_scale;
	  c_value->length = sizeof (short);
	}
      c_value->type = SQL_C_SHORT;
      break;
    case 15:
      if (ti_tuple->maximum_scale == -1)
	{
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	}
      else
	{
	  c_value->value.s = ti_tuple->maximum_scale;
	  c_value->length = sizeof (short);
	}
      c_value->type = SQL_C_SHORT;
      break;
    case 16:
      c_value->value.s = ti_tuple->sql_data_type;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 17:
      if (ti_tuple->sql_datetime_sub == -1)
	{
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	}
      else
	{
	  c_value->value.s = ti_tuple->sql_datetime_sub;
	  c_value->length = sizeof (short);
	}
      c_value->type = SQL_C_SHORT;
      break;
    case 18:
      if (ti_tuple->num_prec_radix == -1)
	{
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	}
      else
	{
	  c_value->value.l = ti_tuple->num_prec_radix;
	  c_value->length = sizeof (long);
	}
      c_value->type = SQL_C_LONG;
      break;
    case 19:
      if (ti_tuple->interval_precision == -1)
	{
	  c_value->value.dummy = NULL;
	  c_value->length = 0;
	}
      else
	{
	  c_value->value.l = ti_tuple->interval_precision;
	  c_value->length = sizeof (long);
	}
      c_value->type = SQL_C_LONG;
      break;
    default:
      odbc_set_diag (stmt->diag, "07009", 0, NULL);
      return ODBC_ERROR;
    }

  return ODBC_SUCCESS;
}

/************************************************************************
* name: create_table_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC ODBC_TABLE_VALUE *
create_table_value (void)
{
  ODBC_TABLE_VALUE *value = NULL;

  value = (ODBC_TABLE_VALUE *) UT_ALLOC (sizeof (ODBC_TABLE_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_TABLE_VALUE));

  return value;
}

/************************************************************************
* name: free_table_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC void
free_table_value (ODBC_TABLE_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->table_name);
      NC_FREE (value->table_type);
      UT_FREE (value);
    }
}

PRIVATE void
free_table_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_table_value ((ODBC_TABLE_VALUE *) (node->value));
      if (node->key != NULL)
	UT_FREE (node->key);
    }
}

/************************************************************************
 * name: make_table_result_set
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE RETCODE
make_table_result_set (ODBC_STATEMENT * stmt, int req_handle, int type_option)
{
  int cci_ind;
  int rc;
  int cci_rc;
  UNI_CCI_A_TYPE cci_value;

  long current_tpl_pos = 0;
  ODBC_TABLE_VALUE *table_node = NULL;
  int class_type;

  while (1)
    {
      // move cursor
      rc = move_cursor (req_handle, &current_tpl_pos, stmt->diag);
      if (rc < 0)
	{
	  if (rc == ODBC_NO_MORE_DATA)
	    break;
	  else
	    {
	      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_MOVE_CURSOR, NULL);
	      goto error;
	    }
	}

      // fetch data
      rc = fetch_tuple (req_handle, stmt->diag, 0);
      if (rc < 0)
	{
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_FETCH_CURSOR, NULL);
	  goto error;
	}

      // get class type data
      cci_rc = cci_get_data (req_handle, 2, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}

      if ((cci_value.i == 0 &&	// 0 system class
	   !IS_OPTION_SETTED (type_option, TABLE_TYPE_SYSTEM)) || 

	  (cci_value.i == 1 &&	// 1 virtual class
	   !IS_OPTION_SETTED (type_option, TABLE_TYPE_VIEW)) ||

	  (cci_value.i == 2 &&	// 2 class 
	   !IS_OPTION_SETTED (type_option, TABLE_TYPE_TABLE)))
	{
	  continue;
	}

      class_type = cci_value.i;


      // get class name
      cci_rc = cci_get_data (req_handle, 1, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}

      // create a tuple
      table_node = create_table_value ();
      if (table_node == NULL)
	continue;

      table_node->table_name = UT_MAKE_STRING (cci_value.str, -1);

      if (class_type == 0)
	{
	  table_node->table_type = UT_MAKE_STRING ("SYSTEM TABLE", -1);
	}
      else if (class_type == 1)
	{
	  table_node->table_type = UT_MAKE_STRING ("VIEW", -1);
	}
      else
	{
	  table_node->table_type = UT_MAKE_STRING ("TABLE", -1);
	}

      ListTailAdd (stmt->catalog_result.value, NULL, table_node, NodeAssign);
      ++stmt->tpl_number;
    }

  return ODBC_SUCCESS;
error:
  return ODBC_ERROR;
}

/************************************************************************
* name: create_column_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC ODBC_COLUMN_VALUE *
create_column_value (void)
{
  ODBC_COLUMN_VALUE *value = NULL;

  value = (ODBC_COLUMN_VALUE *) UT_ALLOC (sizeof (ODBC_COLUMN_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_COLUMN_VALUE));

  return value;
}

/************************************************************************
* name: free_column_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC void
free_column_value (ODBC_COLUMN_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->table_name);
      NC_FREE (value->column_name);
      NC_FREE (value->type_name);
      NC_FREE (value->default_value);
      UT_FREE (value);
    }
}

PRIVATE void
free_column_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_column_value ((ODBC_COLUMN_VALUE *) (node->value));
      if (node->key != NULL)
	UT_FREE (node->key);
    }
}

/************************************************************************
 * name: column_result_set_by_id
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE RETCODE
column_result_set_by_id (ODBC_STATEMENT * stmt,
			 char *table_name, char *column_name)
{
  RETCODE rc;
  int cci_rc;
  T_CCI_ERROR cci_err_buf;
  int handle = -1;

  cci_rc = cci_schema_info (stmt->conn->connhd, CCI_SCH_ATTRIBUTE,
			    table_name, column_name, 0, &cci_err_buf);
  ERROR_GOTO (cci_rc, cci_error);

  handle = cci_rc;

  rc = make_column_result_set (stmt, handle);
  ERROR_GOTO (rc, error);

  cci_close_req_handle (handle);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
error:
  if (handle > 0)
    cci_close_req_handle (handle);
  return ODBC_ERROR;
}

/************************************************************************
 * name: column_result_set_by_pv
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE RETCODE
column_result_set_by_pv (ODBC_STATEMENT * stmt,
			 char *table_name_pattern,
			 int table_type, char *column_name_pattern)
{
  RETCODE rc;
  int result;
  int cci_rc;
  T_CCI_ERROR cci_err_buf;
  UNI_CCI_A_TYPE cci_value;
  int cci_ind;
  int class_handle = -1, handle = -1;
  unsigned long current_tpl_pos;


  if (table_type == TABLE_TYPE_TABLE)
    {
      cci_rc = cci_schema_info (stmt->conn->connhd, CCI_SCH_CLASS,
				NULL, NULL, 0, &cci_err_buf);
    }
  else
    {
      cci_rc = cci_schema_info (stmt->conn->connhd, CCI_SCH_VCLASS,
				NULL, NULL, 0, &cci_err_buf);
    }
  ERROR_GOTO (cci_rc, cci_error);

  class_handle = cci_rc;

  current_tpl_pos = 0;
  while (1)
    {
      rc = move_cursor (class_handle, &current_tpl_pos, NULL);
      if (rc < 0)
	{
	  if (rc == ODBC_NO_MORE_DATA)
	    break;
	  else
	    {
	      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_MOVE_CURSOR, NULL);
	      goto error;
	    }
	}
      rc = fetch_tuple (class_handle, NULL, 0);
      if (rc < 0)
	{
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_FETCH_CURSOR, NULL);
	  goto error;
	}
      cci_rc = cci_get_data (class_handle, 1, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}

      // cci_value.str is table_name
      if (table_name_pattern != NULL)
	{
	  result = str_like (cci_value.str, table_name_pattern, '\\', 0);
	  if (result != _TRUE_)
	    continue;
	}

      cci_rc = cci_schema_info (stmt->conn->connhd, CCI_SCH_ATTRIBUTE,
				cci_value.str, NULL, 0, &cci_err_buf);
      ERROR_GOTO (cci_rc, cci_error);

      handle = cci_rc;

      // rc = make_column_result_set(stmt, handle, cci_value.str, column_name_pattern);
      ERROR_GOTO (rc, error);
    }

  if (handle > 0)
    cci_close_req_handle (handle);
  if (class_handle > 0)
    cci_close_req_handle (class_handle);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
error:
  if (handle > 0)
    cci_close_req_handle (handle);
  if (class_handle > 0)
    cci_close_req_handle (class_handle);
  return ODBC_ERROR;
}

/************************************************************************
 * name: make_column_result_set
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE RETCODE
make_column_result_set (ODBC_STATEMENT * stmt, int req_handle)
{

  int cci_ind;
  int rc;
  int cci_rc;
  UNI_CCI_A_TYPE cci_value;
  int precision, scale;
  short cci_type;

  long current_tpl_pos;
  ODBC_COLUMN_VALUE *column_node = NULL;

  current_tpl_pos = 0;

  while (1)
    {
      // move cursor
      rc = move_cursor (req_handle, &current_tpl_pos, stmt->diag);
      if (rc < 0)
	{
	  if (rc == ODBC_NO_MORE_DATA)
	    break;
	  else
	    {
	      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_MOVE_CURSOR, NULL);
	      goto error;
	    }
	}

      // fetch data
      rc = fetch_tuple (req_handle, stmt->diag, 0);
      if (rc < 0)
	{
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_FETCH_CURSOR, NULL);
	  goto error;
	}

      // create a tuple
      column_node = create_column_value ();
      if (column_node == NULL)
	continue;

      // get table name
      cci_rc = cci_get_data (req_handle, 11, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      column_node->table_name = UT_MAKE_STRING (cci_value.str, -1);

      // get column name
      cci_rc = cci_get_data (req_handle, 1, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      column_node->column_name = UT_MAKE_STRING (cci_value.str, -1);


      // get domain
      cci_rc = cci_get_data (req_handle, 2, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      cci_type = cci_value.i;

      if (CCI_IS_SET_TYPE (cci_type))
	{
	  /* XXX : deprecated     2000.9.21
	     cci_type == CCI_U_TYPE_SET ||
	     cci_type == CCI_U_TYPE_MULTISET ||
	     cci_type == CCI_U_TYPE_SEQUENCE ) {
	   */
	  scale = 0;
	  precision = stmt->conn->max_string_length;
	  //precision = MAX_CUBRID_CHAR_LEN;
	}
      else if (cci_type == CCI_U_TYPE_OBJECT)
	{
	  scale = 0;
	  precision = 32;
	}
      else
	{
	  // get scale
	  cci_rc = cci_get_data (req_handle, 3, CCI_A_TYPE_INT,
				 &cci_value, &cci_ind);
	  if (cci_rc < 0)
	    {
	      odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	      goto error;
	    }
	  scale = cci_value.i;

	  // get precision
	  cci_rc = cci_get_data (req_handle, 4, CCI_A_TYPE_INT,
				 &cci_value, &cci_ind);
	  if (cci_rc < 0)
	    {
	      odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	      goto error;
	    }
	  precision = cci_value.i;
	}

      column_node->concise_data_type = odbc_type_by_cci (cci_type, precision);
      // for 2.x backward compatibility
      if (odbc_is_valid_sql_date_type (column_node->concise_data_type))
	{
	  if (stmt->conn->env->attr_odbc_version == SQL_OV_ODBC2)
	    {
	      column_node->concise_data_type =
		odbc_date_type_backward (column_node->concise_data_type);
	    }
	}

      column_node->type_name =
	UT_MAKE_STRING (odbc_type_name (column_node->concise_data_type), -1);
      column_node->verbose_data_type =
	odbc_concise_to_verbose_type (column_node->concise_data_type);
      column_node->subcode =
	odbc_subcode_type (column_node->concise_data_type);

      column_node->column_size =
	odbc_column_size (column_node->concise_data_type, precision);
      if (column_node->column_size >= stmt->conn->max_string_length)
	column_node->column_size = stmt->conn->max_string_length;

      column_node->buffer_length =
	odbc_buffer_length (column_node->concise_data_type, precision);
      if (column_node->buffer_length >= stmt->conn->max_string_length)
	column_node->buffer_length = stmt->conn->max_string_length;

      column_node->decimal_digits =
	odbc_decimal_digits (column_node->concise_data_type, scale);
      column_node->num_prec_radix =
	odbc_num_prec_radix (column_node->concise_data_type);
      column_node->octet_length =
	odbc_octet_length (column_node->concise_data_type, precision);
      if (column_node->octet_length >= stmt->conn->max_string_length)
	column_node->octet_length = stmt->conn->max_string_length;

      // get nullable
      cci_rc = cci_get_data (req_handle, 6, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      if (cci_value.i == 1)
	{			// non null
	  column_node->nullable = SQL_NO_NULLS;
	}
      else
	{
	  column_node->nullable = SQL_NULLABLE;
	}

      // get ordinal_position
      cci_rc = cci_get_data (req_handle, 10, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      column_node->ordinal_position = cci_value.i;

      // get default value
      cci_rc = cci_get_data (req_handle, 9, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      if (cci_ind == -1)
	{			// non null
	  column_node->default_value = NULL;
	}
      else
	{
	  column_node->default_value = UT_MAKE_STRING (cci_value.str, -1);
	}


      column_node->ordinal_position = current_tpl_pos;

      ListTailAdd (stmt->catalog_result.value, NULL, column_node, NodeAssign);
      ++stmt->tpl_number;
    }

  return ODBC_SUCCESS;
error:
  NC_FREE_WH (free_column_value, column_node);

  // close cursor or free catalog result
  return ODBC_ERROR;
}


/************************************************************************
* name: create_stat_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC ODBC_STAT_VALUE *
create_stat_value (void)
{
  ODBC_STAT_VALUE *value = NULL;

  value = (ODBC_STAT_VALUE *) UT_ALLOC (sizeof (ODBC_STAT_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_STAT_VALUE));

  return value;
}

/************************************************************************
* name: free_stat_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC void
free_stat_value (ODBC_STAT_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->table_name);
      NC_FREE (value->index_name);
      NC_FREE (value->column_name);
      UT_FREE (value);
    }
}

PRIVATE void
free_stat_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_stat_value ((ODBC_STAT_VALUE *) (node->value));
      if (node->key != NULL)
	UT_FREE (node->key);
    }
}

/************************************************************************
* name: create_sp_column_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC ODBC_SP_COLUMN_VALUE *
create_sp_column_value (void)
{
  ODBC_SP_COLUMN_VALUE *value = NULL;

  value = (ODBC_SP_COLUMN_VALUE *) UT_ALLOC (sizeof (ODBC_SP_COLUMN_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_SP_COLUMN_VALUE));

  return value;
}

/************************************************************************
* name: free_sp_column_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC void
free_sp_column_value (ODBC_SP_COLUMN_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->column_name);
      NC_FREE (value->type_name);
      UT_FREE (value);
    }
}

PRIVATE void
free_sp_column_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_sp_column_value ((ODBC_SP_COLUMN_VALUE *) (node->value));
      if (node->key != NULL)
	UT_FREE (node->key);
    }
}


PRIVATE RETCODE
get_sp_column_info (ODBC_STATEMENT * stmt,
		    char *table_name, ODBC_SPC_INFO * spc_info_ptr)
{
  RETCODE rc;
  int cci_rc;
  int cci_handle = -1;
  T_CCI_ERROR cci_err_buf;
  UNI_CCI_A_TYPE cci_value;
  int cci_ind;
  int current_tpl_pos = 0;

  cci_rc = cci_schema_info (stmt->conn->connhd, CCI_SCH_ATTRIBUTE,
			    table_name, spc_info_ptr->column_name, 0,
			    &cci_err_buf);
  ERROR_GOTO (cci_rc, cci_error);

  cci_handle = cci_rc;

  rc = move_cursor (cci_handle, &current_tpl_pos, stmt->diag);
  if (rc < 0)
    {
      if (rc == ODBC_NO_MORE_DATA)
	return ODBC_NO_MORE_DATA;
      else
	{
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_MOVE_CURSOR, NULL);
	  goto error;
	}
    }

  // fetch data
  rc = fetch_tuple (cci_handle, stmt->diag, 0);
  if (rc < 0)
    {
      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_FETCH_CURSOR, NULL);
      goto error;
    }

  // get non-null
  cci_rc = cci_get_data (cci_handle, 6, CCI_A_TYPE_INT, &cci_value, &cci_ind);
  if (cci_rc < 0)
    {
      odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
      goto error;
    }

  if (cci_value.i == 1)
    {
      spc_info_ptr->is_non_null = _TRUE_;
    }
  else
    {
      spc_info_ptr->is_non_null = _FALSE_;
    }

  // get domain
  cci_rc = cci_get_data (cci_handle, 2, CCI_A_TYPE_INT, &cci_value, &cci_ind);
  if (cci_rc < 0)
    {
      odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
      odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
      goto error;
    }
  spc_info_ptr->domain = cci_value.i;

  if (CCI_IS_SET_TYPE (spc_info_ptr->domain))
    {
      /* XXX : deprecated 2000.9.21
         spc_info_ptr->domain == CCI_U_TYPE_SET ||
         spc_info_ptr->domain == CCI_U_TYPE_SET ||
         spc_info_ptr->domain == CCI_U_TYPE_SET ) {
       */
      spc_info_ptr->precision = -1;
      spc_info_ptr->scale = 0;
    }
  else if (spc_info_ptr->domain == CCI_U_TYPE_OBJECT)
    {
      spc_info_ptr->precision = 32;
      spc_info_ptr->scale = 0;
    }
  else
    {

      // get precision
      cci_rc = cci_get_data (cci_handle, 4, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      spc_info_ptr->precision = cci_value.i;

      // get scale
      cci_rc = cci_get_data (cci_handle, 3, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  odbc_set_diag_by_icode (stmt->diag, ODBC_CAS_GET_DATA, NULL);
	  goto error;
	}
      spc_info_ptr->scale = cci_value.i;
    }

  if (cci_handle > 0)
    cci_close_req_handle (cci_handle);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
error:
  if (cci_handle > 0)
    cci_close_req_handle (cci_handle);
  return ODBC_ERROR;
}


PRIVATE RETCODE
make_sp_column_result_set (ODBC_STATEMENT * stmt,
			   char *table_name, ODBC_SPC_INFO * spc_info_ptr)
{
  ODBC_SP_COLUMN_VALUE *spc_node = NULL;

  spc_node = create_sp_column_value ();
  if (spc_node == NULL)
    return ODBC_MEMORY_ALLOC_ERROR;

  spc_node->column_name = UT_MAKE_STRING (spc_info_ptr->column_name, -1);
  spc_node->concise_data_type =
    odbc_type_by_cci (spc_info_ptr->domain, spc_info_ptr->precision);
  // for 2.x backward compatibility
  if (odbc_is_valid_sql_date_type (spc_node->concise_data_type))
    {
      if (stmt->conn->env->attr_odbc_version == SQL_OV_ODBC2)
	{
	  spc_node->concise_data_type =
	    odbc_date_type_backward (spc_node->concise_data_type);
	}
    }
  spc_node->type_name =
    UT_MAKE_STRING (odbc_type_name (spc_node->concise_data_type), -1);
  spc_node->column_size =
    odbc_column_size (spc_node->concise_data_type, spc_info_ptr->precision);
  spc_node->buffer_length =
    odbc_column_size (spc_node->concise_data_type, spc_info_ptr->precision);
  spc_node->decimal_digits =
    odbc_decimal_digits (spc_node->concise_data_type, spc_info_ptr->scale);


  ListTailAdd (stmt->catalog_result.value, NULL, spc_node, NodeAssign);
  ++stmt->tpl_number;

  return ODBC_SUCCESS;
}

/************************************************************************
* name: create_type_info_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC ODBC_TYPE_INFO_VALUE *
create_type_info_value (void)
{
  ODBC_TYPE_INFO_VALUE *value = NULL;

  value = (ODBC_TYPE_INFO_VALUE *) UT_ALLOC (sizeof (ODBC_TYPE_INFO_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_TYPE_INFO_VALUE));

  return value;
}

/************************************************************************
* name: free_type_info_value
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC void
free_type_info_value (ODBC_TYPE_INFO_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->type_name);
      NC_FREE (value->create_params);
      NC_FREE (value->literal_prefix);
      NC_FREE (value->literal_suffix);
      NC_FREE (value->local_type_name);
      UT_FREE (value);
    }
}

PRIVATE void
free_type_info_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_type_info_value ((ODBC_TYPE_INFO_VALUE *) (node->value));
      if (node->key != NULL)
	UT_FREE (node->key);
    }
}

/************************************************************************
* name: free_catalog_result
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC void
free_catalog_result (ST_LIST * result, RESULT_TYPE type)
{
  void (*nodeDelete) (ST_LIST *) = NULL;

  if (result != NULL)
    {
      if (type == TABLES)
	{
	  nodeDelete = free_table_node;
	}
      else if (type == COLUMNS)
	{
	  nodeDelete = free_column_node;
	}
      else if (type == SPECIALCOLUMNS)
	{
	  nodeDelete = free_sp_column_node;
	}
      else if (type == STATISTICS)
	{
	  nodeDelete = free_stat_node;
	}
      else if (type == TYPE_INFO)
	{
	  nodeDelete = free_type_info_node;
	}
      ListDelete (result, nodeDelete);
    }
}
