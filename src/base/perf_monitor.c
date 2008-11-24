/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * perf_monitor.c - Monitor execution statistics at Client
 * 					Monitor execution statistics
 *                  Monitor execution statistics at Server
 * 					diag server module
 */

#include <stdio.h>
#include <time.h>
#if !defined (WINDOWS)
#include <sys/time.h>
#include <sys/resource.h>
#endif /* WINDOWS */
#include "perf_monitor.h"
#include "network_interface_cl.h"

#if !defined(SERVER_MODE)
#include "memory_alloc.h"
#include "error_manager.h"
#include "server_interface.h"
#endif /* !SERVER_MODE */

#if defined(SERVER_MODE)
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#if !defined(WINDOWS)
#include <sys/shm.h>
#include <sys/ipc.h>
#endif /* WINDOWS */

#include <sys/stat.h>
#include "connection_defs.h"
#include "environment_variable.h"
#include "connection_error.h"
#include "databases_file.h"
#endif /* SERVER_MODE */
#include "thread_impl.h"

#if !defined(CS_MODE)
#include <string.h>
#include <assert.h>

#include "error_manager.h"
#include "log_manager.h"
#include "system_parameter.h"
#if defined (SERVER_MODE)
#include "thread_impl.h"
#include "connection_error.h"
#endif /* SERVER_MODE */

#if !defined(SERVER_MODE)
#undef MUTEX_INIT
#define MUTEX_INIT(a)
#undef MUTEX_DESTROY
#define MUTEX_DESTROY(a)
#undef MUTEX_LOCK
#define MUTEX_LOCK(a, b)
#undef MUTEX_UNLOCK
#define MUTEX_UNLOCK(a)
#endif /* SERVER_MODE */
#endif /* !CS_MODE */

#if !defined(SERVER_MODE)
/* Client execution statistics */
static MNT_EXEC_STATS mnt_Stats;
static bool mnt_Iscollecting_stats = false;
static void mnt_client_reset_stats (void);

#if defined(CS_MODE)
/* Server execution statistics */
static MNT_SERVER_EXEC_STATS mnt_Server_stats;

#endif /* CS_MODE */

/*
 * mnt_start_stats - Start collecting client execution statistics
 *   return: NO_ERROR or ER_FAILED
 */
int
mnt_start_stats (void)
{
  int answer;

  /* Restart the statistics */
  if ((answer = mnt_server_start_stats ()) != ER_FAILED)
    {
      mnt_Iscollecting_stats = true;
      mnt_client_reset_stats ();
    }
  return answer;
}

/*
 * mnt_stop_stats - Stop collecting client execution statistics
 *   return: none
 */
void
mnt_stop_stats (void)
{
  if (mnt_Iscollecting_stats != false)
    {
      mnt_server_stop_stats ();
      mnt_client_reset_stats ();
      mnt_Iscollecting_stats = false;
      mnt_Stats.server_stats = NULL;
    }
}

/*
 * mnt_reset_stats - Reset client and server statistics
 *   return: none
 */
void
mnt_reset_stats (void)
{
  if (mnt_Iscollecting_stats != false)
    {
      mnt_server_reset_stats ();
      mnt_client_reset_stats ();
    }
}

/*
 * mnt_client_reset_stats - Reset the client statistics
 *   return: none
 */
static void
mnt_client_reset_stats (void)
{
  mnt_get_current_times (&mnt_Stats.cpu_start_usr_time,
			 &mnt_Stats.cpu_start_sys_time,
			 &mnt_Stats.elapsed_start_time);
  mnt_Stats.server_stats = NULL;
}

/*
 * mnt_get_stats - Get the recorded client statistics
 *   return: client statistics
 */
MNT_EXEC_STATS *
mnt_get_stats ()
{
  if (mnt_Iscollecting_stats != true)
    return NULL;
  else
    {
      /* Refresh statistics from server */
#if defined (SA_MODE)
      mnt_Stats.server_stats = mnt_server_get_stats (NULL);
#else /* SA_MODE */
      mnt_server_copy_stats (&mnt_Server_stats);
      mnt_Stats.server_stats = &mnt_Server_stats;
#endif /* SA_MODE */

      return &mnt_Stats;
    }
}

/*
 * mnt_print_stats - Print the current client statistics
 *   return:
 *   stream(in): if NULL is given, stdout is used
 */
void
mnt_print_stats (FILE * stream)
{
  MNT_EXEC_STATS *stats;
  time_t cpu_total_usr_time;
  time_t cpu_total_sys_time;
  time_t elapsed_total_time;

  if (stream == NULL)
    stream = stdout;

  if ((stats = mnt_get_stats ()) != NULL)
    {
      mnt_get_current_times (&cpu_total_usr_time, &cpu_total_sys_time,
			     &elapsed_total_time);

      fprintf (stream, "\n*** CLIENT EXECUTION STATISTICS ***\n");

      fprintf (stream, "System CPU (sec)              = %10d\n",
	       (int) (cpu_total_sys_time - mnt_Stats.cpu_start_sys_time));
      fprintf (stream, "User CPU (sec)                = %10d\n",
	       (int) (cpu_total_usr_time - mnt_Stats.cpu_start_usr_time));
      fprintf (stream, "Elapsed (sec)                 = %10d\n",
	       (int) (elapsed_total_time - mnt_Stats.elapsed_start_time));

      mnt_server_dump_stats (stats->server_stats, stream);
    }
}

#endif /* !SERVER_MODE */

#if defined(SERVER_MODE)
#if defined(WINDOWS)
#define strcasecmp _stricmp
#endif /* WINDOWS */

