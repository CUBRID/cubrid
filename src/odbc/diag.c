#include		"portable.h"
#include		"util.h"
#include		"sqlext.h"
#include		"diag.h"
#include		"env.h"
#include		"conn.h"
#include		"stmt.h"

#define		ISO_CLASS_ORIGIN			"ISO 9075"
#define		ODBC_CLASS_ORIGIN		"ODBC 3.0"
#define		DIAG_PREFIX				"[CUBRID][ODBC CUBRID Driver]"

PRIVATE const char* get_diag_message(ODBC_DIAG_RECORD *record);
PRIVATE char* get_diag_subclass_origin(char* sql_state);
PRIVATE void odbc_init_diag(ODBC_DIAG *diag);
PRIVATE void odbc_clear_diag(ODBC_DIAG *diag);
PRIVATE void odbc_init_diag_record(ODBC_DIAG_RECORD *node);
PRIVATE void odbc_clear_diag_record(ODBC_DIAG_RECORD *node);
PRIVATE short is_header_field(short field_id);
PRIVATE ODBC_DIAG_RECORD*	find_diag_record(ODBC_DIAG *diag, int rec_number);
PRIVATE char* get_diag_class_origin(char *sql_state);

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

PRIVATE ODBC_ERROR_MAP odbc_3_0_error_map[] = {
	{-1000, "01000", "General warning"},
	{-1001, "01001", "Cursor operation conflict"},
	{-1002, "01002", "Disconnect error"},
	{-1003, "01003", "NULL value eliminated in set function"},
	{-1004, "01004", "String data, right truncated"},
	{-1006, "01006", "Privilege not revoked"},
	{-1007, "01007", "Privilege not granted"},
	{-1100, "01S00", "Invalid connection string attribute"},
	{-1101, "01S01", "Error in row"},
	{-1102, "01S02", "Option value changed"},
	{-1106, "01S06", "Attempt to fetch before the result set retunred the first rowset"},
	{-1107, "01S07", "Fractional truncation"},
	{-1108, "01S08", "Error saving File DSN"},
	{-1109, "01S09", "Invalid keyword"},
	{-1201, "07001", "Wrong number of parameters"},
	{-1202, "07002", "COUNT field incorrect"},
	{-1205, "07005", "Prepared statement not a cursor-specification"},
	{-1206, "07006", "Restricted data type attribute violation"},
	{-1209, "07009", "Invalid descriptor index"},
	{-1301, "07S01", "Invalid use of default parameter"},
	{-1401, "08001", "Client unable to establish connection"},
	{-1402, "08002", "Connection name in use"},
	{-1403, "08003", "Connection does not exist"},
	{-1404, "08004", "Server rejected the connection"},
	{-1407, "08007", "Connection failure during transaction"},
	{-1521, "08S01", "Communication link failure"},
	{-1601, "21S01", "Insert value list does not match column list"},
	{-1602, "21S02", "Degree of derived table does not match column list"},
	{-1701, "22001", "String data, right truncated"},
	{-1702, "22002", "Indicator variable required but not supplied"},
	{-1703, "22003", "Numeric value out of range"},
	{-1707, "22007", "Invalid datetime format"},
	{-1708, "22008", "Datetime field overflow"},
	{-1712, "22012", "Division by zero"},
	{-1715, "22015", "Invalid field overflow"},
	{-1718, "22018", "Invalid charcter value for cast specification"},
	{-1719, "22019", "Invalid escape character"},
	{-1725, "22025", "Invalid escape sequence"},
	{-1726, "22026", "String data, length mismatch"},
	{-1800, "23000", "Integrity constraint violation"},
	{-1900, "24000", "Invalid cursor state"},
	{-2000, "25000", "Invalid transaction state"},
	{-2101, "25S01", "Transaction state"},
	{-2102, "25S02", "Transaction is still active"},
	{-2103, "25S03", "Transaction is rolled back"},
	{-2200, "28000", "Invalid authorization specification"},
	{-2300, "34000", "Invalid cursor name"},
	{-2400, "3C000", "Duplicate cursor name"},
	{-2500, "3D000", "Invalid catalog name"},
	{-2600, "3F000", "Invalid schema name"},
	{-2701, "40001", "Serialization failure"},
	{-2702, "40002", "Integrity constraint violation"},
	{-2703, "40003", "Statement completion unknown"},
	{-2800, "42000", "Syntax error or access violation"},
	{-2901, "42S01", "Base table or view already exists"},
	{-2902, "42S02", "Base table or view not found"},
	{-2911, "42S11", "Index already exists"},
	{-2912, "42S12", "Index not found"},
	{-2921, "42S21", "Column already exists"},
	{-2922, "42S22", "Column not found"},
	{-3000, "44000", "WITH CHECK OPTION violation"},
	{-4000, "HY000", "General error"},
	{-4001, "HY001", "Memory allocation error"},
	{-4003, "HY003", "Invalid application buffer type"},
	{-4004, "HY004", "Invalid SQL data type"},
	{-4007, "HY007", "Associated statement is not prepared"},
	{-4008, "HY008", "Operation canceled"},
	{-4009, "HY009", "Invalid use of null pointer"},
	{-4010, "HY010", "Function sequence error"},
	{-4011, "HY011", "Attribute cannot be set now"},
	{-4012, "HY012", "Invalid transaction operation code"},
	{-4013, "HY013", "Memory management error"},
	{-4014, "HY014", "Limit on the number of handles exceeded"},
	{-4015, "HY015", "No cursor name available"},
	{-4016, "HY016", "Cannot modify an implementation row descriptor"},
	{-4017, "HY017", "Invalid use of an automatically allocated descriptor handle"},
	{-4018, "HY018", "Server declined cancel request"},
	{-4019, "HY019", "Non-character and non-binary data sent in pieces"},
	{-4020, "HY020", "Attempt to concatenate a null value"},
	{-4021, "HY021", "Inconsistent descriptor information"},
	{-4024, "HY024", "Invalid attribute value"},
	{-4090, "HY090", "Invalid string or buffer length"},
	{-4091, "HY091", "Invalid descriptor field identifier"},
	{-4092, "HY092", "Invalid attribute/option identifier"},
	{-4095, "HY095", "Function type out of range"},
	{-4096, "HY096", "Invalid information type"},
	{-4097, "HY097", "Column type out of range"},
	{-4098, "HY098", "Scope type out of range"},
	{-4099, "HY099", "Nullable type out of range"},
	{-4100, "HY100", "Uniqueness option type out of range"},
	{-4101, "HY101", "Accuracy option type out of range"},
	{-4103, "HY103", "Invalid retrieval code"},
	{-4104, "HY104", "Invalid precision or scale value"},
	{-4105, "HY105", "Invalid parameter type"},
	{-4106, "HY106", "Fetch type out of range"},
	{-4107, "HY107", "Row value out of range"},
	{-4109, "HY109", "Invalid cursor position"},
	{-4110, "HY110", "Invalid driver completion"},
	{-4111, "HY111", "Invalid bookmark value"},
	{-4200, "HYC00", "Optional feature not implemented"},
	{-4300, "HYT00", "Timeout expired"},
	{-4301, "HYT01", "Connection timeout expired"},
	{-4401, "IM001", "Driver does not support this function"},
	{-4402, "IM002", "Data source name not found and no default driver specified"},
	{-4403, "IM003", "Specified driver could not be loaded"},
	{-4404, "IM004", "Driver's SQLAllocHandle on SQL_HANDLE_ENV failed"},
	{-4405, "IM005", "Driver's SQLAllocHandle on SQL_HANDLE_DBC failed"},
	{-4406, "IM006", "Driver's SQLSetConnectAttr failed"},
	{-4407, "IM007", "No data source or driver specified; dalo prohibited"},
	{-4408, "IM008", "Dialog failed"},
	{-4409, "IM009", "Unable to load translation DLL"},
	{-4410, "IM010", "Data source name too long"},
	{-4411, "IM011", "Driver name too long"},
	{-4412, "IM012", "DRIVER keyword syntax error"},
	{-4413, "IM013", "Trace file error"},
	{-4414, "IM014", "Invalid name of File DSN"},
	{-4415, "IM015", "Corrupt file data source"},

	// CUBRID ODBC
	{-10001,    "UN001", "General Error"},
	{-10002,	"UN002", "Warning"},
	{-10003,	"UN003", "Unknown Error"},
	{-10004,	"UN004", "Null value"},
	{-10006,	"UN006", "CAS Error"}, 
	{-10007,	"UN007", "No more data"},
	{-10008,	"UN008", "Memory allocation error"},
	{-10009,	"UN009", "Not Implemented"},
	{-10010,	"UN010", "Unknown type"},
	{-10011,	"UN011", "Invalid type conversion"},
	{-10012,	"UN012", "SQL Mode is read-only."},
	{-10050,	"UN050", "Fetch error"},
	{-10051,	"UN051", "Fetch error"},
	{-10052,	"UN052", "Fetch error"},
	
	/*
	{-52,	"UN052", "Unknown type"},
	{-53,	"UN053", "Yet not implemented"},
	{-54,	"UN054", "socket error"},
	{-55,	"UN055", "send error"},
	{-56,	"UN056", "receive error"},
	*/
	// cas cci error
	{-12001,	"CA001", "CUBRID DB error"},
	{-12002,	"CA002", "Invalid Connection handle"},
	{-12003,	"CA003", "Memory allocation error"},
	{-12004,	"CA004", "Communication error"},
	{-12005,	"CA005", "No more data"},
	{-12006,	"CA006", "Unknown Tranaction Type"},
	{-12007,	"CA007", "Invalid string parameter"},
	{-12008,	"CA008", "Type conversion error"},
	{-12009,	"CA009", "Invalid binding error"},
	{-12010,	"CA010", "Invalid type"},
	{-12011,	"CA011", "Parameter binding error"},
	{-12012,	"CA012", "Invalid database parameter name"},
	{-12013,	"CA013", "Invalid column index"},
	{-12014,	"CA014", "Invalid schema type"},
	{-12015,	"CA015", "File open error"},
	{-12016,	"CA016", "Connection error"},
	{-12017,	"CA017", "Connection handle creation error"},
	{-12018,	"CA018", "Invalid request handle"},
	{-12019,	"CA019", "Invalid cursor position"},
	{-12020,	"CA020", "Object is not valid"},
	{-12021,	"CA021", "CAS error"},
	{ODBC_CC_ER_HOSTNAME, "CA022", "Unknown host name"},
	{-12099,	"CA099", "Not implemented"},
	{ODBC_CAS_ER_DBMS,				"CA100", "[CAS]Database connection error"},
	{ODBC_CAS_ER_INTERNAL ,			"CA101", "[CAS]"},
	{ODBC_CAS_ER_NO_MORE_MEMORY ,	"CA102", "[CAS]Memory allocation error"},
	{ODBC_CAS_ER_COMMUNICATION ,	"CA103", "[CAS]Communication error"},
	{ODBC_CAS_ER_ARGS ,				"CA104", "[CAS]Invalid argument"},
	{ODBC_CAS_ER_TRAN_TYPE ,		"CA105", "[CAS]Unknown tranaction type"},
	{ODBC_CAS_ER_SRV_HANDLE ,		"CA106", "[CAS]"},
	{ODBC_CAS_ER_NUM_BIND ,			"CA107", "[CAS]Parameter binding error"},
	{ODBC_CAS_ER_UNKNOWN_U_TYPE ,	"CA108", "[CAS]Parameter binding error"},
	{ODBC_CAS_ER_DB_VALUE ,			"CA109", "[CAS]Can't make DB_VALUE"},
	{ODBC_CAS_ER_TYPE_CONVERSION ,	"CA110", "[CAS]Type conversion error"},
	{ODBC_CAS_ER_PARAM_NAME ,		"CA111", "[CAS]Invalid database parameter name"},
	{ODBC_CAS_ER_NO_MORE_DATA ,		"CA112", "[CAS]Cursor position error"},
	{ODBC_CAS_ER_OBJECT ,			"CA113", "[CAS]Object is not valid"},
	{ODBC_CAS_ER_OPEN_FILE ,		"CA114", "[CAS]File open error"},
	{ODBC_CAS_ER_SCHEMA_TYPE ,		"CA115", "[CAS]Invalid schema type"},
	{ODBC_CAS_ER_VERSION,			"CA116", "[CAS]Version mismatch"},
	{ODBC_CAS_ER_FREE_SERVER,			"CA117", "[CAS]Cannot process the request. Try again later."},
	{ODBC_CAS_ER_NOT_AUTHORIZED_CLIENT,	"CA118", "[CAS]Authorization error"},
	{ODBC_CAS_ER_QUERY_CANCEL,			"CA119", "[CAS]"},
	{ODBC_CAS_ER_NOT_COLLECTION,		"CA120", "[CAS]The attribute domain must be the set type."},
	{ODBC_CAS_ER_COLLECTION_DOMAIN,		"CA121", "[CAS]The domain of a set must be the same data type."},
	{0, NULL, NULL}
};

