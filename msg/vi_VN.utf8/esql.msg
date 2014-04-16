
$
$ Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
$
$   This program is free software; you can redistribute it and/or modify 
$   it under the terms of the GNU General Public License as published by 
$   the Free Software Foundation; either version 2 of the License, or
$   (at your option) any later version.
$
$  This program is distributed in the hope that it will be useful, 
$  but WITHOUT ANY WARRANTY; without even the implied warranty of 
$  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
$  GNU General Public License for more details. 
$
$  You should have received a copy of the GNU General Public License 
$  along with this program; if not, write to the Free Software 
$  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
$

$
$ $Id$
$
$ esql.msg
$

$set 1
1 Non integer argument value "%1$s"\n
2 Non floating argument value "%1$s"\n
3 Missing argument %1$s\n
4 [options] 
5 Options: \n
6 Missing argument value for key %1$s\n
7 Unknown option "%1$s"\n
8 Unexpected positional argument "%1$s"\n

$set 2
1 Redefinition of cursor "%1$s"
2  *\tfor statement %1$s\n
3  *\n * Cursor table:\n

$set 3
1 (INTERNAL ERROR in set_class_bit): bad storage class (%1$d)
2 (INTERNAL ERROR in add_spec_to_decl()): malformed chain (no specifier)
3 storage class specifier not allowed in this context
4 illegal storage class combination
5 illegal type combination
6 illegal modifier combination
7 (INTERNAL ERROR in pp_add_type_spec): unexpected case (%1$d)
8 (INTERNAL ERROR in pp_add_type_adj): unexpected case (%1$d)
9 improperly formed %1$s declaration

$set 4
1 %1$s
2 %1$s: Not a directory\n
3 %1$s: Bad installation root?\n

$set 5
1 ?
2 please check correctness of token
3 USING part not permitted for static cursor "%1$s"
4 cursor "%1$s" is undefined
5 pointer to a descriptor
6 pointer to a DB_OBJECT
7 character string
8 indicator variable for "%1$s" not allowed in this context
9 "%1$s" not declared in BEGIN DECLARE section
52 %1$s (text was "%2$s")
53 Lexical error
54 syntax error at "%1$s": missing %2$s in %3$s
55 syntax error at "%1$s": missing %2$s

$set 6
1 suppress-line-directive
2 output-file
3 include-file
4 dump-scope-info
5 dump-malloc-info
6 announce-version
7 enable-uci-trace
8 disable-varchar-length
9 varchar2-style
10 unsafe-null
11 internal-indicator
12 input-file

$set 7
1 suppress #line directives in output file, default off
2 output file name, default <input-file>.c or stdout
3 include file containing uci_ prototypes, default "esqlx.h"
4 dump scope debugging information, default off
5 dump final malloc debugging information, default off
6 print out version number information, default off
7 enable UCI function trace, default off
8 disable length initialization of VARCHAR host variable, default off
9 use different VARCHAR style, default off
10 ignore -462(null indicator needed) error, default off
11 use internal NULL indicator to prevent -462(null indicator needed) error, default off
12 input file name, default stdin

$set 8
1 REPEATED option ignored
2 %1$s version %2$s\n

$set 9
1 (yy_pop_mode) internal error: empty mode stack
2 "%1$s" is not permitted in this context

$set 10
1 internal error in %1$s: %2$s\n
2 table overflow

$set 11
1 indicator variable "%1$s" must be a short int
2 cannot use an indicator variable with a structure
3 "%1$s" has incomplete struct definition
4 "%1$s" must be a valid %2$s
5 %1$s is not an acceptable type for "%2$s"
6 unanticipated host variable type: %1$s
7 (pp_addr_expr): attempt to find address of NULL variable
8 "%1$s" is not a pointer
9 "%1$s" does not point to a struct
10 "%1$s" is not a struct
11 %1$s has no field "%2$s"
12 "%1$s" cannot be dereferenced by "%2$s"

$set 12
1 out of memory

$set 13
1 array of functions is illegal, assuming function pointers
2 (pp_the_same_type) unknown link class
3 (pp_get_sizeof): bad noun (%1$d)

$set 14
1 (INTERNAL ERROR in %1$s): impossible case