#if defined(WINDOWS)
#define SERVER_SHM_CREATE(SHM_KEY, SIZE, HANDLE_PTR)    \
        server_shm_create(SHM_KEY, SIZE, HANDLE_PTR)
#define SERVER_SHM_OPEN(SHM_KEY, HANDLE_PTR)            \
        server_shm_open(SHM_KEY, HANDLE_PTR)
#define SERVER_SHM_DETACH(PTR, HMAP)	\
        do {				\
          if (HMAP != NULL) {		\
            UnmapViewOfFile(PTR);	\
            CloseHandle(HMAP);		\
          }				\
        } while (0)
#else /* WINDOWS */
#define SERVER_SHM_CREATE(SHM_KEY, SIZE, HANDLE_PTR)    \
        server_shm_create(SHM_KEY, SIZE)
#define SERVER_SHM_OPEN(SHM_KEY, HANDLE_PTR)            \
        server_shm_open(SHM_KEY)
#define SERVER_SHM_DETACH(PTR, HMAP)    shmdt(PTR)
#endif /* WINDOWS */

#define SERVER_SHM_DESTROY(SHM_KEY)     \
        server_shm_destroy(SHM_KEY)

#define CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT(ERR_BUF) \
    do { \
        if (thread_is_manager_initialized() == false) {\
            if (ERR_BUF) strcpy(ERR_BUF, "thread mgr is not initialized");\
            return -1;\
        }\
    } while(0)

#define CHECK_SHM() \
    do { \
        if (g_ShmServer == NULL) return -1; \
    } while(0)

#define CUBRID_KEY_GEN_ID 0x08
#define DIAG_SERVER_MAGIC_NUMBER 07115

/* Global variables */
bool diag_executediag;
int diag_long_query_time;

static int ShmPort;
static T_SHM_DIAG_INFO_SERVER *g_ShmServer = NULL;

#if defined(WINDOWS)
static HANDLE shm_map_object;
#endif /* WINDOWS */

/* Diag value modification function */
static int diag_val_set_query_open_page (int value,
					 T_DIAG_VALUE_SETTYPE settype,
					 char *err_buf);
static int diag_val_set_query_opened_page (int value,
					   T_DIAG_VALUE_SETTYPE settype,
					   char *err_buf);
static int diag_val_set_buffer_page_read (int value,
					  T_DIAG_VALUE_SETTYPE settype,
					  char *err_buf);
static int diag_val_set_buffer_page_write (int value,
					   T_DIAG_VALUE_SETTYPE settype,
					   char *err_buf);
static int diag_val_set_conn_aborted_clients (int value,
					      T_DIAG_VALUE_SETTYPE settype,
					      char *err_buf);
static int diag_val_set_conn_cli_request (int value,
					  T_DIAG_VALUE_SETTYPE settype,
					  char *err_buf);
static int diag_val_set_query_slow_query (int value,
					  T_DIAG_VALUE_SETTYPE settype,
					  char *err_buf);
static int diag_val_set_lock_deadlock (int value,
				       T_DIAG_VALUE_SETTYPE settype,
				       char *err_buf);
static int diag_val_set_lock_request (int value,
				      T_DIAG_VALUE_SETTYPE settype,
				      char *err_buf);
static int diag_val_set_query_full_scan (int value,
					 T_DIAG_VALUE_SETTYPE settype,
					 char *err_buf);
static int diag_val_set_conn_conn_req (int value,
				       T_DIAG_VALUE_SETTYPE settype,
				       char *err_buf);
static int diag_val_set_conn_conn_reject (int value,
					  T_DIAG_VALUE_SETTYPE settype,
					  char *err_buf);

static int server_shm_destroy (int shm_key);
static bool diag_sm_isopened (void);
static bool init_server_diag_value (T_SHM_DIAG_INFO_SERVER * shm_server);
static bool init_diag_sm (const char *server_name, int num_thread,
			  char *err_buf);
static bool rm_diag_sm (void);

static char *linetrim (char *str);
static int create_shm_key_file (int port, char *vol_dir,
				const char *servername);
static int uReadDiagSystemConfig (DIAG_SYS_CONFIG * config, char *err_buf);
static int get_volumedir (char *vol_dir, const char *dbname);
static int getservershmid (char *dir, const char *dbname);


#if defined(WINDOWS)
static void shm_key_to_name (int shm_key, char *name_str);
static void *server_shm_create (int shm_key, int size, HANDLE * hOut);
static void *server_shm_open (int shm_key, HANDLE * hOut);
#else /* WINDOWS */
static void *server_shm_create (int shm_key, int size);
static void *server_shm_open (int shm_key);
#endif /* WINDOWS */

T_DIAG_OBJECT_TABLE diag_obj_list[] = {
  {"open_page", DIAG_OBJ_TYPE_QUERY_OPEN_PAGE, diag_val_set_query_open_page}
  , {"opened_page", DIAG_OBJ_TYPE_QUERY_OPENED_PAGE,
     diag_val_set_query_opened_page}
  , {"slow_query", DIAG_OBJ_TYPE_QUERY_SLOW_QUERY,
     diag_val_set_query_slow_query}
  , {"full_scan", DIAG_OBJ_TYPE_QUERY_FULL_SCAN, diag_val_set_query_full_scan}
  , {"cli_request", DIAG_OBJ_TYPE_CONN_CLI_REQUEST,
     diag_val_set_conn_cli_request}
  , {"aborted_client", DIAG_OBJ_TYPE_CONN_ABORTED_CLIENTS,
     diag_val_set_conn_aborted_clients}
  , {"conn_req", DIAG_OBJ_TYPE_CONN_CONN_REQ, diag_val_set_conn_conn_req}
  , {"conn_reject", DIAG_OBJ_TYPE_CONN_CONN_REJECT,
     diag_val_set_conn_conn_reject}
  , {"buffer_page_read", DIAG_OBJ_TYPE_BUFFER_PAGE_READ,
     diag_val_set_buffer_page_read}
  , {"buffer_page_write", DIAG_OBJ_TYPE_BUFFER_PAGE_WRITE,
     diag_val_set_buffer_page_write}
  , {"lock_deadlock", DIAG_OBJ_TYPE_LOCK_DEADLOCK, diag_val_set_lock_deadlock}
  , {"lock_request", DIAG_OBJ_TYPE_LOCK_REQUEST, diag_val_set_lock_request}
};

