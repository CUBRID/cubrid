/*  This is the parser for the dlg version 2
 *  This is a part of the Purdue Compiler Construction Toolset
 *  8/18/90
 *
 *  Will Cohen
 *
 */

#header	<<
#include <ctype.h>
#include "dlg.h"
#ifdef MEMCHK
#include "trax.h"
#endif
>>

<<
int	action_no = 0;	   /* keep track of actions outputed */
int	nfa_allocated = 0; /* keeps track of number of nfa nodes */
nfa_node **nfa_array = NULL;/* root of binary tree that stores nfa array */
nfa_node nfa_model_node;   /* model to initialize new nodes */
set	used_chars;	   /* used to label trans. arcs */
set	used_classes;	   /* classes or chars used to label trans. arcs */
set	normal_chars;	   /* mask to get rid elements that aren't used
			      in set */
int	flag_paren = FALSE;
int	flag_brace = FALSE;
int	mode_counter = 0;  /* keep track of number of %%names */

>>

#lexaction <<
int	func_action;		/* should actions be turned into functions?*/
int	lex_mode_counter = 0;	/* keeps track of the number of %%names */
>>

#token "[\r\t\ ]+"	<< zzskip(); >>			/* Ignore white */
#token "[\n]"		<< zzline++; zzskip(); >>	/* Track Line # */
#token L_EOF		"\@"
#token PER_PER		"\%\%"
#token NAME_PER_PER	"\%\%[a-zA-Z_][a-zA-Z0-9_]*"
		<< p_mode_def(&zzlextext[2],lex_mode_counter++); >>
#token ACTION		"\<\<"
		<< if (func_action)
			fprintf(OUT,"static void\nact%d()\n{ ", ++action_no);
		   zzmode(ACT); zzskip();
		>>
#token GREAT_GREAT	"\>\>"
#token L_BRACE		"\{"
#token R_BRACE		"\}"
#token L_PAR		"\("
#token R_PAR		"\)"
#token L_BRACK		"\["
#token R_BRACK		"\]"
#token ZERO_MORE	"\*"
#token ONE_MORE		"\+"
#token OR		"\|"
#token RANGE		"\-"
#token NOT		"\~"
#token OCTAL_VALUE "\\0[0-7]*"		
	<< {int t; sscanf(&zzlextext[1],"%o",&t); zzlextext[0] = t;}>>
#token HEX_VALUE   "\\0[Xx][0-9a-fA-F]+"
	<< {int t; sscanf(&zzlextext[3],"%x",&t); zzlextext[0] = t;}>>
#token DEC_VALUE   "\\[1-9][0-9]*"
	<< {int t; sscanf(&zzlextext[1],"%d",&t); zzlextext[0] = t;}>>
#token TAB		"\\t"		<< zzlextext[0] = '\t';>>
#token NL		"\\n"		<< zzlextext[0] = '\n';>>
#token CR		"\\r"		<< zzlextext[0] = '\r';>>
#token BS		"\\b"		<< zzlextext[0] = '\b';>>
/* NOTE: this takes ANYTHING after the \ */
#token LIT		"\\~[tnrb]"	<< zzlextext[0] = zzlextext[1];>>
/* NOTE: this takes ANYTHING that doesn't match the other tokens */
#token REGCHAR		"~[\\]"


grammar		: << p_head(); func_action = FALSE;>> (ACTION)* start_states
			<< func_action = FALSE; p_tables(); p_tail(); >>
			(ACTION)* "@"
		;

start_states	: ( PER_PER do_conversion
		  | NAME_PER_PER do_conversion (NAME_PER_PER do_conversion)*)
		    PER_PER 
		;

do_conversion	: <<new_automaton_mode(); func_action = TRUE;>>
			rule_list
			<<
				dfa_class_nop[mode_counter] =
					relabel($1.l,comp_level);
				if (comp_level)
					p_shift_table(mode_counter);
				dfa_basep[mode_counter] = dfa_allocated+1;
				make_dfa_model_node(dfa_class_nop[mode_counter]);
				nfa_to_dfa($1.l);
				++mode_counter;
		    		func_action = FALSE;
#ifdef HASH_STAT
				fprint_hash_stats(stderr);
#endif
			>>
		;

rule_list	: rule <<$$.l=$1.l; $$.r=$1.r;>>
			(rule
				<<{nfa_node *t1;
				   t1 = new_nfa_node();
				   (t1)->trans[0]=$$.l;
				   (t1)->trans[1]=$1.l;
				   /* all accept nodes "dead ends" */
				   $$.l=t1; $$.r=NULL;
				   }
				>>
			)*
		| /* empty */
			<<$$.l = new_nfa_node(); $$.r = NULL;
			   warning("no regular expressions", zzline);
			>>
		;

