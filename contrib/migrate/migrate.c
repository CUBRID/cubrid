/*
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "migrate.h"

#define VERSION_INFO 		72
#define DATABASES_ENVNAME 	"CUBRID_DATABASES"
#define DATABASES_FILENAME 	"databases.txt"
#define PATH_MAX		256
#define MAX_LINE		4096
#define ENV_VAR_MAX		255

static const char *catalog_query[] = {
  /* alter catalog to add column */
  "alter table _db_class add column unique_name varchar (255) after [class_of]",
  "delete from _db_attribute where class_of.class_name = '_db_class' and rownum % 2 = 1",

  "alter table db_serial add column unique_name varchar first",
  "delete from _db_attribute where class_of.class_name = 'db_serial' and rownum % 2 = 1 and attr_name <> 'unique_name'",

  "alter table db_trigger add column unique_name varchar after owner",
  "delete from _db_attribute where class_of.class_name = 'db_trigger' and rownum % 2 = 1 and attr_name <> 'unique_name'",

  /* alter catalog to modify _db_index_key */
  "alter table _db_index_key modify column func varchar(1023)",
  "delete from _db_attribute where class_of.class_name = '_db_index_key' and rownum % 2 = 1",

  /* alter catalog to add tables and views (_db_server, _db_synonym, db_server, db_synonym) */
  "create table [_db_server] ( \
  	[link_name] character varying(255) not null, \
  	[host] character varying(255), \
  	[port] integer, \
  	[db_name] character varying(255), \
  	[user_name] character varying(255), \
  	[password] character varying(1073741823), \
  	[properties] character varying(2048), \
  	[owner] [db_user] not null, \
  	[comment] character varying(1024), \
  	constraint [pk__db_server_link_name_owner] primary key ([link_name], [owner]) \
  ) dont_reuse_oid, collate utf8_bin",

  "create view [db_server] ( \
  	[link_name] character varying(255), \
  	[host] character varying(255), \
  	[port] integer, \
  	[db_name] character varying(255), \
  	[user_name] character varying(255), \
  	[properties] character varying(2048), \
  	[owner] character varying(256), \
  	[comment] character varying(1024) \
  ) as \
	select \
  	[ds].[link_name], \
  	[ds].[host], \
  	[ds].[port], \
  	[ds].[db_name], \
  	[ds].[user_name], \
  	[ds].[properties], \
  	[ds].[owner].[name], \
  	[ds].[comment] \
  from \
  	[_db_server] [ds] \
  where \
  	current_user = 'DBA' \
    or { [ds].[owner].[name] } subseteq ( \
      select set {current_user} + coalesce (sum (set {[t].[g].[name]}), set { }) \
      from [db_user] [u], table([groups]) as [t]([g]) \
      where [u].[name] = current_user \
    ) \
    or { [ds] } subseteq ( \
      select sum (set {[au].[class_of]}) \
      from [_db_auth] [au] \
      where {[au].[grantee].[name]} subseteq ( \
                select set {current_user} + coalesce (sum (set {[t].[g].[name]}), set { }) \
                from [db_user] [u], table([groups]) as [t]([g]) \
                where [u].[name] = current_user \
              ) \
            and [au].[auth_type] = 'SELECT' \
    )",

  "CREATE TABLE [_db_synonym] ( \
  	[unique_name] CHARACTER VARYING(255) NOT NULL, \
  	[name] CHARACTER VARYING(255) NOT NULL, \
  	[owner] [db_user] NOT NULL, \
  	[is_public] INTEGER DEFAULT 0 NOT NULL, \
  	[target_unique_name] CHARACTER VARYING(255) NOT NULL, \
  	[target_name] CHARACTER VARYING(255) NOT NULL, \
  	[target_owner] [db_user] NOT NULL, [comment] CHARACTER VARYING(2048), \
  	CONSTRAINT [pk__db_synonym_unique_name] PRIMARY KEY  ([unique_name]), \
  	INDEX [i__db_synonym_name_owner_is_public] ([name], [owner], [is_public]) \
  ) DONT_REUSE_OID, COLLATE utf8_bin",

  "create view [db_synonym] ( \
  	[synonym_name] CHARACTER VARYING(255), \
  	[synonym_owner_name] CHARACTER VARYING(255), \
  	[is_public_synonym] CHARACTER VARYING(3), \
  	[target_name] CHARACTER VARYING(255), \
  	[target_owner_name] CHARACTER VARYING(255), \
  	[comment] CHARACTER VARYING(2048) \
  ) as \
  SELECT \
  	[s].[name] AS [synonym_name], \
  	CAST ([s].[owner].[name] AS VARCHAR(255)) AS [synonym_owner_name], \
  	CASE WHEN [s].[is_public] = 1 THEN 'YES' ELSE 'NO' END AS [is_public_synonym], \
  	[s].[target_name] AS [target_name], \
  	CAST ([s].[target_owner].[name] AS VARCHAR(255)) AS [target_owner_name], \
  	[s].[comment] AS [comment] \
	FROM \
  [_db_synonym] [s] \
  WHERE \
    CURRENT_USER = 'DBA' \
    OR [s].[is_public] = 1 \
    OR ( \
      [s].[is_public] = 0 \
      AND { [s].[owner].[name] } SUBSETEQ ( \
              SELECT SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET { }) \
              FROM [db_user] [u], TABLE([groups]) AS [t]([g]) \
              WHERE [u].[name] = CURRENT_USER \
             ) \
    )",

  /* set system class for newly added tables and views */
  "update _db_class set is_system_class = 1 where class_name in ('_db_server', 'db_server', '_db_synonym', 'db_synonym')"
};

