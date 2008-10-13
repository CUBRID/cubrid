#ifndef	__ODBC_DESC_HEADER		/* to avoid multiple inclusion */
#define	__ODBC_DESC_HEADER

#include		"conn.h"
#include		"portable.h"
#include		"diag.h"
#include		"stmt.h"

/*------------------------------------------------------------------------
	STRUCT : st_odbc_record (ODBC_RECORD)
 *-----------------------------------------------------------------------*/
typedef struct st_odbc_record {

	/* Driver-specific members */
	struct st_odbc_desc			*desc;
	struct st_odbc_record			*next;
	short						record_number;

	/* supported descriptor record field */
	char						*base_column_name;
	long						case_sensitive;
	short						concise_type;
	void						*data_ptr;
	short						datetime_interval_code;
	long						datetime_interval_precision;
	long						display_size;
	short						fixed_prec_scale;
	long						*indicator_ptr;
	unsigned long				length;
	char						*literal_prefix;
	char						*literal_suffix;
	char						*local_type_name;
	char						*name;
	short						nullable;
	long						num_prec_radix;
	long						octet_length;
	long						*octet_length_ptr;
	short						parameter_type;
	short						precision;
	short						scale;
	short						searchable;
	char						*table_name;
	short						type;
	char						*type_name;
	short						unnamed;
	short						unsigned_type;

	/* not supported */
	long						auto_unique_value;
	char						*base_table_name;
	char						*catalog_name;
	short						rowver;
	char						*schema_name;
	short						updatable;

}  ODBC_RECORD;


typedef struct st_odbc_desc {
	short					handle_type;
	struct st_diag				*diag;

	struct st_odbc_statement	*stmt;
	struct st_odbc_connection	*conn;  // for explicitly allocated descriptor
	struct st_odbc_desc		*next;  // for explicitly allocated descriptor

	/* header fields */
	short				alloc_type;
	unsigned long		array_size;
	unsigned short		*array_status_ptr;
	long				*bind_offset_ptr;
	long				bind_type;
	short				max_count;
	unsigned long		*rows_processed_ptr;

	struct st_odbc_record	*records;

} ODBC_DESC;

PUBLIC RETCODE odbc_alloc_desc(struct st_odbc_connection *conn,
						ODBC_DESC	**desc_ptr);
PUBLIC RETCODE odbc_free_desc(ODBC_DESC *desc);
PUBLIC RETCODE odbc_alloc_record(ODBC_DESC *desc, ODBC_RECORD **rec, 
								 int rec_number);
PUBLIC RETCODE odbc_free_record(ODBC_RECORD *record);
PUBLIC RETCODE odbc_free_all_records(ODBC_RECORD *head_record);
PUBLIC RETCODE odbc_get_desc_field(ODBC_DESC *desc, 
								   short rec_number,
								   short field_id,
								   void	 *value_ptr,
								   long buffer_length,
								   long *string_length_ptr);
PUBLIC RETCODE odbc_get_desc_rec( ODBC_DESC *desc,
								  short rec_number,
								  char* name,
								  short buffer_length,
								  short *string_length_ptr,
								  short *type_ptr,
								  short *subtype_ptr,
								  long *length_ptr,
								  short *precision_ptr,
								  short *scale_ptr,
								  short *nullable_ptr);
PUBLIC RETCODE odbc_set_desc_field(ODBC_DESC *desc,
								   short rec_number,
								   short field_id,
								   void *value_ptr,
								   long buffer_length,
								   short is_driver);
PUBLIC PUBLIC RETCODE odbc_set_desc_rec(ODBC_DESC *desc,
								 short rec_number,
								 short type,
								 short subtype,
								 long length,
								 short precision,
								 short scale,
								 void *data_ptr,
								 long *string_length_ptr,
								 long *indicator_ptr);
PUBLIC RETCODE odbc_copy_desc(ODBC_DESC *source_desc,
							  ODBC_DESC *dest_desc);
PUBLIC void odbc_set_ird(struct st_odbc_statement *stmt,
						 short			column_number,
						 short			type,
						 char*			table_name,
						 char*			column_name,
						 long			precision,
						 short			scale,
						 short			nullable,
						 short			updatable);
PUBLIC int odbc_is_ird(ODBC_DESC *desc);
PUBLIC ODBC_RECORD *find_record_from_desc(ODBC_DESC *desc, int rec_number);
PUBLIC void reset_descriptor(ODBC_DESC *desc);

#endif	/* ! __ODBC_DESC_HEADER */
