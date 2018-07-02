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
 * loader_scanner.hpp - subclass of yyFlexLexer, provides the main scanner function.
 */

#ifndef _LOADER_SCANNER_HPP_
#define _LOADER_SCANNER_HPP_

#if !defined (yyFlexLexerOnce)
#include <FlexLexer.h>
#endif
#include "loader_grammar.hpp"

namespace cubloader
{
  // forward declaration
  class loader_driver;

  class loader_scanner : public yyFlexLexer
  {
    public:
      loader_scanner (std::istream *arg_yyin) : yyFlexLexer (arg_yyin)
      {
      };

      loader_scanner (const loader_scanner &copy) = delete;
      loader_scanner &operator= (const loader_scanner &other) = delete;

      virtual ~loader_scanner ()
      {
      };

      virtual int yylex (loader_parser::semantic_type *yylval, loader_parser::location_type *yylloc,
			 loader_driver &driver);

      void
      LexerError (const char *msg) override
      {
	ldr_load_failed_error ();
	ldr_increment_fails ();
      }
  };
} // namespace cubloader

#endif // _LOADER_SCANNER_HPP_
