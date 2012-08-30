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
#include <math.h>

#include "odbc_portable.h"
#include "sqlext.h"
#include "odbc_type.h"
#include "cas_cci.h"
#include "odbc_number.h"
#include "odbc_util.h"
#include "odbc_diag_record.h"

#define GET_SET_SIZE(x) ((sizeof (x)) / (sizeof ((x)[0])))

typedef int (*OCTET_LEN_FUNC) (int);
PRIVATE int octet_len_char (int precision);
PRIVATE int octet_len_binary (int precision);

typedef int (*DISPLAY_SIZE_FUNC) (int);
PRIVATE int display_size_char (int);
PRIVATE int display_size_decimal (int);
PRIVATE int display_size_binary (int);

typedef int (*COLUMN_SIZE_FUNC) (int);
PRIVATE int column_size_char (int);
PRIVATE int column_size_decimal (int);
PRIVATE int column_size_binary (int);

typedef struct tagDATA_TYPE_INFO
{
  char *type_name;

  int concise_sql_type;
  int concise_c_type;

  int default_decimal_digits;
  int default_column_size;
  int default_octet_length;
  int default_display_size;

  int decimal_digits;
  int column_size;
  COLUMN_SIZE_FUNC get_column_size;
  int octet_length;
  OCTET_LEN_FUNC get_octet_length;
  int display_size;
  DISPLAY_SIZE_FUNC get_display_size;
} DATA_TYPE_INFO;

typedef struct tagDATETIME_TYPE_INFO
{
  int concise_sql_type;
  int concise_c_type;
  int verbose_type;
  int type_subcode;
} DATETIME_TYPE_INFO;

typedef struct tagDATETIME_TYPE_BACKWARD
{
  int current_type;
  int backward_type;
} DATETIME_TYPE_BACKWARD;

typedef struct tagC_DATA_TYPE_INFO
{
  int concise_c_type;
  T_CCI_A_TYPE cci_a_type;

  long c_type_size;
} C_DATA_TYPE_INFO;

PRIVATE DATA_TYPE_INFO odbc_data_type_info_set[] = {
  {"CHAR", SQL_CHAR, SQL_C_CHAR, 0, 1, 1, 1,
   0, -1, column_size_char, -1, octet_len_char, -1, display_size_char},

  {"VARCHAR", SQL_VARCHAR, SQL_C_CHAR,
   0, MAX_CUBRID_CHAR_LEN, MAX_CUBRID_CHAR_LEN, MAX_CUBRID_CHAR_LEN,
   0, -1, column_size_char, -1, octet_len_char, -1, display_size_char},

  {"VARCHAR", SQL_LONGVARCHAR, SQL_C_CHAR,
   0, MAX_CUBRID_CHAR_LEN, MAX_CUBRID_CHAR_LEN, MAX_CUBRID_CHAR_LEN,
   0, -1, column_size_char, -1, octet_len_char, -1, display_size_char},

  {"STRING", SQL_LONGVARCHAR, SQL_C_CHAR,
   0, MAX_CUBRID_CHAR_LEN, MAX_CUBRID_CHAR_LEN, MAX_CUBRID_CHAR_LEN,
   0, 10, NULL, 10, NULL, 10, NULL},

  {"DECIMAL", SQL_DECIMAL, SQL_C_DOUBLE, 0, 15, sizeof (SQL_NUMERIC_STRUCT),
   16,
   0, -1, column_size_decimal, sizeof (SQL_NUMERIC_STRUCT), NULL, -1,
   display_size_decimal}
  ,

  {"NUMERIC", SQL_NUMERIC, SQL_C_DOUBLE, 0, 15, sizeof (SQL_NUMERIC_STRUCT),
   16,
   0, -1, column_size_decimal, sizeof (SQL_NUMERIC_STRUCT), NULL, -1,
   display_size_decimal}
  ,

  {"SMALLINT", SQL_SMALLINT, SQL_C_SHORT, 0, 5, sizeof (short), 6,
   0, 5, NULL, sizeof (short), NULL, 6, NULL},

  {"SMALLINT", SQL_TINYINT, SQL_C_SHORT, 0, 5, sizeof (short), 6,
   0, 5, NULL, sizeof (short), NULL, 6, NULL},

  {"BIT", SQL_BIT, SQL_C_BIT, 0, 1, sizeof (unsigned char), 1,
   0, 1, NULL, sizeof (unsigned char), NULL, 1, NULL},

  {"INTEGER", SQL_INTEGER, SQL_C_LONG, 0, 10, sizeof (long), 11,
   0, 10, NULL, sizeof (long), NULL, 11, NULL},

  {"BIGINT", SQL_BIGINT, SQL_C_SBIGINT, 0, 19, sizeof (__int64), 20,
   0, 19, NULL, sizeof (__int64), NULL, 20, NULL}
  ,

  {"FLOAT", SQL_FLOAT, SQL_C_FLOAT, 0, 14, sizeof (float), 15,
   0, 15, NULL, sizeof (float), NULL, 15, NULL},

  {"REAL", SQL_REAL, SQL_C_FLOAT, 0, 14, sizeof (float), 15,
   0, 15, NULL, sizeof (float), NULL, 15, NULL},

  {"DOUBLE", SQL_DOUBLE, SQL_C_DOUBLE, 0, 28, sizeof (double), 22,
   0, 22, NULL, sizeof (double), NULL, 22, NULL},

  {"BIT", SQL_BINARY, SQL_C_BINARY, 0, 1, 1, 1,
   0, -1, column_size_binary, -1, octet_len_binary, -1, display_size_binary},

  {"BIT VARYING", SQL_VARBINARY, SQL_C_BINARY,
   0, MAX_CUBRID_CHAR_LEN, (MAX_CUBRID_CHAR_LEN / 8) + 1, MAX_CUBRID_CHAR_LEN,
   0, -1, column_size_binary, -1, octet_len_binary, -1, display_size_binary},

  {"BIT VARYING", SQL_LONGVARBINARY, SQL_C_BINARY,
   0, MAX_CUBRID_CHAR_LEN, (MAX_CUBRID_CHAR_LEN / 8) + 1, MAX_CUBRID_CHAR_LEN,
   0, -1, column_size_binary, -1, octet_len_binary, -1, display_size_binary},

  {"DATE", SQL_TYPE_DATE, SQL_TYPE_DATE, 0, 10, sizeof (SQL_DATE_STRUCT), 10,
   0, 10, NULL, sizeof (SQL_DATE_STRUCT), NULL, 10, NULL}
  ,

  {"TIME", SQL_TYPE_TIME, SQL_C_TYPE_TIME, 0, 8, sizeof (SQL_TIME_STRUCT), 11,
   0, 8, NULL, sizeof (SQL_TIME_STRUCT), NULL, 11, NULL}
  ,

  {"TIMESTAMP", SQL_TYPE_TIMESTAMP, SQL_C_TYPE_TIMESTAMP,
   0, 23, sizeof (SQL_TIMESTAMP_STRUCT), 23,
   0, 23, NULL, sizeof (SQL_TIMESTAMP_STRUCT), NULL, 23, NULL}
  ,

  {"GUID", SQL_GUID, SQL_C_GUID, 36, sizeof (SQLGUID), 36, 0,
   0, 36, NULL, sizeof (SQLGUID), NULL, 36, NULL}
  ,

  /* For 2.x backward compatibility */
  {"DATE", SQL_DATE, SQL_C_DATE, 0, 10, sizeof (SQL_DATE_STRUCT), 10,
   0, 10, NULL, sizeof (SQL_DATE_STRUCT), NULL, 10, NULL}
  ,

  {"TIME", SQL_TIME, SQL_C_TIME, 0, 12, sizeof (SQL_TIME_STRUCT), 12,
   0, 12, NULL, sizeof (SQL_TIME_STRUCT), NULL, 12, NULL}
  ,

  {"TIMESTAMP", SQL_TIMESTAMP, SQL_C_TIMESTAMP,
   0, 23, sizeof (SQL_TIMESTAMP_STRUCT), 23,
   0, 23, NULL, sizeof (SQL_TIMESTAMP_STRUCT), NULL, 23, NULL}
  ,

#if 0
  /* CUBRID types */
  {"MONETARY", SQL_UNI_MONETARY, SQL_C_UNI_MONETARY, 0, -1, NULL, -1, NULL,
   -1, NULL}
  ,
  {"OBJECT", SQL_UNI_OBJECT, SQL_C_UNI_OBJECT, 0, -1, NULL, -1, NULL, -1, NULL}
  ,
  {"SET", SQL_UNI_SET, SQL_C_UNI_SET, 0, -1, NULL, -1, NULL, -1, NULL}
  ,
#endif
};

