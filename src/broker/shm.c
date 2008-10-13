/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * shm.c - 
 */

#ident "$Id$"

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <errno.h>

#ifdef WIN32
#include <windows.h>
#else
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#include "cas_common.h"
#include "shm.h"
#include "error.h"

#ifdef WIN32
#include "link_list.h"
#endif

#define 	SHMODE			0644

#define 	MAGIC_NUMBER		0x20080912

#ifdef WIN32
static int shm_id_cmp_func (void *key1, void *key2);
static int shm_info_assign_func (T_LIST * node, void *key, void *value);
static char *shm_id_to_name (int shm_key);
#endif

#ifdef WIN32
T_LIST *shm_id_list_header = NULL;
#endif

/*
 * name:        uw_shm_open
 *
 * arguments:
 *		key - caller module defined shmared memory key.
 *		      if 'key' value is 0, this module set key value
 *		      from shm key file.
 *
 * returns/side-effects:
 *              attached shared memory pointer if no error
 *		NULL if error shm open error.
 *
 * description: get and attach shared memory
 *
 */
#ifdef WIN32
void *
uw_shm_open (int shm_key, int which_shm, T_SHM_MODE shm_mode)
{
  LPVOID lpvMem = NULL;		/* address of shared memory */
  HANDLE hMapObject = NULL;
  DWORD dwAccessRight;
  char *shm_name;

  shm_name = shm_id_to_name (shm_key);

  if (shm_mode == SHM_MODE_MONITOR)
    {
      dwAccessRight = FILE_MAP_READ;
    }
  else
    {
      dwAccessRight = FILE_MAP_WRITE;
    }

  hMapObject = OpenFileMapping (dwAccessRight, FALSE,	/* inherit flag */
				shm_name);	/* name of map object */

  if (hMapObject == NULL)
    {
      return NULL;
    }

  /* Get a pointer to the file-mapped shared memory. */
  lpvMem = MapViewOfFile (hMapObject,	/* object to map view of    */
			  dwAccessRight, 0,	/* high offset:   map from  */
			  0,	/* low offset:    beginning */
			  0);	/* default: map entire file */

  if (lpvMem == NULL)
    {
      CloseHandle (hMapObject);
      return NULL;
    }

  link_list_add (&shm_id_list_header,
		 (void *) lpvMem, (void *) hMapObject, shm_info_assign_func);

  return lpvMem;
}
#else
void *
uw_shm_open (int shm_key, int which_shm, T_SHM_MODE shm_mode)
{
  int mid;
  void *p;

  if (shm_key < 0)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, 0);
      return NULL;
    }
  mid = shmget (shm_key, 0, SHMODE);

  if (mid == -1)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, errno);
      return NULL;
    }
  p =
    shmat (mid, (char *) 0, ((shm_mode == SHM_MODE_ADMIN) ? 0 : SHM_RDONLY));
  if (p == (void *) -1)
    {
      UW_SET_ERROR_CODE (UW_ER_SHM_OPEN, errno);
      return NULL;
    }
  if (which_shm == SHM_APPL_SERVER)
    {
      if (((T_SHM_APPL_SERVER *) p)->magic == MAGIC_NUMBER)
	return p;
    }
  else
    {
      if (((T_SHM_BROKER *) p)->magic == MAGIC_NUMBER)
	return p;
    }
  UW_SET_ERROR_CODE (UW_ER_SHM_OPEN_MAGIC, 0);
  return NULL;
}
#endif

/*
 * name:        uw_shm_create
 *
 * arguments:
 *		NONE
 *
 * returns/side-effects:
 *              created shared memory ptr if no error
 *		NULL if error
 *
 * description: create and attach shared memory
 *		unless shared memory is already created with same key
 *
 */

#ifdef WIN32
void *
uw_shm_create (int shm_key, int size, int which_shm)
{
  LPVOID lpvMem = NULL;
  HANDLE hMapObject = NULL;
  char *shm_name;

  shm_name = shm_id_to_name (shm_key);

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

  link_list_add (&shm_id_list_header,
		 (void *) lpvMem, (void *) hMapObject, shm_info_assign_func);

  return lpvMem;
}
#else
void *
uw_shm_create (int shm_key, int size, int which_shm)
{
  int mid;
  void *p;

  if (size <= 0 || shm_key <= 0)
    return NULL;

  mid = shmget (shm_key, size, IPC_CREAT | IPC_EXCL | SHMODE);

  if (mid == -1)
    return NULL;
  p = shmat (mid, (char *) 0, 0);

  if (p == (void *) -1)
    return NULL;
  else
    {
      if (which_shm == SHM_APPL_SERVER)
	{
#ifdef USE_MUTEX
	  me_reset_sv (&(((T_SHM_APPL_SERVER *) p)->lock));
#endif /* USE_MUTEX */
	  ((T_SHM_APPL_SERVER *) p)->magic = MAGIC_NUMBER;
	}
      else
	{
#ifdef USE_MUTEX
	  me_reset_sv (&(((T_SHM_BROKER *) p)->lock));
#endif
	  ((T_SHM_BROKER *) p)->magic = MAGIC_NUMBER;
	}
    }
  return p;
}
#endif

/*
 * name:        uw_shm_destroy
 *
 * arguments:
 *		NONE
 *
 * returns/side-effects:
 *              void
 *
 * description: destroy shared memory
 *
 */
int
uw_shm_destroy (int shm_key)
{
#ifdef WIN32
  return 0;
#else
  int mid;

  mid = shmget (shm_key, 0, SHMODE);

  if (mid == -1)
    {
      return -1;
    }

  if (shmctl (mid, IPC_RMID, 0) == -1)
    {
      return -1;
    }

  return 0;

#endif
}

#ifdef WIN32
void
uw_shm_detach (void *p)
{
  HANDLE hMapObject;

  SLEEP_MILISEC (0, 10);
  LINK_LIST_FIND_VALUE (hMapObject, shm_id_list_header, p, shm_id_cmp_func);
  if (hMapObject == NULL)
    return;

  UnmapViewOfFile (p);
  CloseHandle (hMapObject);
  link_list_node_delete (&shm_id_list_header, p, shm_id_cmp_func, NULL);
}
#else
void
uw_shm_detach (void *p)
{
  shmdt (p);
}
#endif

#ifdef WIN32
int
uw_shm_get_magic_number ()
{
  return MAGIC_NUMBER;
}
#endif

#ifdef WIN32
static int
shm_id_cmp_func (void *key1, void *key2)
{
  if (key1 == key2)
    return 1;
  return 0;
}

static int
shm_info_assign_func (T_LIST * node, void *key, void *value)
{
  node->key = key;
  node->value = value;
  return 0;
}

static char *
shm_id_to_name (int shm_key)
{
  static char shm_name[32];

  sprintf (shm_name, "v3mapfile%d", shm_key);
  return shm_name;
}
#endif
