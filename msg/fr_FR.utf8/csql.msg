
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
$ csql.msg
$

$set 1 MSGCAT_CSQL_SET_CSQL
40 \
utilitaire SQL interactive, version %1$s\n\
usage: %2$s [OPTION] database-name[@host]\n\
\n\
valid options:\n\
  -S, --SA-mode                exécution en mode autonome\n\
  -C, --CS-mode                exécution en mode client-serveur\n\
  -u, --user=ARG               autre nom d'utilisateur\n\
  -p, --password=ARG           chaîne de mot de passe, donner "" pour aucun\n\
  -e, --error-continue         pas quitter en cas d'erreur d'instruction\n\
  -i, --input-file=ARG         nom du fichier en entrée\n\
  -o, --output-file=ARG        nom du fichier en sortie\n\
  -s, --single-line            exécution ligne par ligne\n\
  -c, --command=ARG            commandes CSQL\n\
  -l, --line-output            afficher chaque valeur dans une ligne\n\
  -r, --read-only              mode lecture seule\n\
      --string-width           afficher chaque colonne qui est un type chaîne de caractères dans cette largeur\n\
      --no-auto-commit         désactiver le mode de validation automatique\n\
      --no-pager               ne pas utiliser pagineur\n\
      --no-single-line         désactiver exécution ligne par ligne\n\
      --no-trigger-action      désactiver action de déclenchement\n\
\n\
Pour plus d'informations, visitez le site http://www.cubrid.org\n
41 %1$s: la valeur d'environnement du %2$s_MODE est incorrecte. \nIl doit être %3$s ou %4$s.\n
42 %1$s: trop d'arguments donnés. maximum est de %2$d.\n
43 %1$s: La variable d'environnement pour le répertoire racine ($CUBRID) doit être initialisée.\n
44 %1$s: la tentative d'exécuter "%2$s" a échoué.\n
45 %1$s: ne pouvez pas utiliser les options -sa et -cs simultanément.\n
46 %1$d commande(s) traitées avec succès.
47 Dans la commande de la ligne %1$d,
48 Dans ligne %1$d, colonne %2$d,
49 Saisissez `;help' pour les messages d'aide.
50 ERREUR: 
51 CUBRID SQL Interpreter
52 Il ya eu des mises à jour. Voulez-vous enregistrer? (`Y' ou `N') 
53 Entrez `Y' pour enregistrer, `N' pour annuler. 
54 Validé(e).
55 Annulé(e).
56 Tampon de commandes a été enregistré.
57 Le fichier a été lu dans le tampon de commande.
58 Mémoire tampon de commande a été envoyée à l'imprimante.
59 Répertoire courant a changé à 
61 Entrez Mot de passe : 
62 <Résultat de %1$s commande dans ligne %2$d>
63 Il n'y a pas de résultats.
64 Point de vérification a été délivré.
65 La base de données a été redémarré.
66 Tran index            Nom d'utilisateur      Nom d'hôte      iD de processus      Nom du programme\n
67 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
68 La transaction spécifiée a été arrêté.
69 Arrêt de l'opération spécifiée a échoué.
70 %1$d rangées %2$s.
71 %1$d rangée %2$s.
$ Ces 3 doit être vide
75 API Testeur Automatisé
76 API Testeur Automatisé invalide
79 csql>
80 csql
81 sysadm>
82 %1$d transactions %2$s.
83 %1$d transaction %2$s.

145 <Aide: Schéma d'une classe>
146 aucun
147 <Aide: Toutes les déclencheurs>
148 <Aide: Pas de déclencheurs définis>
150 <Aide: Définition Du Déclencheurs>
151 <Aide: Syntaxe Des Commandes>
152 <Help: Sommaire Des Commandes De Dession>
178 Un nom de fichier est requis.
179 Impossible d'exécuter le paginateur.
180 Il n'est pas alloué assez de mémoire.
184 La ligne étant saisi est trop long.
185 Trop de lignes ont été saisis.
188 Trop de noms de fichiers ont été saisis.
190 Commande de session n'existe pas. Saisissez `;help' pour les messages d'aide.
191 Plus d'un commande de session correspondant. S'il vous plaît spécifier plus.
193 Plus d'une commande correspondant. S'il vous plaît spécifier plus.
194 Combinaison incorrecte d'arguments de ligne de commande (options).
196 Erreur de type inconnu s'est produite.
197 ';edit' n'est pas autorisée lorsque la sortie a été redirigée.

