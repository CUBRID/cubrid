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
 * load_scanner.hpp - subclass of yyFlexLexer, provides the main scanner function.
 */

#ifndef _LOAD_SCANNER_HPP_
#define _LOAD_SCANNER_HPP_

#include "load_error_handler.hpp"
#include "load_semantic_helper.hpp"
#include "load_grammar.hpp"
#include "utility.h"

#if !defined (yyFlexLexerOnce)
#include <FlexLexer.h>
#endif
#include <istream>

namespace cubload
{

  class scanner : public yyFlexLexer
  {
    public:
      scanner (semantic_helper &semantic_helper, error_handler &error_handler)
	: yyFlexLexer ()
	, m_semantic_helper (semantic_helper)
	, m_error_handler (error_handler)
      {
	//
      };

      ~scanner () override = default;

      void set_lineno (int line_offset)
      {
	yylineno = line_offset;
      }

      /*
       * The main scanner function.
       * See load_lexer.l file for method declaration
       */
      virtual int yylex (parser::semantic_type *yylval, parser::location_type *yylloc);

      /*
       * Lexer error function
       * @param msg a description of the lexer error.
       */
      void LexerError (const char *msg) override
      {
	/*
	 *  This approach must be done in order not to fail the whole session during any lexer errors
	 *  encountered during parsing of the data file while the --data-check-only argument is enabled.
	 *  This method will fail the session in case the lexer is jammed and the --data-check-only argument is not
	 *  set, but it will not fail the session is --data-check-only is enabled, and it will just report the line
	 *  where the parsing error occured.
	 */
	m_error_handler.on_error_with_line (m_error_handler.get_scanner_lineno (), LOADDB_MSG_LEX_ERROR);
	m_error_handler.on_syntax_failure (true);   // Use scanner line in this case.
      }

    private:
      semantic_helper &m_semantic_helper;
      error_handler &m_error_handler;
  };

} // namespace cubload

#endif /* _LOAD_SCANNER_HPP_ */