PRIVATE C_DATA_TYPE_INFO c_data_type_info_set[] = {
  {SQL_C_CHAR, CCI_A_TYPE_STR, sizeof (SQLCHAR)}
  ,

  {SQL_C_SSHORT, CCI_A_TYPE_INT, sizeof (SQLSMALLINT)}
  ,
  {SQL_C_USHORT, CCI_A_TYPE_INT, sizeof (SQLUSMALLINT)}
  ,

  {SQL_C_SLONG, CCI_A_TYPE_INT, sizeof (SQLINTEGER)}
  ,
  {SQL_C_ULONG, CCI_A_TYPE_INT, sizeof (SQLUINTEGER)}
  ,

  {SQL_C_BINARY, CCI_A_TYPE_STR, sizeof (SQLCHAR)}
  ,

  {SQL_C_FLOAT, CCI_A_TYPE_FLOAT, sizeof (SQLREAL)}
  ,
  {SQL_C_DOUBLE, CCI_A_TYPE_DOUBLE, sizeof (SQLDOUBLE)}
  ,

  {SQL_C_STINYINT, CCI_A_TYPE_INT, sizeof (SQLSCHAR)}
  ,
  {SQL_C_UTINYINT, CCI_A_TYPE_INT, sizeof (SQLCHAR)}
  ,

  {SQL_C_NUMERIC, CCI_A_TYPE_STR, sizeof (SQL_NUMERIC_STRUCT)}
  ,

  {SQL_C_GUID, CCI_A_TYPE_STR, sizeof (SQLGUID)}
  ,

  {SQL_C_BIT, CCI_A_TYPE_BIT, sizeof (SQLCHAR)}
  ,

  {SQL_C_SBIGINT, CCI_A_TYPE_BIGINT, sizeof (SQLBIGINT)}
  ,
  {SQL_C_UBIGINT, CCI_A_TYPE_BIGINT, sizeof (SQLUBIGINT)}
  ,

  {SQL_C_TYPE_DATE, CCI_A_TYPE_DATE, sizeof (SQL_DATE_STRUCT)}
  ,
  {SQL_C_TYPE_TIME, CCI_A_TYPE_DATE, sizeof (SQL_TIME_STRUCT)}
  ,
  {SQL_C_TYPE_TIMESTAMP, CCI_A_TYPE_DATE, sizeof (SQL_TIMESTAMP_STRUCT)}
  ,
  {SQL_C_UBIGINT, CCI_A_TYPE_DATE, sizeof (SQL_TIMESTAMP_STRUCT)}
  ,

#if 0
  /* CUBRID type */
  {SQL_C_UNI_SET, CCI_A_TYPE_STR, sizeof (SQLCHAR)}
  ,
  {SQL_C_UNI_OBJECT, CCI_A_TYPE_STR, sizeof (SQLCHAR)}
  ,
  {SQL_C_UNI_MONETARY, -1, sizeof (SQLCHAR)}
  ,
#endif

  /* For 2.x backward compatibility */
  {SQL_C_TINYINT, CCI_A_TYPE_INT, sizeof (SQLSCHAR)}
  ,
  {SQL_C_SHORT, CCI_A_TYPE_INT, sizeof (SQLSMALLINT)}
  ,
  {SQL_C_LONG, CCI_A_TYPE_INT, sizeof (SQLINTEGER)}
  ,

  {SQL_C_DATE, CCI_A_TYPE_DATE, sizeof (SQL_DATE_STRUCT)}
  ,
  {SQL_C_TIME, CCI_A_TYPE_DATE, sizeof (SQL_TIME_STRUCT)}
  ,
  {SQL_C_TIMESTAMP, CCI_A_TYPE_DATE, sizeof (SQL_TIMESTAMP_STRUCT)}
  ,
};

PRIVATE DATETIME_TYPE_INFO datetime_date_type_info_set[] = {
  {SQL_TYPE_DATE, SQL_C_TYPE_DATE, SQL_DATETIME, SQL_CODE_DATE}
  ,
  {SQL_TYPE_TIME, SQL_C_TYPE_TIME, SQL_DATETIME, SQL_CODE_TIME}
  ,
  {SQL_TYPE_TIMESTAMP, SQL_C_TYPE_TIMESTAMP, SQL_DATETIME, SQL_CODE_TIMESTAMP}
  ,

  /* For 2.x backward compatibility */
  {SQL_DATE, SQL_C_DATE, SQL_DATETIME, SQL_CODE_DATE}
  ,
  {SQL_TIME, SQL_C_TIME, SQL_DATETIME, SQL_CODE_TIME}
  ,
  {SQL_TIMESTAMP, SQL_C_TIMESTAMP, SQL_DATETIME, SQL_CODE_TIMESTAMP}
  ,
};

PRIVATE DATETIME_TYPE_INFO datetime_internal_type_info_set[] = {
  {SQL_INTERVAL_YEAR, SQL_C_INTERVAL_YEAR, SQL_INTERVAL, SQL_CODE_YEAR}
  ,
  {SQL_INTERVAL_MONTH, SQL_C_INTERVAL_MONTH, SQL_INTERVAL, SQL_CODE_MONTH}
  ,
  {SQL_INTERVAL_DAY, SQL_C_INTERVAL_DAY, SQL_INTERVAL, SQL_CODE_DAY}
  ,
  {SQL_INTERVAL_HOUR, SQL_C_INTERVAL_HOUR, SQL_INTERVAL, SQL_CODE_HOUR}
  ,
  {SQL_INTERVAL_MINUTE, SQL_C_INTERVAL_MINUTE, SQL_INTERVAL, SQL_CODE_MINUTE}
  ,
  {SQL_INTERVAL_SECOND, SQL_C_INTERVAL_SECOND, SQL_INTERVAL, SQL_CODE_SECOND}
  ,

  {SQL_INTERVAL_YEAR_TO_MONTH, SQL_C_INTERVAL_YEAR_TO_MONTH, SQL_INTERVAL,
   SQL_CODE_YEAR_TO_MONTH}
  ,
  {SQL_INTERVAL_DAY_TO_HOUR, SQL_C_INTERVAL_DAY_TO_HOUR, SQL_INTERVAL,
   SQL_CODE_DAY_TO_HOUR}
  ,
  {SQL_INTERVAL_DAY_TO_MINUTE, SQL_C_INTERVAL_DAY_TO_MINUTE, SQL_INTERVAL,
   SQL_CODE_DAY_TO_MINUTE}
  ,
  {SQL_INTERVAL_DAY_TO_SECOND, SQL_C_INTERVAL_DAY_TO_SECOND, SQL_INTERVAL,
   SQL_CODE_DAY_TO_SECOND}
  ,
  {SQL_INTERVAL_HOUR_TO_MINUTE, SQL_C_INTERVAL_HOUR_TO_MINUTE, SQL_INTERVAL,
   SQL_CODE_HOUR_TO_MINUTE}
  ,
  {SQL_INTERVAL_HOUR_TO_SECOND, SQL_C_INTERVAL_HOUR_TO_SECOND, SQL_INTERVAL,
   SQL_CODE_HOUR_TO_SECOND}
  ,
  {SQL_INTERVAL_MINUTE_TO_SECOND, SQL_C_INTERVAL_MINUTE_TO_SECOND,
   SQL_INTERVAL, SQL_CODE_MINUTE_TO_SECOND}
  ,
};

PRIVATE DATETIME_TYPE_BACKWARD datetime_sql_type_backward_set[] = {
  {SQL_TYPE_DATE, SQL_DATE}
  ,
  {SQL_TYPE_TIME, SQL_TIME}
  ,
  {SQL_TYPE_TIMESTAMP, SQL_TIMESTAMP}
  ,
};

PRIVATE DATETIME_TYPE_BACKWARD datetime_c_type_backward_set[] = {
  {SQL_C_TYPE_DATE, SQL_C_DATE}
  ,
  {SQL_C_TYPE_TIME, SQL_C_TIME}
  ,
  {SQL_C_TYPE_TIMESTAMP, SQL_C_TIMESTAMP}
  ,
};

PRIVATE int c_common_type_set[] = {
  SQL_C_CHAR,
  SQL_C_SSHORT,
  SQL_C_USHORT,
  SQL_C_SLONG,
  SQL_C_ULONG,
  SQL_C_FLOAT,
  SQL_C_DOUBLE,
  SQL_C_NUMERIC,
  SQL_C_BIT,
  SQL_C_STINYINT,
  SQL_C_UTINYINT,
  SQL_C_SBIGINT,
  SQL_C_UBIGINT,
  SQL_C_BINARY,
  SQL_C_BOOKMARK,
  SQL_C_VARBOOKMARK,
  SQL_C_DEFAULT,
  SQL_C_UNI_OBJECT,
  SQL_C_UNI_SET,
  SQL_C_GUID,

  /* For 2.x backward compatibility */
  SQL_C_TINYINT,
  SQL_C_SHORT,
  SQL_C_LONG
};

PRIVATE int sql_common_type_set[] = {
  SQL_CHAR,
  SQL_VARCHAR,
  SQL_LONGVARCHAR,
  SQL_WCHAR,
  SQL_WVARCHAR,
  SQL_WLONGVARCHAR,
  SQL_DECIMAL,
  SQL_NUMERIC,
  SQL_SMALLINT,
  SQL_INTEGER,
  SQL_REAL,
  SQL_FLOAT,
  SQL_DOUBLE,
  SQL_BIT,
  SQL_TINYINT,
  SQL_BIGINT,
  SQL_BINARY,
  SQL_VARBINARY,
  SQL_LONGVARBINARY,
  SQL_GUID
};

PRIVATE int odbc_is_valid_concise_type (short odbc_type);
PRIVATE int odbc_is_valid_c_concise_type (short odbc_type);
PRIVATE int odbc_is_valid_sql_concise_type (short odbc_type);

PRIVATE int odbc_is_valid_verbose_type (short odbc_type);
PRIVATE int odbc_is_valid_c_verbose_type (short odbc_type);
PRIVATE int odbc_is_valid_sql_verbose_type (short odbc_type);

PRIVATE int odbc_is_valid_c_common_type (short c_type);
PRIVATE int odbc_is_valid_sql_common_type (short sql_type);

PRIVATE int odbc_is_valid_date_code (short code);
PRIVATE int odbc_is_valid_internal_code (short code);

