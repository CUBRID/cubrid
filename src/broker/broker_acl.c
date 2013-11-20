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
 * broker_acl.c -
 */

#ident "$Id$"

#include <assert.h>
#include <errno.h>

#include "porting.h"
#include "broker_acl.h"
#include "cas_error.h"
#include "broker_util.h"
#include "broker_filename.h"

#define ADMIN_ERR_MSG_SIZE	1024
#define ACCESS_FILE_DELIMITER ":"
#define IP_FILE_DELIMITER ","

ACCESS_INFO access_info[ACL_MAX_ITEM_COUNT];
int num_access_info;
int access_info_changed;

static ACCESS_INFO *access_control_find_access_info (ACCESS_INFO ai[],
						     int size, char *dbname,
						     char *dbuser);
static int access_control_read_ip_info (IP_INFO * ip_info, char *filename,
					char *admin_err_msg);
static void access_control_repath_file (char *path);
static int access_control_check_right_internal (T_SHM_APPL_SERVER * shm_as_p,
						char *dbname, char *dbuser,
						unsigned char *address);
static int access_control_check_ip (T_SHM_APPL_SERVER * shm_as_p,
				    IP_INFO * ip_info, unsigned char *address,
				    int info_index);
static char *access_control_trim (char *str);
static int record_ip_access_time (T_SHM_APPL_SERVER * shm_as_p,
				  int info_index, int list_index);

int
access_control_set_shm (T_SHM_APPL_SERVER * shm_as_p,
			T_BROKER_INFO * br_info_p, T_SHM_BROKER * shm_br,
			char *admin_err_msg)
{
  shm_as_p->access_control = shm_br->access_control;
  shm_as_p->acl_chn = 0;

  if (shm_br->access_control && shm_br->access_control_file[0] != '\0')
    {
      char *access_file_name;
#if defined (WINDOWS)
      char sem_name[BROKER_NAME_LEN];

      MAKE_ACL_SEM_NAME (sem_name, br_info_p->name);

      if (uw_sem_init (sem_name) < 0)
	{
	  sprintf (admin_err_msg, "%s: cannot initialize acl semaphore",
		   br_info_p->name);
	  return -1;
	}
#else
      if (uw_sem_init (&shm_as_p->acl_sem) < 0)
	{
	  sprintf (admin_err_msg, "%s: cannot initialize acl semaphore",
		   br_info_p->name);
	  return -1;
	}
#endif
      if (shm_br->access_control_file[0] != '\0')
	{
	  set_cubrid_file (FID_ACCESS_CONTROL_FILE,
			   shm_br->access_control_file);
	  access_file_name = get_cubrid_file_ptr (FID_ACCESS_CONTROL_FILE);
	  if (access_control_read_config_file (shm_as_p,
					       access_file_name,
					       admin_err_msg) != 0)
	    {
	      return -1;
	    }
	}
    }

  return 0;
}

static ACCESS_INFO *
access_control_find_access_info (ACCESS_INFO ai[], int size,
				 char *dbname, char *dbuser)
{
  int i;

  for (i = 0; i < size; i++)
    {
      if (strcmp (ai[i].dbname, dbname) == 0
	  && strcmp (ai[i].dbuser, dbuser) == 0)
	{
	  return &ai[i];
	}
    }

  return NULL;
}

