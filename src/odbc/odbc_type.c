#include		<stdio.h>
#include		"portable.h"
#include		"sqlext.h"
#include		"odbc_type.h"
#include		"cas_cci.h"
#include		"number.h"
#include		"util.h"
#include		"diag.h"

PUBLIC int odbc_is_valid_type(short odbc_type)
{
	return ( 
		odbc_is_valid_concise_type(odbc_type) ||
		odbc_is_valid_verbose_type(odbc_type)
		);
}


PUBLIC int odbc_is_valid_code(short code ) 
{
	return (
		code == SQL_CODE_DATE ||
		code == SQL_CODE_TIME ||
		code == SQL_CODE_TIMESTAMP ||
		code == SQL_CODE_DAY ||
		code == SQL_CODE_DAY_TO_HOUR ||
		code == SQL_CODE_DAY_TO_MINUTE ||
		code == SQL_CODE_DAY_TO_SECOND ||
		code == SQL_CODE_HOUR ||
		code == SQL_CODE_HOUR_TO_MINUTE ||
		code == SQL_CODE_HOUR_TO_SECOND ||
		code == SQL_CODE_MINUTE ||
		code == SQL_CODE_MINUTE_TO_SECOND ||
		code == SQL_CODE_SECOND ||
		code == SQL_CODE_MONTH ||
		code == SQL_CODE_YEAR ||
		code == SQL_CODE_YEAR_TO_MONTH
		);
}

PUBLIC int odbc_is_valid_date_verbose_type(short odbc_type)
{
	return (
		odbc_type == SQL_DATETIME
		);
}

PUBLIC int odbc_is_valid_interval_verbose_type(short odbc_type)
{
	return (
		odbc_type == SQL_INTERVAL
		);
}
PUBLIC int odbc_is_valid_c_type(short odbc_type)
{
	return (
		odbc_is_valid_c_common_type(odbc_type) ||
		odbc_is_valid_c_date_type(odbc_type) ||
		odbc_is_valid_c_interval_type(odbc_type) ||
		odbc_is_valid_date_verbose_type(odbc_type) ||
		odbc_is_valid_interval_verbose_type(odbc_type)
		);
}

PUBLIC int odbc_is_valid_sql_type(short odbc_type)
{
	return (
		odbc_is_valid_sql_common_type(odbc_type) ||
		odbc_is_valid_sql_date_type(odbc_type) ||
		odbc_is_valid_sql_interval_type(odbc_type) ||
		odbc_is_valid_date_verbose_type(odbc_type) ||
		odbc_is_valid_interval_verbose_type(odbc_type)
		);
}

PUBLIC int odbc_is_valid_concise_type(short odbc_type)
{
	return (
		odbc_is_valid_c_concise_type(odbc_type) ||
		odbc_is_valid_sql_concise_type(odbc_type)
		);
}

PUBLIC int odbc_is_valid_c_concise_type(short odbc_type)
{
	return (
		odbc_is_valid_c_common_type(odbc_type) ||
		odbc_is_valid_c_date_type(odbc_type) ||
		odbc_is_valid_c_interval_type(odbc_type)
		);
}

PUBLIC int odbc_is_valid_sql_concise_type(short odbc_type)
{
	return (
		odbc_is_valid_sql_common_type(odbc_type) ||
		odbc_is_valid_sql_date_type(odbc_type) ||
		odbc_is_valid_sql_interval_type(odbc_type)
		);
}

PUBLIC int odbc_is_valid_verbose_type(short odbc_type)
{
	return (
		odbc_is_valid_c_verbose_type(odbc_type) ||
		odbc_is_valid_sql_verbose_type(odbc_type)
		);
}

PUBLIC int odbc_is_valid_c_verbose_type(short odbc_type)
{
	return (
		odbc_is_valid_c_common_type(odbc_type) ||
		odbc_is_valid_date_verbose_type(odbc_type) ||
		odbc_is_valid_interval_verbose_type(odbc_type)
		);
}

PUBLIC int odbc_is_valid_sql_verbose_type(short odbc_type)
{
	return (
		odbc_is_valid_sql_common_type(odbc_type) ||
		odbc_is_valid_date_verbose_type(odbc_type) ||
		odbc_is_valid_interval_verbose_type(odbc_type)
		);
}


PUBLIC int odbc_is_valid_c_common_type(short c_type)
{
	return (
		c_type == SQL_C_CHAR ||
		c_type == SQL_C_SHORT ||	// for 2.x backward compatibility
		c_type == SQL_C_SSHORT ||
		c_type == SQL_C_USHORT ||
		c_type == SQL_C_SLONG ||
		c_type == SQL_C_LONG ||		// for 2.x backward compatibility
		c_type == SQL_C_ULONG ||
		c_type == SQL_C_FLOAT ||
		c_type == SQL_C_DOUBLE ||
		c_type == SQL_C_NUMERIC ||
		c_type == SQL_C_BIT ||
		c_type == SQL_C_STINYINT ||
		c_type == SQL_C_UTINYINT ||
		c_type == SQL_C_TINYINT ||	// for 2.x backward compatibility
		c_type == SQL_C_SBIGINT ||
		c_type == SQL_C_UBIGINT ||
		c_type == SQL_C_BINARY ||
		c_type == SQL_C_BOOKMARK ||
		c_type == SQL_C_VARBOOKMARK ||
		c_type == SQL_C_DEFAULT ||
		c_type == SQL_C_UNI_OBJECT || 
		c_type == SQL_C_UNI_SET ||
		c_type == SQL_C_GUID 
		);
}

PUBLIC int odbc_is_valid_c_date_type(short c_type)
{
	return (
		c_type == SQL_C_TYPE_DATE ||
		c_type == SQL_C_TYPE_TIME ||
		c_type == SQL_C_TYPE_TIMESTAMP ||
		c_type == SQL_C_DATE ||		// for 2.x backward compatibility
		c_type == SQL_C_TIME ||		// for 2.x backward compatibility
		c_type == SQL_C_TIMESTAMP	// for 2.x backward compatibility
		);
}

PUBLIC int odbc_is_valid_c_interval_type(short c_type)
{
	return (
		c_type == SQL_C_INTERVAL_MONTH ||
		c_type == SQL_C_INTERVAL_YEAR ||
		c_type == SQL_C_INTERVAL_YEAR_TO_MONTH ||		
		c_type == SQL_C_INTERVAL_DAY ||
		c_type == SQL_C_INTERVAL_HOUR ||
		c_type == SQL_C_INTERVAL_MINUTE ||
		c_type == SQL_C_INTERVAL_SECOND ||
		c_type == SQL_C_INTERVAL_DAY_TO_HOUR ||
		c_type == SQL_C_INTERVAL_DAY_TO_MINUTE ||
		c_type == SQL_C_INTERVAL_DAY_TO_SECOND ||
		c_type == SQL_C_INTERVAL_HOUR_TO_MINUTE ||
		c_type == SQL_C_INTERVAL_HOUR_TO_SECOND ||
		c_type == SQL_C_INTERVAL_MINUTE_TO_SECOND
		);
}
		
		
PUBLIC int odbc_is_valid_sql_common_type(short sql_type)
{
	return (
		sql_type == SQL_CHAR ||
		sql_type == SQL_VARCHAR ||
		sql_type == SQL_LONGVARCHAR ||
		sql_type == SQL_WCHAR ||
		sql_type == SQL_WVARCHAR ||
		sql_type == SQL_WLONGVARCHAR ||
		sql_type == SQL_DECIMAL ||
		sql_type == SQL_NUMERIC ||
		sql_type == SQL_SMALLINT ||
		sql_type == SQL_INTEGER ||
		sql_type == SQL_REAL ||
		sql_type == SQL_FLOAT ||
		sql_type == SQL_DOUBLE ||
		sql_type == SQL_BIT ||
		sql_type == SQL_TINYINT ||
		sql_type == SQL_BIGINT ||
		sql_type == SQL_BINARY ||
		sql_type == SQL_VARBINARY ||
		sql_type == SQL_LONGVARBINARY ||
		sql_type == SQL_GUID
		/* XXX : deprecated
		sql_type == SQL_UNI_OBJECT ||
		sql_type == SQL_UNI_SET ||
		*/
		);
}
PUBLIC int odbc_is_valid_sql_date_type(short sql_type)

{
	return ( 
		sql_type == SQL_TYPE_DATE ||
		sql_type == SQL_TYPE_TIME ||
		sql_type == SQL_TYPE_TIMESTAMP ||
		sql_type == SQL_DATE ||	// for 2.x backward compatibility
		sql_type == SQL_TIME ||	// for 2.x backward compatibility
		sql_type == SQL_TIMESTAMP	// for 2.x backward compatibility
		);
}

