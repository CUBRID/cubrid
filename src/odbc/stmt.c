#include		<stdio.h>
#include		"portable.h"
#include		"sqlext.h"
#include		"util.h"
#include		"diag.h"
#include		"env.h"
#include		"conn.h"
#include		"stmt.h"
#include		"result.h"
#include		"desc.h"
#include		"odbc_type.h"
#include		"catalog.h"
#include		"cas_cci.h"

/*----------------------------------------------------------------------
	STMT_NEED_DATA - SQL stmt에 parameter가 있으며, binding된 param이
		SQL_DATA_AT_EXEC가 설정된 경우로 stmt가 SQLPutData를 
		필요로 하는 경우
	STMT_NEED_NO_MORE_DATA - binding된 param이 없거나 SQL_DATA_AT_EXEC
	가 아닌 경우이거나 SQLPutData로 data를 모두 보낸 경우
 *-----------------------------------------------------------------------*/
#define		STMT_NEED_DATA				(-1)		
#define		STMT_NEED_NO_MORE_DATA		(-100)

#define		IS_UPDATABLE( cursor )		\
		( cursor == SQL_CURSOR_DYNAMIC || cursor == SQL_CURSOR_KEYSET_DRIVEN )

typedef struct __st_ParamArray
{
	int					value_type;
	int*				ind_array;
	void*				value_array;
} ParamArray;

typedef struct __st_DescInfo
{
	short				type;
	long				bind_type;
	long				offset_size;
	void*				value_ptr;
	unsigned long		length;
	long*				ind_ptr;
	short				precision;
	short				scale;
	long*				octet_len_ptr;
} DescInfo;

PRIVATE SQLSMALLINT next_param_data_at_exec(ODBC_STATEMENT *stmt, 
											SQLSMALLINT param_pos);
PRIVATE void get_appl_desc_info(ODBC_DESC	*desc, 
				  short				col_index, 
				  DescInfo			*desc_info_ptr);
PRIVATE void recalculate_bind_pointer(DescInfo			*desc_info_ptr,
									  unsigned long		row_index, 
									  long				*value_addr,
									  long				*ind_addr,
									  long				*octet_len_addr);
PRIVATE void make_param_array(ODBC_STATEMENT	*stmt, 
							  short				column_index, 
							  DescInfo			*desc_info, 
							  ParamArray		*param_array,
							  int				array_size);

PRIVATE void reset_revised_sql(ODBC_STATEMENT *stmt);
PRIVATE int revised_param_pos(char *param_pos, int i);
PRIVATE char get_flag_of_cci_execute(ODBC_STATEMENT *stmt);
PRIVATE char get_flag_of_cci_prepare(ODBC_STATEMENT *stmt);
PRIVATE void create_bookmark_ird(ODBC_STATEMENT *stmt);
PRIVATE void memory_alloc_param_array(ParamArray *param_array, int array_size);
PRIVATE RETCODE delete_row_by_cursor_pos(ODBC_STATEMENT *stmt,
										unsigned long		cursor_pos);
PRIVATE RETCODE odbc_set_pos_update(ODBC_STATEMENT *stmt, 
									DescInfo *bookmark_desc_info_ptr,
									DescInfo *desc_info_ptr, 
									unsigned long row_pos);
PRIVATE RETCODE odbc_set_pos_delete(ODBC_STATEMENT *stmt, 
									DescInfo		*boomark_desc_info_ptr,
									unsigned long row_pos);
PRIVATE void free_param_array(ParamArray *param_ptr, 
							  int		array_count,
							  int		member_count);
PRIVATE short default_type_to_c_type(short value_type, short parameter_type);
#ifdef _DEBUG
PRIVATE void debug_print_appl_desc(DescInfo *desc_info, 
								   void *value_ptr,
								   long *ind_ptr);
#endif

/************************************************************************
* name: odbc_alloc_statement
* arguments: 
*		con : connection
*		st_pointer : returned statement structure
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC RETCODE odbc_alloc_statement(ODBC_CONNECTION *conn, 
									ODBC_STATEMENT **stmt_ptr)
{
	ODBC_STATEMENT *s;


	/* HY009 - DM */
	if ( stmt_ptr == NULL ) {
		odbc_set_diag(conn->diag, "HY009", 0, NULL);
		goto error;
	}

	s = (ODBC_STATEMENT *) UT_ALLOC(sizeof(ODBC_STATEMENT));
	if ( s == NULL ) {
		odbc_set_diag(conn->diag, "HY001", 0, NULL);
		goto error;
	} 
		
	s->handle_type = SQL_HANDLE_STMT;
	s->stmthd = 0;
	s->conn = NULL;
	s->next = NULL;
	s->cursor = NULL;
	s->sql_text = NULL;
	s->is_prepared = _FALSE_;
	memset(&s->revised_sql, 0, sizeof(s->revised_sql));
	s->result_type = NULL_RESULT;
    s->data_at_exec_state = STMT_NEED_NO_MORE_DATA;
	s->tpl_number = 0;
	s->current_tpl_pos = 0;		/* 0 means cursor closed, prev first tuple, 
								 *	-1 means over last tuple */
	s->param_number = 0;
	s->stmt_type = 0;
	
	InitStr(&s->param_data.val);
	s->param_data.index = 0;

	s->catalog_result.value = NULL;
	s->catalog_result.current = NULL;

	free_column_data(&s->column_data, INIT);
	
	odbc_alloc_desc(NULL, &(s->i_apd));
    odbc_alloc_desc(NULL, &(s->i_ard));
	odbc_alloc_desc(NULL, &(s->i_ipd));
	odbc_alloc_desc(NULL, &(s->i_ird));

	s->apd = s->i_apd;
	s->ard = s->i_ard;
	s->ipd = s->i_ipd;
	s->ird = s->i_ird;

	s->i_apd->stmt = s;
	s->i_ard->stmt = s;
	s->i_ipd->stmt = s;
	s->i_ird->stmt = s;

	// SQL_ATTR_ASYNC_ENABLE의 경우 connection으로부터 상속받는다.
	s->attr_async_enable = conn->attr_async_enable;
	s->attr_cursor_scrollable = SQL_NONSCROLLABLE;
	//s->attr_cursor_type = SQL_CURSOR_DYNAMIC;
	//s->attr_concurrency = SQL_CONCUR_LOCK;

	s->attr_concurrency = SQL_CONCUR_READ_ONLY;  
	s->attr_cursor_type = SQL_CURSOR_FORWARD_ONLY;
	s->attr_cursor_sensitivity = SQL_UNSPECIFIED;
	s->attr_enable_auto_ipd = SQL_FALSE;
	s->attr_fetch_bookmark_ptr = NULL;
	s->attr_keyset_size = 0;
	s->attr_max_length = 0;
	s->attr_max_rows = 0;
	s->attr_metadata_id = SQL_FALSE;
	s->attr_noscan = SQL_NOSCAN_OFF;
	s->attr_query_timeout = 0;
	s->attr_retrieve_data = SQL_RD_ON;
	s->attr_simulate_cursor = SQL_SC_NON_UNIQUE;
	s->attr_use_bookmark = SQL_UB_OFF;

	s->diag = odbc_alloc_diag();
	
	s->conn = conn;

	s->next = conn->statements;
	conn->statements = s;

	*stmt_ptr = s;

	return ODBC_SUCCESS;

error:
    return ODBC_ERROR;
}

/************************************************************************
* name: odbc_reset_statement
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
*	SQLFreeStmt와 match된다.
************************************************************************/
PUBLIC RETCODE odbc_reset_statement(ODBC_STATEMENT *stmt,
									unsigned short option)
{
	RETCODE		rc = ODBC_SUCCESS;

	switch (option) {
	case SQL_CLOSE :
		rc = odbc_close_cursor(stmt);
		break;
	case SQL_UNBIND :
		reset_descriptor(stmt->ard);
		stmt->ird->rows_processed_ptr = NULL;
		break;

	case SQL_RESET_PARAMS :
		reset_descriptor(stmt->apd);
		reset_descriptor(stmt->ipd);
		break;

	case SQL_DROP :
		odbc_free_statement(stmt);
		break;
	}

	return rc;
}


/************************************************************************
* name: odbc_free_statement
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC RETCODE odbc_free_statement(ODBC_STATEMENT *stmt)
{
	ODBC_STATEMENT *s, *prev;
	
	if ( stmt == NULL || stmt->handle_type != SQL_HANDLE_STMT ) 
		return SQL_INVALID_HANDLE;

	if ( stmt->conn != NULL ) {
		for ( s = stmt->conn->statements, prev = NULL; 
						s != NULL && s != stmt; s = s->next) {
			prev = s;
		}
	
		if ( s == stmt ) {
			if ( prev != NULL ) 
				prev->next = stmt->next;
			else
				stmt->conn->statements = stmt->next;
		}
	}


	/* free memebers */
	NA_FREE(stmt->sql_text);
	
	odbc_close_cursor(stmt);
	reset_revised_sql(stmt);

	odbc_free_diag(stmt->diag, FREE_ALL);
	stmt->diag = NULL;

	FreeStr(&stmt->param_data.val);
	stmt->param_data.index = 0;

	odbc_free_desc(stmt->i_apd);
	odbc_free_desc(stmt->i_ard);
	odbc_free_desc(stmt->i_ipd);
	odbc_free_desc(stmt->i_ird);
	stmt->i_apd = NULL;
	stmt->i_ard = NULL;
	stmt->i_ipd = NULL;
	stmt->i_ird = NULL;

	/* WARN : implement descriptor? */
	stmt->apd = NULL;
	stmt->ard = NULL;
	stmt->ird = NULL;
	stmt->ipd = NULL;

	UT_FREE(stmt);
	
	return SQL_SUCCESS;
}