int
access_control_read_config_file (T_SHM_APPL_SERVER * shm_appl,
				 char *filename, char *admin_err_msg)
{
  char buf[1024], path_buf[BROKER_PATH_MAX], *files, *token, *save;
  FILE *fd_access_list;
  int num_access_list = 0, line = 0;
  ACCESS_INFO new_access_info[ACL_MAX_ITEM_COUNT];
  ACCESS_INFO *access_info;
  bool is_current_broker_section;
#if defined(WINDOWS)
  char acl_sem_name[BROKER_NAME_LEN];
#endif

  fd_access_list = fopen (filename, "r");

  if (fd_access_list == NULL)
    {
      sprintf (admin_err_msg,
	       "%s: error while loading access control file(%s)",
	       shm_appl->broker_name, filename);
      return -1;
    }

  is_current_broker_section = false;

  memset (new_access_info, '\0', sizeof (new_access_info));

  while (fgets (buf, 1024, fd_access_list))
    {
      char *dbname, *dbuser, *ip_file, *p;

      line++;
      p = strchr (buf, '#');
      if (p != NULL)
	{
	  *p = '\0';
	}

      trim (buf);

      if (buf[0] == '\0')
	{
	  continue;
	}

      if (is_current_broker_section == false &&
	  strncmp (buf, "[%", 2) == 0 && buf[strlen (buf) - 1] == ']')
	{
	  buf[strlen (buf) - 1] = '\0';
	  if (strcasecmp (shm_appl->broker_name, buf + 2) == 0)
	    {
	      is_current_broker_section = true;
	      continue;
	    }
	}
      if (is_current_broker_section == false)
	{
	  continue;
	}

      if (strncmp (buf, "[%", 2) == 0 && buf[strlen (buf) - 1] == ']')
	{
	  buf[strlen (buf) - 1] = '\0';
	  if (strcasecmp (shm_appl->broker_name, buf + 2) != 0)
	    {
	      break;
	    }
	}

      if (num_access_list >= ACL_MAX_ITEM_COUNT)
	{
	  sprintf (admin_err_msg,
		   "%s: error while loading access control file(%s)"
		   " - max item count(%d) exceeded.",
		   shm_appl->broker_name, filename, ACL_MAX_ITEM_COUNT);
	  goto error;
	}

      dbname = strtok_r (buf, ACCESS_FILE_DELIMITER, &p);
      if (dbname == NULL || strlen (dbname) > (ACL_MAX_DBNAME_LENGTH - 1))
	{
	  sprintf (admin_err_msg,
		   "%s: error while loading access control file(%s:%d)"
		   " - Database name is empty or too long.",
		   shm_appl->broker_name, filename, line);
	  goto error;
	}

      dbuser = strtok_r (NULL, ACCESS_FILE_DELIMITER, &p);
      if (dbuser == NULL || strlen (dbuser) > (ACL_MAX_DBUSER_LENGTH - 1))
	{
	  sprintf (admin_err_msg,
		   "%s: error while loading access control file(%s:%d)"
		   " - Database user is empty or too long.",
		   shm_appl->broker_name, filename, line);
	  goto error;
	}

      ip_file = p;
      if (ip_file == NULL)
	{
	  sprintf (admin_err_msg,
		   "%s: error while loading access control file(%s:%d)"
		   " - IP list file paths are empty.",
		   shm_appl->broker_name, filename, line);
	  goto error;
	}

      access_info = access_control_find_access_info (new_access_info,
						     num_access_list, dbname,
						     dbuser);
      if (access_info == NULL)
	{
	  access_info = &new_access_info[num_access_list];
	  strncpy (access_info->dbname, dbname, ACL_MAX_DBNAME_LENGTH);
	  strncpy (access_info->dbuser, dbuser, ACL_MAX_DBUSER_LENGTH);
	  num_access_list++;
	}

      if (access_info->ip_files[0] != '\0')
	{
	  strncat (access_info->ip_files, ",", LINE_MAX - 1);
	}
      strncat (access_info->ip_files, ip_file, LINE_MAX - 1);
      for (files = ip_file;; files = NULL)
	{
	  token = strtok_r (files, IP_FILE_DELIMITER, &save);
	  if (token == NULL)
	    {
	      break;
	    }

	  if (strlen (token) > BROKER_PATH_MAX - 1)
	    {
	      snprintf (admin_err_msg, ADMIN_ERR_MSG_SIZE,
			"%s: error while loading access control file(%s)"
			" - a IP file path(%s) is too long",
			shm_appl->broker_name, filename, token);
	      goto error;
	    }

	  strncpy (path_buf, token, BROKER_PATH_MAX);
	  access_control_repath_file (path_buf);
	  if (access_control_read_ip_info (&(access_info->ip_info),
					   path_buf, admin_err_msg) < 0)
	    {
	      goto error;
	    }
	}
    }

  fclose (fd_access_list);

#if defined (WINDOWS)
  MAKE_ACL_SEM_NAME (acl_sem_name, shm_appl->broker_name);
  uw_sem_wait (acl_sem_name);
#else
  uw_sem_wait (&shm_appl->acl_sem);
#endif

  memcpy (shm_appl->access_info, new_access_info, sizeof (new_access_info));
  shm_appl->num_access_info = num_access_list;
  shm_appl->acl_chn++;

#if defined(WINDOWS)
  uw_sem_post (acl_sem_name);
#else
  uw_sem_post (&shm_appl->acl_sem);
#endif

  return 0;

error:
  fclose (fd_access_list);

  return -1;
}