/************************************************************************
 * name: odbc_alloc_diag
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC ODBC_DIAG *odbc_alloc_diag(void)
{
	ODBC_DIAG	*diag = NULL;

	diag = UT_ALLOC(sizeof(ODBC_DIAG));
	odbc_init_diag(diag);
	
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
PUBLIC void odbc_free_diag(ODBC_DIAG *diag, int option)
{
	if ( diag == NULL ) return;

	switch ( option ) {
	case INIT :
		odbc_init_diag(diag);
		break;
	case RESET :
		odbc_clear_diag(diag);
		odbc_init_diag(diag);
		break;
	case FREE_MEMBER :
		odbc_clear_diag(diag);
		break;
	case FREE_ALL :
		odbc_clear_diag(diag);
		UT_FREE(diag);
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
PUBLIC ODBC_DIAG_RECORD *odbc_alloc_diag_record(void)
{
	ODBC_DIAG_RECORD	*diag_record = NULL;

	diag_record = UT_ALLOC(sizeof(ODBC_DIAG_RECORD));
	odbc_init_diag_record(diag_record);
	
	return diag_record;
}

/************************************************************************
 * name: odbc_free_diag_record
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC void odbc_free_diag_record(ODBC_DIAG_RECORD *diag_record, int option)
{
	if ( diag_record == NULL ) return;

	switch ( option ) {
	case INIT :
		odbc_init_diag_record(diag_record);
		break;
	case RESET :
		odbc_clear_diag_record(diag_record);
		odbc_init_diag_record(diag_record);
		break;
	case FREE_MEMBER :
		odbc_clear_diag_record(diag_record);
		break;
	case FREE_ALL :
		odbc_clear_diag_record(diag_record);
		UT_FREE(diag_record);
		break;
	}
}

PUBLIC void odbc_free_diag_record_list(ODBC_DIAG_RECORD *diag_record)
{
	ODBC_DIAG_RECORD	*current, *del;

	current = diag_record;

	while ( current != NULL ) {
		del = current;
		current = current->next;
		odbc_free_diag_record(del, ODBC_DIAG_RECORD_FREE_ALL);
	}
}

PUBLIC void odbc_move_diag(ODBC_DIAG *target_diag, ODBC_DIAG *src_diag)
{
	ODBC_DIAG_RECORD	*tmp;

	if ( src_diag == NULL || target_diag == NULL ) return;

	tmp = src_diag->record;

	if ( tmp == NULL ) return;
	else {
		while ( tmp->next != NULL ) tmp = tmp->next;
	}

	tmp->next = target_diag->record;
	target_diag->record = src_diag->record;
	src_diag->record = NULL;

	target_diag->rec_number += src_diag->rec_number;

	return;
}



/* recored added to head */
PUBLIC void odbc_set_diag(ODBC_DIAG *diag, char *sql_state, int native_code, 
						   char *message)
{
	ODBC_DIAG_RECORD *record = NULL;

	if ( diag == NULL ) return;

	record = odbc_alloc_diag_record();
	if ( record == NULL ) return;

	++(diag->rec_number);

	record->sql_state = UT_MAKE_STRING(sql_state, -1);
	record->message = UT_MAKE_STRING(message, -1);
	record->native_code = native_code;
	record->number = diag->rec_number;

	record->next = diag->record;
	diag->record = record;

}

