/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * broker_shm.c -
 */

#ident "$Id$"

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <errno.h>

#if defined(WINDOWS)
#include <windows.h>
#else
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#include "cas_common.h"
#include "broker_shm.h"
#include "broker_error.h"

#if defined(WINDOWS)
#include "broker_list.h"
#endif

#define 	SHMODE			0644

#define 	MAGIC_NUMBER		0x20081209

#if defined(WINDOWS)
static int shm_id_cmp_func (void *key1, void *key2);
static int shm_info_assign_func (T_LIST * node, void *key, void *value);
static char *shm_id_to_name (int shm_key);
#endif

#if defined(WINDOWS)
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
#if defined(WINDOWS)
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

#if defined(WINDOWS)
void *
uw_shm_create (int shm_key, int size, int which_shm)
{
  LPVOID lpvMem = NULL;
  HANDLE hMapObject = NULL;
  char *shm_name;

  shm_name = shm_id_to_name (shm_key);

  hMapObject = CreateFileMapping (INVALID_HANDLE_VALUE,
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
#if defined(WINDOWS)
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

#if defined(WINDOWS)
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

#if defined(WINDOWS)
int
uw_shm_get_magic_number ()
{
  return MAGIC_NUMBER;
}
#endif

#if defined(WINDOWS)
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

#if 0
static void
bs_log_msg (LPTSTR lpszMsg)
{
  char lpDisplayBuf[4096];
  sprintf (lpDisplayBuf, "%s", lpszMsg);
  MessageBox (NULL, (LPCTSTR) lpDisplayBuf, TEXT ("Error"), MB_OK);
}

static void
bs_last_error_msg (LPTSTR lpszFunction)
{
  LPVOID lpDisplayBuf;
  LPVOID lpMsgBuf;
  DWORD dw;

  dw = GetLastError ();
  FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
		 FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw,
		 MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
		 (LPTSTR) & lpMsgBuf, 0, NULL);
  lpDisplayBuf =
    (LPVOID) LocalAlloc (LMEM_ZEROINIT,
			 (lstrlen ((LPCTSTR) lpMsgBuf) +
			  lstrlen ((LPCTSTR) lpszFunction) +
			  40) * sizeof (TCHAR));
  sprintf ((LPTSTR) lpDisplayBuf, "%s failed with error %d: %s", lpszFunction,
	   dw, lpMsgBuf);
  MessageBox (NULL, (LPCTSTR) lpDisplayBuf, TEXT ("Error"), MB_OK);
  LocalFree (lpMsgBuf);
  LocalFree (lpDisplayBuf);
}
#endif

#if defined (WINDOWS)
static HANDLE con_status_sem;

static int
uw_sem_open (char **sem_name)
{
  con_status_sem = OpenMutex (SYNCHRONIZE, FALSE, sem_name);
  if (con_status_sem == NULL)
    {
      return -1;
    }
  return 0;
}

int
uw_sem_init (char **sem_name, char *br_name, int as_index)
{
  snprintf (sem_name, BROKER_NAME_LEN, "%s_%d", br_name, as_index);
  con_status_sem = CreateMutex (NULL, FALSE, *sem_name);
  if (con_status_sem == NULL && GetLastError () == ERROR_ALREADY_EXISTS)
    {
      con_status_sem = OpenMutex (SYNCHRONIZE, FALSE, sem_name);
    }

  if (con_status_sem == NULL)
    {
      return -1;
    }
  else
    {
      return 0;
    }
}

int
uw_sem_wait (char **sem_name)
{
  DWORD dwWaitResult;

  if (con_status_sem == NULL)
    {
      if (uw_sem_open (sem_name) != 0)
	{
	  return -1;
	}
    }

  dwWaitResult = WaitForSingleObject (con_status_sem, INFINITE);
  switch (dwWaitResult)
    {
    case WAIT_OBJECT_0:
      return 0;
    case WAIT_TIMEOUT:
    case WAIT_FAILED:
    case WAIT_ABANDONED:
      return -1;
    }
}

int
uw_sem_post (char **sem_name)
{
  /* if (con_status_sem == NULL) exit (-100); */
  if (ReleaseMutex (con_status_sem) != 0)
    return 0;
  return -1;
}

int
uw_sem_destroy (char **sem_name)
{
  if (con_status_sem != NULL && CloseHandle (con_status_sem) != 0)
    return 0;
  return -1;
}
#else
int
uw_sem_init (sem_t * sem)
{
  return sem_init (sem, 1, 1);
}

int
uw_sem_wait (sem_t * sem)
{
  return sem_wait (sem);
}

int
uw_sem_post (sem_t * sem)
{
  return sem_post (sem);
}

int
uw_sem_destroy (sem_t * sem)
{
  return sem_destroy (sem);
}
#endif
