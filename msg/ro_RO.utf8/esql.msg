
$
$  Copyright 2008 Search Solution Corporation
$  Copyright 2016 CUBRID Corporation
$ 
$   Licensed under the Apache License, Version 2.0 (the "License");
$   you may not use this file except in compliance with the License.
$   You may obtain a copy of the License at
$ 
$       http://www.apache.org/licenses/LICENSE-2.0
$ 
$   Unless required by applicable law or agreed to in writing, software
$   distributed under the License is distributed on an "AS IS" BASIS,
$   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
$   See the License for the specific language governing permissions and
$   limitations under the License.
$ 
$ 
$

$
$ $Id$
$
$ esql.msg
$

$set 1
1 Argument cu valoare ne-întreagă "%1$s"\n
2 Argument cu valoare ne-flotantă "%1$s"\n
3 Argument lipsă %1$s\n
4 [opţiuni] 
5 Opţiuni: \n
6 Valoare absentă a argumentului pentru cheia %1$s\n
7 Opţiune necunoscută "%1$s"\n
8 Argument pozitional neaşteptat "%1$s"\n

$set 2
1 Redefinire pentru cursorul "%1$s"
2  *\tpentru instrucţiunea %1$s\n
3  *\n * Tabela de cursori:\n

$set 3
1 (EROARE INTERNĂ în set_class_bit): clasă de stocare greşită (%1$d)
2 (EROARE INTERNĂ în add_spec_to_decl()): lanţ format greşit (fără specificator)
3 specificator de clasă de stocare nepermis în acest context
4 combinaţie nepermisă de clase de stocare
5 combinaţie nepermisă de tipuri
6 combinaţie nepermisă de modificatori
7 (EROARE INTERNĂ in pp_add_type_spec): caz neprevăzut (%1$d)
8 (EROARE INTERNĂ in pp_add_type_adj): caz neprevăzut (%1$d)
9 formă invalidă de declarare %1$s

$set 4
1 %1$s
2 %1$s: nu este un director\n
3 %1$s: Rădăcină de instalare invalidă?\n

$set 5
1 ?
2 vă rugăm verificaţi corectitudinea simbolului
3 USING nu este permis pentru cursorul static "%1$s"
4 cursorul "%1$s" nu este definit
5 pointer către un descriptor
6 pointer către un DB_OBJECT
7 şir de caractere
8 variabila indicator pentru "%1$s" nu este permisă în acest context
9 "%1$s" nu este declarat în secţiunea BEGIN DECLARE
52 %1$s (textul a fost "%2$s")
53 Eroare lexicală
54 eroare de sintaxă la "%1$s": %2$s absent din %3$s
55 eroare de sintaxă la "%1$s": %2$s absent

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
1 suprimă directivele #line în fişierul de ieşire, dezactivat implicit
2 numele fişierului de ieşire, implicit <input-file>.c sau stdout
3 include fişierul conţinând prototipuri uci_, implicit "esqlx.h"
4 afişează informaţii de debug pentru scop, implicit dezactivat
5 afişează informaţiile finale de debug pentru malloc, implicit dezactivat
6 afişează informaţii despre numărul versiunii, implicit dezactivat
7 activează trace-ul pentru funcţiile UCI, implicit dezactivat
8 dezactivează iniţializarea lungimii pentru variabilele de gazdă de tip VARCHAR, implicit dezactivat
9 foloseşte stil diferit de VARCHAR, implicit dezactivat
10 ignoră eroarea -462(indicator NULL necesar), implicit dezactivat
11 foloseste indicator intern NULL pentru a preveni eroarea -462(indicator NULL necesar), implicit dezactivat
12 numele fişierului de intrare, implicit stdin

$set 8
1 opţiunea REPEATED ignorată
2 %1$s versiunea %2$s\n

$set 9
1 (yy_pop_mode) eroare internă: stivă vidă de moduri
2 "%1$s" nu este permis(ă) în acest context

$set 10
1 eroare internă în %1$s: %2$s\n
2 depăşire de capacitate pentru tabelă

$set 11
1 variabila indicator "%1$s" trebuie să fie un short int
2 nu se poate folosi o variabilă indicator cu o structură
3 "%1$s" are o definiţie de struct incompletă
4 "%1$s" trebuie să fie un %2$s valid
5 %1$s nu este un tip acceptat pentru "%2$s"
6 tip de variabilă de gazdă neprevăzut: %1$s
7 (pp_addr_expr): încercare de a găsi adresa unei variabile NULL
8 "%1$s" nu este un pointer
9 "%1$s" nu referenţiaza un struct
10 "%1$s" nu este un struct
11 %1$s nu conţine un câmp "%2$s"
12 "%1$s" nu poate fi dereferenţiat de către "%2$s"

$set 12
1 memorie epuizată

$set 13
1 vector de funcţii nepermis, se presupun pointeri la funcţii
2 (pp_the_same_type) clasa de legatură necunoscută
3 (pp_get_sizeof): nume invalid (%1$d)

$set 14
1 (EROARE INTERNĂ in %1$s): caz imposibil
