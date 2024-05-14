/*
 *
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

#include "method_query_util.hpp"

#include <algorithm>
#include <cstring>

#include "dbtype.h"

#if !defined(SERVER_MODE)
#include "dbi.h"
#include "object_domain.h"
#include "object_primitive.h"
#endif
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubmethod
{
#define STK_SIZE 100

  void
  str_trim (std::string &str)
  {
    str.erase (0, str.find_first_not_of ("\t\n\r "));
    str.erase (str.find_last_not_of ("\t\n\r ") + 1);
  }

  char *
  get_backslash_escape_string (void)
  {
    if (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES))
      {
	return (char *) "\\";
      }
    else
      {
	return (char *) "\\\\";
      }
  }


#define B_ERROR -1
#define B_TRUE	1
#define B_FALSE 0

  int
  is_korean (unsigned char ch)
  {
    return (ch >= 0xb0 && ch <= 0xc8) || (ch >= 0xa1 && ch <= 0xfe);
  }

  int
  str_eval_like (const unsigned char *tar, const unsigned char *expr, unsigned char escape)
  {
    const int IN_CHECK = 0;
    const int IN_PERCENT = 1;
    const int IN_PERCENT_UNDERSCORE = 2;

    int status = IN_CHECK;
    const unsigned char *tarstack[STK_SIZE], *exprstack[STK_SIZE];
    int stackp = -1;
    int inescape = 0;

    if (escape == 0)
      {
	escape = 2;
      }
    while (1)
      {
	if (status == IN_CHECK)
	  {
	    if (*expr == escape)
	      {
		expr++;
		if (*expr == '%' || *expr == '_')
		  {
		    inescape = 1;
		    continue;
		  }
		else if (*tar
			 && ((!is_korean (*tar) && *tar == *expr)
			     || (is_korean (*tar) && *tar == *expr && * (tar + 1) == * (expr + 1))))
		  {
		    if (is_korean (*tar))
		      {
			tar += 2;
		      }
		    else
		      {
			tar++;
		      }
		    if (is_korean (*expr))
		      {
			expr += 2;
		      }
		    else
		      {
			expr++;
		      }
		    continue;
		  }
	      }

	    if (inescape)
	      {
		if (*tar == *expr)
		  {
		    tar++;
		    expr++;
		  }
		else
		  {
		    if (stackp >= 0 && stackp < STK_SIZE)
		      {
			tar = tarstack[stackp];
			if (is_korean (*tar))
			  {
			    tar += 2;
			  }
			else
			  {
			    tar++;
			  }
			expr = exprstack[stackp--];
		      }
		    else
		      {
			return B_FALSE;
		      }
		  }
		inescape = 0;
		continue;
	      }

	    /* goto check */
	    if (*expr == 0)
	      {
		while (*tar == ' ')
		  {
		    tar++;
		  }

		if (*tar == 0)
		  {
		    return B_TRUE;
		  }
		else
		  {
		    if (stackp >= 0 && stackp < STK_SIZE)
		      {
			tar = tarstack[stackp];
			if (is_korean (*tar))
			  {
			    tar += 2;
			  }
			else
			  {
			    tar++;
			  }
			expr = exprstack[stackp--];
		      }
		    else
		      {
			return B_FALSE;
		      }
		  }
	      }
	    else if (*expr == '%')
	      {
		status = IN_PERCENT;
		while (* (expr + 1) == '%')
		  {
		    expr++;
		  }
	      }
	    else if ((*expr == '_') || (!is_korean (*tar) && *tar == *expr)
		     || (is_korean (*tar) && *tar == *expr && * (tar + 1) == * (expr + 1)))
	      {
		if (is_korean (*tar))
		  {
		    tar += 2;
		  }
		else
		  {
		    tar++;
		  }
		if (is_korean (*expr))
		  {
		    expr += 2;
		  }
		else
		  {
		    expr++;
		  }
	      }
	    else if (stackp >= 0 && stackp < STK_SIZE)
	      {
		tar = tarstack[stackp];
		if (is_korean (*tar))
		  {
		    tar += 2;
		  }
		else
		  {
		    tar++;
		  }

		expr = exprstack[stackp--];
	      }
	    else if (stackp >= STK_SIZE)
	      {
		return B_ERROR;
	      }
	    else
	      {
		return B_FALSE;
	      }
	  }
	else if (status == IN_PERCENT)
	  {
	    if (* (expr + 1) == '_')
	      {
		if (stackp >= STK_SIZE - 1)
		  {
		    return B_ERROR;
		  }
		tarstack[++stackp] = tar;
		exprstack[stackp] = expr;
		expr++;

		inescape = 0;
		status = IN_PERCENT_UNDERSCORE;
		continue;
	      }

	    if (* (expr + 1) == escape)
	      {
		expr++;
		inescape = 1;
		if (* (expr + 1) != '%' && * (expr + 1) != '_')
		  {
		    return B_ERROR;
		  }
	      }

	    while (*tar && *tar != * (expr + 1))
	      {
		if (is_korean (*tar))
		  {
		    tar += 2;
		  }
		else
		  {
		    tar++;
		  }
	      }

	    if (*tar == * (expr + 1))
	      {
		if (stackp >= STK_SIZE - 1)
		  {
		    return B_ERROR;
		  }
		tarstack[++stackp] = tar;
		exprstack[stackp] = expr;
		if (is_korean (*expr))
		  {
		    expr += 2;
		  }
		else
		  {
		    expr++;
		  }

		inescape = 0;
		status = IN_CHECK;
	      }
	  }
	if (status == IN_PERCENT_UNDERSCORE)
	  {
	    if (*expr == escape)
	      {
		expr++;
		inescape = 1;
		if (*expr != '%' && *expr != '_')
		  {
		    return B_ERROR;
		  }
		continue;
	      }

	    if (inescape)
	      {
		if (*tar == *expr)
		  {
		    tar++;
		    expr++;
		  }
		else
		  {
		    if (stackp >= 0 && stackp < STK_SIZE)
		      {
			tar = tarstack[stackp];
			if (is_korean (*tar))
			  {
			    tar += 2;
			  }
			else
			  {
			    tar++;
			  }
			expr = exprstack[stackp--];
		      }
		    else
		      {
			return B_FALSE;
		      }
		  }
		inescape = 0;
		continue;
	      }

	    /* goto check */
	    if (*expr == 0)
	      {
		while (*tar == ' ')
		  {
		    tar++;
		  }

		if (*tar == 0)
		  {
		    return B_TRUE;
		  }
		else
		  {
		    if (stackp >= 0 && stackp < STK_SIZE)
		      {
			tar = tarstack[stackp];
			if (is_korean (*tar))
			  {
			    tar += 2;
			  }
			else
			  {
			    tar++;
			  }
			expr = exprstack[stackp--];
		      }
		    else
		      {
			return B_FALSE;
		      }
		  }
	      }
	    else if (*expr == '%')
	      {
		status = IN_PERCENT;
		while (* (expr + 1) == '%')
		  {
		    expr++;
		  }
	      }
	    else if ((*expr == '_') || (!is_korean (*tar) && *tar == *expr)
		     || (is_korean (*tar) && *tar == *expr && * (tar + 1) == * (expr + 1)))
	      {
		if (is_korean (*tar))
		  {
		    tar += 2;
		  }
		else
		  {
		    tar++;
		  }
		if (is_korean (*expr))
		  {
		    expr += 2;
		  }
		else
		  {
		    expr++;
		  }
	      }
	    else if (stackp >= 0 && stackp < STK_SIZE)
	      {
		tar = tarstack[stackp];
		if (is_korean (*tar))
		  {
		    tar += 2;
		  }
		else
		  {
		    tar++;
		  }

		expr = exprstack[stackp--];
	      }
	    else if (stackp >= STK_SIZE)
	      {
		return B_ERROR;
	      }
	    else
	      {
		return B_FALSE;
	      }
	  }

	if (*tar == 0)
	  {
	    if (*expr)
	      {
		while (*expr == '%')
		  {
		    expr++;
		  }
	      }

	    if (*expr == 0)
	      {
		return B_TRUE;
	      }
	    else
	      {
		return B_FALSE;
	      }
	  }
      }
  }

  int
  str_like (std::string src, std::string pattern, char esc_char)
  {
    int result;

    std::transform (src.begin(), src.end(), src.begin(), ::tolower);
    std::transform (pattern.begin(), pattern.end(), pattern.begin(), ::tolower);

    result =
	    str_eval_like ((const unsigned char *) src.c_str(), (const unsigned char *) pattern.c_str (), (unsigned char) esc_char);

    return result;
  }

  std::string convert_db_value_to_string (DB_VALUE *value, DB_VALUE *value_string)
  {
    const char *val_str = NULL;
    int err, len;

    DB_TYPE val_type = db_value_type (value);

    if (val_type == DB_TYPE_NCHAR || val_type == DB_TYPE_VARNCHAR)
      {
	err = db_value_coerce (value, value_string, db_type_to_db_domain (DB_TYPE_VARNCHAR));
	if (err >= 0)
	  {
	    val_str = db_get_nchar (value_string, &len);
	  }
      }
    else
      {
	err = db_value_coerce (value, value_string, db_type_to_db_domain (DB_TYPE_VARCHAR));
	if (err >= 0)
	  {
	    val_str = db_get_char (value_string, &len);
	  }
      }

    return std::string (val_str);
  }

