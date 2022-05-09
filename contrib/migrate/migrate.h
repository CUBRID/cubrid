#ifndef _CUB_MIGRATE_
#define _CUB_MIGRATE_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <string.h>
#include <pwd.h>
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <dbi.h>

#define VERSION "1.0"

#define BUF_LEN 2048
#define DB_CLIENT_TYPE_ADMIN_UTILITY 7

#define PRINT_LOG(fmt, ...) print_log (fmt "\tin %s () from %s:%d", __VA_ARGS__, __func__, __FILE__, __LINE__)

// global variable
const char *PRO_NAME = NULL;
const char *CUBRID_ENV = NULL;
const int RETRY_COUNT = 3;

int log_file_fd = -1;
void *dl_handle = NULL;

// CUBRID function pointer
void (*cub_au_disable_passwords) (void);
int (*cub_db_restart_ex) (const char *, const char *, const char *, const char *, const char *, int);
int (*cub_er_errid) (void);
/*
int (*cub_db_execute_query) (const char *, DB_QUERY_RESULT **);
*/
DB_SESSION *(*cub_db_open_buffer) (const char *);
int (*cub_db_compile_statement) (DB_SESSION *);
int (*cub_db_execute_statement_local) (DB_SESSION *, int, DB_QUERY_RESULT **);
void (*cub_db_close_session) (DB_SESSION *);
int (*cub_db_query_end) (DB_QUERY_RESULT *);
int (*cub_db_commit_transaction) (void);
int (*cub_db_abort_transaction) (void);
int (*cub_db_query_get_tuple_value) (DB_QUERY_RESULT * result, int tuple_index, DB_VALUE * value);
int (*cub_db_query_next_tuple) (DB_QUERY_RESULT * result);
int (*cub_db_query_first_tuple) (DB_QUERY_RESULT * result);
char *(*cub_db_get_database_version) (void);
int (*cub_db_shutdown) (void);
const char *(*cub_db_error_string) (int);

// function
int initialize (void);
void finalize (void);
int execute_ddl (const char *db_name, const char *table_name);
#endif