/* function definition */
/*
 * linetrim()
 *    return: char *
 *    str(in):
 */
static char *
linetrim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str;
       *s != '\0' && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r');
       s++)
    ;
  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    ;
  for (p--; *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'; p--)
    ;
  *++p = '\0';

  if (s != str)
    {
      memcpy (str, s, strlen (s) + 1);
    }

  return (str);
}

/*
 * create_shm_key_file()
 *    return: int
 *    port(in):
 *    vol_dir(in):
 *    servername(in):
 */
static int
create_shm_key_file (int port, char *vol_dir, const char *servername)
{
  FILE *keyfile;
  char keyfilepath[PATH_MAX];

  if (!vol_dir || !servername)
    {
      return -1;
    }

  sprintf (keyfilepath, "%s/%s_shm.key", vol_dir, servername);
  keyfile = fopen (keyfilepath, "w+");
  if (keyfile)
    {
      fprintf (keyfile, "%x", port);
      fclose (keyfile);
      return 1;
    }

  return -1;
}

/*
 * uReadDiagSystemConfig()
 *    return: int
 *    config(in):
 *    err_buf(in):
 */
static int
uReadDiagSystemConfig (DIAG_SYS_CONFIG * config, char *err_buf)
{
  FILE *conf_file;
  char cbuf[1024], file_path[1024];
  char *cubrid_home;
  char ent_name[128], ent_val[128];

  if (config == NULL)
    return -1;

  /* Initialize config data */
  config->Executediag = 0;
  config->server_long_query_time = 0;

  cubrid_home = getenv ("CUBRID");

  if (cubrid_home == NULL)
    {
      if (err_buf)
	{
	  strcpy (err_buf, "Environment variable CUBRID is not set.");
	}
      return -1;
    }

  sprintf (file_path, "%s/conf/cm.conf", cubrid_home);

  conf_file = fopen (file_path, "r");

  if (conf_file == NULL)
    {
      if (err_buf)
	{
	  sprintf (err_buf, "File(%s) open error.", file_path);
	}
      return -1;
    }

  while (fgets (cbuf, 1024, conf_file))
    {
      linetrim (cbuf);
      if (cbuf[0] == '\0' || cbuf[0] == '#')
	continue;

      if (sscanf (cbuf, "%s %s", ent_name, ent_val) < 2)
	continue;

      if (strcasecmp (ent_name, "Execute_diag") == 0)
	{
	  if (strcasecmp (ent_val, "ON") == 0)
	    {
	      config->Executediag = 1;
	    }
	  else
	    {
	      config->Executediag = 0;
	    }
	}
      else if (strcasecmp (ent_name, "server_long_query_time") == 0)
	{
	  config->server_long_query_time = atoi (ent_val);
	}
    }

  fclose (conf_file);
  return 1;
}

/*
 * get_volumedir()
 *    return: int
 *    vol_dir(in):
 *    dbname(in):
 *    err_buf(in):
 */
static int
get_volumedir (char *vol_dir, const char *dbname)
{
  FILE *databases_txt;
  const char *envpath;
  char db_txt[PATH_MAX];
  char cbuf[PATH_MAX * 2];
  char volname[MAX_SERVER_NAMELENGTH];

  if (!vol_dir || !dbname)
    return -1;

  envpath = envvar_get ("DATABASES");
  if (envpath == NULL || strlen (envpath) == 0)
    {
      return -1;
    }

  sprintf (db_txt, "%s/%s", envpath, DATABASES_FILENAME);
  databases_txt = fopen (db_txt, "r");
  if (databases_txt == NULL)
    {
      return -1;
    }

  while (fgets (cbuf, 1024, databases_txt))
    {
      if (sscanf (cbuf, "%s %s %*s %*s", volname, vol_dir) < 2)
	continue;

      if (strcmp (volname, dbname) == 0)
	{
	  fclose (databases_txt);
	  return 1;
	}
    }

  fclose (databases_txt);
  return -1;
}

/*
 * getservershmid()
 *    return: int
 *    dir(in):
 *    dbname(in):
 */
static int
getservershmid (char *dir, const char *dbname)
{
  int shm_key = 0;
  char vol_full_path[PATH_MAX];
  char *p;

  sprintf (vol_full_path, "%s/%s", dir, dbname);
  for (p = vol_full_path; *p; p++)
    {
      shm_key = 31 * shm_key + (*p);
    }
  shm_key &= 0x00ffffff;

  return shm_key;
}

#if defined(WINDOWS)

/*
 * shm_key_to_name()
 *    return: none
 *    shm_key(in):
 *    name_str(in):
 */
static void
shm_key_to_name (int shm_key, char *name_str)
{
  sprintf (name_str, "cubrid_shm_%d", shm_key);
}

/*
 * server_shm_create()
 *    return: void*
 *    shm_key(in):
 *    size(in):
 *    hOut(in):
 */
