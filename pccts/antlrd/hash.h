				/* H a s h  T a b l e  S t u f f */
/*
 * $Revision: 1.3 $
*/

#ifndef _HASH_H_
#define _HASH_H_

#ifndef HashTableSize
#define HashTableSize	1353
/* size was 353 */
#endif
#ifndef StrTableSize
#define StrTableSize	50000
/* was 5000 */
#endif

typedef struct _entry {		/* Minimum hash table entry -- superclass */
			char *str;
			struct _entry *next;
		} Entry;

/* Hash 's' using 'size', place into h (s is modified) */
#define Hash(s,h,size)								\
	{while ( *s != '\0' ) h = (h<<1) + *s++;		\
	h %= size;}

#ifdef __STDC__
Entry	*hash_get(Entry **, char *),
		**newHashTable(void),
		*hash_add(Entry **, char *, Entry *);
#else
Entry *hash_get(), **newHashTable(), *hash_add();
#endif

#endif
