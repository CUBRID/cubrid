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
 * xa.h - contains the public declarations of the XA+ Specification,
 *        Version 2 Snapshot, June 1994.
 *	  derived from X/Open's "The XA+ Specification Version 2"
 */

#ifndef _XA_H_
#define _XA_H_

#ident "$Id$"

/* 
 * Transaction branch identification: XID and NULLXID:
 */
#define XIDDATASIZE    128	/* size in bytes */
#define MAXGTRIDSIZE	64	/* maximum size in bytes of gtrid */
#define MAXBQUALSIZE	64	/* maximum size in bytes of bqual */
typedef struct xid_t XID;
struct xid_t
{
  long formatID;		/* format identifier */
  long gtrid_length;		/* value not to exceed 64 */
  long bqual_length;		/* value not to exceed 64 */
  char data[XIDDATASIZE];
};
/*
 * A value of -1 in formatID means that the XID is null.
 */

/*
 * Declarations of functions by which RMs call TMs:
 */
#ifdef __STDC__
extern int ax_reg (int, XID *, long);
extern int ax_unreg (int, long);
#else /* ifndef __STDC__ */
extern int ax_reg ();
extern int ax_unreg ();
#endif /* ifndef __STDC__ */

/*
 * XA Switch Data Structure
 */
#define RMNAMESZ 32		/* length of resource manager name */
struct xa_switch_t
{
  char name[RMNAMESZ];		/* name of resource manager */
  long flags;			/* resource manager specific options */
  long version;			/* must be 0 */
#ifdef __STDC__
  int (*xa_open_entry) (char *, int, long);	/*xa_open function pointer */
  int (*xa_close_entry) (char *, int, long);	/*xa_close function pointer */
  int (*xa_start_entry) (XID *, int, long);	/*xa_start function pointer */
  int (*xa_end_entry) (XID *, int, long);	/*xa_end function pointer */
  int (*xa_rollback_entry) (XID *, int, long);	/*xa_rollback function pointer */
  int (*xa_prepare_entry) (XID *, int, long);	/*xa_prepare function pointer */
  int (*xa_commit_entry) (XID *, int, long);	/*xa_commit function pointer */
  int (*xa_recover_entry) (XID *, long, int, long);
  /*xa_recover function pointer */
  int (*xa_forget_entry) (XID *, int, long);	/*xa_forget function pointer */
  int (*xa_complete_entry) (int *, int *, int, long);
  /*xa_complete function pointer */
#else				/* ifndef __STDC__ */
  int (*xa_open_entry) ();	/*xa_open function pointer */
  int (*xa_close_entry) ();	/*xa_close function pointer */
  int (*xa_start_entry) ();	/*xa_start function pointer */
  int (*xa_end_entry) ();	/*xa_end function pointer */
  int (*xa_rollback_entry) ();	/*xa_rollback function pointer */
  int (*xa_prepare_entry) ();	/*xa_prepare function pointer */
  int (*xa_commit_entry) ();	/*xa_commit function pointer */
  int (*xa_recover_entry) ();	/*xa_recover function pointer */
  int (*xa_forget_entry) ();	/*xa_forget function pointer */
  int (*xa_complete_entry) ();	/*xa_complete function pointer */
#endif				/* ifndef __STDC__ */
};

/*
 * Flag definition for the RM switch
 */
#define TMNOFLAGS	0x00000000L	/* no resource manager features
					   selected */
#define TMREGISTER	0x00000001L	/* resource manager dynamically
					   registers */
#define TMNOMIGRATE	0x00000002L	/* resource manager does not support
					   association migration */
#define TMUSEASYNC	0x00000004L	/* resource manager supports
					   asynchronous operations */

/*
 * Flag definitions for xa_ and ax_ routines
 */
/* Use TMNOFLAGS, defined above, when not specifying other flags */
#define TMASYNC		0x80000000L	/* perform routine asynchronously */
#define TMONEPHASE	0x40000000L	/* caller is using one-phase commit
					   optimisation */