static void *
server_shm_create (int shm_key, int size, HANDLE * hOut)
{
  LPVOID lpvMem = NULL;
  HANDLE hMapObject = NULL;
  char shm_name[64];

  *hOut = NULL;

  shm_key_to_name (shm_key, shm_name);

  hMapObject = CreateFileMapping ((HANDLE) 0xFFFFFFFF,
				  NULL, PAGE_READWRITE, 0, size, shm_name);
  if (hMapObject == NULL)
    {
      return NULL;
    }

  if (GetLastError () == ERROR_ALREADY_EXISTS)
    {
      CloseHandle (hMapObject);
      return NULL;
    }

  lpvMem = MapViewOfFile (hMapObject, FILE_MAP_WRITE, 0, 0, 0);
  if (lpvMem == NULL)
    {
      CloseHandle (hMapObject);
      return NULL;
    }

  *hOut = hMapObject;
  return lpvMem;
}

/*
 * server_shm_open()
 *    return: void *
 *    shm_key(in):
 *    hOut(in):
 */
static void *
server_shm_open (int shm_key, HANDLE * hOut)
{
  LPVOID lpvMem = NULL;		/* address of shared memory */
  HANDLE hMapObject = NULL;
  char shm_name[64];

  *hOut = NULL;

  shm_key_to_name (shm_key, shm_name);

  hMapObject = OpenFileMapping (FILE_MAP_WRITE,	/* read/write access */
				FALSE,	/* inherit flag */
				shm_name);	/* name of map object */
  if (hMapObject == NULL)
    {
      return NULL;
    }

  /* Get a pointer to the file-mapped shared memory. */
  lpvMem = MapViewOfFile (hMapObject,	/* object to map view of    */
			  FILE_MAP_WRITE,	/* read/write access        */
			  0,	/* high offset:   map from  */
			  0,	/* low offset:    beginning */
			  0);	/* default: map entire file */
  if (lpvMem == NULL)
    {
      CloseHandle (hMapObject);
      return NULL;
    }

  *hOut = hMapObject;
  return lpvMem;
}

#else /* WINDOWS */

/*
 * server_shm_create()
 *    return: void *
 *    shm_key(in):
 *    size(in):
 */
static void *
server_shm_create (int shm_key, int size)
{
  int mid;
  void *p;

  if (size <= 0 || shm_key <= 0)
    {
      return NULL;
    }

  mid = shmget (shm_key, size, IPC_CREAT | IPC_EXCL | SH_MODE);

  if (mid == -1)
    {
      return NULL;
    }
  p = shmat (mid, (char *) 0, 0);

  if (p == (void *) -1)
    {
      return NULL;
    }

  return p;
}

/*
 * server_shm_open()
 *    return: void *
 *    shm_key(in):
 */
static void *
server_shm_open (int shm_key)
{
  int mid;
  void *p;

  if (shm_key < 0)
    {
      return NULL;
    }
  mid = shmget (shm_key, 0, SHM_RDONLY);

  if (mid == -1)
    return NULL;

  p = shmat (mid, (char *) 0, SHM_RDONLY);

  if (p == (void *) -1)
    {
      return NULL;
    }
  return p;
}
#endif /* WINDOWS */

/*
 * server_shm_destroy() -
 *    return: int
 *    shm_key(in):
 */
static int
server_shm_destroy (int shm_key)
{
#if !defined(WINDOWS)
  int mid;

  mid = shmget (shm_key, 0, SH_MODE);

  if (mid == -1)
    {
      return -1;
    }

  if (shmctl (mid, IPC_RMID, 0) == -1)
    {
      return -1;
    }
#endif /* WINDOWS */
  return 0;
}

/*
 * diag_sm_isopened() -
 *    return : bool
 */
static bool
diag_sm_isopened (void)
{
  return (g_ShmServer == NULL) ? false : true;
}

/*
 * init_server_diag_value() -
 *    return : bool
 *    shm_server(in):
 */
static bool
init_server_diag_value (T_SHM_DIAG_INFO_SERVER * shm_server)
{
  int i, thread_num;

  if (!shm_server)
    return false;

  thread_num = shm_server->num_thread;
  for (i = 0; i < thread_num; i++)
    {
      shm_server->thread[i].query_open_page = 0;
      shm_server->thread[i].query_opened_page = 0;
      shm_server->thread[i].query_slow_query = 0;
      shm_server->thread[i].query_full_scan = 0;
      shm_server->thread[i].conn_cli_request = 0;
      shm_server->thread[i].conn_aborted_clients = 0;
      shm_server->thread[i].conn_conn_req = 0;
      shm_server->thread[i].conn_conn_reject = 0;
      shm_server->thread[i].buffer_page_write = 0;
      shm_server->thread[i].buffer_page_read = 0;
      shm_server->thread[i].lock_deadlock = 0;
      shm_server->thread[i].lock_request = 0;
    }

  return true;
}

/*
 * init_diag_sm()
 *    return: bool
 *    server_name(in):
 *    num_thread(in):
 *    err_buf(in):
 */