PRIVATE int seek_in_common_type_set (int *set, int set_size, short id);

PUBLIC int
odbc_is_valid_type (short odbc_type)
{
  return (odbc_is_valid_concise_type (odbc_type) ||
	  odbc_is_valid_verbose_type (odbc_type));
}


PUBLIC int
odbc_is_valid_code (short code)
{
  return (odbc_is_valid_date_code (code) ||
	  odbc_is_valid_internal_code (code));
}

PUBLIC int
odbc_is_valid_date_verbose_type (short odbc_type)
{
  return (odbc_type == SQL_DATETIME);
}

PUBLIC int
odbc_is_valid_interval_verbose_type (short odbc_type)
{
  return (odbc_type == SQL_INTERVAL);
}
PUBLIC int
odbc_is_valid_c_type (short odbc_type)
{
  return (odbc_is_valid_c_common_type (odbc_type) ||
	  odbc_is_valid_c_date_type (odbc_type) ||
	  odbc_is_valid_c_interval_type (odbc_type) ||
	  odbc_is_valid_date_verbose_type (odbc_type) ||
	  odbc_is_valid_interval_verbose_type (odbc_type));
}

PUBLIC int
odbc_is_valid_sql_type (short odbc_type)
{
  return (odbc_is_valid_sql_common_type (odbc_type) ||
	  odbc_is_valid_sql_date_type (odbc_type) ||
	  odbc_is_valid_sql_interval_type (odbc_type) ||
	  odbc_is_valid_date_verbose_type (odbc_type) ||
	  odbc_is_valid_interval_verbose_type (odbc_type));
}

PUBLIC int
odbc_is_valid_c_date_type (short c_type)
{
  int i;
  int type_info_set_size = GET_SET_SIZE (datetime_date_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (datetime_date_type_info_set[i].concise_c_type == c_type)
	{
	  return _TRUE_;
	}
    }

  return _FALSE_;
}

PUBLIC int
odbc_is_valid_sql_date_type (short sql_type)
{
  int i;
  int type_info_set_size = GET_SET_SIZE (datetime_date_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (datetime_date_type_info_set[i].concise_sql_type == sql_type)
	{
	  return _TRUE_;
	}
    }

  return _FALSE_;
}

PUBLIC int
odbc_is_valid_c_interval_type (short c_type)
{
  int i;
  int type_info_set_size = GET_SET_SIZE (datetime_internal_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (datetime_internal_type_info_set[i].concise_c_type == c_type)
	{
	  return _TRUE_;
	}
    }

  return _FALSE_;
}

PUBLIC int
odbc_is_valid_sql_interval_type (short sql_type)
{
  int i;
  int type_info_set_size = GET_SET_SIZE (datetime_internal_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (datetime_internal_type_info_set[i].concise_sql_type == sql_type)
	{
	  return _TRUE_;
	}
    }

  return _FALSE_;
}




PUBLIC short
odbc_concise_to_verbose_type (short type)
{
  int i;
  int date_set_size = GET_SET_SIZE (datetime_date_type_info_set);
  int internal_set_size = GET_SET_SIZE (datetime_internal_type_info_set);

  for (i = 0; i < date_set_size; i++)
    {
      if (datetime_date_type_info_set[i].concise_sql_type == type)
	{
	  return datetime_date_type_info_set[i].verbose_type;
	}
    }

  for (i = 0; i < date_set_size; i++)
    {
      if (datetime_internal_type_info_set[i].concise_sql_type == type)
	{
	  return datetime_internal_type_info_set[i].verbose_type;
	}
    }

  return type;
}

PUBLIC short
odbc_verbose_to_concise_type (short type, short code)
{
  int i;
  int date_set_size = GET_SET_SIZE (datetime_date_type_info_set);

  for (i = 0; i < date_set_size; i++)
    {
      if (datetime_date_type_info_set[i].verbose_type == type)
	{
	  return datetime_date_type_info_set[i].concise_sql_type;
	}
    }

  return type;
}



PUBLIC short
odbc_subcode_type (short type)
{
  int i;
  int date_set_size = GET_SET_SIZE (datetime_date_type_info_set);
  int internal_set_size = GET_SET_SIZE (datetime_internal_type_info_set);

  for (i = 0; i < date_set_size; i++)
    {
      if (datetime_date_type_info_set[i].concise_sql_type == type)
	{
	  return datetime_date_type_info_set[i].type_subcode;
	}
    }

  for (i = 0; i < date_set_size; i++)
    {
      if (datetime_internal_type_info_set[i].concise_sql_type == type)
	{
	  return datetime_internal_type_info_set[i].type_subcode;
	}
    }

  return 0;
}

PUBLIC int
odbc_type_default_info_by_name (char *type_name,
				ODBC_DATA_TYPE_INFO * type_info)
{
  int i;
  int set_size = GET_SET_SIZE (odbc_data_type_info_set);
  DATA_TYPE_INFO type_info_in_set;

  for (i = 0; i < set_size; i++)
    {
      type_info_in_set = odbc_data_type_info_set[i];
      if (strcmp (type_info_in_set.type_name, type_name) == 0)
	{
	  type_info->concise_c_type = type_info_in_set.concise_c_type;
	  type_info->concise_sql_type = type_info_in_set.concise_sql_type;

	  type_info->decimal_digits = type_info_in_set.default_decimal_digits;
	  type_info->column_size = type_info_in_set.default_column_size;
	  type_info->octet_length = type_info_in_set.default_octet_length;
	  type_info->display_size = type_info_in_set.default_display_size;

	  return 0;
	}
    }

  return -1;
}

PUBLIC short
odbc_default_c_type (short odbc_type)
{
  int i;
  int set_size = GET_SET_SIZE (odbc_data_type_info_set);

  for (i = 0; i < set_size; i++)
    {
      if (odbc_data_type_info_set[i].concise_sql_type == odbc_type)
	{
	  return odbc_data_type_info_set[i].concise_c_type;
	}
    }

  return SQL_C_CHAR;
}

PUBLIC long
odbc_size_of_by_type_id (short odbc_type)
{
  int i;
  int set_size = GET_SET_SIZE (c_data_type_info_set);

  for (i = 0; i < set_size; i++)
    {
      if (c_data_type_info_set[i].concise_c_type == odbc_type)
	{
	  return c_data_type_info_set[i].c_type_size;
	}
    }

  return 0;
}

PUBLIC int
odbc_column_size (short odbc_type, int precision)
{
  int i;
  int type_info_set_size;

  if (precision < 0)
    {
      precision = 0;
    }

  type_info_set_size = GET_SET_SIZE (odbc_data_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (odbc_data_type_info_set[i].concise_sql_type == odbc_type)
	{
	  if (odbc_data_type_info_set[i].column_size = -1)
	    {
	      if (odbc_data_type_info_set[i].get_display_size != NULL)
		{
		  return odbc_data_type_info_set[i].
		    get_column_size (precision);
		}
	      else
		{
		  break;
		}
	    }
	  else
	    {
	      return odbc_data_type_info_set[i].column_size;
	    }
	}
    }

  return 0;
}

PUBLIC int
odbc_buffer_length (short odbc_type, int precision)
{
  return odbc_octet_length (odbc_type, precision);
}

PUBLIC int
odbc_decimal_digits (short odbc_type, int scale)
{
  int i;
  int type_info_set_size;

  if (scale < 0)
    {
      scale = 0;
    }

  type_info_set_size = GET_SET_SIZE (odbc_data_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (odbc_data_type_info_set[i].concise_sql_type == odbc_type)
	{
	  return (odbc_data_type_info_set[i].decimal_digits != -1) ?
	    odbc_data_type_info_set[i].decimal_digits : scale;
	}
    }

  return 0;
}

PUBLIC int
odbc_octet_length (short odbc_type, int precision)
{
  int i;
  int type_info_set_size;

  if (precision < 0)
    {
      precision = 0;
    }

  type_info_set_size = GET_SET_SIZE (odbc_data_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (odbc_data_type_info_set[i].concise_sql_type == odbc_type ||
	  odbc_data_type_info_set[i].concise_c_type == odbc_type)
	{
	  if (odbc_data_type_info_set[i].octet_length == -1)
	    {
	      if (odbc_data_type_info_set[i].get_octet_length != NULL)
		{
		  return odbc_data_type_info_set[i].
		    get_octet_length (precision);
		}
	      else
		{
		  break;
		}
	    }
	  else
	    {
	      return odbc_data_type_info_set[i].octet_length;
	    }
	}
    }

  return 0;
}

PUBLIC int
odbc_num_prec_radix (short odbc_type)
{
  if (IS_STRING_TYPE (odbc_type) || IS_BINARY_TYPE (odbc_type))
    {
      return 0;
    }
  else
    {
      return 10;
    }
}

PUBLIC int
odbc_display_size (short odbc_type, int precision)
{
  int i;
  int type_info_set_size;

  if (precision < 0)
    {
      precision = 0;
    }

  type_info_set_size = GET_SET_SIZE (odbc_data_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (odbc_data_type_info_set[i].concise_sql_type == odbc_type)
	{
	  if (odbc_data_type_info_set[i].octet_length == -1)
	    {
	      if (odbc_data_type_info_set[i].get_display_size != NULL)
		{
		  return odbc_data_type_info_set[i].
		    get_display_size (precision);
		}
	      else
		{
		  break;
		}
	    }
	  else
	    {
	      return odbc_data_type_info_set[i].display_size;
	    }
	}
    }

  return 0;
}

