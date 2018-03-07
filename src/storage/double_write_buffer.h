#ifndef _DWB_H_
#define _DWB_H_

#ident "$Id$"

#include "file_io.h"

/* The double write slot type */
typedef struct double_write_slot DWB_SLOT;
struct double_write_slot
{
  FILEIO_PAGE *io_page;		/* The contained page or NULL. */
  VPID vpid;			/* The page identifier. */
  LOG_LSA lsa;			/* The page LSA */
  unsigned int position_in_block;	/* The position in block. */
  unsigned int block_no;	/* The number of the block where the slot reside. */
  volatile int checksum_status;	/* The checksum status. */
};

/* double write buffer interface */
extern bool dwb_is_created (void);
extern int dwb_create (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p);
extern int dwb_recreate (THREAD_ENTRY * thread_p);
extern int dwb_load_and_recover_pages (THREAD_ENTRY * thread_p, const char *dwb_path_p, const char *db_name_p);
extern int dwb_destroy (THREAD_ENTRY * thread_p);
extern char *dwb_get_volume_name (void);
extern int dwb_flush_next_block (THREAD_ENTRY * thread_p);
extern int dwb_flush_force (THREAD_ENTRY * thread_p, bool * all_sync);
extern int dwb_compute_checksums (THREAD_ENTRY * thread_p);
extern int dwb_flush_block_helper (THREAD_ENTRY * thread_p);
extern int dwb_read_page (THREAD_ENTRY * thread_p, const VPID * vpid, void *io_page, bool * success);
extern int dwb_set_data_on_next_slot (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, bool can_wait,
				      DWB_SLOT ** p_dwb_slot);
extern int dwb_add_page (THREAD_ENTRY * thread_p, FILEIO_PAGE * io_page_p, VPID * vpid, DWB_SLOT ** p_dwb_slot);

#if defined (SERVER_MODE)
extern void dwb_daemons_init ();
extern void dwb_daemons_destroy ();

#endif	/* SERVER_MODE */
#endif	/* _DWB_H_ */	      /* _DWB_H_ */
