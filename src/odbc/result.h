#ifndef	__ODBC_RESULT_HEADER		/* to avoid multiple inclusion */
#define	__ODBC_RESULT_HEADER

#include		"portable.h"
#include		"stmt.h"

PUBLIC RETCODE odbc_bind_col(ODBC_STATEMENT *stmt,
							 unsigned short column_num,
							 short target_type,
							 void* target_value_ptr,
							 long buffer_len,
							 long *strlen_indicator);
PUBLIC RETCODE odbc_describe_col(ODBC_STATEMENT	*stmt,
								 short	column_number,
								 char		*column_name,
								 short	buffer_length,
								 short	*name_length_ptr,
								 short	*data_type_ptr,
								 unsigned long	*column_size_ptr,
								 short	*decimal_digits_ptr,
								 short	*nullable_ptr);
PUBLIC RETCODE odbc_col_attribute(ODBC_STATEMENT	*stmt,
								  unsigned short		column_number,
								  unsigned short		field_identifier,
								  void*		str_value_ptr,
								  short		buffer_length,
								  short		*string_length_ptr,
								  void*		num_value_ptr);
PUBLIC RETCODE odbc_row_count(ODBC_STATEMENT* stmt,
							   long	*row_count);
PUBLIC RETCODE odbc_num_result_cols(ODBC_STATEMENT* stmt,
							   short	*column_count);
PUBLIC RETCODE odbc_fetch(ODBC_STATEMENT *stmt,
						  short		fetch_orientation,
						  long		fetch_offset, 
						  long		bind_offset,
						  short		flag_cursor_move);
PUBLIC RETCODE odbc_get_data(ODBC_STATEMENT *stmt,
							 short			col_number,
							 short			target_type,
							 void*			bound_ptr,
							 long			buffer_length,
							 long*			str_ind_ptr);
PUBLIC RETCODE odbc_more_results(ODBC_STATEMENT *stmt);
PUBLIC RETCODE move_cursor(	int				req_handle,
							unsigned long			*current_tpl_pos,
							ODBC_DIAG		*diag);
PUBLIC RETCODE move_advanced_cursor(ODBC_STATEMENT *stmt,
							long			*current_tpl_pos,
							short			fetch_orientation,
							long			fetch_offset);
PUBLIC RETCODE fetch_tuple(int req_handle, ODBC_DIAG *diag, long sensitivity);
PUBLIC RETCODE get_data(ODBC_STATEMENT *stmt, short row_index, 
							short col_index);

#endif	/* ! __ODBC_RESULT_HEADER */
