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
#include <stdio.h>
#include <string.h>

#include "odbc_portable.h"
#include "sqlext.h"
#include "odbc_util.h"
#include "odbc_diag_record.h"
#include "odbc_statement.h"
#include "odbc_result.h"
#include "odbc_type.h"
#include "cas_cci.h"
#include "odbc_catalog.h"

#define TABLE_TYPE_TABLE    ( 0x1 )
#define TABLE_TYPE_VIEW     ( 0x2 )
#define	TABLE_TYPE_SYSTEM   ( 0x4 )
#define TABLE_TYPE_ALL      ( TABLE_TYPE_TABLE | TABLE_TYPE_VIEW | TABLE_TYPE_SYSTEM )

/* NC:num columns */
#define NC_CATALOG_TABLES			5
#define NC_CATALOG_COLUMNS			18
#define NC_CATALOG_SPECIALCOLUMNS		8
#define NC_CATALOG_STATISTICS			13
#define NC_CATALOG_TYPE_INFO			19
#define NC_CATALOG_PRIMARY_KEYS			6
#define NC_CATALOG_FOREIGN_KEYS			14
#define NC_CATALOG_TABLE_PRIVILEGES		7
#define NC_CATALOG_PROCEDURES			8
#define NC_CATALOG_PROCEDURE_COLUMNS		19

typedef struct tagODBC_COL_INFO
{
  const char *name;
  short type;
  int length;
  short precision;
  short scale;
} ODBC_COL_INFO;

typedef struct tagODBC_TABLE_VALUE
{
  char *table_name;
  char *table_type;
} ODBC_TABLE_VALUE;

typedef struct tagODBC_COLUMN_VALUE
{
  char *table_name;
  char *column_name;
  short concise_data_type;
  char *type_name;
  int column_size;
  int buffer_length;
  short decimal_digits;
  short num_prec_radix;
  short nullable;
  char *default_value;
  short verbose_data_type;
  short subcode;
  int octet_length;
  int ordinal_position;
} ODBC_COLUMN_VALUE;

typedef struct tagODBC_STAT_VALUE
{
  char *table_name;
  short type;
  short non_unique;
  char *index_name;
  char *column_name;
  char *asc_or_desc;
  long cardinality;
  long pages;
  short ordinal_position;
} ODBC_STAT_VALUE;

typedef struct tagODBC_SPECIAL_COLUMN_VALUE
{
  char *column_name;
  short concise_data_type;
  char *type_name;
  int column_size;
  int buffer_length;
  short decimal_digits;
} ODBC_SP_COLUMN_VALUE;

typedef struct tagODBC_TYPE_INFO_VALUE
{
  char *type_name;
  short data_type;
  int column_size;
  char *literal_prefix;
  char *literal_suffix;
  char *create_params;
  short nullable;
  short case_sensitive;
  short searchable;
  short unsigned_attribute;
  short fixed_prec_scale;
  short auto_unique_value;
  char *local_type_name;
  short minimum_scale;
  short maximum_scale;
  short sql_data_type;
  short sql_datetime_sub;
  short num_prec_radix;
  short interval_precision;
} ODBC_TYPE_INFO_VALUE;

typedef struct tagODBC_PRIMARY_KEYS_VALUE
{
  char *table_name;
  char *column_name;
  short key_sequence;
  char *key_name;
} ODBC_PRIMARY_KEYS_VALUE;

typedef struct tagODBC_FOREIGN_KEYS_VALUE
{
  char *pk_table_name;
  char *pk_column_name;
  char *fk_table_name;
  char *fk_column_name;
  short key_sequence;
  short update_rule;
  short delete_rule;
  char *fk_name;
  char *pk_name;
} ODBC_FOREIGN_KEYS_VALUE;

typedef struct tagODBC_TABLE_PRIVILEGES_VALUE
{
  char *table_name;
  char *grantor;
  char *grantee;
  char *privilege;
  char *is_grantable;
} ODBC_TABLE_PRIVILEGES_VALUE;

typedef struct tagODBC_PROCEDURES_VALUE
{
  char *proc_name;
  short proc_type;
} ODBC_PROCEDURES_VALUE;

typedef struct tagODBC_PROCEDURE_COLUMNS_VALUE
{
  char *proc_name;
  char *column_name;
  short column_type;
  short concise_data_type;
  char *type_name;
  int column_size;
  int buffer_length;
  short decimal_digits;
  short num_prec_radix;
  short nullable;
  char *default_value;
  short verbose_data_type;
  short subcode;
  int octet_length;
  int ordinal_position;
} ODBC_PROCEDURE_COLUMNS_VALUE;

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

