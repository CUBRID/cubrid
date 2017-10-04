#ifndef _DWB_H_
#define _DWB_H_

#ident "$Id$"

#include "file_io.h"

/* The double write slot type */
typedef struct dwb_slot DWB_SLOT;
struct dwb_slot
{
  FILEIO_PAGE *io_page;		/* the contained page or null */
  VPID vpid;			/* the page identifier */
  LOG_LSA lsa;			/* the page LSA */
  unsigned int position_in_block;	/* position in block */
  unsigned int block_no;	/* the position in block */
  volatile int checksum_status;	/* checksum status */
};

/* double write buffer interface */
extern bool dwb_is_created (THREAD_ENTRY * thread_p);
extern int dwb_create (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p);
extern int dwb_load_and_recover_pages (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p);
extern int dwb_destroy (THREAD_ENTRY * thread_p);
extern char *dwb_get_volume_name ();
extern int dwb_flush_block_with_checksum (THREAD_ENTRY * thread_p);
extern int dwb_flush_force (THREAD_ENTRY * thread_p, bool * all_sync);
extern int dwb_compute_checksums (THREAD_ENTRY * thread_p);
extern int dwb_read_page (THREAD_ENTRY * thread_p, const VPID * vpid, void *io_page, bool * success);
extern int dwb_set_data_on_next_slot (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, bool can_wait,
					    DWB_SLOT ** dwb_slot);
extern int dwb_add_page (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, VPID * vpid,
			       DWB_SLOT * p_dwb_slot);



#endif _DWB_H_	/* _DWB_H_ */