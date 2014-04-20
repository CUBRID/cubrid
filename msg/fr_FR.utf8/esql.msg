
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
1 Valeur "%1$s" d'argument n'est pas un entier\n
2 Valeur "%1$s" d'argument n'est pas de type float\n
3 Argument %1$s manquant\n
4 [options] 
5 Options: \n
6 Valeur d'argument manquant pour clé %1$s\n
7 Option inconnue "%1$s"\n
8 Argument positionnel inconnu "%1$s"\n

$set 2
1 Redéfinition du curseur "%1$s"
2  *\tpour instruction %1$s\n
3  *\n * Table du curseur:\n

$set 3
1 (ERREUR INTERNE dans set_class_bit): classe de stockage incorrecte (%1$d)
2 (ERREUR INTERNE dans add_spec_to_decl()): chaîne mal formée (pas de spécificateur)
3 spécificateur de classe de stockage est interdit dans ce contexte
4 association incorrecte des classes de stockage
5 association incorrecte des types
6 association incorrecte des modificateurs
7 (ERREUR INTERNE dans pp_add_type_spec): cas inattendu (%1$d)
8 (ERREUR INTERNE dans pp_add_type_adj): cas inattendu (%1$d)
9 déclaration %1$s mal formée

$set 4
1 %1$s
2 %1$s: Pas un répertoire\n
3 %1$s: racine de l'installation incorrecte?\n

$set 5
1 ?
2 s'il vous plaît vérifier l'exactitude de token
3 La partie USING est interdite pour le curseur statique "%1$s"
4 curseur "%1$s" n'est pas défini
5 pointeur vers un descripteur
6 pointeur vers une DB_OBJECT
7 Chaîne de caractères
8 variable indicatrice pour "%1$s" est interdite dans ce contexte
9 "%1$s" n'est pas déclaré dans la section BEGIN DECLARE
52 %1$s (texte était "%2$s")
53 Erreur lexicale
54 Erreur de syntaxe à "%1$s": manquant %2$s dans %3$s
55 Erreur de syntaxe à "%1$s": manquant %2$s

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
1 supprime les directives #line dans le fichier de sortie, désactivé par défaut
2 Nom du fichier de sortie, par défaut <input-file>.c ou stdout
3 inclue le fichier contenant les prototypes uci_, par défaut "esqlx.h"
4 sauvegarde des informations de débogage du but, désactivé par défaut
5 sauvegarde des informations de débogage de malloc finale, désactivé par défaut
6 affiche des informations sur le numéro de version, désactivé par défaut
7 active le suivi de fonction UCI, désactivé par défaut
8 désactive l'initialisation de la longueur de la variable hôte VARCHAR, désactivé par défaut
9 utilise style différent pour VARCHAR, désactivé par défaut
10 ignore l'erreur -462(indicateur nul nécessaire) error, désactivé par défaut
11 utiliser l'indicateur interne NULL pour prévenir l'erreur -462 (indicateur nul nécessaire), désactivé par défaut
12 nom du fichier d'entrée, par défaut stdin

$set 8
1 option REPEATED ignorée
2 %1$s version %2$s\n

$set 9
1 (yy_pop_mode) erreur interne: pile des modes vide
2 "%1$s" est interdit dans ce contexte

$set 10
1 erreur interne dans %1$s: %2$s\n
2 dépassement de table

$set 11
1 variable indicatrice "%1$s" doit être de type short int
2 ne peut pas utiliser une variable indicatrice avec une structure
3 "%1$s" a une définition incomplète pour la structure
4 "%1$s" doit être un(e) %2$s valid(e)
5 %1$s n'est pas un type acceptable pour "%2$s"
6 Type de variable d'hôte inattendu: %1$s
7 (pp_addr_expr): tentative de trouver l'adresse de la variable NULL
8 "%1$s" n'est pas un pointeur
9 "%1$s" ne pointe pas vers une struct
10 "%1$s" n'est pas une struct
11 %1$s n'a pas le champ "%2$s"
12 "%1$s" ne peut pas être déréférencé par "%2$s"

$set 12
1 mémoire épuisée

$set 13
1 tableau des fonctions est interdit, en supposant des pointeurs de fonction
2 (pp_the_same_type) classe de lien inconnu
3 (pp_get_sizeof): nom incorrect (%1$d)

$set 14
1 (ERREUR INTERNE dans %1$s): cas impossible
