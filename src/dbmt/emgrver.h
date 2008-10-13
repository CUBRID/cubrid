/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * emgrver.h - 
 */

#ifndef _EMGRVER_H_
#define _EMGRVER_H_

#ident "$Id$"

#define EMGR_CUR_VERSION	EMGR_MAKE_VER(3, 0)
#define EMGR_MAKE_VER(MAJOR, MINOR)	\
	((T_EMGR_VERSION) (((MAJOR) << 8) | (MINOR)))
typedef short T_EMGR_VERSION;

#endif /* _EMGRVER_H_ */