static void
access_control_repath_file (char *path)
{
  char tmp_str[BROKER_PATH_MAX];

  trim (path);
  strncpy (tmp_str, path, BROKER_PATH_MAX);
  tmp_str[BROKER_PATH_MAX - 1] = 0;

  MAKE_FILEPATH (path, tmp_str, BROKER_PATH_MAX);

  if (IS_ABS_PATH (path))
    {
      return;
    }

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  envvar_confdir_file (tmp_str, BROKER_PATH_MAX, path);
  MAKE_FILEPATH (path, tmp_str, BROKER_PATH_MAX);
#endif

  return;
}

static int
access_control_read_ip_info (IP_INFO * ip_info, char *filename,
			     char *admin_err_msg)
{
  char buf[LINE_MAX];
  char *save;
  FILE *fd_ip_list;
  unsigned char i, ln = 0;

  fd_ip_list = fopen (filename, "r");

  if (fd_ip_list == NULL)
    {
      sprintf (admin_err_msg, "Could not open ip info file(%s)", filename);
      return -1;
    }

  buf[LINE_MAX - 2] = 0;
  while (fgets (buf, LINE_MAX, fd_ip_list))
    {
      char *token, *p;
      int address_index;

      ln++;
      if (buf[LINE_MAX - 2] != 0 && buf[LINE_MAX - 2] != '\n')
	{
	  sprintf (admin_err_msg, "Error while loading ip info file(%s)"
		   " - %d line is too long", filename, ln);
	  goto error;
	}

      p = strchr (buf, '#');
      if (p != NULL)
	{
	  *p = '\0';
	}

      trim (buf);
      if (buf[0] == '\0')
	{
	  continue;
	}

      if (ip_info->num_list >= ACL_MAX_IP_COUNT)
	{
	  sprintf (admin_err_msg,
		   "Error while loading ip info file(%s) line(%d)"
		   " - max ip count(%d) exceeded.",
		   filename, ln, ACL_MAX_IP_COUNT);
	  goto error;
	}

      token = strtok_r (buf, ".", &save);

      address_index = ip_info->num_list * IP_BYTE_COUNT;
      for (i = 0; i < 4; i++)
	{
	  if (token == NULL)
	    {
	      sprintf (admin_err_msg,
		       "Error while loading ip info file(%s) line(%d)",
		       filename, ln);
	      goto error;
	    }

	  if (strcmp (token, "*") == 0)
	    {
	      break;
	    }
	  else
	    {
	      long adr;
	      char *p = NULL;

	      adr = strtol (token, &p, 10);
	      if ((errno == ERANGE) ||
		  (errno != 0 && adr == 0) ||
		  (p && *p != '\0') || (adr > 255 || adr < 0))
		{
		  sprintf (admin_err_msg,
			   "Error while loading ip info file(%s) line(%d)",
			   filename, ln);
		  goto error;
		}

	      ip_info->address_list[address_index + 1 + i] =
		(unsigned char) adr;
	    }

	  token = strtok_r (NULL, ".", &save);
	  if (i == 3 && token != NULL)
	    {
	      sprintf (admin_err_msg,
		       "Error while loading ip info file(%s) line(%d)",
		       filename, ln);
	      goto error;
	    }
	}
      ip_info->address_list[address_index] = i;
      ip_info->last_access_time[ip_info->num_list] = 0;
      ip_info->num_list++;
    }

  fclose (fd_ip_list);
  return 0;

error:
  fclose (fd_ip_list);
  return -1;
}

