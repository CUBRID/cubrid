
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
1 Non-Integer-Argument Wert "%1$s"\n
2 Non-Gleitzahl-Argument value "%1$s"\n
3 Fehlender Argument %1$s\n
4 [options] 
5 Optionen: \n
6 Fehlender Argumentwert für Schlüssel %1$s\n
7 Unbekannte Option "%1$s"\n
8 Unerwartetes Positionsargument "%1$s"\n

$set 2
1 Redefinition of cursor "%1$s"
2  *\t für Anweisung %1$s\n
3  *\n * Cursor-Tabelle:\n

$set 3
1 (INTERNER FEHLER in set_class_bit): ungültige Speicherungsklasse (%1$d)
2 (INTERNER FEHLER in add_spec_to_decl()): ungültige Kette (kein Spezifikationssymbol)
3 Spezifikationssymbol der Speicherungsklasse nicht erlaubt in diesem Kontext 
4 ungültige Speicherungsklassekombination
5 ungültige Kombination der Typen
6 ungültige Modifikator-Kombination
7 (INTERNER FEHLER in pp_add_type_spec): unerwarteter Zustand (%1$d)
8 (INTERNER FEHLER in pp_add_type_adj): unerwarteter Zustand (%1$d)
9 ungültige %1$s Deklaration

$set 4
1 %1$s
2 %1$s: Kein Ordner\n
3 %1$s: Ungültiger Installationspfad?\n

$set 5
1 ?
2 überprüfen Sie bitte die Richtigkeit des Tokens
3 USING-Teil nicht für den statischen Cursor "%1$s" gestattet 
4 Cursor "%1$s" ist nicht definiert worden
5 Zeiger an einen Deskriptor
6 Zeiger an ein DB_OBJECT
7 Zeichen-String
8 Indikator-Variable für "%1$s" nicht erlaubt in diesem Kontext
9 "%1$s" nicht deklariert in der BEGIN DECLARE Sektion
52 %s (Text war "%s")
53 lexikalischer Fehler
54 Syntax-Fehler an "%s": %s fehlt in %s
55 Syntax-Fehler an "%s": %s fehlt

$set 6
1 Suppress-Line-Direktive
2 Ausgabe-Datei
3 Include-Datei
4 dump-scope-info
5 dump-malloc-info
6 announce-Version
7 enable-uci-trace
8 disable-varchar-length
9 varchar2-style
10 unsafe-null
11 interner Indikator
12 Input-Datei

$set 7
1 #line-Direktiven in Ausgabedatei unterdrücken, standardmäßig ausgeschaltet
2 Ausgabe-Date-Name, standardmäßig <input-file>.c oder stdout
3 Include-Datei enthält uci_ Prototypen, standardmäßig "esqlx.h"
4 Scope Debugging-Informationen Anzeigen, standardmäßig ausgeschaltet
5 Malloc Debugging-Information Anzeigen, standardmäßig ausgeschaltet
6 Version Information drucken, standardmäßig ausgeschaltet
7 UCI-Funktion verfolgen, standardmäßig ausgeschaltet
8 deaktivierung der Länge-Initialisierung der VARCHAR Hostvariable, standardmäßig ausgeschaltet
9 ein anderer VARCHAR Styl benutzen, standardmäßig ausgeschaltet
10 -462(Null-Indicator erforderlich) Fehler ignorieren, standardmäßig ausgeschaltet
11 Internes NULL Indicator verwenden, um den Fehler -462(Null-Indicator erforderlich) zu verhindern, standardmäßig ausgeschaltet
12 Input-Datei-Name, standardmäßig stdin

$set 8
1 REPEATED Option wurde ignoriert
2 %s Version %s\n

$set 9
1 (yy_pop_mode) interner Fehler: leerer Modus-Stapel
2 "%1$s" ist nicht erlaubt in diesem Kontext

$set 10
1 interner Fehler in %s: %s\n
2 Tabellenüberlauf

$set 11
1 Indicator-Variable "%1$s" soll ein short int sein
2 Eine Indicator-Variable darf mit einer Struktur nicht verwendet werden 
3 "%1$s" hat inkomplette struct Definition
4 "%s" muss ein valides %s sein
5 %s ist kein erlaubtes Typ für "%s"
6 unerwartets Host-Variablen-Typ: %1$s
7 (pp_addr_expr): Versuch, die Adresse der NULL-Variable zu finden 
8 "%1$s" ist kein Zeiger
9 "%1$s" zeigt an einem struct nicht
10 "%1$s" ist kein struct
11 %s hat keinen Feld "%s"
12 "%s" kann durch "%s" nicht de-referenziert werden

$set 12
1 außer Speicher

$set 13
1 Reihe von Funktionen ist nicht erlaubt, interpretiert wie Funktionszeiger
2 (pp_the_same_type) unbekannte Klasse von Links
3 (pp_get_sizeof): ungültiger noun (%1$d)

$set 14
1 (INTERNER FEHLER in %1$s): unmöglicher Fall
