#ifndef _PHP_CUBRID_H
#define _PHP_CUBRID_H

#include "php.h"

#include "php_cubrid_version.h"

#ifdef PHP_WIN32
#define PHP_CUBRID_API __declspec(dllexport)
#else /* PHP_WIN32 */
#define PHP_CUBRID_API
#endif /* PHP_WIN32 */

#ifdef __ZTS
#include "TSRM.h"
#endif /* __ZTS */

typedef struct {
	int facility;
	int code;
	char msg[1000];
} T_CUBRID_ERROR;

extern zend_module_entry cubrid_module_entry;

#define cubrid_module_ptr &cubrid_module_entry

extern PHP_MINIT_FUNCTION(cubrid);
PHP_MINFO_FUNCTION(cubrid);

/* API prototype */

PHP_FUNCTION(cubrid_version);
PHP_FUNCTION(cubrid_connect);
PHP_FUNCTION(cubrid_disconnect);
PHP_FUNCTION(cubrid_prepare);
PHP_FUNCTION(cubrid_bind);
PHP_FUNCTION(cubrid_execute);
PHP_FUNCTION(cubrid_affected_rows);
PHP_FUNCTION(cubrid_close_request);
PHP_FUNCTION(cubrid_fetch);
PHP_FUNCTION(cubrid_current_oid);
PHP_FUNCTION(cubrid_column_types);
PHP_FUNCTION(cubrid_column_names);
PHP_FUNCTION(cubrid_move_cursor);
PHP_FUNCTION(cubrid_num_rows);
PHP_FUNCTION(cubrid_num_cols);
PHP_FUNCTION(cubrid_get);
PHP_FUNCTION(cubrid_put);
PHP_FUNCTION(cubrid_drop);
PHP_FUNCTION(cubrid_is_instance);
PHP_FUNCTION(cubrid_get_class_name);
PHP_FUNCTION(cubrid_lock_read);
PHP_FUNCTION(cubrid_lock_write);
PHP_FUNCTION(cubrid_schema);
PHP_FUNCTION(cubrid_col_size);
PHP_FUNCTION(cubrid_col_get);
PHP_FUNCTION(cubrid_set_add);
PHP_FUNCTION(cubrid_set_drop);
PHP_FUNCTION(cubrid_seq_drop);
PHP_FUNCTION(cubrid_seq_insert);
PHP_FUNCTION(cubrid_seq_put);
PHP_FUNCTION(cubrid_commit);
PHP_FUNCTION(cubrid_rollback);
PHP_FUNCTION(cubrid_new_glo);
PHP_FUNCTION(cubrid_save_to_glo);
PHP_FUNCTION(cubrid_load_from_glo);
PHP_FUNCTION(cubrid_send_glo);
PHP_FUNCTION(cubrid_error_msg);
PHP_FUNCTION(cubrid_error_code);
PHP_FUNCTION(cubrid_error_code_facility);


/* end of API prototype */

ZEND_BEGIN_MODULE_GLOBALS(cubrid)
	
	T_CUBRID_ERROR recent_error;

ZEND_END_MODULE_GLOBALS(cubrid);

#ifdef ZTS
# define UniSLS_D	zend_cubrid_globals *cubrid_globals
# define UniSLS_DC	, UniSLS_D
# define UniSLS_C	cubrid_globals
# define UniSLS_CC	, MySLS_C
# define UniSG(v)	(cubrid_globals->v)
# define UniSLS_FETCH()	zend_cubrid_globals *cubrid_globals = ts_resource(cubrid_globals_id)
#else /* ZTS */
# define UniSLS_D	
# define UniSLS_DC	
# define UniSLS_C	
# define UniSLS_CC	
# define UniSG(v)	(cubrid_globals.v)
# define UniSLS_FETCH()	
#endif /* ZTS */

#define phpext_cubrid_ptr cubrid_module_ptr


#endif /* _PHP_CUBRID_H */