PUBLIC short
odbc_type_by_cci (T_CCI_U_TYPE cci_type, int precision)
{
  switch (cci_type)
    {

    case CCI_U_TYPE_CHAR:
      return SQL_CHAR;
    case CCI_U_TYPE_STRING:
    case CCI_U_TYPE_VARNCHAR:
#ifdef DELPHI
      if (precision == MAX_CUBRID_CHAR_LEN)
	{
	  return SQL_LONGVARCHAR;
	}
      else
	{
	  return SQL_VARCHAR;
	}
#endif
      return SQL_VARCHAR;
    case CCI_U_TYPE_NCHAR:
      return SQL_CHAR;
    case CCI_U_TYPE_BIT:
      return SQL_BINARY;
    case CCI_U_TYPE_VARBIT:
      return SQL_VARBINARY;
    case CCI_U_TYPE_NUMERIC:
      return SQL_NUMERIC;
    case CCI_U_TYPE_BIGINT:
      return SQL_BIGINT;
    case CCI_U_TYPE_INT:
      return SQL_INTEGER;
    case CCI_U_TYPE_SHORT:
      return SQL_SMALLINT;
    case CCI_U_TYPE_FLOAT:
      return SQL_FLOAT;
    case CCI_U_TYPE_DOUBLE:
      return SQL_DOUBLE;
    case CCI_U_TYPE_DATE:
      return SQL_TYPE_DATE;
    case CCI_U_TYPE_TIME:
      return SQL_TYPE_TIME;
    case CCI_U_TYPE_TIMESTAMP:
    case CCI_U_TYPE_DATETIME:
      return SQL_TYPE_TIMESTAMP;
    case CCI_U_TYPE_MONETARY:
      return SQL_DOUBLE;
    case CCI_U_TYPE_SET:
    case CCI_U_TYPE_MULTISET:
    case CCI_U_TYPE_SEQUENCE:
      return SQL_VARCHAR;
    case CCI_U_TYPE_OBJECT:
      return SQL_CHAR;

    case CCI_U_TYPE_UNKNOWN:
    default:
      return -1;
    }
}

PUBLIC const char *
odbc_type_name (short odbc_type)
{
  int i;
  int type_info_set_size;

  type_info_set_size = GET_SET_SIZE (odbc_data_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (odbc_data_type_info_set[i].concise_sql_type == odbc_type)
	{
	  return odbc_data_type_info_set[i].type_name;
	}
    }

  return NULL;
}

PUBLIC T_CCI_A_TYPE
odbc_type_to_cci_a_type (short c_type)
{
  int i;
  int set_size = GET_SET_SIZE (c_data_type_info_set);

  for (i = 0; i < set_size; i++)
    {
      if (c_data_type_info_set[i].concise_c_type == c_type)
	{
	  return c_data_type_info_set[i].cci_a_type;
	}
    }

  return -1;
}

PUBLIC T_CCI_U_TYPE
odbc_type_to_cci_u_type (short sql_type)
{
  switch (sql_type)
    {

    case SQL_CHAR:
      return CCI_U_TYPE_CHAR;
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
      return CCI_U_TYPE_STRING;

    case SQL_BINARY:
      return CCI_U_TYPE_BIT;
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
      return CCI_U_TYPE_VARBIT;

    case SQL_DECIMAL:
    case SQL_NUMERIC:
      return CCI_U_TYPE_NUMERIC;

    case SQL_SMALLINT:
    case SQL_INTEGER:
    case SQL_TINYINT:
      return CCI_U_TYPE_INT;

    case SQL_BIGINT:
      return CCI_U_TYPE_BIGINT;

    case SQL_FLOAT:
    case SQL_REAL:
      return CCI_U_TYPE_FLOAT;

    case SQL_DOUBLE:
      return CCI_U_TYPE_DOUBLE;

    case SQL_TYPE_DATE:
    case SQL_DATE:		// for 2.x backward compatibility
      return CCI_U_TYPE_DATE;
    case SQL_TYPE_TIME:
    case SQL_TIME:		// for 2.x backward compatibility
      return CCI_U_TYPE_TIME;
    case SQL_TYPE_TIMESTAMP:
    case SQL_TIMESTAMP:	// for 2.x backward compatibility
      return CCI_U_TYPE_DATETIME;
    default:
      return CCI_U_TYPE_UNKNOWN;
    }
}


/************************************************************************
 * name: odbc_value_to_cci
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 *		내부적으로 memory allocation이 일어나므로, 외부에서 free해줘야 한다.
 *
 *			SQL_C_TYPE	|		T_CCI_A_TYPE
 *		----------------------------------------------
 *		SQL_C_SHORT		|			int
 *		SQL_C_SSHORT	|			int
 *		SQL_C_USRHOT	|			int
 *		SQL_C_STINYINT	|			int
 *		SQL_C_TINYINT	|			int
 *		SQL_C_UTINYINT	|			int
 *		SQL_C_LONG		|			int
 *		SQL_C_SLONG		|			int
 *		SQL_C_ULONG		|			int
 *		SQL_C_SBIGINT	|			int
 *		SQL_C_UBIGINT	|			int
 *		SQL_C_FLOAT		|			float
 *		SQL_C_DOUBLE	|			double
 *		SQL_C_CHAR		|			char*
 *		SQL_C_BINARY	|			T_CCI_BIT
 *		SQL_C_NUMERIC	|			char*
 *		SQL_C_DATE		|			T_CCI_DATE
 *		SQL_C_TIME		|			T_CCI_DATE
 *		SQL_C_TIMESTAMP |			T_CCI_DATE
 *
 ************************************************************************/
PUBLIC void *
odbc_value_to_cci (void *c_value, short c_type, long c_length,
		   short c_precision, short c_scale)
{
  void *value = NULL;

  switch (c_type)
    {

	/*---------------------------------------------------------------
	 *					INTEGRAL TYPE
	 *--------------------------------------------------------------*/
    case SQL_C_SHORT:		// for 2.x backward compatibility
    case SQL_C_SSHORT:
    case SQL_C_USHORT:
      value = UT_ALLOC (sizeof (int));
      *(int *) value = *(short *) c_value;
      break;

    case SQL_C_STINYINT:
    case SQL_C_UTINYINT:
    case SQL_C_TINYINT:	// for 2.x backward compatibility
      value = UT_ALLOC (sizeof (int));
      *(int *) value = *(char *) c_value;
      break;

    case SQL_C_LONG:		// for 2.x backward compatibility
    case SQL_C_SLONG:
    case SQL_C_ULONG:
      value = UT_ALLOC (sizeof (int));
      *(int *) value = *(long *) c_value;
      break;
    case SQL_C_SBIGINT:
    case SQL_C_UBIGINT:
      value = UT_ALLOC (sizeof (__int64));
      *(__int64 *) value = *(__int64 *) c_value;
      break;

	/*---------------------------------------------------------------
	 *					floating point type
	 *--------------------------------------------------------------*/
    case SQL_C_FLOAT:
      value = UT_ALLOC (sizeof (float));
      *(float *) value = *(float *) c_value;
      break;

    case SQL_C_DOUBLE:
      value = UT_ALLOC (sizeof (double));
      *(double *) value = *(double *) c_value;
      break;

	/*---------------------------------------------------------------
	 *					char & binary type
	 *--------------------------------------------------------------*/
    case SQL_C_CHAR:
      value = UT_MAKE_STRING (c_value, c_length);
      break;

    case SQL_C_BINARY:
      value = UT_ALLOC (sizeof (T_CCI_BIT));
      ((T_CCI_BIT *) value)->size = c_length;
      ((T_CCI_BIT *) value)->buf = UT_MAKE_BINARY (c_value, c_length);
      break;

	/*---------------------------------------------------------------
	 *					date & time type
	 *--------------------------------------------------------------*/
    case SQL_C_TYPE_DATE:
    case SQL_C_DATE:		// for 2.x backward compatibility
      value = UT_ALLOC (sizeof (T_CCI_DATE));
      ((T_CCI_DATE *) value)->yr = ((SQL_DATE_STRUCT *) c_value)->year;
      ((T_CCI_DATE *) value)->mon = ((SQL_DATE_STRUCT *) c_value)->month;
      ((T_CCI_DATE *) value)->day = ((SQL_DATE_STRUCT *) c_value)->day;
      break;

    case SQL_C_TYPE_TIME:
    case SQL_C_TIME:		// for 2.x backward compatibility
      value = UT_ALLOC (sizeof (T_CCI_DATE));
      ((T_CCI_DATE *) value)->hh = ((SQL_TIME_STRUCT *) c_value)->hour;
      ((T_CCI_DATE *) value)->mm = ((SQL_TIME_STRUCT *) c_value)->minute;
      ((T_CCI_DATE *) value)->ss = ((SQL_TIME_STRUCT *) c_value)->second;
      break;

    case SQL_C_TYPE_TIMESTAMP:
    case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
      value = UT_ALLOC (sizeof (T_CCI_DATE));
      ((T_CCI_DATE *) value)->yr = ((SQL_TIMESTAMP_STRUCT *) c_value)->year;
      ((T_CCI_DATE *) value)->mon = ((SQL_TIMESTAMP_STRUCT *) c_value)->month;
      ((T_CCI_DATE *) value)->day = ((SQL_TIMESTAMP_STRUCT *) c_value)->day;
      ((T_CCI_DATE *) value)->hh = ((SQL_TIMESTAMP_STRUCT *) c_value)->hour;
      ((T_CCI_DATE *) value)->mm = ((SQL_TIMESTAMP_STRUCT *) c_value)->minute;
      ((T_CCI_DATE *) value)->ss = ((SQL_TIMESTAMP_STRUCT *) c_value)->second;
      break;

	/*---------------------------------------------------------------
	 *					numeric type
	 *--------------------------------------------------------------*/
    case SQL_C_NUMERIC:
      {
	bc_num num1 = NULL, num2 = NULL, base = NULL, res_num =
	  NULL, res_tmp = NULL;
	unsigned char *pt;
	char buf[16];
	short i;

	init_numbers ();

	str2num (&res_num, "0", 0);
	str2num (&base, "256", 0);

	for (pt =
	     ((SQL_NUMERIC_STRUCT *) c_value)->val + (SQL_MAX_NUMERIC_LEN -
						      1), i = 0;
	     i < SQL_MAX_NUMERIC_LEN; --pt, ++i)
	  {
	    sprintf (buf, "%d", *pt);
	    str2num (&num2, buf, 0);

	    num1 = res_num;
	    res_num = NULL;
	    bc_multiply (num1, base, &res_tmp, 0);
	    bc_add (res_tmp, num2, &res_num, 0);

	    free_num (&num1);	// free_num - assign null to num1
	    free_num (&num2);
	    free_num (&res_tmp);
	  }

	value = num2str (res_num);
	if (c_scale > 0)
	  {
	    value = UT_REALLOC (value, strlen (value) + 2);	// for period
	    pt = value;
//                              pt += c_precision - c_scale;  // OR pt += strlen(value) - scale;
	    pt += strlen (value) - c_scale;
	    memmove (pt + 1, pt, c_scale + 1);
	    *pt = '.';
	  }

	if (((SQL_NUMERIC_STRUCT *) c_value)->sign == 0)
	  {			// negative
	    value = UT_REALLOC (value, strlen ((char *) value) + 2);	// for sign
	    memmove ((char *) value + 1, value, strlen ((char *) value) + 1);
	    ((char *) value)[0] = '-';
	  }

	free_num (&num1);
	free_num (&num2);
	free_num (&res_tmp);
	free_num (&res_num);
	free_num (&base);
	break;
      }
    }

  return value;
}