/************************************************************************
* name: odbc_set_stmt_attr
* arguments: 
*	is_driver - SQLSetStmtAttr에 의한 ftn call인지 판단.
*	0 - SQLSetStmtAttr, 1 - driver
* returns/side-effects: 
* description: 
*	is_client의 값에 따라 read-only ATTR에 대해 다른 작업
* NOTE: 
************************************************************************/
PUBLIC RETCODE odbc_set_stmt_attr(ODBC_STATEMENT *stmt,
								   long attribute,
								   void	*valueptr,
								   long stringlength,
								   short is_driver)
{
	RETCODE rc;

	switch ( attribute ) {

	case SQL_ATTR_APP_PARAM_DESC :
		if ( valueptr == SQL_NULL_HDESC ) {
			stmt->apd = stmt->i_apd;
		} else {
			stmt->apd = valueptr;
		}
		break;

	case SQL_ATTR_APP_ROW_DESC :
		if ( valueptr == SQL_NULL_HDESC ) {
			stmt->ard = stmt->i_ard;
		} else {
			stmt->ard = valueptr;
		}
		break;

	case SQL_ATTR_ASYNC_ENABLE :
		switch ( (unsigned long)valueptr ) {
		case SQL_ASYNC_ENABLE_OFF  :
		case SQL_ASYNC_ENABLE_ON :
			stmt->attr_async_enable = (unsigned long)valueptr;
			break;

		default :
			odbc_set_diag(stmt->diag, "HY024", 0, NULL);
			goto error;
		}
		break;

	case SQL_ATTR_CURSOR_SENSITIVITY :
		switch ( (unsigned long)valueptr ) {
		case SQL_INSENSITIVE  :
			/* implicit setting */
			stmt->attr_concurrency =SQL_CONCUR_READ_ONLY;
			stmt->attr_cursor_type = SQL_CURSOR_STATIC;
			break;
		case SQL_SENSITIVE :
			break;
		case SQL_UNSPECIFIED :
			break;
		default :
			odbc_set_diag(stmt->diag, "HY024", 0, NULL);
			goto error;
		}

		stmt->attr_cursor_sensitivity = (unsigned long)valueptr;
		break;
		
	case SQL_ATTR_CURSOR_SCROLLABLE :
		switch ( (unsigned long)valueptr ) {
		case SQL_NONSCROLLABLE :
		case SQL_SCROLLABLE :
			stmt->attr_cursor_scrollable = (unsigned long)valueptr;
			break;

		default :
			odbc_set_diag(stmt->diag, "HY024", 0, NULL);
			goto error;
		}
		break;

	case SQL_ATTR_CURSOR_TYPE :
		switch ( (unsigned long)valueptr ) {
		case SQL_CURSOR_FORWARD_ONLY :
			stmt->attr_cursor_type = (unsigned long)valueptr;

			/* implicit setting */
			stmt->attr_cursor_scrollable = SQL_NONSCROLLABLE;
			break;

		case SQL_CURSOR_STATIC :
			stmt->attr_cursor_type = (unsigned long)valueptr;

			/* implicit setting */
			stmt->attr_cursor_scrollable = SQL_SCROLLABLE;
			if (stmt->attr_concurrency == SQL_CONCUR_READ_ONLY ) {
				stmt->attr_cursor_sensitivity = SQL_INSENSITIVE;
			} else {
				/*  or SQL_SESITIVE */
				stmt->attr_cursor_sensitivity = SQL_UNSPECIFIED;
			}
			break;
		
		case SQL_CURSOR_KEYSET_DRIVEN :
		case SQL_CURSOR_DYNAMIC :
			stmt->attr_cursor_type = (unsigned long)valueptr;

			/* implicit setting */
			stmt->attr_cursor_scrollable = SQL_SCROLLABLE;
			stmt->attr_cursor_sensitivity = SQL_SENSITIVE;
			break;


		default :
			odbc_set_diag(stmt->diag, "HY024", 0, NULL);
			goto error;

		}
		break;

	case SQL_ATTR_FETCH_BOOKMARK_PTR :
		stmt->attr_fetch_bookmark_ptr = valueptr;
		break;


	case SQL_ATTR_IMP_PARAM_DESC :
		if ( is_driver == 1 ) {
			if ( valueptr == SQL_NULL_HDESC ) {
			    stmt->ipd = stmt->i_ipd;
			} else {
				stmt->ipd = valueptr;
			}
		} else {
			/* DM SQLSTATE - READ-ONLY */
			odbc_set_diag(stmt->diag, "HY092", 0, NULL);
			goto error;
		}
		break;
		
	case SQL_ATTR_IMP_ROW_DESC :
		if ( is_driver == 1 ) {
			if ( valueptr == SQL_NULL_HDESC ) {
				stmt->ird  = stmt->ird;
			} else {
				stmt->ird = valueptr;
			}
		} else {
    		/* DM SQLSTATE - READ-ONLY */
	    	odbc_set_diag(stmt->diag, "HY092", 0, NULL);
			goto error;
		}
		break;

	case SQL_ATTR_METADATA_ID :
		switch ( (unsigned long)valueptr ) {
		case SQL_TRUE :
		case SQL_FALSE :
			stmt->attr_metadata_id = (unsigned long)valueptr;
			break;

		default :
			odbc_set_diag(stmt->diag, "HY024", 0, NULL);
			goto error;
		}
		break;

	case SQL_ATTR_PARAM_BIND_OFFSET_PTR : 
		 odbc_set_desc_field(stmt->apd, 0, SQL_DESC_BIND_OFFSET_PTR,
								 valueptr, sizeof(valueptr), 1);
		break;

	case SQL_ATTR_PARAM_BIND_TYPE : 
		rc = odbc_set_desc_field(stmt->apd, 0, SQL_DESC_BIND_TYPE, 
								valueptr, sizeof(valueptr), 1);
		if ( rc < 0 )  {
			odbc_set_diag(stmt->diag, "HY024", 0, NULL);
			goto error;
		}
		break;

	case SQL_ATTR_PARAM_OPERATION_PTR :
		odbc_set_desc_field(stmt->apd, 0, SQL_DESC_ARRAY_STATUS_PTR, 
								valueptr, sizeof(valueptr), 1);
		break;

	case SQL_ATTR_PARAM_STATUS_PTR :
		odbc_set_desc_field(stmt->ipd, 0, SQL_DESC_ARRAY_STATUS_PTR,
								valueptr, sizeof(valueptr), 1);
		break;

	case SQL_ATTR_PARAMS_PROCESSED_PTR :
		odbc_set_desc_field(stmt->ipd, 0, SQL_DESC_ROWS_PROCESSED_PTR,
								valueptr, sizeof(valueptr), 1);
		break;

	case SQL_ATTR_PARAMSET_SIZE :
		odbc_set_desc_field(stmt->apd, 0, SQL_DESC_ARRAY_SIZE,
								valueptr, sizeof(valueptr), 1);
		break;


	case SQL_ATTR_ROW_ARRAY_SIZE :
	case SQL_ROWSET_SIZE :		// for 2.x backward compatiablity
		odbc_set_desc_field(stmt->ard, 0, SQL_DESC_ARRAY_SIZE,
								valueptr, sizeof(valueptr), 1);
		break;

	case SQL_ATTR_ROW_BIND_OFFSET_PTR :
		odbc_set_desc_field(stmt->ard, 0, SQL_DESC_BIND_OFFSET_PTR,
								valueptr, sizeof(valueptr), 1);
		break;

	case SQL_ATTR_ROW_BIND_TYPE :
		rc = odbc_set_desc_field(stmt->ard, 0, SQL_DESC_BIND_TYPE,
								valueptr, sizeof(valueptr), 1);
		if ( rc < 0 )  {
			odbc_set_diag(stmt->diag, "HY024", 0, NULL);
			goto error;
		}
		break;

	case SQL_ATTR_ROW_STATUS_PTR :
		odbc_set_desc_field(stmt->ird, 0, SQL_DESC_ARRAY_STATUS_PTR,
								valueptr, sizeof(valueptr), 1);
		break;

	case SQL_ATTR_ROWS_FETCHED_PTR :
		odbc_set_desc_field(stmt->ird, 0, SQL_DESC_ROWS_PROCESSED_PTR,
								valueptr, sizeof(valueptr), 1);
		break;

	case SQL_ATTR_ROW_NUMBER :
		// READ-ONLY
		odbc_set_diag(stmt->diag, "HY092", 0, NULL);
		goto error;

	case SQL_ATTR_USE_BOOKMARKS :
		stmt->attr_use_bookmark = (unsigned long)valueptr;
		break;

	
	// Not supported attribute
	// 어떠한 작업도 하지 않는다.
	case SQL_ATTR_CONCURRENCY :
	case SQL_ATTR_ENABLE_AUTO_IPD :
	case SQL_ATTR_KEYSET_SIZE :
	case SQL_ATTR_MAX_LENGTH :
	case SQL_ATTR_MAX_ROWS :
	case SQL_ATTR_NOSCAN :
	case SQL_ATTR_QUERY_TIMEOUT :
	case SQL_ATTR_RETRIEVE_DATA :

		break;
	


	default :
		odbc_set_diag(stmt->diag, "HY092", 0, NULL);
		goto error;

	}

	return ODBC_SUCCESS;
error:
	return ODBC_ERROR;
}

