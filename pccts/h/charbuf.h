/* ANTLR attribute definition -- constant width text */
/*
 *	Revision History: $Revision: 1.3 $
 */

#ifndef D_TextSize
#define D_TextSize	30
#endif

typedef struct
{
  char text[D_TextSize];
} Attrib;

#define zzcr_attr(a,tok,t)	strncpy((a)->text, t, D_TextSize-1);