/************************************************************************
 * name: odbc_value_to_cci2
 * arguments:
 *		index - array index
 *		sql_value_root - void* type
 * returns/side-effects:
 * description:
 * NOTE:
 *		odbc_value_to_cci와 달리 argument에 값을 할당해준다.
 *		Binary와 String의 경우만 memory alloc이 일어난다.
 *
 *			SQL_C_TYPE	|		T_CCI_A_TYPE
 *		----------------------------------------------
 *		SQL_C_SHORT		|			int
 *		SQL_C_SSHORT	|			int
 *		SQL_C_USRHOT	|			int
 *		SQL_C_STINYINT	|			int
 *		SQL_C_TINYINT	|			int
 *		SQL_C_UTINYINT	|			int
 *		SQL_C_LONG		|			int
 *		SQL_C_SLONG		|			int
 *		SQL_C_ULONG		|			int
 *		SQL_C_SBIGINT	|			int
 *		SQL_C_UBIGINT	|			int
 *		SQL_C_FLOAT		|			float
 *		SQL_C_DOUBLE	|			double
 *		SQL_C_CHAR		|			char*
 *		SQL_C_BINARY	|			T_CCI_BIT
 *		SQL_C_NUMERIC	|			char*
 *		SQL_C_DATE		|			T_CCI_DATE
 *		SQL_C_TIME		|			T_CCI_DATE
 *		SQL_C_TIMESTAMP |			T_CCI_DATE
 *
 ************************************************************************/
PUBLIC void
odbc_value_to_cci2 (void *sql_value_root, int index, void *c_value,
		    short c_type, long c_length, short c_precision,
		    short c_scale)
{

  switch (c_type)
    {

	/*---------------------------------------------------------------
	 *					INTEGRAL TYPE
	 *--------------------------------------------------------------*/
    case SQL_C_SHORT:		// for 2.x backward compatibility
    case SQL_C_SSHORT:
    case SQL_C_USHORT:
      *((int *) sql_value_root + index) = *(short *) c_value;
      break;

    case SQL_C_STINYINT:
    case SQL_C_UTINYINT:
    case SQL_C_TINYINT:	// for 2.x backward compatibility
      *((int *) sql_value_root + index) = *(char *) c_value;
      break;

    case SQL_C_LONG:		// for 2.x backward compatibility
    case SQL_C_SLONG:
    case SQL_C_ULONG:
      *((int *) sql_value_root + index) = *(long *) c_value;
      break;
    case SQL_C_SBIGINT:
    case SQL_C_UBIGINT:
      *((__int64 *) sql_value_root + index) = *(__int64 *) c_value;
      break;

	/*---------------------------------------------------------------
	 *					floating point type
	 *--------------------------------------------------------------*/
    case SQL_C_FLOAT:
      *((float *) sql_value_root + index) = *(float *) c_value;
      break;

    case SQL_C_DOUBLE:
      *((double *) sql_value_root + index) = *(double *) c_value;
      break;

	/*---------------------------------------------------------------
	 *					char & binary type
	 *--------------------------------------------------------------*/
    case SQL_C_CHAR:
      *((char **) sql_value_root + index) =
	UT_MAKE_STRING (c_value, c_length);
      break;

    case SQL_C_BINARY:
      ((T_CCI_BIT *) sql_value_root + index)->size = c_length;
      ((T_CCI_BIT *) sql_value_root + index)->buf =
	UT_MAKE_BINARY (c_value, c_length);
      break;

	/*---------------------------------------------------------------
	 *					date & time type
	 *--------------------------------------------------------------*/
    case SQL_C_TYPE_DATE:
    case SQL_C_DATE:		// for 2.x backward compatibility
      ((T_CCI_DATE *) sql_value_root + index)->yr =
	((SQL_DATE_STRUCT *) c_value)->year;
      ((T_CCI_DATE *) sql_value_root + index)->mon =
	((SQL_DATE_STRUCT *) c_value)->month;
      ((T_CCI_DATE *) sql_value_root + index)->day =
	((SQL_DATE_STRUCT *) c_value)->day;
      break;

    case SQL_C_TYPE_TIME:
    case SQL_C_TIME:		// for 2.x backward compatibility
      ((T_CCI_DATE *) sql_value_root + index)->hh =
	((SQL_TIME_STRUCT *) c_value)->hour;
      ((T_CCI_DATE *) sql_value_root + index)->mm =
	((SQL_TIME_STRUCT *) c_value)->minute;
      ((T_CCI_DATE *) sql_value_root + index)->ss =
	((SQL_TIME_STRUCT *) c_value)->second;
      break;

    case SQL_C_TYPE_TIMESTAMP:
    case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
      ((T_CCI_DATE *) sql_value_root + index)->yr =
	((SQL_TIMESTAMP_STRUCT *) c_value)->year;
      ((T_CCI_DATE *) sql_value_root + index)->mon =
	((SQL_TIMESTAMP_STRUCT *) c_value)->month;
      ((T_CCI_DATE *) sql_value_root + index)->day =
	((SQL_TIMESTAMP_STRUCT *) c_value)->day;
      ((T_CCI_DATE *) sql_value_root + index)->hh =
	((SQL_TIMESTAMP_STRUCT *) c_value)->hour;
      ((T_CCI_DATE *) sql_value_root + index)->mm =
	((SQL_TIMESTAMP_STRUCT *) c_value)->minute;
      ((T_CCI_DATE *) sql_value_root + index)->ss =
	((SQL_TIMESTAMP_STRUCT *) c_value)->second;
      break;

	/*---------------------------------------------------------------
	 *					numeric type
	 *--------------------------------------------------------------*/
    case SQL_C_NUMERIC:
      {
	bc_num num1 = NULL, num2 = NULL, base = NULL, res_num =
	  NULL, res_tmp = NULL;
	unsigned char *pt;
	char *value;
	char buf[16];
	short i;

	init_numbers ();

	str2num (&res_num, "0", 0);
	str2num (&base, "256", 0);

	for (pt =
	     ((SQL_NUMERIC_STRUCT *) c_value)->val + (SQL_MAX_NUMERIC_LEN -
						      1), i = 0;
	     i < SQL_MAX_NUMERIC_LEN; ++pt, ++i)
	  {
	    sprintf (buf, "%d", *pt);
	    str2num (&num2, buf, 0);

	    num1 = res_num;
	    res_num = NULL;
	    bc_multiply (num1, base, &res_tmp, 0);
	    bc_add (res_tmp, num2, &res_num, 0);

	    free_num (&num1);	// free_num - assign null to num1
	    free_num (&num2);
	    free_num (&res_tmp);
	  }

	value = num2str (res_num);
	if (c_scale > 0)
	  {
	    value = UT_REALLOC (value, strlen (value) + 2);	// for period
	    pt = value;
	    pt += c_precision - c_scale;	// OR pt += strlen(value) - scale;
	    memmove (pt + 1, pt, c_scale + 1);
	    *pt = '.';
	  }

	if (((SQL_NUMERIC_STRUCT *) c_value)->sign == 0)
	  {			// negative
	    value = UT_REALLOC (value, strlen ((char *) value) + 2);	// for sign
	    memmove ((char *) value + 1, value, strlen ((char *) value) + 1);
	    ((char *) value)[0] = '-';
	  }

	/* NUMERIC은 CCI_A_TYPE_STR로 conversion한다. */
	*((char **) sql_value_root + index) = value;

	free_num (&num1);
	free_num (&num2);
	free_num (&res_tmp);
	free_num (&res_num);
	free_num (&base);
	break;

      }

    }
}