static bool
init_diag_sm (const char *server_name, int num_thread, char *err_buf)
{
  DIAG_SYS_CONFIG config_diag;
  char vol_dir[PATH_MAX];
  int i;

  if (server_name == NULL)
    goto init_error;
  if (uReadDiagSystemConfig (&config_diag, err_buf) != 1)
    goto init_error;
  if (!config_diag.Executediag)
    goto init_error;
  if (get_volumedir (vol_dir, server_name) == -1)
    goto init_error;

  ShmPort = getservershmid (vol_dir, server_name);

  if (ShmPort == -1)
    goto init_error;

  g_ShmServer = (T_SHM_DIAG_INFO_SERVER *)
    SERVER_SHM_CREATE (ShmPort,
		       sizeof (T_SHM_DIAG_INFO_SERVER), &shm_map_object);

  for (i = 0; (i < 5 && !g_ShmServer); i++)
    {
      if (errno == EEXIST)
	{
	  T_SHM_DIAG_INFO_SERVER *shm = (T_SHM_DIAG_INFO_SERVER *)
	    SERVER_SHM_OPEN (ShmPort,
			     &shm_map_object);
	  if (shm != NULL)
	    {
	      if ((shm->magic_key == DIAG_SERVER_MAGIC_NUMBER)
		  && (shm->servername)
		  && strcmp (shm->servername, server_name) == 0)
		{
		  SERVER_SHM_DETACH ((void *) shm, shm_map_object);
		  SERVER_SHM_DESTROY (ShmPort);
		  g_ShmServer = (T_SHM_DIAG_INFO_SERVER *)
		    SERVER_SHM_CREATE (ShmPort,
				       sizeof (T_SHM_DIAG_INFO_SERVER),
				       &shm_map_object);
		  break;
		}
	      else
		SERVER_SHM_DETACH ((void *) shm, shm_map_object);
	    }

	  ShmPort++;
	  g_ShmServer = (T_SHM_DIAG_INFO_SERVER *)
	    SERVER_SHM_CREATE (ShmPort,
			       sizeof (T_SHM_DIAG_INFO_SERVER),
			       &shm_map_object);
	}
      else
	break;
    }

  if (g_ShmServer == NULL)
    {
      if (err_buf)
	{
	  strcpy (err_buf, strerror (errno));
	}
      goto init_error;
    }

  diag_long_query_time = config_diag.server_long_query_time;
  diag_executediag = (config_diag.Executediag == 0) ? false : true;

  if (diag_long_query_time < 1)
    {
      diag_long_query_time = DB_INT32_MAX;
    }

  strcpy (g_ShmServer->servername, server_name);
  g_ShmServer->num_thread = num_thread;
  g_ShmServer->magic_key = DIAG_SERVER_MAGIC_NUMBER;

  init_server_diag_value (g_ShmServer);

  if (create_shm_key_file (ShmPort, vol_dir, server_name) == -1)
    {
      if (err_buf)
	{
	  strcpy (err_buf, strerror (errno));
	}
      SERVER_SHM_DETACH ((void *) g_ShmServer, shm_map_object);
      SERVER_SHM_DESTROY (ShmPort);
      goto init_error;
    }

  return true;

init_error:
  g_ShmServer = NULL;
  diag_executediag = false;
  diag_long_query_time = DB_INT32_MAX;
  return false;
}

/*
 * rm_diag_sm()
 *    return: bool
 *
 */
static bool
rm_diag_sm (void)
{
  if (diag_sm_isopened () == true)
    {
      SERVER_SHM_DESTROY (ShmPort);
      return true;
    }

  return false;
}

/*
 * diag_val_set_query_open_page()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_query_open_page (int value,
			      T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;

  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  if (settype == DIAG_VAL_SETTYPE_INC)
    {
      g_ShmServer->thread[thread_index].query_open_page += value;
    }
  else if (settype == DIAG_VAL_SETTYPE_SET)
    {
      g_ShmServer->thread[thread_index].query_open_page = value;
    }
  else if (settype == DIAG_VAL_SETTYPE_DEC)
    {
      g_ShmServer->thread[thread_index].query_open_page -= value;
    }

  return 0;
}

/*
 * diag_val_set_query_opened_page()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_query_opened_page (int value,
				T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;

  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].query_opened_page += value;

  return 0;
}

/*
 * diag_val_set_buffer_page_read()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_buffer_page_read (int value,
			       T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;

  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].buffer_page_read += value;

  return 0;
}

/*
 * diag_val_set_buffer_page_write()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_buffer_page_write (int value,
				T_DIAG_VALUE_SETTYPE settype, char *err_buf)
{
  int thread_index;

  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].buffer_page_write += value;

  return 0;
}


/*
 * diag_val_set_conn_aborted_clients()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_conn_aborted_clients (int value, T_DIAG_VALUE_SETTYPE settype,
				   char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].conn_aborted_clients += value;

  return 0;
}

/*
 * diag_val_set_conn_cli_request()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_conn_cli_request (int value, T_DIAG_VALUE_SETTYPE settype,
			       char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].conn_cli_request += value;

  return 0;
}

/*
 * diag_val_set_query_slow_query()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_query_slow_query (int value, T_DIAG_VALUE_SETTYPE settype,
			       char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].query_slow_query += value;

  return 0;
}

/*
 * diag_val_set_lock_deadlock()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 *
 */
static int
diag_val_set_lock_deadlock (int value, T_DIAG_VALUE_SETTYPE settype,
			    char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].lock_deadlock += value;

  return 0;
}

