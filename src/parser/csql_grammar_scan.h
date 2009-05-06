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
            if (parser_instnum_check == 1 && \
                expr && !pt_instnum_compatibility(expr)) { \
                PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                          MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                          "INST_NUM() or ROWNUM", "INST_NUM() or ROWNUM"); \
            } \
            if (parser_groupbynum_check == 1 && \
                expr && !pt_groupbynum_compatibility(expr)) { \
                PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                          MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                          "GROUPBY_NUM()", "GROUPBY_NUM()"); \
            } \
            if (parser_orderbynum_check == 1 && \
                expr && !pt_orderbynum_compatibility(expr)) { \
                PT_ERRORmf2(this_parser, expr, MSGCAT_SET_PARSER_SEMANTIC, \
                          MSGCAT_SEMANTIC_INSTNUM_COMPATIBILITY_ERR, \
                          "ORDERBY_NUM()", "ORDERBY_NUM()"); \
            } \
        } while (0)

  extern int parser_input_host_index;
  extern int parser_output_host_index;
  extern int parser_statement_OK;
  extern PARSER_CONTEXT *this_parser;
  extern PT_HINT parser_hint_table[];

  extern int pt_nextchar (void);
  extern char *pt_makename (const char *name);
  extern void pt_fix_left_parens (void);
  extern void pt_check_hint (const char *text, PT_HINT hint_table[],
			     PT_HINT_ENUM * result_hint,
			     bool prev_is_white_char);
  extern void pt_get_hint (const char *text, PT_HINT hint_table[],
			   PT_NODE * node);

  extern void pt_parser_line_col (PT_NODE * node);

#ifdef __cplusplus
}
#endif

#endif /* _SCANNER_SUPPORT_H_ */
