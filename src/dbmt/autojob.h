/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * autojob.h - 
 */

#ifndef _AUTOJOB_H_
#define _AUTOJOB_H_

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

#endif /* _AUTOJOB_H_ */