/*
 * diag_val_set_lock_request()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
static int
diag_val_set_lock_request (int value, T_DIAG_VALUE_SETTYPE settype,
			   char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].lock_request += value;

  return 0;
}

/*
 * diag_val_set_query_full_scan()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
static int
diag_val_set_query_full_scan (int value, T_DIAG_VALUE_SETTYPE settype,
			      char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].query_full_scan += value;

  return 0;
}

/*
 * diag_val_set_conn_conn_req()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
static int
diag_val_set_conn_conn_req (int value, T_DIAG_VALUE_SETTYPE settype,
			    char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].conn_conn_req += value;

  return 0;
}

/*
 * diag_val_set_conn_conn_reject()
 *    return: int
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
static int
diag_val_set_conn_conn_reject (int value, T_DIAG_VALUE_SETTYPE settype,
			       char *err_buf)
{
  int thread_index;
  CHECK_SHM ();
  CHECK_DIAG_OBJ_FUNC_THREAD_MGR_INIT (err_buf);
  thread_index = thread_get_current_entry_index ();

  g_ShmServer->thread[thread_index].conn_conn_reject += value;

  return 0;
}

/* Interface function */
/*
 * init_diag_mgr()
 *    return: bool
 *    server_name(in):
 *    num_thread(in):
 *    err_buf(in):
 */
bool
init_diag_mgr (const char *server_name, int num_thread, char *err_buf)
{
  if (init_diag_sm (server_name, num_thread, err_buf) == false)
    return false;

  return true;
}

/*
 * close_diag_mgr()
 *    return: none
 */
void
close_diag_mgr (void)
{
  rm_diag_sm ();
}

/*
 * set_diag_value() -
 *    return: bool
 *    type(in):
 *    value(in):
 *    settype(in):
 *    err_buf(in):
 */
bool
set_diag_value (T_DIAG_OBJ_TYPE type, int value, T_DIAG_VALUE_SETTYPE settype,
		char *err_buf)
{
  T_DO_FUNC task_func;

  if (diag_executediag == false)
    return false;

  task_func = diag_obj_list[type].func;

  if (task_func (value, settype, err_buf) < 0)
    {
      return false;
    }
  else
    {
      return true;
    }
}
#endif /* SERVER_MODE */

#if !defined(CS_MODE)
int mnt_Num_tran_exec_stats = 0;

/* Server execution statistics on each transactions */
struct mnt_server_table
{
  int num_tran_indices;
  MNT_SERVER_EXEC_STATS **stats;
#if defined (SERVER_MODE)
  MUTEX_T lock;
#endif				/* SERVER_MODE */
};
static struct mnt_server_table mnt_Server_table = {
  0, NULL
#if defined (SERVER_MODE)
    , MUTEX_INITIALIZER
#endif /* SERVER_MODE */
};


static void mnt_server_reset_stats_internal (MNT_SERVER_EXEC_STATS * stats);


/*
 * mnt_server_init - Initialize monitoring resources in the server
 *   return: NO_ERROR or ER_FAILED
 *   num_tran_indices(in): maximum number of know transaction indices
 */
int
mnt_server_init (int num_tran_indices)
{
  MNT_SERVER_EXEC_STATS **stats;
  size_t size;
  int rv, i;

  size = num_tran_indices * sizeof (*stats);

  MUTEX_LOCK (rv, mnt_Server_table.lock);
  if (mnt_Server_table.stats != NULL)
    {
      MUTEX_UNLOCK (mnt_Server_table.lock);
      return ER_FAILED;
    }

  if ((mnt_Server_table.stats = malloc (size)) == NULL)
    {
      MUTEX_UNLOCK (mnt_Server_table.lock);
      return ER_FAILED;
    }

  for (i = 0; i < num_tran_indices; i++)
    mnt_Server_table.stats[i] = NULL;
  mnt_Server_table.num_tran_indices = num_tran_indices;
  mnt_Num_tran_exec_stats = 0;
  MUTEX_UNLOCK (mnt_Server_table.lock);

  return NO_ERROR;
}

/*
 * mnt_server_final - Terminate monitoring resources in the server
 *   return: none
 */
void
mnt_server_final (void)
{
  void *table;
  int rv, i;

  MUTEX_LOCK (rv, mnt_Server_table.lock);
  if (mnt_Server_table.stats != NULL)
    {
      table = mnt_Server_table.stats;
      for (i = 0; i < mnt_Server_table.num_tran_indices; i++)
	{
	  if (mnt_Server_table.stats[i] != NULL)
	    free_and_init (mnt_Server_table.stats[i]);
	}
      mnt_Server_table.stats = NULL;
      mnt_Server_table.num_tran_indices = 0;
      mnt_Num_tran_exec_stats = 0;
      free_and_init (table);
    }
  MUTEX_UNLOCK (mnt_Server_table.lock);
}


/*
 * xmnt_server_start_stats - Start collecting server execution statistics
 *                           for the current transaction index
 *   return: NO_ERROR or ER_FAILED
 */
int
xmnt_server_start_stats (THREAD_ENTRY * thread_p)
{
  int tran_index;
  int rv;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0);

  MUTEX_LOCK (rv, mnt_Server_table.lock);
  /* Get an statistic block for current transaction if one is not available */
  if (mnt_Server_table.num_tran_indices > tran_index
      && mnt_Server_table.stats[tran_index] == NULL)
    {
      if ((mnt_Server_table.stats[tran_index] =
	   malloc (sizeof (MNT_SERVER_EXEC_STATS))) == NULL)
	{
	  MUTEX_UNLOCK (mnt_Server_table.lock);
	  return ER_FAILED;
	}
      MUTEX_INIT (mnt_Server_table.stats[tran_index]->lock);
    }
  /* Restart the statistics */
  mnt_server_reset_stats_internal (mnt_Server_table.stats[tran_index]);

  mnt_Num_tran_exec_stats++;

  MUTEX_UNLOCK (mnt_Server_table.lock);

  return NO_ERROR;
}

/*
 * xmnt_server_stop_stats - Stop collecting server execution statistics
 *                          for the current transaction index
 *   return: none
 */
