/*
 *	Revision History: $Revision: 1.3 $
 */
#include <string.h>
#include "ustr.h"

void
zzcr_attr (a, token, text)
     Attrib *a;
     long token;
     char *text;
{
  *a = DB_MALLOC (strlen (text) + 1);
  strcpy (*a, text);
}