/************************************************************************
* name: odbc_get_stmt_attr
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC RETCODE odbc_get_stmt_attr(ODBC_STATEMENT *stmt,
								  long	attr,
								  void	*value_ptr,
								  long	buffer_length,
								  long	*length_ptr)
{

	if ( value_ptr == NULL ) {
		odbc_set_diag(stmt->diag, "HY009", 0, NULL);
		goto error;
	}

	switch ( attr ) {
	case SQL_ATTR_APP_PARAM_DESC :
		if ( value_ptr != NULL )
			*(ODBC_DESC**)value_ptr = stmt->apd;

		if ( length_ptr != NULL ) 
			*length_ptr = sizeof(stmt->apd);
		break;

	case SQL_ATTR_APP_ROW_DESC :
		if ( value_ptr != NULL )
			*(ODBC_DESC**)value_ptr = stmt->ard;
		if ( length_ptr != NULL )
			*length_ptr = sizeof(stmt->ard);
		break;

	case SQL_ATTR_ASYNC_ENABLE :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = stmt->attr_async_enable;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;
		
	case SQL_ATTR_CURSOR_SCROLLABLE :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = stmt->attr_cursor_scrollable;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(stmt->attr_cursor_scrollable);
		break;

	case SQL_ATTR_IMP_PARAM_DESC :
		if ( value_ptr != NULL )
			*(ODBC_DESC**)value_ptr = stmt->ipd;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(stmt->ipd);
		break;

	case SQL_ATTR_IMP_ROW_DESC :
		if ( value_ptr != NULL )
			*(ODBC_DESC**)value_ptr = stmt->ird;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(stmt->ird);
		break;

	case SQL_ATTR_METADATA_ID :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = stmt->attr_metadata_id;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(stmt->attr_metadata_id);
		break;

	case SQL_ATTR_PARAM_BIND_OFFSET_PTR :
		odbc_get_desc_field(stmt->apd, 0, SQL_DESC_BIND_OFFSET_PTR,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_PARAM_BIND_TYPE :
		odbc_get_desc_field(stmt->apd, 0, SQL_DESC_BIND_TYPE,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_PARAM_OPERATION_PTR :
		odbc_get_desc_field(stmt->apd, 0, SQL_DESC_ARRAY_STATUS_PTR,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_PARAM_STATUS_PTR :
		odbc_get_desc_field(stmt->ipd, 0, SQL_DESC_ARRAY_STATUS_PTR,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_PARAMS_PROCESSED_PTR :
		odbc_get_desc_field(stmt->ipd, 0, SQL_DESC_ROWS_PROCESSED_PTR,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_PARAMSET_SIZE :
		odbc_get_desc_field(stmt->apd, 0, SQL_DESC_ARRAY_SIZE,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_RETRIEVE_DATA :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = SQL_RD_ON;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;

	case SQL_ATTR_ROW_ARRAY_SIZE :
	case SQL_ROWSET_SIZE :		// for 2.x backward compatiablity
		odbc_get_desc_field(stmt->ard, 0, SQL_DESC_ARRAY_SIZE,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_ROW_BIND_OFFSET_PTR :
		odbc_get_desc_field(stmt->ard, 0, SQL_DESC_BIND_OFFSET_PTR,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_ROW_BIND_TYPE :
		odbc_get_desc_field(stmt->ard, 0, SQL_DESC_BIND_TYPE,
									value_ptr, buffer_length, length_ptr);
		break;
	
	case SQL_ATTR_ROW_STATUS_PTR :
		odbc_get_desc_field(stmt->ird, 0, SQL_DESC_ARRAY_STATUS_PTR,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_ROWS_FETCHED_PTR :
		odbc_get_desc_field(stmt->ird, 0, SQL_DESC_ROWS_PROCESSED_PTR,
									value_ptr, buffer_length, length_ptr);
		break;

	case SQL_ATTR_ROW_NUMBER :
		if ( value_ptr != NULL ) {
			if ( stmt->current_tpl_pos <=0 ) 
				*(unsigned long*)value_ptr = 0;
			else 
				*(unsigned long*)value_ptr = stmt->current_tpl_pos;
		}
		if ( length_ptr != NULL )
			*length_ptr = sizeof(stmt->attr_cursor_scrollable);
		break;

	case SQL_ATTR_CURSOR_TYPE :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = stmt->attr_cursor_type;
		if ( length_ptr != NULL ) 
			*length_ptr = sizeof(unsigned long);
		break;
		
	// SQL_CONCUR_READ_ONLY
	case SQL_ATTR_CONCURRENCY :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = stmt->attr_concurrency;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;

	// Not supported attribute
	
	case SQL_ATTR_CURSOR_SENSITIVITY :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = stmt->attr_cursor_sensitivity;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;
		 
	case SQL_ATTR_ENABLE_AUTO_IPD :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = SQL_FALSE;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;
	 
	case SQL_ATTR_FETCH_BOOKMARK_PTR :
		if ( value_ptr != NULL )
			*((void**)value_ptr) = NULL;

		if ( length_ptr != NULL ) {
			*length_ptr = sizeof(void*);
		}
		break;

	case SQL_ATTR_KEYSET_SIZE :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = 0;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;

	case SQL_ATTR_MAX_LENGTH :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = 0;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;

	case SQL_ATTR_MAX_ROWS :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = 0;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;

	case SQL_ATTR_NOSCAN :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = SQL_NOSCAN_ON ;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;

	case SQL_ATTR_QUERY_TIMEOUT :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = 0;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;
	

	case SQL_ATTR_ROW_OPERATION_PTR :
		if ( value_ptr != NULL )
			*((void**)value_ptr) = NULL;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(void*);
		break;

	case SQL_ATTR_SIMULATE_CURSOR :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = SQL_SC_NON_UNIQUE ;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;
	case SQL_ATTR_USE_BOOKMARKS :
		if ( value_ptr != NULL )
			*(unsigned long*)value_ptr = stmt->attr_use_bookmark ;

		if ( length_ptr != NULL )
			*length_ptr = sizeof(unsigned long);
		break;

	default :
		odbc_set_diag(stmt->diag, "HY092", 0, NULL);
		goto error;
	}

	return ODBC_SUCCESS;
error:
	return ODBC_ERROR;
}


/************************************************************************
 * name: odbc_set_cursor_name
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE odbc_set_cursor_name(ODBC_STATEMENT	*stmt,
									char *cursor_name,	
									short	name_length)
{
	if ( cursor_name != NULL ) {
		stmt->cursor = UT_MAKE_STRING(cursor_name, name_length);
	}
	return ODBC_SUCCESS;
}

/************************************************************************
 * name: odbc_get_cursor_name
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE odbc_get_cursor_name(ODBC_STATEMENT	*stmt,
									char *cursor_name,
									short buffer_length,
									long *name_length_ptr)
{
	RETCODE		rc, status = ODBC_SUCCESS;
	char *pt;

	if ( stmt->cursor != NULL ) {
		pt = stmt->cursor;
	} else {
		pt = "";
	}
	rc = str_value_assign(pt, cursor_name, buffer_length, name_length_ptr);
	if ( rc == ODBC_SUCCESS_WITH_INFO ) {
		odbc_set_diag(stmt->diag, "01004", 0, NULL);
		status = rc;
	}

	return status;
}


/************************************************************************
* name: odbc_bind_parameter
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
*	column_size에 대해서 고려하지 않고 있음.
************************************************************************/
PUBLIC RETCODE odbc_bind_parameter(ODBC_STATEMENT *stmt,
								   unsigned short parameter_num,
								   short	input_output_type,
								   short	value_type,
								   short	parameter_type,
								   unsigned long	column_size,
								   short	decimal_digits,
								   void*	parameter_value_ptr,
								   long		buffer_length,
								   long		*strlen_ind_ptr)
{
	ODBC_DESC	*apd = NULL, *ipd = NULL;
	RETCODE	rc;
	short	odbc_type;
	long	size;
	ODBC_RECORD	*record;

	apd = stmt->apd;
	ipd = stmt->ipd;


	/*****           APD             *****/
	/* setting concise type */

	record = find_record_from_desc(apd, parameter_num);
	if ( record == NULL ) {
		 odbc_alloc_record(apd, NULL, parameter_num);
	}

	if (value_type == SQL_C_DEFAULT) {
		value_type = default_type_to_c_type(value_type, parameter_type);
	}

	rc = odbc_set_desc_field(apd, parameter_num, SQL_DESC_CONCISE_TYPE,
								(SQLPOINTER)value_type, 0, 1);
	ERROR_GOTO(rc, error1);

	/* setting verbose type */
	odbc_type = odbc_concise_to_verbose_type(value_type);

	rc = odbc_set_desc_field(apd, parameter_num, SQL_DESC_TYPE,
							(SQLPOINTER)odbc_type, 0, 1);
	ERROR_GOTO(rc, error1);

	/* setting subcode type */
	/* parameter type is c data type */
	if ( odbc_is_valid_c_interval_type(value_type) ||
			odbc_is_valid_c_date_type(value_type) ) {
	    odbc_type = odbc_subcode_type(value_type);
	    rc = odbc_set_desc_field(apd, parameter_num, SQL_DESC_DATETIME_INTERVAL_CODE,
								(SQLPOINTER)odbc_type, 0, 1);
		ERROR_GOTO(rc, error1);
	}

	/* char data type의 경우는 buffer_length를 octet_length로 사용한다. */
	size = odbc_octet_length(value_type, buffer_length);
	rc = odbc_set_desc_field(apd, parameter_num, SQL_DESC_OCTET_LENGTH,
			(void*)size, 0, 1);
	ERROR_GOTO(rc, error1);


	rc = odbc_set_desc_field(apd, parameter_num, SQL_DESC_DATA_PTR,
								parameter_value_ptr, 0, 1);
	ERROR_GOTO(rc, error1);

	rc = odbc_set_desc_field(apd, parameter_num, SQL_DESC_OCTET_LENGTH_PTR, 
								strlen_ind_ptr, 0, 1);
	ERROR_GOTO(rc, error1);

	rc = odbc_set_desc_field(apd, parameter_num, SQL_DESC_INDICATOR_PTR,
								strlen_ind_ptr, 0, 1);
	ERROR_GOTO(rc, error1);

	/*****           IPD             *****/
	record = find_record_from_desc(ipd, parameter_num);
	if ( record == NULL ) {
		odbc_alloc_record(ipd, NULL, parameter_num);
	}
	/* setting concise type */
	rc = odbc_set_desc_field(ipd, parameter_num, SQL_DESC_CONCISE_TYPE,
								(SQLPOINTER)parameter_type, 0, 1);
	ERROR_GOTO(rc, error2);

	/* setting verbose type */
	odbc_type = odbc_concise_to_verbose_type(parameter_type);

	rc = odbc_set_desc_field(ipd, parameter_num, SQL_DESC_TYPE,
							(SQLPOINTER)odbc_type, 0, 1);
	ERROR_GOTO(rc, error2);

	/* setting subcode type */
	/* parameter type is c data type */
	if ( odbc_is_valid_c_interval_type(parameter_type) ||
			odbc_is_valid_c_date_type(parameter_type) ) {
	    odbc_type = odbc_subcode_type(parameter_type);
	    rc = odbc_set_desc_field(ipd, parameter_num, SQL_DESC_DATETIME_INTERVAL_CODE,
								(SQLPOINTER)odbc_type, 0, 1);
		ERROR_GOTO(rc, error2);
	}

	if ( parameter_type == SQL_CHAR ||
		 parameter_type == SQL_VARCHAR ||
		 parameter_type == SQL_LONGVARCHAR ||
		 parameter_type == SQL_BINARY ||
		 parameter_type == SQL_VARBINARY ||
		 parameter_type == SQL_LONGVARBINARY ||
		 odbc_is_valid_sql_date_type(parameter_type) ||
		 odbc_is_valid_sql_interval_type(parameter_type) ) {

		rc = odbc_set_desc_field(ipd, parameter_num, SQL_DESC_LENGTH, 
									(SQLPOINTER)column_size, 0, 1);
		ERROR_GOTO(rc, error2);
	} else if ( parameter_type == SQL_DECIMAL ||
				parameter_type == SQL_NUMERIC ||
				parameter_type == SQL_FLOAT ||
				parameter_type == SQL_REAL ||
				parameter_type == SQL_DOUBLE ) {

		rc = odbc_set_desc_field(ipd, parameter_num, SQL_DESC_PRECISION,
									(SQLPOINTER)column_size, 0, 1);
		ERROR_GOTO(rc, error2);
	}

	if ( parameter_type == SQL_TYPE_TIME ||
		 parameter_type  == SQL_TIME		||		// for 2.x backward compatibility
		 parameter_type == SQL_TYPE_TIMESTAMP ||
		 parameter_type == SQL_TIMESTAMP ||		// for 2.x backward compatibility
		 parameter_type == SQL_INTERVAL_SECOND ||
		 parameter_type == SQL_INTERVAL_DAY_TO_SECOND ||
		 parameter_type == SQL_INTERVAL_HOUR_TO_SECOND ||
		 parameter_type == SQL_INTERVAL_MINUTE_TO_SECOND ) {

		rc = odbc_set_desc_field(ipd, parameter_num, SQL_DESC_PRECISION,
									(SQLPOINTER)decimal_digits, 0, 1);
		ERROR_GOTO(rc, error2);
	} else if ( parameter_type == SQL_NUMERIC ||
				parameter_type == SQL_DECIMAL ) {

		rc = odbc_set_desc_field(ipd, parameter_num, SQL_DESC_SCALE,
									(SQLPOINTER)decimal_digits, 0, 1);
		ERROR_GOTO(rc, error2);
	}

	return ODBC_SUCCESS;

error1:
	odbc_move_diag(stmt->diag, apd->diag);
	return ODBC_ERROR;

error2 :
	odbc_move_diag(stmt->diag, ipd->diag);
	return ODBC_ERROR;
}

/************************************************************************
* name: odbc_num_params
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
*		odbc_num_result_cols는 ird가 sql stmt에 맞게 생성되기 때문에
*		ird의 record개수가 num of result cols이지만,
*		odbc_num_params에서는 ipd가 자동적으로 생성되지 않기 때문에,
*		stmt->param_number의 member에 유지하고 있다.
************************************************************************/
PUBLIC RETCODE odbc_num_params(ODBC_STATEMENT* stmt,
							   short	*parameter_count)
{

	if ( parameter_count ) {
		*parameter_count = stmt->param_number;
	}

	return ODBC_SUCCESS;
}

/************************************************************************
* name: odbc_prepare
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
*	- statement_text가 null이면 DM error가 난다.
*	- IRD setting이 일어난다.
*	- Method에 의한 result set에 대해서
*		col type - SQL_VARCHAR
*		col precision - MAX_PRECISION을 적용하였다.
************************************************************************/
PUBLIC RETCODE odbc_prepare(ODBC_STATEMENT *stmt,
							char	*statement_text)
{
	int				cci_rc;
	T_CCI_ERROR		cci_err_buf;
	T_CCI_COL_INFO	*cci_col_info;
	int				column_number;
	char			flag_cci_prepare;
	
	//init & reset
	// reset - result set and related info, sql statement
	if ( stmt->stmthd > 0 ) {
		cci_close_req_handle(stmt->stmthd);
		stmt->stmthd = -1;
	}
	reset_result_set(stmt);	
	reset_descriptor(stmt->ird);
	stmt->param_number = 0;
	
	reset_revised_sql(stmt);
	NA_FREE(stmt->sql_text);

	stmt->sql_text = UT_MAKE_STRING(statement_text, -1);
	stmt->revised_sql.sql_text = UT_MAKE_STRING(statement_text, -1);
	// revise sql text because oid string value
	stmt->revised_sql.oid_param_num = replace_oid(stmt->revised_sql.sql_text,
				&stmt->revised_sql.org_param_pos, 
				&stmt->revised_sql.oid_param_pos, &stmt->revised_sql.oid_param_val);
		
	flag_cci_prepare = get_flag_of_cci_prepare(stmt);
	cci_rc = cci_prepare(stmt->conn->connhd, stmt->revised_sql.sql_text, flag_cci_prepare, &cci_err_buf);
	if ( cci_rc < 0 ) 	goto error;

	stmt->stmthd = cci_rc;
	
	// set fetch size
	if ( stmt->conn->fetch_size > 0 ) {
		cci_fetch_size(stmt->stmthd, stmt->conn->fetch_size);
	}

	// get param number
	stmt->param_number = cci_get_bind_num(stmt->stmthd) - stmt->revised_sql.oid_param_num;

	// second argument is stmt->stmt_type
	cci_col_info = cci_get_result_info(stmt->stmthd, &stmt->stmt_type, &column_number);

	// check read-only mode
	if ( stmt->conn->attr_access_mode == SQL_MODE_READ_ONLY &&
		!RESULTSET_STMT_TYPE(stmt->stmt_type) ) {
		odbc_set_diag(stmt->diag, "UN012", 0, NULL);
		return ODBC_ERROR;
	}
	
	create_ird(stmt, cci_col_info, column_number);
	
	return ODBC_SUCCESS;

error:
	odbc_set_diag_by_cci(stmt->diag, cci_rc, cci_err_buf.err_msg);
	return ODBC_ERROR;
}