PUBLIC int odbc_is_valid_sql_interval_type(short sql_type)
{
	return (
		sql_type == SQL_INTERVAL_MONTH ||
		sql_type == SQL_INTERVAL_YEAR ||
		sql_type == SQL_INTERVAL_DAY ||
		sql_type == SQL_INTERVAL_HOUR ||
		sql_type == SQL_INTERVAL_MINUTE ||
		sql_type == SQL_INTERVAL_SECOND ||
		sql_type == SQL_INTERVAL_DAY_TO_HOUR ||
		sql_type == SQL_INTERVAL_DAY_TO_MINUTE ||
		sql_type == SQL_INTERVAL_DAY_TO_SECOND ||
		sql_type == SQL_INTERVAL_HOUR_TO_SECOND ||
		sql_type == SQL_INTERVAL_MINUTE_TO_SECOND
		);
}

PUBLIC short odbc_default_c_type(short odbc_type)
{
	switch ( odbc_type ) {
	case SQL_CHAR :
	case SQL_VARCHAR :
	case SQL_LONGVARCHAR :
		return SQL_C_CHAR;
	case SQL_DECIMAL :
	case SQL_NUMERIC :
		return SQL_C_DOUBLE;
	case SQL_SMALLINT :
	case SQL_TINYINT :
	case SQL_BIT :
		return SQL_C_SHORT;
	case SQL_INTEGER :
	case SQL_BIGINT :
		return SQL_C_LONG;
	case SQL_FLOAT :
	case SQL_REAL :
		return SQL_C_FLOAT;
	case SQL_DOUBLE :
		return SQL_C_DOUBLE;
	case SQL_BINARY :
	case SQL_VARBINARY :
	case SQL_LONGVARBINARY :
		return SQL_C_BINARY;
	case SQL_TYPE_DATE :
		return SQL_C_TYPE_DATE;
	case SQL_TYPE_TIME :
		return SQL_C_TYPE_TIME;
	case SQL_TYPE_TIMESTAMP :
		return SQL_C_TYPE_TIMESTAMP;
	case SQL_DATE :		// for 2.x backward compatibility
		return SQL_C_DATE;
	case SQL_TIME :		// for 2.x backward compatibility
		return SQL_C_TIME;
	case SQL_TIMESTAMP :	// for 2.x backward compatibility
		return SQL_C_TIMESTAMP;
	default :
		return SQL_C_CHAR ;
	}
}


/************************************************************************
* name: odbc_size_of_by_type_id
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: interval type은 지원되지 않으며,
*		string, binary type은 length(non-fixed)에 의해서 결정된다.
************************************************************************/		
PUBLIC long odbc_size_of_by_type_id(short odbc_type)
{
	long	size;

	switch ( odbc_type ) {
	case SQL_C_SSHORT :
	case SQL_C_SHORT:		// for 2.x backward compatibility
		size = sizeof(SQLSMALLINT);
		break;

	case SQL_C_USHORT :
		size = sizeof(SQLUSMALLINT);
		break;

	case SQL_C_SLONG :
	case SQL_C_LONG :		// for 2.x backward compatibility
		size = sizeof(SQLINTEGER);
		break;

	case SQL_C_ULONG :
		size = sizeof(SQLUINTEGER);
		break;

	case SQL_C_FLOAT :
		size = sizeof(SQLREAL);
		break;

	case SQL_C_DOUBLE :
		size = sizeof(SQLDOUBLE);
		break;

	case SQL_C_BIT :
		size = sizeof(SQLCHAR);
		break;

	case SQL_C_STINYINT :
		size = sizeof(SQLSCHAR);
		break;

	case SQL_C_UTINYINT :
	case SQL_C_TINYINT :	// for 2.x backward compatibility
		size = sizeof(SQLCHAR);
		break;

	case SQL_C_SBIGINT :
		size = sizeof(SQLBIGINT);
		break;

	case SQL_C_UBIGINT :
		size = sizeof(SQLUBIGINT);
		break;

	/*
	case SQL_C_BOOKMARK :
		size = sizeof(BOOKMARK);
		break;
	*/

	case SQL_C_TYPE_DATE :
	case SQL_C_DATE :		// for 2.x backward compatibility
		size = sizeof(SQL_DATE_STRUCT);
		break;

	case SQL_C_TYPE_TIME :
	case SQL_C_TIME :		// for 2.x backward compatibility
		size = sizeof(SQL_TIME_STRUCT);
		break;

	case SQL_C_TYPE_TIMESTAMP :
	case SQL_C_TIMESTAMP :	// for 2.x backward compatibility
		size = sizeof(SQL_TIMESTAMP_STRUCT);
		break;

	case SQL_C_NUMERIC :
		size = sizeof(SQL_NUMERIC_STRUCT);
		break;

	case SQL_C_GUID :
		size = sizeof(SQLGUID);
		break;

	default :
		size = 0;
		break;
	}

	return size;
}




PUBLIC short odbc_concise_to_verbose_type(short	type)
{
	switch ( type ) {
	case SQL_TYPE_DATE :
	case SQL_TYPE_TIME :
	case SQL_TYPE_TIMESTAMP :
	case SQL_DATE :	// for 2.x backward compatibility
	case SQL_TIME :	// for 2.x backward compatibility
	case SQL_TIMESTAMP :	// for 2.x backward compatibility
	    return SQL_DATETIME;

	case SQL_INTERVAL_MONTH :
	case SQL_INTERVAL_YEAR :
	case SQL_INTERVAL_YEAR_TO_MONTH :
	case SQL_INTERVAL_DAY :
	case SQL_INTERVAL_HOUR :
	case SQL_INTERVAL_MINUTE :
	case SQL_INTERVAL_SECOND :
	case SQL_INTERVAL_DAY_TO_HOUR :
	case SQL_INTERVAL_DAY_TO_MINUTE :
	case SQL_INTERVAL_DAY_TO_SECOND :
	case SQL_INTERVAL_HOUR_TO_MINUTE :
	case SQL_INTERVAL_HOUR_TO_SECOND :
	case SQL_INTERVAL_MINUTE_TO_SECOND :
	/*	
	case SQL_C_INTERVAL_MONTH :
	case SQL_C_INTERVAL_YEAR :
	case SQL_C_INTERVAL_YEAR_TO_MONTH :
	case SQL_C_INTERVAL_DAY :
	case SQL_C_INTERVAL_HOUR :
	case SQL_C_INTERVAL_MINUTE :
	case SQL_C_INTERVAL_SECOND :
	case SQL_C_INTERVAL_DAY_TO_HOUR :
	case SQL_C_INTERVAL_DAY_TO_MINUTE :
	case SQL_C_INTERVAL_DAY_TO_SECOND :
	case SQL_C_INTERVAL_HOUR_TO_MINUTE :
	case SQL_C_INTERVAL_HOUR_TO_SECOND :
	case SQL_C_INTERVAL_MINUTE_TO_SECOND :
	*/
		return SQL_INTERVAL;
	
	default :
		return type;
	}
}

PUBLIC short odbc_verbose_to_concise_type(short type,short	code)
{
	if ( type == SQL_DATETIME ) {
		switch ( code ) {
		case SQL_CODE_DATE :
			return SQL_TYPE_DATE;
		case SQL_CODE_TIME :
			return SQL_TYPE_TIME;
		case SQL_CODE_TIMESTAMP:
			return SQL_TYPE_TIMESTAMP;
		default : 
			return SQL_C_DEFAULT;
		}
	} else {
		return type;
	}
}

	

