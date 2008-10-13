/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cci_xa.h - 
 */

#ifndef	_CCI_XA_H_
#define	_CCI_XA_H_

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

#define XIDDATASIZE    128	/* size in bytes */
#define MAXGTRIDSIZE    64	/* maximum size in bytes of gtrid */
#define MAXBQUALSIZE    64	/* maximum size in bytes of bqual */

/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/

struct xid_t
{
  long formatID;		/* format identifier */
  long gtrid_length;		/* value not to exceed 64 */
  long bqual_length;		/* value not to exceed 64 */
  char data[XIDDATASIZE];
};
typedef struct xid_t XID;

/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

extern int cci_xa_prepare (int con_id, XID * xid, T_CCI_ERROR * err_buf);
extern int cci_xa_recover (int con_id, XID * xid, int num_xid,
			   T_CCI_ERROR * err_buf);
extern int cci_xa_end_tran (int con_id, XID * xid, char type,
			    T_CCI_ERROR * err_buf);

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#endif /* _CCI_XA_H_ */