/************************************************************************
 * name: cci_value_to_odbc
 * arguments:
 * returns/side-effects:
 *		cci_value length
 * description:
 * NOTE:
 *		SQL_C_TYPE과 T_CCI_A_TYPE과의 관계는 odbc_value_to_cci()를 참조한다.
 *		value truncation에 대해서 고려되지 않았다.
 *		string type과 binary type은 실제로 발생하지 않는다.
 ************************************************************************/
PUBLIC SQLLEN
cci_value_to_odbc (void *c_value, short concise_type,
		   short precision, short scale,
		   SQLLEN buffer_length, UNI_CCI_A_TYPE * cci_value,
		   T_CCI_A_TYPE a_type)
{
  SQLLEN length = 0;

  switch (concise_type)
    {

	/*---------------------------------------------------------------
	 *					INTEGRAL TYPE
	 *--------------------------------------------------------------*/
    case SQL_C_SHORT:		// for 2.x backward compatibility
    case SQL_C_SSHORT:
    case SQL_C_USHORT:
      *(short *) c_value = cci_value->i;
      length = sizeof (short);
      break;

    case SQL_C_STINYINT:
    case SQL_C_UTINYINT:
    case SQL_C_TINYINT:	// for 2.x backward compatibility
      *(char *) c_value = cci_value->i;
      length = sizeof (char);
      break;

    case SQL_C_LONG:		// for 2.x backward compatibility
    case SQL_C_SLONG:
    case SQL_C_ULONG:
      *(long *) c_value = cci_value->i;
      length = sizeof (long);
      break;
    case SQL_C_SBIGINT:
    case SQL_C_UBIGINT:
      *(__int64 *) c_value = cci_value->bi;
      length = sizeof (__int64);
      break;


	/*---------------------------------------------------------------
	 *					floating point type
	 *--------------------------------------------------------------*/
    case SQL_C_FLOAT:
      *(float *) c_value = cci_value->f;
      length = sizeof (float);
      break;

    case SQL_C_DOUBLE:
      *(double *) c_value = cci_value->d;
      length = sizeof (double);
      break;

	/*---------------------------------------------------------------
	 *					char & binary type
	 *--------------------------------------------------------------*/
    case SQL_C_CHAR:
      str_value_assign (cci_value->str, c_value, buffer_length, &length);
      break;

    case SQL_C_BINARY:
      bin_value_assign (cci_value->bit.buf, cci_value->bit.size,
			c_value, buffer_length, &length);
      break;

	/*---------------------------------------------------------------
	 *					date & time type
	 *--------------------------------------------------------------*/
    case SQL_C_TYPE_DATE:
    case SQL_C_DATE:		// for 2.x backward compatibility
      ((SQL_DATE_STRUCT *) c_value)->year = cci_value->date.yr;
      ((SQL_DATE_STRUCT *) c_value)->month = cci_value->date.mon;
      ((SQL_DATE_STRUCT *) c_value)->day = cci_value->date.day;
      length = sizeof (SQL_DATE_STRUCT);
      break;

    case SQL_C_TYPE_TIME:
    case SQL_C_TIME:		// for 2.x backward compatibility
      ((SQL_TIME_STRUCT *) c_value)->hour = cci_value->date.hh;
      ((SQL_TIME_STRUCT *) c_value)->minute = cci_value->date.mm;
      ((SQL_TIME_STRUCT *) c_value)->second = cci_value->date.ss;
      length = sizeof (SQL_TIME_STRUCT);
      break;

    case SQL_C_TYPE_TIMESTAMP:
    case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
      ((SQL_TIMESTAMP_STRUCT *) c_value)->year = cci_value->date.yr;
      ((SQL_TIMESTAMP_STRUCT *) c_value)->month = cci_value->date.mon;
      ((SQL_TIMESTAMP_STRUCT *) c_value)->day = cci_value->date.day;
      ((SQL_TIMESTAMP_STRUCT *) c_value)->hour = cci_value->date.hh;
      ((SQL_TIMESTAMP_STRUCT *) c_value)->minute = cci_value->date.mm;
      ((SQL_TIMESTAMP_STRUCT *) c_value)->second = cci_value->date.ss;
      ((SQL_TIMESTAMP_STRUCT *) c_value)->fraction = 0;
      length = sizeof (SQL_TIMESTAMP_STRUCT);
      break;

	/*---------------------------------------------------------------
	 *					numeric type
	 *--------------------------------------------------------------*/
    case SQL_C_NUMERIC:
      {
	bc_num num1 = NULL, num2 = NULL, quot = NULL, rem = NULL, res_tmp =
	  NULL;
	char *pt, *pt2, *tmp_str_num = NULL;
	char str[64];		/* numeric value that is removed a period
				 * cf) The max precision of numeric is 38 in CUBRID
				 */
	short i;
	unsigned char num_add_zero = 0;


	((SQL_NUMERIC_STRUCT *) c_value)->precision =
	  (unsigned char) precision;
	((SQL_NUMERIC_STRUCT *) c_value)->scale = (unsigned char) scale;

	if (cci_value->str[0] == '-')
	  {
	    ((SQL_NUMERIC_STRUCT *) c_value)->sign = 0;	// negative
	    pt = cci_value->str + 1;
	  }
	else
	  {
	    ((SQL_NUMERIC_STRUCT *) c_value)->sign = 1;	// positive
	    pt = cci_value->str;
	  }
	// pt means the first vaild digit position

	pt2 = strchr (pt, '.');
	if (pt2 != NULL)
	  {
	    strncpy (str, pt, pt2 - pt);
	    str[pt2 - pt] = '\0';
	    ++pt2;
	    strcat (str, pt2);
	    num_add_zero = scale - strlen (pt2);
	    // add additional '0' for scale
	    for (pt = str + strlen (str), i = 1; i <= num_add_zero; ++pt, ++i)
	      {
		*pt = '0';
	      }
	    *pt = '\0';
	  }
	else
	  {
	    strcpy (str, pt);
	  }

	init_numbers ();

	str2num (&num1, str, 0);
	str2num (&num2, "256", 0);


	for (i = 0, tmp_str_num = NULL; i < SQL_MAX_NUMERIC_LEN; ++i)
	  {
	    bc_divmod (num1, num2, &quot, &rem, 0);

	    tmp_str_num = num2str (rem);

	    ((SQL_NUMERIC_STRUCT *) c_value)->val[i] =
	      (unsigned char) atoi (tmp_str_num);

	    NA_FREE (tmp_str_num);
	    free_num (&rem);
	    free_num (&num1);
	    num1 = quot;
	  }

	free_num (&num1);
	free_num (&num2);
	free_num (&quot);
	free_num (&rem);
	NA_FREE (tmp_str_num);

	length = sizeof (SQL_NUMERIC_STRUCT);

	break;
      }
    }

  return length;
}



PUBLIC VALUE_CONTAINER *
create_value_container ()
{
  VALUE_CONTAINER *value = NULL;

  value = (VALUE_CONTAINER *) UT_ALLOC (sizeof (VALUE_CONTAINER));

  if (value != NULL)
    {
      memset (value, 0, sizeof (VALUE_CONTAINER));
    }
  return value;
}

PUBLIC void
clear_value_container (VALUE_CONTAINER * value)
{
  if (value->type == SQL_C_CHAR || value->type == SQL_C_BINARY)
    {
      if (value->value.dummy != NULL)
	{
	  UT_FREE (value->value.dummy);
	}
    }
}

PUBLIC void
free_value_container (VALUE_CONTAINER * value)
{
  if (value == NULL)
    return;

  clear_value_container (value);

  UT_FREE (value);
}

/*
 *	partially not implemented
 *			BINARY, date type
 *	Not implemented about
 *			BIT, NUMERIC, OBJECT, SET, TINYINT, BIGINT
 */