/* i_code = internal error code
 */
PUBLIC void odbc_set_diag_by_icode(ODBC_DIAG *diag, int i_code, char *message)
{
	ODBC_DIAG_RECORD *record = NULL;
	const char*	sql_state;
	int	i;

	if ( diag == NULL ) return;

	for ( i = 0; ; ++i ) {
		if ( odbc_3_0_error_map[i].code == i_code ||
			 odbc_3_0_error_map[i].code == 0 )
			break;
	}
	sql_state = odbc_3_0_error_map[i].status;

	if ( sql_state == NULL ) {
		sql_state = "HY000";
	}


	record = odbc_alloc_diag_record();
	if ( record == NULL ) return;

	++(diag->rec_number);


	record->sql_state = UT_MAKE_STRING(sql_state, -1);
	record->message = UT_MAKE_STRING(message, -1);
	record->number = diag->rec_number;

	record->next = diag->record;
	diag->record = record;

}

PUBLIC void odbc_set_diag_by_cci(ODBC_DIAG *diag, int cci_err_code, char *message)
{
	odbc_set_diag_by_icode(diag, cci_err_code + ODBC_CC_ER_OFFSET, message);

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
PUBLIC RETCODE odbc_get_diag_field(short		handle_type,
								   void			*handle, 
								   short		rec_number,
								   short		diag_identifier,
								   void			*diag_info_ptr,
								   short		buffer_length,
								   long			*string_length_ptr)
{
	ODBC_ENV	*env;
	ODBC_DIAG_RECORD	*record;

	RETCODE		status = ODBC_SUCCESS, rc; 
	char		*pt;
	char		empty_str[1] = "";

	env = (ODBC_ENV*)handle;

	if ( (diag_identifier == SQL_DIAG_CURSOR_ROW_COUNT ||
		  diag_identifier == SQL_DIAG_DYNAMIC_FUNCTION ||
		  diag_identifier == SQL_DIAG_DYNAMIC_FUNCTION_CODE ||
		  diag_identifier == SQL_DIAG_ROW_COUNT) && 
		  handle_type != SQL_HANDLE_STMT ) {
		return ODBC_ERROR;
	}
		
	if ( is_header_field(diag_identifier) == _TRUE_ ) {
		switch ( diag_identifier ) {
		case SQL_DIAG_CURSOR_ROW_COUNT :
		case SQL_DIAG_ROW_COUNT :
			if ( diag_info_ptr != NULL )
				*(long*)diag_info_ptr = ((ODBC_STATEMENT*)handle)->current_tpl_pos;

			if ( string_length_ptr != NULL ) {
				*string_length_ptr = sizeof(long);
			}
			break;

		case SQL_DIAG_DYNAMIC_FUNCTION :
		case SQL_DIAG_DYNAMIC_FUNCTION_CODE :
			/* Yet not implemented */
			return ODBC_ERROR;

		case SQL_DIAG_NUMBER :
			if ( diag_info_ptr != NULL )
				*(long*)diag_info_ptr = env->diag->rec_number;

			if ( string_length_ptr != NULL ) {
				*string_length_ptr = sizeof(long);
			}
			break;

		case SQL_DIAG_RETURNCODE :
			if ( diag_info_ptr != NULL )
				*(short*)diag_info_ptr = env->diag->retcode;

			if ( string_length_ptr != NULL ) {
				*string_length_ptr = sizeof(short);
			}
			break;
		default :
			return ODBC_ERROR;
		}
	} else {
		/* record field */
		if ( rec_number <= 0 || rec_number > env->diag->rec_number ) {
			return ODBC_NO_DATA;
		}
		record = find_diag_record(env->diag, rec_number);
		if ( record == NULL ) {
			return ODBC_NO_DATA;
		}

		switch ( diag_identifier ) {
		case SQL_DIAG_CLASS_ORIGIN :
			pt = get_diag_class_origin(record->sql_state);
			rc = str_value_assign(pt, diag_info_ptr, buffer_length, string_length_ptr);
			if ( rc == ODBC_SUCCESS_WITH_INFO ) {
				status = rc;
			}
			break;

		case SQL_DIAG_COLUMN_NUMBER : /* yet not implemeted */
			if ( diag_info_ptr != NULL )
				*(long*)diag_info_ptr = SQL_COLUMN_NUMBER_UNKNOWN;

			if ( string_length_ptr != NULL ) {
				*string_length_ptr = sizeof(long);
			}
			break;

		case SQL_DIAG_CONNECTION_NAME : /* yet not implemeted */
			return ODBC_NO_DATA;

		case SQL_DIAG_MESSAGE_TEXT :
			{
				char *mesg = NULL;


				pt = (char*)get_diag_message(record);
				if ( pt == NULL ) pt = empty_str;

				mesg = UT_MAKE_STRING("[CUBRID][CUBRID ODBC Driver]", -1);
				mesg = UT_APPEND_STRING(mesg, pt, -1);

				rc = str_value_assign(mesg, diag_info_ptr, buffer_length, string_length_ptr);
				if ( rc == ODBC_SUCCESS_WITH_INFO ) {
					status = rc;
				}

				NC_FREE(mesg);
			}
			break;

		case SQL_DIAG_NATIVE : 
			if ( diag_info_ptr != NULL )
				*(long*)diag_info_ptr = record->native_code;

			if ( string_length_ptr != NULL ) {
				*string_length_ptr = sizeof(long);
			}
			break;


		case SQL_DIAG_ROW_NUMBER : /* yet not implemeted */
			if ( diag_info_ptr != NULL )
				*(long*)diag_info_ptr = SQL_ROW_NUMBER_UNKNOWN;

			if ( string_length_ptr != NULL ) {
				*string_length_ptr = sizeof(long);
			}
			break;

		case SQL_DIAG_SERVER_NAME :
			if ( handle_type == SQL_HANDLE_ENV ) {
				pt = empty_str;
			} else {
				if ( handle_type == SQL_HANDLE_DBC ) {
					pt = ((ODBC_CONNECTION*)handle)->data_source;
				} else if ( handle_type == SQL_HANDLE_STMT ) {
					pt = ((ODBC_STATEMENT*)handle)->conn->data_source;
				} else if ( handle_type == SQL_HANDLE_DESC ) {
					pt = ((ODBC_DESC*)handle)->conn->data_source;
				}
				rc = str_value_assign(pt, diag_info_ptr, buffer_length, string_length_ptr);
				if ( rc == ODBC_SUCCESS_WITH_INFO ) {
					status = rc;
				}
			}
			break;

		case SQL_DIAG_SQLSTATE :
			if ( record->sql_state != NULL ) {
				pt = record->sql_state;
			} else {
				pt = empty_str;
			}
			rc = str_value_assign(pt, diag_info_ptr, buffer_length, string_length_ptr);
			if ( rc == ODBC_SUCCESS_WITH_INFO ) {
				status = rc;
			}
			break;

		case SQL_DIAG_SUBCLASS_ORIGIN :
			pt = get_diag_subclass_origin(record->sql_state);
			if ( pt == NULL ) {
				pt = empty_str;
			}
			rc = str_value_assign(pt, diag_info_ptr, buffer_length, string_length_ptr);
			if ( rc == ODBC_SUCCESS_WITH_INFO ) {
				status = rc;
			}
			break;

		default :
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
PUBLIC RETCODE odbc_get_diag_rec(short handle_type,
								 void*	handle,
								 short	rec_number,
								 char	*sqlstate,
								 long	*native_error_ptr,
								 char	*message_text,
								 short	buffer_length,
								 long	*text_length_ptr)
{
	RETCODE status = ODBC_SUCCESS, rc;
	ODBC_DIAG_RECORD	*record;
	ODBC_ENV	*env;
	char		*pt;
	char *mesg = NULL;
	char		empty_str[1] = "";

	env = (ODBC_ENV*)handle;

	if ( rec_number <= 0 ) return ODBC_ERROR;

	/* Sequence error record is not implemented */
	if ( rec_number > env->diag->rec_number ) return ODBC_NO_DATA;
	if ( (record = find_diag_record(env->diag, rec_number)) == NULL ) {
		return ODBC_NO_DATA;
	}

	if ( sqlstate != NULL ) {
		strcpy(sqlstate, record->sql_state);
	}
	if ( native_error_ptr != NULL ) {
		*native_error_ptr = record->native_code;
	}

	pt = (char*)get_diag_message(record);
	if ( pt == NULL ) pt = empty_str;

	mesg = UT_MAKE_STRING("[CUBRID][CUBRID ODBC Driver]", -1);
	mesg = UT_APPEND_STRING(mesg, pt, -1);

	rc = str_value_assign(mesg, message_text, buffer_length, text_length_ptr);
	if ( rc == ODBC_SUCCESS_WITH_INFO ) {
		status = rc;
	}
	NC_FREE(mesg);

	return status;
}

PRIVATE const char* get_diag_message(ODBC_DIAG_RECORD *record)
{
	int i;

	if ( record->message != NULL && record->message[0] != '\0' ) {
		return record->message;
	} else {
		if ( record->native_code == 0 ) {
			for ( i = 0; ; ++i) {
				if ( odbc_3_0_error_map[i].code == 0 ) 
					break;
				if ( strncmp(odbc_3_0_error_map[i].status, record->sql_state, 5) == 0 )
					break;
			}
			return odbc_3_0_error_map[i].msg;
		} else {
			for ( i = 0; ; ++i) {
				if ( odbc_3_0_error_map[i].code == 0 ) 
					break;
				if ( odbc_3_0_error_map[i].code == record->native_code )
					break;
			}
			return odbc_3_0_error_map[i].msg;
		}
	}
}



PRIVATE char* get_diag_subclass_origin(char* sql_state)
{
	if ( sql_state == NULL || sql_state[0] == '\0' ) {
		return NULL;
	}

	if (	strcmp(sql_state, "01S00") == 0 ||
			strcmp(sql_state, "01S01") == 0 ||
			strcmp(sql_state, "01S02") == 0 ||
			strcmp(sql_state, "01S06") == 0 ||
			strcmp(sql_state, "01S07") == 0 ||
			strcmp(sql_state, "07S01") == 0 ||
			strcmp(sql_state, "08S01") == 0 ||
			strcmp(sql_state, "21S01") == 0 ||
			strcmp(sql_state, "21S02") == 0 ||
			strcmp(sql_state, "25S01") == 0 ||
			strcmp(sql_state, "25S02") == 0 ||
			strcmp(sql_state, "25S03") == 0 ||
			strcmp(sql_state, "42S01") == 0 ||
			strcmp(sql_state, "42S02") == 0 ||
			strcmp(sql_state, "42S11") == 0 ||
			strcmp(sql_state, "42S12") == 0 ||
			strcmp(sql_state, "42S21") == 0 ||
			strcmp(sql_state, "42S22") == 0 ||
			strcmp(sql_state, "HY095") == 0 ||
			strcmp(sql_state, "HY097") == 0 ||
			strcmp(sql_state, "HY098") == 0 ||
			strcmp(sql_state, "HY099") == 0 ||
			strcmp(sql_state, "HY100") == 0 ||
			strcmp(sql_state, "HY101") == 0 ||
			strcmp(sql_state, "HY105") == 0 ||
			strcmp(sql_state, "HY107") == 0 ||
			strcmp(sql_state, "HY109") == 0 ||
			strcmp(sql_state, "HY110") == 0 ||
			strcmp(sql_state, "HY111") == 0 ||
			strcmp(sql_state, "HYT00") == 0 ||
			strcmp(sql_state, "HYT01") == 0 ||
			strcmp(sql_state, "IM001") == 0 ||
			strcmp(sql_state, "IM002") == 0 ||
			strcmp(sql_state, "IM003") == 0 ||
			strcmp(sql_state, "IM004") == 0 ||
			strcmp(sql_state, "IM005") == 0 ||
			strcmp(sql_state, "IM006") == 0 ||
			strcmp(sql_state, "IM007") == 0 ||
			strcmp(sql_state, "IM008") == 0 ||
			strcmp(sql_state, "IM010") == 0 ||
			strcmp(sql_state, "IM011") == 0 ||
			strcmp(sql_state, "IM012") == 0  ) {
		return ODBC_CLASS_ORIGIN;
	}
	return NULL;
}

PRIVATE ODBC_DIAG_RECORD*	find_diag_record(ODBC_DIAG *diag, int rec_number)
{
	ODBC_DIAG_RECORD	*record = NULL;

	record = diag->record;

	while ( record != NULL )
		if ( record->number == rec_number ) 
			break;
		else 
			record = record->next;
		
	return record;
}


	
PRIVATE void odbc_init_diag(ODBC_DIAG *diag)
{
	if ( diag == NULL ) return;

	memset(diag, 0, sizeof(ODBC_DIAG));
}

PRIVATE void odbc_clear_diag(ODBC_DIAG *diag)
{
	if ( diag == NULL ) return;

	odbc_free_diag_record_list(diag->record);
}

PRIVATE void odbc_init_diag_record(ODBC_DIAG_RECORD *node)
{
	if ( node != NULL ) {
		memset(node, 0, sizeof(ODBC_DIAG_RECORD));
	}
}

PRIVATE void odbc_clear_diag_record(ODBC_DIAG_RECORD *node)
{
	if ( node == NULL ) return;
	NC_FREE(node->message);
	NC_FREE(node->sql_state);
}

PRIVATE short is_header_field(short field_id)
{
	switch ( field_id ) {
	case SQL_DIAG_CURSOR_ROW_COUNT :
	case SQL_DIAG_DYNAMIC_FUNCTION :
	case SQL_DIAG_DYNAMIC_FUNCTION_CODE :
	case SQL_DIAG_NUMBER :
	case SQL_DIAG_RETURNCODE :
	case SQL_DIAG_ROW_COUNT :
		return _TRUE_;
	default :
		return _FALSE_;
	}
}

PRIVATE char* get_diag_class_origin(char *sql_state)
{
	if ( strncmp(sql_state, "IM", 2) == 0 ) 
		return ODBC_CLASS_ORIGIN;
	else
		return ISO_CLASS_ORIGIN;
}
