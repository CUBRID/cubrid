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
 * cm_autojob.h - 
 */

#ifndef _CM_AUTOJOB_H_
#define _CM_AUTOJOB_H_

#ident "$Id$"

#include <time.h>

#define AUTOJOB_SIZE 5

/* autobackupdb.conf */
#define AJ_BACKUP_CONF_DBNAME		"dbname"
#define AJ_BACKUP_CONF_BACKUPID		"backupid"
#define AJ_BACKUP_CONF_PATH		"path"
#define AJ_BACKUP_CONF_PERIOD_TYPE	"period_type"
#define AJ_BACKUP_CONF_PERIOD_DATE	"period_date"
#define AJ_BACKUP_CONF_TIME		"time"
#define AJ_BACKUP_CONF_LEVEL		"level"
#define AJ_BACKUP_CONF_ARCHIVEDEL	"archivedel"
#define AJ_BACKUP_CONF_UPDATESTATUS	"updatestatus"
#define AJ_BACKUP_CONF_STOREOLD		"storeold"
#define AJ_BACKUP_CONF_ONOFF		"onoff"

/* automatic job */
typedef struct _ajob
{
  char name[64];
  char config_file[512];	/* config file name */
  time_t last_modi;		/* last modified time of config file */
  int is_on;
  void (*ajob_handler) (void *, time_t, time_t);
  void (*ajob_loader) (struct _ajob *);
  void *hd;			/* handler specific data strudture */
  /* this pointer will be handed to ajob_handler as parameter */
  void *mondata;
} ajob;

void aj_initialize (ajob * ajlist, void *ud);

#endif /* _CM_AUTOJOB_H_ */
