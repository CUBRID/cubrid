/* output.c, output generator for dlg
 *
 * Will Cohen
 * 10/24/92
 */

#include <stdio.h>
#include "dlg.h"
#ifdef MEMCHK
#include "trax.h"
#else
#ifdef __STDC__
#include <stdlib.h>
#else
#include <malloc.h>
#endif /* __STDC__ */
#endif

int operation_no = 0; /* used to mark nodes so that infinite loops avoided */
int dfa_basep[MAX_MODES]; 	/* start of each group of states */
int dfa_class_nop[MAX_MODES];	/* number of elements in each group of states*/

int gen_ansi = FALSE;		/* allows ansi code to be generated */

FILE *input_stream;	/* where to read description from */
FILE *output_stream;	/* where to put the output	  */
FILE *mode_stream;	/* where to put the mode.h stuff */

/* NOTE: This section is MACHINE DEPENDENT */
#define DIF_SIZE 4
#ifdef PC
long typesize[DIF_SIZE]  = { 0x7f, 0x7fff, 0x7fff, 0x7fffffff };
char t0[] = "unsigned char";
char t1[] = "unsigned short";
char t2[] = "unsigned int";
char t3[] = "unsigned long";
char *typevar[DIF_SIZE] = { t0, t1, t2, t3};
#else
long typesize[DIF_SIZE]  = { 0x7f, 0x7fff, 0x7fffffff, 0x7fffffff };
char t0[] = "unsigned char";
char t1[] = "unsigned short";
char t2[] = "unsigned int";
char t3[] = "unsigned long";
char *typevar[DIF_SIZE] = { t0, t1, t2, t3};
#endif

/* generate require header on output */
void p_head()
{
	fprintf(OUT, "/*\n");
	fprintf(OUT, " * D L G tables\n");
	fprintf(OUT, " *\n");
	fprintf(OUT, " * Generated from:");
	fprintf(OUT, " %s", file_str[0]);
	fprintf(OUT, "\n");
	fprintf(OUT, " *\n");
	fprintf(OUT, " * 1989-1992 by  Will Cohen, Terence Parr, and Hank Dietz\n");
	fprintf(OUT, " * Purdue University Electrical Engineering\n");
	fprintf(OUT, " * DLG Version %s\n", version);
	fprintf(OUT, " */\n\n");
	fprintf(OUT, "#include \"%s\"\n\n", mode_file);
	fprintf(OUT,"\n");
}


/* generate code to tie up any loose ends */
void p_tail()
{
	fprintf(OUT, "\n");
	fprintf(OUT, "\n");
	if (comp_level)
		fprintf(OUT, "#define ZZSHIFT(c) (b_class_no[zzauto][1+c])\n");
	else
		fprintf(OUT, "#define ZZSHIFT(c) (1+c)\n");
	fprintf(OUT, "#define MAX_MODE %d\n",mode_counter);
	fprintf(OUT, "#include \"dlgauto.h\"\n");
}


void p_node_table();
void p_dfa_table();
void p_accept_table();
void p_action_table();
void p_base_table();
void p_class_table();
void p_bshift_table();
void p_alternative_table();

/* output the table of DFA for general use */
void p_tables()
{
	char *minsize();

	fprintf(OUT, "#define DfaStates\t%d\n", dfa_allocated);
	fprintf(OUT, "typedef %s DfaState;\n\n", minsize(dfa_allocated));

	p_node_table();
	p_dfa_table();
	p_accept_table();
	p_action_table();
	p_base_table();
	p_class_table();
	if (comp_level)
		p_bshift_table();
	if (interactive)
		p_alternative_table();
}


/* figures out the smallest variable type that will hold the transitions
 */
char *minsize(elements)
int elements;
{
	int i = 0;

	while (elements > typesize[i])
		++i;
	return typevar[i];
		
}


void p_single_node(int i,int classes);
void p_node_table()
{
	register int	i;
	register int	m = 0;

	for(m=0; m<(mode_counter-1); ++m){
		for(i=dfa_basep[m]; i<dfa_basep[m+1]; ++i)
			p_single_node(i,dfa_class_nop[m]);
	}
	for(i=dfa_basep[m]; i<=dfa_allocated; ++i)
		p_single_node(i,dfa_class_nop[m]);
}


