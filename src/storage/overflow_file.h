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
 * overflow_file.h - Overflow file manager (at server)
 */

#ifndef _OVERFLOW_FILE_H_
#define _OVERFLOW_FILE_H_

#ident "$Id$"

#include "config.h"

#include "storage_common.h"
#include "slotted_page.h"
#include "page_buffer.h"
#include "recovery.h"

extern VPID *overflow_insert (THREAD_ENTRY * thread_p, const VFID * ovf_vfid,
			      VPID * ovf_vpid, RECDES * recdes);
extern const VPID *overflow_update (THREAD_ENTRY * thread_p,
				    const VFID * ovf_vfid,
				    const VPID * ovf_vpid, RECDES * recdes);
extern const VPID *overflow_delete (THREAD_ENTRY * thread_p,
				    const VFID * ovf_vfid,
				    const VPID * ovf_vpid);
extern void overflow_flush (THREAD_ENTRY * thread_p, const VPID * ovf_vpid);
extern int overflow_get_length (THREAD_ENTRY * thread_p,
				const VPID * ovf_vpid);
extern SCAN_CODE overflow_get (THREAD_ENTRY * thread_p, const VPID * ovf_vpid,
			       RECDES * recdes);
extern SCAN_CODE overflow_get_nbytes (THREAD_ENTRY * thread_p,
				      const VPID * ovf_vpid, RECDES * recdes,
				      int start_offset, int max_nbytes,
				      int *remaining_length);
extern int overflow_get_capacity (THREAD_ENTRY * thread_p,
				  const VPID * ovf_vpid, int *ovf_length,
				  int *ovf_num_pages, int *ovf_overhead,
				  int *ovf_free_space);
extern int overflow_estimate_npages_needed (THREAD_ENTRY * thread_p,
					    int total_novf_sets,
					    int avg_ovfdata_size);
extern int overflow_dump (THREAD_ENTRY * thread_p, VPID * ovf_vpid);
extern int overflow_rv_newpage_logical_undo (THREAD_ENTRY * thread_p,
					     LOG_RCV * rcv);
extern void overflow_rv_newpage_logical_dump_undo (int length_ignore,
						   void *data);
extern int overflow_rv_newpage_link_undo (THREAD_ENTRY * thread_p,
					  LOG_RCV * rcv);
extern int overflow_rv_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv);
extern void overflow_rv_link_dump (int length_ignore, void *data);

#endif /* _OVERFLOW_FILE_H_ */
