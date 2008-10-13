/* dlg header file
 *
 * Will Cohen
 * 8/18/90
 * $Revision: 1.3 $
 */
#ifndef _DLG_H_
#define _DLG_H_

#include "set.h"

#define TRUE	1
#define FALSE	0

/***** output related stuff *******************/
#define IN	input_stream
#define OUT	output_stream

#define MAX_MODES	50	/* number of %%names allowed */
#define MAX_ON_LINE	10

#define NFA_MIN		64	/* minimum nfa_array size */
#define DFA_MIN		64	/* minimum dfa_array size */


/* these macros allow the size of the character set to be easily changed */
/* NOTE: do NOT change MIN_CHAR since EOF is the lowest char, -1 */
#define MIN_CHAR (-1)	/* lowest possible character possible on input */
#define MAX_CHAR 255	/* highest possible character possible on input */
#define CHAR_RANGE (1+(MAX_CHAR) - (MIN_CHAR))

/* indicates that the not an "array" reference */
#define NIL_INDEX 0

/* size of hash table used to find dfa_states quickly */
#define HASH_SIZE 211

#define nfa_node struct _nfa_node
nfa_node {
	int		node_no;
	int		nfa_set;
	int		accept;	/* what case to use */
	nfa_node	*trans[2];
	set		label;	/* one arc always labelled with epsilon */
};

#define dfa_node struct _dfa_node
dfa_node {
	int		node_no;
	int		dfa_set;
	int		alternatives;	/* used for interactive mode */
					/* are more characters needed */
	int		done;
	set		nfa_states;
	int		trans[1];/* size of transition table depends on
				  * number of classes required for automata.
				  */


};

/******** macros for accessing the NFA and DFA nodes ****/
#define NFA(x)	(nfa_array[x])
#define DFA(x)	(dfa_array[x])
#define DFA_NO(x) ( (x) ? (x)->node_no : NIL_INDEX)
#define NFA_NO(x) ( (x) ? (x)->node_no : NIL_INDEX)

/******** wrapper for memory checking ***/
/*#define malloc(x)	dlg_malloc((x),__FILE__,__LINE__)*/

/*#define calloc(x,y)	dlg_calloc((x),(y),__FILE__,__LINE__)*/

/******** antlr attributes *************/
typedef struct {
	short letter;
	nfa_node *l,*r;
	set label;
	} Attrib;

#define zzcr_attr(attr, token, text) {					\
	(attr)->letter = text[0]; (attr)->l = NULL;			\
	(attr)->r = NULL; (attr)->label = empty;			\
}
#define zzd_attr(a)	set_free((a)->label);

/******************** Variable ******************************/
extern char	program[];	/* tells what program this is */
extern char	version[];	/* tells what version this is */
extern char	*file_str[];	/* file names being used */
extern int	err_found;	/* flag to indicate error occured */
extern int	action_no;	/* last action function printed */
extern int	func_action;	/* should actions be turned into functions?*/
extern set	used_chars;	/* used to label trans. arcs */
extern set	used_classes;	/* classes or chars used to label trans. arcs */
extern int	class_no;	/* number of classes used */
extern set	class[];	/* shows char. in each class */
extern set	normal_chars;	/* mask off unused portion of set */
extern int	comp_level;	/* what compression level to use */
extern int	interactive;	/* interactive scanner (avoid lookahead)*/
extern int	mode_counter;	/* keeps track of the number of %%name */
extern int	dfa_basep[];	/* start of each group of dfa */
extern int	dfa_class_nop[];/* number of transistion arcs in */
				/* each dfa in each mode */
extern int	nfa_allocated;
extern int	dfa_allocated;
extern nfa_node	**nfa_array;	/* start of nfa "array" */
extern dfa_node	**dfa_array;	/* start of dfa "array" */
extern int	operation_no;	/* unique number for each operation */
extern FILE	*input_stream;	/* where description read from */
extern FILE	*output_stream; /* where to put the output */
extern FILE	*mode_stream;	/* where to put the mode output */
extern char	*mode_file;	/* name of file for mode output */
extern int	gen_ansi;	/* produce ansi compatible code */
extern int	case_insensitive;/* ignore case of input spec. */
extern int	warn_ambig;	/* show if regular expressions ambigious */

/******************** Functions ******************************/
#ifdef __STDC__
extern char 	*dlg_malloc(int, char *, int); /* wrapper malloc */
extern char 	*dlg_calloc(int, int, char *, int); /* wrapper calloc */
extern int	reach(unsigned *, register int, unsigned *);
extern set	closure(set *, unsigned *);
extern dfa_node *new_dfa_node(set);
extern nfa_node *new_nfa_node(void);
extern dfa_node *dfastate(set);
extern dfa_node **nfa_to_dfa(nfa_node *);
extern void	internal_error(char *, char *, int);
extern FILE	*read_stream(char *);	/* opens file for reading */
extern FILE	*write_stream(char *);	/* opens file for writing */
extern void	make_nfa_model_node(void);
extern void	make_dfa_model_node(int);
#else
extern char *dlg_malloc();	/* wrapper malloc */
extern char *dlg_calloc();	/* wrapper calloc */
extern int	reach();
extern set	closure();
extern dfa_node *new_dfa_node();
extern nfa_node *new_nfa_node();
extern dfa_node *dfastate();
extern dfa_node **nfa_to_dfa();
extern		internal_error();
extern FILE	*read_stream();		/* opens file for reading */
extern FILE	*write_stream();	/* opens file for writing */
extern void	make_nfa_model_node();
extern void	make_dfa_model_node();
#endif

#endif