PUBLIC RETCODE
odbc_value_converter (VALUE_CONTAINER * target_value,
		      VALUE_CONTAINER * src_value)
{
  char buf[BUF_SIZE];

  // NULL value
  if (src_value->length == 0)
    {
      target_value->length = 0;
      target_value->value.dummy = NULL;
      return ODBC_SUCCESS;
    }

  // SQL_C_DEFAULT
  if (target_value->type == SQL_C_DEFAULT)
    {
      target_value->type = src_value->type;
    }

  switch (src_value->type)
    {
    case SQL_C_CHAR:
      switch (target_value->type)
	{
	case SQL_C_CHAR:
	case SQL_C_BINARY:
	  target_value->value.str = UT_MAKE_STRING (src_value->value.str, -1);
	  target_value->length = src_value->length;
	  break;
	case SQL_C_SHORT:	// for 2.x backward compatibility
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	  target_value->value.s = (short) atoi (src_value->value.str);
	  target_value->length = sizeof (short);
	  break;
	case SQL_C_LONG:	// for 2.x backward compatibility
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	  target_value->value.l = atol (src_value->value.str);
	  target_value->length = sizeof (long);
	  break;
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	  target_value->value.bi = _atoi64 (src_value->value.str);
	  target_value->length = sizeof (__int64);
	  break;
	case SQL_C_FLOAT:
	  target_value->value.f = (float) atof (src_value->value.str);
	  target_value->length = sizeof (float);
	  break;
	case SQL_C_DOUBLE:
	  target_value->value.d = atof (src_value->value.str);
	  target_value->length = sizeof (double);
	  break;
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_DATE:	// for 2.x backward compatibility
	case SQL_C_TIME:	// for 2.x backward compatibility
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  return ODBC_NOT_IMPLEMENTED;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;
    case SQL_C_SHORT:
    case SQL_C_SSHORT:
    case SQL_C_USHORT:
      switch (target_value->type)
	{
	case SQL_C_CHAR:
	  sprintf (buf, "%hd", src_value->value.s);
	  target_value->value.str = UT_MAKE_STRING (buf, -1);
	  target_value->length = strlen (buf) + 1;
	  break;
	case SQL_C_BINARY:
	  target_value->value.bin = UT_ALLOC (sizeof (short));
	  bin_value_assign (&src_value->value.s, sizeof (short),
			    target_value->value.bin, sizeof (short),
			    &(target_value->length));
	  break;
	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	  target_value->value.s = src_value->value.s;
	  target_value->length = sizeof (short);
	  break;
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	  target_value->value.l = (long) src_value->value.s;
	  target_value->length = sizeof (long);
	  break;
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	  target_value->value.bi = (__int64) src_value->value.s;
	  target_value->length = sizeof (__int64);
	  break;
	case SQL_C_FLOAT:
	  target_value->value.f = (float) src_value->value.s;
	  target_value->length = sizeof (float);
	  break;
	case SQL_C_DOUBLE:
	  target_value->value.d = (double) src_value->value.s;
	  target_value->length = sizeof (double);
	  break;
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_DATE:	// for 2.x backward compatibility
	case SQL_C_TIME:	// for 2.x backward compatibility
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  return ODBC_NOT_IMPLEMENTED;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;

      ////////////////
    case SQL_C_LONG:
    case SQL_C_SLONG:
    case SQL_C_ULONG:
      switch (target_value->type)
	{
	case SQL_C_CHAR:
	  sprintf (buf, "%ld", src_value->value.l);
	  target_value->value.str = UT_MAKE_STRING (buf, -1);
	  target_value->length = strlen (buf) + 1;
	  break;
	case SQL_C_BINARY:
	  target_value->value.bin = UT_ALLOC (sizeof (long));
	  bin_value_assign (&src_value->value.l, sizeof (long),
			    target_value->value.bin, sizeof (long),
			    &(target_value->length));
	  break;
	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	  target_value->value.s = (short) src_value->value.l;
	  target_value->length = sizeof (short);
	  break;
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	  target_value->value.l = src_value->value.l;
	  target_value->length = sizeof (long);
	  break;
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	  target_value->value.bi = (__int64) src_value->value.l;
	  target_value->length = sizeof (__int64);
	  break;
	case SQL_C_FLOAT:
	  target_value->value.f = (float) src_value->value.l;
	  target_value->length = sizeof (float);
	  break;
	case SQL_C_DOUBLE:
	  target_value->value.d = (double) src_value->value.l;
	  target_value->length = sizeof (double);
	  break;
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_DATE:	// for 2.x backward compatibility
	case SQL_C_TIME:	// for 2.x backward compatibility
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  return ODBC_NOT_IMPLEMENTED;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;

    case SQL_C_SBIGINT:
    case SQL_C_UBIGINT:
      switch (target_value->type)
	{
	case SQL_C_CHAR:
	  sprintf (buf, "%lld", (long long) src_value->value.bi);
	  target_value->value.str = UT_MAKE_STRING (buf, -1);
	  target_value->length = strlen (buf) + 1;
	  break;
	case SQL_C_BINARY:
	  target_value->value.bin = UT_ALLOC (sizeof (__int64));
	  bin_value_assign (&src_value->value.bi, sizeof (__int64),
			    target_value->value.bin, sizeof (__int64),
			    &(target_value->length));
	  break;
	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	  target_value->value.s = (short) src_value->value.bi;
	  target_value->length = sizeof (short);
	  break;
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	  target_value->value.l = (long) src_value->value.bi;
	  target_value->length = sizeof (long);
	  break;
	case SQL_C_FLOAT:
	  target_value->value.f = (float) src_value->value.bi;
	  target_value->length = sizeof (float);
	  break;
	case SQL_C_DOUBLE:
	  target_value->value.d = (double) src_value->value.bi;
	  target_value->length = sizeof (double);
	  break;
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_DATE:	// for 2.x backward compatibility
	case SQL_C_TIME:	// for 2.x backward compatibility
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  return ODBC_NOT_IMPLEMENTED;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;

    case SQL_C_FLOAT:
      switch (target_value->type)
	{
	case SQL_C_CHAR:
	  sprintf (buf, "%f", src_value->value.f);
	  target_value->value.str = UT_MAKE_STRING (buf, -1);
	  target_value->length = strlen (buf) + 1;
	  break;
	case SQL_C_BINARY:
	  target_value->value.bin = UT_ALLOC (sizeof (float));
	  target_value->length = sizeof (float);
	  memcpy (target_value->value.bin, &src_value->value.s,
		  sizeof (float));
	  break;
	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	  target_value->value.s = (short) src_value->value.f;
	  target_value->length = sizeof (short);
	  break;
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	  target_value->value.l = (long) src_value->value.f;
	  target_value->length = sizeof (long);
	  break;
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	  target_value->value.bi = (__int64) src_value->value.f;
	  target_value->length = sizeof (__int64);
	  break;
	case SQL_C_FLOAT:
	  target_value->value.f = src_value->value.f;
	  target_value->length = sizeof (float);
	  break;
	case SQL_C_DOUBLE:
	  target_value->value.d = (double) src_value->value.f;
	  target_value->length = sizeof (double);
	  break;
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_DATE:	// for 2.x backward compatibility
	case SQL_C_TIME:	// for 2.x backward compatibility
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  return ODBC_NOT_IMPLEMENTED;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;

    case SQL_C_DOUBLE:
      switch (target_value->type)
	{
	case SQL_C_CHAR:
	  sprintf (buf, "%lf", src_value->value.d);
	  target_value->length = strlen (buf) + 1;
	  break;
	case SQL_C_BINARY:
	  target_value->value.bin = UT_ALLOC (sizeof (double));
	  bin_value_assign (&src_value->value.d, sizeof (double),
			    target_value->value.bin, sizeof (double),
			    &(target_value->length));
	  break;
	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	  target_value->value.s = (short) src_value->value.d;
	  target_value->length = sizeof (short);
	  break;
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	  target_value->value.l = (long) src_value->value.d;
	  target_value->length = sizeof (long);
	  break;
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	  target_value->value.bi = (__int64) src_value->value.d;
	  target_value->length = sizeof (__int64);
	  break;
	case SQL_C_FLOAT:
	  target_value->value.f = (float) src_value->value.d;
	  target_value->length = sizeof (float);
	  break;
	case SQL_C_DOUBLE:
	  target_value->value.d = src_value->value.d;
	  target_value->length = sizeof (double);
	  break;
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_DATE:	// for 2.x backward compatibility
	case SQL_C_TIME:	// for 2.x backward compatibility
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  return ODBC_NOT_IMPLEMENTED;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;

    case SQL_C_BINARY:
      switch (target_value->type)
	{
	case SQL_C_BINARY:
	  target_value->value.bin = UT_ALLOC (src_value->length);
	  memcpy (target_value->value.bin, src_value->value.bin,
		  src_value->length);
	  target_value->length = src_value->length;
	  break;
	case SQL_C_CHAR:
	  target_value->value.str = UT_ALLOC (src_value->length + 1);
	  memcpy (target_value->value.str, src_value->value.bin,
		  src_value->length);
	  target_value->value.str[src_value->length] = '\0';
	  break;

	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	  memcpy (&target_value->value.s, src_value->value.bin,
		  sizeof (short));
	  target_value->length = sizeof (short);
	  break;
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	  memcpy (&target_value->value.l, src_value->value.bin,
		  sizeof (long));
	  target_value->length = sizeof (long);
	  break;
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	  memcpy (&target_value->value.bi, src_value->value.bin,
		  sizeof (__int64));
	  target_value->length = sizeof (__int64);
	  break;
	case SQL_C_FLOAT:
	  memcpy (&target_value->value.f, src_value->value.bin,
		  sizeof (float));
	  target_value->length = sizeof (float);
	  break;
	case SQL_C_DOUBLE:
	  memcpy (&target_value->value.d, src_value->value.bin,
		  sizeof (double));
	  target_value->length = sizeof (double);
	  break;
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_DATE:	// for 2.x backward compatibility
	case SQL_C_TIME:	// for 2.x backward compatibility
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  return ODBC_NOT_IMPLEMENTED;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;

    case SQL_C_TYPE_DATE:
    case SQL_C_DATE:		// for 2.x backward compatibility
      switch (target_value->type)
	{
	case SQL_C_CHAR:
	  sprintf (buf, "%d-%d-%d", target_value->value.date.year,
		   target_value->value.date.month,
		   target_value->value.date.day);
	  target_value->value.str = UT_MAKE_STRING (buf, -1);
	  target_value->length = strlen (buf) + 1;
	  break;
	case SQL_C_BINARY:
	  target_value->value.bin = UT_ALLOC (sizeof (SQL_DATE_STRUCT));
	  bin_value_assign (&src_value->value.date, sizeof (SQL_DATE_STRUCT),
			    target_value->value.bin, sizeof (SQL_DATE_STRUCT),
			    &(target_value->length));
	  break;
	case SQL_C_TYPE_DATE:
	case SQL_C_DATE:	// for 2.x backward compatibility
	  target_value->value.date.year = src_value->value.date.year;
	  target_value->value.date.month = src_value->value.date.month;
	  target_value->value.date.day = src_value->value.date.day;
	  target_value->length = sizeof (SQL_DATE_STRUCT);
	  break;
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  target_value->value.ts.year = src_value->value.date.year;
	  target_value->value.ts.month = src_value->value.date.month;
	  target_value->value.ts.day = src_value->value.date.day;
	  target_value->value.ts.hour = 0;
	  target_value->value.ts.minute = 0;
	  target_value->value.ts.second = 0;
	  target_value->value.ts.fraction = 0;
	  target_value->length = sizeof (SQL_TIMESTAMP_STRUCT);
	  break;
	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	case SQL_C_FLOAT:
	case SQL_C_DOUBLE:
	case SQL_C_TYPE_TIME:
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	  return ODBC_INVALID_TYPE_CONVERSION;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;

    case SQL_C_TYPE_TIME:
    case SQL_C_TIME:		// for 2.x backward compatibility
      switch (target_value->type)
	{
	case SQL_C_CHAR:
	  sprintf (buf, "%d:%d:%d", src_value->value.time.hour,
		   src_value->value.time.minute,
		   src_value->value.time.second);

	  target_value->value.str = UT_MAKE_STRING (buf, -1);
	  target_value->length = strlen (buf) + 1;
	  break;
	case SQL_C_BINARY:
	  target_value->value.bin = UT_ALLOC (sizeof (SQL_TIME_STRUCT));
	  bin_value_assign (&src_value->value.time, sizeof (SQL_TIME_STRUCT),
			    target_value->value.bin, sizeof (SQL_TIME_STRUCT),
			    &(target_value->length));
	  break;
	case SQL_C_TYPE_TIME:
	case SQL_C_TIME:	// for 2.x backward compatibility
	  target_value->value.time.hour = src_value->value.time.hour;
	  target_value->value.time.minute = src_value->value.time.minute;
	  target_value->value.time.second = src_value->value.time.second;
	  target_value->length = sizeof (SQL_TIME_STRUCT);
	  break;
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  target_value->value.ts.year = 0;
	  target_value->value.ts.month = 0;
	  target_value->value.ts.day = 0;
	  target_value->value.ts.hour = src_value->value.time.hour;
	  target_value->value.ts.minute = src_value->value.time.minute;
	  target_value->value.ts.second = src_value->value.time.second;
	  target_value->value.ts.fraction = 0;
	  target_value->length = sizeof (SQL_TIMESTAMP_STRUCT);
	  break;
	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	case SQL_C_FLOAT:
	case SQL_C_DOUBLE:
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	case SQL_C_TYPE_DATE:
	case SQL_C_DATE:	// for 2.x backward compatibility
	  return ODBC_INVALID_TYPE_CONVERSION;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;

    case SQL_C_TYPE_TIMESTAMP:
    case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
      switch (target_value->type)
	{
	case SQL_C_CHAR:
	  sprintf (buf, "%d-%d-%d %d:%d:%d", src_value->value.ts.year,
		   src_value->value.ts.month,
		   src_value->value.ts.day,
		   src_value->value.ts.hour,
		   src_value->value.ts.minute, src_value->value.ts.second);
	  target_value->value.str = UT_MAKE_STRING (buf, -1);
	  target_value->length = strlen (buf) + 1;
	  break;
	case SQL_C_BINARY:
	  target_value->value.bin = UT_ALLOC (sizeof (SQL_TIMESTAMP_STRUCT));
	  bin_value_assign (&src_value->value.ts,
			    sizeof (SQL_TIMESTAMP_STRUCT),
			    target_value->value.bin,
			    sizeof (SQL_TIMESTAMP_STRUCT),
			    &(target_value->length));
	  break;
	case SQL_C_TYPE_DATE:
	case SQL_C_DATE:	// for 2.x backward compatibility
	  target_value->value.date.year = src_value->value.ts.year;
	  target_value->value.date.month = src_value->value.ts.month;
	  target_value->value.date.day = src_value->value.ts.day;
	  target_value->length = sizeof (SQL_DATE_STRUCT);
	  break;
	case SQL_C_TYPE_TIME:
	case SQL_C_TIME:	// for 2.x backward compatibility
	  target_value->value.time.hour = src_value->value.ts.hour;
	  target_value->value.time.minute = src_value->value.ts.minute;
	  target_value->value.time.second = src_value->value.ts.second;
	  target_value->length = sizeof (SQL_TIME_STRUCT);
	  break;
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_TIMESTAMP:	// for 2.x backward compatibility
	  target_value->value.ts.year = src_value->value.ts.year;
	  target_value->value.ts.month = src_value->value.ts.month;
	  target_value->value.ts.day = src_value->value.ts.day;
	  target_value->value.ts.hour = src_value->value.ts.hour;
	  target_value->value.ts.minute = src_value->value.ts.minute;
	  target_value->value.ts.second = src_value->value.ts.second;
	  target_value->value.ts.fraction = 0;
	  target_value->length = sizeof (SQL_TIMESTAMP_STRUCT);
	  break;
	case SQL_C_SHORT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	case SQL_C_LONG:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	case SQL_C_FLOAT:
	case SQL_C_DOUBLE:
	case SQL_C_NUMERIC:
	case SQL_C_BIT:
	  return ODBC_INVALID_TYPE_CONVERSION;
	default:
	  return ODBC_UNKNOWN_TYPE;
	}
      break;

    case SQL_C_BIT:
    case SQL_C_NUMERIC:
      return ODBC_NOT_IMPLEMENTED;
    default:
      return ODBC_UNKNOWN_TYPE;
    }

  return ODBC_OK;
}

PUBLIC short
odbc_date_type_backward (short type)
{
  int i;
  int set_size = GET_SET_SIZE (datetime_c_type_backward_set);

  for (i = 0; i < set_size; i++)
    {
      if (datetime_c_type_backward_set[i].current_type == type)
	{
	  return datetime_c_type_backward_set[i].backward_type;
	}
    }

  return type;
}

PRIVATE int
odbc_is_valid_concise_type (short odbc_type)
{
  return (odbc_is_valid_c_concise_type (odbc_type) ||
	  odbc_is_valid_sql_concise_type (odbc_type));
}

PRIVATE int
odbc_is_valid_c_concise_type (short odbc_type)
{
  return (odbc_is_valid_c_common_type (odbc_type) ||
	  odbc_is_valid_c_date_type (odbc_type) ||
	  odbc_is_valid_c_interval_type (odbc_type));
}

PRIVATE int
odbc_is_valid_sql_concise_type (short odbc_type)
{
  return (odbc_is_valid_sql_common_type (odbc_type) ||
	  odbc_is_valid_sql_date_type (odbc_type) ||
	  odbc_is_valid_sql_interval_type (odbc_type));
}

PRIVATE int
odbc_is_valid_verbose_type (short odbc_type)
{
  return (odbc_is_valid_c_verbose_type (odbc_type) ||
	  odbc_is_valid_sql_verbose_type (odbc_type));
}

PRIVATE int
odbc_is_valid_c_verbose_type (short odbc_type)
{
  return (odbc_is_valid_c_common_type (odbc_type) ||
	  odbc_is_valid_date_verbose_type (odbc_type) ||
	  odbc_is_valid_interval_verbose_type (odbc_type));
}

PRIVATE int
odbc_is_valid_sql_verbose_type (short odbc_type)
{
  return (odbc_is_valid_sql_common_type (odbc_type) ||
	  odbc_is_valid_date_verbose_type (odbc_type) ||
	  odbc_is_valid_interval_verbose_type (odbc_type));
}

PRIVATE int
odbc_is_valid_c_common_type (short c_type)
{
  int set_size = GET_SET_SIZE (c_common_type_set);

  return seek_in_common_type_set (c_common_type_set, set_size, c_type);
}

PRIVATE int
odbc_is_valid_sql_common_type (short sql_type)
{
  int set_size = GET_SET_SIZE (sql_common_type_set);

  return seek_in_common_type_set (sql_common_type_set, set_size, sql_type);
}

PRIVATE int
odbc_is_valid_date_code (short code)
{
  int i;
  int type_info_set_size = GET_SET_SIZE (datetime_date_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (datetime_date_type_info_set[i].type_subcode == code)
	{
	  return _TRUE_;
	}
    }

  return _FALSE_;
}

PRIVATE int
odbc_is_valid_internal_code (short code)
{
  int i;
  int type_info_set_size = GET_SET_SIZE (datetime_internal_type_info_set);

  for (i = 0; i < type_info_set_size; i++)
    {
      if (datetime_internal_type_info_set[i].type_subcode == code)
	{
	  return _TRUE_;
	}
    }

  return _FALSE_;
}

PRIVATE int
octet_len_char (int precision)
{
  return precision;
}

PRIVATE int
octet_len_binary (int precision)
{
  return (int) ceil ((double) precision / 8.0);
}

PRIVATE int
display_size_char (int precision)
{
  return precision;
}
PRIVATE int
display_size_decimal (int precision)
{
  return (precision + 2);
}

PRIVATE int
display_size_binary (int precision)
{
  return (int) ceil ((double) precision / 4.0) + 2;
}

PRIVATE int
column_size_char (int precision)
{
  return precision;
}
PRIVATE int
column_size_decimal (int precision)
{
  return precision;
}

PRIVATE int
column_size_binary (int precision)
{
  return precision;
}

PRIVATE int
seek_in_common_type_set (int *set, int set_size, short id)
{
  int i;

  for (i = 0; i < set_size; i++)
    {
      if (id == set[i])
	{
	  return _TRUE_;
	}
    }

  return _FALSE_;
}
