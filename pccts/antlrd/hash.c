/*
 * hash.c
 *
 * Manage hash tables.
 *
 * The following functions are visible:
 *
 *		char	*strdup(char *);		Make space and copy string
 *		Entry 	**newHashTable();		Create and return initialized hash table
 *		Entry	*hash_add(Entry **, char *, Entry *)
 *		Entry	*hash_get(Entry **, char *)
 * $Revision: 1.3 $
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hash.h"
#ifdef MEMCHK
#include "trax.h"
#else
#ifdef __STDC__
void *malloc(unsigned int), *calloc(unsigned int, unsigned int);
#else
char *malloc(), *calloc();
#endif
#endif

#define StrSame		0
#define fatal(err)															\
			{fprintf(stderr, "%s(%d):", __FILE__, __LINE__);				\
			fprintf(stderr, " %s\n", err); exit(-1);}
#define require(expr, err) {if ( !(expr) ) fatal(err);}

static unsigned size = HashTableSize;
static char *strings = NULL;
static char *strp;
static unsigned strsize = StrTableSize;

/* create the hash table and string table for terminals (string table only once) */
Entry **
newHashTable()
{
	Entry **table;
	
	table = (Entry **) calloc(size, sizeof(Entry *));
	require( table != NULL, "cannot allocate hash table");
	if ( strings == NULL )
	{
		strings = (char *) calloc(strsize, sizeof(char));
		require( strings != NULL, "cannot allocate string table");
		strp = strings;
	}
	return table;
}

/* Given a table, add 'rec' with key 'key' (add to front of list). return ptr to entry */
Entry *
hash_add(table,key,rec)
Entry **table;
char *key;
Entry *rec;
{
	unsigned h=0;
	char *p=key;
	extern Entry *Globals;
	require(table!=NULL && key!=NULL && rec!=NULL, "add: invalid addition");
	
	Hash(p,h,size);
	rec->next = table[h];			/* Add to singly-linked list */
	table[h] = rec;
	return rec;
}

/* Return ptr to 1st entry found in table under key (return NULL if none found) */
Entry *
hash_get(table,key)
Entry **table;
char *key;
{
	unsigned h=0;
	char *p=key;
	Entry *q;
	require(table!=NULL && key!=NULL, "get: invalid table and/or key");
	
	Hash(p,h,size);
	for (q = table[h]; q != NULL; q = q->next)
	{
		if ( strcmp(key, q->str) == StrSame ) return( q );
	}
	return( NULL );
}

void
hashStat(table)
Entry **table;
{
	static unsigned short count[20];
	int i,n=0,low=0, hi=0;
	Entry **p;
	float avg=0.0;
	
	for (i=0; i<20; i++) count[i] = 0;
	for (p=table; p<&(table[size]); p++)
	{
		Entry *q = *p;
		int len;
		
		if ( q != NULL && low==0 ) low = p-table;
		len = 0;
		if ( q != NULL ) fprintf(stderr, "[%d]", p-table);
		while ( q != NULL )
		{
			len++;
			n++;
			fprintf(stderr, " %s", q->str);
			q = q->next;
			if ( q == NULL ) fprintf(stderr, "\n");
		}
		count[len]++;
		if ( *p != NULL ) hi = p-table;
	}

	fprintf(stderr, "Storing %d recs used %d hash positions out of %d\n",
					n, size-count[0], size);
	fprintf(stderr, "%f %% utilization\n",
					((float)(size-count[0]))/((float)size));
	for (i=0; i<20; i++)
	{
		if ( count[i] != 0 )
		{
			avg += (((float)(i*count[i]))/((float)n)) * i;
			fprintf(stderr, "Bucket len %d == %d (%f %% of recs)\n",
							i, count[i], ((float)(i*count[i]))/((float)n));
		}
	}
	fprintf(stderr, "Avg bucket length %f\n", avg);
	fprintf(stderr, "Range of hash function: %d..%d\n", low, hi);
}

/* Add a string to the string table and return a pointer to it.
 * Bump the pointer into the string table to next avail position.
 */
#ifdef strdup 
#undef strdup
#endif

char *
strdup(s)
const char *s;
{
	char *start=strp;
	require(s!=NULL, "strdup: NULL string");

	while ( *s != '\0' )
	{
		require( strp <= &(strings[strsize-2]),
				 "string table overflow\nIncrease StrTableSize in hash.h and recompile hash.c\n");
		*strp++ = *s++;
	}
	*strp++ = '\0';

	return( start );
}
