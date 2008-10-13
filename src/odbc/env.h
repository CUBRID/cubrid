#ifndef	__ODBC_ENV_HEADER		/* to avoid multiple inclusion */
#define	__ODBC_ENV_HEADER

#include		"portable.h"
#include		"diag.h"

typedef struct st_odbc_env {
	unsigned short		handle_type;
	ODBC_DIAG			*diag;
	struct st_odbc_env		*next;
	void				*conn;
	/*
	struct odbc_connection *conn;
	*/
	
	char				*program_name;

	/* ODBC environment attributes */
	unsigned long		attr_odbc_version;
	unsigned long		attr_output_nts;

	/*  (Not supported) Optional features */
	unsigned long		attr_connection_pooling;
	unsigned long		attr_cp_match;
} ODBC_ENV;

PUBLIC RETCODE odbc_alloc_env(ODBC_ENV **envptr);
PUBLIC RETCODE odbc_free_env(ODBC_ENV *env);
PUBLIC RETCODE odbc_set_env_attr(	ODBC_ENV *env, 
									long attribute, 
									void *valueptr, 
									long stringlength);
PUBLIC odbc_get_env_attr(	ODBC_ENV *env, 
							long attribute, 
							void *value_ptr,
							long buffer_length,
							long *string_length_ptr);
PUBLIC RETCODE odbc_end_tran(short handle_type, 
							 void*	handle,
							 short	completion_type);

#endif	/* ! __ODBC_ENV_HEADER */
