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
 * scanner.hpp - TODO
 */

#ifndef _SCANNER_HPP_
#define _SCANNER_HPP_

#if !defined (yyFlexLexerOnce)
#include <FlexLexer.h>
#endif

#include "loader_parser.tab.hpp"

namespace cubloaddb
{

  class scanner : public yyFlexLexer
  {
    public:
      scanner (std::istream &arg_yyin, std::ostream &arg_yyout)
	: yyFlexLexer (arg_yyin, arg_yyout)
      {
      };

      virtual ~scanner()
      {
      };

      virtual int yylex (cubloaddb::loader_yyparser::semantic_type *yylval,
			 cubloaddb::loader_yyparser::location_type *yylloc);
  };
} // namespace cubloaddb

#endif // _SCANNER_HPP_