static char *rename_query = "select \
  	'rename table ' || class_name || ' to [' || lower (owner.name) || '.' || class_name || '] ' as q \
   from  _db_class \
   where is_system_class % 8 = 0";

static char *update_unique_name =
  "update _db_class set class_name = substring_index (class_name, '.', -1), unique_name = class_name where is_system_class % 8 = 0";

static char *serial_query = {
  "select \
      'call change_serial_owner (''' || name || ''', ''' || substring_index (name, '.', 1) || ''') on class db_serial' as q \
   from db_serial \
   where class_name is not null and instr (name, '.') > 0"
};

static char *update_serial[] = {
  "update db_serial set name = substring_index (name, '.', -1)",
  "update db_serial set unique_name = lower (owner.name) || '.' || name"
};

static char *update_trigger = "update db_trigger set unique_name = lower (owner.name) || '.' || name";

static char *index_query[] = {
  "create unique index u_db_serial_name_owner ON db_serial (name, owner)",
  "alter table db_serial drop constraint pk_db_serial_name",
  "alter table db_serial add constraint pk_db_serial_unique_name primary key (unique_name)",

  "alter table db_serial modify column name varchar not null",
  "alter table db_serial modify column unique_name varchar not null",

  "create index i__db_class_unique_name on _db_class (unique_name)",
  "create index i__db_class_class_name_owner on _db_class (class_name, owner)",

  "drop index i__db_class_class_name on _db_class"
};

static char *update_db_class = "update db_trigger set unique_name = lower (owner.name) || '.' || name";

static void
print_errmsg (const char *err_msg)
{
  fprintf (stderr, "ERROR: %s\n", err_msg);

  return;
}

