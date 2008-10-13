#ifndef _DLG_PROTO_H_
#define _DLG_PROTO_H_

#include "dlg.h"

void p_head();
void p_tail();
void p_tables();
void p_shift_table(int m);

void new_automaton_mode();
int relabel(nfa_node *start, int level);

void fatal(char *message, int line_no);
void error(char *message, int line_no);
void warning(char *message, int line_no);

void _set_pdq( set a, register unsigned *q );

void p_mode_def(char *s, int m);

void clear_hash();
#endif
