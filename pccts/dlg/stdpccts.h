/*
 * stdpccts.h -- P C C T S  I n c l u d e
 *
 * Terence Parr, Hank Dietz and Will Cohen: 1989-1993
 * Purdue University Electrical Engineering
 * ANTLR Version 1.10
 * $Revision: 1.3 $
 */
#ifndef _STDPCCTS_H_
#define _STDPCCTS_H_

#include <stdio.h>
#define ANTLR_VERSION	1.10

#include <ctype.h>
#include "dlg.h"
#ifdef MEMCHK
#include "trax.h"
#endif
#define zzEOF_TOKEN 1
#define zzSET_SIZE 8
#include "antlr.h"
#include "tokens.h"
#include "dlgdef.h"
#include "mode.h"
#endif
