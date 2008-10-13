
/* parser.dlg -- DLG Description of scanner
 *
 * Generated from: dlg_p.g
 *
 * Terence Parr, Will Cohen, and Hank Dietz: 1989-1993
 * Purdue University Electrical Engineering
 * ANTLR Version 1.10a
 */
#include <stdio.h>
#define ANTLR_VERSION	1.10a

#include <ctype.h>
#include "dlg.h"
#ifdef MEMCHK
#include "trax.h"
#endif
#include "antlr.h"
#include "tokens.h"
#include "dlgdef.h"
#include "dlg_proto.h"

LOOKAHEAD
void zzerraction()
{
	(*zzerr)("invalid token");
	zzadvance();
	zzskip();
}
/*
 * D L G tables
 *
 * Generated from: parser.dlg
 *
 * 1989-1992 by  Will Cohen, Terence Parr, and Hank Dietz
 * Purdue University Electrical Engineering
 * DLG Version 1.10a
 */

#include "mode.h"




int	func_action;		/* should actions be turned into functions?*/
int	lex_mode_counter = 0;	/* keeps track of the number of %%names */
static void
act1()
{ 
		NLA = 1;
	}

static void
act2()
{ 
		NLA = 2;
		zzskip();   
	}

static void
act3()
{ 
		NLA = 3;
		zzline++; zzskip();   
	}

static void
act4()
{ 
		NLA = L_EOF;
	}

static void
act5()
{ 
		NLA = PER_PER;
	}

static void
act6()
{ 
		NLA = NAME_PER_PER;
		p_mode_def(&zzlextext[2],lex_mode_counter++);   
	}

static void
act7()
{ 
		NLA = ACTION;
		if (func_action)
		fprintf(OUT,"static void\nact%d()\n{ ", ++action_no);
		zzmode(ACT); zzskip();
	}

static void
act8()
{ 
		NLA = GREAT_GREAT;
	}

static void
act9()
{ 
		NLA = L_BRACE;
	}

static void
act10()
{ 
		NLA = R_BRACE;
	}

static void
act11()
{ 
		NLA = L_PAR;
	}

static void
act12()
{ 
		NLA = R_PAR;
	}

static void
act13()
{ 
		NLA = L_BRACK;
	}

static void
act14()
{ 
		NLA = R_BRACK;
	}

static void
act15()
{ 
		NLA = ZERO_MORE;
	}

static void
act16()
{ 
		NLA = ONE_MORE;
	}

static void
act17()
{ 
		NLA = OR;
	}

static void
act18()
{ 
		NLA = RANGE;
	}

static void
act19()
{ 
		NLA = NOT;
	}

static void
act20()
{ 
		NLA = OCTAL_VALUE;
		{int t; sscanf(&zzlextext[1],"%o",&t); zzlextext[0] = t;}  
	}

static void
act21()
{ 
		NLA = HEX_VALUE;
		{int t; sscanf(&zzlextext[3],"%x",&t); zzlextext[0] = t;}  
	}

static void
act22()
{ 
		NLA = DEC_VALUE;
		{int t; sscanf(&zzlextext[1],"%d",&t); zzlextext[0] = t;}  
	}

static void
act23()
{ 
		NLA = TAB;
		zzlextext[0] = '\t';  
	}

static void
act24()
{ 
		NLA = NL;
		zzlextext[0] = '\n';  
	}

static void
act25()
{ 
		NLA = CR;
		zzlextext[0] = '\r';  
	}

static void
act26()
{ 
		NLA = BS;
		zzlextext[0] = '\b';  
	}

static void
act27()
{ 
		NLA = LIT;
		zzlextext[0] = zzlextext[1];  
	}

static void
act28()
{ 
		NLA = REGCHAR;
	}

unsigned char shift0[257] = {
  0, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  1, 2, 29, 29, 1, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 1, 29, 29, 29, 29, 4, 29, 
  29, 19, 20, 23, 24, 29, 26, 29, 29, 12, 
  13, 13, 13, 13, 13, 13, 13, 14, 14, 29, 
  29, 15, 29, 16, 29, 3, 7, 7, 7, 7, 
  7, 7, 11, 11, 11, 11, 11, 11, 11, 11, 
  11, 11, 11, 11, 11, 11, 11, 11, 11, 5, 
  11, 11, 21, 28, 22, 29, 11, 29, 7, 6, 
  7, 7, 7, 7, 11, 11, 11, 11, 11, 11, 
  11, 9, 11, 11, 11, 10, 11, 8, 11, 11, 
  11, 5, 11, 11, 17, 25, 18, 27, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 
  29, 29, 29, 29, 29, 29, 29
};

static void
act29()
{ 
		NLA = 1;
		error("unterminated action", zzline); zzmode(START);   
	}

static void
act30()
{ 
		NLA = ACTION;
		if (func_action) fprintf(OUT,"}\n\n");
		zzmode(START);
	}

static void
act31()
{ 
		NLA = 29;
		putc(zzlextext[0], OUT); zzskip();   
	}

