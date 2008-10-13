/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * Public defines for set scans
 */

#ifndef _QP_SET_H_
#define _QP_SET_H_

#ident "$Id$"

#include "scan_manager.h"

extern SCAN_CODE qproc_next_set_scan (THREAD_ENTRY * thread_p,
				      SCAN_ID * s_id);

#endif /* _QP_SET_H_ */
