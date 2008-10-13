/* This group of functions does the character class compression.
   It goes over the dfa and relabels the arcs with the partitions
   of characters in the NFA.  The partitions are stored in the
   array class.

   Will Cohen
   9/3/90
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

int	class_no = CHAR_RANGE;	/* number of classes for labels */
int	first_el[CHAR_RANGE];	/* first element in each class partition */
set	class[CHAR_RANGE];	/* array holds partitions from class */
				/* compression */

/* goes through labels on NFA graph and partitions the characters into
 * character classes.  This reduces the amount of space required for each
 * dfa node, since only one arc is required each class instead of one arc
 * for each character
 * level:
 * 0 no compression done
 * 1 remove unused characters from classes
 * 2 compress equivalent characters into same class
 *
 * returns the number of character classes required
 */

void partition(nfa_node *, int);
void label_with_classes(nfa_node *start);
void intersect_nfa_labels(nfa_node *start, set *maximal_class);
void r_intersect(nfa_node *start, set * maximal_class);
void label_node(nfa_node *start);

int relabel(start,level)
nfa_node *start;
int level;
{
	if (level){
		set_free(used_classes);	
		partition(start,level);
		label_with_classes(start);
	}else{
		/* classes equivalent to all characters in alphabet */
		class_no = CHAR_RANGE;
	}
	return class_no;
}

/* makes character class sets for new labels */
void partition(start,level)
nfa_node	*start;	/* beginning of nfa graph */
int		level;	/* compression level to uses */
{
	set current_class;
	set unpart_chars;
	set temp;

	unpart_chars = set_dup(used_chars);
#if 0
	/* EOF (-1+1) alway in class 0 */
	class[0] = set_of(0);
	first_el[0] = 0;
	used_classes = set_of(0);
	temp = set_dif(unpart_chars, class[0]);
	set_free(unpart_chars);
	unpart_chars = temp;
	class_no = 1;
#else
	class_no = 0;
#endif
	while (!set_nil(unpart_chars)){
		/* don't look for equivalent labels if c <= 1 */
		if (level <= 1){
			current_class = set_of(set_int(unpart_chars));
		}else{
			current_class = set_dup(unpart_chars);
			intersect_nfa_labels(start,&current_class);
		}
		set_orel(class_no,&used_classes);
		first_el[class_no] = set_int(current_class);
		class[class_no] = current_class;
		temp = set_dif(unpart_chars,current_class);
		set_free(unpart_chars);
		unpart_chars = temp;
		++class_no;
	}
#if 0
	/* group all the other unused characters into a class */
	set_orel(class_no,&used_classes);
	first_el[class_no] = set_int(current_class);
	class[class_no] = set_dif(normal_chars,used_chars);
	++class_no;
#endif
}


/* given pointer to beginning of graph and recursively walks it trying
 * to find a maximal partition.  This partion in returned in maximal_class
 */
void intersect_nfa_labels(start,maximal_class)
nfa_node *start;
set *maximal_class;
{
	/* pick a new operation number */
	++operation_no;
	r_intersect(start,maximal_class);	
}

void r_intersect(start,maximal_class)
nfa_node *start;
set * maximal_class;
{
	set temp;

	if(start && start->nfa_set != operation_no)
	{
		start->nfa_set = operation_no;
		temp = set_and(*maximal_class,start->label);
		if (!set_nil(temp))
		{
			set_free(*maximal_class);
			*maximal_class = temp;
		}else{
			set_free(temp);
		}
		r_intersect(start->trans[0],maximal_class);
		r_intersect(start->trans[1],maximal_class);
	}
}


/* puts class labels in place of old character labels */
void label_with_classes(start)
nfa_node *start;
{
	++operation_no;
	label_node(start);
}

void label_node(start)
nfa_node *start;
{
	set new_label;
	register int i;

	/* only do node if it hasn't been done before */
	if (start && start->nfa_set != operation_no){
		start->nfa_set = operation_no;
		new_label = empty;
		for (i = 0; i<class_no; ++i){
			/* if one element of class in old_label,
			   all elements are. */
			if (set_el(first_el[i],start->label))
				set_orel(i,&new_label);
		}
		set_free(start->label);
		start->label = new_label;
		/* do any nodes that can be reached from this one */
		label_node(start->trans[0]);
		label_node(start->trans[1]);
	}
}