#if !defined(SERVER_MODE)
  int
  get_stmt_type (std::string sql)
  {
    char *stmt = sql.data ();
    if (strncasecmp (stmt, "insert", 6) == 0)
      {
	return CUBRID_STMT_INSERT;
      }
    else if (strncasecmp (stmt, "update", 6) == 0)
      {
	return CUBRID_STMT_UPDATE;
      }
    else if (strncasecmp (stmt, "delete", 6) == 0)
      {
	return CUBRID_STMT_DELETE;
      }
    else if (strncasecmp (stmt, "call", 4) == 0)
      {
	return CUBRID_STMT_CALL;
      }
    else if (strncasecmp (stmt, "evaluate", 8) == 0)
      {
	return CUBRID_STMT_EVALUATE;
      }
    else
      {
	return CUBRID_MAX_STMT_TYPE;
      }
  }

  int
  calculate_num_markers (const std::string &sql)
  {
    if (sql.empty())
      {
	return -1;
      }

    int num_markers = 0;
    int sql_len = sql.size ();
    for (int i = 0; i < sql_len; i++)
      {
	if (sql[i] == '?')
	  {
	    num_markers++;
	  }
	else if (sql[i] == '-' && sql[i + 1] == '-')
	  {
	    i = consume_tokens (sql, i + 2, SQL_STYLE_COMMENT);
	  }
	else if (sql[i] == '/' && sql[i + 1] == '*')
	  {
	    i = consume_tokens (sql, i + 2, C_STYLE_COMMENT);
	  }
	else if (sql[i] == '/' && sql[i + 1] == '/')
	  {
	    i = consume_tokens (sql, i + 2, CPP_STYLE_COMMENT);
	  }
	else if (sql[i] == '\'')
	  {
	    i = consume_tokens (sql, i + 1, SINGLE_QUOTED_STRING);
	  }
	else if (/* cas_default_ansi_quotes == false && */ sql[i] == '\"')
	  {
	    i = consume_tokens (sql, i + 1, DOUBLE_QUOTED_STRING);
	  }
      }

    return num_markers;
  }

  int
  consume_tokens (std::string sql, int index, STATEMENT_STATUS stmt_status)
  {
    int sql_len = sql.size ();
    if (stmt_status == SQL_STYLE_COMMENT || stmt_status == CPP_STYLE_COMMENT)
      {
	for (; index < sql_len; index++)
	  {
	    if (sql[index] == '\n')
	      {
		break;
	      }
	  }
      }
    else if (stmt_status == C_STYLE_COMMENT)
      {
	for (; index < sql_len; index++)
	  {
	    if (sql[index] == '*' && sql[index + 1] == '/')
	      {
		index++;
		break;
	      }
	  }
      }
    else if (stmt_status == SINGLE_QUOTED_STRING)
      {
	for (; index < sql_len; index++)
	  {
	    if (sql[index] == '\'' && sql[index + 1] == '\'')
	      {
		index++;
	      }
	    else if (/* cas_default_no_backslash_escapes == false && */ sql[index] == '\\')
	      {
		index++;
	      }
	    else if (sql[index] == '\'')
	      {
		break;
	      }
	  }
      }
    else if (stmt_status == DOUBLE_QUOTED_STRING)
      {
	for (; index < sql_len; index++)
	  {
	    if (sql[index] == '\"' && sql[index + 1] == '\"')
	      {
		index++;
	      }
	    else if (/* cas_default_no_backslash_escapes == false && */ sql[index] == '\\')
	      {
		index++;
	      }
	    else if (sql[index] == '\"')
	      {
		break;
	      }
	  }
      }

    return index;
  }

  std::string
  get_column_default_as_string (DB_ATTRIBUTE *attr)
  {
    int error = NO_ERROR;

    std::string result_default_value_string;
    char *default_value_string = NULL;

    /* Get default value string */
    DB_VALUE *def = db_attribute_default (attr);
    if (def == NULL)
      {
	return "";
      }

    const char *default_value_expr_type_string = NULL, *default_expr_format = NULL;
    const char *default_value_expr_op_string = NULL;

    default_value_expr_type_string = db_default_expression_string (attr->default_value.default_expr.default_expr_type);
    if (default_value_expr_type_string != NULL)
      {
	/* default expression case */
	int len;

	if (attr->default_value.default_expr.default_expr_op != NULL_DEFAULT_EXPRESSION_OPERATOR)
	  {
	    /* We now accept only T_TO_CHAR for attr->default_value.default_expr.default_expr_op */
	    default_value_expr_op_string = "TO_CHAR";	/* FIXME - remove this hard code */
	  }

	default_expr_format = attr->default_value.default_expr.default_expr_format;
	if (default_value_expr_op_string != NULL)
	  {
	    result_default_value_string.assign (default_value_expr_op_string);
	    result_default_value_string.append ("(");
	    result_default_value_string.append (default_value_expr_type_string);
	    if (default_expr_format)
	      {
		result_default_value_string.append (", \'");
		result_default_value_string.append (default_expr_format);
		result_default_value_string.append ("\'");
	      }
	    result_default_value_string.append (")");
	  }
	else
	  {
	    result_default_value_string.assign (default_value_expr_type_string);
	  }

	return result_default_value_string;
      }

    if (db_value_is_null (def))
      {
	return "NULL";
      }

    /* default value case */
    switch (db_value_type (def))
      {
      case DB_TYPE_UNKNOWN:
	break;
      case DB_TYPE_SET:
      case DB_TYPE_MULTISET:
      case DB_TYPE_SEQUENCE:	/* DB_TYPE_LIST */
	serialize_collection_as_string (def, result_default_value_string);
	break;

      case DB_TYPE_CHAR:
      case DB_TYPE_NCHAR:
      case DB_TYPE_VARCHAR:
      case DB_TYPE_VARNCHAR:
      {
	int def_size = db_get_string_size (def);
	const char *def_str_p = db_get_string (def);
	if (def_str_p)
	  {
	    result_default_value_string.push_back ('\'');
	    result_default_value_string.append (def_str_p);
	    result_default_value_string.push_back ('\'');
	    result_default_value_string.push_back ('\0');
	  }
      }
      break;

      default:
      {
	DB_VALUE tmp_val;
	error = db_value_coerce (def, &tmp_val, db_type_to_db_domain (DB_TYPE_VARCHAR));
	if (error == NO_ERROR)
	  {
	    int def_size = db_get_string_size (&tmp_val);
	    const char *def_str_p = db_get_string (&tmp_val);
	    result_default_value_string.assign (def_str_p);
	  }
	db_value_clear (&tmp_val);
      }
      break;
      }

    return result_default_value_string;
  }

  void
  serialize_collection_as_string (DB_VALUE *col, std::string &out)
  {
    out.clear ();

    if (!TP_IS_SET_TYPE (db_value_type (col)))
      {
	return;
      }

    DB_COLLECTION *db_set = db_get_collection (col);
    int size = db_set_size (db_set);

    /* first compute the size of the result */
    const char *single_value = NULL;
    DB_VALUE value, value_string;

    out.push_back ('{');
    for (int i = 0; i < size; i++)
      {
	if (db_set_get (db_set, i, &value) != NO_ERROR)
	  {
	    out.clear ();
	    return;
	  }

	std::string single_value = convert_db_value_to_string (&value, &value_string);
	out.append (single_value);
	if (i != size - 1)
	  {
	    out.append (", ");
	  }

	db_value_clear (&value_string);
	db_value_clear (&value);
      }
    out.push_back ('}');
  }

  char
  get_set_domain (DB_DOMAIN *set_domain, int &precision, short &scale, char &charset)
  {
    int set_domain_count = 0;
    int set_type = DB_TYPE_NULL;

    precision = 0;
    scale = 0;
    charset = lang_charset ();

    DB_DOMAIN *ele_domain = db_domain_set (set_domain);
    for (; ele_domain; ele_domain = db_domain_next (ele_domain))
      {
	set_domain_count++;
	set_type = TP_DOMAIN_TYPE (ele_domain);

	precision = db_domain_precision (ele_domain);
	scale = (short) db_domain_scale (ele_domain);
	charset = db_domain_codeset (ele_domain);
      }

    return (set_domain_count != 1) ? DB_TYPE_NULL : set_type;
  }
#endif
}
