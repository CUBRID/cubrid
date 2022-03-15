/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */


/*
 * scanner_support.h - scanner support functions
 */

#ifndef _SCANNER_SUPPORT_H_
#define _SCANNER_SUPPORT_H_

#ident "$Id$"


#ifdef __cplusplus
extern "C"
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
  extern bool is_parser_hint_node_select;

  extern int pt_nextchar (void);
  extern char *pt_makename (const char *name);
  extern void pt_fix_left_parens (void);

  extern void pt_initialize_hint (PARSER_CONTEXT * parser, PT_HINT hint_table[]);
  extern void pt_check_hint (const char *text, PT_HINT hint_table[], PT_HINT_ENUM * result_hint);
  extern void pt_get_hint (const char *text, PT_HINT hint_table[], PT_NODE * node);
  extern void pt_cleanup_hint (PARSER_CONTEXT * parser, PT_HINT hint_table[]);

  extern void pt_parser_line_col (PT_NODE * node);

  extern bool pt_check_ipv4 (char *p);
  extern bool pt_check_hostname (char *p);

#ifdef __cplusplus
}
#endif

#endif				/* _SCANNER_SUPPORT_H_ */
