/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * scanner_support.h - scanner support functions
 */

#ifndef _SCANNER_SUPPORT_H_
#define _SCANNER_SUPPORT_H_

#ident "$Id$"


#ifdef __cplusplus
{
#endif

#define pt_orderbynum_compatibility(expr) pt_instnum_compatibility(expr)

#define PICE(expr) \
        do { \
            if (instnum_check == 1 && \
                expr && !pt_instnum_compatibility(expr)) { \
                PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                          MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                          "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM"); \
            } \
            if (groupbynum_check == 1 && \
                expr && !pt_groupbynum_compatibility(expr)) { \
                PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                          MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                          "GROUPBY_NUM()", "GROUPBY_NUM()"); \
            } \
            if (orderbynum_check == 1 && \
                expr && !pt_orderbynum_compatibility(expr)) { \
                PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                          MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                          "ORDERBY_NUM()", "ORDERBY_NUM()"); \
            } \
        } while (0)

/* For expressions : expr "+" ... */
#define PFOP(OP) \
        do { \
            PT_NODE *expr, *arg1; \
            arg1 = pt_pop(this_parser); \
            expr = parser_new_node(this_parser, PT_EXPR); \
            if (expr) { \
                expr->info.expr.op = OP; \
                expr->info.expr.arg1 = arg1; \
                expr->info.expr.arg2 = expr->info.expr.arg3 = NULL; \
                if (instnum_check == 1 && \
                    !pt_instnum_compatibility(expr)) { \
                    PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                              MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                              "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM"); \
                } \
                if (groupbynum_check == 1 && \
                    !pt_groupbynum_compatibility(expr)) { \
                    PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                              MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                              "GROUPBY_NUM()", "GROUPBY_NUM()"); \
                } \
                if (orderbynum_check == 1 && \
                    !pt_orderbynum_compatibility(expr)) { \
                    PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                              MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                              "ORDERBY_NUM()", "ORDERBY_NUM()"); \
                } \
            } \
            pt_push(this_parser, expr); \
        } while (0)

/* For expressions : expr "+" ..., does the second arg */
#define PSOP \
        do { \
            PT_NODE *expr, *arg2; \
            arg2 = pt_pop(this_parser); \
            expr = pt_pop(this_parser); \
            if (expr) { \
                expr->info.expr.arg2 = arg2; \
                expr->info.expr.arg3 = NULL; \
                if (instnum_check == 1 && \
                    !pt_instnum_compatibility(expr)) { \
                    PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                              MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                              "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM"); \
                } \
                if (groupbynum_check == 1 && \
                    !pt_groupbynum_compatibility(expr)) { \
                    PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                              MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                              "GROUPBY_NUM()", "GROUPBY_NUM()"); \
                } \
                if (orderbynum_check == 1 && \
                    !pt_orderbynum_compatibility(expr)) { \
                    PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                              MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                              "ORDERBY_NUM()", "ORDERBY_NUM()"); \
                } \
            } \
            pt_push(this_parser, expr); \
        } while (0)

/* For expressions : expr "+" ..., does the third arg */
#define PTOP \
        do { \
            PT_NODE *expr, *arg3; \
            arg3 = pt_pop(this_parser); \
            expr = pt_pop(this_parser); \
            if (expr) { \
                expr->info.expr.arg3 = arg3; \
                if (instnum_check == 1 && \
                    !pt_instnum_compatibility(expr)) { \
                    PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                              MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                              "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM"); \
                } \
                if (groupbynum_check == 1 && \
                    !pt_groupbynum_compatibility(expr)) { \
                    PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                              MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                              "GROUPBY_NUM()", "GROUPBY_NUM()"); \
                } \
                if (orderbynum_check == 1 && \
                    !pt_orderbynum_compatibility(expr)) { \
                    PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                              MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                              "ORDERBY_NUM()", "ORDERBY_NUM()"); \
                } \
            } \
            pt_push(this_parser, expr); \
        } while (0)


/* Used to combine top two stack items into a single list */
#define PLIST \
        do { \
            PT_NODE *node, *list; \
            node=pt_pop(this_parser); \
            list=pt_pop(this_parser); \
            parser_append_node(node, list); \
            pt_push(this_parser, list); \
        } while (0)

#define PLISTOR \
        do { \
            PT_NODE *node, *list; \
            node=pt_pop(this_parser); \
            list=pt_pop(this_parser); \
            parser_append_node_or(node, list); \
            pt_push(this_parser, list); \
        } while (0)

  extern int input_host_index;
  extern int output_host_index;
  extern int statement_OK;
  extern int lp_look_state;
  extern PARSER_CONTEXT *this_parser;
  extern PT_HINT hint_table[];

/* defined at pccts/h/dlgauto.h */
  extern long zzbufovfcnt;
  extern long zzcharfull;
  extern char *zznextpos;
  extern long zzclass;
  extern long zzadd_erase;
  extern char zzebuf[];

  extern int pt_nextchar (void);
  extern char *pt_makename (const char *name);
  extern void pt_fix_left_parens (void);
  extern void pt_check_hint (const char *text, PT_HINT hint_table[],
			     PT_HINT_ENUM * result_hint,
			     bool prev_is_white_char);
  extern void pt_get_hint (const char *text, PT_HINT hint_table[],
			   PT_NODE * node);

  extern long zzTRACE (void);

  extern void pt_parser_line_col (PT_NODE * node);
  extern void zzerrstd (const char *s);

#ifdef __cplusplus
}
#endif

#endif /* _SCANNER_SUPPORT_H_ */