void
xmnt_server_stop_stats (THREAD_ENTRY * thread_p)
{
  int tran_index;
  int rv;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index >= 0);

  MUTEX_LOCK (rv, mnt_Server_table.lock);
  if (mnt_Server_table.num_tran_indices > tran_index
      && mnt_Server_table.stats[tran_index] != NULL)
    {
      MUTEX_DESTROY (mnt_Server_table.stats[tran_index]->lock);
      free_and_init (mnt_Server_table.stats[tran_index]);
      mnt_Server_table.stats[tran_index] = NULL;
      mnt_Num_tran_exec_stats--;
    }
  MUTEX_UNLOCK (mnt_Server_table.lock);
}

/*
 * mnt_server_get_stats - Get the recorded server statistics for the current
 *                        transaction index
 *   return: server statistics
 */
MNT_SERVER_EXEC_STATS *
mnt_server_get_stats (THREAD_ENTRY * thread_p)
{
  int tran_index;
  int rv;
  MNT_SERVER_EXEC_STATS *p;

  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
  assert (tran_index > 0);

  MUTEX_LOCK (rv, mnt_Server_table.lock);
  if (mnt_Server_table.num_tran_indices > tran_index)
    p = mnt_Server_table.stats[tran_index];
  else
    p = NULL;
  MUTEX_UNLOCK (mnt_Server_table.lock);

  return p;
}

/*
 * xmnt_server_copy_stats - Copy recorded server statistics for the current
 *                          transaction index
 *   return: none
 *   to_stats(out): buffer to copy
 */
void
xmnt_server_copy_stats (THREAD_ENTRY * thread_p,
			MNT_SERVER_EXEC_STATS * to_stats)
{
  MNT_SERVER_EXEC_STATS *from_stats;
  int rv;

  if ((from_stats = mnt_server_get_stats (thread_p)) == NULL)
    {
      mnt_server_reset_stats_internal (to_stats);
    }
  else
    {
      MUTEX_LOCK (rv, from_stats->lock);
      *to_stats = *from_stats;	/* Structure copy */
      MUTEX_UNLOCK (from_stats->lock);
    }
}

/*
 * xmnt_server_reset_stats - Reset the server statistics for current
 *                           transaction index
 *   return: none
 */
void
xmnt_server_reset_stats (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      mnt_server_reset_stats_internal (stats);
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_server_reset_stats_internal - Reset server stadistics of given block
 *   return: none
 *   stats(in/out): server statistics block to reset
 */
static void
mnt_server_reset_stats_internal (MNT_SERVER_EXEC_STATS * stats)
{
  stats->pb_num_fetches = 0;
  stats->pb_num_dirties = 0;
  stats->pb_num_ioreads = 0;
  stats->pb_num_iowrites = 0;

  stats->log_num_ioreads = 0;
  stats->log_num_iowrites = 0;
  stats->log_num_appendrecs = 0;
  stats->log_num_archives = 0;
  stats->log_num_checkpoints = 0;

  stats->lk_num_acquired_on_pages = 0;
  stats->lk_num_acquired_on_objects = 0;
  stats->lk_num_converted_on_pages = 0;
  stats->lk_num_converted_on_objects = 0;
  stats->lk_num_re_requested_on_pages = 0;
  stats->lk_num_re_requested_on_objects = 0;
  stats->lk_num_waited_on_pages = 0;
  stats->lk_num_waited_on_objects = 0;

  stats->io_num_format_volume = 0;
}

/*
 * enclosing_method - Print server statistics for current transaction index
 *   return: none
 *   stream(in): if NULL is given, stdout is used
 */
void
mnt_server_print_stats (THREAD_ENTRY * thread_p, FILE * stream)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) == NULL)
    return;
  MUTEX_LOCK (rv, stats->lock);
  mnt_server_dump_stats (stats, stream);
  MUTEX_UNLOCK (stats->lock);
}

/*
 * mnt_x_pb_fetches - Increase pb_num_fetches counter of the current
 *                    transaction index
 *   return: none
 */