static void
act32()
{ 
		NLA = 30;
		putc('>', OUT); zzskip();   
	}

static void
act33()
{ 
		NLA = 31;
		putc('\\', OUT); zzskip();   
	}

static void
act34()
{ 
		NLA = 32;
		putc(zzlextext[0], OUT); ++zzline; zzskip();   
	}

static void
act35()
{ 
		NLA = 33;
		fprintf(OUT, "%s", &(zzlextext[0])); zzskip();   
	}

unsigned char shift1[257] = {
  0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 3, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 1, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 2, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
  4, 4, 4, 4, 4, 4, 4
};

#define DfaStates	46
typedef unsigned char DfaState;

static DfaState st0[31] = {
  1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 
  6, 6, 6, 6, 6, 7, 8, 9, 10, 11, 
  12, 13, 14, 15, 16, 17, 18, 19, 20, 6, 
  46
};

static DfaState st1[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st2[31] = {
  46, 21, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st3[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st4[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st5[31] = {
  46, 46, 46, 46, 22, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st6[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st7[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 23, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st8[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 24, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st9[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st10[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st11[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st12[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st13[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st14[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st15[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st16[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st17[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st18[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st19[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st20[31] = {
  46, 25, 25, 25, 25, 25, 26, 25, 27, 28, 
  29, 25, 30, 31, 31, 25, 25, 25, 25, 25, 
  25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 
  46
};

static DfaState st21[31] = {
  46, 21, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st22[31] = {
  46, 46, 46, 46, 46, 32, 32, 32, 32, 32, 
  32, 32, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st23[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st24[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st25[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st26[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st27[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st28[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st29[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st30[31] = {
  46, 46, 46, 46, 46, 33, 46, 46, 46, 46, 
  46, 46, 34, 34, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st31[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 35, 35, 35, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st32[31] = {
  46, 46, 46, 46, 46, 36, 36, 36, 36, 36, 
  36, 36, 36, 36, 36, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st33[31] = {
  46, 46, 46, 46, 46, 46, 37, 37, 46, 46, 
  46, 46, 37, 37, 37, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st34[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 34, 34, 46, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st35[31] = {
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46, 46, 35, 35, 35, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st36[31] = {
  46, 46, 46, 46, 46, 36, 36, 36, 36, 36, 
  36, 36, 36, 36, 36, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st37[31] = {
  46, 46, 46, 46, 46, 46, 37, 37, 46, 46, 
  46, 46, 37, 37, 37, 46, 46, 46, 46, 46, 
  46, 46, 46, 46, 46, 46, 46, 46, 46, 46, 
  46
};

static DfaState st38[6] = {
  39, 40, 41, 42, 43, 46
};

static DfaState st39[6] = {
  46, 46, 46, 46, 46, 46
};

static DfaState st40[6] = {
  46, 44, 46, 46, 46, 46
};

static DfaState st41[6] = {
  46, 45, 46, 46, 46, 46
};

static DfaState st42[6] = {
  46, 46, 46, 46, 46, 46
};

static DfaState st43[6] = {
  46, 46, 46, 46, 43, 46
};

static DfaState st44[6] = {
  46, 46, 46, 46, 46, 46
};

static DfaState st45[6] = {
  46, 46, 46, 46, 46, 46
};


DfaState *dfa[46] = {
	st0,
	st1,
	st2,
	st3,
	st4,
	st5,
	st6,
	st7,
	st8,
	st9,
	st10,
	st11,
	st12,
	st13,
	st14,
	st15,
	st16,
	st17,
	st18,
	st19,
	st20,
	st21,
	st22,
	st23,
	st24,
	st25,
	st26,
	st27,
	st28,
	st29,
	st30,
	st31,
	st32,
	st33,
	st34,
	st35,
	st36,
	st37,
	st38,
	st39,
	st40,
	st41,
	st42,
	st43,
	st44,
	st45
};


DfaState accepts[47] = {
  0, 1, 2, 3, 4, 28, 28, 28, 28, 9, 
  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 
  0, 2, 5, 7, 8, 27, 26, 23, 24, 25, 
  20, 22, 6, 0, 20, 22, 6, 21, 0, 29, 
  31, 33, 34, 35, 30, 32, 0
};

void (*actions[36])() = {
	zzerraction,
	act1,
	act2,
	act3,
	act4,
	act5,
	act6,
	act7,
	act8,
	act9,
	act10,
	act11,
	act12,
	act13,
	act14,
	act15,
	act16,
	act17,
	act18,
	act19,
	act20,
	act21,
	act22,
	act23,
	act24,
	act25,
	act26,
	act27,
	act28,
	act29,
	act30,
	act31,
	act32,
	act33,
	act34,
	act35
};

static int dfa_base[] = {
	0,
	38
};

static int dfa_class_no[] = {
	30,
	5
};

static unsigned char *b_class_no[] = {
	shift0,
	shift1
};



#define ZZSHIFT(c) (b_class_no[zzauto][1+c])
#define MAX_MODE 2
#include "dlgauto.h"