/************************************************************************
* name: odbc_execute
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
*   1) 현재 array parameter bind & execution을 지원하고 있지 않다.
*	2) error가 발생한 경우, SQL_ERROR나 SQL_SUCCESS_WITH_INFO를 return한다.
*	이 때는 autocommit mode일지라도 autocommit은 일어나지 않는다.(rollback도 없다.)
************************************************************************/
PUBLIC RETCODE odbc_execute(ODBC_STATEMENT *stmt)
{
	short			count, valid_param_count;   
	short			sql_type;
	T_CCI_A_TYPE	a_type;
	T_CCI_U_TYPE	u_type;
	void			*c_value = NULL, *sql_value = NULL;
	short			is_data_at_exec = 0;
	void			*cci_value = NULL;
	int				cci_rc = 0;
	T_CCI_ERROR		cci_err_buf;
	short			i;
	unsigned long	j;
	unsigned long	h;
	DescInfo		desc_info;
	unsigned short	*ParamOperationArrayPtr, *ParamStatusArrayPtr;
	unsigned long	ProcessedParamNum;
	unsigned long*	ProcessedParamNumPtr = NULL;
	int				RevisedParamPos;
	char			flag_cci_exec = 0;
	char			flag_stmt_need_data = 0;
	char			flag_error = 0;

	char			*pt_pos, *pt_val;
	char			pos_buf[128], val_buf[128];
	ParamArray		*array_of_param_array = NULL;
	ParamArray		*array_of_oid_param_array = NULL;
	T_CCI_QUERY_RESULT	*qr;
	int				res;
	int				update_count;
	char			*err_msg;

	void*	value_ptr;
	long*	ind_ptr;
	char			err_buf[BUF_SIZE];
	long*	octet_len_ptr;



	// Init 
	reset_result_set(stmt);

	stmt->result_type = QUERY;
	

	odbc_get_desc_field(stmt->apd, 0, SQL_DESC_COUNT, &count, 0, NULL);
	valid_param_count = MIN(stmt->param_number, count);

	if ( stmt->apd->array_size == 1 ) {
		/* cursor가 sensitive할 경우 SQL_ATTR_ROW_ARRAY_SIZE와 fetch_size를 일치시킨다.
		 * connection handle(or DSN)의 fetch_size는 의미를 상실한다. */
		if ( stmt->attr_cursor_sensitivity == SQL_SENSITIVE ) {
			cci_fetch_size(stmt->stmthd, stmt->ard->array_size);
		}

		for ( i = 1; i <= valid_param_count; ++i ) {
			RevisedParamPos = revised_param_pos(stmt->revised_sql.org_param_pos, i);

			/* APD record */
			get_appl_desc_info(stmt->apd, i, &desc_info);

			/* IPD Record */
			odbc_get_desc_field(stmt->ipd, i, SQL_DESC_CONCISE_TYPE,
									&sql_type, 0, NULL);
			
			a_type = odbc_type_to_cci_a_type(desc_info.type);
			u_type = odbc_type_to_cci_u_type(sql_type);

			recalculate_bind_pointer(&desc_info, 1, (long*)&value_ptr, (long*)&ind_ptr, (long*)&octet_len_ptr);

			if (octet_len_ptr) {
				desc_info.length = *octet_len_ptr;
			} else
				odbc_get_desc_field( stmt->ipd, i, SQL_DESC_LENGTH, &(desc_info.length), 0, NULL );

			if ( ind_ptr != NULL  && *ind_ptr == SQL_NULL_DATA ) {
				cci_rc = cci_bind_param(stmt->stmthd, RevisedParamPos, a_type, NULL, u_type, 0);
				ERROR_GOTO(cci_rc, cci_error);
			} else if ( ind_ptr != NULL && IND_IS_DATA_AT_EXEC(*ind_ptr) ) {
				stmt->data_at_exec_state = STMT_NEED_DATA;
			} else {
				/*--
				 * cci_bind_param()을 사용할 때 SQLBindParam()에 의한 value pointer
				 * 를 바로 사용할 수 없다. 왜냐면 SQLBindParam()에 의한 value pointer
				 * 와 cci_bind_param()에서 사용하는 value pointer사이에는 type conversion
				 * 등이 발생하기 때문이다.
				 *--*/
				cci_value = odbc_value_to_cci(value_ptr, desc_info.type, 
 									desc_info.length, desc_info.precision, desc_info.scale);
				if ( cci_value == NULL ) {
					odbc_set_diag(stmt->diag, "HY000", 0, err_buf);
					goto error;
				}

				
				// oid string checking
				if ( a_type == CCI_A_TYPE_STR && 
					( u_type == CCI_U_TYPE_CHAR || u_type == CCI_U_TYPE_VARNCHAR ) ) {
					if ( is_oidstr(cci_value) == _TRUE_ ) {
						u_type = CCI_U_TYPE_OBJECT;
					}
				}
				
				cci_rc = cci_bind_param(stmt->stmthd, RevisedParamPos, a_type, cci_value, u_type, 0);
				ERROR_GOTO(cci_rc, cci_error);

				NA_FREE(cci_value);
			}
		}

		// oid param bind
		if ( stmt->revised_sql.oid_param_num > 0 ) {
			pt_pos = stmt->revised_sql.oid_param_pos;
			pt_val = stmt->revised_sql.oid_param_val;
			while ( element_from_setstring(&pt_pos, pos_buf) > 0 ) {
				RevisedParamPos = atoi(pos_buf);
				element_from_setstring(&pt_val, val_buf);
				cci_rc = cci_bind_param(stmt->stmthd, RevisedParamPos, CCI_A_TYPE_STR,
					val_buf, CCI_U_TYPE_OBJECT, 0);
				ERROR_GOTO(cci_rc, cci_error);
			}
		}

		if ( stmt->data_at_exec_state == STMT_NEED_DATA ) {
			flag_stmt_need_data = 1;
		} else {
			flag_cci_exec = get_flag_of_cci_execute(stmt);
			cci_rc = cci_execute(stmt->stmthd, flag_cci_exec, 0, &cci_err_buf);
			ERROR_GOTO(cci_rc, cci_error);

			stmt->tpl_number = cci_rc;
		}
				
		// resource release
		NA_FREE(cci_value);

	} else {  /* ARRAY binding & query for update */
		odbc_get_stmt_attr(stmt, SQL_ATTR_PARAM_STATUS_PTR, &ParamStatusArrayPtr, 0, NULL);
		odbc_get_stmt_attr(stmt, SQL_ATTR_PARAM_OPERATION_PTR, &ParamOperationArrayPtr, 0, NULL);

		if ( RESULTSET_STMT_TYPE(stmt->stmt_type) ) {
			// multi result set
			odbc_set_diag(stmt->diag, "HY000", 0, "The driver "
				"does not support multi result set.");
			goto error;
		}
		
		for ( h = 1, ProcessedParamNum = 0; h <= stmt->apd->array_size; ++h ) {
			if ( ParamOperationArrayPtr != NULL &&
				ParamOperationArrayPtr[h-1] == SQL_PARAM_IGNORE ) {
				if ( ParamStatusArrayPtr != NULL )
					ParamStatusArrayPtr[h-1] = SQL_PARAM_UNUSED;
				continue;
			} else {
				if ( ParamStatusArrayPtr != NULL )
					ParamStatusArrayPtr[h-1] = SQL_PARAM_SUCCESS;
			}
			++ProcessedParamNum;
		}

		if ( valid_param_count > 0 ) {
			array_of_param_array = UT_ALLOC( sizeof(ParamArray) * valid_param_count);
			memset(array_of_param_array, 0, sizeof(ParamArray) * valid_param_count);
		}

		cci_bind_param_array_size(stmt->stmthd, ProcessedParamNum);


		for ( i = 1; i <= valid_param_count; ++i ) {
			RevisedParamPos = revised_param_pos(stmt->revised_sql.org_param_pos, i);

			/* APD record */
			get_appl_desc_info(stmt->apd, i, &desc_info);

			/* IPD Record */
			odbc_get_desc_field(stmt->ipd, i, SQL_DESC_CONCISE_TYPE,
									&sql_type, 0, NULL);
			
			a_type = odbc_type_to_cci_a_type(desc_info.type);
			u_type = odbc_type_to_cci_u_type(sql_type);
			

			array_of_param_array[i-1].value_type = a_type;

			make_param_array(stmt, i, &desc_info, &array_of_param_array[i-1], ProcessedParamNum);
		

			// oid string checking
			if ( a_type == CCI_A_TYPE_STR && 
				( u_type == CCI_U_TYPE_CHAR || u_type == CCI_U_TYPE_VARNCHAR ) ) {
				if ( is_oidstr_array(array_of_param_array[i-1].value_array, ProcessedParamNum ) == _TRUE_ ) {
					u_type = CCI_U_TYPE_OBJECT;
				}
			}
			
			cci_rc = cci_bind_param_array(stmt->stmthd, RevisedParamPos, a_type, array_of_param_array[i-1].value_array, 
				array_of_param_array[i-1].ind_array, u_type);
			ERROR_GOTO(cci_rc, cci_error);
		}


		// oid param bind
		if ( stmt->revised_sql.oid_param_num > 0 ) {
			array_of_oid_param_array = UT_ALLOC( sizeof(ParamArray) * stmt->revised_sql.oid_param_num );
			memset(array_of_oid_param_array, 0, sizeof(ParamArray) * stmt->revised_sql.oid_param_num);

			
			pt_pos = stmt->revised_sql.oid_param_pos;
			pt_val = stmt->revised_sql.oid_param_val;
			
			for (i =0; element_from_setstring(&pt_pos, pos_buf) > 0 ; ++i) {
				array_of_oid_param_array[i].value_array = UT_ALLOC( sizeof(void*) * ProcessedParamNum);
				array_of_oid_param_array[i].ind_array = UT_ALLOC( sizeof(int) * ProcessedParamNum );
				memset(array_of_oid_param_array[i].ind_array, 0, sizeof(int) * ProcessedParamNum);

				RevisedParamPos = atoi(pos_buf);
				element_from_setstring(&pt_val, val_buf);

				for ( j = 0; j < ProcessedParamNum; ++j) {
					((char**)array_of_oid_param_array[i].value_array)[j] = UT_MAKE_STRING(val_buf, -1);
				}

				cci_rc = cci_bind_param_array(stmt->stmthd, RevisedParamPos, CCI_A_TYPE_STR,
					array_of_oid_param_array[i].value_array, 
					array_of_oid_param_array[i].ind_array, CCI_U_TYPE_OBJECT);

				ERROR_GOTO(cci_rc, cci_error);
			}
		}
		

		cci_rc = cci_execute_array(stmt->stmthd, &qr, &cci_err_buf);
		ERROR_GOTO(cci_rc, cci_error);

		res = cci_rc;

		stmt->tpl_number = 0;

		for ( i = 1, j = 0, ProcessedParamNum = 0; i <= res ; ++i, ++j ) {
			if ( ParamStatusArrayPtr[j] == SQL_PARAM_UNUSED ) {
				++j;
			}
			update_count = CCI_QUERY_RESULT_RESULT(qr, i);
			if ( update_count < 0 ) {
				err_msg = CCI_QUERY_RESULT_ERR_MSG(qr, i);
				ParamStatusArrayPtr[j] = SQL_PARAM_ERROR;
			} else {
				stmt->tpl_number += update_count;
				ParamStatusArrayPtr[j] = SQL_PARAM_SUCCESS;
				++ProcessedParamNum;
			}
		}
		cci_query_result_free(qr, res);
		free_param_array(array_of_param_array, valid_param_count,ProcessedParamNum);
		free_param_array(array_of_oid_param_array, stmt->revised_sql.oid_param_num, ProcessedParamNum);

		// set processed parameters number
		odbc_get_stmt_attr(stmt, SQL_ATTR_PARAMS_PROCESSED_PTR, &ProcessedParamNumPtr, 0, NULL);
		if ( ProcessedParamNumPtr != NULL ) 
			*ProcessedParamNumPtr = ProcessedParamNum;

		if ( stmt->tpl_number == 0 && 
				(	stmt->stmt_type ==  SQLX_CMD_UPDATE || 
				stmt->stmt_type == SQLX_CMD_DELETE ) ) {
			if ( stmt->conn->env->attr_odbc_version == SQL_OV_ODBC2 ) {
				return ODBC_SUCCESS;		// for 2.x backward compatibility
			} else {
				return ODBC_NO_DATA;
			}
		}

		
	}

	if ( flag_stmt_need_data != 0 ) return ODBC_NEED_DATA;

	if ( !RESULTSET_STMT_TYPE(stmt->stmt_type) ) {
		odbc_auto_commit(stmt->conn);
	}

	return ODBC_SUCCESS;

cci_error :
		if ( cci_rc < 0 ) 
			odbc_set_diag_by_cci(stmt->diag, cci_rc, cci_err_buf.err_msg);
error :
		NA_FREE(cci_value);
		return ODBC_ERROR;
}


