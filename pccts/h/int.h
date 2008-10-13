/* ANTLR attribute definition -- long integers */
/*
 *	Revision History: $Log: int.h,v $
 *	Revision History: Revision 1.3  2005/10/27 05:39:25  beatrice
 *	Revision History: bug fix. gcc version.
 *	Revision History:
 *	Revision 1.2  1992/07/09  17:42:49  treyes
 *	This is the original pccts version 1.0 of this file.
 *
 */

typedef long Attrib;

#define zzcr_attr(a,tok,t)	*(a) = atoi(t);