PUBLIC short odbc_subcode_type(short type )
{
	switch ( type ) {
	case SQL_TYPE_DATE : 	/* SQL_C_TYPE_DATE */
	case SQL_DATE : // for 2.x backward compatibility
		return SQL_CODE_DATE;

	case SQL_TYPE_TIME :	/* SQL_C_TYPE_TIME */
	case SQL_TIME :			// for 2.x backward compatibility
		return SQL_CODE_TIME;

	case SQL_TYPE_TIMESTAMP :	/* SQL_C_TYPE_TIMESTAMP */
	case SQL_TIMESTAMP :		// for 2.x backward compatibility
		return SQL_CODE_TIMESTAMP;

	case SQL_INTERVAL_MONTH :	/* SQL_C_INTERVAL_MONTH */
		return SQL_CODE_MONTH;

	case SQL_INTERVAL_YEAR :	/* SQL_C_INTERVAL_YEAR */
		return SQL_CODE_YEAR;

	case SQL_INTERVAL_YEAR_TO_MONTH :	/* SQL_C_INTERVAL_YEAR_TO_MONTH */
		return SQL_CODE_YEAR_TO_MONTH;

	case SQL_INTERVAL_DAY :		/*	SQL_C_INTERVAL_DAY */
		return SQL_CODE_DAY;

	case SQL_INTERVAL_HOUR :	/* SQL_C_INTERVAL_HOUR */
		return SQL_CODE_HOUR;

	case SQL_INTERVAL_MINUTE :	/* SQL_C_INTERVAL_MINUTE */
		return SQL_CODE_MINUTE;

	case SQL_INTERVAL_SECOND :	/* SQL_C_INTERVAL_SECOND */
		return SQL_CODE_SECOND;

	case SQL_INTERVAL_DAY_TO_HOUR :	/* SQL_C_INTERVAL_DAY_TO_HOUR */
		return SQL_CODE_DAY_TO_HOUR;

	case SQL_INTERVAL_DAY_TO_MINUTE :	/* SQL_C_INTERVAL_DAY_TO_MINUTE */
		return SQL_CODE_DAY_TO_MINUTE;

	case SQL_INTERVAL_DAY_TO_SECOND :	/* SQL_C_INTERVAL_DAY_TO_SECOND */
		return SQL_CODE_DAY_TO_SECOND;

	case SQL_INTERVAL_HOUR_TO_MINUTE :	/* SQL_C_INTERVAL_HOUR_TO_MINUTE */
		return SQL_CODE_HOUR_TO_MINUTE;

	case SQL_INTERVAL_HOUR_TO_SECOND :	/* SQL_C_INTERVAL_HOUR_TO_SECOND */
		return SQL_CODE_HOUR_TO_SECOND;

	case SQL_INTERVAL_MINUTE_TO_SECOND :	/* SQL_C_INTERVAL_MINUTE_TO_SECOND */
		return SQL_CODE_MINUTE_TO_SECOND;

	default :
		return 0;
	}
}

/************************************************************************
* name:  odbc_column_size
* arguments:
*       odbc_type
*       precision - by attribute domain
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC int odbc_column_size(short odbc_type, int precision)
{
	return odbc_display_size(odbc_type, precision);
}

/************************************************************************
* name:  odbc_buffer_length
* arguments:
*       odbc_type
*       precision - by attribute domain
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC int odbc_buffer_length(short odbc_type, int precision)
{
	return odbc_octet_length(odbc_type, precision);
}


/************************************************************************
* name:  odbc_decimal_digits
* arguments:
*       odbc_type
*       scale - by attribute domain
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC int odbc_decimal_digits(short odbc_type, int scale)
{
	if ( scale < 0 ) scale = 0;

    switch ( odbc_type ) {
    case SQL_CHAR :
    case SQL_VARCHAR :
    case SQL_LONGVARCHAR :
        return 0;
    case SQL_DECIMAL :
    case SQL_NUMERIC :
        return scale;
    case SQL_SMALLINT :
    case SQL_TINYINT :
    case SQL_INTEGER :
    case SQL_BIGINT :
        return 0;
    case SQL_REAL :
    case SQL_FLOAT :
    case SQL_DOUBLE :
        return 0;
    case SQL_BINARY :
    case SQL_VARBINARY :
    case SQL_LONGVARBINARY :
        return 0;
    case SQL_TYPE_DATE :
	case SQL_DATE :		// for 2.x backward compatibility
    case SQL_TYPE_TIME :
	case SQL_TIME :		// for 2.x backward compatibility
    case SQL_TYPE_TIMESTAMP :
	case SQL_TIMESTAMP :	// for 2.x backward compatibility
        return 0;
    default :
        return 0;
    }
}

/************************************************************************
* name:  odbc_octet_length
* arguments:
*       odbc_type
*       precision - by attribute domain
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC int odbc_octet_length(short odbc_type, int precision)
{
	if ( precision < 0 ) precision = 0;
	
    switch ( odbc_type ) {
    case SQL_CHAR :		/* SQL_C_CHAR */
    case SQL_VARCHAR :
    case SQL_LONGVARCHAR :
        return precision;
    case SQL_DECIMAL :
    case SQL_NUMERIC :	/* SQL_C_NUMERIC */
        return sizeof(SQL_NUMERIC_STRUCT);
    case SQL_SMALLINT :
	case SQL_C_SSHORT :
	case SQL_C_USHORT :
    case SQL_TINYINT :
	case SQL_C_STINYINT :
	case SQL_C_UTINYINT :
        return sizeof(short);
    case SQL_INTEGER :
	case SQL_C_ULONG :
	case SQL_C_SLONG :
    case SQL_BIGINT :
	case SQL_C_UBIGINT :
	case SQL_C_SBIGINT :
        return sizeof(long);
    case SQL_REAL :		/* SQL_C_FLOAT */
    case SQL_FLOAT :
        return sizeof(float);
    case SQL_DOUBLE :		/* SQL_C_DOUBLE */
        return sizeof(double);
    case SQL_BINARY :	/* SQL_C_BINARY */
    case SQL_VARBINARY :
    case SQL_LONGVARBINARY :
        return (int)(precision/8);
    case SQL_TYPE_DATE :	/* SQL_C_TYPE_DATE */
	case SQL_DATE :		// for 2.x backward compatibility
        return sizeof(SQL_DATE_STRUCT);
    case SQL_TYPE_TIME :	/* SQL_C_TYPE_TIME */
	case SQL_TIME :		// for 2.x backward compatibility
        return sizeof(SQL_TIME_STRUCT);
    case SQL_TYPE_TIMESTAMP :	/* SQL_C_TYPE_TIMESTAMP */
	case SQL_TIMESTAMP :	// for 2.x backward compatibility
        return sizeof(SQL_TIMESTAMP_STRUCT);
    default :
        return 0;
    }
}


/************************************************************************
* name:  odbc_num_prec_radix
* arguments:
*       odbc_type
*       precision - by attribute domain
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC int odbc_num_prec_radix(short odbc_type)
{
    if ( IS_STRING_TYPE(odbc_type) || IS_BINARY_TYPE(odbc_type) ) {
        return 0;
    } else {
        return 10;
    }
}


/************************************************************************
* name: odbc_display_size
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC int odbc_display_size(short odbc_type, int precision)
{
	if ( precision < 0 ) precision = 0;

    switch ( odbc_type ) {
    case SQL_CHAR :
    case SQL_VARCHAR :
    case SQL_LONGVARCHAR :
        return precision;
    case SQL_DECIMAL :
    case SQL_NUMERIC :
        return precision+2;
    case SQL_SMALLINT :
    case SQL_TINYINT :
        return 6;
    case SQL_INTEGER :
    case SQL_BIGINT :
        return 11;
    case SQL_REAL :
    case SQL_FLOAT :
        return 15;
    case SQL_DOUBLE :
        return 22;
    case SQL_BINARY :
    case SQL_VARBINARY :
    case SQL_LONGVARBINARY :
        return (int)(precision/8);
    case SQL_TYPE_DATE :
	case SQL_DATE :		// for 2.x backward compatibility
        return 10;
    case SQL_TYPE_TIME :
	case SQL_TIME :		// for 2.x backward compatibility
        return 12;
    case SQL_TYPE_TIMESTAMP :
	case SQL_TIMESTAMP :	// for 2.x backward compatibility
        return 23;
	/* XXX : deprecated 
	case SQL_UNI_OBJECT :
		return 15;
	*/
    default :
        return 0;
    }
}

/* 
 *		SQL_VARCHAR에서 precision이 MAX_PRECISON일 경우, 
 *		SQL_LONGVARCHAR로 mapping한다.
 */
PUBLIC short odbc_type_by_cci(T_CCI_U_TYPE	cci_type, int precision)
{
	switch ( cci_type ) {
	
	case CCI_U_TYPE_CHAR :
		return SQL_CHAR;
	case CCI_U_TYPE_STRING :
	case CCI_U_TYPE_VARNCHAR :
#ifdef DELPHI
		if ( precision == MAX_CUBRID_CHAR_LEN ) {
			return SQL_LONGVARCHAR;
		} else {
			return SQL_VARCHAR;
		}
#endif
		return SQL_VARCHAR;
	case CCI_U_TYPE_NCHAR:
		return SQL_CHAR;
	case CCI_U_TYPE_BIT :
		return SQL_BINARY;
	case CCI_U_TYPE_VARBIT :
		return SQL_VARBINARY;
	case CCI_U_TYPE_NUMERIC :
		return SQL_NUMERIC;
	case CCI_U_TYPE_INT :
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
		return SQL_TYPE_TIMESTAMP;
	case CCI_U_TYPE_MONETARY :
		return SQL_DOUBLE;
	case CCI_U_TYPE_SET:
	case CCI_U_TYPE_MULTISET :
	case CCI_U_TYPE_SEQUENCE :
		return SQL_VARCHAR;
	case CCI_U_TYPE_OBJECT :
		return SQL_CHAR;

	case CCI_U_TYPE_UNKNOWN :
	default:
		return -1;
	}
}