/************************************************************************
* name: odbc_param_data
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/

PUBLIC RETCODE odbc_param_data(ODBC_STATEMENT *stmt,
							   void **valueptr_ptr)
{
	short			param;
	short			sql_type;
	T_CCI_A_TYPE	a_type;
	T_CCI_U_TYPE	u_type;
	void			*c_value = NULL, *sql_value = NULL;
	//long			*ind_value_ptr;
	void			*cci_value = NULL;
	int				cci_rc;
	T_CCI_ERROR		cci_err_buf;
	short			i;
	char			flag_cci_exec = 0;
	char			err_buf[BUF_SIZE];
	DescInfo		desc_info;
	int				RevisedParamPos;

	if ( valueptr_ptr == NULL ) {
		odbc_set_diag(stmt->diag, "HY009", 0, NULL);
		goto error;
	}

	// odbc_execute의 putting param data 과정과 같다.
	if ( stmt->param_data.index != 0 ) {
		i = stmt->param_data.index;
		
		/* APD record */
		get_appl_desc_info(stmt->apd, i, &desc_info);

		/* IPD Record */
		odbc_get_desc_field(stmt->ipd, i, SQL_DESC_CONCISE_TYPE,
									&sql_type, 0, NULL);
			
		a_type = odbc_type_to_cci_a_type(desc_info.type);
		u_type = odbc_type_to_cci_u_type(sql_type);


		cci_value = odbc_value_to_cci(stmt->param_data.val.value, desc_info.type, 
								stmt->param_data.val.usedSize, desc_info.precision, 
								desc_info.scale);

		if ( cci_value == NULL ) {
			odbc_set_diag(stmt->diag, "HY000", 0, err_buf);
			goto error;
		}

		/*--
		 * cci_bind_param()을 사용할 때 SQLBindParam()에 의한 value pointer
		 * 를 바로 사용할 수 없다. 왜냐면 SQLBindParam()에 의한 value pointer
		 * 와 cci_bind_param()에서 사용하는 value pointer사이에는 type conversion
		 * 등이 발생하기 때문이다.
		 *--*/
		RevisedParamPos = revised_param_pos(stmt->revised_sql.org_param_pos, i);
		cci_rc = cci_bind_param(stmt->stmthd, RevisedParamPos, a_type, cci_value, u_type, 0);
		ERROR_GOTO(cci_rc, cci_error);

		NA_FREE(cci_value);

		// reset column data
		FreeStr(&stmt->param_data.val);
		stmt->param_data.index = 0;
	}

	if ( stmt->data_at_exec_state == STMT_NEED_NO_MORE_DATA ) {
		return ODBC_SUCCESS;
	} else if ( stmt->data_at_exec_state == STMT_NEED_DATA ) {
		/* Start of Data at exec */
		param = next_param_data_at_exec(stmt, 0);
	} else {
		param = next_param_data_at_exec(stmt, stmt->data_at_exec_state);
	}

	if ( param >= 0 ) {
		/* data_at_exec 상태에 있는 parameter가 존재 */
		stmt->data_at_exec_state = param;
		stmt->param_data.index = param;

		// spec에서는 SQLBindCol에 의한 data ptr도 얻어올 수 있다고 언급하는데, 
		// 이해하기 힘들다. SQLBindParam에 대해서만 얻어온다.
		odbc_get_desc_field(stmt->apd, param, SQL_DESC_DATA_PTR, valueptr_ptr, 0, NULL);
	
		return ODBC_NEED_DATA;
	}

	// indicator가 data_at_exec인 모든 param에 대해서 SQLPutData가 끝난 상태
	// execute를 실행한다.
	stmt->data_at_exec_state = STMT_NEED_NO_MORE_DATA;

	flag_cci_exec = get_flag_of_cci_execute(stmt);
	cci_rc = cci_execute(stmt->stmthd, flag_cci_exec, 0, &cci_err_buf);
	ERROR_GOTO(cci_rc, cci_error);

	stmt->tpl_number = cci_rc;
		
	if ( stmt->tpl_number == 0 && 
			(	stmt->stmt_type ==  SQLX_CMD_UPDATE || 
			stmt->stmt_type == SQLX_CMD_DELETE ) ) {
		if ( stmt->conn->env->attr_odbc_version == SQL_OV_ODBC2 ) {
			return ODBC_SUCCESS;		// for 2.x backward compatibility
		} else {
			return ODBC_NO_DATA;
		}
	}

	return ODBC_SUCCESS;
		
cci_error:
	if ( cci_rc < 0 ) 
		odbc_set_diag_by_cci(stmt->diag, cci_rc, cci_err_buf.err_msg);
error:
	FreeStr(&stmt->param_data.val);
	stmt->param_data.index = 0;
	NA_FREE(cci_value);
	return ODBC_ERROR;
}

/************************************************************************
* name: odbc_put_data
* arguments: 
* returns/side-effects: 
* description: 
*	data_at_exec parameter를 위해서 stmt->param_data에 data를 적재한다.
* NOTE: 
************************************************************************/
PUBLIC RETCODE odbc_put_data(ODBC_STATEMENT *stmt,
							 void		*data_ptr,
							 long		strlen_or_ind)
{

	MemcatImproved(&stmt->param_data.val, data_ptr, strlen_or_ind);

	return ODBC_SUCCESS;

}

/************************************************************************
* name: odbc_close_cursor
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC RETCODE odbc_close_cursor(ODBC_STATEMENT *stmt)
{
	if (stmt->stmthd > 0) {
		cci_close_req_handle(stmt->stmthd);
		stmt->stmthd = -1;
	}

	if ( stmt->result_type != TYPE_INFO )
	{
		odbc_auto_commit(stmt->conn);
	}

	// reset result set
	reset_result_set(stmt);
	//reset_cursor_state(stmt);

	

	return ODBC_SUCCESS;
}

/************************************************************************
* name: odbc_cancel
* arguments: 
* returns/side-effects: 
* description: 
*		1. Canceling Functions that Need Data
*		More functions
*			Canceling Asynchronous Processing
*			Canceling Functions in Multithread Applications
* NOTE: 
************************************************************************/
PUBLIC RETCODE odbc_cancel(ODBC_STATEMENT *stmt)
{

	if ( stmt->data_at_exec_state != STMT_NEED_NO_MORE_DATA ) {
		// reset column data
		FreeStr(&stmt->param_data.val);
		stmt->param_data.index = 0;
		stmt->data_at_exec_state = STMT_NEED_DATA;
	} else if ( stmt->attr_async_enable == SQL_ASYNC_ENABLE_ON ) {
		/* CHECK : 이로 인해서 발생하는 현상은? */
		if ( stmt->stmthd > 0 ) {
			cci_close_req_handle(stmt->stmthd);
			stmt->stmthd = -1;	
		}
	}

	return ODBC_SUCCESS;
}