void p_single_node(i,classes)
int i,classes;
{
	register int	j;
	register int	trans, items_on_line;

#if 1
	/* extra state (classes+1) for invalid characters */
	fprintf(OUT, "static const DfaState st%d[%d] = {\n  ", (i-1), (classes+1));
#else
	fprintf(OUT, "static const DfaState st%d[%d] = {\n  ", (i-1), classes);
#endif
	items_on_line = MAX_ON_LINE;
	for(j=0; j<classes; ++j){
		trans = DFA(i)->trans[j];
		if (trans == NIL_INDEX)
			trans = dfa_allocated+1;
		/* all of DFA moved down one in array */
		fprintf(OUT, "%d", trans-1);
		fprintf(OUT, ", ");
		if (!(--items_on_line)){
			fprintf(OUT, "\n  ");
			items_on_line = MAX_ON_LINE;
		}
	}
#if 1
	/* put in jump to error state */
	fprintf(OUT, "%d\n};\n\n", dfa_allocated);
#else
	fprintf(OUT, "\n};\n\n");
#endif
}


void p_dfa_table()
{
	register int	i;

	fprintf(OUT, "static const DfaState *dfa[%d] = {\n", dfa_allocated);
	for (i=0; i<(dfa_allocated-1); ++i){
		fprintf(OUT, "\tst%d,\n", i);
	}
	fprintf(OUT, "\tst%d\n", i);
	fprintf(OUT, "};\n\n");
}


void p_accept_table()
{
	register int	i = 1;
	register int	items_on_line = 0;
	int		true_interactive = TRUE;

	/* make sure element for one past (zzerraction) -WEC 12/16/92 */
	fprintf(OUT,"\nstatic const DfaState accepts[%d] = {\n  ",dfa_allocated+1);
	/* don't do anything if no dfa nodes */
	if (i>dfa_allocated) goto skip_accepts;
	while (TRUE){
		int accept;
		set accept_set;
		set nfa_states;
		unsigned int *t, *nfa_i;
		unsigned int *q, *regular_expr;

		accept_set = empty;
		nfa_states = DFA(i)->nfa_states;
		t = nfa_i = set_pdq(nfa_states);
		/* NOTE: picks lowest accept because accepts monotonic	*/
		/*	with respect to nfa node numbers and set_pdq	*/
		/*	returns in that order				*/
		while((*nfa_i != nil) && (!(accept = NFA(*nfa_i)->accept))){
			nfa_i++;
		}

		/* figure out if more than one accept state there */
		if (warn_ambig ){
			set_orel(accept, &accept_set);
			while(*nfa_i != nil){
				set_orel(NFA(*nfa_i)->accept, &accept_set);
				nfa_i++;
			}
			/* remove error action from consideration */
			set_rm(0, accept_set);

			if( set_deg(accept_set)>1){
				fprintf(stderr, "dlg warning: ambigious regular expression ");
				q = regular_expr = set_pdq(accept_set);
				while(*regular_expr != nil){
					fprintf(stderr," %d ", *regular_expr);
					++regular_expr;
				}
				fprintf(stderr, "\n");
				free(q);
			}
		}

		if ((DFA(i)->alternatives) && (accept != 0)){
			true_interactive = FALSE;
		}
		fprintf(OUT, "%d, ", accept);
		if ((++i)>dfa_allocated)
			break;
		if ((++items_on_line)>=MAX_ON_LINE){
			fprintf(OUT,"\n  ");
			items_on_line = 0;
		}
		free(t);
		set_free(accept_set);
	}
	/* make sure element for one past (zzerraction) -WEC 12/16/92 */
skip_accepts:
	fprintf(OUT, "0\n};\n\n");
}


void p_action_table()
{
	register int	i;

	fprintf(OUT, "typedef void (*zzactionfunction)();\n");
	fprintf(OUT, "static /* should be const, except solaris is braindead */ zzactionfunction actions[%d] = {\n", action_no+1);
	fprintf(OUT, "\tzzerraction,\n");
	for (i=1; i<action_no; ++i){
		fprintf(OUT,"\tact%d,\n", i);
		}
	fprintf(OUT, "\tact%d\n", i);
	fprintf(OUT, "};\n\n");
}


void p_shift_table(m)
int m;
{
	register int	i = 0, j;
	register int	items_on_line = 0;

	fprintf(OUT, "static const unsigned char shift%d[%d] = {\n  ", m,
		CHAR_RANGE);
	while (TRUE){
		/* find which partition character i is in */
		for (j=0; j<dfa_class_nop[mode_counter]; ++j){
			if (set_el(i,class[j]))
				break;
			}
		fprintf(OUT,"%d",j);
		if ((++i)>=CHAR_RANGE)
			break;
		fprintf(OUT,", ");
		if ((++items_on_line)>=MAX_ON_LINE){
			fprintf(OUT,"\n  ");
			items_on_line = 0;
			}
		}
	fprintf(OUT, "\n};\n\n");
}