rule		: reg_expr ACTION
			<<$$.l=$1.l; $$.r=$1.r; ($1.r)->accept=action_no;>>
		| ACTION
			<<$$.l = NULL; $$.r = NULL;
			  error("no expression for action  ", zzline);
			>>
		;

reg_expr	: and_expr <<$$.l=$1.l; $$.r=$1.r;>>
			(OR and_expr 
				<<{nfa_node *t1, *t2;
				   t1 = new_nfa_node(); t2 = new_nfa_node();
				   (t1)->trans[0]=$$.l;
				   (t1)->trans[1]=$2.l;
				   ($$.r)->trans[1]=t2;
				   ($2.r)->trans[1]=t2;
				   $$.l=t1; $$.r=t2;
				  }
				>>
			)*
		;

and_expr	: repeat_expr <<$$.l=$1.l; $$.r=$1.r;>>
			(repeat_expr <<($$.r)->trans[1]=$1.l; $$.r=$1.r;>>)*
		;

repeat_expr	: expr <<$$.l=$1.l; $$.r=$1.r;>>
			{ ZERO_MORE
			<<{	nfa_node *t1,*t2;
				($$.r)->trans[0] = $$.l;
				t1 = new_nfa_node(); t2 = new_nfa_node();
				t1->trans[0]=$$.l;
				t1->trans[1]=t2;
				($$.r)->trans[1]=t2;
				$$.l=t1;$$.r=t2;
			  }
			>>
			| ONE_MORE
			<<($$.r)->trans[0] = $$.l;>>
			}
		| ZERO_MORE
			<< error("no expression for *", zzline);>>
		| ONE_MORE
			<< error("no expression for +", zzline);>>
		;

expr		: << $$.l = new_nfa_node(); $$.r = new_nfa_node(); >>
		  L_BRACK atom_list R_BRACK
			<<
				($$.l)->trans[0] = $$.r;
				($$.l)->label = set_dup($2.label);
				set_orin(&used_chars,($$.l)->label);
			>>
		| NOT L_BRACK atom_list R_BRACK
			<<
				($$.l)->trans[0] = $$.r;
				($$.l)->label = set_dif(normal_chars,$3.label);
				set_orin(&used_chars,($$.l)->label);
			>>
		| L_PAR reg_expr R_PAR
			<<
				($$.l)->trans[0] = $2.l;
				($2.r)->trans[1] = $$.r;
			>>
		| L_BRACE reg_expr R_BRACE
			<<
				($$.l)->trans[0] = $2.l;
				($$.l)->trans[1] = $$.r;
				($2.r)->trans[1] = $$.r;
			>>
		| atom
			<<
				($$.l)->trans[0] = $$.r;
				($$.l)->label = set_dup($1.label);
				set_orin(&used_chars,($$.l)->label);
			>>
		;

atom_list	: << set_free($$.label); >>
			(near_atom <<set_orin(&($$.label),$1.label);>>)*
		;

near_atom	: << register int i;
		     register int i_prime;
		  >>
		  anychar
			<<$$.letter=$1.letter; $$.label=set_of($1.letter);
			i_prime = $1.letter + MIN_CHAR;
			if (case_insensitive && islower(i_prime))
				set_orel(toupper(i_prime)-MIN_CHAR,
					&($$.label));
			if (case_insensitive && isupper(i_prime))
	 			set_orel(tolower(i_prime)-MIN_CHAR,
					&($$.label));
			>>
			{ RANGE anychar 
				<< if (case_insensitive){
					$$.letter = (islower($$.letter) ?
						toupper($$.letter) : $$.letter);
					$2.letter = (islower($2.letter) ?
						toupper($2.letter) : $2.letter);
				   }
				   /* check to see if range okay */
				   if ($$.letter > $2.letter){
					  error("invalid range  ", zzline);
				   }
				   for (i=$$.letter; i<= $2.letter; ++i){
					set_orel(i,&($$.label));
					i_prime = i+MIN_CHAR;
					if (case_insensitive && islower(i_prime))
						set_orel(toupper(i_prime)-MIN_CHAR,
							&($$.label));
					if (case_insensitive && isupper(i_prime))
		 				set_orel(tolower(i_prime)-MIN_CHAR,
							&($$.label));
					}
				>>
			}
		;

atom		: << register int i_prime;>>
		  anychar
		  <<$$.label = set_of($1.letter);
		    i_prime = $1.letter + MIN_CHAR;
		    if (case_insensitive && islower(i_prime))
			set_orel(toupper(i_prime)-MIN_CHAR,
				&($$.label));
		    if (case_insensitive && isupper(i_prime))
	 		set_orel(tolower(i_prime)-MIN_CHAR,
				&($$.label));
		  >>
		;