int
access_control_check_right (T_SHM_APPL_SERVER * shm_as_p,
			    char *dbname, char *dbuser,
			    unsigned char *address)
{
  if (access_info_changed != shm_as_p->acl_chn)
    {
#if defined (WINDOWS)
      char acl_sem_name[BROKER_NAME_LEN];

      MAKE_ACL_SEM_NAME (acl_sem_name, shm_as_p->broker_name);
      uw_sem_wait (acl_sem_name);
#else
      uw_sem_wait (&shm_as_p->acl_sem);
#endif
      memcpy (access_info, shm_as_p->access_info, sizeof (access_info));
      num_access_info = shm_as_p->num_access_info;
      access_info_changed = shm_as_p->acl_chn;
#if defined (WINDOWS)
      uw_sem_post (acl_sem_name);
#else
      uw_sem_post (&shm_as_p->acl_sem);
#endif
    }

  return (access_control_check_right_internal
	  (shm_as_p, dbname, dbuser, address));
}

static int
access_control_check_right_internal (T_SHM_APPL_SERVER * shm_as_p,
				     char *dbname, char *dbuser,
				     unsigned char *address)
{
  int i;
  char *address_ptr;
  int ret_val = -1;

  if (address[0] == 127 && address[1] == 0 &&
      address[2] == 0 && address[3] == 1)
    {
      return 0;
    }

  address_ptr = strchr (dbname, '@');
  if (address_ptr != NULL)
    {
      *address_ptr = '\0';
    }

  for (i = 0; i < num_access_info; i++)
    {
      if ((strcmp (access_info[i].dbname, "*") == 0
	   || strncasecmp (access_info[i].dbname, dbname,
			   ACL_MAX_DBNAME_LENGTH) == 0)
	  && (strcmp (access_info[i].dbuser, "*") == 0
	      || strncasecmp (access_info[i].dbuser, dbuser,
			      ACL_MAX_DBUSER_LENGTH) == 0))
	{
	  if (access_control_check_ip
	      (shm_as_p, &access_info[i].ip_info, address, i) == 0)
	    {
	      ret_val = 0;
	      break;
	    }
	}
    }

  if (address_ptr != NULL)
    {
      *address_ptr = '@';
    }

  return ret_val;
}

static int
access_control_check_ip (T_SHM_APPL_SERVER * shm_as_p, IP_INFO * ip_info,
			 unsigned char *address, int info_index)
{
  int i;

  assert (ip_info && address);

  if (address[0] == 127 && address[1] == 0 &&
      address[2] == 0 && address[3] == 1)
    {
      return 0;
    }

  for (i = 0; i < ip_info->num_list; i++)
    {
      int address_index = i * IP_BYTE_COUNT;

      if (ip_info->address_list[address_index] == 0)
	{
	  record_ip_access_time (shm_as_p, info_index, i);
	  return 0;
	}
      else if (memcmp ((void *) &ip_info->address_list[address_index + 1],
		       (void *) address,
		       ip_info->address_list[address_index]) == 0)
	{
	  record_ip_access_time (shm_as_p, info_index, i);
	  return 0;
	}
    }

  return -1;
}

static int
record_ip_access_time (T_SHM_APPL_SERVER * shm_as_p, int info_index,
		       int list_index)
{
  if (access_info_changed != shm_as_p->acl_chn)
    {
      return -1;
    }
#if defined (WINDOWS)
  char acl_sem_name[BROKER_NAME_LEN];

  MAKE_ACL_SEM_NAME (acl_sem_name, shm_as_p->broker_name);
  uw_sem_wait (acl_sem_name);
#else
  uw_sem_wait (&shm_as_p->acl_sem);
#endif
  shm_as_p->access_info[info_index].ip_info.last_access_time[list_index] =
    time (NULL);
#if defined (WINDOWS)
  uw_sem_post (acl_sem_name);
#else
  uw_sem_post (&shm_as_p->acl_sem);
#endif

  return 0;
}