/************************************************************************
* name: odbc_bulk_operations
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC RETCODE odbc_bulk_operations(ODBC_STATEMENT *stmt, 
									short     operation)
{
	RETCODE					rc;
	unsigned long			row_array_size, i;
	unsigned long			update_count =0;
	unsigned long			*RowFetchedPtr;
	short					j;
	DescInfo				*desc_info_ptr = NULL, bookmark_desc_info;
	void*					value_ptr;
	long*					ind_ptr;
	long					bookmark;
	T_CCI_A_TYPE			a_type;
	int						cci_rc = 0;
	T_CCI_ERROR				cci_err_buf;
	void*					cci_value = NULL;
	char					err_flag = 0;
	char					init_flag = 0;	/* if flag = 1, that is not first condition */
	unsigned short			*RowStatusPtr;
	char					query_buf[BUF_SIZE];
	char					buf[BUF_SIZE];
	char					param_buf[BUF_SIZE];
	char					attr_name[NAMEBUFLEN];
	
	
	row_array_size = stmt->ard->array_size;

	odbc_get_stmt_attr(stmt, SQL_ATTR_ROW_STATUS_PTR, &RowStatusPtr, 0, NULL);
	odbc_get_stmt_attr(stmt, SQL_ATTR_ROWS_FETCHED_PTR, &RowFetchedPtr, 0, NULL);

	get_appl_desc_info(stmt->ard, 0, &bookmark_desc_info);

	desc_info_ptr = UT_ALLOC( sizeof(DescInfo) * stmt->ard->max_count );

	for( j =1; j <= stmt->ard->max_count; ++j ) {
		get_appl_desc_info(stmt->ard, j, &desc_info_ptr[j-1]);
	}

	
	switch ( operation ) {
	case SQL_UPDATE_BY_BOOKMARK :
		for ( i = 1 ; i <= row_array_size; ++i ) {
			recalculate_bind_pointer(&bookmark_desc_info, i, (long*)&value_ptr, (long*)&ind_ptr, NULL);
			bookmark = *((long*)value_ptr);

			for( j =1; j <= stmt->ard->max_count; ++j ) {
				recalculate_bind_pointer(&desc_info_ptr[j-1], i, (long*)&value_ptr, (long*)&ind_ptr, NULL);
				a_type = odbc_type_to_cci_a_type(desc_info_ptr[j-1].type);

				if ( *ind_ptr == SQL_COLUMN_IGNORE ) {
					continue;
				}
				else if ( *ind_ptr == SQL_NULL_DATA ) {
					cci_rc = cci_cursor_update(stmt->stmthd, bookmark, j, a_type, (void*)NULL, &cci_err_buf);
					if ( cci_rc < 0 ) {
						odbc_set_diag_by_cci(stmt->diag, cci_rc, cci_err_buf.err_msg);
						err_flag = 1;
					}
				} else {
					cci_value = odbc_value_to_cci(value_ptr, desc_info_ptr[j-1].type, 
 									desc_info_ptr[j-1].length, desc_info_ptr[j-1].precision, desc_info_ptr[j-1].scale);
					cci_rc = cci_cursor_update(stmt->stmthd, bookmark, j, a_type, value_ptr, &cci_err_buf);
					if ( cci_rc < 0 ) {
						odbc_set_diag_by_cci(stmt->diag, cci_rc, cci_err_buf.err_msg);
						err_flag = 1;
					}

					NA_FREE(cci_value);
				}
			}
			++update_count;
			RowStatusPtr[i-1] = SQL_ROW_UPDATED;
		}
		break;

	case SQL_DELETE_BY_BOOKMARK :
		for ( i = 1 ; i <= row_array_size; ++i ) {
			recalculate_bind_pointer(&bookmark_desc_info, i, (long*)&value_ptr, (long*)&ind_ptr, NULL);
			bookmark = *((long*)value_ptr);
			
			rc = delete_row_by_cursor_pos(stmt, bookmark);
			ERROR_GOTO(rc, delete_error);

			++update_count;
			RowStatusPtr[i-1] = SQL_ROW_DELETED;
			continue;

delete_error:
			RowStatusPtr[i-1] = SQL_ROW_ERROR;
			err_flag = 1;
		}
		break;

	case SQL_ADD :
	/* 주의1 :select의 결과가 하나의 table에서 기인할 때만 유효하다.
	 * 그렇지 않은 경우 오작동
	 * 주의2 : base_column_name이 지원되지 않으므로 select attribute
	 * 에 alias걸면 안된다. 
	 */
	{
		int				addhd = -1;
		int				attr_num;
		int				cci_rc;
		T_CCI_ERROR		cci_err_buf;
		short			sql_type;
		T_CCI_A_TYPE	a_type;
		T_CCI_U_TYPE	u_type;
		void			*cci_value = NULL;
		RETCODE			rc;
		

		for ( i = 1; i <= row_array_size; ++i ) {
			recalculate_bind_pointer(&bookmark_desc_info, i, (long*)&value_ptr, (long*)&ind_ptr, NULL);
			bookmark = *((long*)value_ptr);

			init_flag = 0;
			sprintf(query_buf, "insert into %s(", stmt->ird->records->table_name);
			sprintf(param_buf, " values(");

			for ( j = 1, attr_num = 0; j <= stmt->ard->max_count; ++j ) {
				recalculate_bind_pointer(&desc_info_ptr[j-1], i, (long*)&value_ptr, (long*)&ind_ptr, NULL);
				if ( *ind_ptr == SQL_COLUMN_IGNORE ) {
					continue;
				} else {
					rc= odbc_get_desc_field(stmt->ird, j, SQL_DESC_BASE_COLUMN_NAME, attr_name, sizeof(attr_name), NULL);
					if ( init_flag == 1 ) {
						sprintf(buf, ", %s", attr_name);
						strcat(param_buf, ", ?");
						
					} else {
						sprintf(buf, "%s", attr_name);
						strcat(param_buf, "?");
						init_flag = 1;
					}
					strcat(query_buf, buf);
					++attr_num;
				}
				
			}
			strcat(query_buf, ") ");
			strcat(param_buf, ");");
			strcat(query_buf, param_buf);
			

			cci_rc = cci_prepare(stmt->conn->connhd, query_buf, 0, &cci_err_buf);
			ERROR_GOTO(cci_rc, add_error);

			addhd = cci_rc;

			for ( j = 1, attr_num = 0; j <= stmt->ard->max_count; ++j ) {
				recalculate_bind_pointer(&desc_info_ptr[j-1], i, (long*)&value_ptr, (long*)&ind_ptr, NULL);

				odbc_get_desc_field(stmt->ird, j, SQL_DESC_CONCISE_TYPE,
									&sql_type, 0, NULL);
			
				a_type = odbc_type_to_cci_a_type(desc_info_ptr[j-1].type);
				u_type = odbc_type_to_cci_u_type(sql_type);

				if ( *ind_ptr == SQL_COLUMN_IGNORE ) {
					continue;
				} else if ( *ind_ptr == SQL_NULL_DATA ) {
					cci_rc = cci_bind_param(addhd, j, a_type, NULL, u_type, 0);
					ERROR_GOTO(cci_rc, add_error);
				} else {
					cci_value = odbc_value_to_cci(value_ptr, desc_info_ptr[j-1].type, 
 									desc_info_ptr[j-1].length, desc_info_ptr[j-1].precision, desc_info_ptr[j-1].scale);
					if ( cci_value == NULL ) {
						goto add_error;
					}				
					// oid string checking
					if ( a_type == CCI_A_TYPE_STR && 
						( u_type == CCI_U_TYPE_CHAR || u_type == CCI_U_TYPE_VARNCHAR ) ) {
						if ( is_oidstr(cci_value) == _TRUE_ ) {
							u_type = CCI_U_TYPE_OBJECT;
						}
					}
				
					cci_rc = cci_bind_param(addhd, j, a_type, cci_value, u_type, 0);
					ERROR_GOTO(cci_rc, add_error);

					NA_FREE(cci_value);
				}
				
			}

			cci_rc = cci_execute(addhd, 0, 0, &cci_err_buf);
			ERROR_GOTO(cci_rc, add_error);


			if ( addhd > 0 ) {
				cci_close_req_handle(addhd);
				addhd = -1;
			}

			++update_count;
			RowStatusPtr[i-1] = SQL_ROW_ADDED;
			continue;

add_error:
			NA_FREE(cci_value);

			if ( addhd > 0 ) {
				cci_close_req_handle(addhd);
				addhd = -1;
			}
			RowStatusPtr[i-1] = SQL_ROW_ERROR;
			err_flag = 1;
		}

	}
		break;

	case SQL_FETCH_BY_BOOKMARK :
		/* array size와 무관하게 설정된 bookmark에 의해서 한 row씩 fetch
		 * 해 와야 함. */
		odbc_set_stmt_attr(stmt, SQL_ATTR_ROW_ARRAY_SIZE, (void*)1, 0, 1);

		for ( i = 1 ; i <= row_array_size; ++i ) {
			recalculate_bind_pointer(&bookmark_desc_info, i, (long*)&value_ptr, (long*)&ind_ptr, NULL);
			bookmark = *((long*)value_ptr);
			// cursor의 위치는 그대로.
			rc = odbc_fetch(stmt, SQL_FETCH_ABSOLUTE, bookmark, i-1, 1);
			if ( rc < 0 ) {
				err_flag = 1;
			} else {
				++update_count;
			}
		}
		
		odbc_set_stmt_attr(stmt, SQL_ATTR_ROW_ARRAY_SIZE, (void*)row_array_size, 0, 1);
		break;

	default :
		break;
	}

	NC_FREE(desc_info_ptr);

	*RowFetchedPtr = update_count;

	if ( err_flag == 1 ) return ODBC_ERROR;

	return ODBC_SUCCESS;
}

/************************************************************************
* name: odbc_set_pos
* arguments: 
*	lock_type  - ignored...
* returns/side-effects: 
* description: 
* NOTE: 
*	- refresh일 때는 operation이 반영이 안된다.
*	- 누군가 bookmark를 조정하면 오작동한다.  절대로 bookmark를 조절해서는 
*	안된다.
************************************************************************/
PUBLIC RETCODE odbc_set_pos(ODBC_STATEMENT *stmt,
							unsigned short	row_number,
							unsigned short	operation,
							unsigned short  lock_type)
{
	RETCODE					rc;
	DescInfo				*desc_info_ptr = NULL, bookmark_desc_info;
	unsigned short			*RowOperationPtr;
	unsigned short			*RowStatusPtr;
	unsigned long			row_array_size, i;
	short					max_col_num, j;
	char					error_flag;

	row_array_size = stmt->ard->array_size;
	max_col_num =  stmt->ard->max_count;

	odbc_get_stmt_attr(stmt, SQL_ATTR_ROW_STATUS_PTR, &RowStatusPtr, 0, NULL);
	odbc_get_stmt_attr(stmt, SQL_ATTR_ROW_OPERATION_PTR, &RowOperationPtr, 0, NULL);
	
	get_appl_desc_info(stmt->ard, 0, &bookmark_desc_info);



	switch ( operation ) {
	case SQL_POSITION :
		/* There is no operation. Just emulation. */
		break;

	case SQL_REFRESH :
		// cursor의 위치는 그대로.
		odbc_fetch(stmt, SQL_FETCH_RELATIVE, 0, 0, 1);
		break;

	case SQL_UPDATE :
		desc_info_ptr = UT_ALLOC( sizeof(DescInfo) * max_col_num);

		for( j =1; j <= max_col_num; ++j ) {
			get_appl_desc_info(stmt->ard, j, &desc_info_ptr[j-1]);
		}
		if ( row_number == 0 ) {
			for ( i = 0; i < row_array_size; ++ i ) {
				if ( RowOperationPtr[i] == SQL_ROW_IGNORE ) {
					continue;
				}
				rc = odbc_set_pos_update(stmt, &bookmark_desc_info, desc_info_ptr, i+1);
				if ( rc < 0 ) {
					error_flag = 1;
					RowStatusPtr[i] = SQL_ROW_ERROR;
				} else {
					RowStatusPtr[i] = SQL_ROW_UPDATED;
				}
			}
		} else {
			rc = odbc_set_pos_update(stmt, &bookmark_desc_info, desc_info_ptr, row_number);
			if ( rc < 0 ) {
				error_flag = 1;
				RowStatusPtr[row_number-1] = SQL_ROW_ERROR;
			} else {
				RowStatusPtr[row_number-1] = SQL_ROW_UPDATED;
			}
		}

		NC_FREE(desc_info_ptr);
		break;

	case SQL_DELETE :
		if ( row_number == 0 ) {
			for ( i = 0; i < row_array_size; ++ i ) {
				if ( RowOperationPtr[i] == SQL_ROW_IGNORE ) {
					continue;
				}
				rc = odbc_set_pos_delete(stmt, &bookmark_desc_info, i+1);
				if ( rc < 0 ) {
					error_flag = 1;
					RowStatusPtr[i] = SQL_ROW_ERROR;
				} else {
					RowStatusPtr[i] = SQL_ROW_DELETED;
				}
			}
		} else {
			rc = odbc_set_pos_delete(stmt, &bookmark_desc_info, row_number);
			if ( rc < 0 ) {
				error_flag = 1;
				RowStatusPtr[row_number-1] = SQL_ROW_ERROR;
			} else {
				RowStatusPtr[row_number-1] = SQL_ROW_DELETED;
			}
		}
		break;
	}

	if ( error_flag == 1 ) return ODBC_ERROR;

	return ODBC_SUCCESS;
}

/************************************************************************
* name: reset_result_set
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
*	IRD는 SQLPrepare 시점에서 이루어지므로, 여기서 IRD를 delete해서는
*	안된다.  왜냐면 SQLExecute도 reset_result_set을 사용하고, 
*	SQLCloseCursor시 IRD가 delete되지 않기 때문이다.
************************************************************************/
PUBLIC void reset_result_set(ODBC_STATEMENT*	stmt)
{
	if ( stmt->result_type == NULL_RESULT ) return;
	
		// delete catalog result set
	if ( stmt->catalog_result.value != NULL ) {
		free_catalog_result(stmt->catalog_result.value, stmt->result_type);
		stmt->catalog_result.value = NULL;
		stmt->catalog_result.current = NULL;
	}

	// reset result set state
	stmt->result_type = NULL_RESULT;
	stmt->data_at_exec_state = STMT_NEED_NO_MORE_DATA;
	stmt->tpl_number = 0;
	stmt->current_tpl_pos = 0;
	NA_FREE(stmt->cursor);

	// reset column data
	free_column_data(&stmt->column_data, RESET);
}