anychar		: REGCHAR	<<$$.letter = $1.letter - MIN_CHAR;>>
		| OCTAL_VALUE	<<$$.letter = $1.letter - MIN_CHAR;>>
		| HEX_VALUE	<<$$.letter = $1.letter - MIN_CHAR;>>
		| DEC_VALUE	<<$$.letter = $1.letter - MIN_CHAR;>>
		| TAB		<<$$.letter = $1.letter - MIN_CHAR;>>
		| NL		<<$$.letter = $1.letter - MIN_CHAR;>>
		| CR		<<$$.letter = $1.letter - MIN_CHAR;>>
		| BS		<<$$.letter = $1.letter - MIN_CHAR;>>
		| LIT		<<$$.letter = $1.letter - MIN_CHAR;>>
		/* NOTE: LEX_EOF is ALWAYS shifted to 0 = MIN_CHAR - MIN_CHAR*/
		| L_EOF		<<$$.letter = 0;>>
		;

<</* empty action */>>

#lexclass ACT
#token "@"	<< error("unterminated action", zzline); zzmode(START); >>
#token ACTION "\>\>"
		<< if (func_action) fprintf(OUT,"}\n\n");
		   zzmode(START);
		>>
#token "\>"		<< putc(zzlextext[0], OUT); zzskip(); >>
#token "\\\>"		<< putc('>', OUT); zzskip(); >>
#token "\\"		<< putc('\\', OUT); zzskip(); >>
#token "\n"		<< putc(zzlextext[0], OUT); ++zzline; zzskip(); >>
#token "~[\>\\@\n]+"	<< fprintf(OUT, "%s", &(zzlextext[0])); zzskip(); >>

<<
/* adds a new nfa to the binary tree and returns a pointer to it */
nfa_node *new_nfa_node()
{
	register nfa_node *t;
	static int nfa_size=0;	/* elements nfa_array[] can hold */

	++nfa_allocated;
	if (nfa_size<=nfa_allocated){
		/* need to redo array */
		if (!nfa_array){
			/* need some to do inital allocation */
			nfa_size=nfa_allocated+NFA_MIN;
			nfa_array=(nfa_node **) malloc(sizeof(nfa_node*)*
				nfa_size);
		}else{
			/* need more space */
			nfa_size=2*(nfa_allocated+1);
			nfa_array=(nfa_node **) realloc(nfa_array, 
				sizeof(nfa_node*)*nfa_size);
		}
	}
	/* fill out entry in array */
	t = (nfa_node*) malloc(sizeof(nfa_node));
	nfa_array[nfa_allocated] = t;
	*t = nfa_model_node;
	t->node_no = nfa_allocated;
	return t;
}


/* initialize the model node used to fill in newly made nfa_nodes */
void
make_nfa_model_node()
{
	nfa_model_node.node_no = -1; /* impossible value for real nfa node */
	nfa_model_node.nfa_set = 0;
	nfa_model_node.accept = 0;   /* error state default*/
	nfa_model_node.trans[0] = NULL;
	nfa_model_node.trans[1] = NULL;
	nfa_model_node.label = empty;
}
>>

<<
#ifdef DEBUG

/* print out the pointer value and the node_number */
fprint_dfa_pair(f, p)
FILE *f;
nfa_node *p;
{
	if (p){
		fprintf(f, "%x (%d)", p, p->node_no);
	}else{
		fprintf(f, "(nil)");
	}
}

/* print out interest information on a set */
fprint_set(f,s)
FILE *f;
set s;
{
	unsigned int *x;

	fprintf(f, "n = %d,", s.n);
	if (s.setword){
		fprintf(f, "setword = %x,   ", s.setword);
		/* print out all the elements in the set */
		x = set_pdq(s);
		while (*x!=nil){
			fprintf(f, "%d ", *x);
			++x;
		}
	}else{
		fprintf(f, "setword = (nil)");
	}
}

/* code to be able to dump out the nfas
	return 0 if okay dump
	return 1 if screwed up
 */
int dump_nfas(first_node, last_node)
int first_node;
int last_node;
{
	register int i;
	nfa_node *t;

	for (i=first_node; i<=last_node; ++i){
		t = NFA(i);
		if (!t) break;
		fprintf(stderr, "nfa_node %d {\n", t->node_no);
		fprintf(stderr, "\n\tnfa_set = %d\n", t->nfa_set);
		fprintf(stderr, "\taccept\t=\t%d\n", t->accept);
		fprintf(stderr, "\ttrans\t=\t(");
		fprint_dfa_pair(stderr, t->trans[0]);
		fprintf(stderr, ",");
		fprint_dfa_pair(stderr, t->trans[1]);
		fprintf(stderr, ")\n");
		fprintf(stderr, "\tlabel\t=\t{ ");
		fprint_set(stderr, t->label);
		fprintf(stderr, "\t}\n");
		fprintf(stderr, "}\n\n");
	}
	return 0;
}
#endif
>>