/************************************************************************
* name:  odbc_type_name
* arguments:
*	odbc_type	- odbc concise type
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC const char*	odbc_type_name(short odbc_type)
{
	char *pt;

	switch ( odbc_type ) {
	case SQL_CHAR :
		pt = "CHAR";
		break;
	case SQL_LONGVARCHAR :
#ifdef DELPHI
		pt = "STRING";
		break;
#endif
    case SQL_VARCHAR :
		pt = "VARCHAR";
		break;
    case SQL_DECIMAL :
		pt = "DECIMAL";
		break;
    case SQL_NUMERIC :
		pt = "NUMERIC";
		break;
    case SQL_SMALLINT :
		pt = "SMALLINT";
		break;
    case SQL_TINYINT :
        pt = "SMALLINT";
		break;
    case SQL_INTEGER :
		pt = "INTEGER";
		break;
    case SQL_BIGINT :
		pt = "INTEGER";
		break;
    case SQL_REAL :
		pt = "REAL";
		break;
    case SQL_FLOAT :
		pt = "FLOAT";
		break;
    case SQL_DOUBLE :
        pt = "DOUBLE";
		break;
    case SQL_BINARY :
		pt = "BIT";
		break;
    case SQL_VARBINARY :
    case SQL_LONGVARBINARY :
        pt = "BIT VARYING";
		break;
    case SQL_TYPE_DATE :
	case SQL_DATE :		// for 2.x backward compatibility
        pt = "DATE";
		break;
    case SQL_TYPE_TIME :
	case SQL_TIME :		// for 2.x backward compatibility
        pt = "TIME";
		break;
    case SQL_TYPE_TIMESTAMP :
	case SQL_TIMESTAMP :	// for 2.x backward compatibility
        pt = "TIMESTAMP";
		break;
	case SQL_UNI_MONETARY :
		pt = "MONETARY";
		break;
	/* XXX : deprecated 
	case SQL_UNI_SET :
		pt = "SET";
		break;
	case SQL_UNI_OBJECT :
		pt = "OBJECT";
		break;
	*/
    default :
        pt = NULL;
		break;
	}
	return pt;
}



/************************************************************************
 * name: odbc_type_to_cci_a_type
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
  *		default에 대해서 -1을 return 한다.
 ************************************************************************/	
PUBLIC T_CCI_A_TYPE odbc_type_to_cci_a_type(short c_type)
{
	switch (c_type) {
	case SQL_C_CHAR :
		return CCI_A_TYPE_STR;

	case SQL_C_BINARY :
		return CCI_A_TYPE_BIT;

	case SQL_C_SHORT :	// for 2.x backward compatibility
	case SQL_C_SSHORT :
	case SQL_C_USHORT :
	case SQL_C_LONG :	// for 2.x backward compatibility
	case SQL_C_SLONG :
	case SQL_C_ULONG :
	case SQL_C_STINYINT :
	case SQL_C_UTINYINT :
	case SQL_C_TINYINT :	// for 2.x backward compatibility
	case SQL_C_SBIGINT :
	case SQL_C_UBIGINT :
		return CCI_A_TYPE_INT;

	case SQL_C_FLOAT :
		return CCI_A_TYPE_FLOAT;

	case SQL_C_DOUBLE :
		return CCI_A_TYPE_DOUBLE;

	case SQL_C_NUMERIC :
		return CCI_A_TYPE_STR;

	case SQL_C_TYPE_DATE :
	case SQL_C_DATE :		// for 2.x backward compatibility
	case SQL_C_TYPE_TIME :
	case SQL_C_TIME :		// for 2.x backward compatibility
	case SQL_C_TYPE_TIMESTAMP :
	case SQL_C_TIMESTAMP :	// for 2.x backward compatibility
		return CCI_A_TYPE_DATE;

	// set과 object type은 display용으로 string으로 match된다.
	case SQL_C_UNI_SET :
	case SQL_C_UNI_OBJECT :
		return CCI_A_TYPE_STR ;
	default:
		return -1;
	}
}

/************************************************************************
 * name: odbc_type_to_cci_u_type
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 *		check : set, object, monetary type에 대해서 고혀되지 않았다.
 ************************************************************************/	