void p_base_table()
{
	register int m;

	fprintf(OUT, "static const int dfa_base[] = {\n");
	for(m=0; m<(mode_counter-1); ++m)
		fprintf(OUT, "\t%d,\n", dfa_basep[m]-1);
	fprintf(OUT, "\t%d\n};\n\n", dfa_basep[m]-1);
}


void p_class_table()
{
	register int m;

/*	fprintf(OUT,"static const int dfa_class_no[] = {\n");
	for(m=0; m<(mode_counter-1); ++m)
		fprintf(OUT,"\t%d,\n", dfa_class_nop[m]);
	fprintf(OUT,"\t%d\n};\n\n", dfa_class_nop[m]); */
}


void p_bshift_table()
{
	register int m;

	fprintf(OUT,"static const unsigned char *b_class_no[] = {\n");
	for(m=0; m<(mode_counter-1); ++m)
		fprintf(OUT, "\tshift%d,\n", m);
	fprintf(OUT, "\tshift%d\n};\n\n", m);
}

void p_alternative_table()
{
	register int i;

	fprintf(OUT, "#define ZZINTERACTIVE\n\n");
	fprintf(OUT, "static const char zzalternatives[DfaStates+1] = {\n");
	for(i=1; i<=dfa_allocated; ++i)
		fprintf(OUT, "\t%d,\n", DFA(i)->alternatives);
	fprintf(OUT, "/* must have 0 for zzalternatives[DfaStates] */\n");
	fprintf(OUT, "\t0\n};\n\n");
}

void p_mode_def(s,m)
char *s;
int m;
{
	fprintf(mode_stream, "#define %s %d\n", s, m);
}


#ifdef DEBUG
/* print out a particular nfa node that is pointed to by p */
void p_nfa_node(p)
nfa_node *p;
{
	 register nfa_node *t;

	if (p != NIL_INDEX){
		printf("NFA state : %d\naccept state : %d\n",
			NFA_NO(p),p->accept);
		if (p->trans[0] != NIL_INDEX){
			printf("trans[0] => %d on ", NFA_NO(p->trans[0]));
			p_set(p->label);
			printf("\n");
			}
		else
			printf("trans[0] => nil\n");
		if (p->trans[1] != NIL_INDEX)
			printf("trans[1] => %d on epsilon\n",
				NFA_NO(p->trans[1]));
		else
			printf("trans[1] => nil\n");
		printf("\n");
		}
}
#endif

#ifdef  DEBUG
/* code to print out special structures when using a debugger */

void p_nfa(p)
nfa_node *p;	/* state number also index into array */
{
/* each node has a marker on it so it only gets printed once */

	operation_no++; /* get new number */
	s_p_nfa(p);
}

void s_p_nfa(p)
nfa_node *p;	/* state number also index into array */
{
	if ((p != NIL_INDEX) && (p->nfa_set != operation_no)){
		/* so it is only printed once */
		p->nfa_set = operation_no;
		p_nfa_node(p);
		s_p_nfa(p->trans[0]);
		s_p_nfa(p->trans[1]);
		}
}

void p_dfa_node(p)
dfa_node *p;
{
	int i;

	if (p != NIL_INDEX){
		printf("DFA state :%d\n",NFA_NO(p));
		if (p->done)
			printf("done\n");
		else
			printf("undone\n");
		printf("from nfa states : ");
		p_set(p->nfa_states);
		printf("\n");
		/* NOTE: trans arcs stored as ints rather than pointer*/
		for (i=0; i<class_no; i++){
			printf("%d ",p->trans[i]);
			}
		printf("\n\n");
		}
}

void p_dfa()
{
/* prints out all the dfa nodes actually allocated */

	int i;

	for (i = 1; i<=dfa_allocated; i++)
		p_dfa_node(NFA(i));
}


/* print out numbers in the set label */
void p_set(label)
set label;
{
	unsigned *t, *e;

	if (set_nil(label)){
		printf("epsilon\n");
	}else{
		t = e = set_pdq(label);
		while(*e != nil){
			printf("%d ", (*e+MIN_CHAR));
			e++;
		}
		printf("\n");
		free(t);
	}
	
}
#endif