203 <%1$s> 
204 <Super Classes> 
205 <Sub Classes> 
206 <Attributes> 
207 <Class Attributes> 
208 <Methods> 
209 <Class Methods> 
210 <Resolutions> 
211 <Method files> 
212 <Query_specifications> 
213 <Triggers> 
214 <Name> 
215 <Event> 
216 <Condition Time> 
217 <Condition> 
218 <Action Time> 
219 <Action> 
220 <Status> 
221 <Priority> 
222 <Name> 
223 <Description> 
224 <Syntax> 
225 <Example> 

$ csql help messages
231 \
\n\
   Toutes les commandes de session doit être préfixé par `;' et seulement\n\
   des blancs / tabulations peuvent précéder du préfixe. Caractères capitalisés\n\
   représentent l'abréviation minimale que vous devez entrer pour exécuter la commande spécifiée\n\
\n\
   ;REAd   [<file-name>]        - lit un fichier en tampon de commande.\n\
   ;Write  [<file-name>]        - écrit le tampon de commande dans un fichier.\n\
   ;APpend [<file-name>]        - ajoute tampon de commande dans un fichier.\n\
   ;PRINT                       - affiche tampon de commande.\n\
   ;SHELL                       - invoque l'interpréteur de commandes.\n\
   ;CD                          - change le répertoire courant.\n\
   ;EXit                        - quitte le programme.\n\
\n\
   ;CLear                       - efface le tampon de commande.\n\
   ;EDIT                        - invoque éditeur du système avec tampon de commande.\n\
   ;LISt                        - affiche le contenu de tampon de commande.\n\
\n\
   ;RUn                         - exécute sql de tampon de commande.\n\
   ;Xrun                        - exécute sql de tampon de commande,\n\
                                  et efface le tampon de commande.\n\
   ;COMmit                      - valide la transaction en cours.\n\
   ;ROllback                    - annule la transaction en cours.\n\
   ;AUtocommit [ON|OFF]         - active/désactive la validation automatique des transactions.\n\
   ;REStart                     - redémarre la base de données.\n\
\n\
   ;SHELL_Cmd    [shell-cmd]    - définit nouveau commande par défaut pour shell,\n\
   ;EDITOR_Cmd   [editor-cmd]     éditeur, affiche et paginateur, ou afficher la \n\
   ;PRINT_Cmd    [print-cmd]      commande de courrant.\n\
   ;PAger_cmd    [pager-cmd]\n\
\n\
   ;DATE                        - affiche l'heure et la date locales.\n\
   ;DATAbase                    - affiche le nom de la base de données en cours d'accès.\n\
   ;SChema class-name           - affiche les informations de schéma d'une classe.\n\
   ;TRIgger [`*'|trigger-name]  - affiche la définition du déclencheur.\n\
   ;Get system_parameter        - obtient la valeur d'un paramètre de système.\n\
   ;SEt system_parameter=value  - définit la valeur d'un paramètre de système.\n\
   ;STring-width [width]        - définir la largeur d'affichage de chaque colonne qui est un type de chaîne.\n\
   ;COLumn-width [name]=[width] - set width that a specific column is displayed.\n\
   ;PLan [simple/detail/off]    - aficher plan d'exécution de la requête.\n\
   ;Info <command>              - affiche des informations internes.\n\
   ;TIme [ON/OFF]               - active/désactive pour afficher le temps\n\
                                  d'exécution des requêtes.\n\
   ;LINe-output [ON/OFF]        - active/désactive afficher chaque valeur dans une ligne\n\
   ;HISTORYList                 - affiche la liste des requêtes exécutées.\n\
   ;HISTORYRead <history_num>   - lit l 'entrée sur le numéro d'historique dans le tampon de commande.\n\
   ;TRAce [ON/OFF] [text/json]  - enable/disable sql auto trace.\n\
   ;HElp                        - affiche ce message d'aide.\n\n

232 <Constraints> 
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - information de schéma\n\
\ttrigger [trigger-name] - information de déclencheurs\n\
\tdeferred               - information de déclencheurs différés\n\
\tworkspace              - information d'espace de travail\n\
\tlock                   - information de verrous (mode client-serveur)\n\
\tstats class-name       - information de statistiques de classe\n\
\ttrantable              - information de transaction\n\
\tlogstat                - information du journal\n\
\tcsstat                 - information de section critique\n\
\tplan                   - information du cache de plans\n\
\tqcache                 - information du cache de requêtes\n
235 <Partitions>
236 Un nom de classe est requis.