/************************************************************************
* name: create_ird
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC void create_ird(ODBC_STATEMENT *stmt, T_CCI_COL_INFO* cci_col_info, int column_number)
{
	T_CCI_U_TYPE	cci_u_type;
	short			col_type;
	int				col_scale;
	int				col_precision;
	short			col_nullable = SQL_NULLABLE_UNKNOWN;
	short			col_updatable = SQL_ATTR_READONLY;
	char			*table_name;
	char			*col_name;
	short			i;

	if ( stmt == NULL || cci_col_info == NULL ) return;

	if ( cci_col_info != NULL ) {

		if ( GET_STAT_STMT_TYPE(stmt->stmt_type) ) {
			col_type = SQL_VARCHAR;
			col_precision = MAX_CUBRID_CHAR_LEN;
			col_scale = 0;
			col_nullable = SQL_NULLABLE_UNKNOWN;
			col_updatable = SQL_ATTR_READONLY;
			col_name = "GET_STAT";
			odbc_set_ird(stmt, 1, col_type, "", col_name, col_precision, (short)col_scale, col_nullable, col_updatable);
		} else if ( METHOD_STMT_TYPE(stmt->stmt_type) ) {
			col_type = SQL_VARCHAR;
			col_precision = MAX_CUBRID_CHAR_LEN;
			col_scale = 0;
			col_nullable = SQL_NULLABLE_UNKNOWN;
			col_updatable = SQL_ATTR_READONLY;
			col_name = "METHOD_RESULT";
			odbc_set_ird(stmt, 1, col_type, "", col_name, col_precision, (short)col_scale, col_nullable, col_updatable);
		} else {
			// SQLX_CMD_SELECT

			if ( stmt->attr_use_bookmark == SQL_UB_VARIABLE ) {
				create_bookmark_ird(stmt);
			}

			if ( IS_UPDATABLE(stmt->attr_cursor_type) ) {
				if ( cci_is_updatable(stmt->stmthd) ) {
					col_updatable = SQL_ATTR_WRITE;
				} else {
					col_updatable = SQL_ATTR_READONLY;
					stmt->attr_concurrency = SQL_CONCUR_READ_ONLY;
					stmt->attr_cursor_sensitivity = SQL_INSENSITIVE;
					stmt->attr_cursor_type = SQL_CURSOR_STATIC;
				}
			}
			
			for( i =1; i <= (signed)column_number; ++i ) {
				
				cci_u_type = CCI_GET_RESULT_INFO_TYPE(cci_col_info, i);
				
				// OBJECT는 string으로 bind되기 때문에 display를 위해서 precision을
				// 32로 설정하였다.
				
				if ( cci_u_type == CCI_U_TYPE_OBJECT ) {
					cci_u_type = CCI_U_TYPE_CHAR;
					col_precision = 32;
					col_scale = 0;
				} else if ( CCI_IS_COLLECTION_TYPE(cci_u_type) ) {
							/* XXX : deprecated
							cci_u_type == CCI_U_TYPE_SET ||
							cci_u_type == CCI_U_TYPE_MULTISET ||
							cci_u_type == CCI_U_TYPE_SEQUENCE ) {
							*/
					cci_u_type = CCI_U_TYPE_STRING;
					col_precision = MAX_CUBRID_CHAR_LEN;
					col_scale = 0;
				} else {
					col_precision = CCI_GET_RESULT_INFO_PRECISION(cci_col_info, i);
					// revise col_precision
					col_precision = ( col_precision < 0 ) ? DEFAULT_COL_PRECISION : col_precision;
#ifdef DELPHI
					col_type = odbc_type_by_cci( cci_u_type, col_precision );
#endif
					if ( cci_u_type == CCI_U_TYPE_STRING || cci_u_type == CCI_U_TYPE_CHAR )	{
						if ( col_precision >= stmt->conn->max_string_length )
							col_precision = stmt->conn->max_string_length;
					}
					col_scale = CCI_GET_RESULT_INFO_SCALE(cci_col_info, i);
				}

				col_name = CCI_GET_RESULT_INFO_NAME(cci_col_info, i);
				table_name = CCI_GET_RESULT_INFO_CLASS_NAME(cci_col_info, i);

				if (  CCI_GET_RESULT_INFO_IS_NON_NULL(cci_col_info, i) == 0 ) { // nullable
					col_nullable = SQL_NULLABLE;
				} else if ( CCI_GET_RESULT_INFO_IS_NON_NULL(cci_col_info, i) == 1 ) {
					col_nullable = SQL_NO_NULLS;
				} else {
					col_nullable = SQL_NULLABLE_UNKNOWN;
				}
#ifndef DELPHI
				col_type = odbc_type_by_cci(cci_u_type, col_precision);
#endif

				odbc_set_ird(stmt, i, col_type, table_name, col_name, col_precision, (short)col_scale, col_nullable, col_updatable);
						
			}
		}
	}
}

/************************************************************************
* name: next_param_data_at_exec
* arguments: 
* returns/side-effects: 
*
* description: 
*	APD에서 param_pos이후의 record에 대해서 indicator가 DATA_AT_EXEC 인 것을
*	찾는다.  찾으면 record number, else -1
* NOTE: 
************************************************************************/
PRIVATE short next_param_data_at_exec(ODBC_STATEMENT *stmt, 
											short param_pos)
{
	ODBC_RECORD	*record = NULL;
	int i;


	if ( param_pos > stmt->apd->max_count ) {
		return -1;
	}

	for ( i = param_pos +1; i <= stmt->apd->max_count; ++i ) {
		record = find_record_from_desc(stmt->apd, i);
		if ( record != NULL ) {
			if ( IND_IS_DATA_AT_EXEC(*(record->indicator_ptr)) ) {
				return i;
			}
		}
	}

	return -1;
}

PUBLIC void free_column_data(COLUMN_DATA *data, int option)
{
	if ( data == NULL ) return;

	switch ( option ) {
	case INIT :
	case RESET :
	case FREE_MEMBER :
		data->current_pt = NULL;
		data->column_no = 0;
		data->remain_length = 0;
		data->prev_return_status = -1;
		break;
	
	case FREE_ALL :
	default :
		data->current_pt = NULL;
		data->column_no = 0;
		data->remain_length = 0;
		data->prev_return_status = -1;
		UT_FREE(data);
		
		break;
	}
}


/************************************************************************
* name: get_appl_desc_info
* arguments: 

*	col_index - parameter col index, starting from 1
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PRIVATE void get_appl_desc_info(ODBC_DESC	*desc, 
				  short		col_index, 
				  DescInfo			*desc_info_ptr)
{
	if ( desc == NULL || desc_info_ptr == NULL ) return;

	memset(desc_info_ptr, 0, sizeof(DescInfo));


	desc_info_ptr->bind_type = desc->bind_type;  
						/* 0 means single or column wise, >0 means row wise */
	if ( desc->bind_offset_ptr == NULL ) {
		desc_info_ptr->offset_size = 0;
	} else {
		desc_info_ptr->offset_size = *(desc->bind_offset_ptr);
	}

	if ( desc_info_ptr == NULL ) return;
	
	odbc_get_desc_field(desc, col_index, SQL_DESC_CONCISE_TYPE,
									&(desc_info_ptr->type), 0, NULL);
	odbc_get_desc_field(desc, col_index, SQL_DESC_DATA_PTR,
									&(desc_info_ptr->value_ptr), 0, NULL);
	odbc_get_desc_field(desc, col_index, SQL_DESC_INDICATOR_PTR,
									&(desc_info_ptr->ind_ptr), 0, NULL);
	odbc_get_desc_field(desc, col_index, SQL_DESC_OCTET_LENGTH,
									&(desc_info_ptr->length), 0, NULL);
	odbc_get_desc_field(desc, col_index, SQL_DESC_PRECISION, 
									&(desc_info_ptr->precision), 0, NULL);
	odbc_get_desc_field(desc, col_index, SQL_DESC_SCALE,
									&(desc_info_ptr->scale), 0, NULL);
	odbc_get_desc_field(desc, col_index, SQL_DESC_OCTET_LENGTH_PTR, &(desc_info_ptr->octet_len_ptr), 0, NULL);
}

/************************************************************************
* name: recalculate_bind_pointer
* arguments: 
*	row_index - array index, starting from 1
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PRIVATE void recalculate_bind_pointer(DescInfo			*desc_info_ptr,
									  unsigned long		row_index, 
									  long				*value_addr,
									  long				*ind_addr,
									  long				*octet_len_addr)
{
	long element_size;

	if ( desc_info_ptr->value_ptr == NULL ) {
		(void*)*value_addr = NULL;
		(void*)*ind_addr = NULL;
		if (octet_len_addr) {
		  (void*) *octet_len_addr = NULL;
		}
	} else {
		if ( desc_info_ptr->bind_type == SQL_PARAM_BIND_BY_COLUMN ) {
			*value_addr = (long)desc_info_ptr->value_ptr + desc_info_ptr->offset_size + (row_index -1)*(desc_info_ptr->length);
			*ind_addr = (long)desc_info_ptr->ind_ptr + desc_info_ptr->offset_size + (row_index-1)*sizeof(long);
			if (octet_len_addr) {
			  if (desc_info_ptr->octet_len_ptr)
			    *octet_len_addr = (long)desc_info_ptr->octet_len_ptr + desc_info_ptr->offset_size + (row_index-1)*sizeof(long);
			  else
			    *octet_len_addr = 0;
			}
		} else {
			element_size = desc_info_ptr->bind_type;
			*value_addr = (long)desc_info_ptr->value_ptr + desc_info_ptr->offset_size + (row_index-1)*element_size;
			*ind_addr = (long)desc_info_ptr->ind_ptr + desc_info_ptr->offset_size + (row_index-1)*element_size;
			if (octet_len_addr) {
			  if (desc_info_ptr->octet_len_ptr)
			    *octet_len_addr = (long)desc_info_ptr->octet_len_ptr + desc_info_ptr->offset_size + (row_index-1)*element_size;
			  else
			    *octet_len_addr = 0;
			}
		}
	}
}
									
/************************************************************************
 * name:  reset_revised_sql
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE void reset_revised_sql(ODBC_STATEMENT *stmt)
{
	if ( stmt == NULL ) return;
	
	NA_FREE(stmt->revised_sql.sql_text);
	NA_FREE(stmt->revised_sql.oid_param_pos);
	NA_FREE(stmt->revised_sql.oid_param_val);
	NA_FREE(stmt->revised_sql.org_param_pos);

	stmt->revised_sql.oid_param_num = 0;

}

/************************************************************************
 * name:  reset_revised_sql
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE int revised_param_pos(char *param_pos, int index)
{
	int i;
	short rv;
	char *pt;
	char buf[128];
	int revised_pos = 0;

	if ( param_pos == NULL ) return 0;

	pt = param_pos;
	i = 0;

	while ( (rv = element_from_setstring(&pt, buf)) > 0 ) {
		++i;
		if ( i == index ) {
			revised_pos = atoi(buf);
			break;
		}
	}

	return revised_pos;

}

/************************************************************************
 * name:  get_flag_of_cci_prepare
 * arguments:
 * returns/side-effects:
 *		flag of cci_execute()
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE char get_flag_of_cci_prepare(ODBC_STATEMENT *stmt)
{
	if ( stmt == NULL ) return 0;

	if ( IS_UPDATABLE(stmt->attr_cursor_type) ) {
		return (CCI_PREPARE_UPDATABLE | CCI_PREPARE_INCLUDE_OID);
	}

	return 0;
}

/************************************************************************
 * name:  get_flag_of_cci_execute
 * arguments:
 * returns/side-effects:
 *		flag of cci_execute()
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE char get_flag_of_cci_execute(ODBC_STATEMENT *stmt)
{
	if ( stmt == NULL ) return 0;

	if ( stmt->attr_async_enable == SQL_ASYNC_ENABLE_ON ) {
		return ( CCI_EXEC_ASYNC | CCI_EXEC_QUERY_ALL) ;
	} else {
		return CCI_EXEC_QUERY_ALL;
	}
}

/************************************************************************
 * name:  create_bookmark_ird
 * arguments:
 * returns/side-effects:
 * description:
 *		0번 ird record는 BOOKMARK를 위한 것이다.
 * NOTE:
 ************************************************************************/
PRIVATE void create_bookmark_ird(ODBC_STATEMENT *stmt)
{
	short		verbose_type;
	long		display_size;
	long		octet_length;
	ODBC_RECORD		*record;
	short			type;
	long			precision;
	short			scale;

	if ( stmt == NULL ) return;
	
	record = find_record_from_desc(stmt->ird, 0);
	if ( record == NULL ) {
	 	odbc_alloc_record(stmt->ird, &record, 0);
	}

	type = SQL_VARBINARY;
	precision = 4;
	scale = 0;
	verbose_type = odbc_concise_to_verbose_type(type);
	display_size = odbc_display_size(type, precision);
	octet_length = odbc_octet_length(type, precision);
	
	odbc_set_desc_field(stmt->ird, 0, SQL_DESC_CONCISE_TYPE, (SQLPOINTER)type, 0, 1);
	odbc_set_desc_field(stmt->ird, 0, SQL_DESC_TYPE_NAME, (SQLPOINTER)odbc_type_name(type), SQL_NTS, 1);
	odbc_set_desc_field(stmt->ird, 0, SQL_DESC_TYPE,(SQLPOINTER)verbose_type, 0, 1);
	if ( IS_STRING_TYPE(type) || IS_BINARY_TYPE(type) ) {
		odbc_set_desc_field(stmt->ird, 0, SQL_DESC_PRECISION, (SQLPOINTER)0, 0, 1);
		// precision에 대해서 정의하고 있지 않다.
	} else if ( odbc_is_valid_sql_date_type(type) ) {
		odbc_set_desc_field(stmt->ird, 0, SQL_DESC_PRECISION, (SQLPOINTER)0, 0, 1);
		// CUBRID은 date type에 대해서 precision(for second)은 0이다.
		// date type에 대한 length는 char형색의 display size와 같다.
	} else {
		odbc_set_desc_field(stmt->ird, 0, SQL_DESC_PRECISION, (SQLPOINTER)precision, 0, 1);
	}
	odbc_set_desc_field(stmt->ird, 0, SQL_DESC_LENGTH, (SQLPOINTER)display_size, 0, 1);
	odbc_set_desc_field(stmt->ird, 0, SQL_DESC_OCTET_LENGTH, (SQLPOINTER)octet_length, 0, 1);
	odbc_set_desc_field(stmt->ird, 0, SQL_DESC_SCALE, (SQLPOINTER)scale, 0, 1);
	odbc_set_desc_field(stmt->ird, 0, SQL_DESC_DISPLAY_SIZE, (SQLPOINTER)display_size, 0, 1);
	odbc_set_desc_field(stmt->ird, 0, SQL_DESC_UNSIGNED, (SQLPOINTER)SQL_TRUE, 0, 1);

}

