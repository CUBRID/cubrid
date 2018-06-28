/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * loader_scanner.hpp - TODO CBRD-21654
 */

#ifndef _SCANNER_HPP_
#define _SCANNER_HPP_

#if !defined (yyFlexLexerOnce)
#include <FlexLexer.h>
#endif

#include "loader_grammar.hpp"
#include "loader_driver.hpp"

namespace cubloader
{
  class loader_scanner : public yyFlexLexer
  {
    public:
      loader_scanner (std::istream *arg_yyin = 0, std::ostream *arg_yyout = 0)
	: yyFlexLexer (arg_yyin, arg_yyout)
      {
      };

      virtual ~loader_scanner()
      {
      };

      virtual int yylex (loader_parser::semantic_type *yylval, loader_parser::location_type *yylloc, loader_driver &driver);
  };
} // namespace cubloader

#endif // _SCANNER_HPP_
