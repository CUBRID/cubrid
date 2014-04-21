
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
1 Numero intero non valido "%s"\n
2 Numero decimale non valido (floating) "%s"\n
3 Mancanza argomento %s\n
4 [opzioni] 
5 Opzioni: \n
6 Mancanza valore per chiave %s\n
7 Opzione sconosciuta "%s"\n
8 Posizione argomento sconosciuta "%s"\n

$set 2
1 Cursore ridefinito "%s"
2  *\tper comando %s\n
3  *\n * Tabella Cursore :\n

$set 3
1 (ERRORE INTERNO in set_class_bit): classe storage errata (%d)
2 (ERRORE INTERNO in add_spec_to_decl()): catena errata (nessuna specifica)
3 classe di salvataggio non permessa in questo contesto
4 combinazione di classi illegale per salvataggio
5 combinazione illegale di tipo
6 combinazione illegale di modifica
7 (ERRORE INTERNO in pp_add_type_spec): caso non congruo (%d)
8 (ERRORE INTERNO in pp_add_type_adj): caso non congruo (%d)
9 dichiarazione non propriamente dchiarata %s

$set 4
1 %s
2 %s: Non è una directory\n
3 %s: Root di installazione sbagliata?\n

$set 5
1 ?
2 Prego controlalre la correttezza del token
3 USING non è permesso per un cursore statico "%s"
4 cursore "%s" non definito
5 Puntatore a descrizione
6 Puntatore a DB_OBJECT
7 Strinag di caratteri
8 Indicatore di variabile "%s" non permesso in questo contesto
9 "%s" non dichiarata nella sezione BEGIN DECLARE
52 %s (testo era "%s")
53 Errore lessicale
54 errore di sinatassi in "%s": manca %s in %s
55 errore di sitassi in "%s": manca %s

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
1 Tolto #line directives nel file di output, default off
2 nome file di output, default <input-file>.c o stdout
3 incluso file contenente uci_ prototypes, default "esqlx.h"
4 scrittura informazioni di debug, default off
5 scrittura informazioni di debug malloc, default off
6 scrittura informazioni di version number, default off
7 attiva UCI funzione trace, default off
8 disabilita inizializzazione lunghezza delle variabili VARCHAR host, default off
9 usa differente stile per VARCHAR , default off
10 ignora errore -462(serve null indicator) , default off
11 usa indicatore interno NULL per prevenire errore -462(server null indicator), default off
12 nome input file, default stdin

$set 8
1 opzione REPEATED ignorata
2 %s versione %s\n

$set 9
1 (yy_pop_mode) ERRORE INTERNO: empty mode stack
2 "%s" non è permesso in questo contesto

$set 10
1 ERRORE INTERNO in %s: %s\n
2 overflow nella tabella

$set 11
1 variabili indicator  "%s" devono essere short int
2 non posso usare una variabile indicator con una struttura
3 "%s" ha una definizione di struttura incompleta
4 "%s" deve essere una %s valida
5 %s non è un tipo accettato per "%s"
6 variabile host unanticipated di tipo : %s
7 (pp_addr_expr): attempt to find address of NULL variable
8 "%s" non è un puntatore
9 "%s" non punta ad una struttura
10 "%s" non è una struttura
11 %s non ha il campo "%s"
12 "%s" non può essere dereferenced a "%s"

$set 12
1 out of memory

$set 13
1  array di funzioni non permessa, uso come puntatore a funzione
2 (pp_the_same_type) classe di link sconosciuta
3 (pp_get_sizeof): bad noun (%d)

$set 14
1 (ERRORE INTERNO in %s): caso non possibile