#define TMFAIL		0x20000000L	/* dissociates caller and marks
					   transaction branch rollback-only */
#define TMNOWAIT	0x10000000L	/* return if blocking condition
					   exists */
#define TMRESUME	0x08000000L	/* caller is resuming association
					   with suspended transaction branch */
#define TMSUCCESS	0x04000000L	/* dissociate caller from transaction
					   branch */
#define TMSUSPEND	0x02000000L	/* caller is suspending, not ending,
					   association */
#define TMSTARTRSCAN	0x01000000L	/* start a recovery scan */
#define TMENDRSCAN	0x00800000L	/* end a recovery scan */
#define TMMULTIPLE	0x00400000L	/* wait for any asynchronous
					   operation */
#define TMJOIN		0x00200000L	/* caller is joining existing
					   transaction branch */
#define TMMIGRATE	0x00100000L	/* caller intends to perform
					   migration */

/* 
 * ax_() return codes (transaction manager reports to resource manager)
 */
#define TM_JOIN		2	/* caller is joining existing transaction
				   branch */
#define TM_RESUME	1	/* caller is resuming association with
				   suspended transaction branch */
#define TM_OK	0		/* normal execution */
#define TMER_TMERR	-1	/* an error occurred in the transaction
				   manager */
#define TMER_INVAL	-2	/* invalid arguments were given */
#define TMER_PROTO	-3	/* routine invoked in an improper context */

/* 
 * xa_() return codes (resource manager reports to transaction manager)
 */
#define XA_RBBASE	100	/* The inclusive lower bound of the
				   rollback codes */
#define XA_RBROLLBACK	XA_RBBASE	/* The rollback was caused by an
					   unspecified reason */
#define XA_RBCOMMFAIL	XA_RBBASE+1	/* The rollback was caused by a
					   communication failure */
#define	XA_RBDEADLOCK	XA_RBBASE+2	/* A deadlock was detected */
#define XA_RBINTEGRITY	XA_RBBASE+3	/* A condition that violates the
					   integrity of the resources was
					   detected */
#define XA_RBOTHER	XA_RBBASE+4	/* The resource manager rolled back the
					   transaction for a reason not on this
					   list */
#define XA_RBPROTO	XA_RBBASE+5	/* A protocal error occurred in the
					   resource manager */
#define	XA_RBTIMEOUT	XA_RBBASE+6	/* A transaction branch took too long */
#define XA_RBTRANSIENT	XA_RBBASE+7	/* May retry the transaction branch */
#define XA_RBEND	XA_RBTRANSIENT	/* The inclusive upper bound of the
					   rollback codes */

#define XA_NOMIGRATE	9	/* resumption must occur where
				   suspension occurred */
#define XA_HEURHAZ	8	/* the transaction branch may have been
				   heuristically completed */
#define XA_HEURCOM	7	/* the transaction branch has been
				   heuristically comitted */
#define XA_HEURRB	6	/* the transaction branch has been
				   heuristically rolled back */
#define XA_HEURMIX	5	/* the transaction branch has been
				   heuristically committed and rolled
				   back */
#define XA_RETRY	4	/* routine returned with no effect
				   and may be re-issued */
#define XA_RDONLY	3	/* the transaction was read-only
				   and has been committed */
#define XA_OK		0	/* normal execution */
#define XAER_ASYNC	-2	/* asynchronous operation already
				   outstanding */
#define XAER_RMERR	-3	/* a resource manager error occurred
				   in the transaction branch */
#define XAER_NOTA	-4	/* the XID is not valid */
#define XAER_INVAL	-5	/* invalid arguments were given */
#define XAER_PROTO	-6	/* routine invoked in an improper
				   context */
#define XAER_RMFAIL	-7	/* resource manager unavailable */
#define XAER_DUPID	-8	/* the XID already exists */
#define XAER_OUTSIDE	-9	/* resource manager doing work */
					/* outside global transaction */

#endif /* _XA_H_ */
