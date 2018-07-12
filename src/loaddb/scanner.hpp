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
 * scanner.hpp - subclass of yyFlexLexer, provides the main scanner function.
 */

#ifndef _SCANNER_HPP_
#define _SCANNER_HPP_

#ident "$Id$"

#if !defined (yyFlexLexerOnce)
#include <FlexLexer.h>
#endif
#include <istream>

#include "grammar.hpp"

namespace cubload
{

  // forward declaration
  class driver;

  class scanner : public yyFlexLexer
  {
    public:
      // Default constructor.
      scanner () : yyFlexLexer ()
      {
      };

      /**
       * Constructor (invokes constructor from parent class)
       * @param arg_yyin input stream used for scanning
       */
      scanner (std::istream *arg_yyin) : yyFlexLexer (arg_yyin)
      {
      };

      virtual ~scanner ()
      {
      };

      /*
       * The main scanner function.
       * See lexer.l file for method declaration
       */
      virtual int yylex (parser::semantic_type *yylval, parser::location_type *yylloc, driver &driver);

      /**
       * Lexer error function
       * @param msg a description of the lexer error.
       */
      void LexerError (const char *msg) override
      {
	//ldr_load_failed_error ();
	//ldr_increment_fails ();
      }
  };
} // namespace cubload

#endif // _SCANNER_HPP_