PUBLIC T_CCI_U_TYPE odbc_type_to_cci_u_type(short sql_type)
{
	switch (sql_type) {

	case SQL_CHAR :
		return CCI_U_TYPE_CHAR;
	case SQL_VARCHAR :
	case SQL_LONGVARCHAR :
		return CCI_U_TYPE_STRING;

	case SQL_BINARY :
		return CCI_U_TYPE_BIT;
	case SQL_VARBINARY :
	case SQL_LONGVARBINARY :
		return CCI_U_TYPE_VARBIT;

	case SQL_DECIMAL :
	case SQL_NUMERIC :
		return CCI_U_TYPE_NUMERIC;

	case SQL_SMALLINT :
	case SQL_INTEGER :
	case SQL_TINYINT :
	case SQL_BIGINT :
		return CCI_U_TYPE_INT;

	case SQL_FLOAT :
	case SQL_REAL :
		return CCI_U_TYPE_FLOAT;

	case SQL_DOUBLE :
		return CCI_U_TYPE_DOUBLE;

	case SQL_TYPE_DATE :
	case SQL_DATE :		// for 2.x backward compatibility
		return CCI_U_TYPE_DATE;
	case SQL_TYPE_TIME :
	case SQL_TIME :		// for 2.x backward compatibility
		return CCI_U_TYPE_TIME;
	case SQL_TYPE_TIMESTAMP:
	case SQL_TIMESTAMP :	// for 2.x backward compatibility
		return CCI_U_TYPE_TIMESTAMP;
	default :
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
PUBLIC void *odbc_value_to_cci(void *c_value,short c_type, long c_length, 
							   short c_precision, short c_scale)
{
	void	*value = NULL;

	switch ( c_type ) {

	/*---------------------------------------------------------------
	 *					INTEGRAL TYPE
	 *--------------------------------------------------------------*/
	case SQL_C_SHORT :		// for 2.x backward compatibility
	case SQL_C_SSHORT :
	case SQL_C_USHORT :
		value = UT_ALLOC(sizeof(int));
		*(int*)value = *(short*)c_value;
		break;

	case SQL_C_STINYINT :
	case SQL_C_UTINYINT :
	case SQL_C_TINYINT :		// for 2.x backward compatibility
		value = UT_ALLOC(sizeof(int));
		*(int*)value = *(char*)c_value;
		break;
	
	case SQL_C_LONG :		// for 2.x backward compatibility
	case SQL_C_SLONG :
	case SQL_C_ULONG :
	case SQL_C_SBIGINT :		// warning : __int64에 대해서 고려 안됨
	case SQL_C_UBIGINT :
		value = UT_ALLOC(sizeof(int));
		*(int*)value = *(long*)c_value;
		break;

	/*---------------------------------------------------------------
	 *					floating point type
	 *--------------------------------------------------------------*/
	case SQL_C_FLOAT :
		value = UT_ALLOC(sizeof(float));
		*(float*)value = *(float*)c_value;
		break;

	case SQL_C_DOUBLE :
		value = UT_ALLOC(sizeof(double));
		*(double*)value = *(double*)c_value;
		break;

	/*---------------------------------------------------------------
	 *					char & binary type
	 *--------------------------------------------------------------*/
	case SQL_C_CHAR :
		value = UT_MAKE_STRING(c_value, c_length);
		break;
	
	case SQL_C_BINARY :
		value = UT_ALLOC(sizeof(T_CCI_BIT));
		((T_CCI_BIT*)value)->size = c_length;
		((T_CCI_BIT*)value)->buf = UT_MAKE_BINARY(c_value, c_length);
		break;

	/*---------------------------------------------------------------
	 *					date & time type
	 *--------------------------------------------------------------*/
	case SQL_C_TYPE_DATE :
	case SQL_C_DATE :	// for 2.x backward compatibility
		value = UT_ALLOC(sizeof(T_CCI_DATE));
		((T_CCI_DATE*)value)->yr = ((SQL_DATE_STRUCT*)c_value)->year;
		((T_CCI_DATE*)value)->mon = ((SQL_DATE_STRUCT*)c_value)->month;
		((T_CCI_DATE*)value)->day = ((SQL_DATE_STRUCT*)c_value)->day;
		break;

	case SQL_C_TYPE_TIME :
	case SQL_C_TIME :	// for 2.x backward compatibility
		value = UT_ALLOC(sizeof(T_CCI_DATE));
		((T_CCI_DATE*)value)->hh = ((SQL_TIME_STRUCT*)c_value)->hour;
		((T_CCI_DATE*)value)->mm = ((SQL_TIME_STRUCT*)c_value)->minute;
		((T_CCI_DATE*)value)->ss = ((SQL_TIME_STRUCT*)c_value)->second;
		break;

	case SQL_C_TYPE_TIMESTAMP :
	case SQL_C_TIMESTAMP :		// for 2.x backward compatibility
		value = UT_ALLOC(sizeof(T_CCI_DATE));
		((T_CCI_DATE*)value)->yr = ((SQL_TIMESTAMP_STRUCT*)c_value)->year;
		((T_CCI_DATE*)value)->mon = ((SQL_TIMESTAMP_STRUCT*)c_value)->month;
		((T_CCI_DATE*)value)->day = ((SQL_TIMESTAMP_STRUCT*)c_value)->day;
		((T_CCI_DATE*)value)->hh = ((SQL_TIMESTAMP_STRUCT*)c_value)->hour;
		((T_CCI_DATE*)value)->mm = ((SQL_TIMESTAMP_STRUCT*)c_value)->minute;
		((T_CCI_DATE*)value)->ss = ((SQL_TIMESTAMP_STRUCT*)c_value)->second;
		break;

	/*---------------------------------------------------------------
	 *					numeric type
	 *--------------------------------------------------------------*/
	case SQL_C_NUMERIC :
		{
			bc_num	num1 = NULL, num2 = NULL, base = NULL, res_num = NULL, res_tmp = NULL;
			unsigned char	*pt;
			char	buf[16];
			short	i;
		
			init_numbers();

			str2num(&res_num, "0", 0);
			str2num(&base, "256", 0);
		
			for ( pt = ((SQL_NUMERIC_STRUCT*)c_value)->val+(SQL_MAX_NUMERIC_LEN-1), i =0 ; 
					i < SQL_MAX_NUMERIC_LEN; --pt, ++i ) {
				sprintf(buf, "%d", *pt);
				str2num(&num2, buf, 0);

				num1 = res_num;		res_num = NULL;
				bc_multiply(num1, base, &res_tmp, 0);
				bc_add(res_tmp, num2, &res_num, 0);

				free_num(&num1);	// free_num - assign null to num1
				free_num(&num2);
				free_num(&res_tmp);
			}

			value = num2str(res_num);
			if ( c_scale > 0 ) {
				value = UT_REALLOC(value, strlen(value) + 2 ); // for period
				pt = value;
//				pt += c_precision - c_scale;  // OR pt += strlen(value) - scale;
				pt += strlen(value) - c_scale;
				memmove(pt+1, pt, c_scale +1);
				*pt = '.';
			}

			if ( ((SQL_NUMERIC_STRUCT*)c_value)->sign == 0 ) { // negative
				value = UT_REALLOC(value, strlen((char*)value) +2 ); // for sign
				memmove( (char*)value+1, value, strlen((char*)value) +1);
				((char*)value)[0] = '-';
			}

			free_num(&num1);
			free_num(&num2);
			free_num(&res_tmp);
			free_num(&res_num);
			free_num(&base);
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
PUBLIC void odbc_value_to_cci2(void *sql_value_root, int index, void *c_value,short c_type, long c_length, 
							   short c_precision, short c_scale)
{

	switch ( c_type ) {

	/*---------------------------------------------------------------
	 *					INTEGRAL TYPE
	 *--------------------------------------------------------------*/
	case SQL_C_SHORT :		// for 2.x backward compatibility
	case SQL_C_SSHORT :
	case SQL_C_USHORT :
		*((int*)sql_value_root + index)= *(short*)c_value;
		break;

	case SQL_C_STINYINT :
	case SQL_C_UTINYINT :
	case SQL_C_TINYINT :		// for 2.x backward compatibility
		*((int*)sql_value_root + index)= *(char*)c_value;
		break;
	
	case SQL_C_LONG :		// for 2.x backward compatibility
	case SQL_C_SLONG :
	case SQL_C_ULONG :
	case SQL_C_SBIGINT :		// warning : __int64에 대해서 고려 안됨
	case SQL_C_UBIGINT :
		*((int*)sql_value_root + index)= *(long*)c_value;
		break;

	/*---------------------------------------------------------------
	 *					floating point type
	 *--------------------------------------------------------------*/
	case SQL_C_FLOAT :
		*((float*)sql_value_root + index) = *(float*)c_value;
		break;

	case SQL_C_DOUBLE :
		*((double*)sql_value_root + index) = *(double*)c_value;
		break;

	/*---------------------------------------------------------------
	 *					char & binary type
	 *--------------------------------------------------------------*/
	case SQL_C_CHAR :
		*((char**)sql_value_root + index) = UT_MAKE_STRING(c_value, c_length);
		break;
	
	case SQL_C_BINARY :
		((T_CCI_BIT*)sql_value_root + index)->size = c_length;
		((T_CCI_BIT*)sql_value_root + index)->buf = UT_MAKE_BINARY(c_value, c_length);
		break;

	/*---------------------------------------------------------------
	 *					date & time type
	 *--------------------------------------------------------------*/
	case SQL_C_TYPE_DATE :
	case SQL_C_DATE :	// for 2.x backward compatibility
		((T_CCI_DATE*)sql_value_root + index)->yr = ((SQL_DATE_STRUCT*)c_value)->year;
		((T_CCI_DATE*)sql_value_root + index)->mon = ((SQL_DATE_STRUCT*)c_value)->month;
		((T_CCI_DATE*)sql_value_root + index)->day = ((SQL_DATE_STRUCT*)c_value)->day;
		break;

	case SQL_C_TYPE_TIME :
	case SQL_C_TIME :	// for 2.x backward compatibility
		((T_CCI_DATE*)sql_value_root + index)->hh = ((SQL_TIME_STRUCT*)c_value)->hour;
		((T_CCI_DATE*)sql_value_root + index)->mm = ((SQL_TIME_STRUCT*)c_value)->minute;
		((T_CCI_DATE*)sql_value_root + index)->ss = ((SQL_TIME_STRUCT*)c_value)->second;
		break;

	case SQL_C_TYPE_TIMESTAMP :
	case SQL_C_TIMESTAMP :		// for 2.x backward compatibility
		((T_CCI_DATE*)sql_value_root + index)->yr = ((SQL_TIMESTAMP_STRUCT*)c_value)->year;
		((T_CCI_DATE*)sql_value_root + index)->mon = ((SQL_TIMESTAMP_STRUCT*)c_value)->month;
		((T_CCI_DATE*)sql_value_root + index)->day = ((SQL_TIMESTAMP_STRUCT*)c_value)->day;
		((T_CCI_DATE*)sql_value_root + index)->hh = ((SQL_TIMESTAMP_STRUCT*)c_value)->hour;
		((T_CCI_DATE*)sql_value_root + index)->mm = ((SQL_TIMESTAMP_STRUCT*)c_value)->minute;
		((T_CCI_DATE*)sql_value_root + index)->ss = ((SQL_TIMESTAMP_STRUCT*)c_value)->second;
		break;

	/*---------------------------------------------------------------
	 *					numeric type
	 *--------------------------------------------------------------*/
	case SQL_C_NUMERIC :
		{
			bc_num	num1 = NULL, num2 = NULL, base = NULL, res_num = NULL, res_tmp = NULL;
			unsigned char	*pt;
			char*			value;
			char	buf[16];
			short	i;
		
			init_numbers();

			str2num(&res_num, "0", 0);
			str2num(&base, "256", 0);
		
			for ( pt = ((SQL_NUMERIC_STRUCT*)c_value)->val+(SQL_MAX_NUMERIC_LEN-1), i =0 ; 
					i < SQL_MAX_NUMERIC_LEN; ++pt, ++i ) {
				sprintf(buf, "%d", *pt);
				str2num(&num2, buf, 0);

				num1 = res_num;		res_num = NULL;
				bc_multiply(num1, base, &res_tmp, 0);
				bc_add(res_tmp, num2, &res_num, 0);

				free_num(&num1);	// free_num - assign null to num1
				free_num(&num2);
				free_num(&res_tmp);
			}

			value = num2str(res_num);
			if ( c_scale > 0 ) {
				value = UT_REALLOC(value, strlen(value) + 2 ); // for period
				pt = value;
				pt += c_precision - c_scale;  // OR pt += strlen(value) - scale;
				memmove(pt+1, pt, c_scale +1);
				*pt = '.';
			}

			if ( ((SQL_NUMERIC_STRUCT*)c_value)->sign == 0 ) { // negative
				value = UT_REALLOC(value, strlen((char*)value) +2 ); // for sign
				memmove( (char*)value+1, value, strlen((char*)value) +1);
				((char*)value)[0] = '-';
			}

			/* NUMERIC은 CCI_A_TYPE_STR로 conversion한다. */
			*((char**)sql_value_root + index) = value;

			free_num(&num1);
			free_num(&num2);
			free_num(&res_tmp);
			free_num(&res_num);
			free_num(&base);
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
PUBLIC long cci_value_to_odbc(void  *c_value, short concise_type,
							  short precision, short scale, 
							  long  buffer_length, UNI_CCI_A_TYPE *cci_value, 
							  T_CCI_A_TYPE a_type)
{
	long		length = 0;

	switch ( concise_type ) {
		
	/*---------------------------------------------------------------
	 *					INTEGRAL TYPE
	 *--------------------------------------------------------------*/
	case SQL_C_SHORT :		// for 2.x backward compatibility
	case SQL_C_SSHORT :
	case SQL_C_USHORT :
		*(short*)c_value = cci_value->i;
		length = sizeof(short);
		break;

	case SQL_C_STINYINT :
	case SQL_C_UTINYINT :
	case SQL_C_TINYINT :		// for 2.x backward compatibility
		*(char*)c_value = cci_value->i;
		length = sizeof(char);
		break;
	
	case SQL_C_LONG :		// for 2.x backward compatibility
	case SQL_C_SLONG :
	case SQL_C_ULONG :
	case SQL_C_SBIGINT :
	case SQL_C_UBIGINT :	// warning : __int64, yet not implemented
		*(long*)c_value = cci_value->i;
		length = sizeof(long);
		break;
	
	
	/*---------------------------------------------------------------
	 *					floating point type
	 *--------------------------------------------------------------*/
	case SQL_C_FLOAT :
		*(float*)c_value = cci_value->f;
		length = sizeof(float);
		break;

	case SQL_C_DOUBLE :
		*(double*)c_value = cci_value->d;
		length = sizeof(double);
		break;

	/*---------------------------------------------------------------
	 *					char & binary type
	 *--------------------------------------------------------------*/
	case SQL_C_CHAR :
		str_value_assign(cci_value->str, c_value, buffer_length, &length);
		break;
	
	case SQL_C_BINARY :
		bin_value_assign(cci_value->bit.buf, cci_value->bit.size, 
						c_value, buffer_length, &length);
		break;

	/*---------------------------------------------------------------
	 *					date & time type
	 *--------------------------------------------------------------*/
	case SQL_C_TYPE_DATE :
	case SQL_C_DATE :		// for 2.x backward compatibility
		((SQL_DATE_STRUCT*)c_value)->year = cci_value->date.yr;
		((SQL_DATE_STRUCT*)c_value)->month = cci_value->date.mon;
		((SQL_DATE_STRUCT*)c_value)->day = cci_value->date.day;
		length = sizeof(SQL_DATE_STRUCT);
		break;

	case SQL_C_TYPE_TIME :
	case SQL_C_TIME :		// for 2.x backward compatibility
		((SQL_TIME_STRUCT*)c_value)->hour = cci_value->date.hh;
		((SQL_TIME_STRUCT*)c_value)->minute = cci_value->date.mm;
		((SQL_TIME_STRUCT*)c_value)->second = cci_value->date.ss;
		length = sizeof(SQL_TIME_STRUCT);
		break;

	case SQL_C_TYPE_TIMESTAMP :
	case SQL_C_TIMESTAMP :		// for 2.x backward compatibility
		((SQL_TIMESTAMP_STRUCT*)c_value)->year = cci_value->date.yr;
		((SQL_TIMESTAMP_STRUCT*)c_value)->month = cci_value->date.mon;
		((SQL_TIMESTAMP_STRUCT*)c_value)->day = cci_value->date.day;
		((SQL_TIMESTAMP_STRUCT*)c_value)->hour = cci_value->date.hh;
		((SQL_TIMESTAMP_STRUCT*)c_value)->minute = cci_value->date.mm;
		((SQL_TIMESTAMP_STRUCT*)c_value)->second = cci_value->date.ss;
		((SQL_TIMESTAMP_STRUCT*)c_value)->fraction = 0;
		length = sizeof(SQL_TIMESTAMP_STRUCT);
		break;

	/*---------------------------------------------------------------
	 *					numeric type
	 *--------------------------------------------------------------*/
	case SQL_C_NUMERIC :
		{
			bc_num	num1 = NULL, num2 = NULL, quot = NULL, rem = NULL, res_tmp = NULL;
			char	*pt, *pt2, *tmp_str_num = NULL;
			char	str[64];  /* numeric value that is removed a period
							   * cf) The max precision of numeric is 38 in CUBRID
							   */
			short	i;
			unsigned char	num_add_zero = 0;
			

			((SQL_NUMERIC_STRUCT*)c_value)->precision = (unsigned char)precision;
			((SQL_NUMERIC_STRUCT*)c_value)->scale = (unsigned char)scale;

			if ( cci_value->str[0] == '-' ) {
				((SQL_NUMERIC_STRUCT*)c_value)->sign = 0;	// negative
				pt = cci_value->str +1;
			} else {
				((SQL_NUMERIC_STRUCT*)c_value)->sign = 1;	// positive
				pt = cci_value->str;
			}
			// pt means the first vaild digit position

			pt2 = strchr(pt, '.');
			if ( pt2 != NULL ) {
				strncpy(str, pt, pt2 - pt);
				str[pt2-pt] = '\0';
				++pt2;
				strcat(str, pt2);
				num_add_zero = scale - strlen(pt2);
				// add additional '0' for scale
				for ( pt = str + strlen(str), i = 1;  i <= num_add_zero ; ++pt, ++i ) {
					*pt = '0';
				}
				*pt = '\0';
			} else {
				strcpy(str, pt);
			}

			init_numbers();

			str2num(&num1, str, 0);
			str2num(&num2, "256", 0);

			
			for (i=0, tmp_str_num = NULL; i < SQL_MAX_NUMERIC_LEN; ++i ) {
				bc_divmod(num1, num2, &quot, &rem, 0);

				tmp_str_num = num2str(rem);

				((SQL_NUMERIC_STRUCT*)c_value)->val[i] = (unsigned char)atoi(tmp_str_num);
				
				NA_FREE(tmp_str_num);
				free_num(&rem);
				free_num(&num1);
				num1 = quot;
			}

			free_num(&num1);
			free_num(&num2);
			free_num(&quot);
			free_num(&rem);
			NA_FREE(tmp_str_num);

			length = sizeof(SQL_NUMERIC_STRUCT);

			break;
		}
	}

	return length;
}


			
PUBLIC VALUE_CONTAINER* create_value_container()
{
	VALUE_CONTAINER *value = NULL;

	value = (VALUE_CONTAINER*)UT_ALLOC(sizeof(VALUE_CONTAINER));

	if ( value != NULL ) {
		memset(value, 0, sizeof(VALUE_CONTAINER));
	}
	return value;
}

PUBLIC void clear_value_container(VALUE_CONTAINER *value)
{
	if ( value->type == SQL_C_CHAR || value->type == SQL_C_BINARY ) {
		if ( value->value.dummy != NULL ) {
			UT_FREE(value->value.dummy);
		}
	}
}

PUBLIC void free_value_container(VALUE_CONTAINER *value)
{
	if ( value == NULL )  return;

	clear_value_container(value);

	UT_FREE(value);
}

/*
 *	partially not implemented 
 *			BINARY, date type
 *	Not implemented about
 *			BIT, NUMERIC, OBJECT, SET, TINYINT, BIGINT
 */
PUBLIC RETCODE odbc_value_converter(VALUE_CONTAINER* target_value, 
									VALUE_CONTAINER* src_value)
{
	char				buf[BUF_SIZE];

	// NULL value
	if ( src_value->length == 0 ) {
		target_value->length = 0;
		target_value->value.dummy = NULL;
		return ODBC_SUCCESS;
	}

	// SQL_C_DEFAULT
	if ( target_value->type == SQL_C_DEFAULT ) {
		target_value->type = src_value->type;
	}

	switch ( src_value->type ) {
	case SQL_C_CHAR :
		switch ( target_value->type ) {
		case SQL_C_CHAR :
		case SQL_C_BINARY :
			target_value->value.str = UT_MAKE_STRING(src_value->value.str, -1);
			target_value->length = src_value->length;
			break;
		case SQL_C_SHORT :		// for 2.x backward compatibility
		case SQL_C_SSHORT :
		case SQL_C_USHORT :
			target_value->value.s = (short)atoi(src_value->value.str);
			target_value->length = sizeof(short);
			break;
		case SQL_C_LONG :		// for 2.x backward compatibility
		case SQL_C_SLONG :
		case SQL_C_ULONG :
			target_value->value.l = atol(src_value->value.str);
			target_value->length = sizeof(long);
			break;
		case SQL_C_FLOAT :
			target_value->value.f = (float)atof(src_value->value.str);
			target_value->length = sizeof(float);
			break;
		case SQL_C_DOUBLE :
			target_value->value.d = atof(src_value->value.str);
			target_value->length = sizeof(double);
			break;
		case SQL_C_NUMERIC :
		case SQL_C_BIT :
		case SQL_C_TYPE_DATE :
		case SQL_C_TYPE_TIME :
		case SQL_C_TYPE_TIMESTAMP :
		case SQL_C_DATE :		// for 2.x backward compatibility
		case SQL_C_TIME :		// for 2.x backward compatibility
		case SQL_C_TIMESTAMP :	// for 2.x backward compatibility
			return ODBC_NOT_IMPLEMENTED;
		default :
			return ODBC_UNKNOWN_TYPE;
		}
		break;
	case SQL_C_SHORT :
	case SQL_C_SSHORT :
	case SQL_C_USHORT :
		switch ( target_value->type ) {
		case SQL_C_CHAR :
			sprintf(buf, "%hd", src_value->value.s);
			target_value->value.str = UT_MAKE_STRING(buf, -1);
			target_value->length = strlen(buf) +1;
			break;
		case SQL_C_BINARY :
			target_value->value.bin = UT_ALLOC(sizeof(short));
			bin_value_assign(&src_value->value.s, sizeof(short), 
						target_value->value.bin, sizeof(short), &(target_value->length));
			break;
		case SQL_C_SHORT :
		case SQL_C_SSHORT :
		case SQL_C_USHORT :
			target_value->value.s = src_value->value.s;
			target_value->length = sizeof(short);
			break;
		case SQL_C_LONG :
		case SQL_C_SLONG :
		case SQL_C_ULONG :
			target_value->value.l = (long)src_value->value.s;
			target_value->length = sizeof(long);
			break;
		case SQL_C_FLOAT :
			target_value->value.f = (float)src_value->value.s;
			target_value->length = sizeof(float);
			break;
		case SQL_C_DOUBLE :
			target_value->value.d = (double)src_value->value.s;
			target_value->length = sizeof(double);
			break;
		case SQL_C_NUMERIC :
		case SQL_C_BIT :
		case SQL_C_TYPE_DATE :
		case SQL_C_TYPE_TIME :
		case SQL_C_TYPE_TIMESTAMP :
		case SQL_C_DATE :		// for 2.x backward compatibility
		case SQL_C_TIME :		// for 2.x backward compatibility
		case SQL_C_TIMESTAMP :	// for 2.x backward compatibility
			return ODBC_NOT_IMPLEMENTED;
		default :
			return ODBC_UNKNOWN_TYPE;
		}
		break;

		////////////////
	case SQL_C_LONG :
	case SQL_C_SLONG :
	case SQL_C_ULONG :
		switch ( target_value->type ) {
		case SQL_C_CHAR :
			sprintf(buf, "%ld", src_value->value.l);
			target_value->value.str = UT_MAKE_STRING(buf, -1);
			target_value->length = strlen(buf) +1;
			break;
		case SQL_C_BINARY :
			target_value->value.bin = UT_ALLOC(sizeof(long));
			bin_value_assign(&src_value->value.l, sizeof(long), 
							target_value->value.bin, sizeof(long), &(target_value->length));
			break;
		case SQL_C_SHORT :
		case SQL_C_SSHORT :
		case SQL_C_USHORT :
			target_value->value.s = (short)src_value->value.l;
			target_value->length = sizeof(short);
			break;
		case SQL_C_LONG :
		case SQL_C_SLONG :
		case SQL_C_ULONG :
			target_value->value.l = src_value->value.l;
			target_value->length = sizeof(long);
			break;
		case SQL_C_FLOAT :
			target_value->value.f = (float)src_value->value.l;
			target_value->length = sizeof(float);
			break;
		case SQL_C_DOUBLE :
			target_value->value.d = (double)src_value->value.l;
			target_value->length = sizeof(double);
			break;
		case SQL_C_NUMERIC :
		case SQL_C_BIT :
		case SQL_C_TYPE_DATE :
		case SQL_C_TYPE_TIME :
		case SQL_C_TYPE_TIMESTAMP :
		case SQL_C_DATE :		// for 2.x backward compatibility
		case SQL_C_TIME :		// for 2.x backward compatibility
		case SQL_C_TIMESTAMP :	// for 2.x backward compatibility
			return ODBC_NOT_IMPLEMENTED;
		default :
			return ODBC_UNKNOWN_TYPE;
		}
		break;

	case SQL_C_FLOAT :
		switch ( target_value->type ) {
		case SQL_C_CHAR :
			sprintf(buf, "%f", src_value->value.f);
			target_value->value.str = UT_MAKE_STRING(buf, -1);
			target_value->length = strlen(buf) +1;
			break;
		case SQL_C_BINARY :
			target_value->value.bin = UT_ALLOC(sizeof(float));
			target_value->length = sizeof(float);
			memcpy(target_value->value.bin, &src_value->value.s, sizeof(float));
			break;
		case SQL_C_SHORT :
		case SQL_C_SSHORT :
		case SQL_C_USHORT :
			target_value->value.s = (short)src_value->value.f;
			target_value->length = sizeof(short);
			break;
		case SQL_C_LONG :
		case SQL_C_SLONG :
		case SQL_C_ULONG :
			target_value->value.l = (long)src_value->value.f;
			target_value->length = sizeof(long);
			break;
		case SQL_C_FLOAT :
			target_value->value.f = src_value->value.f;
			target_value->length = sizeof(float);
			break;
		case SQL_C_DOUBLE :
			target_value->value.d = (double)src_value->value.f;
			target_value->length = sizeof(double);
			break;
		case SQL_C_NUMERIC :
		case SQL_C_BIT :
		case SQL_C_TYPE_DATE :
		case SQL_C_TYPE_TIME :
		case SQL_C_TYPE_TIMESTAMP :
		case SQL_C_DATE :		// for 2.x backward compatibility
		case SQL_C_TIME :		// for 2.x backward compatibility
		case SQL_C_TIMESTAMP :	// for 2.x backward compatibility
			return ODBC_NOT_IMPLEMENTED;
		default :
			return ODBC_UNKNOWN_TYPE;
		}
		break;

	case SQL_C_DOUBLE :
		switch ( target_value->type ) {
		case SQL_C_CHAR :
			sprintf(buf, "%lf", src_value->value.d);
			target_value->length = strlen(buf) +1;
			break;
		case SQL_C_BINARY :
			target_value->value.bin = UT_ALLOC(sizeof(double));
			bin_value_assign(&src_value->value.d, sizeof(double), 
				target_value->value.bin, sizeof(double), &(target_value->length));
			break;
		case SQL_C_SHORT :
		case SQL_C_SSHORT :
		case SQL_C_USHORT :
			target_value->value.s = (short)src_value->value.d;
			target_value->length = sizeof(short);
			break;
		case SQL_C_LONG :
		case SQL_C_SLONG :
		case SQL_C_ULONG :
			target_value->value.l = (long)src_value->value.d;
			target_value->length = sizeof(long);
			break;
		case SQL_C_FLOAT :
			target_value->value.f = (float)src_value->value.d;
			target_value->length = sizeof(float);
			break;
		case SQL_C_DOUBLE :
			target_value->value.d = src_value->value.d;
			target_value->length = sizeof(double);
			break;
		case SQL_C_NUMERIC :
		case SQL_C_BIT :
		case SQL_C_TYPE_DATE :
		case SQL_C_TYPE_TIME :
		case SQL_C_TYPE_TIMESTAMP :
		case SQL_C_DATE :		// for 2.x backward compatibility
		case SQL_C_TIME :		// for 2.x backward compatibility
		case SQL_C_TIMESTAMP :	// for 2.x backward compatibility
			return ODBC_NOT_IMPLEMENTED;
		default :
			return ODBC_UNKNOWN_TYPE;
		}
		break;

	case SQL_C_BINARY :
		switch ( target_value->type ) {
		case SQL_C_BINARY :
			target_value->value.bin = UT_ALLOC(src_value->length);
			memcpy(target_value->value.bin, src_value->value.bin, src_value->length);
			target_value->length = src_value->length;
			break;
		case SQL_C_CHAR :
			target_value->value.str = UT_ALLOC(src_value->length +1);
			memcpy(target_value->value.str, src_value->value.bin, src_value->length);
			target_value->value.str[src_value->length] = '\0';
			break;
			
		case SQL_C_SHORT :
		case SQL_C_SSHORT :
		case SQL_C_USHORT :
			memcpy(&target_value->value.s, src_value->value.bin, sizeof(short));
			target_value->length = sizeof(short);
			break;
		case SQL_C_LONG :
		case SQL_C_SLONG :
		case SQL_C_ULONG :
			memcpy(&target_value->value.l, src_value->value.bin, sizeof(long));
			target_value->length = sizeof(long);
			break;
		case SQL_C_FLOAT :
			memcpy(&target_value->value.f , src_value->value.bin, sizeof(float));
			target_value->length = sizeof(float);
			break;
		case SQL_C_DOUBLE :
			memcpy(&target_value->value.d, src_value->value.bin, sizeof(double));
			target_value->length = sizeof(double);
			break;
		case SQL_C_NUMERIC :
		case SQL_C_BIT :
		case SQL_C_TYPE_DATE :
		case SQL_C_TYPE_TIME :
		case SQL_C_TYPE_TIMESTAMP :
		case SQL_C_DATE :		// for 2.x backward compatibility
		case SQL_C_TIME :		// for 2.x backward compatibility
		case SQL_C_TIMESTAMP :	// for 2.x backward compatibility
			return ODBC_NOT_IMPLEMENTED;
		default :
			return ODBC_UNKNOWN_TYPE;
		}
		break;

	case SQL_C_TYPE_DATE :
	case SQL_C_DATE :		// for 2.x backward compatibility
		switch ( target_value->type ) {
		case SQL_C_CHAR :
			sprintf(buf, "%d-%d-%d", target_value->value.date.year, 
									target_value->value.date.month,
									target_value->value.date.day);
			target_value->value.str = UT_MAKE_STRING(buf, -1);
			target_value->length = strlen(buf) +1;
			break;
		case SQL_C_BINARY :
			target_value->value.bin = UT_ALLOC(sizeof(SQL_DATE_STRUCT));
			bin_value_assign(&src_value->value.date, sizeof(SQL_DATE_STRUCT), 
					target_value->value.bin, sizeof(SQL_DATE_STRUCT), &(target_value->length));
			break;
		case SQL_C_TYPE_DATE :
		case SQL_C_DATE :		// for 2.x backward compatibility
			target_value->value.date.year = src_value->value.date.year;
			target_value->value.date.month = src_value->value.date.month;
			target_value->value.date.day = src_value->value.date.day;
			target_value->length = sizeof(SQL_DATE_STRUCT);
			break;
		case SQL_C_TYPE_TIMESTAMP :
		case SQL_C_TIMESTAMP :	// for 2.x backward compatibility
			target_value->value.ts.year = src_value->value.date.year;
			target_value->value.ts.month = src_value->value.date.month;
			target_value->value.ts.day = src_value->value.date.day;
			target_value->value.ts.hour = 0;
			target_value->value.ts.minute = 0;
			target_value->value.ts.second  = 0;
			target_value->value.ts.fraction = 0;
			target_value->length = sizeof(SQL_TIMESTAMP_STRUCT);
			break;
		case SQL_C_SHORT :
		case SQL_C_SSHORT :
		case SQL_C_USHORT :
		case SQL_C_LONG :
		case SQL_C_SLONG :
		case SQL_C_ULONG :
		case SQL_C_FLOAT :
		case SQL_C_DOUBLE :
		case SQL_C_TYPE_TIME :
		case SQL_C_NUMERIC :
		case SQL_C_BIT :
			return ODBC_INVALID_TYPE_CONVERSION;
		default :
			return ODBC_UNKNOWN_TYPE;
		}
		break;

	case SQL_C_TYPE_TIME :
	case SQL_C_TIME :		// for 2.x backward compatibility
		switch ( target_value->type ) {
		case SQL_C_CHAR :
			sprintf(buf, "%d:%d:%d", src_value->value.time.hour,
										src_value->value.time.minute,
										src_value->value.time.second);
									
			target_value->value.str = UT_MAKE_STRING(buf, -1);
			target_value->length = strlen(buf) +1;
			break;
		case SQL_C_BINARY :
			target_value->value.bin = UT_ALLOC(sizeof(SQL_TIME_STRUCT));
			bin_value_assign(&src_value->value.time, sizeof(SQL_TIME_STRUCT),
				target_value->value.bin, sizeof(SQL_TIME_STRUCT), &(target_value->length));
			break;
		case SQL_C_TYPE_TIME :
		case SQL_C_TIME :		// for 2.x backward compatibility
			target_value->value.time.hour = src_value->value.time.hour;
			target_value->value.time.minute = src_value->value.time.minute;
			target_value->value.time.second = src_value->value.time.second;
			target_value->length = sizeof(SQL_TIME_STRUCT);
			break;
		case SQL_C_TYPE_TIMESTAMP :
		case SQL_C_TIMESTAMP :		// for 2.x backward compatibility
			target_value->value.ts.year = 0;
			target_value->value.ts.month = 0;
			target_value->value.ts.day = 0;
			target_value->value.ts.hour = src_value->value.time.hour;
			target_value->value.ts.minute = src_value->value.time.minute;
			target_value->value.ts.second  = src_value->value.time.second;
			target_value->value.ts.fraction = 0;
			target_value->length = sizeof(SQL_TIMESTAMP_STRUCT);
			break;
		case SQL_C_SHORT :
		case SQL_C_SSHORT :
		case SQL_C_USHORT :
		case SQL_C_LONG :
		case SQL_C_SLONG :
		case SQL_C_ULONG :
		case SQL_C_FLOAT :
		case SQL_C_DOUBLE :
		case SQL_C_NUMERIC :
		case SQL_C_BIT :
		case SQL_C_TYPE_DATE :
		case SQL_C_DATE :	// for 2.x backward compatibility
			return ODBC_INVALID_TYPE_CONVERSION;
		default :
			return ODBC_UNKNOWN_TYPE;
		}
		break;

	case SQL_C_TYPE_TIMESTAMP :
	case SQL_C_TIMESTAMP :		// for 2.x backward compatibility
		switch ( target_value->type ) {
		case SQL_C_CHAR :
			sprintf(buf, "%d-%d-%d %d:%d:%d", src_value->value.ts.year,
											src_value->value.ts.month,
											src_value->value.ts.day,
											src_value->value.ts.hour,
											src_value->value.ts.minute,
											src_value->value.ts.second);
			target_value->value.str = UT_MAKE_STRING(buf, -1);
			target_value->length = strlen(buf) +1;
			break;
		case SQL_C_BINARY :
			target_value->value.bin = UT_ALLOC(sizeof(SQL_TIMESTAMP_STRUCT));
			bin_value_assign(&src_value->value.ts, sizeof(SQL_TIMESTAMP_STRUCT),
				target_value->value.bin, sizeof(SQL_TIMESTAMP_STRUCT), &(target_value->length));
			break;
		case SQL_C_TYPE_DATE :
		case SQL_C_DATE :		// for 2.x backward compatibility
			target_value->value.date.year = src_value->value.ts.year;
			target_value->value.date.month = src_value->value.ts.month;
			target_value->value.date.day = src_value->value.ts.day;
			target_value->length = sizeof(SQL_DATE_STRUCT);
			break;
		case SQL_C_TYPE_TIME :
		case SQL_C_TIME :		// for 2.x backward compatibility
			target_value->value.time.hour = src_value->value.ts.hour;
			target_value->value.time.minute = src_value->value.ts.minute;
			target_value->value.time.second = src_value->value.ts.second;
			target_value->length = sizeof(SQL_TIME_STRUCT);
			break;
		case SQL_C_TYPE_TIMESTAMP :
		case SQL_C_TIMESTAMP :		// for 2.x backward compatibility
			target_value->value.ts.year = src_value->value.ts.year;
			target_value->value.ts.month = src_value->value.ts.month;
			target_value->value.ts.day = src_value->value.ts.day;
			target_value->value.ts.hour = src_value->value.ts.hour;
			target_value->value.ts.minute = src_value->value.ts.minute;
			target_value->value.ts.second = src_value->value.ts.second;
			target_value->value.ts.fraction = 0;
			target_value->length = sizeof(SQL_TIMESTAMP_STRUCT);
			break;
		case SQL_C_SHORT :
		case SQL_C_SSHORT :
		case SQL_C_USHORT :
		case SQL_C_LONG :
		case SQL_C_SLONG :
		case SQL_C_ULONG :
		case SQL_C_FLOAT :
		case SQL_C_DOUBLE :
		case SQL_C_NUMERIC :
		case SQL_C_BIT :
			return ODBC_INVALID_TYPE_CONVERSION;
		default :
			return ODBC_UNKNOWN_TYPE;
		}
		break;

	case SQL_C_BIT :
	case SQL_C_NUMERIC :
		return ODBC_NOT_IMPLEMENTED;
	default :
		return ODBC_UNKNOWN_TYPE;
	}
	
	return ODBC_OK;
}


/************************************************************************
 * name:  odbc_date_type_backward
 * arguments:
 *	type - c type이든, sql type이든 상관 없다.
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC short odbc_date_type_backward(short type )
{
	switch ( type ) {
	case SQL_C_TYPE_DATE :
		return SQL_C_DATE;
	case SQL_C_TYPE_TIME :
		return SQL_C_TIME;
	case SQL_C_TYPE_TIMESTAMP :
		return SQL_C_TIMESTAMP;
		
	default :
		return type;

	}
}
	
