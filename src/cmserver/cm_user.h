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
 * cm_user.h -
 */

#ifndef _CM_USER_H_
#define _CM_USER_H_

#ident "$Id$"

#define DBMT_USER_NAME_LEN 64

#define MALLOC_USER_INFO(USER_INFO, NUM_USER)				\
    do {								\
      if ((NUM_USER) > 0)	{					\
        if ((NUM_USER) == 1)  {                                   \
          if (USER_INFO != NULL) free(USER_INFO);               \
          USER_INFO = (T_DBMT_USER_INFO *) malloc(sizeof(T_DBMT_USER_INFO));                    \
        } else {                                                        \
          USER_INFO = (T_DBMT_USER_INFO *) realloc(USER_INFO, sizeof(T_DBMT_USER_INFO) * (NUM_USER)); \
        }                                                               \
        memset(&(USER_INFO[NUM_USER - 1]), 0, sizeof(T_DBMT_USER_INFO));        \
      }                                                                 \
    } while(0)

#define MALLOC_USER_DBINFO(DBINFO, NUM_INFO)				\
    do {								\
      if ((NUM_INFO) > 0) {                                             \
        if ((NUM_INFO) == 1) {						\
          if (DBINFO != NULL) free(DBINFO);                     	\
          DBINFO = (T_DBMT_USER_DBINFO *) malloc(sizeof(T_DBMT_USER_DBINFO));				\
        } else {                                                        \
          DBINFO = (T_DBMT_USER_DBINFO *) realloc(DBINFO, sizeof(T_DBMT_USER_DBINFO) * (NUM_INFO)); 	\
        }                                                               \
        memset(&(DBINFO[(NUM_INFO) - 1]), 0, sizeof(T_DBMT_USER_DBINFO));				\
      }                                                                 \
    } while (0)

typedef struct
{
  char dbname[DBMT_USER_NAME_LEN];
  char auth[16];
  char uid[32];
  char passwd[80];
  char broker_address[260];
} T_DBMT_USER_DBINFO;

typedef struct
{
  char user_name[DBMT_USER_NAME_LEN];
  char user_passwd[80];
  int num_dbinfo;
  T_DBMT_USER_DBINFO *dbinfo;
} T_DBMT_USER_INFO;

typedef struct
{
  int num_dbmt_user;
  T_DBMT_USER_INFO *user_info;
} T_DBMT_USER;

int dbmt_user_read (T_DBMT_USER * dbmt_user, char *_dbmt_error);
void dbmt_user_free (T_DBMT_USER * dbmt_user);
int dbmt_user_write_cubrid_pass (T_DBMT_USER * dbmt_user, char *_dbmt_error);
int dbmt_user_write_pass (T_DBMT_USER * dbmt_user, char *_dbmt_error);
void dbmt_user_db_auth_str (T_DBMT_USER_DBINFO * dbinfo, char *buf);
void dbmt_user_set_dbinfo (T_DBMT_USER_DBINFO * dbinfo, const char *dbname,
			   const char *auth, const char *uid,
			   const char *passwd, const char *broker_address);
void dbmt_user_set_userinfo (T_DBMT_USER_INFO * usrinfo, char *user_name,
			     char *user_passwd, int num_dbinfo,
			     T_DBMT_USER_DBINFO * dbinfo);
int dbmt_user_search (T_DBMT_USER_INFO * user_info, const char *dbname);
void dbmt_user_db_delete (T_DBMT_USER * dbmt_user, char *dbname);
int dbmt_user_add_dbinfo (T_DBMT_USER_INFO * usrinfo,
			  T_DBMT_USER_DBINFO * dbinfo);

#endif /* _CM_USER_H_ */