PRIVATE ODBC_COL_INFO primary_keys_cinfo[] = {
  {"TABLE_CAT", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_SCHEM", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"COLUMN_NAME", SQL_VARCHAR, 128, 128, 0},
  {"KEY_SEQ", SQL_SMALLINT, 5, 5, 0},
  {"PK_NAME", SQL_VARCHAR, 128, 128, 0},
  {NULL}
};

PRIVATE ODBC_COL_INFO foreign_keys_cinfo[] = {
  {"PKTABLE_CAT", SQL_VARCHAR, 128, 128, 0},
  {"PKTABLE_SCHEM", SQL_VARCHAR, 128, 128, 0},
  {"PKTABLE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"PKCOLUMN_NAME", SQL_VARCHAR, 128, 128, 0},
  {"FKTABLE_CAT", SQL_VARCHAR, 128, 128, 0},
  {"FKTABLE_SCHEM", SQL_VARCHAR, 128, 128, 0},
  {"FKTABLE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"FKCOLUMN_NAME", SQL_VARCHAR, 128, 128, 0},
  {"KEY_SEQ", SQL_SMALLINT, 5, 5, 0},
  {"UPDATE_RULE", SQL_SMALLINT, 5, 5, 0},
  {"DELETE_RULE", SQL_SMALLINT, 5, 5, 0},
  {"FK_NAME", SQL_VARCHAR, 128, 128, 0},
  {"PK_NAME", SQL_VARCHAR, 128, 128, 0},
  {"DEFERRABILITY", SQL_SMALLINT, 5, 5, 0},
  {NULL}
};

PRIVATE ODBC_COL_INFO table_privileges_cinfo[] = {
  {"TABLE_CAT", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_SCHEM", SQL_VARCHAR, 128, 128, 0},
  {"TABLE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"GRANTOR", SQL_VARCHAR, 128, 128, 0},
  {"GRANTEE", SQL_VARCHAR, 128, 128, 0},
  {"PRIVILEGE", SQL_VARCHAR, 128, 128, 0},
  {"IS_GRANTABLE", SQL_VARCHAR, 128, 128, 0},
  {NULL}
};

PRIVATE ODBC_COL_INFO procedures_cinfo[] = {
  {"PROCEDURE_CAT", SQL_VARCHAR, 128, 128, 0},
  {"PROCEDURE_SCHEM", SQL_VARCHAR, 128, 128, 0},
  {"PROCEDURE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"NUM_INPUT_PARAMS", SQL_UNKNOWN_TYPE, 0, 0, 0},
  {"NUM_OUTPUT_PARAMS", SQL_UNKNOWN_TYPE, 0, 0, 0},
  {"NUM_RESULT_SETS", SQL_UNKNOWN_TYPE, 0, 0, 0},
  {"REMARKS", SQL_VARCHAR, 128, 128, 0},
  {"PROCEDURE_TYPE", SQL_SMALLINT, 5, 5, 0},
  {NULL}
};

PRIVATE ODBC_COL_INFO procedure_columns_cinfo[] = {
  {"PROCEDURE_CAT", SQL_VARCHAR, 128, 128, 0},
  {"PROCEDURE_SCHEM", SQL_VARCHAR, 128, 128, 0},
  {"PROCEDURE_NAME", SQL_VARCHAR, 128, 128, 0},
  {"COLUMN_NAME", SQL_VARCHAR, 128, 128, 0},
  {"COLUMN_TYPE", SQL_SMALLINT, 5, 5, 0},

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

#ifdef DELPHI
  {"STRING", SQL_LONGVARCHAR, MAX_CUBRID_CHAR_LEN, "'", "'", "length",
   SQL_NULLABLE,
   SQL_FALSE, SQL_SEARCHABLE, -1, SQL_FALSE, SQL_FALSE, "STRING", -1, -1,
   SQL_LONGVARCHAR, -1, -1, -1},
#endif

  {NULL}
};

PRIVATE RETCODE make_table_result_set (ODBC_STATEMENT * stmt, int req_handle,
				       int handle_type);
PRIVATE RETCODE make_column_result_set (ODBC_STATEMENT * stmt,
					int req_handle);
PRIVATE RETCODE make_stat_result_set (ODBC_STATEMENT * stmt, int req_handle,
				      char *table_name,
				      unsigned short unique);
PRIVATE RETCODE make_sp_column_result_set (ODBC_STATEMENT * stmt,
					   int req_handle, char *table_name);
PRIVATE RETCODE make_primary_keys_result_set (ODBC_STATEMENT * stmt,
					      int req_handle);
PRIVATE RETCODE make_foreign_keys_result_set (ODBC_STATEMENT * stmt,
					      int req_handle);
PRIVATE RETCODE make_table_privileges_result_set (ODBC_STATEMENT * stmt,
						  int req_handle);
PRIVATE RETCODE make_procedures_result_set (ODBC_STATEMENT * stmt,
					    int req_handle);
PRIVATE RETCODE make_procedure_columns_result_set (ODBC_STATEMENT * stmt,
						   int req_handle);

PRIVATE RETCODE odbc_get_table_data (ODBC_STATEMENT * stmt, short col_index,
				     VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_column_data (ODBC_STATEMENT * stmt, short col_index,
				      VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_stat_data (ODBC_STATEMENT * stmt, short col_index,
				    VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_sp_column_data (ODBC_STATEMENT * stmt,
					 short col_index,
					 VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_type_info_data (ODBC_STATEMENT * stmt,
					 short col_index,
					 VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_primary_keys_data (ODBC_STATEMENT * stmt,
					    short col_index,
					    VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_foreign_keys_data (ODBC_STATEMENT * stmt,
					    short col_index,
					    VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_table_privileges_data (ODBC_STATEMENT * stmt,
						short col_index,
						VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_procedures_data (ODBC_STATEMENT * stmt,
					  short col_index,
					  VALUE_CONTAINER * c_value);
PRIVATE RETCODE odbc_get_procedure_columns_data (ODBC_STATEMENT * stmt,
						 short col_index,
						 VALUE_CONTAINER * c_value);

PRIVATE ODBC_TABLE_VALUE *create_table_value (void);
PRIVATE ODBC_COLUMN_VALUE *create_column_value (void);
PRIVATE ODBC_STAT_VALUE *create_stat_value (void);
PRIVATE ODBC_PRIMARY_KEYS_VALUE *create_primary_keys_value (void);
PRIVATE ODBC_FOREIGN_KEYS_VALUE *create_foreign_keys_value (void);
PRIVATE ODBC_SP_COLUMN_VALUE *create_sp_column_value (void);
PRIVATE ODBC_TYPE_INFO_VALUE *create_type_info_value (void);
PRIVATE ODBC_TABLE_PRIVILEGES_VALUE *create_table_privileges_value (void);
PRIVATE ODBC_PROCEDURES_VALUE *create_procedures_value (void);
PRIVATE ODBC_PROCEDURE_COLUMNS_VALUE *create_procedure_columns_value (void);

PRIVATE void free_table_value (ODBC_TABLE_VALUE * value);
PRIVATE void free_column_value (ODBC_COLUMN_VALUE * value);
PRIVATE void free_stat_value (ODBC_STAT_VALUE * value);
PRIVATE void free_sp_column_value (ODBC_SP_COLUMN_VALUE * value);
PRIVATE void free_type_info_value (ODBC_TYPE_INFO_VALUE * value);
PRIVATE void free_primary_keys_value (ODBC_PRIMARY_KEYS_VALUE * value);
PRIVATE void free_foreign_keys_value (ODBC_FOREIGN_KEYS_VALUE * value);
PRIVATE void free_table_privileges_value (ODBC_TABLE_PRIVILEGES_VALUE *
					  value);
PRIVATE void free_procedures_value (ODBC_PROCEDURES_VALUE * value);
PRIVATE void free_procedure_columns_value (ODBC_PROCEDURE_COLUMNS_VALUE *
					   value);

PRIVATE void free_table_node (ST_LIST * node);
PRIVATE void free_column_node (ST_LIST * node);
PRIVATE void free_stat_node (ST_LIST * node);
PRIVATE void free_sp_column_node (ST_LIST * node);
PRIVATE void free_type_info_node (ST_LIST * node);
PRIVATE void free_primary_keys_node (ST_LIST * node);
PRIVATE void free_foreign_keys_node (ST_LIST * node);
PRIVATE void free_table_privileges_node (ST_LIST * node);
PRIVATE void free_procedures_node (ST_LIST * node);
PRIVATE void free_procedure_columns_node (ST_LIST * node);

PRIVATE int retrieve_table_from_db_class (int cci_connection,
					  char *table_name, T_CCI_ERROR *error);
PRIVATE int schema_info_table_privileges (int cci_connection,
					  int *cci_request, char *table_name, T_CCI_ERROR *error);
PRIVATE int schema_info_procedures (int cci_connection, int *cci_request,
				    char *proc_name, T_CCI_ERROR *error);
PRIVATE int schema_info_procedure_columns (int cci_connection,
					   int *cci_request, char *proc_name,
					   char *column_name, T_CCI_ERROR *error);
PRIVATE int sql_execute (int cci_connection, int *cci_request,
			 char *sql_statment, char *param_name[],
			 int param_num, T_CCI_ERROR *error);

PRIVATE void catalog_result_set_init (ODBC_STATEMENT * stmt,
				      RESULT_TYPE task_type);
PRIVATE void catalog_set_ird (ODBC_STATEMENT * stmt,
			      ODBC_COL_INFO * colum_info, int column_num);
PRIVATE void err_msg_table_not_exist (char *err_msg, const char *db_name,
				      const char *table_name);

PUBLIC RETCODE
odbc_tables (ODBC_STATEMENT * stmt,
	     char *catalog_name, char *schema_name,
	     char *table_name, char *table_type)
{
  int t_type = 0;
  char search_pattern_flag;
  int handle = -1;

  T_CCI_ERROR cci_err_buf;
  int cci_rc;

  RETCODE rc;

  catalog_result_set_init (stmt, TABLES);
  catalog_set_ird (stmt, table_cinfo, NC_CATALOG_TABLES);

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
  odbc_set_diag_by_cci (stmt->diag, cci_rc, &cci_err_buf);
error:
  if (handle > 0)
    {
      cci_close_req_handle (handle);
    }
  return ODBC_ERROR;
}

PUBLIC RETCODE
odbc_columns (ODBC_STATEMENT * stmt,
	      char *catalog_name,
	      char *schema_name, char *table_name, char *column_name)
{
  RETCODE rc;
  char search_pattern_flag;
  int handle = -1;

  int cci_rc;
  T_CCI_ERROR cci_err_buf;

  catalog_result_set_init (stmt, COLUMNS);
  catalog_set_ird (stmt, column_cinfo, NC_CATALOG_COLUMNS);

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
  odbc_set_diag_by_cci (stmt->diag, cci_rc, &cci_err_buf);
error:
  if (handle > 0)
    cci_close_req_handle (handle);

  return ODBC_ERROR;
}

PUBLIC RETCODE
odbc_statistics (ODBC_STATEMENT * stmt,
		 char *catalog_name, char *schema_name,
		 char *table_name, unsigned short unique,
		 unsigned short reversed)
{
  int cci_retval = 0;
  int cci_request = 0;

  T_CCI_ERROR cci_error;
  char err_msg[SQL_MAX_MESSAGE_LENGTH + 1];

  catalog_result_set_init (stmt, STATISTICS);
  catalog_set_ird (stmt, statistics_cinfo, NC_CATALOG_STATISTICS);

  cci_retval = retrieve_table_from_db_class (stmt->conn->connhd, table_name, &cci_error);
  if (cci_retval == 0)
    {
      err_msg_table_not_exist (err_msg, stmt->conn->db_name, table_name);
      odbc_set_diag (stmt->diag, "HY000", 0, err_msg);
      goto error;
    }
  else if (cci_retval < 0)
    {
      goto cci_error;
    }

  if ((cci_request =
       cci_schema_info (stmt->conn->connhd, CCI_SCH_CONSTRAINT, table_name,
			NULL, 0, &cci_error)) < 0)
    {
      goto cci_error;
    }

  if (make_stat_result_set (stmt, cci_request, table_name, unique) < 0)
    {
      goto error;
    }

  cci_close_req_handle (cci_request);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, &cci_error);
error:
  if (cci_request > 0)
    {
      cci_close_req_handle (cci_request);
    }
  return ODBC_ERROR;
}

PUBLIC RETCODE
odbc_special_columns (ODBC_STATEMENT * stmt,
		      short identifier_type,
		      char *catalog_name,
		      char *schema_name,
		      char *table_name, short scope, short nullable)
{
  int cci_retval = 0;
  int cci_request = 0;

  T_CCI_ERROR cci_error;
  char err_msg[SQL_MAX_MESSAGE_LENGTH + 1];

  catalog_result_set_init (stmt, SPECIALCOLUMNS);
  catalog_set_ird (stmt, special_column_cinfo, NC_CATALOG_SPECIALCOLUMNS);

  /* CUBRID not have auto-update value */
  if (identifier_type == SQL_ROWVER)
    {
      return ODBC_SUCCESS;
    }

  cci_retval = retrieve_table_from_db_class (stmt->conn->connhd, table_name, &cci_error);
  if (cci_retval == 0)
    {
      err_msg_table_not_exist (err_msg, stmt->conn->db_name, table_name);
      odbc_set_diag (stmt->diag, "HY000", 0, err_msg);
      goto error;
    }
  else if (cci_retval < 0)
    {
      goto cci_error;
    }

  if ((cci_request =
       cci_schema_info (stmt->conn->connhd, CCI_SCH_CONSTRAINT, table_name,
			NULL, 0, &cci_error)) < 0)
    {
      goto cci_error;
    }

  if (make_sp_column_result_set (stmt, cci_request, table_name) < 0)
    {
      goto error;
    }

  cci_close_req_handle (cci_request);

  return ODBC_SUCCESS;

cci_error:
	odbc_set_diag_by_cci (stmt->diag, cci_retval, &cci_error);
error:
  if (cci_request > 0)
    {
      cci_close_req_handle (cci_request);
    }
  return ODBC_ERROR;
}

PUBLIC RETCODE
odbc_foreign_keys (ODBC_STATEMENT * stmt, char *pk_table_name,
		   char *fk_table_name)
{
  int cci_retval = 0;
  int cci_request = 0;

  T_CCI_ERROR cci_error;
  char search_pattern_flag;

  char err_msg[SQL_MAX_MESSAGE_LENGTH + 1];

  catalog_result_set_init (stmt, FOREIGN_KEYS);
  catalog_set_ird (stmt, foreign_keys_cinfo, NC_CATALOG_FOREIGN_KEYS);

  if (pk_table_name)
    {
      cci_retval =
	retrieve_table_from_db_class (stmt->conn->connhd, pk_table_name, &cci_error);
      if (cci_retval == 0)
	{
	  err_msg_table_not_exist (err_msg, stmt->conn->db_name,
				   pk_table_name);
	  odbc_set_diag (stmt->diag, "HY000", 0, err_msg);
	  goto error;
	}
      else if (cci_retval < 0)
	{
	  goto cci_error;
	}
    }

  if (fk_table_name)
    {
      cci_retval =
	retrieve_table_from_db_class (stmt->conn->connhd, fk_table_name, &cci_error);
      if (cci_retval == 0)
	{
	  err_msg_table_not_exist (err_msg, stmt->conn->db_name,
				   fk_table_name);
	  odbc_set_diag (stmt->diag, "HY000", 0, err_msg);
	  goto error;
	}
      else if (cci_retval < 0)
	{
	  goto cci_error;
	}
    }

  if (stmt->attr_metadata_id == SQL_TRUE)
    {
      search_pattern_flag = 0;
    }
  else
    {
      search_pattern_flag = CCI_CLASS_NAME_PATTERN_MATCH;
    }

  if (pk_table_name && fk_table_name)
    {
      cci_request =
	cci_schema_info (stmt->conn->connhd, CCI_SCH_CROSS_REFERENCE,
			 pk_table_name, fk_table_name, search_pattern_flag,
			 &cci_error);
    }
  else if (pk_table_name)
    {
      cci_request =
	cci_schema_info (stmt->conn->connhd, CCI_SCH_EXPORTED_KEYS,
			 pk_table_name, NULL, search_pattern_flag,
			 &cci_error);
    }
  else if (fk_table_name)
    {
      cci_request =
	cci_schema_info (stmt->conn->connhd, CCI_SCH_IMPORTED_KEYS,
			 fk_table_name, NULL, search_pattern_flag,
			 &cci_error);
    }

  if (cci_request < 0)
    {
      cci_retval = cci_request;
      goto cci_error;
    }

  if (make_foreign_keys_result_set (stmt, cci_request) < 0)
    {
      goto error;
    }

  cci_close_req_handle (cci_request);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, &cci_error);
error:
  if (cci_request > 0)
    {
      cci_close_req_handle (cci_request);
    }
  return ODBC_ERROR;
}

PUBLIC RETCODE
odbc_primary_keys (ODBC_STATEMENT * stmt, char *catalog_name,
		   char *schema_name, char *table_name)
{
  int cci_retval = 0;
  int cci_request = 0;

  T_CCI_ERROR cci_error;
  char search_pattern_flag;

  char err_msg[SQL_MAX_MESSAGE_LENGTH + 1];

  catalog_result_set_init (stmt, PRIMARY_KEYS);
  catalog_set_ird (stmt, primary_keys_cinfo, NC_CATALOG_PRIMARY_KEYS);

  cci_retval = retrieve_table_from_db_class (stmt->conn->connhd, table_name, &cci_error);
  if (cci_retval == 0)
    {
      err_msg_table_not_exist (err_msg, stmt->conn->db_name, table_name);
      odbc_set_diag (stmt->diag, "HY000", 0, err_msg);
      goto error;
    }
  else if (cci_retval < 0)
    {
      goto cci_error;
    }

  if (stmt->attr_metadata_id == SQL_TRUE)
    {
      search_pattern_flag = 0;
    }
  else
    {
      search_pattern_flag = CCI_CLASS_NAME_PATTERN_MATCH;
    }

  if ((cci_request =
       cci_schema_info (stmt->conn->connhd, CCI_SCH_PRIMARY_KEY, table_name,
			NULL, search_pattern_flag, &cci_error)) < 0)
    {
      cci_retval = cci_request;
      goto cci_error;
    }

  if (make_primary_keys_result_set (stmt, cci_request) < 0)
    {
      goto error;
    }

  cci_close_req_handle (cci_request);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, &cci_error);
error:
  if (cci_request > 0)
    {
      cci_close_req_handle (cci_request);
    }
  return ODBC_ERROR;
}

PUBLIC RETCODE
odbc_table_privileges (ODBC_STATEMENT * stmt, char *catalog_name,
		       char *schema_name, char *table_name)
{
  int cci_retval = 0;
  int cci_request = 0;
	T_CCI_ERROR cci_error;

  char err_msg[SQL_MAX_MESSAGE_LENGTH + 1];

  catalog_result_set_init (stmt, TABLE_PRIVILEGES);
  catalog_set_ird (stmt, table_privileges_cinfo, NC_CATALOG_TABLE_PRIVILEGES);

  cci_retval = retrieve_table_from_db_class (stmt->conn->connhd, table_name, &cci_error);
  if (cci_retval == 0)
    {
      err_msg_table_not_exist (err_msg, stmt->conn->db_name, table_name);
      odbc_set_diag (stmt->diag, "HY000", 0, err_msg);
      goto error;
    }
  else if (cci_retval < 0)
    {
      goto cci_error;
    }

  if ((cci_retval = schema_info_table_privileges (stmt->conn->connhd,
						  &cci_request,
						  table_name, &cci_error)) < 0)
    {
      goto cci_error;
    }

  if (make_table_privileges_result_set (stmt, cci_request) < 0)
    {
      goto error;
    }

  cci_close_req_handle (cci_request);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, &cci_error);
error:
  if (cci_request > 0)
    {
      cci_close_req_handle (cci_request);
    }
  return ODBC_ERROR;
}

PUBLIC RETCODE
odbc_procedures (ODBC_STATEMENT * stmt, char *catalog_name,
		 char *schema_name, char *proc_name)
{
  int cci_retval = 0;
  int cci_request = 0;
	T_CCI_ERROR cci_error;

  catalog_result_set_init (stmt, PROCEDURES);
  catalog_set_ird (stmt, procedures_cinfo, NC_CATALOG_PROCEDURES);

  if ((cci_retval = schema_info_procedures (stmt->conn->connhd,
					    &cci_request, proc_name, &cci_error)) < 0)
    {
      goto cci_error;
    }

  if (make_procedures_result_set (stmt, cci_request) < 0)
    {
      goto error;
    }

  cci_close_req_handle (cci_request);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, &cci_error);
error:
  if (cci_request > 0)
    {
      cci_close_req_handle (cci_request);
    }
  return ODBC_ERROR;
}

PUBLIC RETCODE
odbc_procedure_columns (ODBC_STATEMENT * stmt,
			char *catalog_name,
			char *schema_name, char *proc_name, char *column_name)
{
  int cci_retval = 0;
  int cci_request = 0;
	T_CCI_ERROR cci_error;

  catalog_result_set_init (stmt, PROCEDURE_COLUMNS);
  catalog_set_ird (stmt, procedure_columns_cinfo,
		   NC_CATALOG_PROCEDURE_COLUMNS);

  if ((cci_retval = schema_info_procedure_columns (stmt->conn->connhd,
						   &cci_request, proc_name,
						   column_name, &cci_error)) < 0)
    {
      goto cci_error;
    }

  if (make_procedure_columns_result_set (stmt, cci_request) < 0)
    {
      goto error;
    }

  cci_close_req_handle (cci_request);

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, &cci_error);
error:
  if (cci_request > 0)
    {
      cci_close_req_handle (cci_request);
    }
  return ODBC_ERROR;
}

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

PUBLIC RETCODE
odbc_get_catalog_data (ODBC_STATEMENT * stmt,
		       short col_index, VALUE_CONTAINER * c_value)
{
  RETCODE rc = ODBC_SUCCESS;

  switch (stmt->result_type)
    {
    case TABLES:
      rc = odbc_get_table_data (stmt, col_index, c_value);
      break;
    case COLUMNS:
      rc = odbc_get_column_data (stmt, col_index, c_value);
      break;
    case STATISTICS:
      rc = odbc_get_stat_data (stmt, col_index, c_value);
      break;
    case SPECIALCOLUMNS:
      rc = odbc_get_sp_column_data (stmt, col_index, c_value);
      break;
    case TYPE_INFO:
      rc = odbc_get_type_info_data (stmt, col_index, c_value);
      break;
    case PRIMARY_KEYS:
      rc = odbc_get_primary_keys_data (stmt, col_index, c_value);
      break;
    case FOREIGN_KEYS:
      rc = odbc_get_foreign_keys_data (stmt, col_index, c_value);
      break;
    case TABLE_PRIVILEGES:
      rc = odbc_get_table_privileges_data (stmt, col_index, c_value);
      break;
    case PROCEDURES:
      rc = odbc_get_procedures_data (stmt, col_index, c_value);
      break;
    case PROCEDURE_COLUMNS:
      rc = odbc_get_procedure_columns_data (stmt, col_index, c_value);
      break;
    default:
      rc = ODBC_ERROR;
      break;
    }

  return rc;
}

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

  if (stmt->catalog_result.current == NULL)
    {
      return ODBC_NO_MORE_DATA;
    }

  stat_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:
      /* catalog name , SQL_C_CHAR */
    case 2:
      /* schema name, SQL_C_CHAR */
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 3:
      /* table name, SQL_C_CHAR */
      c_value->value.str = stat_tuple->table_name;
      c_value->length = strlen (stat_tuple->table_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 4:
      /* non-unique , SQL_C_SHORT */
      c_value->value.s = stat_tuple->non_unique;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 5:
      /* index qualifier, SQL_C_CHAR */
      c_value->value.dummy = NULL;
      c_value->length = 0;
      c_value->type = SQL_C_CHAR;
      break;
    case 6:
      /* index name, SQL_C_CHAR */
      c_value->value.str = stat_tuple->index_name;
      c_value->length = strlen (stat_tuple->index_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 7:
      /* type, SQL_C_SHORT */
      c_value->value.s = stat_tuple->type;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 8:
      /* ordinal position, SQL_C_SHORT */
      c_value->value.s = stat_tuple->ordinal_position;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 9:
      /* column name, SQL_C_CHAR */
      c_value->value.str = stat_tuple->column_name;
      c_value->length = strlen (stat_tuple->column_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 10:
      /* asc_or_desc, SQL_C_CHAR */
      c_value->value.str = stat_tuple->asc_or_desc;
      c_value->length = strlen (stat_tuple->asc_or_desc) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 11:
      /* cardinality, SQL_C_LONG */
      c_value->value.l = stat_tuple->cardinality;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 12:
      /* pages, SQL_C_LONG */
      c_value->value.l = stat_tuple->pages;
      c_value->length = sizeof (long);
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
    case 1:
      /* scope, SQL_C_SHORT */
      c_value->value.s = SQL_SCOPE_CURROW;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 2:
      /* column name, SQL_C_CHAR */
      c_value->value.str = spc_tuple->column_name;
      c_value->length = strlen (spc_tuple->column_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 3:
      /* data type, SQL_C_SHORT */
      c_value->value.s = spc_tuple->concise_data_type;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 4:
      /* type name, SQL_C_CHAR */
      c_value->value.str = spc_tuple->type_name;
      c_value->length = strlen (spc_tuple->type_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 5:
      /* column size, SQL_C_LONG */
      c_value->value.l = spc_tuple->column_size;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 6:
      /* buffer length, SQL_C_LONG */
      c_value->value.l = spc_tuple->buffer_length;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 7:
      /* decimal digits, SQL_C_SHORT */
      c_value->value.s = spc_tuple->decimal_digits;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 8:
      /* pseudo column, SQL_C_SHORT */
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

PRIVATE RETCODE
odbc_get_primary_keys_data (ODBC_STATEMENT * stmt,
			    short col_index, VALUE_CONTAINER * c_value)
{
  ODBC_PRIMARY_KEYS_VALUE *primary_keys_tuple;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;

  primary_keys_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:
      /* catalog_name, SQL_C_CHAR */
    case 2:
      /* schema_name, SQL_C_CHAR */
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 3:
      /* table_name, SQL_C_CHAR */
      c_value->value.str = primary_keys_tuple->table_name;
      c_value->length = strlen (primary_keys_tuple->table_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 4:
      /* column_name, SQL_C_CHAR */
      c_value->value.str = primary_keys_tuple->column_name;
      c_value->length = strlen (primary_keys_tuple->column_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 5:
      /* key sequence number */
      c_value->value.s = primary_keys_tuple->key_sequence;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 6:
      /* key name, SQL_C_CHAR */
      c_value->value.str = primary_keys_tuple->key_name;
      c_value->length = strlen (primary_keys_tuple->key_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    default:
      odbc_set_diag (stmt->diag, "07009", 0, NULL);
      return ODBC_ERROR;
    }

  return ODBC_SUCCESS;
}

PRIVATE RETCODE
odbc_get_foreign_keys_data (ODBC_STATEMENT * stmt, short col_index,
			    VALUE_CONTAINER * c_value)
{
  ODBC_FOREIGN_KEYS_VALUE *foreign_keys_tuple;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;

  foreign_keys_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:
      /* pk_catalog_name, SQL_C_CHAR */
      c_value->value.str = stmt->conn->db_name;
      c_value->length = strlen (stmt->conn->db_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 2:
      /* pk_schema_name, SQL_C_CHAR */
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 3:
      /* pk_table_name, SQL_C_CHAR */
      c_value->value.str = foreign_keys_tuple->pk_table_name;
      c_value->length = strlen (foreign_keys_tuple->pk_table_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 4:
      /* pk_column_name, SQL_C_CHAR */
      c_value->value.str = foreign_keys_tuple->pk_column_name;
      c_value->length = strlen (foreign_keys_tuple->pk_column_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 5:
      /* fk_catalog_name, SQL_C_CHAR */
      c_value->value.str = stmt->conn->db_name;
      c_value->length = strlen (stmt->conn->db_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 6:
      /* fk_schema_name, SQL_C_CHAR */
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 7:
      /* fk_table_name, SQL_C_CHAR */
      c_value->value.str = foreign_keys_tuple->fk_table_name;
      c_value->length = strlen (foreign_keys_tuple->fk_table_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 8:
      /* fk_column_name, SQL_C_CHAR */
      c_value->value.str = foreign_keys_tuple->fk_column_name;
      c_value->length = strlen (foreign_keys_tuple->fk_column_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 9:
      /* key sequence number */
      c_value->value.s = foreign_keys_tuple->key_sequence;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 10:
      /* update rule */
      c_value->value.s = foreign_keys_tuple->update_rule;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 11:
      /* delete rule */
      c_value->value.s = foreign_keys_tuple->delete_rule;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 12:
      /* fk name, SQL_C_CHAR */
      c_value->value.str = foreign_keys_tuple->fk_name;
      c_value->length = strlen (foreign_keys_tuple->fk_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 13:
      /* pk name, SQL_C_CHAR */
      c_value->value.str = foreign_keys_tuple->pk_name;
      c_value->length = strlen (foreign_keys_tuple->pk_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 14:
      /* DEFERRABILITY */
      c_value->value.s = SQL_INITIALLY_IMMEDIATE;
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
odbc_get_table_privileges_data (ODBC_STATEMENT * stmt, short col_index,
				VALUE_CONTAINER * c_value)
{
  ODBC_TABLE_PRIVILEGES_VALUE *table_privileges_tuple;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;

  table_privileges_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:
      /* catalog_name, SQL_C_CHAR */
    case 2:
      /* schema_name, SQL_C_CHAR */
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 3:
      /* table_name, SQL_C_CHAR */
      c_value->value.str = table_privileges_tuple->table_name;
      c_value->length = strlen (table_privileges_tuple->table_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 4:
      /* grantor, SQL_C_CHAR */
      c_value->value.str = table_privileges_tuple->grantor;
      c_value->length = strlen (table_privileges_tuple->grantor) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 5:
      /* grantee, SQL_C_CHAR */
      c_value->value.str = table_privileges_tuple->grantee;
      c_value->length = strlen (table_privileges_tuple->grantee) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 6:
      /* privilege, SQL_C_CHAR */
      c_value->value.str = table_privileges_tuple->privilege;
      c_value->length = strlen (table_privileges_tuple->privilege) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 7:
      /* privilege, SQL_C_CHAR */
      c_value->value.str = table_privileges_tuple->is_grantable;
      c_value->length = strlen (table_privileges_tuple->is_grantable) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    default:
      odbc_set_diag (stmt->diag, "07009", 0, NULL);
      return ODBC_ERROR;
    }

  return ODBC_SUCCESS;
}

PRIVATE RETCODE
odbc_get_procedures_data (ODBC_STATEMENT * stmt,
			  short col_index, VALUE_CONTAINER * c_value)
{
  ODBC_PROCEDURES_VALUE *procedures_tuple;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;

  procedures_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:
      /* catalog_name, SQL_C_CHAR */
      c_value->value.str = stmt->conn->db_name;
      c_value->length = strlen (stmt->conn->db_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 2:
      /* schema_name, SQL_C_CHAR */
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 3:
      /* proc_name, SQL_C_CHAR */
      c_value->value.str = procedures_tuple->proc_name;
      c_value->length = strlen (procedures_tuple->proc_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 4:
      /* num_input_params */
    case 5:
      /* num_output_params */
    case 6:
      /* num_result_sets */
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 7:
      /* remarks */
      c_value->value.str = "";
      c_value->length = strlen ("") + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 8:
      /* key sequence number */
      c_value->value.s = procedures_tuple->proc_type;
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
odbc_get_procedure_columns_data (ODBC_STATEMENT * stmt,
				 short col_index, VALUE_CONTAINER * c_value)
{
  ODBC_PROCEDURE_COLUMNS_VALUE *procedure_columns_tuple;

  if (stmt->catalog_result.current == NULL)
    return ODBC_NO_MORE_DATA;

  procedure_columns_tuple = ((ST_LIST *) stmt->catalog_result.current)->value;

  switch (col_index)
    {
    case 1:
      /* catalog_name, SQL_C_CHAR */
      c_value->value.str = stmt->conn->db_name;
      c_value->length = strlen (stmt->conn->db_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 2:
      /* schema_name, SQL_C_CHAR */
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 3:
      /* proc_name, SQL_C_CHAR */
      c_value->value.str = procedure_columns_tuple->proc_name;
      c_value->length = strlen (procedure_columns_tuple->proc_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 4:
      /* column_name */
      c_value->value.str = procedure_columns_tuple->column_name;
      c_value->length = strlen (procedure_columns_tuple->column_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 5:
      /* column_type */
      c_value->value.s = procedure_columns_tuple->column_type;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 6:
      /* data_type */
      c_value->value.s = procedure_columns_tuple->concise_data_type;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 7:
      /* type_name */
      c_value->value.str = procedure_columns_tuple->type_name;
      c_value->length = strlen (procedure_columns_tuple->type_name) + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 8:
      /* column_size */
      c_value->value.l = procedure_columns_tuple->column_size;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 9:
      /* buffer_length */
      c_value->value.l = procedure_columns_tuple->buffer_length;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 10:
      /* decimal_digits */
      c_value->value.s = procedure_columns_tuple->decimal_digits;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 11:
      /* num_prec_radix */
      c_value->value.s = procedure_columns_tuple->num_prec_radix;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 12:
      /* nullable */
      c_value->value.s = procedure_columns_tuple->nullable;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 13:
      /* remarks */
      c_value->value.str = "";
      c_value->length = strlen ("") + 1;
      c_value->type = SQL_C_CHAR;
      break;
    case 14:
      /* column default value */
      c_value->value.str = NULL;
      c_value->type = SQL_C_CHAR;
      c_value->length = 0;
      break;
    case 15:
      /* verbose type */
      c_value->value.s = procedure_columns_tuple->verbose_data_type;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 16:
      /* data/time subcode type */
      c_value->value.s = procedure_columns_tuple->subcode;
      c_value->length = sizeof (short);
      c_value->type = SQL_C_SHORT;
      break;
    case 17:
      /* octet_length */
      c_value->value.l = procedure_columns_tuple->octet_length;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 18:
      /* octet_length */
      c_value->value.l = procedure_columns_tuple->ordinal_position;
      c_value->length = sizeof (long);
      c_value->type = SQL_C_LONG;
      break;
    case 19:
      /* is_nullable */
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

PRIVATE ODBC_TABLE_VALUE *
create_table_value (void)
{
  ODBC_TABLE_VALUE *value = NULL;

  value = (ODBC_TABLE_VALUE *) UT_ALLOC (sizeof (ODBC_TABLE_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_TABLE_VALUE));

  return value;
}

PRIVATE void
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
	      goto error;
	    }
	}

      // fetch data
      rc = fetch_tuple (req_handle, stmt->diag, 0);
      if (rc < 0)
	{
	  goto error;
	}

      // get class type data
      cci_rc = cci_get_data (req_handle, 2, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
	  goto error;
	}

      if ((cci_value.i == 0 &&	// 0 system class
	   !IS_OPTION_SETTED (type_option, TABLE_TYPE_SYSTEM)) || (cci_value.i == 1 &&	// 1 virtual class
								   !IS_OPTION_SETTED (type_option, TABLE_TYPE_VIEW)) || (cci_value.i == 2 &&	// 2 class 
															 !IS_OPTION_SETTED
															 (type_option,
															  TABLE_TYPE_TABLE)))
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

PRIVATE ODBC_COLUMN_VALUE *
create_column_value (void)
{
  ODBC_COLUMN_VALUE *value = NULL;

  value = (ODBC_COLUMN_VALUE *) UT_ALLOC (sizeof (ODBC_COLUMN_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_COLUMN_VALUE));

  return value;
}

PRIVATE void
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
	      goto error;
	    }
	}

      // fetch data
      rc = fetch_tuple (req_handle, stmt->diag, 0);
      if (rc < 0)
	{
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
	  goto cci_error;
	}
      column_node->table_name = UT_MAKE_STRING (cci_value.str, -1);

      // get column name
      cci_rc = cci_get_data (req_handle, 1, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  goto cci_error;
	}
      column_node->column_name = UT_MAKE_STRING (cci_value.str, -1);


      // get domain
      cci_rc = cci_get_data (req_handle, 2, CCI_A_TYPE_INT,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  goto cci_error;
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
	      goto cci_error;
	    }
	  scale = cci_value.i;

	  // get precision
	  cci_rc = cci_get_data (req_handle, 4, CCI_A_TYPE_INT,
				 &cci_value, &cci_ind);
	  if (cci_rc < 0)
	    {
	      goto cci_error;
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
	  goto cci_error;
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
	  goto cci_error;
	}
      column_node->ordinal_position = cci_value.i;

      // get default value
      cci_rc = cci_get_data (req_handle, 9, CCI_A_TYPE_STR,
			     &cci_value, &cci_ind);
      if (cci_rc < 0)
	{
	  goto cci_error;
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
cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_rc, NULL);
error:
  NC_FREE_WH (free_column_value, column_node);

  // close cursor or free catalog result
  return ODBC_ERROR;
}

PRIVATE ODBC_STAT_VALUE *
create_stat_value (void)
{
  ODBC_STAT_VALUE *value = NULL;

  value = (ODBC_STAT_VALUE *) UT_ALLOC (sizeof (ODBC_STAT_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_STAT_VALUE));

  return value;
}

PRIVATE void
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

PRIVATE RETCODE
make_stat_result_set (ODBC_STATEMENT * stmt, int req_handle, char *table_name,
		      unsigned short unique)
{
  int cci_ind;
  int retval;
  int cci_retval;
  UNI_CCI_A_TYPE cci_value;

  long current_tpl_pos = 0;
  ODBC_STAT_VALUE *stat_node = NULL;

  while (1)
    {
      if ((retval =
	   move_cursor (req_handle, &current_tpl_pos, stmt->diag)) < 0)
	{
	  if (retval == ODBC_NO_MORE_DATA)
	    {
	      break;
	    }
	  else
	    {
	      goto error;
	    }
	}

      if (fetch_tuple (req_handle, stmt->diag, 0) < 0)
	{
	  goto error;
	}

      /* index type */
      if ((cci_retval =
	   cci_get_data (req_handle, 1, CCI_A_TYPE_INT, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      if (unique == SQL_INDEX_UNIQUE && cci_value.i != 0)
	{
	  continue;
	}

      if ((stat_node = create_stat_value ()) == NULL)
	{
	  continue;
	}

      /* type */
      if (unique == SQL_INDEX_UNIQUE)
	{
	  stat_node->type = SQL_INDEX_UNIQUE;
	}
      else
	{
	  stat_node->type = SQL_INDEX_OTHER;
	}

      /* non_unique */
      if (cci_value.i == 1)
	{
	  stat_node->non_unique = SQL_TRUE;
	}
      else
	{
	  stat_node->non_unique = SQL_FALSE;
	}

      /* table name */
      stat_node->table_name = UT_MAKE_STRING (table_name, -1);

      /* index name */
      if ((cci_retval =
	   cci_get_data (req_handle, 2, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      stat_node->index_name = UT_MAKE_STRING (cci_value.str, -1);

      /* column name */
      if ((cci_retval =
	   cci_get_data (req_handle, 3, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      stat_node->column_name = UT_MAKE_STRING (cci_value.str, -1);

      /* num pages */
      if ((cci_retval =
	   cci_get_data (req_handle, 4, CCI_A_TYPE_INT, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      stat_node->pages = cci_value.i;

      /* num keys */
      if ((cci_retval =
	   cci_get_data (req_handle, 5, CCI_A_TYPE_INT, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      stat_node->cardinality = cci_value.i;

      /* key order */
      if ((cci_retval =
	   cci_get_data (req_handle, 7, CCI_A_TYPE_INT, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      stat_node->ordinal_position = cci_value.i;

      /* asc_or_desc */
      if ((cci_retval =
	   cci_get_data (req_handle, 8, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      stat_node->asc_or_desc = UT_MAKE_STRING (cci_value.str, -1);

      ListTailAdd (stmt->catalog_result.value, NULL, stat_node, NodeAssign);
      ++stmt->tpl_number;
      stat_node = NULL;
    }

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, NULL);
error:
  NC_FREE_WH (free_stat_value, stat_node);

  return ODBC_ERROR;
}

PRIVATE ODBC_SP_COLUMN_VALUE *
create_sp_column_value (void)
{
  ODBC_SP_COLUMN_VALUE *value = NULL;

  value = (ODBC_SP_COLUMN_VALUE *) UT_ALLOC (sizeof (ODBC_SP_COLUMN_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_SP_COLUMN_VALUE));

  return value;
}

PRIVATE void
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
make_sp_column_result_set (ODBC_STATEMENT * stmt, int req_handle,
			   char *table_name)
{
  int retval;
  int cci_retval;

  int cci_req;
  T_CCI_ERROR cci_error;

  int cci_ind;
  UNI_CCI_A_TYPE cci_value;

  long param_num = 0;
  long current_tpl_pos = 0, cur_tpl_pos = 0;
  ODBC_SP_COLUMN_VALUE *special_columns_node = NULL;

  int cci_u_type, precision, scale;
  short odbc_type;

  while (1)
    {
      if ((retval =
	   move_cursor (req_handle, &current_tpl_pos, stmt->diag)) < 0)
	{
	  if (retval == ODBC_NO_MORE_DATA)
	    {
	      break;
	    }
	  else
	    {
	      goto error;
	    }
	}

      if (fetch_tuple (req_handle, stmt->diag, 0) < 0)
	{
	  goto error;
	}

      /* constraint type:  0 unique */
      if ((cci_retval =
	   cci_get_data (req_handle, 1, CCI_A_TYPE_INT, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      if (cci_value.i != 0)
	{
	  continue;
	}

      if ((special_columns_node = create_sp_column_value ()) == NULL)
	{
	  continue;
	}

      /* column name */
      if ((cci_retval =
	   cci_get_data (req_handle, 3, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      special_columns_node->column_name = UT_MAKE_STRING (cci_value.str, -1);

      /* get column attribute info */
      if ((cci_req =
	   cci_schema_info (stmt->conn->connhd, CCI_SCH_ATTRIBUTE, table_name,
			    special_columns_node->column_name, 0,
			    &cci_error)) < 0)
	{
	  odbc_set_diag_by_cci (stmt->diag, cci_retval, &cci_error);
	  goto error;
	}

      while (1)
	{
	  if ((retval = move_cursor (cci_req, &cur_tpl_pos, stmt->diag)) < 0)
	    {
	      if (retval == ODBC_NO_MORE_DATA)
		{
		  break;
		}
	      else
		{
		  goto error;
		}
	    }

	  if (fetch_tuple (cci_req, stmt->diag, 0) < 0)
	    {
	      goto error;
	    }

	  if ((cci_retval =
	       cci_get_data (cci_req, 2, CCI_A_TYPE_INT, &cci_u_type,
			     &cci_ind)) < 0)
	    {
	      goto cci_error;
	    }

	  if ((cci_retval =
	       cci_get_data (cci_req, 3, CCI_A_TYPE_INT, &scale,
			     &cci_ind)) < 0)
	    {
	      goto cci_error;
	    }

	  if ((cci_retval =
	       cci_get_data (cci_req, 4, CCI_A_TYPE_INT, &precision,
			     &cci_ind)) < 0)
	    {
	      goto cci_error;
	    }
	}

      cci_close_req_handle (cci_req);

      odbc_type = odbc_type_by_cci (cci_u_type, precision);

      /* sql data type */
      special_columns_node->concise_data_type = odbc_type;

      /* type name */
      special_columns_node->type_name =
	UT_MAKE_STRING (odbc_type_name (odbc_type), -1);

      /* column size */
      special_columns_node->column_size =
	odbc_column_size (odbc_type, precision);

      /* decimal digits */
      special_columns_node->decimal_digits =
	odbc_decimal_digits (odbc_type, scale);

      /* buffer length */
      special_columns_node->buffer_length =
	odbc_buffer_length (odbc_type, precision);

      ListTailAdd (stmt->catalog_result.value, NULL, special_columns_node,
		   NodeAssign);
      ++stmt->tpl_number;
      special_columns_node = NULL;
    }

  return ODBC_SUCCESS;

cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, NULL);
error:
  NC_FREE_WH (free_sp_column_value, special_columns_node);

  return ODBC_ERROR;
}

PRIVATE ODBC_PRIMARY_KEYS_VALUE *
create_primary_keys_value (void)
{
  ODBC_PRIMARY_KEYS_VALUE *value = NULL;

  if ((value =
       (ODBC_PRIMARY_KEYS_VALUE *)
       UT_ALLOC (sizeof (ODBC_PRIMARY_KEYS_VALUE))) == NULL)
    {
      return NULL;
    }

  memset (value, 0, sizeof (ODBC_PRIMARY_KEYS_VALUE));

  return value;
}

PRIVATE void
free_primary_keys_value (ODBC_PRIMARY_KEYS_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->table_name);
      NC_FREE (value->column_name);
      NC_FREE (value->key_name);

      UT_FREE (value);
    }
}

PRIVATE void
free_primary_keys_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_primary_keys_value ((ODBC_PRIMARY_KEYS_VALUE *) (node->value));
      if (node->key != NULL)
	{
	  UT_FREE (node->key);
	}
    }
}

PRIVATE RETCODE
make_primary_keys_result_set (ODBC_STATEMENT * stmt, int req_handle)
{
  int cci_ind;
  int retval;
  int cci_retval;
  UNI_CCI_A_TYPE cci_value;

  long current_tpl_pos = 0;
  ODBC_PRIMARY_KEYS_VALUE *primary_keys_node = NULL;

  while (1)
    {
      if ((retval =
	   move_cursor (req_handle, &current_tpl_pos, stmt->diag)) < 0)
	{
	  if (retval == ODBC_NO_MORE_DATA)
	    {
	      break;
	    }
	  else
	    {
	      goto error;
	    }
	}

      if (fetch_tuple (req_handle, stmt->diag, 0) < 0)
	{
	  goto error;
	}

      if ((primary_keys_node = create_primary_keys_value ()) == NULL)
	{
	  continue;
	}

      /* class name */
      if ((cci_retval = cci_get_data (req_handle, 1, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      primary_keys_node->table_name = UT_MAKE_STRING (cci_value.str, -1);

      /* column name */
      if ((cci_retval = cci_get_data (req_handle, 2, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      primary_keys_node->column_name = UT_MAKE_STRING (cci_value.str, -1);

      /* key sequence number */
      if ((cci_retval = cci_get_data (req_handle, 3, CCI_A_TYPE_INT,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      primary_keys_node->key_sequence = cci_value.i;

      /* primary key name */
      if ((cci_retval = cci_get_data (req_handle, 4, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      primary_keys_node->key_name = UT_MAKE_STRING (cci_value.str, -1);

      ListTailAdd (stmt->catalog_result.value, NULL, primary_keys_node,
		   NodeAssign);
      ++stmt->tpl_number;
    }

  return ODBC_SUCCESS;
cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, NULL);
error:
  NC_FREE_WH (free_primary_keys_value, primary_keys_node);

  return ODBC_ERROR;
}

PRIVATE ODBC_FOREIGN_KEYS_VALUE *
create_foreign_keys_value (void)
{
  ODBC_FOREIGN_KEYS_VALUE *value = NULL;

  if ((value =
       (ODBC_FOREIGN_KEYS_VALUE *)
       UT_ALLOC (sizeof (ODBC_FOREIGN_KEYS_VALUE))) == NULL)
    {
      return NULL;
    }

  memset (value, 0, sizeof (ODBC_FOREIGN_KEYS_VALUE));

  return value;
}

PRIVATE void
free_foreign_keys_value (ODBC_FOREIGN_KEYS_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->pk_table_name);
      NC_FREE (value->pk_column_name);
      NC_FREE (value->fk_table_name);
      NC_FREE (value->fk_column_name);
      NC_FREE (value->fk_name);
      NC_FREE (value->pk_name);

      UT_FREE (value);
    }
}

PRIVATE void
free_foreign_keys_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_foreign_keys_value ((ODBC_FOREIGN_KEYS_VALUE *) (node->value));
      if (node->key != NULL)
	{
	  UT_FREE (node->key);
	}
    }
}

PRIVATE RETCODE
make_foreign_keys_result_set (ODBC_STATEMENT * stmt, int req_handle)
{
  int cci_ind;
  int retval;
  int cci_retval;
  UNI_CCI_A_TYPE cci_value;

  long current_tpl_pos = 0;
  ODBC_FOREIGN_KEYS_VALUE *foreign_keys_node = NULL;

  while (1)
    {
      if ((retval =
	   move_cursor (req_handle, &current_tpl_pos, stmt->diag)) < 0)
	{
	  if (retval == ODBC_NO_MORE_DATA)
	    {
	      break;
	    }
	  else
	    {
	      goto error;
	    }
	}

      if (fetch_tuple (req_handle, stmt->diag, 0) < 0)
	{
	  goto error;
	}

      if ((foreign_keys_node = create_foreign_keys_value ()) == NULL)
	{
	  continue;
	}

      /* pk class name */
      if ((cci_retval =
	   cci_get_data (req_handle, 1, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      foreign_keys_node->pk_table_name = UT_MAKE_STRING (cci_value.str, -1);

      /* pk column name */
      if ((cci_retval =
	   cci_get_data (req_handle, 2, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      foreign_keys_node->pk_column_name = UT_MAKE_STRING (cci_value.str, -1);

      /* fk class name */
      if ((cci_retval =
	   cci_get_data (req_handle, 3, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      foreign_keys_node->fk_table_name = UT_MAKE_STRING (cci_value.str, -1);

      /* fk column name */
      if ((cci_retval =
	   cci_get_data (req_handle, 4, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      foreign_keys_node->fk_column_name = UT_MAKE_STRING (cci_value.str, -1);

      /* key sequence number */
      if ((cci_retval =
	   cci_get_data (req_handle, 5, CCI_A_TYPE_INT, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      foreign_keys_node->key_sequence = cci_value.i;

      /* update rule */
      if ((cci_retval =
	   cci_get_data (req_handle, 6, CCI_A_TYPE_INT, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      foreign_keys_node->update_rule = cci_value.i;

      /* delete rule */
      if ((cci_retval =
	   cci_get_data (req_handle, 7, CCI_A_TYPE_INT, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      foreign_keys_node->delete_rule = cci_value.i;

      /* foreign key name */
      if ((cci_retval =
	   cci_get_data (req_handle, 8, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      foreign_keys_node->fk_name = UT_MAKE_STRING (cci_value.str, -1);

      /* primary key name */
      if ((cci_retval =
	   cci_get_data (req_handle, 9, CCI_A_TYPE_STR, &cci_value,
			 &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      foreign_keys_node->pk_name = UT_MAKE_STRING (cci_value.str, -1);

      ListTailAdd (stmt->catalog_result.value, NULL, foreign_keys_node,
		   NodeAssign);
      ++stmt->tpl_number;
    }

  return ODBC_SUCCESS;
cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, NULL);
error:
  NC_FREE_WH (free_foreign_keys_value, foreign_keys_node);

  return ODBC_ERROR;
}

PRIVATE ODBC_TABLE_PRIVILEGES_VALUE *
create_table_privileges_value (void)
{
  ODBC_TABLE_PRIVILEGES_VALUE *value = NULL;

  if ((value =
       (ODBC_TABLE_PRIVILEGES_VALUE *)
       UT_ALLOC (sizeof (ODBC_TABLE_PRIVILEGES_VALUE))) == NULL)
    {
      return NULL;
    }

  memset (value, 0, sizeof (ODBC_TABLE_PRIVILEGES_VALUE));

  return value;
}

PRIVATE void
free_table_privileges_value (ODBC_TABLE_PRIVILEGES_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->table_name);
      NC_FREE (value->grantor);
      NC_FREE (value->grantee);
      NC_FREE (value->privilege);
      NC_FREE (value->is_grantable);

      UT_FREE (value);
    }
}

PRIVATE void
free_table_privileges_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_table_privileges_value ((ODBC_TABLE_PRIVILEGES_VALUE *) (node->
								    value));
      if (node->key != NULL)
	{
	  UT_FREE (node->key);
	}
    }
}

PRIVATE RETCODE
make_table_privileges_result_set (ODBC_STATEMENT * stmt, int req_handle)
{
  int cci_ind;
  int retval;
  int cci_retval;
  UNI_CCI_A_TYPE cci_value;

  long current_tpl_pos = 0;
  ODBC_TABLE_PRIVILEGES_VALUE *table_privileges_node = NULL;

  while (1)
    {
      if ((retval =
	   move_cursor (req_handle, &current_tpl_pos, stmt->diag)) < 0)
	{
	  if (retval == ODBC_NO_MORE_DATA)
	    {
	      break;
	    }
	  else
	    {
	      goto error;
	    }
	}

      if (fetch_tuple (req_handle, stmt->diag, 0) < 0)
	{
	  goto error;
	}

      if ((table_privileges_node = create_table_privileges_value ()) == NULL)
	{
	  continue;
	}

      /* class name */
      if ((cci_retval = cci_get_data (req_handle, 1, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      table_privileges_node->table_name = UT_MAKE_STRING (cci_value.str, -1);

      /* grantor */
      if ((cci_retval = cci_get_data (req_handle, 2, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      table_privileges_node->grantor = UT_MAKE_STRING (cci_value.str, -1);

      /* grantee */
      if ((cci_retval = cci_get_data (req_handle, 3, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      table_privileges_node->grantee = UT_MAKE_STRING (cci_value.str, -1);

      /* privilege */
      if ((cci_retval = cci_get_data (req_handle, 4, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      table_privileges_node->privilege = UT_MAKE_STRING (cci_value.str, -1);

      /* is_grantable */
      if ((cci_retval = cci_get_data (req_handle, 5, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      table_privileges_node->is_grantable =
	UT_MAKE_STRING (cci_value.str, -1);

      ListTailAdd (stmt->catalog_result.value, NULL, table_privileges_node,
		   NodeAssign);
      ++stmt->tpl_number;
    }

  return ODBC_SUCCESS;
cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, NULL);
error:
  NC_FREE_WH (free_table_privileges_value, table_privileges_node);

  return ODBC_ERROR;
}

PRIVATE ODBC_PROCEDURES_VALUE *
create_procedures_value (void)
{
  ODBC_PROCEDURES_VALUE *value = NULL;

  if ((value =
       (ODBC_PROCEDURES_VALUE *) UT_ALLOC (sizeof (ODBC_PROCEDURES_VALUE)))
      == NULL)
    {
      return NULL;
    }

  memset (value, 0, sizeof (ODBC_PROCEDURES_VALUE));

  return value;
}

PRIVATE void
free_procedures_value (ODBC_PROCEDURES_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->proc_name);

      UT_FREE (value);
    }
}

PRIVATE void
free_procedures_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_procedures_value ((ODBC_PROCEDURES_VALUE *) (node->value));
      if (node->key != NULL)
	{
	  UT_FREE (node->key);
	}
    }
}

PRIVATE RETCODE
make_procedures_result_set (ODBC_STATEMENT * stmt, int req_handle)
{
  int cci_ind;
  int retval;
  int cci_retval;
  UNI_CCI_A_TYPE cci_value;

  long current_tpl_pos = 0;
  ODBC_PROCEDURES_VALUE *procedures_node = NULL;

  while (1)
    {
      if ((retval =
	   move_cursor (req_handle, &current_tpl_pos, stmt->diag)) < 0)
	{
	  if (retval == ODBC_NO_MORE_DATA)
	    {
	      break;
	    }
	  else
	    {
	      goto error;
	    }
	}

      if (fetch_tuple (req_handle, stmt->diag, 0) < 0)
	{
	  goto error;
	}

      if ((procedures_node = create_procedures_value ()) == NULL)
	{
	  continue;
	}

      /* procedure name */
      if ((cci_retval = cci_get_data (req_handle, 1, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      procedures_node->proc_name = UT_MAKE_STRING (cci_value.str, -1);

      /* procedure type: function or procedure */
      if ((cci_retval = cci_get_data (req_handle, 2, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      if (strcmp (cci_value.str, "FUNCTION") == 0)
	{
	  procedures_node->proc_type = SQL_PT_FUNCTION;
	}
      else if (strcmp (cci_value.str, "PROCEDURE") == 0)
	{
	  procedures_node->proc_type = SQL_PT_PROCEDURE;
	}
      else
	{
	  procedures_node->proc_type = SQL_PT_UNKNOWN;
	}

      ListTailAdd (stmt->catalog_result.value, NULL, procedures_node,
		   NodeAssign);
      ++stmt->tpl_number;
    }

  return ODBC_SUCCESS;
cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, NULL);
error:
  NC_FREE_WH (free_procedures_value, procedures_node);

  return ODBC_ERROR;
}

PRIVATE ODBC_PROCEDURE_COLUMNS_VALUE *
create_procedure_columns_value (void)
{
  ODBC_PROCEDURE_COLUMNS_VALUE *value = NULL;

  value =
    (ODBC_PROCEDURE_COLUMNS_VALUE *)
    UT_ALLOC (sizeof (ODBC_PROCEDURE_COLUMNS_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_PROCEDURE_COLUMNS_VALUE));

  return value;
}

PRIVATE void
free_procedure_columns_value (ODBC_PROCEDURE_COLUMNS_VALUE * value)
{
  if (value != NULL)
    {
      NC_FREE (value->proc_name);
      NC_FREE (value->column_name);
      NC_FREE (value->type_name);
      NC_FREE (value->default_value);

      UT_FREE (value);
    }
}

PRIVATE void
free_procedure_columns_node (ST_LIST * node)
{
  if (node != NULL)
    {
      free_procedure_columns_value ((ODBC_PROCEDURE_COLUMNS_VALUE *) (node->
								      value));
      if (node->key != NULL)
	UT_FREE (node->key);
    }
}

PRIVATE RETCODE
make_procedure_columns_result_set (ODBC_STATEMENT * stmt, int req_handle)
{
  int retval;
  int cci_retval;

  int cci_ind;
  UNI_CCI_A_TYPE cci_value;

  long param_num = 0;
  long current_tpl_pos = 0;
  ODBC_PROCEDURE_COLUMNS_VALUE *procedure_columns_node = NULL;
  ODBC_DATA_TYPE_INFO param_type_info;

  while (1)
    {
      if ((retval =
	   move_cursor (req_handle, &current_tpl_pos, stmt->diag)) < 0)
	{
	  if (retval == ODBC_NO_MORE_DATA)
	    {
	      break;
	    }
	  else
	    {
	      goto error;
	    }
	}

      if (fetch_tuple (req_handle, stmt->diag, 0) < 0)
	{
	  goto error;
	}

      if ((procedure_columns_node =
	   create_procedure_columns_value ()) == NULL)
	{
	  continue;
	}

      /* procedure name */
      if ((cci_retval = cci_get_data (req_handle, 1, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      procedure_columns_node->proc_name = UT_MAKE_STRING (cci_value.str, -1);

      /* argument name */
      if ((cci_retval = cci_get_data (req_handle, 2, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      procedure_columns_node->column_name =
	UT_MAKE_STRING (cci_value.str, -1);

      /* argument type */
      if ((cci_retval = cci_get_data (req_handle, 3, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}

      if (strcmp (cci_value.str, "IN") == 0)
	{
	  procedure_columns_node->column_type = SQL_PARAM_INPUT;
	}
      else if (strcmp (cci_value.str, "OUT") == 0)
	{
	  procedure_columns_node->column_type = SQL_PARAM_OUTPUT;
	}
      else if (strcmp (cci_value.str, "INOUT") == 0)
	{
	  procedure_columns_node->column_type = SQL_PARAM_INPUT_OUTPUT;
	}
      else
	{
	  procedure_columns_node->column_type = SQL_PARAM_TYPE_UNKNOWN;
	}

      /* type name */
      if ((cci_retval = cci_get_data (req_handle, 4, CCI_A_TYPE_STR,
				      &cci_value, &cci_ind)) < 0)
	{
	  goto cci_error;
	}
      procedure_columns_node->type_name = UT_MAKE_STRING (cci_value.str, -1);

      if ((retval =
	   odbc_type_default_info_by_name (procedure_columns_node->type_name,
					   &param_type_info)) < 0)
	{
	  /* ODBC doesn't support this type, just ignore it. */
	  free_procedure_columns_value (procedure_columns_node);
	  continue;
	}

      param_num++;

      /* sql data type */
      procedure_columns_node->concise_data_type =
	param_type_info.concise_sql_type;

      /* verbose type */
      if (odbc_is_valid_sql_date_type
	  (procedure_columns_node->concise_data_type)
	  || odbc_is_valid_sql_interval_type (procedure_columns_node->
					      concise_data_type))
	{
	  procedure_columns_node->verbose_data_type =
	    odbc_concise_to_verbose_type (procedure_columns_node->
					  concise_data_type);

	  /* sql date/time subcode */
	  procedure_columns_node->subcode =
	    odbc_subcode_type (procedure_columns_node->concise_data_type);
	}
      else
	{
	  procedure_columns_node->verbose_data_type =
	    procedure_columns_node->concise_data_type;
	  procedure_columns_node->subcode = 0;
	}

      /* column size */
      procedure_columns_node->column_size = param_type_info.column_size;

      /* buffer length */
      procedure_columns_node->buffer_length = param_type_info.octet_length;

      /* decimal digits */
      procedure_columns_node->decimal_digits = param_type_info.decimal_digits;

      /* num prec radix */
      procedure_columns_node->num_prec_radix =
	odbc_num_prec_radix (procedure_columns_node->concise_data_type);

      /* nullable */
      procedure_columns_node->nullable = SQL_NULLABLE_UNKNOWN;

      /* column default value */
      procedure_columns_node->default_value = NULL;

      /* octet length */
      procedure_columns_node->octet_length = param_type_info.octet_length;

      /* position */
      procedure_columns_node->ordinal_position = param_num;

      ListTailAdd (stmt->catalog_result.value, NULL, procedure_columns_node,
		   NodeAssign);
      ++stmt->tpl_number;
    }

  return ODBC_SUCCESS;
cci_error:
  odbc_set_diag_by_cci (stmt->diag, cci_retval, NULL);
error:
  NC_FREE_WH (free_procedure_columns_value, procedure_columns_node);

  return ODBC_ERROR;
}

PRIVATE ODBC_TYPE_INFO_VALUE *
create_type_info_value (void)
{
  ODBC_TYPE_INFO_VALUE *value = NULL;

  value = (ODBC_TYPE_INFO_VALUE *) UT_ALLOC (sizeof (ODBC_TYPE_INFO_VALUE));
  if (value == NULL)
    return NULL;

  memset (value, 0, sizeof (ODBC_TYPE_INFO_VALUE));

  return value;
}

PRIVATE void
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

PUBLIC void
free_catalog_result (ST_LIST * result, RESULT_TYPE type)
{
  void (*nodeDelete) (ST_LIST *) = NULL;

  if (result != NULL)
    {
      switch (type)
	{
	case TABLES:
	  nodeDelete = free_table_node;
	  break;
	case COLUMNS:
	  nodeDelete = free_column_node;
	  break;
	case SPECIALCOLUMNS:
	  nodeDelete = free_sp_column_node;
	  break;
	case STATISTICS:
	  nodeDelete = free_stat_node;
	  break;
	case TYPE_INFO:
	  nodeDelete = free_type_info_node;
	  break;
	case PRIMARY_KEYS:
	  nodeDelete = free_primary_keys_node;
	  break;
	case FOREIGN_KEYS:
	  nodeDelete = free_foreign_keys_node;
	  break;
	case TABLE_PRIVILEGES:
	  nodeDelete = free_table_privileges_node;
	  break;
	case PROCEDURES:
	  nodeDelete = free_procedures_node;
	  break;
	case PROCEDURE_COLUMNS:
	  nodeDelete = free_procedure_columns_node;
	  break;
	default:
	  nodeDelete = NULL;
	  break;
	}

      ListDelete (result, nodeDelete);
    }
}

PRIVATE int
retrieve_table_from_db_class (int cci_connection, char *table_name, T_CCI_ERROR *error)
{
  int cci_request;

  char *sql_statment = "SELECT class_name FROM db_class WHERE class_name = ?";
  char *param_list[] = { table_name };

  return sql_execute (cci_connection, &cci_request, sql_statment, param_list,
		      1, error);
}

PRIVATE int
schema_info_table_privileges (int cci_connection, int *cci_request,
			      char *table_name, T_CCI_ERROR *error)
{
  char *sql_statment =
    "SELECT "
    "class_name, grantor_name, grantee_name, auth_type, is_grantable "
    "FROM " "db_auth " "WHERE " "class_name = ?";

  char *param_list[] = { table_name };

  return sql_execute (cci_connection, cci_request, sql_statment, param_list,
		      1, error);
}

PRIVATE int
schema_info_procedures (int cci_connection, int *cci_request, char *proc_name, T_CCI_ERROR *error)
{
  char *sql_statment;
  char *param_list[1] = { NULL };
  int param_num;

  if (proc_name == NULL)
    {
      sql_statment = "SELECT sp_name, sp_type FROM db_stored_procedure";

      param_num = 0;
    }
  else
    {
      sql_statment =
	"SELECT sp_name, sp_type FROM db_stored_procedure WHERE sp_name = ?";

      param_num = 1;
      param_list[0] = proc_name;
    }

  return sql_execute (cci_connection, cci_request, sql_statment, param_list,
		      param_num, error);
}

PRIVATE int
schema_info_procedure_columns (int cci_connection, int *cci_request,
			       char *proc_name, char *column_name, T_CCI_ERROR *error)
{
  char *sql_statment;
  char *param_list[2] = { NULL };
  int param_num;

  if (proc_name != NULL && column_name != NULL)
    {
      sql_statment =
	"SELECT "
	"sp_name, arg_name, mode, data_type "
	"FROM "
	"db_stored_procedure_args " "WHERE " "sp_name = ? AND arg_name = ?";

      param_num = 2;
      param_list[0] = proc_name;
      param_list[1] = column_name;
    }
  else if (proc_name != NULL)
    {
      sql_statment =
	"SELECT "
	"sp_name, arg_name, mode, data_type "
	"FROM " "db_stored_procedure_args " "WHERE " "sp_name = ?";

      param_num = 1;
      param_list[0] = proc_name;
    }
  else if (column_name != NULL)
    {
      sql_statment =
	"SELECT "
	"sp_name, arg_name, mode, data_type "
	"FROM " "db_stored_procedure_args " "WHERE " "arg_name = ?";

      param_num = 1;
      param_list[0] = column_name;
    }
  else
    {
      sql_statment =
	"SELECT "
	"sp_name, arg_name, mode, data_type "
	"FROM " "db_stored_procedure_args";

      param_num = 0;
    }

  return sql_execute (cci_connection, cci_request, sql_statment, param_list,
		      param_num, error);
}

PRIVATE int
sql_execute (int cci_connection, int *cci_request,
	     char *sql_statment, char *param_list[], int param_num, T_CCI_ERROR *error)
{
  int cci_retval;
  int i;

  if (((*cci_request) = cci_prepare (cci_connection, sql_statment,
				     0, error)) < 0)
    {
      goto cci_error;
    }

  if (param_num != 0 && param_list != NULL)
    {
      for (i = 0; i < param_num; i++)
	{
	  if ((cci_retval =
	       cci_bind_param (*cci_request, i + 1, CCI_A_TYPE_STR,
			       param_list[i], CCI_U_TYPE_STRING, 0)) < 0)
	    {
	      goto cci_error;
	    }
	}
    }

  if ((cci_retval = cci_execute (*cci_request, 0, 0, error)) < 0)
    {
      goto cci_error;
    }

  return cci_retval;

cci_error:
  if ((*cci_request) > 0)
    {
      cci_close_req_handle (*cci_request);
    }

  return cci_retval;
}

PRIVATE void
catalog_result_set_init (ODBC_STATEMENT * stmt, RESULT_TYPE task_type)
{
  reset_result_set (stmt);

  stmt->param_number = 0;
  stmt->result_type = task_type;

  ListCreate (&stmt->catalog_result.value);
  stmt->catalog_result.current = NULL;
}

PRIVATE void
catalog_set_ird (ODBC_STATEMENT * stmt, ODBC_COL_INFO * colum_info,
		 int column_num)
{
  int i;

  reset_descriptor (stmt->ird);

  for (i = 1; i <= column_num; ++i)
    {
      odbc_set_ird (stmt, i, colum_info[i - 1].type,
		    "", (char *) colum_info[i - 1].name,
		    colum_info[i - 1].precision, (short) 0,
		    SQL_NULLABLE_UNKNOWN, SQL_ATTR_READONLY);
    }
}

PRIVATE void
err_msg_table_not_exist (char *err_msg, const char *db_name,
			 const char *table_name)
{
  int retval;

  memset (err_msg, 0, SQL_MAX_MESSAGE_LENGTH + 1);
  retval =
    _snprintf (err_msg, SQL_MAX_MESSAGE_LENGTH + 1,
	       "Table '%s.%s' doesn't exist.", db_name, table_name);
  if (retval == (SQL_MAX_MESSAGE_LENGTH + 1) || retval < 0)
    {
      err_msg[SQL_MAX_MESSAGE_LENGTH] = '\0';
    }
}