void
mnt_x_pb_fetches (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->pb_num_fetches++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_pb_dirties - Increase pb_num_dirties counter of the current
 *                    transaction index
 *   return: none
 */
void
mnt_x_pb_dirties (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->pb_num_dirties++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_pb_ioreads - Increase pb_num_ioreads counter of the current
 *                    transaction index
 *   return: none
 */
void
mnt_x_pb_ioreads (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->pb_num_ioreads++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_pb_iowrites - Increase pb_num_iowrites counter of the current
 *                     transaction index
 *   return: none
 */
void
mnt_x_pb_iowrites (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->pb_num_iowrites++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_log_ioreads - Increase pb_num_ioreads counter of the current
 *                     transaction index
 *   return: none
 */
void
mnt_x_log_ioreads (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->log_num_ioreads++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_log_iowrites - Increase log_num_iowrites counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_log_iowrites (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->log_num_iowrites++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_log_appendrecs - Increase log_num_appendrecs counter of the current
 *                        transaction index
 *   return: none
 */
void
mnt_x_log_appendrecs (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->log_num_appendrecs++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_log_archives - Increase log_num_archives counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_log_archives (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->log_num_archives++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_log_checkpoints - Increase log_num_checkpoints counter of the current
 *                      transaction index
 *   return: none
 */
void
mnt_x_log_checkpoints (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->log_num_checkpoints++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_lk_acquired_on_pages - Increase lk_num_acquired_on_pages counter
 *                              of the current transaction index
 *   return: none
 */
void
mnt_x_lk_acquired_on_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->lk_num_acquired_on_pages++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_lk_acquired_on_objects - Increase lk_num_acquired_on_objects counter
 *                                of the current transaction index
 *   return: none
 */
void
mnt_x_lk_acquired_on_objects (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->lk_num_acquired_on_objects++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_lk_converted_on_pages - Increase lk_num_converted_on_pages counter
 *                               of the current transaction index
 *   return: none
 */
void
mnt_x_lk_converted_on_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->lk_num_converted_on_pages++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_lk_converted_on_objects - Increase lk_num_converted_on_objects
 *                                 counter of the current transaction index
 *   return: none
 */
void
mnt_x_lk_converted_on_objects (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->lk_num_converted_on_objects++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_lk_re_requested_on_pages - Increase lk_num_re_requested_on_pages
 *                                  counter of the current transaction index
 *   return: none
 */
void
mnt_x_lk_re_requested_on_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->lk_num_re_requested_on_pages++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_lk_re_requested_on_objects - Increase lk_num_re_requested_on_objects
 *                                    counter of the current transaction index
 *   return: none
 */
void
mnt_x_lk_re_requested_on_objects (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->lk_num_re_requested_on_objects++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_lk_waited_on_pages - Increase lk_num_waited_on_pages counter of the
 *                            current transaction index
 *   return: none
 */
void
mnt_x_lk_waited_on_pages (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->lk_num_waited_on_pages++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_lk_waited_on_objects - Increase lk_num_waited_on_objects counter of
 *                              the current transaction index
 *   return: none
 */
void
mnt_x_lk_waited_on_objects (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->lk_num_waited_on_objects++;
      MUTEX_UNLOCK (stats->lock);
    }
}

/*
 * mnt_x_io_format_vols - Increase io_num_format_volume counter of the current
 *                        transaction index
 *   return: none
 */
void
mnt_x_io_format_vols (THREAD_ENTRY * thread_p)
{
  MNT_SERVER_EXEC_STATS *stats;
  int rv;

  if ((stats = mnt_server_get_stats (thread_p)) != NULL)
    {
      MUTEX_LOCK (rv, stats->lock);
      stats->io_num_format_volume++;
      MUTEX_UNLOCK (stats->lock);
    }
}
#endif

/*
 * mnt_server_dump_stats - Print the given server statistics
 *   return: none
 *   stats(in) server statistics to print
 *   stream(in): if NULL is given, stdout is used
 */
void
mnt_server_dump_stats (const MNT_SERVER_EXEC_STATS * stats, FILE * stream)
{
  if (stream == NULL)
    stream = stdout;

  fprintf (stream, "\n *** SERVER EXECUTION STATISTICS *** \n");

  fprintf (stream, "Num_data_page_fetches         = %10u\n",
	   stats->pb_num_fetches);
  fprintf (stream, "Num_data_page_dirties         = %10u\n",
	   stats->pb_num_dirties);
  fprintf (stream, "Num_data_page_ioreads         = %10u\n",
	   stats->pb_num_ioreads);
  fprintf (stream, "Num_data_page_iowrites        = %10u\n",
	   stats->pb_num_iowrites);

  fprintf (stream, "Num_log_page_ioreads          = %10u\n",
	   stats->log_num_ioreads);
  fprintf (stream, "Num_log_page_iowrites         = %10u\n",
	   stats->log_num_iowrites);
  fprintf (stream, "Num_log_append_records        = %10u\n",
	   stats->log_num_appendrecs);
  fprintf (stream, "Num_log_archives              = %10u\n",
	   stats->log_num_archives);
  fprintf (stream, "Num_log_checkpoints           = %10u\n",
	   stats->log_num_checkpoints);

  fprintf (stream, "Num_page_locks_acquired       = %10u\n",
	   stats->lk_num_acquired_on_pages);
  fprintf (stream, "Num_object_locks_acquired     = %10u\n",
	   stats->lk_num_acquired_on_objects);
  fprintf (stream, "Num_page_locks_converted      = %10u\n",
	   stats->lk_num_converted_on_pages);
  fprintf (stream, "Num_object_locks_converted    = %10u\n",
	   stats->lk_num_converted_on_objects);
  fprintf (stream, "Num_page_locks_re-requested   = %10u\n",
	   stats->lk_num_re_requested_on_pages);
  fprintf (stream, "Num_object_locks_re-requested = %10u\n",
	   stats->lk_num_re_requested_on_objects);
  fprintf (stream, "Num_page_locks_waits          = %10u\n",
	   stats->lk_num_waited_on_pages);
  fprintf (stream, "Num_object_locks_waits        = %10u\n",
	   stats->lk_num_waited_on_objects);

  fprintf (stream, "Num_format_volume             = %10u\n",
	   stats->io_num_format_volume);
}

/*
 * mnt_get_current_times - Get current CPU and elapsed times
 *   return:
 *   cpu_user_time(out):
 *   cpu_sys_time(out):
 *   elapsed_time(out):
 *
 * Note:
 */
void
mnt_get_current_times (time_t * cpu_user_time, time_t * cpu_sys_time,
		       time_t * elapsed_time)
{
#if defined (WINDOWS)
  *cpu_user_time = 0;
  *cpu_sys_time = 0;
  *elapsed_time = 0;

  *elapsed_time = time (NULL);
#else /* WINDOWS */
  struct rusage rusage;

  *cpu_user_time = 0;
  *cpu_sys_time = 0;
  *elapsed_time = 0;

  *elapsed_time = time (NULL);

  if (getrusage (RUSAGE_SELF, &rusage) == 0)
    {
      *cpu_user_time = rusage.ru_utime.tv_sec;
      *cpu_sys_time = rusage.ru_stime.tv_sec;
    }
#endif /* WINDOWS */
};
