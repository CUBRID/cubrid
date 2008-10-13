/*	set.h

	The following is a general-purpose set library originally developed
	by Hank Dietz and enhanced by Terence Parr to allow dynamic sets.
	
	Sets are now structs containing the #words in the set and
	a pointer to the actual set words.

	1987 by Hank Dietz
	
	Modified by:
		Terence Parr
		Purdue University
		October 1989
*/

#ifndef _SET_H_
#define _SET_H_

/* Define usable bits per unsigned int word */
#ifdef PC
#define WORDSIZE 16
#define LogWordSize	4
#else
#define	WORDSIZE 32
#define LogWordSize 5
#endif
#define BytesPerWord	sizeof(unsigned)

#define	SETSIZE(a) ((a).n<<LogWordSize)		/* Maximum items per set */
#define	MODWORD(x) ((x) & (WORDSIZE-1))		/* x % WORDSIZE */
#define	DIVWORD(x) ((x) >> LogWordSize)		/* x / WORDSIZE */
#define	nil	((unsigned) -1)		/* An impossible set member all bits on (big!) */

typedef struct {
			unsigned int n;		/* Number of words in set */
			unsigned int *setword;
		} set;

#define set_init	{0, NULL}
#define set_null	((a).setword==NULL)

#define	NumBytes(x)		(((x)>>3)+1)				/* Num bytes to hold x */
#define	NumWords(x)		(((x)>>LogWordSize)+1)		/* Num words to hold x */


/* M a c r o s */

/* make arg1 a set big enough to hold max elem # of arg2 */
#define set_new(a,max) \
if (((a).setword=(unsigned *)calloc(NumWords(max),BytesPerWord))==NULL) \
        fprintf(stderr, "set_new: Cannot allocate set with max of %d\n", max); \
        (a).n = NumWords(max);

#define set_free(a)									\
	{if ( (a).setword != NULL ) free((a).setword);	\
	(a) = empty;}

set	set_and();		/* returns arg1 intersection arg2 */
unsigned int set_deg();		/* returns degree (element count) of set arg */
set	set_dif();		/* returns set difference, arg1 - arg2 */
int	set_el();		/* returns non-0 if arg1 is an element of arg2 */
int	set_equ();		/* returns non-0 if arg1 equals arg2 */
unsigned set_int();		/* returns an int which is in the set arg */
int	set_nil();		/* returns non-0 if arg1 is nil */
set	set_not();		/* returns not arg (well, sort-of) */
set	set_of();		/* returns singleton set of int arg */
set	set_or();		/* returns arg1 union arg2 */
void set_orin();	/* OR's set arg2 into set arg1 */
char *set_str();	/* formats a string representing set arg */
int	set_sub();		/* returns non-0 if arg1 is a proper subset of arg2 */
set	set_val();		/* converts set_str-format string arg into a set */
void set_ext();		/* resizes arg1 to have arg2 words */
void set_rm();		/* removes elem arg1 from set arg2 */
void set_clr();		/* clears all elems of set arg1 */
set set_dup();		/* return duplicate of set arg1 */
void set_orel();	/* OR elem arg2 into set arg1 */
void set_size();	/* Set minimum set size */
unsigned *set_pdq();
int set_hash();		/* returns integer hash of set */
extern set empty;

#endif
