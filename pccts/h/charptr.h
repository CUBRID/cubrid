/*
 *	Revision History: $Revision: 1.3 $
 *
 */
#include "ustr.h"

typedef char *Attrib;
#define zzdef0(a)		{*(a)="";}
#define zzd_attr(a)		{if ( *(a)!=NULL ) DB_FREE(*a);}

extern void zzcr_attr (Attrib * a, long token, char *text);
