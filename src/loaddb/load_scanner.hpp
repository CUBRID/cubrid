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
#include <utility>

#include "load_common.hpp"

namespace cubload
{

  class scanner : public yyFlexLexer
  {
    public:
      scanner (semantic_helper &semantic_helper, error_handler &error_handler)
	: yyFlexLexer ()
	, m_semantic_helper (semantic_helper)
	, m_error_handler (error_handler)
	, m_data_format (OBJECT_FORMAT_TEXT)
	, m_format_arg_count (0)
      {
	//
      };

      ~scanner () override = default;

      void set_lineno (int line_offset)
      {
	yylineno = line_offset;
      }

      /* Keep the base class yylex () overload visible; the one below only adds a new overload. */
      using yyFlexLexer::yylex;

      /*
       * The main scanner function.
       * See load_lexer.l file for method declaration
       */
      virtual int yylex (parser::semantic_type *yylval, parser::location_type *yylloc);


      /* The function is defined in load_lexer.l
       * because it requires access to yyFlexLexer member variables such as yy_start.
       */
      void set_ldr_data_format (int data_format, int format_version, int bit_platform, int endian);

      OBJECT_DATA_FORMAT get_ldr_data_format () const
      {
	return m_data_format;
      }

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
      /*
       * Report a binary section, or the "%format" command line in front of it, that cannot be read.
       *
       * The load has to fail in both modes, and the two count a failure differently:
       *   - SA mode reads the object file twice, a syntax check pass and then the load, and goes on to
       *     the load only when the first pass counted no error (ldr_sa_load ()). on_failure_with_line ()
       *     counts a failed row there, not an error, so the load would run and end as a success. The
       *     failure is reported as an error instead, the way a syntax error is.
       *   - On the server on_failure_with_line () fails the session, and it counts one failed row:
       *     on_error_with_line () followed by on_syntax_failure (), as LexerError () does, would count
       *     two for the one failure.
       */
      template<typename... Args>
      void fail_binary_section (MSGCAT_LOADDB_MSG msg_id, Args &&... args)
      {
#if defined (SA_MODE)
	m_error_handler.on_error_with_line (msg_id, std::forward<Args> (args)...);
#else
	m_error_handler.on_failure_with_line (msg_id, std::forward<Args> (args)...);
#endif
      }

      /* fail_binary_section () at the line given, see on_failure_with_line (int lineno, ...) */
      template<typename... Args>
      void fail_binary_section_at (int lineno, MSGCAT_LOADDB_MSG msg_id, Args &&... args)
      {
#if defined (SA_MODE)
	m_error_handler.on_error_with_line (lineno, msg_id, std::forward<Args> (args)...);
#else
	m_error_handler.on_failure_with_line (lineno, msg_id, std::forward<Args> (args)...);
#endif
      }

      /*
       * Copy raw bytes of a binary section out of the scanner input.
       * See load_lexer.l file for method definition
       */
      std::size_t read_binary (char *buf, std::size_t len);

      semantic_helper &m_semantic_helper;
      error_handler &m_error_handler;
      OBJECT_DATA_FORMAT m_data_format;

      /*
       * Number of arguments of the "%format" command line scanned so far, see the FORMAT_ARG rules in
       * load_lexer.l. The command takes exactly FORMAT_ARG_COUNT of them.
       */
      static const int FORMAT_ARG_COUNT = 4;
      int m_format_arg_count;
  };

} // namespace cubload

#endif /* _LOAD_SCANNER_HPP_ */