static void
print_log (const char *log_fmt, ...)
{
  char log_msg[BUF_LEN];
  time_t t;
  struct tm *tm;
  va_list ap;

  t = time (NULL);
  tm = localtime (&t);

  snprintf (log_msg, BUF_LEN, "%d-%02d-%02d %02d:%02d:%02d\t",
	    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

  va_start (ap, log_fmt);

  vsnprintf (log_msg + strlen (log_msg), BUF_LEN - strlen (log_msg), log_fmt, ap);

  va_end (ap);

  snprintf (log_msg + strlen (log_msg), BUF_LEN - strlen (log_msg), "\n");

  fprintf (stderr, "ERROR : %s\n", log_msg);

  return;
}

static int
char_isspace (int c)
{
  return ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n' || (c) == '\f' || (c) == '\v');
}

static char *
next_char (char *str_p)
{
  char *p;

  p = str_p;
  while (char_isspace ((int) *p) && *p != '\0')
    {
      p++;
    }

  return (p);
}

static char *
get_token (char *str_p, char **token_p)
{
  char *p, *end, *token = NULL;
  int length;

  token = NULL;
  p = str_p;
  while (char_isspace ((int) *p) && *p != '\0')
    {
      p++;
    }
  end = p;
  while (!char_isspace ((int) *end) && *end != '\0')
    {
      end++;
    }

  length = (int) (end - p);
  if (length > 0)
    {
      token = (char *) malloc (length + 1);
      if (token != NULL)
	{
	  strncpy (token, p, length);
	  token[length] = '\0';
	}
    }

  *token_p = token;
  return (end);
}

static void
get_directory_path (char *buffer)
{
  const char *env_name;

  env_name = getenv (DATABASES_ENVNAME);
  if (env_name == NULL || strlen (env_name) == 0)
    {
    }
  else
    {
      if (env_name[strlen (env_name) - 1] == '/')
	{
	  sprintf (buffer, "%s%s", env_name, DATABASES_FILENAME);
	}
      else
	{
	  sprintf (buffer, "%s/%s", env_name, DATABASES_FILENAME);
	}
    }
}

static int
get_db_path (char *dbname, char **pathname)
{
  FILE *file_p = NULL;
  char line[MAX_LINE];
  char filename[PATH_MAX];
  char *name;
  char *host;
  char *str;

  get_directory_path (filename);

  file_p = fopen (filename, "r");

  while (fgets (line, MAX_LINE - 1, file_p) != NULL)
    {
      str = next_char (line);
      if (*str != '\0' && *str != '#')
	{
	  str = get_token (str, &name);
	  str = get_token (str, pathname);
	  str = get_token (str, &host);

	  if (strcmp (host, "localhost") == 0)
	    {
	      if (strcmp (name, dbname) == 0)
		{
		  str = get_token (str, pathname);	// log path
		  fclose (file_p);

		  return 1;
		}
	    }
	}
    }

  fclose (file_p);

  return 0;
}

static void
migrate_get_db_path (char *dbname, char *db_path)
{
  char *path;

  get_db_path (dbname, &path);
  sprintf (db_path, "%s/%s_lgat", path, dbname);
}

static int
migrate_check_log_volume (char *dbname)
{
  char db_path[PATH_MAX];
  char *version = cub_db_get_database_version ();
  char *path;
  int fd;
  float log_ver;

  if (version)
    {
      if ((strncmp (version + 16, "11.0", 4) == 0 || strncmp (version + 16, "11.1", 4) == 0))
	{
	  printf ("CUBRID version %s, should be 11.0.x or 11.1.x\n", version);
	  return -1;
	}

      printf ("%s\n", version);
    }
  else
    {
      printf ("migrate: can not get version info.\n");
      return -1;
    }

  migrate_get_db_path (dbname, db_path);
  printf ("%s reading\n", db_path);

  fd = open (db_path, O_RDONLY);

  if (fd < 0)
    {
      printf ("migrate: can not open the log file\n");
      return -1;
    }

  if (lseek (fd, VERSION_INFO, SEEK_SET) < 0)
    {
      printf ("migrate: can not seek the version info.\n");
      close (fd);
      return -1;
    }

  if (read (fd, &log_ver, 4) < 0)
    {
      printf ("migrate: can not read the version info.\n");
      close (fd);
      return -1;
    }

  if (log_ver < 11.0 || log_ver >= 11.2)
    {
      printf ("migrate: the database volume %f is not a migratable version\n", log_ver);
      close (fd);
      return -1;
    }

  close (fd);

  return 0;
}

static void
migrate_update_log_volume (char *dbname)
{
  char db_path[PATH_MAX];
  float version = 11.2;
  int fd;

  migrate_get_db_path (dbname, db_path);
  printf ("%s version updating\n", db_path);

  fd = open (db_path, O_RDWR);

  if (fd < 0)
    {
      printf ("migrate: can not open the log file for upgrade\n");
      return;
    }

  if (lseek (fd, VERSION_INFO, SEEK_SET) < 0)
    {
      printf ("migrate: can not seek the version info. for upgrade\n");
      close (fd);
      return;
    }

  if (write (fd, &version, 4) < 0)
    {
      printf ("migrate: can not write the version info. for upgrade\n");
      close (fd);
      return;
    }

  fsync (fd);

  close (fd);

  printf ("migration done\n");
}

static int
cub_db_execute_query (const char *str, DB_QUERY_RESULT ** result)
{
  DB_SESSION *session = NULL;
  STATEMENT_ID stmt_id;
  int error = NO_ERROR;

  session = cub_db_open_buffer (str);
  if (session == NULL)
    {
      error = cub_er_errid ();
      goto exit;
    }

  stmt_id = cub_db_compile_statement (session);
  if (stmt_id < 0)
    {
      error = cub_er_errid ();
      goto exit;
    }

  error = cub_db_execute_statement_local (session, stmt_id, result);

  if (error >= 0)
    {
      return error;
    }

exit:
  if (session != NULL)
    {
      cub_db_close_session (session);
    }

  return error;
}

int
migrate_initialize ()
{
  char libcubridsa_path[BUF_LEN];
  int error = 0;

  CUBRID_ENV = getenv ("CUBRID");
  if (CUBRID_ENV == NULL)
    {
      print_errmsg ("$CUBRID environment variable is not defined.");
      error = -1;
    }

  if (getenv ("CUBRID_DATABASES") == NULL)
    {
      print_errmsg ("$CUBRID_DATABASES environment variable is not defined.");
      error = -1;
    }

  snprintf (libcubridsa_path, BUF_LEN, "%s/lib/libcubridsa.so.11.0", CUBRID_ENV);

  /* dynamic loading (libcubridsa.so) */
  dl_handle = dlopen (libcubridsa_path, RTLD_LAZY);
  if (dl_handle == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_get_database_version = dlsym (dl_handle, "db_get_database_version");
  if (cub_db_get_database_version == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_restart_ex = dlsym (dl_handle, "db_restart_ex");
  if (cub_db_restart_ex == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_er_errid = dlsym (dl_handle, "er_errid");
  if (cub_er_errid == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_open_buffer = dlsym (dl_handle, "db_open_buffer");
  if (cub_db_open_buffer == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_compile_statement = dlsym (dl_handle, "db_compile_statement");
  if (cub_db_compile_statement == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_execute_statement_local = dlsym (dl_handle, "db_execute_statement_local");
  if (cub_db_execute_statement_local == NULL)
    {
      cub_db_execute_statement_local =
	dlsym (dl_handle, "_Z26db_execute_statement_localP10db_sessioniPP15db_query_result");
      if (cub_db_execute_statement_local == NULL)
	{
	  PRINT_LOG ("%s", dlerror ());
	  error = -1;
	}
    }

  cub_db_query_get_tuple_value = dlsym (dl_handle, "db_query_get_tuple_value");
  if (cub_db_query_get_tuple_value == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_query_first_tuple = dlsym (dl_handle, "db_query_first_tuple");
  if (cub_db_query_first_tuple == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_query_next_tuple = dlsym (dl_handle, "db_query_next_tuple");
  if (cub_db_query_next_tuple == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_close_session = dlsym (dl_handle, "db_close_session");
  if (cub_db_close_session == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_query_end = dlsym (dl_handle, "db_query_end");
  if (cub_db_query_end == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_commit_transaction = dlsym (dl_handle, "db_commit_transaction");
  if (cub_db_commit_transaction == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_abort_transaction = dlsym (dl_handle, "db_abort_transaction");
  if (cub_db_abort_transaction == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_shutdown = dlsym (dl_handle, "db_shutdown");
  if (cub_db_shutdown == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_db_error_string = dlsym (dl_handle, "db_error_string");
  if (cub_db_error_string == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  cub_Au_disable = dlsym (dl_handle, "Au_disable");
  if (cub_Au_disable == NULL)
    {
      PRINT_LOG ("%s", dlerror ());
      error = -1;
    }

  return error;
}

static int
migrate_execute_query (const char *query)
{
  DB_QUERY_RESULT *result;
  int error;

  error = cub_db_execute_query (query, &result);
  printf ("migrate: execute query: %s\n", query);
  if (error < 0)
    {
      printf ("migrate: execute query failed \"%s\"\n", query);
      return -1;
    }
  cub_db_query_end (result);
}

static int
migrate_generated (const char *generated, int col_num)
{
  DB_QUERY_RESULT *gen_result;
  DB_QUERY_RESULT *result;
  const char *query;
  int error;

  error = cub_db_execute_query (generated, &gen_result);
  if (error < 0)
    {
      printf ("generated: execute query failed\n \"%s\"\n", generated);
      return -1;
    }

  if (cub_db_query_first_tuple (gen_result) == DB_CURSOR_SUCCESS)
    {
      do
	{
	  int i;
	  DB_VALUE value;

	  for (i = 0; i < col_num; i++)
	    {
	      /* from query */
	      error = cub_db_query_get_tuple_value (gen_result, i, &value);
	      if (error < 0)
		{
		  printf ("generated: can not get a tuple for \"%s\"\n", generated);
		  return -1;
		}

	      query = value.data.ch.medium.buf;

	      /* from generated result */
	      error = migrate_execute_query (query);
	      if (error < 0)
		{
		  return error;
		}
	    }
	  error = cub_db_query_next_tuple (gen_result);
	}
      while (error != DB_CURSOR_END && error != DB_CURSOR_ERROR);

      cub_db_query_end (gen_result);
    }
  else
    {
      printf ("generated: can not get first tuple for \"%s\"\n", generated);
      return -1;
    }

  if (error < 0)
    {
      printf ("generated: can not get a next tuple for \"%s\"\n", generated);
      return -1;
    }

  return 0;
}

static int
migrate_queries ()
{
  DB_QUERY_RESULT *result;
  DB_VALUE value;
  int i, error;

  /* catalog query */
  for (i = 0; i < sizeof (catalog_query) / sizeof (const char *); i++)
    {
      error = migrate_execute_query (catalog_query[i]);
      if (error < 0)
	{
	  return -1;
	}
    }

  /* generated query */
  error = migrate_generated (rename_query, 1);
  if (error < 0)
    {
      printf ("migrate: execute query failed \"%s\"\n", rename_query);
      return -1;
    }

  error = migrate_execute_query (update_unique_name);
  if (error < 0)
    {
      return -1;
    }

  error = migrate_generated (serial_query, 1);
  if (error < 0)
    {
      printf ("migrate: execute query failed \"%s\"\n", serial_query);
      return -1;
    }

  for (i = 0; i < sizeof (update_serial) / sizeof (const char *); i++)
    {
      error = migrate_execute_query (update_serial[i]);
      if (error < 0)
	{
	  return -1;
	}
    }

  error = migrate_execute_query (update_trigger);
  if (error < 0)
    {
      return -1;
    }

  /* index query */
  for (i = 0; i < sizeof (index_query) / sizeof (const char *); i++)
    {
      error = migrate_execute_query (index_query[i]);
      if (error < 0)
	{
	  return -1;
	}
    }

  error = migrate_execute_query (update_db_class);
  if (error < 0)
    {
      return -1;
    }

  return 0;
}

int
main (int argc, char *argv[])
{
  int status, error;
  char *password = "";

  if (argc < 3 || argc > 4)
    {
      printf ("usage: migrate db-name db-user password\n");
      return -1;
    }

  error = migrate_initialize ();
  if (error < 0)
    {
      printf ("migrate: error encountered while initializing\n");
      return -1;
    }

  status = migrate_check_log_volume (argv[1]);
  if (status < 0)
    {
      return -1;
    }

  if (argc > 3)
    {
      password = argv[3];
    }

  error = cub_db_restart_ex ("migrate", argv[1], argv[2], password, NULL, DB_CLIENT_TYPE_ADMIN_UTILITY);
  if (error)
    {
      PRINT_LOG ("migrate: db_restart_ex () call failed [err_code: %d, err_msg: %s]", error, cub_db_error_string (1));
      return -1;
    }

  *cub_Au_disable = 1;

  error = migrate_queries ();
  if (error < 0)
    {
      printf ("migrate: error encountered while executing quries\n");
      cub_db_abort_transaction ();
      return -1;
    }

  error = cub_db_commit_transaction ();
  if (error < 0)
    {
      printf ("migrate: error encountered while committing\n");
      cub_db_abort_transaction ();
      return -1;
    }

  error = cub_db_shutdown ();
  if (error < 0)
    {
      printf ("migrate: error encountered while shutdown db\n");
      cub_db_abort_transaction ();
      return -1;
    }

  migrate_update_log_volume (argv[1]);

  return 0;
}