/************************************************************************
 * name:  make_param_array
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 *		이미 param_array에는 memory가 할당된 상태이다.
 ************************************************************************/
PRIVATE void make_param_array(ODBC_STATEMENT	*stmt, 
							  short				column_index, 
							  DescInfo			*desc_info, 
							  ParamArray		*param_array,
							  int				array_size)
{
	unsigned long h, i;
	void*	value_ptr;
	long*	ind_ptr;
	void*	cci_value = NULL;
	unsigned short	*ParamOperationArrayPtr;

	odbc_get_stmt_attr(stmt, SQL_ATTR_PARAM_OPERATION_PTR, &ParamOperationArrayPtr, 0, NULL);

	memory_alloc_param_array(param_array, array_size);
	
	for ( h = 1, i = 0; h <= stmt->apd->array_size; ++h ) {
		if ( stmt->apd->array_size > 1 ) {
			if ( ParamOperationArrayPtr != NULL &&
				ParamOperationArrayPtr[h-1] == SQL_PARAM_IGNORE ) {
				continue;
			}
		}
		
		recalculate_bind_pointer(desc_info, h, (long*)&value_ptr, (long*)&ind_ptr, NULL);

		if ( ind_ptr != NULL && *ind_ptr == SQL_NULL_DATA ) {
			param_array->ind_array[i] = 1;
			((char**)param_array->value_array)[i] = NULL;
		} else {
			param_array->ind_array[i] = 0;
			odbc_value_to_cci2( param_array->value_array, i, value_ptr, desc_info->type, 
 							desc_info->length, desc_info->precision, desc_info->scale);
		}
		++i;
	}

}

/************************************************************************
 * name:  
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE void memory_alloc_param_array(ParamArray *param_array, int array_size)
{
	param_array->ind_array = UT_ALLOC( sizeof(int)*array_size );

	switch ( param_array->value_type ) {
	case CCI_A_TYPE_STR :
		param_array->value_array = UT_ALLOC( sizeof(char*) * array_size );
		break;
	case CCI_A_TYPE_INT :
		param_array->value_array = UT_ALLOC( sizeof(int) * array_size );
		break;
	case CCI_A_TYPE_FLOAT :
		param_array->value_array = UT_ALLOC( sizeof(float) * array_size );
		break;
	case CCI_A_TYPE_DOUBLE :
		param_array->value_array = UT_ALLOC( sizeof(double) * array_size );
		break;
	case CCI_A_TYPE_BIT :
		param_array->value_array = UT_ALLOC( sizeof(T_CCI_BIT) * array_size );
		break;
	case CCI_A_TYPE_SET :
		param_array->value_array = UT_ALLOC( sizeof(T_CCI_SET) * array_size );
		break;
	case CCI_A_TYPE_DATE :
		param_array->value_array = UT_ALLOC( sizeof(T_CCI_DATE) * array_size );
		break;
	}

}

/************************************************************************
 * name:  delete_row_by_cursor_pos
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE RETCODE delete_row_by_cursor_pos(ODBC_STATEMENT *stmt,
										unsigned long		cursor_pos)
{
	int						cci_rc = 0;
	T_CCI_ERROR				cci_err_buf;
	char					oid_buf[32];

	cci_rc = cci_cursor(stmt->stmthd, cursor_pos, CCI_CURSOR_FIRST, &cci_err_buf);
	ERROR_GOTO(cci_rc, cci_error);

	cci_rc = cci_fetch(stmt->stmthd, &cci_err_buf);
	ERROR_GOTO(cci_rc, cci_error);

	cci_rc = cci_get_cur_oid(stmt->stmthd, oid_buf);
	ERROR_GOTO(cci_rc, cci_error);

	cci_rc = cci_oid(stmt->conn->connhd, CCI_OID_DROP, oid_buf, &cci_err_buf);
	ERROR_GOTO(cci_rc, cci_error);

	return ODBC_SUCCESS;
	
cci_error :
	if ( cci_rc < 0 ) 
		odbc_set_diag_by_cci(stmt->diag, cci_rc, cci_err_buf.err_msg);
	return ODBC_ERROR;
}


/************************************************************************
 * name:  odbc_set_pos_update
 * arguments:
 *		row_pos - row position in ard. (starting from 1)
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE RETCODE odbc_set_pos_update(ODBC_STATEMENT *stmt, 
									DescInfo *bookmark_desc_info_ptr,
									DescInfo *desc_info_ptr, 
									unsigned long row_pos)
{

	short					max_col_num, j;
	void*					value_ptr;
	long*					ind_ptr;
	long					bookmark;
	T_CCI_A_TYPE			a_type;
	int						cci_rc = 0;
	T_CCI_ERROR				cci_err_buf;
	void*					cci_value = NULL;

	if ( stmt == NULL || bookmark_desc_info_ptr == NULL || desc_info_ptr == NULL ) return ODBC_ERROR;

	max_col_num =  stmt->ard->max_count;

	recalculate_bind_pointer(bookmark_desc_info_ptr, row_pos, (long*)&value_ptr, (long*)&ind_ptr, NULL);
	bookmark = *((long*)value_ptr);
	
	for( j =1; j <= stmt->ard->max_count; ++j ) {
		recalculate_bind_pointer(&desc_info_ptr[j-1], row_pos, (long*)&value_ptr, (long*)&ind_ptr, NULL);
		a_type = odbc_type_to_cci_a_type(desc_info_ptr[j-1].type);

		if ( *ind_ptr == SQL_COLUMN_IGNORE ) {
			continue;
		} else if ( *ind_ptr == SQL_NULL_DATA ) {
			cci_rc = cci_cursor_update(stmt->stmthd, bookmark, j, a_type, (void*)NULL, &cci_err_buf);
			ERROR_GOTO(cci_rc, cci_error);
		} else {
			cci_value = odbc_value_to_cci(value_ptr, desc_info_ptr[j-1].type, 
 									desc_info_ptr[j-1].length, desc_info_ptr[j-1].precision, desc_info_ptr[j-1].scale);

			cci_rc = cci_cursor_update(stmt->stmthd, bookmark, j, a_type, value_ptr, &cci_err_buf);
			ERROR_GOTO(cci_rc, cci_error);

			NA_FREE(cci_value);
			
		}
	}

	return ODBC_SUCCESS;

cci_error :
		NA_FREE(cci_value);
		odbc_set_diag_by_cci(stmt->diag, cci_rc, cci_err_buf.err_msg);
		return ODBC_ERROR;
}


/************************************************************************
 * name:  odbc_set_pos_delete
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE RETCODE odbc_set_pos_delete(ODBC_STATEMENT *stmt, 
									DescInfo		*bookmark_desc_info_ptr,
									unsigned long row_pos)
{
	RETCODE rc;
	void*					value_ptr;
	long*					ind_ptr;
	long					bookmark;

	if ( stmt == NULL ) return ODBC_ERROR;

	recalculate_bind_pointer(bookmark_desc_info_ptr, row_pos, (long*)&value_ptr, (long*)&ind_ptr, NULL);
	bookmark = *((long*)value_ptr);
			
	rc = delete_row_by_cursor_pos(stmt, bookmark);
	if ( rc < 0 ) return ODBC_ERROR;

	return ODBC_SUCCESS;
}

/************************************************************************
 * name:  free_param_array
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PRIVATE void free_param_array(ParamArray *param_ptr, 
							  int		param_count,
							  int		array_count)
{
	int i;
	int j;
	char** cpt;
	T_CCI_BIT*	bpt;

	if ( param_ptr == NULL || param_count < 0 ) return;

	for ( i = 0; i < param_count ; ++i) { 
		switch ( param_ptr[i].value_type ) {
		case CCI_A_TYPE_STR :
			cpt = param_ptr[i].value_array;
			for ( j = 0; j < array_count; ++j ) {
				NC_FREE( cpt[j] );
			}
		break;

		case CCI_A_TYPE_BIT :
			bpt = param_ptr[i].value_array;
			for ( j = 0; j < array_count; ++j ) {
				NC_FREE(bpt[i].buf);
			}
			break;
		
		}

		NC_FREE(param_ptr[i].value_array);
		NC_FREE(param_ptr[i].ind_array);
	}


	free(param_ptr);
}


#ifdef _DEBUG
PRIVATE void debug_print_appl_desc(DescInfo *desc_info, 
								   void *value_ptr,
								   long *ind_ptr)
{
	switch ( desc_info->type ) {
	case SQL_C_SHORT :		// for 2.x backward compatibility
	case SQL_C_SSHORT :
	case SQL_C_USHORT :
		printf("[short] %d ||",  *(short*)value_ptr);
		break;

	case SQL_C_STINYINT :
	case SQL_C_UTINYINT :
	case SQL_C_TINYINT :		// for 2.x backward compatibility
		printf("[short] %d ||",  *(char*)value_ptr);
		break;
	
	case SQL_C_LONG :		// for 2.x backward compatibility
	case SQL_C_SLONG :
	case SQL_C_ULONG :
	case SQL_C_SBIGINT :		// warning : __int64에 대해서 고려 안됨
	case SQL_C_UBIGINT :
		printf("[long] %ld || ",  *(long*)value_ptr);
		break;

	/*---------------------------------------------------------------
	 *					floating point type
	 *--------------------------------------------------------------*/
	case SQL_C_FLOAT :
		printf("[float] %f || ",  *(float*)value_ptr);
		break;

	case SQL_C_DOUBLE :
		printf("[double] %lf || ",  *(double*)value_ptr);
		break;

	/*---------------------------------------------------------------
	 *					char & binary type
	 *--------------------------------------------------------------*/
	case SQL_C_CHAR :
		printf("[string] %s || ", (char*)value_ptr);
		break;
	}
}

#endif

PRIVATE short default_type_to_c_type(short value_type, short parameter_type)
{
	switch (parameter_type) {
	case SQL_CHAR:
	case SQL_VARCHAR:
	case SQL_LONGVARCHAR:
		return SQL_C_CHAR;
	case SQL_WCHAR:
	case SQL_WVARCHAR:
	case SQL_WLONGVARCHAR:
		return SQL_C_WCHAR;
	case SQL_DECIMAL:
	case SQL_NUMERIC:
		return SQL_C_CHAR;
	case SQL_BIT:
		return SQL_C_BIT;
	case SQL_TINYINT:
		return SQL_C_TINYINT;
	case SQL_SMALLINT:
		return SQL_C_SSHORT;
	case SQL_INTEGER:
		return SQL_C_SLONG;
	case SQL_BIGINT:
		return SQL_C_SBIGINT;
	case SQL_REAL:
		return SQL_C_FLOAT;
	case SQL_FLOAT:
	case SQL_DOUBLE:
		return SQL_C_DOUBLE;
	case SQL_BINARY:
	case SQL_VARBINARY:
	case SQL_LONGVARBINARY:
		return SQL_C_BINARY;
	case SQL_TYPE_DATE:
		return SQL_C_TYPE_DATE;
	case SQL_TYPE_TIME:
		return SQL_C_TYPE_TIME;
	case SQL_TYPE_TIMESTAMP:
		return SQL_C_TYPE_TIMESTAMP;
	case SQL_GUID:
		return SQL_C_CHAR;
	}
	return value_type;
}
