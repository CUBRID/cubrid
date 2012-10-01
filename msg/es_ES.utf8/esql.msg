
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
1 Valor de argumento no entero "%s"\n.
2 Valor de argumento no flotante "%s"\n
3 Faltando argumento %s\n
4 [opciones]
5 Opciones: \n
6 Falta valor de argumento para clave %s\n
7 Opcion desconocida "%s"\n
8 Argumento posicional inesperado "%s"\n

$set 2
1 Redefinicion del cursor "%s"
2 *\tfor declaracion %s\n
3 *\n * Tabla de cursor:\n

$set 3
1 (INTERNAL ERROR in set_class_bit): clase de mal almacenamiento (%d)
2 (INTERNAL ERROR in add_spec_to_decl()): cadena malformado (sin especificador)
3 especificador de clase de almacenamiento no permitido en este contexto.
4 combinacion de clase de almacenamiento ilegal
5 combinacion de tipo ilegal
6 combinacion de modificador ilegal
7 (INTERNAL ERROR in pp_add_type_spec): caso inesperado (%d)
8 (INTERNAL ERROR in pp_add_type_adj): caso inesperado (%d)
9 declaracion %s formada impropiamente 

$set 4
1 %s
2 %s: No es un directorio\n
3 %s: Raiz de instalacion mala?\n

$set 5
1 ?
2 por favor verifique la corectitud del token
3 UTILIZAR parte no permitido para cursor statico "%s"
4 cursor "%s" no es definido
5 puntero hacia un descriptor
6 puntero hacia un DB_OBJECT
7 string de caracteres
8 variable de indicador para "%s" no permitida en este contexto
9 "%s" no declarado en seccion BEGIN DECLARE
52 %s (texto era "%s")
53 Error lexical
54 error de sintaxis a "%s": faltando %s en %s
55 error de sintaxis a "%s": faltando %s

$set 6
1 suprimir-directiva-de-linea
2 archivo-de-salida
3 incluye-archivo
4 abandonar-informacion-de-alcance
5 abandonar-informacion-malloc
6 anunciar-version
7 habilitar-uci-rastro
8 deshabilitar-longitud-de-caracter-variable
9 estilo-varchar2
10 unsafe-null
11 indicador-interno
12 archivo-de-entrada

$set 7
1 suprimir #directivas de linea en archivo de salida, estandar off
2 nombre de archivo de salida, default <input-file>.c o stdout
3 incluye archivo conteniendo uci_ prototypes, estandar "esqlx.h"
4 abandonar alcance informacion de debugging, estandar off
5 abandonar informacion final de debugging malloc, estandar off
6 imprimir informacion de numero de version, estandar off
7 habilitar rastro de funcion UCI, estandar off
8 deshabilitar inicializacion de longitud de la variable de host VARCHAR, estandar off
9 utilizar estilo VARCHAR diferente, estandar off
10 ignorar error -462(indicador null necesitado), estandar off
11 utilizar indicador interno NULL para evitar error -462 (indicador null necesitado), estandar off
12 nombre de archivo de entrada, estandar stdin

$set 8
1 Opcion REPEATED ignorada
2 %s version %s\n

$set 9
1 error interno (yy_pop_mode): pila de modo vacio
2 "%s" no es permitido en este contexto

$set 10
1 error interno en %s: %s\n
2 sobreflujo de tabla

$set 11
1 variable de indicador "%s" tiene que ser un entero tipo short
2 no se puede utilizar una variable de indicador con una estructura
3 "%s" tiene definicion de estructura incompleta
4 "%s" tiene que ser un %s valido
5 %s no es un tipo acceptable para "%s"
6 tipo de variable de host no anticipado: %s
7 (pp_addr_expr): intente buscar direccion de variable NULL
8 "%s" no es un puntero
9 "%s" no apunta a una estructura
10 "%s" no es una estructura
11 %s no tiene campo "%s"
12 "%s" nu puede ser dereferenciado por "%s"

$set 12
1 sin memoria

$set 13
1 matriz de funciones es ilegal, asumando punteros de funcion
2 (pp_the_same_type) clase de enlace desconocida
3 (pp_get_sizeof): sustantivo malo (%d)

$set 14
1 (INTERNAL ERROR in %s): caso imposible
