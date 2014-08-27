
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
$ cubrid.msg
$

$set 1 MSGCAT_SET_GENERAL
1 \n%1$s\n\n
2 Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
3 Informations sur le copyright\n
5 Valeur `%1$s' non entière pour argument 
6 Valeur `%1$s' pas de type float pour argument
7 Argument manquant `%1$s'
8 Argument en double `%1$s'
9 Valeur manquant pour argument `%1$s'
10 Argument inconnu `%1$s'
11 Argument de position inattendue `%1$s'
12 Aucune définition donné pour argument
13 Impossible d'allouer de la mémoire pour l'analyse d'arguments
14 Chaîne d'arguments dépasse la taille maximale: %1$s
15 Invalid numéro du message %1$d à l'intérieur de catalogue pour l'analyse d'arguments
16 [options]
17 Options:

$set 4 MSGCAT_SET_PARAMETER
2 %1$s  n'est pas un répertoire. Creation %2$s dans ".".
3 Impossible d'ouvrir %1$s. Fichier de configuration n'est pas créé.
4 Incapable d'écrire la ligne %1$d dans le fichier de configuration %2$s.
6 Impossible d'accéder au fichier de configuration du système "%1$s": %2$s
7 HOME pas défini
8 Paramètre requis "%1$s" n'a aucune valeur.
9 Impossible d'ouvrir le fichier de configuration "%1$s": %2$s
10 Le paramètre '%1$s' à la ligne %2$d dans le fichier '%3$s' : 
11 Variable d'environnement %1$s: 
12 Mot-clé non reconnue.
13 Type de valeur ne correspond pas au type de paramètre.
14 Impossible d'allouer de la mémoire pour stocker la valeur.
15 Valeur de chaîne non terminée.
16 La valeur est en dehors des limites.
17 Erreur de UNIX: %1$s
18 Pas de message disponible
19 La valeur est en dehors des limites. Réinitialisation aux valeurs par défaut.
20 Mot-clé = '%1$s'    Valeur = %2$d    Valeur par défaut = %3$d\n
21 Mot-clé = '%1$s'    Valeur = %2$f    Valeur par défaut = %3$f\n
22 paramètre obsolète
23 Les paramètres '%1$s' et '%2$s' ne peuvent pas être utilisés ensemble.\n
31 Le paramètre '%1$s':

$set 5 MSGCAT_SET_ERROR
1 Message manquant pour code d'erreur %1$d.
2 Défaut système interne: aucune information plus spécifique n'est disponible.
3 Mémoire virtuelle épuisée: incapable d'allouer %1$lld octets de mémoire.
4 A été interrompue.
5 Clé = "%1$s" est introuvable dans la table de hachage "%2$s".
6 Une table de hachage avec NULL mémoire a été fourni à une fonction de table de hachage.
7 Essayer de formater volume de disque "%1$s" avec une valeur incorrecte %2$d pour nombre de pages.
8 Impossible de formater volume de disque "%1$s" avec %2$d pages (%3$lld octets).
9 Impossible de formater volume de disque "%1$s" avec %2$d pages (%3$lld Ko) en raison d'un espace insuffisant. L'espace actuellement disponible est %4$d pages (%5$lld Ko).
10 Impossible de monter le volume de disque "%1$s".
11 Impossible de monter le volume de disque "%1$s". La base de données "%2$s" à laquelle appartient le volume de disque, est utilisé par %3$s sur le processus %4$d de l'hôte %5$s depuis %6$s.
12 Problèmes au démontage du volume "%1$s".
13 Une erreur d'E/S s'est produite pendant la lecture de la page %1$d du volume "%2$s".
14 Une erreur d'E/S s'est produite pendant l'écriture la page %1$d du volume "%2$s".
15 Impossible d'écrire la page %1$d du volume "%2$s" en raison d'un espace insuffisant dans le dispositif du système d'exploitation.
16 Impossible de renommer le volume de disque "%1$s" à "%2$s".
17 Erreur interne: obtention de la pageid désallouée %1$d du volume "%2$s".
18 Tous les tampons de pages sont fixes (LRU: %1$d).
19 Erreur interne: pageptr = %1$p de la page %2$d du volume "%3$s" n'est pas fixe.
20 Erreur interne: pageptr inconnu = %1$p.
21 Erreur interne: secteur inconnu %1$d du volume "%2$s".
22 Erreur interne: page inconnu %1$d du volume "%2$s".
23 Erreur interne: tentative de désallouer la page de système %1$d du volume "%2$s".
24 Près de manquer d'espace dans le volume "%1$s". Nombre total de pages = %2$d, nombre total de pages libres = %3$d.
25 Près de manquer d'espace dans les volumes ayant des données que le but principal de stockage. Nombre total de pages = %1$d, nombre total de pages libres = %2$d.
26 Près de manquer d'espace dans les volumes ayant un indice que le but principal de stockage. Nombre total de pages = %1$d, nombre total de pages libres = %2$d.
27 Près de manquer d'espace dans les volumes ayant le but principal de stockage générique. Nombre total de pages = %1$d, nombre total de pages libres = %2$d.
28 Près de manquer d'espace dans les volumes ayant des données temporaires que le but principal de stockage. Nombre total de pages = %1$d, nombre total de pages libres = %2$d.
29 Dernière notification de manquer d'espace dans le volume "%1$s". Nombre total de pages = %2$d, nombre total de pages libres = %3$d.
30 Dernière notification de manquer d'espace dans les volumes ayant des données que le but principal de stockage. Nombre total de pages = %1$d, nombre total de pages libres = %2$d.
31 Dernière notification de manquer d'espace dans les volumes ayant un indice que le but principal de stockage. Nombre total de pages = %1$d, nombre total de pages libres = %2$d.
32 Dernière notification de manquer d'espace dans les volumes ayant le but principal de stockage générique. Nombre total de pages = %1$d, nombre total de pages libres = %2$d.
33 Dernière notification de manquer d'espace dans les volumes ayant des données temporaires que le but principal de stockage. Nombre total de pages = %1$d, nombre total de pages libres = %2$d.
34 Erreur interne: page %1$d est hors limites pour le fichier %2$d en volume "%3$s". Nombre actuel de pages = %4$d.
35 Erreur interne: Inconnu identificateur du volume %1$d.
36 Pas d'espace de disque dans la base de données. Nombre de page(s) demandée(s) = %1$d.
37 Pas d'espace de disque dans volume "%1$s". Nombre de page(s) demandée(s) = %2$d.
38 Erreur interne: VFID inconnu %1$d|%2$d du fichier.
39 Erreur interne: Page %1$d|%2$d(volume "%3$s") de VFID %4$d|%5$d du fichier(volume "%6$s") n'est pas valide / désallouée selon la carte de répartition.
40 Erreur interne: %1$d pages attendues, %2$d pages trouvées pour VFID %3$d|%4$d du fichier(volume "%5$s").
41 Erreur interne: Page %1$d|%2$d(volume "%3$s") ne ​​fait pas partie du VFID %4$d|%5$d du fichier(volume "%6$s").
42 Impossible de réorganiser les enregistrements ancrés sur la page %1$d du volume "%2$s".
43 Erreur interne: La page %1$d du volume "%2$s" peut-être corrompue. Enregistrement %3$d a été trouvée lorsque %4$d a été attendu.
44 Pas d'espace disponible dans la page de fentes %1$d du volume "%2$s".
45 Fente %1$d sur page %2$d du volume "%3$s" est attribué à un enregistrement ancré. Un nouveau record ne peut pas être inséré ici.
46 Erreur interne: fente %1$d sur page %2$d du volume "%3$s" est pas allouée.
47 Impossible de créer un fichier de tas dans le volume "%1$s".
48 Accès à l'objet supprimé %1$d|%2$d|%3$d.
49 Erreur interne: la classe de l'objet %1$d|%2$d|%3$d est inconnue.
50 Erreur interne: enregistrement de relocalisation de l'objet %1$d|%2$d|%3$d peut être corrompu.
51 Erreur interne: objet %1$d|%2$d|%3$d peut être corrompu.
52 Erreur interne: adresse de dépassement pour objet %1$d|%2$d|%3$d peut être corrompue.
53 Obtention de l'objet %1$d|%2$d|%3$d seulement quand son OID a été attribué.
54 Erreur interne: tentative de manipuler un objet long de %1$d pages. La taille maximale permise pour un objet est de %2$d pages.
55 Erreur interne: un cycle de références de pages a été détecté sur la page %1$d|%2$d du fichier de tas %3$d|%4$d|%5$d.
56 Erreur interne: inconnue fichier de hachage extensible (Volid: %1$d Fileid: %2$d) page (Volid: %3$d Pageid: %4$d) a été spécifié.
57 La clé n'existe pas dans la structure de hachage extensible.
58 Chaîne "%1$s" est trop long pour tenir dans une page de fentes.
59 Erreur interne: le type de clé spécifiée %1$d n'est pas valide pour la structure de hachage extensible.
60 Erreur interne: la structure de hachage extensible a été corrompue.
61 Erreur interne: la page du répertoire racine de la structure de hachage extensible (Volid: %1$d Fileid: %2$d Pageid: %3$d) a été corrompue.
62 Erreur interne: enregistrement pour trier %1$d (size %2$d) ne rentre pas dans une page, la taille maximale permise est de %3$d.
63 Erreur interne: une page temporaire a été corrompue pendant le tri.
64 Classe inconnue "%1$s".
65 Classe "%1$s" existe déjà.
66 Erreur interne: inconnue opération forcée %1$d pour objet %2$d|%3$d|%4$d.
67 Erreur interne: un fichier de tas n'a pas été allouée pour stocker l'objet %1$d|%2$d|%3$d.
68 Erreur interne: noms de classes différentes pour la classe avec oid = %1$d|%2$d|%3$d  été trouvé. Classnames trouvés sont "%4$s", "%5$s" à l'aide de table de hachage et du tas pour les noms des classes, respectivement.
69 Erreur interne: différents identificateurs d'objets de classe ont été trouvés pour la classe avec le nom "%1$s". OIDs trouvés sont %2$d|%3$d|%4$d, %5$d|%6$d|%7$d à l'aide de table de hachage et du tas pour les noms des classes, respectivement.
70 Erreur interne: Classe avec le nom "%1$s" and oid = %2$d|%3$d|%4$d n'existe pas dans la table de hachage pour les noms des classes.
71 Erreur interne: Classe avec le nom "%1$s" and oid = %2$d|%3$d|%4$d n'existe pas dans son tas.
72 Votre transaction (index %1$d, %2$s@%3$s|%4$d) a été unilatéralement annulée par le système.
73 Votre transaction (index %1$d, %2$s@%3$s|%4$d) a expiré en attendant de %5$s verrou sur objet %6$d|%7$d|%8$d. Vous êtes en attente pour l(es)'utilisateur(s) %9$s à la fin.
74 Votre transaction (index %1$d, %2$s@%3$s|%4$d) a expiré en attendant de %5$s verrou sur classe %6$s. Vous êtes en attente pour l(es)'utilisateur(s) %7$s à la fin.
75 Votre transaction (index %1$d, %2$s@%3$s|%4$d) a expiré en attendant de %5$s verrou sur instance %6$d|%7$d|%8$d de classe %9$s. Vous êtes en attente pour l(es)'utilisateur(s) %10$s à la fin.
76 Votre transaction (index %1$d, %2$s@%3$s|%4$d) a expiré en attendant de %5$s sur page %6$d|%7$d. Vous êtes en attente pour l(es)'utilisateur(s) %8$s pour libérer le verrou de page.
77 Erreur interne: tous les tampons sont fixés.
78 Erreur interne: une erreur d'E/S s'est produite pendant la lecture de page logique %1$lld du journal (page physique %2$lld) sur "%3$s".
79 Erreur interne: une erreur d'E/S s'est produite pendant l'écriture de page logique %1$lld du journal (page physique %2$lld) sur "%3$s".
80 Espace insuffisant dans le dispositif de l'appareil du système d'exploitation pendant l'écriture de page logique %1$lld du journal (page physique %2$lld) sur "%3$s".Impossible d'écrire plus de %4$d octets.
81 Erreur interne: page logique %1$lld de journal peuvent être corrompue.
82 Impossible de monter le volume de disque/fichier de journal "%1$s".
83 Longueur du chemin du fichier "%1$s" plus la longueur du nom du journal de préfixes "%2$s" est trop long; la longueur totale doit être inférieure à %3$d.
84 Longueur du nom du journal de préfixes "%1$s" est trop long; la longueur doit être inférieure à %2$d.
85 Le nom du préfixe "%1$s"  n'est pas le même que "%2$s" sur le disque journal. Le journal a probablement été renommé en dehors du domaine de base de données.
86 Base de données est incompatible avec le courant "%1$s" version "%2$s".
87 Il existe des actions de restauration qui doivent être effectuées en utilisant "%1$s" version "%2$s" au lieu de la version "%3$s". Après la restauration, la base de données peut être exécuté sur la version "%4$s".
88 Erreur interne: chaîne de version "%1$s" ne peut pas dépasser %2$d. Doit changer en-tête du journal.
89 Journal "%1$s" n'appartient pas à la base de données.
90 Redo-logging est toujours une opération à niveau de la page. Un pointeur de page de données doit être donnée dans le cadre de l'adresse.
91 Postpone-logging est toujours une opération à niveau de la page. Un pointeur de page de données doit être donnée dans le cadre de l'adresse.
92 Compensate-logging est toujours une opération à niveau de la page. Un pointeur de page de données doit être donnée dans le cadre de l'adresse.
93 Attention: redo-logging est ignorée pendant la récupération répétée ("REDO") et les annulations normales.
94 Attention: postpone-logging est ignorée pendant la récupération répétée ("REDO") et les annulations normales.
95 Attention: undo-logging est ignorée pendant la récupération et les annulations normales.
96 Récupération des médias peut être nécessaire sur le volume "%1$s".
97 Erreur interne: impossible de trouver la page de journal %1$lld dans le journal des transaction.
98 Impossible de créer le journal de transactions "%1$s" pour enregistrer les pages à partir de %2$lld à %3$lld.
99 Impossible de créer le répertoire de sauvegarde "%1$s".
100 Impossible de sauvegarder la base de données "%1$s".
101 Fichier inconnu "%1$s" de l'utilisateur.
102 Fichier "%1$s" n'a pas assez d'enregistrements: %2$d enregistrements attendues.
103 Fichier "%1$s" semble avoir des enregistrements non ordonnées. Enregistrement %2$d avec des valeurs: %3$d %4$s %5$s, a été trouvé pendant l'enregistrement avec les valeurs %6$d %7$s a été attendue.
104 Fichier "%1$s" semble avoir une enregistrement incorrecte pour le nom de base de données (volume principal). Enregistrement %2$d avec des valeurs: %3$d %4$s %5$s, a été trouvé pendant l'enregistrement avec les valeurs %6$d %7$s %8$s a été attendue.
105 Ne peut pas accéder au fichier de sauvegarde "%1$s". Restauration ou Sauvegarde sont annulées.
106 Impossible de préparer pour valider la transaction en cours avec identifiant de transaction global %1$d parce que l'identifiant de transaction est utilisé par une autre transaction.
107 Il n'ya pas de transaction globale distribuée associée à identificateur de transaction %1$d.
108 Transaction en cours %1$d doit être validée ou abandonnée avant de tenter de joindre à la transaction distribuée avec identifiant global %2$d.
109 Votre transaction %1$d a été unilatéralement annulée par le système, car un site participant (à distance) a baissé ou unilatéralement annulé la transaction.
110 Valeur de niveau d'isolement doit être compris entre %1$d et %2$d.
111 Votre transaction a été abandonnée par le système en raison de défaillance d'un serveur ou d'un changement de mode.
112 Hôte "%1$s" n'est pas autorisé à exécuter la base de données.
113 Impossible de redémarrer / initialiser le serveur de base de données. %1$s
114 Impossible d'interpréter "%1$s" as a database. Les volumes/fichiers de base de données peuvent avoir été renommé/copié en dehors du domaine de base de données.
115 Base de données "%1$s" existe déjà.
116 Base de données "%1$s" est inconnu, ou le fichier "databases.txt" n'est pas accessible.
117 Nom de chemin absolu pour la base de données est trop long. La longueur totale de le chemin "%1$s" et le nom "%2$s" est %3$d; la longueur combinée doit être inférieur à %4$d.
118 Impossible de localiser le répertoire de travail courant.
119 Impossible de trouver hôte local.
120 Le nombre maximal de volumes (%1$d) a été dépassé.
121 Tentative de supprimer "%1$s" un volume permanente de la base de données.
122 Impossible d'accéder au catalogue de messages du système.
123 Impossible de créer %1$s pour la base de données "%2$s".  S'il vous plaît se référer à "%3$s" pour plus d'informations.
124 Volume "%1$s" existe déjà.
125 Une extension de volume automatique "%1$s" de %2$d pages et le but de stockage générique a été créé.
126 But inconnu "%1$s" donné dans la ligne %2$d.
127 Valeur incorrecte %1$d pour le nombre de pages données dans la ligne %2$d.
128 Nombre de pages ne figurant pas dans la ligne %1$d.
129 Token inconnu "%1$s" a été trouvé dans la ligne %2$d.
130 Mémoire virtuelle saturée.
131 Erreur du système de base de données.
132 Fonctionnalité pas implémentée dans la version actuelle: %1$s.
133 Constant de type ensemble imbriqué est interdit.
134 Il n'est pas permis d'utiliser les deux attributs de classe et les attributs des non-classe dans une mise à jour unique.
135 Instruction invalide pour les colonnes avec OID: %1$s.
136 Fonction "%1$s" non implémentée.
137 Objets d'autorisation corrompu.
138 Classe d'autorisation "%1$s" introuvable.
139 Erreur d'accès sur l'attribut d'autorisation "%2$s" de la classe "%1$s".
140 Opération "%1$s" peut être effectuée seulement par l'administrateur ou un membre du groupe DBA.
141 Impossible d'ajouter "%1$s"  comme membre de "%2$s".
142 L'ajout d'un élément entraîne le hiérarchie des utilisateurs à un diagramme cyclique.
143 Rencontré une classe sans propriétaire.
144 Impossible d'accéder à l'objet utilisateur.
145 Ne peut pas émettre grant / revoke commandes pour vous-même.
146 Ne peut pas émettre GRANT / REVOKE pour le propriétaire d'une classe.
147 Aucune option GRANT.
148 Impossible d'obtenir un verrou pour écriture sur l'objet d'autorisation.
149 Impossible de créer une instance d'un objet d'autorisation.
150 Ne peut pas révoquer les privilèges de même.
151 Ne peut pas révoquer les privilèges d'un propriétaire d'une classe.
152 GRANT pas trouvé.
153 Pas de privilèges d'autorisation en vigueur pour la base de données.
154 Installation d'autorisation incomplète - pas de privilèges d'autorisation en vigueur pour la base de données.
155 Le contenu de la classe d'autorisation n'est pas valide (enregistrements multiples).
156 Échec d'autorisation.
157 Échec d'autorisation pour SELECT.
158 Échec d'autorisation pour ALTER.
159 Échec d'autorisation pour UPDATE.
160 Échec d'autorisation pour INSERT.
161 Échec d'autorisation pour DELETE.
162 Échec d'autorisation pour INDEX.
163 Échec d'autorisation pour EXECUTE.
164 L'utilisateur "%1$s" existe déjà.
165 L'utilisateur "%1$s" n'est pas valide.
166 L'utilisateur spécifié n'est pas valide.
167 DBA, les membres du groupe DBA et le propriétaire de la classe peut effectuer l'opération.
168 Membre introuvable.
169 Impossible de supprimer l'utilisateur %1$s de la base de données.
170 Pas d'utilisateur connecté.
171 Mot de passe incorrect ou manquant.
172 Chaîne de mot de passe ne peut pas avoir plus de 31 octets.
173 Impossible de trouver le fichier de base de données "%1$s".
174 Impossible d'obtenir un accès d'écriture au fichier de base de données "%1$s".
175 Fichier de base de données mal formatées "%1$s".
176 Erreur de conversion dans le format de la date.
177 Impossible de créer un objet de grande taille pour le stockage interne.
178 Temporaire OID rencontré sans MOP.
179 Domaine "%1$s" n'est pas compatible avec le domaine "%2$s".
180 Domaine "%1$s" de la valeur fournie n'est pas compatible avec domaine de destination "%2$s".
181 Ne peut pas contraindre la valeur du domaine "%1$s" à domaine "%2$s".
182 Ne peut pas contraindre la valeur du domaine "%1$s" à domaine "%2$s" sans dépassement.
183 Montant inattendu de données reçues; %1$d attendu, %2$d reçu.
184 Impossible d'allouer un tampon de communication.
185 Erreur de réception des données de client.
186 Erreur de réception des données du serveur.
187 Tampon de communication pas utilisé.
188 Base de données inconnu "%1$s".
189 Nom d'hôte "%1$s" invalide.
190 Hôte du serveur pas identifié.
191 N'a pas pu se connecter au serveur  "%1$s" sur "%2$s".
192 Impossible de charger le fichier du réseau de configuration.
193 Serveur a reçu la commande d'arrêt à partir du client.
194 Demande id %1$d inconnue de serveur.
195 Erreur de communication de serveur: %1$s.
196 Nom du serveur pas identifié.
197 Ne pouvait pas entrer en contact avec le serveur maître.
198 Tampon de réception trop faible, données tronquées.
199 Serveur ne répond plus.
200 Pas correctement supprimer l'ensemble de l'objet après libre.
201 Suppression d'incorrecte enregistration UNIQUE de la table.
202 Attribut "%1$s" pas trouvé.
203 Valeur de l'attribut "%1$s" doit être de type "%2$s", non de type "%3$s".
204 Fonction appelée avec des arguments manquants ou non valides.
205 Attribut "%1$s" ne peut pas être NULL.
206 Attribut "%1$s" n'a pas la contrainte UNIQUE.
207 Classe "%1$s" a un objet de la taille zéro.
208 Méthode "%1$s" est introuvable.
209 Aucun attribut ou méthode avec le nom "%1$s" a été trouvé.
210 Erreur interne: traitement de modèle d'objet.
211 Attribut a été supprimé pendant la construction de modèle d'objet.
212 Attribut "%1$s" viole la contrainte UNIQUE.
213 Enregistrement UNIQUE invalide rencontrée.
214 Un conflit de domaine existe sur l'attribut "%1$s".
215 Attribut avec l'ID %1$d est introuvable.
216 Tag de propriété d'un objet sur ​​la valeur est invalide.
217 L'opération peut être effectuée que sur objets de classe.
218 Objet non valide dans db_get expression de chemin.
219 Invalid expression de chemin db_get.
220 Ensemble non valide dans expression de chemin db_get.
221 Index sur ensembles n'est pas valid dans expression de chemin db_get.
222 Tentative d'attribuer un chaîne supérieure à %2$d caractères à l'attribut "%1$s".
223 Valeur %2$d est trop grande pour l'attribut de type smallint "%1$s".
224 Une base de données n'a pas été redémarré.
225 Valeur manquante pour l'attribut "%1$s" avec la contrainte NOT NULL
226 Liste des composants de classe est vide.
227 Tentative d'attribuer une valeur à l'attribut "%1$s" plus d'une fois.
228 Méthode "%1$s" est appelée avec %2$d arguments. Le système permet un maximum de %3$d arguments.
229 Méthode "%1$s" nécessite argument %2$d à l'intérieur du domaine "%3$s". Une valeur non valide de domaine "%4$s" a été donné.
230 Attribut "%1$s" ne peut pas être attribué une chaîne de longueur %2$d.  La longueur de chaîne maximale permise est de %3$d.
231 Incorrecte opération d'API tentée sur un objet temporaire.
232 Il n'es pas permis d'utiliser des modèles d'objets sur de multiples transactions.
233 Impossible de créer la classe avec le nom de type de base "%1$s".
234 Fichier des méthodes "%1$s" est introuvable.
235 %1$d fonctions de méthode non résolues après la liaison.
236 Méthode "%1$s" non résolue.
237 De graves problèmes ont été rencontrés pendant la liaison dynamique.
238 Impossible d'accéder à la fichier des méthodes "%1$s".
239 Attribut "%1$s" pas trouvé.
240 Méthode "%1$s" pas trouvé.
241 Il n'y a pas d'attribut ou méthode avec le nom "%1$s".
242 Signature pour %2$s n'a pas trouvé pour méthode "%1$s".
243 L'argument %2$d de la méthode "%1$s" n'est pas defini.
244 Le nom de domaine "%1$s" n'est pas le nom d'une classe ou d'un type de base.
245 Le nom "%1$s" a déjà été utilisé comme un nom d'un attribut.
246 Le nom "%1$s" a déjà été utilisé comme un nom d'une méthode.
247 Arguments non valides pour fonction interne de gestionnaire de schéma.
248 Le domaine d'attribut "%1$s"  n'est pas approprié pour utilisation avec la contrainte %2$s.
249 L'opération demandée sur la classe "%1$s" ne peut être effectuée que devant les instances sont créées.
250 Impossible de créer un index sur l'attribut "%1$s", défini avec une valeur partagée.
251 Signature %2$s existe déjà pour la méthode "%1$s".
252 Attribut "%1$s" doit avoir un domaine fixé pour cette opération.
253 Attribut "%1$s" ne peut pas être définie avec un ensemble imbriqué de domaine.
254 Impossible de supprimer le domaine "%2$s" de l'attribut "%1$s".
255 Attribut "%1$s" n'est pas d'un domaine de taille variable.
256 Super-classe existe déjà.
257 L'ajout d'une super-classe causerait des cycles dans la hiérarchie de classe.
258 Super-classe pas trouvé.
259 Signatures multiples existent pour la méthode "%1$s".
260 Argument %2$d de méthode "%1$s" n'est pas un domaine d'ensemble.
261 Résolution pour "%1$s" est introuvable.
262 La valeur de domaine "%2$s" pas approprié pour l'attribut "%1$s".
263 Corruption interne détecté dans le gestionnaire de schéma.
264 Alias "%2$s"  pour l'attribut / méthode "%1$s" n'est pas unique.
265 Masquage d'attribut "%1$s" avec domaine en conflit "%2$s".
266 Conflit de nom sur l'attribut "%1$s" avec les classes "%2$s" et "%3$s".
267 Masquage de méthode "%1$s" avec signature contradictoire.
268 Conflict du nom sur méthode "%1$s" avec les classes "%2$s" et "%3$s".
269 Impossible de définir un indice sur le domaine "%1$s".
270 Impossible de définir une contrainte UNIQUE sur le domaine "%1$s".
271 Cycle détecté dans la hiérarchie de classe; "%1$s" est une super-classe de "%2$s".
272 Index "%2$s" déjà défini pour la classe "%1$s".
273 Index "%1$s" n'existe pas.
274 Caractères interdits dans le nom: "%1$s".
275 Opération interdite sur l'attribut "%1$s", héritée de la classe "%2$s".
276 Opération interdite sur méthode  "%1$s", héritée de la classe "%2$s".
277 Opération interdite sur "%1$s", héritée de la classe "%2$s".
278 Classe "%4$s" a des domaines incompatibles pour "%1$s", héritée de la classe "%2$s" et "%3$s".
279 Dans la classe "%4$s", "%1$s" héritée de la classe "%2$s" ne peut être utilisé conformément à la demande. La définition de la classe "%3$s" a un domaine plus spécifique.
280 Dans la classe "%3$s", "%1$s" ne peut pas être héritée de la classe "%2$s" parce qu'il ya une définition locale avec un domaine incompatible.
281 Dans la classe "%3$s", "%1$s" héritée de la classe "%2$s" a un alias mais pas de substitut d'une autre source n'a été trouvée.
282 Dans la classe "%4$s", "%1$s" héritée de la classe "%3$s" ne peut pas servir comme un substitut d'alias pour la classe "%2$s". Les domaines sont incompatibles.
283 Dans la classe "%4$s", "%1$s" héritée de la classe "%3$s" ne peut pas servir comme un substitut d'alias pour la classe "%2$s". Il dispose d'un domaine moins spécifique.
284 Impossible de demander l'héritage de "%1$s" car elle est définie localement dans la classe.
285 Impossible d'utiliser "%1$s" comme alias pour la composante héréditaire "%2$s" parce qu'il ya déjà une composante définie localement avec ce nom.
286 Impossible d'utiliser "%1$s" comme alias pour "%2$s" de "%3$s". Une composante de ce nom est déjà hérité de "%4$s".
287 Dans la classe "%1$s", ne peut pas définir l'attribut "%2$s" car el masque une méthode héritée de "%3$s".
288 Dans la classe "%1$s", ne peut pas définir la méthode "%2$s" car elle masque un attribut hérité de "%3$s".
289 Sous-classe "%1$s" ne peut hériter méthode "%2$s" de la classe "%3$s" parce qu'il ya déjà un attribut avec ce nom.
290 Sous-classe "%1$s" ne peut hériter attribut "%2$s" de la classe "%3$s" parce qu'il ya déjà une méthode avec ce nom.
291 "%1$s" est défini dans la classe "%2$s" comme un attribut, et dans la classe "%3$s" comme une méthode..
292 Aucune spécification pour remplir avec l'indice "%1$s".
293 Opération non permise pour ce type de classe.
294 Variable d'environnement pas définie "%1$s" est référencée dans le fichier de méthode.
295 Impossible de modifier la classe "%1$s". Le catalogue système est à court d'espace. Pour récupérer de l'espace, compacte la base de données avec l'utilitaire compactdb.
296 Liste des propriétés incorrecte rencontrée.
297 Alias ​​"%1$s" est utilisé plus d'une fois.
298 Dans la classe "%1$s", une résolution pour "%2$s" de "%3$s"' n'est pas permise. Cette classe n'est pas une super-classe.
299 "%1$s" est un mot réservé et ne peut pas être utilisé comme un attribut, une méthode, ou un nom d'une classe.
300 Attribut "%1$s" ne peut pas être définie avec contraintes DEFAULT et UNIQUE simultanément.
301 Attribut "%1$s" ne peut pas être définie avec une contrainte de longueur de %2$d. La contrainte de longueur maximale est de %3$d.
302 Impossible d'ajouter l'élément à un ensemble.
303 Valeur existe déjà dans l'ensemble.
304 Index d'ensemble %1$d est en dehors des limites.
305 Indice de séquence %1$d est en dehors des limites.
306 Opération demandée ne peut être réalisée que sur des séquences.
307 Opération demandée ne peut être réalisée que sur des ensembles.
308 Valeur de l'élément n'est pas dans le domaine de l'ensemble.
309 Index de l'élément d'ensemble %1$d est interdit.
310 Élément introuvable dans l'ensemble.
311 Élément introuvable dans l'ordre.
312 "%1$s" n'est pas un domaine valide de type ensemble.
313 Dépassement de capacité négatif pour tampon d'objet pendant la lecture.
314 Dépassement de tampon d'objet pendant l'écriture..
315 Définition illégale pour métaclasse a été rencontrée.
316 Discordance dans calcul de la taille du transformeur, attendue %1$d calculée %2$d.
317 Représentation incorrecte sur disque a été rencontrée pour la classe "%1$s".
318 Synchronisation rompu pendant le chargement d'objets. Base de données est probablement corrompue ou pas mis à jour.
319 Ignorant extension inconnu d'attribut.
320 Représentation corrompu sur disque, pour l'objet, a été rencontrée.
321 Rupture possible détectée dans l'espace de travail.
322 MOP pas trouvé dans l'espace de travail.
323 Tentative de mise à niveau un MOP non-temporaire dans l'espace de travail.
324 Objet sans classe a été trouvé dans l'espace de travail.
325 Tentative de libérer la mémoire d'un MOP à l'intérieur de la liste sale.
326 Modification du pointeur de classe pour l'objet dans l'espace de travail.
327 Impossible de créer MOP avec OID NULL.
328 Instance sans classe a été rencontrée dans l'espace de travail.
329 Liste des objets non allouée dans l'espace de travail.
330 Violation d'épinglage d'espace de travail.
331 Impossible d'allouer le bloc de mémoire pour l'espace de travail dans l'OS, mémoire virtuelle épuisée. Transaction annulée.
332 Estimation initiale de la taille de l'espace de travail a été dépassé. Une extension automatique a été effectué.
333 Mémoire épuisée pendant l'allocation de stockage pour l'espace de travail. Transaction annulée.
334 Allocation essayée de %1$d octets est plus grand que la taille maximale de bloc de mémoire pour l'espace de travail. Essayez d'ajuster le paramètre max_block_size. Transaction annulée.
335 Tentative de libérer un objet pas alloué dans l'espace de travail est ignorée.
336 Tentative de libérer un objet plus d'une fois est ignorée.
337 Tentative d'allouer un objet avec une taille négative est ignorée.
338 Dépassement d'identificateur d'entrée de carte.
339 Impossible de quitter la section critique, car il n'appartient pas à la transaction %1$d.
340 Fork de processus enfant est impossible.
341 Exec de processus enfant est impossible.
342 Impossible de modifier le groupe de processus.
343 Request_id est déjà utilisé.
344 %1$s.
345 Une erreur s'est produite lors de la lecture d'un message d'arrêt envoyé par le maître.
346 Une erreur s'est produite lors de la lecture d'un message d'annulation d'arrêt envoyé par le maître.
347 Impossible de créer un pipe vers le serveur maître.
348 Doit spécifier un service ou un identificateur de port.
349 Service inconnu pour TCP: %1$s.
350 Erreur de récupération de l'hôte local par le nom pour l'hôte "%1$s". Hôte introuvable.
351 Impossible de créer un socket TCP.
352 tcp_open: ne peut pas obtenir un port TCP réservé.
353 Impossible d'établir la connexion au serveur maître sur l'hôte "%1$s".
354 Impossible de définir le nouveau propriétaire du processus pour socket.
355 Impossible de créer un flux de socket.
356 Fichier '%1$s' pour socket UNIX de domaine existe déjà.
357 Impossible de lier l'adresse locale... abandon.
358 Réception  échoué.
359 Erreur sur datagramme pendant BIND.
360 Erreur sur datagramme pendant ACCEPT.
361 Erreur sur datagramme pendant CONNECT.
362 Erreur sur datagramme du socket.
363 Recvmsg de maître socket a échoué.
364 Envoi d'une nouvelle demande au serveur.
365 Envoi d'un message diffusé au client.
366 Erreur venant du serveur: %1$s
367 Serveur %1$s existe déjà.
368 Erreur de communication pendant CONNECT pour %1$s.
369 Code d'erreur pas utilisé.
370 Code d'erreur pas utilisé.
371 Code d'erreur pas utilisé.
372 Code d'erreur pas utilisé.
373 EOF prématuré trouvé pendant la lecture: %1$d.
374 EOF prématuré trouvé pendant la lecture de %1$d ou %2$d.
375 Erreur de conversion sur (0x%1$c%2$c) %3$d et %4$d convertis en %5$d.
376 Aucune classe publique trouvé.
377 Erreur %1$d: échec de l'ajout d'attribut "%2$s" à la hiérarchie multimédia.
378 Erreur %1$d: échec de l'ajout du méthode "%2$s" implémenté comme "%3$s" à la hiérarchie multimédia.
379 Erreur %1$d: échec de l'ajout d'une super-classe à la hiérarchie multimédia.
380 Chargeur dynamique déjà initialisé.
381 Chargeur dynamique pas initialisé.
382 Chargeur dynamique incorrectement initialisé.
383 "%1$s" n'est pas un fichier objet ou une journal.
384 Impossible de déterminer le chemin absolu pour "%1$s".
385 ld processus quitté avec le code de %1$d.
386 ld processus quitté en raison de signaler %1$d.
387 ld processus quitté avec WAITVAL inconnu 0x%1$x.
388 Impossible d'accéder à l'image initiale pour "%1$s".
389 Erreur du système: %1$s.
390 Problème avec "%1$s": %2$s.
391 Conflit avec autre gestionnaire du SIGPIPE.
392 Impossible de créer le démon pour chargeur dynamique.
393 Démon du chargeur dynamique a disparu.
394 Point final de la plage pas compris entre 0 et 256.
395 Sous-expression spécifiée est interdite.
396 Chiffre octal hors des limites existe dans échappement.
397 Expression régulière n'est pas délimité correctement.
398 Recherche de texte appelé avant la compilation d'expression.
399 Une parenthèse sans correspondance se trouve dans l'expression régulière.
400 Les parenthèses sont imbriquées trop profondément dans l'expression régulière.
401 Plus de deux nombres spécifiés dans une plage.
402 Une accolade sans correspondance se trouve dans le expession régulière.
403 Premier numéro de l'expression plage est plus grande que le deuxième.
404 Un crochet sans correspondance se trouve dans l'expression régulière.
405 Le expression régulière était trop long à compiler.	
406 Identificateur d'arbre B+ d'index non valid: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
407 Clé inconnue %1$s référencé dans arbre B+ d'index {vfid: (%2$d, %3$d), rt_pgid: %4$d, key_type: %5$s}.
408 Valeur inconnue (oid) %1$d|%2$d|%3$d référencé.
409 Valeur en double (oid) %1$d|%2$d|%3$d référencé.
410 Clé NULL référencé.
411 Type incorrect de clé d'arbre B+ d'index "%1$s".
412 Spécification invalide pour recherche de plage.
413 Identificateur d'attribut inconnu: %1$d.
414 Identificateur de classe inconnu: %1$d|%2$d|%3$d.
415 Identificateur de classe non valid: %1$d|%2$d|%3$d.
416 Identificateur de représentation inconnu: %1$d.
417 Identificateur de représentation non valid: %1$d.
418 Descripteur d'enregistrement n'a pas assez d'espace: %1$d.
419 Classe a atteint le maximum nombre de représentations: %1$d.
420 Classe %1$d|%2$d|%3$d déjà eu des représentations.
421 Annuaire des représentations manquant dans le catalogue pour classe: %1$d|%2$d|%3$d.
422 Fiche de renseignements sur la représentation manquante dans le catalogue pour classe - Repr_Id: %1$d|%2$d|%3$d - %4$d.
423 Session est invalide.
424 Aucune instruction à exécuter.
425 Type de données incompatible pour l'attribut "%1$s" de la classa "%2$s".
426 Type de données incompatible pour l'attribut "%1$s".
427 Dépassement de données du type de données "%1$s".
428 Instruction n'est pas modifiable.
429 Commande inconnue..
430 Variable inconnue. "%1$s".
431 Erreur dans interpréteur ou analyseur SQL/X.
432 Classe inconnue "%1$s".
433 Attribut inconnu "%1$s" dans la classe "%2$s".
434 Impossible de trouver les noms de colonnes corrects d'une requête.
435 Instructions multiples pas permises.
436 Instruction n'est pas une requête.
437 ID longue de l'objet est invalide (%1$s,%2$d, %3$s, %4$s).
438 Un conflit existe dans le descripteur long d'objet.
439 L'offset %1$d est supérieure à la longueur de l'objet long %2$d.
440 Position du curseur pas valide.
441 Opération de curseur pas valide.
442 Position du curseur est inconnue.
443 Indice du valeur de tuple %1$d est en dehors des limites.
444 Nom de l'attribut non valide: "%1$s".
445 Index de liste de valeurs %1$d est en dehors des limites.
446 Il existe des structures de résultats de la requête fermées par le gestionnaire de transaction, mais non fermées par l'application.
447 Tentative de manipuler une structure de résultat de la requête déjà fermée.
448 Ttailles des tuples sont actuellement limités à pas plus d'une page.
449 Identificateur de requête inconnu: %1$d.
450 Opérateur d'ensembe non valid: %1$d.
451 Fichier de tas à aucune instance.
452 Invalid contenu pour nœud XASL de l'arbre.
453 Pas plus de nœuds de spécification d'acess.
454 Type de données non valide a été référencé.
455 Pas plus de pages de fichiers de requêtes.
456 Références de type de données sont incompatibles.
457 Invalid type de résultat de la requête.
458 Dépassement produit dans un contexte d'addition.
459 Résultat de la requête contient plus d'un tuple unique.
460 Trop peu de variables d'hôte des entrée fourni.
461 Les variables d'hôte fournies sont plus que les colonnes de résultat.
462 La valeur de résultat est NULL, mais il n'y a pas de variable d'indicateur.
463 Instruction de curseur n'a pas encore été préparé.
464 Curseur devrait être une instruction SELECT.
465 Le curseur n'est pas ouvert.
466 Le curseur doit être fermée avant la réouverture.
467 Résultat doit renvoyer un seul objet.
468 Aucun marqueur de position este permis dans les instructions EXECUTE IMMEDIATE.
469 Type de données utilisé non valid.
470 Requête implique trop d'articles pour l'optimiseur.
471 Mémoire épuisée dans la ligne %1$d du fichier %2$s.
472 Connecteur %1$s pour la base de données locale '%2$s' sur l'hôte '%3$s' est mort.
473 Rapporté à partir de la base de données locale '%1$s' sur l'hôte '%2$s': %3$s.
474 Erreur %1$s détectée pendant %2$s.
475 Aucune spécification de requête avec index %1$s.
476 LDB %1$s existe déjà..
477 Impossible de se connecter au maître de l'hôte %2$s pour accéder le LDB %1$s.
478 La base de données locale "%1$s" n'est pas un LDB enregistré.
479 La base de données locale "%1$s" est actuellement en cours d'accès par d'autres clients, la modification ou la suppression ne sont pas autorisées.
480 La base de données locale "%1$s" n'est pas vide et ne peut être supprimée.
481 "%1$s" n'est pas une classe proxy virtuel.
482 "%1$s" n'est pas une classe virtuelle.
483 "%1$s" n'est pas un attribut de la classe "%2$s".
484 Le object_id pour la classe proxy virtuelle "%1$s" est déjà défini.
485 Un ou plusieurs attributs d'object_id sont NULL pour l'instance de la classe proxy "%1$s".
486 Objet n'est pas une instance modifiable de classe virtuelle.
487 Objet n'est pas une instance.
488 Un attribut d'une classe ne peut pas être une classe virtuelle ou une classe proxy virtuelle.
489 Un attribut d'une classe proxy virtuelle ne peut pas être une classe, classe virtuelle, ou un objet.
490 La super-classe "%1$s" doit être du même type de classe comme "%2$s".
491 Impossible d'extraire nom de l'entité de la spécification de la requête proxy "%1$s".
492 %1$s
493 Syntaxe: %1$s %2$s
494 Sémantique: %1$s %2$s
495 Exécuter: %1$s %2$s
496 Clé inconnue référencé.
497 Valeur inconnue (oid) %1$d|%2$d|%3$d référencée.
498 Valeur en double (oid) %1$d|%2$d|%3$d référencée.
499 Clé NULL référencé.
500 Clé en double référencé.
501 Priorité de déclenchement non valide.
502 Classe cible manquant dans le déclencheur.
503 Déclencheur "%1$s" pas trouvé.
504 Erreur interne: traitement du déclencheur "%1$s".
505 Un déclencheur avec le nom "%1$s" existe déjà.
506 Un déclencheur peut pas être définie sur "%1$s" parce que c'est une classe virtuelle.
507 Classe cible "%1$s" inappropriée pour déclencheur.
508 Attribut cible "%1$s" inapproprié pour déclencheur.
509 Condition de déclencheur pas valide "%1$s".
510 Condition de déclencheur pas valide "%1$s".
511 Non autorisé à accéder le déclencheur "%1$s".
512 Non autorisé à supprimer le déclencheur "%1$s".
513 Non autorisé à mettre à jour le déclencheur "%1$s".
514 Non autorisé à modifier le déclencheur "%1$s".
515 L'heure d'exécution de l'action de déclencheur %2$s ne peut pas être antérieure à l'heure d'exécution de condition de %1$s.
516 La profondeur maximale %1$d de déclencheur dépassée au déclencheur "%2$s".
517 L'opération a été rejetée par le déclencheur "%1$s".
518 Erreur interne: traitement du déclencheur "%1$s".
519 Type interdit pour condition de déclenchement.
520 L'action de REJECT ne peut pas être utilisé avec un temps d'action de AFTER ou DEFERRED.
521 Le REJECT action ne peut pas être utilisé avec les événements ABORT ou TIMEOUT.
522 Expression de la condition de déclenchement n'a pas été donnée.
523 Expression d'action de déclenchement n'a pas été donnée.
524 L'activité spécifié ne peut pas être supprimée car elle est détenue par un autre utilisateur.
525 Erreur pendant la compilation de la condition pour "%1$s": %2$s
526 Erreur pendant la compilation de l'action pour "%1$s", %2$s
527 Erreur pendant l'évaluation de la condition pour "%1$s": %2$s
528 Erreur pendant l'évaluation de l'action pour "%1$s", %2$s
529 Transaction ne peut être enregistrée, il a été invalidé par le déclencheur "%1$s".
530 Expression régulière manquante.
531 Expression régulière est trop longue ou complexe.
532 Parenthèses ou crochets déséquilibrées dans l'expression régulière.
533 Référence est en dehors des limites dans l'expression régulière.
534 Caractère ou syntaxe interdits dans l'expression régulière.
535 Aucun texte donné pour la recherche avec expressions régulières.
536 Opcode interdit trouvé dans l'expression régulière.
537 Code de commande de recherche pour le compilateur d'expressions régulières est interdit.
538 Tampon d'expression régulière de travail doit être initialisé.
539 Tentative de division par zéro.
540 %1$s.
541 Nombre de pages libres pour le volume "%1$s" est inconsistent. %2$d et %3$d ont été trouvés selon les en-tête de volume et bitmap, respectivement.
542 Nombre de secteurs libres pour le volume "%1$s" est inconsistent. %2$d et %3$d ont été trouvés selon les en-tête de volume et bitmap, respectivement.
543 En-tête de disque pour le volume "%1$s" is inconsistent.
544 Erreur interne: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Clé B+tree %6$s pour OID d'ojet: %7$d|%8$d|%9$d est introuvable dans l'arbre B+: %10$d|%11$d|%12$d.
545 Erreur interne: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Clé et OID: %6$d|%7$d|%8$d dans l'arbre B+: %9$d|%10$d|%11$d sont incorrects. L'objet n'existe pas.
546 Erreur interne: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Attendre  %6$d valeurs d'entrée OID, mais %7$d ont été trouvés dans l'arbre B+: %8$d|%9$d|%10$d.
547 Version du serveur %1$s est différente de version du client %2$s.
548 Il n'y a pas un object_id défini pour la classe proxy "%1$s".
549 L'instance de proxy n'a pas été trouvé.
550 Nom de point de sauvegarde inconnu %1$s
551 Impossible de localiser le chemin du fichier d'information de volume "%1$s"... Continue la lecture des tables internes
552 Dépassement arithmétique de capacité négatif sur les dates.
553 Dépassement arithmétique de capacité négatif sur les temps.
$ LOADDB
554 Le chargeur est entré dans un état non valide. Pas d'autres opérations sont possibles.
555 Erreur d'allocation de mémoire lors du chargement de base de données.
556 Trop de valeurs fourni. S'attendant à %1$d.
557 Pour attribut %1$s de %2$s, type %3$s n'est pas permis dans le domaine de l'ensemble.
558 Pour attribut %1$s de %2$s, type attendu %3$s, avait reçu un ensemble constant.
559 Pour attribut %1$s de %2$s, type attendu %3$s, avait reçu %4$s.
560 Pour attribut %1$s de %2$s, domaine n'est pas suffisamment spécifique pour supporter\n des références des objets non qualifiés.
561 Ensembles imbriqués ne sont pas autorisés.
562 Classe de système %1$s ne peut pas être rempli en utilisant le chargeur de la base de données.
563 Référence à la classe interne %1$s convertie en NULL.
564 Violé la contrainte UNIQUE pour attribut %1$s of %2$s.
565 Méthode constructeur %1$s n'est pas défini sur la classe de %2$s.
566 Aucune classe spécifiée ou aucun attribut dans la classe.
567 Trop d'arguments de méthode de constructeur fourni. Attendu %1$d.
568 Paramètres de la méthode constructeur sont manquants. Attendu %1$d, trouvé %2$d.
569 Valeurs d'attributs sont manquants. Attendu %1$d, trouvé %2$d.
570 Impossible d'accéder au fichier de données Glo "%1$s".
571 Instance a déjà été créée par une référence avancée.\nCe n'est pas autorisé pour les instances créées à l'aide des méthodes de constructeur.
572 ERREUR INTERNE: ne peut pas construire une représentation du disque pour instance.
573 ERREUR INTERNE: Impossible d'insérer nouvelle instance dans la base de données.
574 ERREUR INTERNE: Impossible de mettre à jour une nouvelle instance dans la base de données.
575 Pour argument %1$d du constructeur %2$s, type attendu %3$s, reçu %4$s.
576 Référence à la classe %3$s n'est pas compatible avec le domaine %1$s de %2$s.
577 Pour argument %1$d du constructeur %2$s, domaine n'est pas suffisamment spécifique pour supporter les\n références des objets non qualifiés.
578 Référence à la classe %3$s n'est pas compatible avec le domaine d'argument %1$d \n du constructeur %2$s.
579 Domaine cible doit inclure le type "object" pour permettre les références aux classes.
580 Attribut %1$s n'est pas défini pour la classe %2$s.
581 Tentative de mettre à jour la base de données lors de mises à jour sont désactivées.
582 Buts de stockage inconnus %1$d. La plage valide est de %2$d à %3$d.
583 Essayer d'attribuer un numéro non valide (%1$d) de pages.
584 Erreur interne: Une boucle a été détectée dans la table de fichiers du %1$d en volume "%2$s". La boucle peut être causée par la page %3$d|%4$d.
585 Tas inconnu %1$s|%2$d|%3$d
586 Erreur interne: Impossible de mettre à jour l'en-tête du chaîne dans %1$d|%2$d pour tas %3$d dans volume "%4$s".
587 Enregistrements de volumes permanents ne sont pas triés dans votre "%1$s" volinfo fichier. Enregistrement %2$d: %3$d %4$s est hors séquence.
588 ERREUR INTERNE: Échec de l'assertion '%1$s'.
589 Nom d'utilisateur non valide "%1$s".
590 Le domaine "%1$s" d'attribut d'une classe ne peut pas être une \nclasse virtuelle ou une classe proxy virtuelle.
591 Le domaine "%1$s" d'attribut d'une classe proxy virtuelle \ndoit être une classe proxy virtuelle.
592 Le domaine "%1$s" d'attribut d'une classe proxy virtuelle \net la classe proxy virtuelle doit être sur la même base de données locale.
593 Une instance virtuelle non modifiable a été réutilisé et ne peut pas être reconstruit.
594 Une clause d'object_id doit être fournie sans l'aide d'attributs (%1$s) de types de données approximatifs (float ou double).
595 Le nom de base de données "%1$s" est trop long. Nom de la base doit être inférieure à %2$d caractères.
596 Les %1$d pages de l'espace temporaire total permis a été dépassée.
597 Nombre de pages pour le fichier de tas %1$d|%2$d|%3$d est inconsistent. \n%4$d et %5$d ont été trouvés selon la chaîne tas et le table des fichiers, respectivement.
598 Nombre de fichiers est inconsistent. %1$d ont été attendue, %2$d ont été trouvés
599 Un erreur E/S s'est produite pendant la synchronisation d'état de volume "%1$s".
600 Fonction "%1$s" n'est pas mis en œuvre sur le PC.
601 "%1$s".
602 Erreur dl: "%1$s" est défini plus d'une fois.
603 Erreur interne: la table des secteurs/pages pour le VFID %1$d|%2$d semble corrompu.
604 Erreur interne: Une ensemble d'allocation le VFID %1$d|%2$d(Volume "%3$s") semble inconsistent dans %4$d pages supprimés ou marquées comme supprimées et %5$d trous de page dans la table
605 Erreur interne: Nombre inconsistente des pages marquées comme supprimées pour le VFID %1$d|%2$d(Volume "%3$s"). Attendu %4$d, Trouvé %5$d.
606 Impossible de localiser odbc.ini avec chemin "%1$s".
607 Base de données "%1$s" n'a pas été trouvé dans odbc.ini.
608 Base de données "%1$s" n'a pas une spécification d'hôte dans le fichier odbc.ini.
609 Votre transaction ne peut pas être annulée car la journalisation a été ignoré. Votre base de données peut être corrompue.
610 Votre base de données est susceptible d'être corrompu car la journalisation a été désactivé pendant que votre base de données s'est plantagée.
611 %1$s ne peut pas être exécuté en mode client / serveur.
612 La journalisation peut pas être désactivé en ce moment car il ya des actions en attente de restaurer.
613 Dans la classe "%3$s", composante locale "%1$s" ne peut pas servir comme un substitut d'alias pour la classe "%2$s". Les domaines sont incompatibles.
614 Nombre des journaux actifs de transactions a été dépassé. Le nombre maximum souhaité est de %1$d.
615 Impossible d'établir le contact avec l'interface Winsock. Erreur Winsock: %1$d.
616 Impossible de déterminer le nom de la machine en utilisant l'interface Winsock. Erreur Winsock: %1$d.
617 Impossible de déterminer l'id de machine en utilisant l'interface Winsock. Il doit y avoir une enregistration dans la table de nom d'hôte pour cette machine. Erreur Winsock: %1$d.
618 Attribut n'est pas modifiable.
619 Mauvais jeux de caractères codés de source.
620 Mauvais codeset de destination.
621 Type de données non valide. Les arguments contiennent un type de données non valide.
622 Codesets incompatibles. Les codesets des arguments sont incompatibles.
623 Séquence d'échappement non valide. Une séquence d'échappement valide a provoqué une erreur dans le compilateur d'expressions régulières.
624 Caractère d'échappement non valid. Le caractère d'échappement est non NULL, avec la longueur supérieure à 1.
625 Erreur interne. Essayer de mettre à jour le modèle incorrecte d'information d'attributs %1$d|%2$d|%3$d avec l'objet d'instance %4$d|%5$d|%6$d.
626 Erreur interne. %1$d attributs demandés n'ont pas été trouvés.
627 Invalid opérande pour TRIM. L'opérande de TRIM a une longueur supérieure à 1.
628 Type de devises non valide: %1$d.
629 Pagesize %1$d n'est pas une puissance de 2 ou Pagesize est trop petit. Pagesize de %2$d est utilisé au lieu.
630 La conversion n'est pas supportée par %1$s.
631 Instruction SQL rompe contrainte NOT NULL.
632 %1$s n'est pas un volume de sauvegarde.
633 %1$s est une sauvegarde de base de données %2$s créé sur %3$s au lieu de donnée base de données %4$s créé sur %5$s
634 Un volume/fichier de base de données a été attendu dans la sauvegarde.
635 AVERTISSEMENT: Pool de %1$d tampons de journal semble très faible pour l'environnement actuel.\n  Il est possible que certains tampons ne sont pas libérés par le système en raison d'une ERREUR INCONNUE\n ou qu'il ya beaucoup de transactions concurrentes abandonnées dans le même temps. Le pool de mémoire tampon de journal est élargi avec %2$d plus tampons.
636 AVERTISSEMENT: Num_buffers %1$d  est trop petit. La valeur minimale de %2$d a été supposé.
637 Erreur de système: trop de mémoire libérée pour une mémoire tampon du journal. Nombre est mis à zéro.
638 Avertissment: Vidage d'un page de journal de transactions qui ne peuvent pas être mis à jour, pageid = %1$d
639 Erreur de système: Précédente page du journal de séquence %1$d n'est pas la page %2$d vidée en retard.
640 Erreur de système: un point de sauvegarde ne peut pas être ajoutée lorsque la transaction n'est pas active (par exemple, lors de l'enregistration ou l'anullation).
641 Une nom doit être donné pour un point de sauvegarde.
642 Erreur de système: Aucune opération supérieure active du système pour la transaction en cours.
643 Peut-être une erreur du système: Enregistrer / Annuler la transaction = %1$d (indice = %2$d) ayant opérations permanents attachées.\nAttachera ces opérations du système à la transaction
644 ERREUR FATALE DU JOURNAL: %1$s
645 ERREUR DU SYSTÈME: Etat = %1$s de la Transaction = %2$d (index = %3$d) \n n'est pas l'état correct pour appliquer des actions postpone/undo du client ... ignoré
646 SYSTEM ERROR: Il semble que le dossier d'équilibrage est manquant pour la journalisation logique rcvindex = %1$s\n. Sans cet enregistrement d'équilibrage, la récupération après plantage ne se fera pas correctement
647 Sauvegarde "%1$s" n'appartient pas à la base de données.
648 La sauvegarde est incompatible avec courant "%1$s" version "%2$s".
649 La précision fournie de %1$d est invalide, elle doit être supérieure à %2$d et pas plus de %3$d.
650 Impossible de créer assez des fils d'exécution pour 'max_clients=%1$d'.
651 Pas de mémoire virtuelle pour pré-allouer %1$d tampons de tri pour (chacun des) %2$d fil(s) d'exécution.
652 Mauvais offset %1$d a été donnée de partager l'enregistrement avec la longueur %2$d stocké sur fente %3$d
653 Mauvais offset %1$d ou longueur %2$d a été donnée à enlever une partie des données de l'enregistrement d'une longueur de %3$d stocké sur fente %4$d
654 Mauvais offset %1$d ou longueur %2$d a été donné à remplacer une partie des données de l'enregistrement d'une longueur de %3$d stocké sur fente %4$d
655 Impossible d'ouvrir le fichier de configuration du gestionnaire d'événements, "%1$s".
656 Avertissment: Id/nom null du fichier de configuration du gestionnaire d'événements, "%1$s", ligne %2$d
657 Avertissment: Nom de l'événement pas permis, "%1$s", dans le fichier de configuration du gestionnaire d'événements, "%2$s", ligne %3$d
658 Avertissment: id %1$d d événement pas dans la plage d'erreur %2$d <> %3$d, file "%4$s", ligne %5$d
659 Impossible d'accéder au processus de gestionnaire d'événements, "%1$s".
660 Impossible d'ouvrir/connecter le processus de gestionnaire d'événements, "%1$s".
661 Impossible d'écrire dans le processus de gestionnaire d'événements, "%1$s".
662 Échec d'initialisation du mécanisme de gestion des événements.
663 Avertissment: Ligne trop longue dans le fichier de configuration du gestionnaire d'événements, "%1$s", ligne %2$d, elle a été tronqué.
664 Mécanisme de gestion des événements a commencé.
665 Pipe de gestionnaire d'événements cassé, "%1$s"
666 Mécanisme de gestion des événements arrêté.
667 Impossible d'assortir la classe '%1$s' à la classe c++ correspondant.
668 Erreur interne: type de domaine '%1$d' inconnu.
669 Serveur a refusé la connexion du client: le nombre maximal des clients (%1$d) a été dépassé.
670 Opération aurait causé une ou plusieurs violations de contraintes uniques. INDEX %1$s%2$s ON CLASS %3$s%4$s. clé: %5$s%6$s.
671 Une erreur de lecture/écriture de réseau s'est produite pendant pour socket %1$d associé à votre transaction (indice %2$d, %3$s@%4$s|%5$d)
672 Socket %1$d associé à votre transaction (indice %2$d, %3$s@%4$s|%5$d) est fermé.
673 Votre connexion au serveur a été fermé parce que le serveur s'arrête immédiatement.
674 Il n'ya pas assez des fils d'exécution actifs de poursuivre l'exécution du serveur actuel. Nombre actuel des fils d'exécution actifs est de %1$d. Jusqu'à %2$d fils d'exécution peut être nécessaire pour l'exécution propre du serveur dans votre environnement. Transaction avec indice %3$d a expiré à poursuivre l'exécution du serveur.
675 Impossible de lire le fichier d'emplacement de la base de données, '%1$s'.
676 Base de données '%1$s' ne peut être trouvé dans le fichier d'emplacement de la base de données, '%2$s'
677 Impossible de se connecter au serveur de base de données '%1$s' sur l'hôte suivant (s): %2$s
678 Erreur interne. Module de redémarrage du client a reçu le nom du serveur de base de données NULL.
679 Cette transaction C++ a déjà commencé.
680 Tentative de dépasser le niveau maximal d'imbrication de transactions C++ (%1$d).
681 %1$s transaction a été tentée sur une transaction C++ qui n'a pas encore commencé.
682 Impossible de %1$s la transaction jusqu'à ce que toutes les transactions subordonnées sont terminées.
683 Tentative de création de chaîne avec une longueur interdite - %1$d
684 Méthode a retourné un code d'erreur %1$d défini par l'utilisateur.
685 Classe "%1$s" n'est pas valide.
686 Notification: le gestionnaire d'événements a été redémarré.
687 Non admis à effectuer une conversion de type de (%1$s *) à (%2$s *).
688 Indice hors limites. Tentative d'accès à l'élément %1$lld. Taille de la collection = %2$lld.
689 Erreur interne. Itérateur contient état corrompu.
690 Tentative de déréférencer une instance non initialisée de odb_Ref<>.
691 Tentative d'utilisation d'un itérateur invalide.
692 Tentative d'utilisation d'un itérateur avec une collection sans lien.
693 Dépassement s'est produite sur le type de données numérique.
694 ERREUR DU SYSTÈME: Impossible de charger arbre B+.
695 Interface incorrecte a été utilisée pour tuer un client.
696 Transaction inconnu (index %1$d, %2$s@%3$s|%4$d).
697 Indice fourni pour transaction (index %1$d, %2$s@%3$s|%4$d) ne correspond pas avec l'indice transaction en cours (indice %5$d, %6$s@%7$s|%8$d). Probablement la transaction fourni est terminée et son indice de transaction a été réattribué à une autre transaction.
698 Erreur interne: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). le nombre d'OID dans la hiérarchie unique: %6$d n'est pas égal au nombre d'OID: %7$d trouvé dans l'unique arbre B+, plus le nombre de valeurs NULL: %8$d trouvé dans la hiérarchie unique pour l'arbre B+: %9$d|%10$d|%11$d.
699 Erreur interne: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). le nombre d'OID dans la hiérarchie unique: %6$d n'est pas égal au nombre d'OID: %7$d trouvé dans les statistiques de racine d'unique arbre B+ pour l'arbre B+: %8$d|%9$d|%10$d.
700 Erreur interne: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). le nombre d'OID: %6$d n'est pas égal au nombre de valeurs NULL: %7$d plus le nombre de clés: %8$d dans l'arbre B+ unique: %9$d|%10$d|%11$d.
701 Pas de proxy est défini sur "%1$s" à ldb "%2$s".
702 Erreur interne: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d).  le nombre de valeurs NULL dans la hiérarchie unique: %6$d n'est pas égal au nombre de valeurs NULL: %7$d  trouvé dans les statistiques de racine d'arbre B+ pour l'arbre B+: %8$d|%9$d|%10$d.
703 Erreur interne: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). OID: %6$d|%7$d|%8$d  trouvé dans l'unique arbre B+: %9$d|%10$d|%11$d n'appartient pas à l'une des classes de la contrainte d'unicité.
704 L'évaluation de la fonction générique de serveur a échoué.
705 %1$d volumes inconsistents permanents ont été trouvés lors de  %2$d sont attendues.
706 Impossible d'agrandir les volumes permanents. Volume permanents "%1$s" avec but %2$s a été donné.
707 Impossible d'agrandir le volume temporaire "%1$s" avec %2$d pages
708 Impossible d'agrandir le volume temporaire de disque "%1$s" avec %2$d pages (%3$lld Ko) en raison d'un espace insuffisant. L'espace actuellement disponible est %4$d pages (%5$lld Ko).
709 Contrainte "%1$s" est introuvable.
710 Contrainte non valide.
711 Un objet avec la valeur d'attribut fourni n'existe pas.
712 Contrainte" %1$s" existe déjà.
713 Un index n'existe pas sur cet attribut.
714 La requête a échoué en raison d'espace insuffisant du fichier temporaire.
715 Impossible de vérifier le schéma: %1$s
716 Contraintes NOT NULL ne sont pas permises dans les classes virtuelles ou proxy.
717 Contraintes UNIQUE ne sont pas permises dans les classes virtuelles ou proxy.
718 Gadget invalid.
719 Le nombre d'attributs (%1$d) et des valeurs (%2$d) ne sont pas égaux.
720 Impossible d'utiliser un gadget à insérer dans une classe virtuelle ou proxy.
721 Requête d'insertion donne plus d'une ligne.
722 Nombre incorrect d'attributs pour contrainte NOT NULL.
723 Impossible de modifier le domaine d'un attribut existant.
724 Paramètre(s) invalid(s) pour la génération de nom pour défaut contrainte.
725 Erreur interne: OID temporaire attendu et OID permanente rencontré.
726 Contrainte UNIQUE doit être ajouté à l'aide d'une instruction de modification distincte\naprès les instances ont été créées. Par exemple, au lieu de:\n\talter table1 add attribute a int unique;\n utilise:\n\talter table1 add attribute a int;\n\talter table1 add unique(a);
727 Événement de déclencheur non valide.
728 La requête a échoué car vfid du fichier temporaire n'est pas valide pour la transaction %1$d.
729 Dépassement produite dans le contexte de soustraction.
730 Dépassement produite dans le contexte de multiplication.
731 Dépassement produite dans le contexte de division.
732 Dépassement s'est produit dans le contexte de moins unaire.
733 Dépassement s'est produit dans le contexte de coercition.
734 Erreur interne: En-tête du VFID du fichier %1$d|%2$d(Volume "%3$s") est corrompu.
735 Inconnu indice de transaction de %1$d.
736 Doit spécifier un service ou identificateur de port. Erreur de Winsock: %1$d.
737 Service inconnu pour TCP: %1$s. Erreur de Winsock: %2$d.
738 Impossible de créer un flux de socket. Erreur de Winsock: %1$d.
739 Impossible de lier l'adresse locale ... une nouvelle tentative. Erreur de Winsock: %1$d.
740 Impossible de lier l'adresse locale ... abandon.. Erreur de Winsock: %1$d.
741 Accept échoué. Erreur de Winsock: %1$d.
742 Envoi d'un message diffusé au client. Erreur de Winsock: %1$d.
743 Échec lors de la négociation entre le client et le serveur. (homologue hôte %1$s)
744 Erreur dans le re-hachage d'un MOP proxy/vclass.
745 Invalid phase de ramasse-miettes.
746 Invalid callback de ramasse-miettes.
747 Ticket invalid de callback de ramasse-miettes.
748 Pointeur d'objet invalid rencontré par db_gc_mark_object.
749 Le nombre d'instances extraites ne correspondent pas: %1$d attendues, %2$d reçues.
750 Le nombre d'instances extraites ne correspondent pas: %1$d attendues, %2$d reçues.
751 L'OID d'un nouveau objet n'est pas disponible dans un déclencheur BEFORE INSERT
752 Erreur pendant la restauration de sauvegarde unit_num %1$d.
753 Erreur pendant la restauration de sauvegarde unit_num %1$d, pageid %2$d dépasse nombre total de pages %3$d pour volid %4$d.
754 Pnstance proxy OO a un type non-chaîne ou clé nulle.
755 %1$s ne peut pas être exécuté en mode autonome.
756 Tampon de chaîne spécifiée est trop petite pour encoder cet objet.
757 Erreur interne: ne peut pas attribuer OID permanente au cours de coder objet.
758 Chaîne spécifiée n'est pas valide pour le décodage de l'objet.
759 L'objet cible a été supprimé.
760 Erreur interne: instance de l'objet proxy a la clé NULL au cours de coder objet.
761 Erreur interne: l'instance d'objet vclass correspond à une invalide instance réelle.
762 Erreur interne dans la résolution d'instance VClass à une instance réelle.
763 Erreur interne dans le codage d'une instance d'objet non modifiable.
764 Erreur interne dans le codage objet en forme vobj interne.
765 Argument d'objet de sortie a une adresse NULL.
766 Erreur interne dans le décodage de l'objet.
767 Impossible de créer une sauvegarde de niveau %1$d sans créer au préalable une sauvegarde de niveau %2$d.
768 La base de données locale "%1$s" a été consultée lors de cette session, la modification ou la chute est interdite.
769 Attribut "%1$s" du classe "%2$s" n'est pas une classe valide ou un attribut partagé.
770 Pas de fonctions de table.
771 Paramètre invalide pour la fonction.
772 db_serial du classe est introuvable.
773 Serial "%1$s" est introuvable.
774 Serial "%1$s" existe déjà.
775 La prochaine valeur dépasse la valeur MAX(MIN).
776 Impossible d'extraire l'objet de série.
777 Impossible de mettre à jour l'objet de série.
778 Impossible de créer un lien symbolique "%1$s" au fichier "%2$s".
779 Résultat dépasse la limite: année de la date DOIT être en 1 et 9999.
780 Défaillance interne du système: impossible pour obtenir le temps.
781 Chaîne de format est trop long. Il doit être inférieure à 16Ko.
782 Chaîne vide pas permis ici.
783 Format non valide.
784 Deux arguments sont incompatibles.
785 Chaîne du première argument est trop long. Il doit être inférieure à 16Ko.
786 Format est dupliqué
787 Erreur de conversion en format de l'heure.
788 Erreur de conversion en format d'horodatage.
789 Mauvais numéro.
790 Exécution de la requête a été interrompue par l'utilisateur.
791 Valeur de série incorrecte.
792 Impossible d'allouer de la mémoire pour css.
793 pthread_attr_init() a échoué.
794 pthread_attr_destroy() a échoué.
795 pthread_attr_setdetachstate() a échoué.
796 pthread_attr_setscope() a échoué.
797 pthread_attr_setstacksize() a échoué.
798 pthread_create() a échoué.
799 pthread_join() a échoué.
800 pthread_mutex_init() a échoué.
801 pthread_mutex_destroy() a échoué.
802 pthread_mutex_lock() a échoué.
803 pthread_mutex_trylock() a échoué.
804 pthread_mutex_unlock() a échoué.
805 pthread_mutexattr_init() a échoué.
806 pthread_mutexattr_destroy() a échoué.
807 pthread_mutexattr_settype() a échoué.
808 pthread_mutexattr_gettype() a échoué.
809 pthread_cond_init() a échoué.
810 pthread_cond_destroy() a échoué.
811 pthread_cond_wait() a échoué.
812 pthread_cond_timedwait() a échoué.
813 pthread_cond_signal() a échoué.
814 pthread_cond_broadcast() a échoué.
815 pthread_key_create() a échoué.
816 pthread_key_delete() a échoué.
817 pthread_setspecific() a échoué.
818 pthread_getspecific() a échoué.
819 pthread_once() a échoué.
820 Impossible d'initialiser une connexion.
821 Arrêt d'une connexion.
822 Opération de mutex a échoué pendat l'obtention d'Id suivant du client.
823 Opération de mutex a échoué pendat l'initialisation de la liste des connexions.
824 Impossible de détruire un mutex pendant la finalisation liste des connexions.
825 Appel de pread() a échoué.
826 Appel de pwrite() a échoué.
827 Fonction traversant de la liste des connexions retourne une valeur non valide.
828 Section critique non valide de l'indice a été donné.
829 Ce verrou de mutex a été déverrouillé déjà.
830 Impossible d'allouer entrée de requête. Nombre maximal d'entrées allouables est de %1$d.
831 Tous les descripteurs de transaction(%1$d) sont en cours d'utilisation. Augmente le paramètre max_clients.
832 Sauvegarde est déjà en cours d'exécution. Sauvegarde ne peut pas être exécuté par deux processus.
833 Le nom de l'index "%1$s" est ambigu. Spécifiez le nom de la classe de l'index.
834 Il ya quelques incohérences entre la chaîne source "%1$s" et la chaîne de format "%2$s" donnée dans to_number().
835 Objet de statistique pas defini.
836 LATCH ON PAGE(%1$d|%2$d) a expiré.
837 Impossible de supprimer l'utilisateur qui possède des objets de base de données(classe/verrou/serial etc). 
838  Impossible d'ouvrir plus de %1$d analyses d'index simultanément.
839 "%1$s": Paramètre de système inconnu ou mauvaise valeur.
840 Impossible de modifier le paramètres de système "%1$s".
841 Opération demandée ne peut être exécutée que par seul client (transaction).
842 Ne peut pas mettre en cache le verrou de l'objet %1$d|%2$d|%3$d.
843 Arguments erronés dans %1$s, un %2$s a été transmis.
844 Isolement inconnue(%1$d) est trouvé. L'indice transaction est %2$d.
845 Type d'objet non valide(%1$d) est trouvé. L'objet verrouillable est %2$d|%3$d|%4$d.
846 %1$s de tran(%2$d) ne se trouve pas dans la liste des détenteurs d'un objet(%3$d|%4$d|%5$d).
847 %1$s sur un %2$s(%3$d|%4$d|%5$d) ne se trouve pas dans la liste des détenteurs(cnt=%6$d) de la transaction(%7$d).
848 %1$s sur un objet(%2$d|%3$d|%4$d) ne se trouve pas dans la liste non2pl de la transaction(%5$d).
849 Essayer d'annuler une transaction(%1$d) deux fois.
850 Tran(%1$d) n'est pas un titulaire de verrou ni attendeur du verrou d'un objet(%2$d|%3$d|%4$d).
851 Impossible d'allouer la ressource de verrouillage (%1$s). 
852 Mode de détenteurs totals(%1$d) d'une liste vide des détenteurs de verrou n'est pas NULL_LOCK.
853 Pour un objet(%1$d|%2$d|%3$d), tran(%4$d) d'une lecture pas enregistrée a détenu un verrou de faute(%5$s).
854 ATTENTION: Titulaire de verrou manquant, mais il y a des attendeurs du verrou. Reportez-vous à %1$s.
855 ATTENTION: Deux ou plusieurs fils d'exécution sont en attente de verrouillage pour une transaction(%1$d).
856 ATTENTION: Etrange lockwait état(%1$d|%2$d), fil d'exécution(%3$d|%4$d) et transaction(%5$d).
857 Une erreur d'E/S s'est produite lors de tronquer la page %1$d de volume "%2$s".
858 Volume "%1$s" est inconnu au redémarrage du serveur.
859 LATCH ON PAGE(%1$d|%2$d) ANNULÉ
860 Impossible d'effectuer des transactions en cours dans le cadre d'une transaction globale, car son état est '%1$s'.
861 Impossible de préparer pour valider la transaction en cours avec identifiant de transaction %1$d car cette transaction n'est pas faite dans le cadre d'une transaction globale.
862 Impossible de définir l'information d'utilisateur de la transaction globale avec l'identifieur %1$d car son état est '%2$s'.
863 Impossible d'obtenir l'information d'utilisateur de la transaction globale avec l'identifieur %1$d parce qu'il n'est pas définie.
864 Impossible d'ouvrir fichier de backupdb/restoredb '%1$s'.
865 Impossible d'exécuter l'instruction en raison de marqueurs d'entrée de type inconnu.
866 Code d'erreur pas utilisé.
867 Code d'erreur pas utilisé.
868 Code d'erreur pas utilisé.
869 Code d'erreur pas utilisé.
870 Code d'erreur pas utilisé.
871 Code d'erreur pas utilisé.
872 Code d'erreur pas utilisé.
873 Erreur d'authentification PKI "%1$s".
874 Argument de power() est hors limites.
875 Dépassement produite dans le contexte de puissance.
876 Code d'erreur pas utilisé.
877 Pas de données d'être déchargés.
878 Fichier de placement de la base de données est invalid '%1$s'.
879 Impossible d'obtenir la permission: '%1$s'.
880 Impossible de modifier permission '%1$s'.
881 Impossible d'obtenir verrou pour fichier '%1$s(FILE ID: %2$d)'.
882 Impossible de libérer verrou pour fichier 'FILE ID: %1$d'.
883 Impossible d'obtenir l'état du fichier '%1$s'.
884 Volext-chemin '%1$s' n'est pas accessible en écriture.
885 Échec de déplacer '%1$s' en '%2$s'. Utilise le même périphérique (disque).
886 "%1$s" causé violation de contrainte unique.
887 La procédure stockée '%1$s' existe déjà.
888 Nombre de paramètres n'est pas valide. attendu: %1$d, effectif: %2$d
889 Erreur d'exécution de procédure stockée: %1$s
890 Partition a échoué.
891 Partition appropriée n'existe pas.
892 Clé primaire "%2$s" déjà définie pour la classe "%1$s".
893 Attribut '%1$s', membre de la clé primaire, ne pouvait pas être supprimée.
894 La procédure/fonction stockée '%1$s' n'existe pas.
895 Type incorrect du procédure stockée: '%1$s' est le '%2$s'.
896 Compression échoué. Méthode d'archivage Zip: %1$d (%2$s), Niveau Zip: %3$d (%4$s)
897 Décompression a échoué.
898 Erreur de réplication: "%1$s"
899 Demandes de partition non valides.
900 Bibliothèque Java VM est introuvable:: %1$s.
901 Machine virtuelle Java ne peut pas être démarré: %1$s.
902 Machine virtuelle Java ne fonctionne pas.
903 Impossible de connecter Java VM: %1$s
904 Nom incorrect du procédure stockée.
905 Mise en réseau avec JVM a échoué: %1$d
906 Invalid signature de méthode Java.
907 Le propriétaire ou un membre du groupe DBA peut chuter procédure stockée.
908 Trop d'arguments de procédure stockée '%1$s'.
909 Classe de catalogue/VClass est manquant ou invalide.
910 Accès à la partition n'es pas permis.
911 Appel invalide: il ne peut pas retourner un ResultSet.
912 ResultSet peut pas être utilisé comme paramètre d'entrée.
913 Un trop grand nombre d'appels de procédure stockée imbriquée.
914 Un objet serial existe déjà comme une contrainte d'incrémentation automatique.
915 Valeur d'augmentation d'une contrainte d'incrémentation automatique doit être supérieure à 0.
916 Le volume du journal actif de transactions n'est pas inclus dans ce volume de sauvegarde(%1$s).
917 Volume du journal actif de transactions existe déjà.
918 La classe '%1$s' refferencée par la clé étrangère n'existe pas.
919 La classe '%1$s' refferencée par la clé étrangère ne possède pas la clé primaire.
920 La clé étrangère '%1$s' ne comprend pas le membre de clé primaire '%2$s'.
921 Le domaine de l'élément '%1$s' de clé étrangère est différente de celui de l'élément '%2$s' de clé primaire.
922 La contrainte de la clé étrangère '%1$s' est invalide, en raison de la valeur de '%2$s'.
923 La clé primaire '%1$s' référencée par une clé étrangère '%2$s' n'est pas censé à être supprimé.
924 Opérations de UPDATE/DELETE sont limitées par la clé étrangère '%1$s'.
925 Acquisition de verrouillage pour la clé étrangère '%1$s' a échoué..
926 L'instance ayant la clé étrangère '%1$s' ne peut pas être supprimé.
927 Le nombre de clés de la clé étrangère '%1$s' est différent de celui de la clé primaire '%2$s'.
928 Pas un utilisateur, mais le système peut assigner l'attribut '%1$s' créé par 'ON OBJET CACHE' de la clé étrangère.
929 Une clé étrangère ne peut pas être définie dans la classe virtuelle.
930 Il est possible de déposer l'attribut '%1$s' créé par 'ON OBJET CACHE' de clé étrangère seulement après la suppression de la contrainte de la clé étrangère.
931 Valeur de départ d'une contrainte d'incrémentation automatique doit être inférieure à la valeur maximale.
932 Pas possible de réparer une incohérence du nombre de pages libres pour le volume "%1$s" parce que %2$d pages obtenues à partir de l'entête de volume sont incompatibles avec %3$d pages du bitmap.
933 Pas possible de réparer une incohérence du nombre de secteurs libres pour le volume "%1$s" parce que %2$d secteurs obtenues à partir de l'entête de volume sont incompatibles avec %3$d secteurs du bitmap.
934 Domaine nul référencé.
935 Argument de la fonction %1$s est hors limites.
936 Dépassement s'est produit dans le contexte exp.
937 Erreur de base de données.
938 Argument non valide.
939 Il ya trop de connexions.
940 Le handle n'est pas valid.
941 L'opération n'est pas pris en charge.
942 Dépassement du délai pour handle en attendant des verrous.
943 Erreur générique.
944 L'instruction n'est pas préparé.
945 L'instruction n'a aucun jeu de résultats.
946 L'instruction n'est pas exécuté.
947 Il n'existe pas plus des ResultSets.
948 Le taille du tampon n'est pas suffisante.
949 Il n'y a pas d'information disponible.
950 Le nom n'est pas valid.
951 Le ResultSet n'est pas modifiable.
952 La ligne est supprimée.
953 Paramètres de l'instruction préparée ne sont pas définis.
954 L'instruction n'est pas une instruction par lots.
955 Impossible d'effacer cette instruction par lots.
956 Impossible d'exécuter par lots.
957 L'instruction est une instruction préparée.
958 L'instruction n'est pas une instruction préparée.
959 L'instruction est une instruction par lots.
960 Il n'y a pas plus d'erreurs.
961 Il n'y a pas plus de résultats.
962 Mémoire épuisée.
963 Erreur de Broker.
964 Le handle du ResultSet est fermé.
965 Impossible d'utiliser des noms d'attributs en double dans la liste de clé d'index. Nom de l'attribut '%1$s' inscrit plus d'une fois.
966 Votre transaction (indice %1$d, %2$s@%3$s|%4$d) a expiré en attente sur le(s) verrou(s) %5$s sur l'objet %6$d|%7$d|%8$d en raison d'un interblocage. Vous êtes en attente pour utilisateur(s) %9$s à la fin.
967 Votre transaction (indice %1$d, %2$s@%3$s|%4$d) a expiré en attente sur le(s) verrou(s) %5$s sur la classe %6$s en raison d'un interblocage. Vous êtes en attente pour utilisateur(s) %7$s à la fin.
968 Votre transaction (indice %1$d, %2$s@%3$s|%4$d) a expiré en attente sur le(s) verrou(s) %5$s sur l'instance %6$d|%7$d|%8$d de la classe %9$s en raison d'un interblocage. Vous êtes en attente pour utilisateur(s) %10$s à la fin.
969 Plate-forme de bit de serveur (%1$d) est différente de la plate-forme de bit du client (%2$d).
970 Le mode HA du serveur est modifié de '%1$s' à '%2$s'.
971 Programme '%1$s' (pid %2$d) s'est connectée à le serveur de base de données '%3$s' sur l'hôte '%4$s' (port %5$d).
972 Programme '%1$s' (pid %2$d)a été connecté à partir de l'hôte '%3$s'. (indice de transaction %4$d)
973 L'état du serveur est %1$s.
974 Journal de transactions "%1$s" est créé pour la journalisation des pages à partir de %2$lld à %3$lld.
975 La version actuelle de la réplication ne permet pas de modifier plusieurs lignes avec une seule instruction UPDATE qui peuvent violer la contrainte UNIQUE.
976 Erreur interne: dépassement de la taille du tableau (taille allouée: %1$d, taille consultée: %2$d) à la page de fichier de table de %3$d|%4$d(volume "%5$s")
977 Point de contrôle commencé (id de la page précédente du point de contrôle: %1$lld, id de la page REDO précédente: %2$lld).
978 Checkpoint finished (checkpoint page id: %1$lld, checkpoint redo page id: %2$lld, flushed page count: %3$d).
979 Boucle CONNECT BY dans les données d'utilisateur.
980 Fil d'exécution est en attente en %1$s depuis plus de %2$d msec.
981 Transaction %1$d a atteint le seuil des statistiques de surveillance %2$s.
982 Interruption de la transaction %1$d.
983 Instances d'une classe OID réutilisable ne sont pas attribuables. Cette opération est interdite sur les non-attribuables instances.
984 Impossible de configurer %3$s à "%6$s" dans la section "[%5$s]" quand il est configuré sur "%4$s" dans la section "[%2$s]" du fichier de configuration "%1$s".
985 Le nom d'hôte sur la chaîne de connexion de base de données doit être spécifié lors de multihôte est configuré dans "databases.txt".
986 Fonction heartbeat du CUBRID commencée.
987 Fonction heartbeat du CUBRID arrêtée.
988 Événement de noeud: %1$s.
989 Événement de processus: %1$s. %2$s
990 Exécution de la commande: %1$s. %2$s
991 Candidats pour victimes de FLUSH de mémoire tampon des pages sont commencés.
992 Candidats pour victimes de FLUSH de mémoire tampon des pages sont finis. (nombre: %1$d).
993 Un dépassement s'est produit dans le contexte d'opération de bits.
994 Cette instruction ne peut pas être une instruction préparée.
995 Une instruction préparée avec le nom %1$s n'existe pas.
996 Nombre incorrect de valeurs données. %1$d  valeurs ont été donnés, mais %2$d valeurs sont nécessaires.
997 Impossible d'ajouter la contrainte de clé étrangère %1$s à le %2$s partitionné. Les actions référentielles que les attributs de mise à jour ne sont pas encore pris en charge sur les classes partitionnées.
998 Impossible d'ajouter la contrainte de clé étrangère %1$s à l'attribut %2$s. L'attribut ne peut pas avoir de contrainte NOT NULL, car il doit correspondre à l'action SET NULL référentielle.
999 Impossible de créer la '%1$s' comme la table '%2$s' parce que la table existante possède des fonctionnalités qui ne seront pas dupliqués (%3$s).
1000 '%1$s' est un attribut SHARED (partagée) ou du CLASS mais seuls les attributs normaux sont permises dans ce contexte.
1001 Une situation ou état inattendu a été atteinte. '%1$s'
1002 La transaction actuelle ne pouvait pas acquérir un verrou de base de données dont elle a besoin pour poursuivre le traitement.
1003 La contrainte '%1$s' existe mais a un type différent.
1004 S'il vous plaît noter que lorsque vous utilisez ALTER TABLE DROP FOREIGN KEY, CUBRID supprimet la contrainte et son index associé.
1005 Code d'erreur pas utilisé.
1006 Code d'erreur pas utilisé.
1007 Impossible de créer un index avec longueur du préfixe sur contraintes unique, de clé étrangère ou de clé primaire.
1008 Impossible de créer un index avec longueur du préfixe sur la classe partitionné.
1009 COMPACTDB déjà commencé.
1010 Impossible de définir avec longueur du préfixe sur le domaine "%1$s".
1011 Impossible d'ajouter la contrainte %1$s de clé étrangère sur l'attribut partagée %2$s.
1012 Impossible de créer un tableau avec un type de colonne NULL.
1013 Réglage de DEFAULT ou SHARED sur colonne typée %1$s est interdit.
1014 Réglage de NOT NULL sur la colonne typée %1$s n'est pas autorisée.
1015 Répertoire spécifié '%1$s' n'existe pas.
1016 %1$s erreur de stockage externe: %2$s
1017 Chemin pour stockage externe '%1$s' n'est pas valide.
1018 Impossible de copier le fichier externe sur le type différent de stockage. (source %1$s dest %2$s)
1019 Stockage externe n'est pas initialisé parce que le chemin n'est pas spécifié dans "databases.txt".
1020 Fichier externe "%1$s" n'a pas été trouvé.
1021 Un cycle d'interblocage est détecté. %1$s.
1022 Valeur non valide "%1$d" pour la longueur du préfixe de l'index.
1023 Code d'erreur réservé pour HA.
1024 log writer: failed to get log page(s) starting from page id %1$lld.
1025 HA delay info: The replication delay (%1$d secs) has exceeded the limit (%2$d secs).
1026 HA delay info: The replication delay (%1$d secs) is decreased below the acceptable level (%2$d secs).
1027 Applicateur du journal: pas réussi à changer l'état d'application '%1$s' à '%2$s'.
1028 Applicateur du journal: enregistrement EOF inattendu dans le journal de transactions. LSA: %1$lld|%2$d.
1029 Applicateur du journal: invalide la page du journal de réplication. page HDR: %1$1lld|%2$d, finale: %3$lld|%4$d, LSA annexé: %5$lld|%6$d, LSA EOF: %7$lld|%8$d, l'état des fichiers ha: %9$d, est en fin de journal (end-of-log): %10$d.
1030 Applicateur du journal: enregistrement invalide de réplication. LSA: %1$lld|%2$d, forw LSA: %3$lld|%4$d, backw LSA: %5$lld|%6$d, Trid: %7$d, prev tran LSA: %8$lld|%9$d, type: %10$d.
1031 log applier: failed to apply schema replication log. class: "%1$s", schema: "%2$s", server error: %3$d, %4$s.
1032 log applier: failed to apply insert replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1033 log applier: failed to apply update replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1034 log applier: failed to apply delete replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1035 Applicateur du journal: taille de la mémoire(%1$d Mo) d'applicateur de journal est supérieure à la taille maximale de la mémoire (%2$d Mo) ou a été augmenter de plus de 2 fois (%3$d Mo). LSA nécessaire: %4$lld|%5$d. Dernière LSA enregistré: %6$lld|%7$d.
1036 Applicateur du journal: applicateur du journal s'est arrêté par un signal.
1037 Écrivain du journal: écrivain du journal s'est arrêté par un signal.
1038 Applicateur du journal: applicateur du journal a commencé. LSA requis: %1$lld|%2$d. Dernière LSA enregistré: %3$lld|%4$d. Dernière LSA repl enregistré: %5$lld|%6$d.
1039 Écrivain du journal: écrivain du journal a commencé. Mode: %1$d.
1040 Générique HA: %1$s. 
1041 Une chaîne a été tronquée dans le contexte de "%1$s".
1042 Tentative de créer une chaîne de %1$d octets de mémoire, alors que le maximum autorisé est de %2$d octets.
1043 Impossible de modifier l'attribut "%1$s". CUBRID ne peut pas modifier les propriétés de l'attribut demandé si la classe possède des sous-classes.
1044 Impossible de modifier l'attribut "%1$s". CUBRID ne peut pas changer les attributs qui sont des clés pour le partitionnement.
1045 Impossible de modifier l'attribut "%1$s". CUBRID ne peut pas modifier les attributs utilisés dans les clés étrangères (soit référencée ou de référencement).
1046 Impossible de modifier l'attribut "%1$s". CUBRID ne peut pas changer de domaine de l'attribut. Ce changement de domaine n'est pas pris en charge.
1047 Impossible de modifier l'attribut "%1$s". CUBRID ne peut pas changer de domaine de l'attribut. Le nouveau domaine peut ne pas représenter correctement toutes les valeurs d'attributs existants.
1048 Impossible de modifier l'attribut "%1$s". CUBRID ne peut actuellement pas changer l'AUTO_INCREMENT d'un attribut si le domaine de l'attribut change aussi.
1049 Impossible de modifier l'attribut "%1$s". CUBRID ne peut actuellement pas changer de domaine de l'attribut. La conversion de domaine est possible, mais cette fonction est interdit par la configuration.
1050 Impossible de modifier l'attribut "%1$s". CUBRID ne peut actuellement pas changer de domaine de l'attribut. L'attribut possède un index sur lui-même.
1051 Impossible de modifier l'attribut "%1$s". CUBRID ne peut actuellement pas changer l'attribut à ajouter une contrainte. Il est possible que les valeurs des attributs existants ne satisfont pas la contrainte.
1052 Attribut "%1$s" n'a pas changé. CUBRID n'a pas détecté de changements réels dans la représentation de l'attribut.
1053 Les instructions REPLACE INTO... et INSERT ... ON DUPLICATE KEY UPDATE ne sont pas autorisés pour les classes qui font partie d'une chaîne d'héritage.
1054 La nouvelle valeur de la contrainte d'incrémentation automatique doit être supérieure ou égale à la valeur courante.
1055 La nouvelle valeur d'une contrainte d'incrémentation automatique doit être inférieure à la valeur maximale.
1056 Pour éviter toute ambiguïté, l'option AUTO_INCREMENT de la table nécessite que la table possède exactement une colonne de type AUTO_INCREMENT et aucune spécification de graine/incrément.
1057 Impossible d'ajouter un attribut '%1$s' de type NOT NULL typé sans valeur par défaut.
1058 ALTER TABLE .. CHANGE : changer à nouveau domaine : cast échoué, la configuration actuelle ne permet pas la troncature ou de dépassement.
1059 ALTER TABLE .. CHANGE : changer à nouveau domaine : cast échoué, valeur par défaut de nouveau domaine was set.
1060 ALTER TABLE .. CHANGE : changer à nouveau domaine : cast échoué, valeur minimale de nouveau domaine was set.
1061 ALTER TABLE .. CHANGE : changer à nouveau domaine : cast échoué, valeur maximale de nouveau domaine was set.
1062 Impossible de modifier l'attribut "%1$s". Le changement se traduirait par plusieurs clés primaires étant définis.
1063 Impossible d'ajouter la contrainte NOT NULL pour l'attribut "%1$s": il ya des valeurs NULL pour cet attribut.
1064 ALTER TABLE .. CHANGE : adding NOT NULL : toutes les valeurs NULL existantes ont été modifiées au défaut de domaine.
1065 Impossible de modifier l'attribut "%1$s". CUBRID ne pouvez pas ajouter de contrainte NOT NULL, une valeur par défaut n'existe pas pour le nouveau domaine.
1066 La session est expirée.
1067 Colonne '%1$s' ne peut pas avoir de contrainte NOT NULL et une valeur par défaut NULL.
1068 Profondeur maximale de déclenchement ne peut pas dépasser %1$d.
1069 Trop des instructions préparées.
1070 Variable de session '@%1$s' n'est pas défini.
1071 Variables de session trop nombreux.
1072 Adresse IP (%1$s) n'est pas autorisée.
1073 Format non valide dans le fichier(%1$s).
1074 Impossible de lire le fichier de liste d'accès(%1$s).
1075 Scan d'index décroissant abandonnée en raison de moindre priorité d'arbre B+ avec identifiant d'index: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
1076 Impossible de charger le paramètre du système.
1077 Le paramètre '%1$s' à la ligne %2$d dans le fichier '%3$s' : paramètre inconnu.
1078 Caractère au offset %1$d n'est pas valide avec jeu de caractères codés actuelle.
1079 Jeux de codes de base de données(%1$s) et la configuration du serveur(%2$s) sont incompatibles.
1080 La longueur maximale de la chaîne de filtration avec prédicat doit être %1$d.
1081 Impossible de modifier un attribut impliqué dans un index de filtration ou de fonction.
1082 Tous les tampons de pages sont sales (LRU: %1$d).
1083 Des informations spécifiques sur interblocage.\n%1$s
1084 Passer page invalide dans point de contrôle. (page id: %1$d, "%2$s", oldest_unflush_lsa: %3$lld|%4$d, lsa du point de contrôle REDO précédent: %5$lld|%6$d)
1085 Impossible de compiler expression régulière: '%1$s'
1086 Impossible d'exécuter expression régulière: '%1$s'
1087 Journal actif de sauvegarde '%1$s' a commencé.
1088 Journal actif de sauvegarde '%1$s' est terminé.
1089 L'allocation des ressources a échoué. Augmentez 'max_bestspace_entries=%1$d'.
1090 Initialisation de paramètres régionaux: %1$s.
1091 Génération de paramètres régionaux: %1$s.
1092 Impossible de préparer instruction de type '%1$s' avec des variables d'hôte.
1093 Le arg '%1$s' pour inet_aton n'est pas une chaîne valide formatée pour une adresse IPv4.
1094 Le arg '%1$lld' pour inet_ntoa n'est pas un nombre valid formaté pour une adresse IPv4.
1095 Nom d'utilisateur est trop long.
1096 La modification de schéma de partitionnement n'est pas autorisée lorsque la classe partitionnée est référencée par une clé étrangère.
1097 Colonne '%1$s' ne peut pas avoir une contrainte PRIMARY KEY et une valeur par défaut NULL.
1098 Paramètres régionaux pour la langue '%1$s' ne sont pas disponibles avec jeux de caractères codés '%2$s'.
1099 Instruction DDL n'est pas autorisé par la configuration (block_ddl_statement=yes).\n Texte SQL: %1$s
1100 Instruction sans clause WHERE est interdit par la configuration (block_nowhere_statement=yes).
1101 Code d'erreur pas utilisé.
1102 Paramètre du système inconnu ou mauvaise valeur.
1103 Impossible de changer le paramètre du système.
1104 Requête lente (%1$d msec)\n%2$s
1105 Erreur interne: partiellement échoué à vider objet sale.
1106 Plusieurs lignes dans la table source correspond à la même ligne dans la table de destination.
1107 Numéro pas valide de seau pour NTILE. Doit être supérieur à zéro et pas plus de 2.147.483.647.
1108 Numéro pas valide de seau pour WIDTH_BUCKET. Doit être supérieur à zéro et pas plus de 2.147.483.647.
1109 Valeur incorrecte pour la définition de la partition.
1110 Espace insuffisant dans la page d'une feuille de B-arbre. (longueur de la clé: %1$d)\n%2$s
1111 Valeur résultat est tronqué, car la précision(%1$d) est inférieure à sa longueur(%2$d).
1112 Un caractère ne peut pas être converti à partir de '%1$s' à '%2$s'.
1113 Indice d'élément hors de portée..
1114 Argument '%1$s' est hors de portée..
1115 Commencé à mettre à jour les statistiques (classe "%1$s", oid : %2$d|%3$d|%4$d).
1116  Fini de mettre à jour les statistiques (classe "%1$s", oid : %2$d|%3$d|%4$d, code d'erreur : %5$d).
1117 Impossible de modifier l'attribut "%1$s". CUBRID ne peut pas changer un attribut à SHARED(partagée) et vice versa.
1118 L'argument de "%1$s" ne peut pas être forcé au domaine souhaité "%2$s".
1119 Erreur interne: ID de page %1$d est allouée selon la carte d'allocation du volume "%2$s", mais il n'appartient pas à un fichier. 
1120 Erreur interne: ID de page %1$d du volume "%2$s" est actuellement utilisé, mais il n'est pas alloué selon la carte d'allocation du volume.
1121 Contraintes NOT NULL ne permettent pas la valeur NULL.
1122 Délai d'attente en réception de données.
1123 pthread_cond_timedwait() a expiré. 
1124 Erreur d'exécution de requête. ERROR_CODE = %1$d, /* SQL_ID: %2$s */ %3$s
1125 Création du fichier de clé de débordement. INDEX %1$s%2$s ON CLASS %3$s%4$s. clé: %5$s%6$s.
1126 Création d'une nouvelle page de débordement. INDEX %1$s%2$s ON CLASS %3$s%4$s. clé: %5$s%6$s.
1127 Suppression d'une page de débordement vide. INDEX %1$s%2$s ON CLASS %3$s%4$s. clé: %5$s%6$s.
1128 Reprise du journal est démarré. Le nombre d'enregistrements de journal à appliquer: %1$lld. Page du jurnal: %2$lld ~ %3$lld.
1129 Reprise du journal est finie.
1130 Transaction est abandonnée parce que nombre de verrous de la transaction dépasse %1$d.
1131 Erreur interne: La page %1$d du volume "%2$s" peut être corrompue.\n%3$s
1132 Encryption library failure: %1$s
1133 Copylogdb pour %1$s existe déjà.
1134 Applylogdb pour %1$s existe déjà.
1135 Un lien précédent de l'arbre B+(%1$s) est corrompu: vpid = (%2$d, %3$d)
1136 Un lien précédent de l'arbre B+(%1$s) est réparé: vpid = (%2$d, %3$d)
1137 La requête a été rejetée en raison de tenter de dépasser le niveau maximal autorisé de nidification(%1$d).
1138 Erreur de prise de contact (hôte homologue de %1$s): interruptibilité incompatible. (client: %2$s, serveur: %3$s)
1139 Erreur de prise de contact (hôte homologue de %1$s): mode de lecture/écriture incompatible. (client: %2$s, serveur: %3$s)
1140 Erreur de prise de contact (hôte homologue de %1$s): réplication HA retardé.
1141 Erreur de prise de contact (hôte homologue de %1$s): réplique-seul client à serveur non-réplique.
1142 Erreur de prise de contact (hôte homologue de %1$s): version du serveur non identifiée.
1143 Erreur de prise de contact (hôte homologue de %1$s): l'accès à distance au serveur n'est pas autorisé.
1144 Expiré tentative de connexion à %1$s. (délai: %2$d sec(s))
1145 Modification du propriétaire d'une classe de système n'est pas autorisé.
1146 Identificateur de volume %1$d n'existe pas.
1147 La taille de XASL flux cache(%1$d) dépasse le maximum(%2$d). Nouvelle entrée est ajoutée bien que rien est victime parce que toutes les entrées du cache sont utilisées.
1148 La taille de filtre prédicat flux cache(%1$d) dépasse le maximum(%2$d). Nouvelle entrée est ajoutée bien que rien est victime parce que toutes les entrées du cache sont utilisées.
1149 Impossible de trouver la page %1$d du volume %2$d.
1150 Contexte oblige à avoir classements compatibles.
1151 The page %1$d of volume %2$d is not a slotted page.
1152 The owner or a member of DBA group is only allowed to kill the transaction (%1$d).
1153 Attempt to use 'zero date'.
1154 MVCC row %1$d|%2$d|%3$d does not satisfies reevaluation.
1155 MVCC invalid row for delete: %1$d|%2$d|%3$d.
1156 MVCC can't get snapshot
1157 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.
1158 Serializable conflict due to concurent updates
1159 Dernière erreur

$set 6 MSGCAT_SET_INTERNAL
1 Erreur dans le sous-système d'erreur (ligne %1$d):
2 Aucun message d'erreur disponible.
3 Impossible d'allouer %1$d octets.
4 Impossible de trouver le catalogue de messages "%1$s".
5 Message manquant pour le code d'erreur %1$d.
6 ... ABANDONER/QUITTER IMMÉDIATEMENT ...\n
7 Voulez-vous quitter? 1/0
8 Fichier inconnu.
9 Pas de message disponible, ce message a été remplacé par le message erroné intial.
10 er_init: *** Valeur incorrecte de exit_ask = %1$d; assumera %2$s au lieu. ***\n
11 er_start: *** WARNING: Impossible d'ouvrir le journal de messages "%1$s"; assumera stderr au lieu. ***\n
12 er_study_fmt: Message bizarre pour le code d'erreur %1$d.
13 er_estimate_size: code de conversion inconnu (%1$d).
14 \n\n*** Enveloppement  journal de messages. Messages continuera à la partie supérieure du fichier. ***\n\n
15 \nTemps: %1$s - %2$s *** %3$s CODE = %4$d, Tran = %5$d%6$s\n%7$s\n
16 CUBRID (pid: %1$d) *** %2$s *** %3$s CODE = %4$d, Tran = %5$d %6$s
17 \nTemps: %1$s - %2$s *** file %3$s, ligne %4$d %5$s CODE = %6$d Tran = %7$d%8$s\n%9$s\n
18 CUBRID (pid: %1$d) *** %2$s *** fichier %3$s, ligne %4$d, %5$s CODE = %6$d, Tran = %7$d. %8$s
19 \n*** Le message d'erreur précédent est le dernier. ***\n\n
20 \nTemps: %1$s - DEBUG *** file %2$s, ligne %3$d\n
21 Mail -s "Le serveur a été arrêté"
22 --->>>\n %1$s a été arrêté à la demande lors de l'erreur suivante a été fixé:\n errid = %2$d, %3$s\nUtilisateur: %4$s, pid: %5$d, hôte: %6$s, temps: %7$s<<<---\n
23 %1$s a été arrêté avec errid = %2$d. Utilisateur: %3$s, pid: %4$d
24 er_init: ne peut pas installer de gestionnaire d'événements "%1$s"\n

$set 7 MSGCAT_SET_PARSER_SYNTAX
1 s'il vous plaît vérifier la syntaxe dans '%1$s', s'attendant à %2$s.
2 s'il vous plaît vérifier la syntaxe dans '%1$s', s'attendant à %2$s en %3$s.
3 s'il vous plaît vérifier l'exactitude de token '%1$s'.
4 'read uncommitted schema' n'est pas acceptable, en utilisant \n'read committed schema' au lieu.
5 Longueur maximale de chaînes de caractères est 16383.
6 %1$s dépasse la limite de %2$lld.
7 Longueur maximale de chaînes de bits est %1$d bits.
8 Ne peut utiliser une sous-requête dans la clause de condition de jointure.
9 Puis spécifiez pas l'attribut "(%1$s)" à l'intérieur de la clause de mise à jour les privilège de l'instruction grant/revoke.
10 Utilisation incorrecte du mot-clé '%1$s', s'attendent à %2$s.
11 Longueur de token est trop long.
12 Type de données non valide
13 instruction non valide: attach statement\n  attach_statement: ATTACH unsigned_integer
14 instruction non valide: prepare statement\n  prepare_statement:  PREPARE {to} COMMIT unsigned_integer
15 instruction non valide: execute statement\n  execute_statement:  EXECUTE DEFERRED TRIGGER [identifier {, identifier}* | ALL TRIGGERS]
16 instruction non valide: scope statement\n  scope_statement: SCOPE trigger_action { FROM table_spec_list }
17 instruction non valide: set system parameter\n  set_system: SET SYSTEM PARAMETERS 'STRING' {, 'STRING_N'}*
18 instruction non valide: set transaction\n  set_transaction: SET TRANSACTION ISOLATION LEVEL expression {, expression}\n                 | SET TRANSACTION LOCK TIMEOUT timeout_spec
19 instruction non valide: set trigger trace\n  set_trigger: SET TRIGGER TRACE [ON|OFF|unsigned_integer|parameter|host_param]
20 instruction non valide: set trigger depth\n  set_trigger: SET TRIGGER {MAXIMUM} DEPTH [unsigned_integer|parameter|host_param]
21 instruction non valide: set optimization level\n  SET OPTIMIZATION LEVEL {=|to} [ON|OFF|unsigned_integer|parameter|host_param]
22 instruction non valide: set optimization cost\n  SET OPTIMIZATION COST {of} 'STRING' {=|to} literal_
23 instruction non valide: get optimization level\n  GET OPTIMIZATION LEVEL {[INTO|TO] [host_param|parameter|identifier]}
24 instruction non valide: get optimization cost\n  GET OPTIMIZATION COST {of} 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
25 instruction non valide: get transaction isol\n  GET TRANSACTION ISOL LEVEL {[INTO|TO] [host_param|parameter|identifier]}
26 instruction non valide: get transaction lock\n  GET TRANSACTION LOCK TIMEOUT {[INTO|TO] [host_param|parameter|identifier]}
27 instruction non valide: get trigger trace\n  GET TRIGGER TRACE {[INTO|TO] [host_param|parameter|identifier]}
28 instruction non valide: get trigger depth\n  GET TRIGGER {MAXIMUM} DEPTH {[INTO|TO] [host_param|parameter|identifier]}
29 instruction non valide: get stat\n  GET STATISTICS 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
30 instruction non valide: create index\n  CREATE {REVERSE} {UNIQUE} INDEX identifier ON {only} class_name index_column_name_list {WHERE where_clause}
31 instruction non valide: create user\n  CREATE USER identifier {PASSWORD 'STRING'} {GROUPS identifier {, identifier}* } {MEMBERS identifier {, identifier}* }
32 instruction non valide: create trigger\n  CREATE TRIGGER identifier {STATE [ACTIVE|INACTIVE]} {PRIORITY unsigned_real}\n    [BEFORE|AFTER|DEFERRED] event_spec {IF trigger_condition}\n    EXECUTE {AFTER|DEFERRED} trigger_action
33 instruction non valide: create serial\n  CREATE SERIAL identifier {START WITH integer} {INCREMENT BY integer}\n    serial_min_max {CYCLE|NOCYCLE} {CACHE unsigned_integer|NOCACHE}
34 instruction non valide: create procedure\n  CREATE PROCEDURE identifier '(' {sp_param_list} ')' {IS|AS} LANGUAGE JAVA NAME 'STRING'
35 instruction non valide: create function\n  CREATE FUNCTION identifier '(' {sp_param_list} ')' RETURN {data_type|CURSOR} {IS|AS} LANGUAGE JAVA NAME 'STRING'
36 instruction non valide: groups\n  groups spec: GROUPS identifier {, identifier}*
37 instruction non valide: members\n  member spec: MEMBERS identifier {, identifier}*
38 instruction non valide: password\n  password spec: PASSWORD 'STRING'
39 manquant liste des commandes autorisées\n  commande autorisée = SELECT, INSERT, INDEX, DELETE, UPDATE, ALTER, ADD, DROP, EXECUTE, REFERENCES, ALL PRIVILEGES, ALL 
40 liste de spec des classes manquante
41 liste d'identificateurs manquante
42 expression manquante
43 identifiant invalide
44 utilisation non valide de substring\n  SUBSTRING( expression FROM expression FOR expression )\n  SUBSTRING( expression FROM expression )
45 utilisation non valide de trim\n  TRIM ( [LEADING|TRAILING|BOTH] expression FROM expression )\n  TRIM ( [LEADING|TRAILING|BOTH] FROM expression )\n  TRIM ( expression FROM expression )\n  TRIM ( expression )
46 utilisation non valide de cast\n  CAST ( expression AS data_type )
47 fonction add_months a besoin de deux paramètres
48 octet_length a besoin d'un paramètre.
49 bit_length a besoin d'un paramètre.
50 lower needs a besoin d'un paramètre.
51 upper needs a besoin d'un paramètre.
52 translate a besoin de trois paramètres
53 replace a besoin de deux ou trois paramètres
54 utilisation non valide de to_number\n  TO_NUMBER ( expression , char_string )\n  TO_NUMBER ( expression )
55 opérateur de liaison des chaînes n'est pas '|' mais '||'
56 instruction non valide: alter statement
57 instruction non valide: drop statement
58 instruction non valide: create statement
59 opérateur relationnel invalid
60 opérateur d'égalité n'est pas '==' mais '='
61 opérateur d'inégalité n'est pas '!=' mais '<>'
62 minus invalid (ou minus dans la liste)
63 règle de référence dupliquée
64 SYS_CONNECT_BY_PATH a besoin de deux paramètres et la seconde doit être une chaîne littérale.
65 nombre entier non signé invalid. %1$s.
66 IF a besoin de trois paramètres: le premier doit être une expression logique, le deuxième et le troisième doivent être des expressions évaluant soit à un type numérique ou chaîne.
67 IFNULL besoin de deux paramètres qui doivent être des expressions évaluant soit à un type numérique ou chaîne.
68 utilisation non valide de isnull\n  ISNULL ( expression )
69 utilisation non valide de left\n  LEFT ( str , len )
70 utilisation non valide de right\n  RIGHT ( str , len )
71 utilisation non valide de mod\n MOD( expression, expression )
72 utilisation non valide de adddate\n ADDDATE( expression, expression )
73 utilisation non valide de date_add\n DATE_ADD( expression, INTERVAL expression unit)
74 utilisation non valide de subdate\n SUBDATE( expression, expression )
75 utilisation non valide de date_sub\n DATE_SUB( expression, INTERVAL expression unit)
76 utilisation non valide de user / system_user\n USER() / SYSTEM_USER()
77 utilisation non valide de timestamp\n TIMESTAMP( expression ), TIMESTAMP( expression, expression )
78 utilisation non valide de database\n DATABASE()
79 utilisation non valide de current_time\n CURRENT_TIME()
80 utilisation non valide de current_date\n CURRENT_DATE()
81 utilisation non valide de current_timestamp\n CURRENT_TIMESTAMP()
82 utilisation non valide de now\n NOW()
83 utilisation non valide de default\n DEFAULT(column_name)
84 utilisation non valide de date\n DATE(expression)
85 utilisation non valide de localtime\n LOCALTIME()
86 utilisation non valide de localtimestamp\n LOCALTIMESTAMP()
87 utilisation non valide de schema\n SCHEMA()
88 utilisation non valide de truncate\n TRUNCATE( expression, expression )
89 utilisation non valide de str_to_date\n STR_TO_DATE( expression, string_literal )
90 utilisation non valide de current_datetime\n CURRENT_DATETIME()
91 utilisation non valide de insert\n INSERT( expression, expression , expression, expression )
92 utilisation non valide de elt\n ELT(expression [, expression]...)
93 utilisation non valide de year\n YEAR( expression )
94 utilisation non valide de month\n MONTH( expression )
95 utilisation non valide de day\n DAY( expression )
96 utilisation non valide de hour\n HOUR( expression )
97 utilisation non valide de minute\n MINUTE( expression )
98 utilisation non valide de second\n SECOND( expression )
99 utilisation non valide de quarter\n QUARTER( expression )
100 utilisation non valide de week\n WEEK( expression ), WEEK( expression, expression)
101 utilisation non valide de time\n TIME(expression)
102 utilisation non valide de group_concat\n GROUP_CONCAT (([DISTINCT] {col | expr} \n [ORDER BY {col | unsigned_int} [ASC | DESC] ] \n [SEPARATOR str_val])
103 utilisation non valide de index hint
104 La longueur maximale des chaînes d'octets est %1$d octets.
105 Erreur de syntaxe: unexpected
106 , expecting '{'
107 , expecting SELECT or '('
108 , expecting
109 unexpected
110 END OF STATEMENT
111 In line %1$d, column %2$d before '%3$c%4$s'\n
112 In line %1$d, column %2$d before %3$s\n
113 before ' %1$s'\n
114 before END OF STATEMENT\n
115 instruction invalide: set names\n  set_names: SET NAMES charset [COLLATE collation]\n
116 utilisation invalide du chr\n  CHR ( number [using charset] )\n
117 utilisation invalide du charset\n CHARSET ( expression )\n
118 utilisation invalide du collation\n COLLATION ( expression )\n
119 utilisation invalide du clob_to_char\n CLOB_TO_CHAR ( expression [using charset] )\n
120 utilisation invalide du index_prefix\n  INDEX_PREFIX ( expression, expression, expression )\n
121 Utilisation incorrecte de UNION et ORDER BY.
122 Expecting ON. Outer Joins require join condition.

$set 8 MSGCAT_SET_PARSER_SEMANTIC
1 Mémoire épuisée.
2 Fonction %1$s n'est pas défini.
3 Impossible de trouver ldb '%1$s' dans la base de données.
4 Le nombre de colonnes, %1$d, dans la requête gauche ne correspondent pas au nombre de colonnes, %2$d, dans la requête de droite.
5 '%1$s'  n'est pas compatible à union avec '%2$s'.
6 %1$s n'est pas défini.
7 Le domaine de %1$s ne peut pas être un VClass.
8 Le domaine de %1$s ne peut pas être un VClass proxy.
9 Le domaine de %1$s ne peut pas être une classe.
10 %1$s  n'est pas un attribut de %2$s.
11 %1$s ne peut pas être changé de cette classe.\nIl a été hérité de %2$s.
12 La classe %1$s n'existe pas.
13 %1$s n'est pas un %2$s.
14 %1$s n'est pas une méthode ou un attribut de %2$s.
15 %1$s n'a pas et ne doit pas avoir des spécifications de requête.
16 %1$s n'est pas défini dans la base de données.
17 %1$s n'est pas une super-classe de %2$s.
18 Classe %1$s hérite de non-classe %2$s.
19 VClass %1$s hérite de non-VClasse %2$s.
20 VClass Proxy %1$s hérite de non-proxy VClass %2$s.
21 %1$s est déjà une super-classe de of %2$s.
22 %1$s ne peut pas être une super-classe pour elle-même.
23 %1$s est déjà une sous-classe de %2$s.
24 %1$s n'est pas une valeur unique. Attributs exposés dans les requêtes d'agrégation doit également figurer dans la clause group by.
25 Le nombre de colonnes(%1$d) et into_names(%2$d) ne sont pas égaux.
26 La class %1$s existe déjà.
27 Super-classe %1$s dans '%2$s' n'existe pas.
28 S'attendant à une clause SUBCLASS de faire clause INHERIT '%1$s' significative.
29 Clause INHERIT '%1$s' fait référence à une classe qui n'est pas dans la clause SUBCLASS '%2$s'.
30 Classe ne peut pas avoir une spécification de requête.
31 %1$s n'est pas un ldb enregistré.
32 Un héritier proxy peut ne pas avoir sa propre clause object_id. Il ne peut pas remplacer son attribut hérité object_id(s).
33 VClass proxy a plus d'une spécification de requête.
34 Le nombre d'attributs, %1$d, ne correspond pas au nombre de colonnes, \n%2$d, dans la spécification de la requête.
35 Manquant un nom d'attribut attendu pour %1$s.
36 Attribut '%1$s' n'est pas compatible avec la colonne '%2$s' de la spécification de la requête.
37 %1$s n'est pas une classe.
38 Les indices peuvent pas être créés sur des classes virtuelles.
39 Les indices peuvent être créés que sur un seul attribut.
40 Utilisation de plusieurs classes dans la clause FROM est interdite.
41 Erreur du système -- Résolution échoué sur %1$s.
42 Vous ne pouvez pas supprimer d'une table dérivée.
43 L'utilisateur %1$s n'est pas dans la base de données.
44 Mise à jour ne peut pas avoir d'agrégation.
45 Les noms des attributs de chaque classe doit être unique. Nom de l'attribut '%1$s' dans la classe '%2$s' est spécifié plus d'une fois.
46 Il existe plusieurs assignations à %1$s.%2$s.
47 Nombre de spécification de tri n'est pas un entier.
48 %1$d dans la spécification de tri est hors limites.
49 Élément ORDER BY n'est pas un entier.
50 %1$s dans la spécification de tri ne correspond pas à une colonne de sélection.
51 Il ya des directions contradictoires de tri pour la colonne %1$s.
52 %1$s dans spécification de tri n'est pas une expression de type numéro, nom ou chemin.
53 Expression INSERT ne peut figurer dans l'instruction d'insertion.
54 Le nombre d'attributs(%1$d) et des colonnes de sélection(%2$d) ne sont pas égaux.
55 Le nombre d'attributs(%1$d) and valeurs(%2$d) ne sont pas égaux.
56 Dépassement de données sur le type de données de %1$s.
57 Type de données incompatible sur l'attribut %1$s.
58 %1$s\n n'est pas un SELECT à partir d'une seule classe.
59 `*' n'est pas une liste de sélection valide dans une spécification de requête du VClass proxy.
60 %1$s n'est un VClass ni un VClass proxy.
61 Il ya plus d'une clause de niveau d'isolement.
62 Il ya plus d'un clauses de délai d'expiration (TIMEOUT).
63 Aucune niveau d'isolation a été entré.
64 Opérateur BETWEEN n'est pas défini sur les types %1$s, %2$s et %3$s.
65 Opérateur '%1$s' n'est pas défini sur les types %2$s et %3$s.
66 La fonction doit avoir des arguments: %1$s.
67 Opérandes incompatibles: %1$s(%2$s).
68 Fonction d'agrégation doit avoir 1 argument: %1$s.
69 Tentative de division par zéro.
70 Dépassement pendant coercition du type %1$s à type %2$s.
71 Coercition impossible du type %1$s à type %2$s.
72 Le type de '%1$s' doit être SET, MULTISET, ou SEQUENCE.
73 '%1$s' doit avoir exactement un type d'élément d'ensemble.
74 %1$s a plus d'un nom de colonne dérivée.
75 %1$s a aucun nom de colonne dérivée.
76 %1$s n'a pas un nombre correspondant de colonnes (%2$d) et de noms de colonnes (%3$d).
77 Il n'y a aucun attribut dans %1$s.
78 S'attendant seule table dans %1$s.
79 Nom exposé %1$s est ambigu.
80 Noms de colonnes dérivées '%1$s' sont interdits pour les spécifications simples des classes.
81 Table dérivée '%1$s' doit avoir un nom de corrélation.
82 Échec d'autorisation pour SELECT, classe %1$s.
83 Référence d'instance de classe %1$s à un nom de table dérivée n'est pas autorisée.
84 Corrélation du chemin (%1$s) apparaît à l'extérieur un terme de chemin intérieur.
85 Référence à %1$s est ambigu.
86 Classe of %1$s  n'a pas l'attribut %2$s.
87 Pas toutes les classes (%1$s) ont l'attribut %2$s.
88 Classe (%1$s)  n'a pas l'attribut %2$s.
89 %1$s est un %2$s type, pas un type d'objet.
90 Sélecteur {%1$s} ne résout pas à une spécification de classe exposée dans \n le contexte requis (%2$s).
91 Sélecteur {%1$s} se réfère à la spécification de non-classe (%2$s).
92 Sélecteur {%1$s} ne peut pas référencer la racine de l'expression de chemin qui se definit elle-même.
93 Sélecteur {%1$s} est défini circulairement en termes de sélecteur {%2$s}.
94 Sélecteur {%1$s} est redéfinie à %2$s.
95 %1$s (domaine d'objet) ne possède pas l'attribut %2$s.
96 %1$s dans la spécification EXCEPT n'est pas une sous-classe.
97 Liste de ALL ... EXCEPT exclut TOUT à l'intérieur de la classe.
98 '%1$s' est incompatible avec\ntoute autre utilisation du sélecteur de chemin '%2$s' ou de la chemin racine '%3$s'.
99 Argument pas défini.
100 Argument n'est pas un objet.
101 %1$s n'est pas une méthode de classe ou d'un attribut de %2$s.
102 La spécification de requête ne doit pas avoir un clause INTO.
103 Spécification proxy de requête ne doit pas avoir d'expression agrégate,\naucun clause GROUP-BY, et aucun clause HAVING.
104 Spécification proxy de requête ne doit pas avoir le mot-clé DISTINCT,\nni UNIQUE dans la liste de sélection.
105 Spécification proxy de requête doit être une instruction SELECT simple.
106 Spécification proxy de requête doit faire référence à une seule table ou\nune classe à partir de la base de données locale.
107 L'utilisateur '%1$s' n'est pas le propriétaire de %2$s.
108 Proxy '%1$s' est déjà défini sur la classe '%2$s' de ldb '%3$s'.
109 CUBRID ne supporte pas des proxys avec des objets dans des ensembles hétérogènes.
110 Clause WHERE doit être une expression de type logique.
111 Clause ESCAPE du LIKE doit être une chaîne littérale ou une variable hôte.
112 Combinaison incorrecte de précision (%1$d) et l'échelle (%2$d).
113 La précision donnée de %1$d est invalide, elle doit être supérieure à %2$d et pas plus de %3$d.
114 Précision (%1$d) trop élevée. Précision maximale est %2$d.
115 Méthode %1$s doit avoir un type de retour.
116 Nombre d'arguments fournis à la méthode n'est pas égal au \nNombre défini pour la méthode.
117 Appel de méthode doit avoir une cible.
118 Méthode %1$s n'est pas défini pour la cible fournie.
119 Les méthodes d'instance ne peut pas être appelée sur des objets de classe.
120 Les méthodes de classe ne peut pas être appelée sur les objets d'instance.
121 Les méthodes de classe nécessitent un objet de classe pour leur cible.
122 Méthodes ont besoin d'un objet pour pour leur cible.
123 Méthode %1$s n'est pas entièrement entré, vérifiez définition de la méthode dans le schéma de classe.
124 Dépassement de capacité négatif du calcul de temps.
125 Dépassement de capacité négatif du calcul de date.
126 Côté gauche invalide pour une clause d'attribution.
127 Appels de méthode sur les tables dérivées d'ensemble en corrélation avec \nune portée externe ne sont pas prises en charge sauf l'expression d'ensemble est enveloppé dans une sous-requête.
128 La colonne de spécification de requête qui correspond à \nl'attribut partagé '%1$s' doit être NULL ou NA.
129 Une colonne NULL n'est pas autorisée sur la liste de sélection de la spécification requête de vue si la définition de l'attribut est omis.
130 Objets de type VClass dans ensembles ne sont pas pris en charge.
131 S'attendant à une valeur de chaîne.
132 S'attendant à une instruction ou une valeur de chaîne.
133 Attribut '%1$s' du object_id proxy ne doit pas correspondre \nà une expression de colonne de spécification de la requête.
134 Contrainte %1$s ignorée.
135 Déclarations de contrainte sont interdites dans ce contexte.
136 Conversion de type %1$s en type %2$s n'est pas pris en charge.
137 %1$s n'est pas une chaîne de bits valide.
138 L'opérateur '%1$s' n'est pas défini sur le type %2$s.
139 L'opérateur '%1$s' n'est pas défini sur les types de %2$s à %3$s et %4$s.
140 Opérandes de l'opérateur '%1$s' ne peut pas être tous les deux des variables hôtes dynamiques.
141 Trop d'aguments Trop pour la fonction générique %1$s. \nSeulement %2$d arguments sont permises pour les fonctions génériques.
142 Pour sélecteur {%1$s}, la classe '%2$s' n'est pas une sous-classe de '%3$s'.
143 Impossible d'extraire le champ '%1$s' type %2$s.
144 La spécification de requête ne doit pas avoir une clause ORDER BY.
145 Condition CASE doit être une expression de type logique.
146 Expression doit être de type %1$s.
147 Serial '%1$s' existe déjà.
148 Serial '%1$s' n'est pas défini.
149 La valeur d'augmentation ne doit pas être égal à zéro.
150 Valeur maximale dépasse 1.0e37.
151 La valeur minimale est invalide.
152 Valeur d'incrément n'est pas valide.
153 Valeur minimale dépasse -1.0e36.
154 Valeur maximale n'est pas valide.
155 Spécifiez option pour ALTER SERIAL.
156 Le second argument de la fonction serial_next_value doit être supérieure à 0.
157 Valeurs de série ne peuvent pas dépasse son minimum ou maximum.
158 Erreur E/S interne pour serial.
159 Impossible de trouver l'index '%1$s'.
160 Le nom d'index qualifié '%1$s' se réfère à une classe, mais il n'est pas précisé dans la clause FROM.
161 Deux ou plusieurs classes ont le même index '%1$s'. Utilisez les noms d'index qualifiés.
162 Utilisation invalide de %1$s. \nExpression %2$s ne peut pas être utilisé avec attribut ou sous-requête corrélée.
163 La version actuelle limite une expression qui contient ORDERBY_NUM () dans la liste de sélection.
164 Expression du chemin '%1$s' dans la condition de jointure.
165 '%1$s' dans la condition de jointure n'est pas défini.
166 La version actuelle limite les tables dérivées d'ensembles pour RIGHT OUTER JOIN.
167 Impossible d'insérer avec requête sur autre ldb.
168 Une seule colonne peut être spécifiée dans la liste de sélection quand la sous-requête n'est pas introduite par EXISTS.
169 Côté droite interdit pour une clause d'attribution. Contexte exige une requête d'être seule-colonne.
170 Impossible d'utiliser une sous-requête dans la condition de clause JOIN.
171 Définition de partition non valide.
172 Colonne de partition doit être spécifiée.
173 Une seule colonne peut être spécifiée comme colonne de partition.
174 Type de la colonne de partition n'est pas pris en charge.
175 Définition de la partition est dupliqué. '%1$s'
176 Incompatibilité  de type du constante. '%1$s'
177 Taille de partition non valide.
178 '%1$s' n'est pas une classe partitionnée.
179 '%1$s' est déjà une classe partitionnée.
180 Demandes de partition non valides.
181 %1$s ne peut pas être changé de cette classe (colonne clé de partition).
182 %1$s ne peut pas être gérés dans le cadre des partitions avec déclencheurs actifs.
183 Partition %1$s n'existe pas.
184 Incompatibilités de type de partition. '%1$s'
185 Information du partition n'est pas valide.
186 Ll'ajout de partitions est permise seulement avec une plage plus grande que celle existant.
187 Réorganisation des partitions est interdit car cela pourrait provoquer la perte de données.
188 La plage de la partition '%1$s' n'est pas valide.
189 Un attribut de classe ne peut pas avoir une contrainte d'incrémentation automatique.
190 Un attribut de classe virtuelle ne peut pas avoir une contrainte d'incrémentation automatique.
191 Le domaine de l'attribut '%1$s' ayant une contrainte d'incrémentation automatique n'est pas valide.
192 Le système et pas un utilisateur peut assigner l'attribut '%1$s' créé par 'ON OBJET CACHE' de la clé étrangère.
193 Impossible d'utiliser des noms d'attributs en double dans la liste de clé d'index. Nom de l'attribut '%1$s' inscrit plus d'une fois.
194 Fonction %1$s n'est pas définie ou donné un numéro invalid de paramètres.
195 '%1$s' peut être utilisé seulement dans les requêtes hiérarchiques. Spécifiez une clause CONNECT BY.
196 '%1$s' est interdit ici.
197 Utilisation de CONNECT_BY_ISCYCLE est permise seulement si la requête a une clause CONNECT BY NOCYCLE.
198 Aucune table utilisée.
199 ORDER BY NULL nécessite GROUP BY.
200 Le nombre à CACHE doit être inférieure à un cycle (%1$s).
201 '%1$s' est interdit dans une requête avec la clause LIMIT.
202 WITH ROLLUP nécessite GROUP BY.
203 Un paramètre nom de colonne doit être transmise à la fonction DEFAULT. Les expressions de chemin sont pas encore pris en charge.
204 Option de table en double '%1$s'
205 La classe '%1$s' est marqué comme REUSE_OID et il n'est pas rapportables. Classes non rapportablées ne peuvent pas être le domaine d'un attribut et les OID de leurs instances ne peuvent être retournés.
206 Option 'serial %1$s' en double.
207 La colonne '%1$s' dans la clause de tri est ambigu.
208 Attribut '%1$s' n'a pas de valeur par défaut.
209 Éléments d'ORDER BY doivent apparaître dans la liste de sélection si SELECT DISTINCT est spécifié.
210 Variable d'hôte est interdite dans les colonnes de la spécification de la requête.
211 %1$s ne peut pas être une colonne de ORDER BY
212 %1$s ne peut pas être une colonne de GROUP BY
213 Impossible de définir '%1$s de %2$s' comme un type de colonne.
214 Valeur invalide "%1$s" pour la longueur du préfixe de l'indice.
215 Fonction '%1$s' n'est pas définie sur types %2$s ,%3$s, %4$s et %5$s.
216 Fonction '%1$s' n'est pas définie sur types %2$s, %3$s et %4$s.
217 Fonction '%1$s' n'est pas définie sur types %2$s et %3$s.
218 Fonction '%1$s' n'est pas définie sur type %2$s.
219 Fonction '%1$s' n'est pas définie pour un argument d'index de type %2$s.
220 ALTER VIEW '%1$s' AS SELECT est interdit pour les vues qui ont des sous-classes ou des superclasses. Utilisez ALTER VIEW CHANGE QUERY au lieu.
221 Les attributs du vue ne peuvent pas être modifiés à l'aide de cette instruction. 'ALTER VIEW nom AS requête' pourrait aider au lieu.
222 CUBRID nécessite ORDER BY d'être la même expression comme argument GROUP_CONCAT ou un entier (position d'argument).
223 Le prédicat LIKE nécessite un caractère d'échappement d'être spécifié. Consultez la documentation pour les paramètres de configuration '%1$s' et '%2$s'.
224 KEYLIMIT ne peut pas être utilisé dans la clause INDEX NONE.
225 Variable de session interdite dans ce contexte.
226 Conflit d'indicateur d'index entre '%1$s' et '%2$s'.
227 Fonction '%1$s' ne peut pas être utilisée dans la clause DEFAULT.
228 Mise à jour des tables tables dérivées est interdite.
229 Mise à jour des attributs de classe et les attributs d'instance est interdite dans le même instruction.
230 Mise à jour des hiérarchies de classes virtuelles est interdite.
231 Les variables hôte sont interdits dans une instruction DDL.
232 Expression de filtration non valide (%1$s) pour l'indice.
233 Fonctions multiples ou imbriqués sont interdits dans une définition d'index de fonction.
234 Spécification d'index non valide.
235 SHARED, DEFAULT et AUTO_INCREMENT ne peut pas être définie avec l'autre.
236 Trop valeurs de l'énumération (%1$d). Un type énumération peut avoir un maximum de valeurs de %2$d.
237 Des valeurs en double dans le type enum.
238 Impossible de fusionner (MERGE) en une table dérivée.
239 Impossible d'affecter la table source dans une instruction MERGE.
240 MERGE avec des hiérarchies de classes est interdit.
241 Nom d'utilisateur est trop long.
242 L'expression constante ne peut pas être utilisé comme index de fonction.
243 Nom de la classe ou l'alias ne sont pas uniques dans la liste: '%1$s'
244 Clause DEFAULT non valide. '%1$s' ne peut pas être utilisée dans une expression imbriquée.
245 '%1$s' nécessite des arguments avec classements compatibles.
246 Jeu de caractères codés non valide.
247 Jeu de caractères codés et le classement requis ne sont pas compatibles.
248 Classement '%1$s' inconnue.
249 Contexte nécessite collection avec des éléments ayant des jeux de caractères et des collations identiques.
250 Index de préfixe est interdit sur l'attribut '%1$s' (a une classement avec des expansions).
251 Valeur initiale n'est pas valide.
252 Index de préfixe multi-colonne est interdit.
253 Fonction '%1$s' ne peut pas être utilisée pour un index à base de fonctions.
254 Définition de colonne d'index non valide: %1$s
255 '%1$s' est interdit dans une expression de filtration pour l'index.
256 Impossible d'utiliser GROUPBY_NUM() et la clause WITH ROLLUP dans la même requête.
257 Impossible d'utiliserMERGE UPDATE sur colonne %1$s référencée dans la clause ON.
258 Le nombre de colonnes, %1$d, dans (%2$s) ne correspond pas \nà le nombre de colonnes, %3$d, (%4$s).
259 Préfixes de colonnes interdits dans l'expression d'index à base de fonctions.
260 %1$s ne peut pas être une collone de PARTITION BY ou d'ORDER BY
261 Requête doit doit s'évaluer en une valeur unique \n (%1$s).
262 Fonctions analytiques imbriquées sont interdits.
263 Un attribut hérité ne peut pas être utilisé comme clé de partitionnement.
264 La taille de l'agrégat d'éléments d'énumération est trop grand.
265 Impossible d'utiliser la table cible dans la spécification de la source.
266 COLLATE n'est pas autorisée dans ce contexte.
267 Modificateur COLLATE  ne peut pas changer de jeu de codes (de '%1$s' à '%2$s'). Essayez d'utiliser CAST au lieu.
268 Jeu de codes invalid '%1$s' pour la valeur de la partition. S'attendant jeu de codes '%2$s'.
269 Variable de session ne peut pas avoir un classement différent de classement du système ('%1$s').
270 Expression invalide pout index de fonction.
271 La spécification de la requête de %1$s contient une référence cyclique pour lui-même.
272 Expression %1$s n'est pas permis dans condition du JOIN.
273 ORDER BY n'est pas autorisé en fonction MEDIAN.
274 Les arguments de la fonction '%1$s' doit être constants.
275 La fonction '%1$s' doit avoir la clause ORDER BY.
276 Pour la fonction '%1$s', le nombre d'arguments ne correspond pas au nombre de colonnes ORDER BY.
277 Pour la fonction '%1$s', la clause ORDER BY n'est pas valide.
278 Only one table within a view can be updated.
279 Multiple insert targets not allowed.
280 '%1$s' type is vclass, drop vclass with cascade constraints is not allowed.
281 The FOR UPDATE clause cannot be used in subqueries, unions, with GROUP BY clause, DISTINCT operator, aggregate functions.
282 Expects %1$s in argument position %2$d.
283 Too many arguments specified.
284 Too few arguments, need %1$d arguments.
285 Expects %1$s for argument %2$s.
286 Argument %1$s is missing.
287 Unknown argument %1$s.
288 Vous ne pouvez pas supprimer toutes les partitions, utilisez DROP TABLE.
289 The expression including %1$s is not allowed in this context.
290 Transaction id is not an integer.
291 Subquery is not allowed in ORDER BY FOR clause.

$set 9 MSGCAT_SET_PARSER_RUNTIME
1 Mémoire virtuelle épuisée: impossible d'allouer %1$d octets.
2 Niveau d'isolement de la transaction doit être de 1, 2, 3, 4, 5 ou 6.
3 Valeur du délai d'expiration doit être -1, 0 ou >0.
4 Niveaux d'isolation définie à:\n
5          Unknown isolation.\n
6          Unknown isolation.\n
7          Unknown isolation.\n
8          READ COMMITTED\n
9          REPEATABLE READ\n
10 Une requête non modifiable est interdite dans un appel db_query_execute_oid.
11 Un délai d'expiration s'est passé pour transaction en attente des verrous.
12 Tentative d'accès à le variable hôte de l'index %1$d lorsque seulement\n%2$d variables d'entrée ont été donnés.
13 Conversion pas implémenté.
14 Erreur interne, conversion indéfinie.
15 '%1$s' ne donne aucun résultat.
16 '%1$s' donne plus d'une ligne.
17 La variable d'interpreteur %1$s n'est pas définie.
18 Impossible d'évaluer '%1$s'.
19 Variable %1$s inconnue.
20 Attribut partagé %1$s inconnu.
21 '%1$s' donne plus d'une colonne.
22 Utilisation imbriquée ou invalide de la fonction d'agrégation.
23 Fonction %1$s n'est pas défini.
24 Contexte nécessite requête d'être seule colonne \n (%1$s).
25 Valeurs trop nombreux ont été spécifiés.
26 Cible d'attribution virtuelle (%1$s) n'est pas inversible.
27 Il ya plus de colonnes dans la spécification de requête que les attributs de la classe %1$s.
28 Il ya plus d'attributs dans la classe %1$s que les colonnes de la spécification de la requête.
29 Version actuelle limite l'agrégation de vclasses SANS \n spécifications de requête d'agrégat.
30 La version actuelle limite l'agrégation de vclasses SANS spécifications \n de requête d'union.
31 Utilisation de VClass (%1$s) dans une hiérarchie de composition nécessite qu'il soit actualisable. \n Il ne peut pas être mis à jour à l'égard de la classe réel %2$s.
32 pas utilisé
33 Construction interdite dans l'expression de chemin '%1$s'.
34 Expression de chemin '%1$s' dépasse la limite de db à %2$s, avec VClass %3$s.
35 Expression de chemin '%1$s' dépasse la limite de db à partir de %2$s, à la classe %3$s.
36 %1$s est interdit sur %2$s.
37 SELECT est interdit sur %1$s.
38 VClass %1$s n'est pas modifiable.
39 %1$s n'est pas un VClass proxy.
40 Spécification de la requête de VClass proxy ne doit pas être un JOIN.
41 %1$s n'a pas d'identifiant d'objet affecté.
42 Classes (%1$s) avec plusieurs champs object_id dans une hiérarchie \nde composition ne sont pas pris en charge.
43 Attribut d'objet, %1$s de %2$s, n'est pas un VClass sur ldb %3$s.
44 Type de la spécification de requête incompatible avec l'attribut %1$s.
45 La spécification de la requête de %1$s contient une référence cyclique pour lui-même.
46 Aucune expression donnée à évaluer.
47 Aucune classe réelle trouvée pour VClass %1$s.
48 Exécution de '%1$s' sur ldb '%2$s' \n a donné: %3$s.
49 ​​Mise valeur '%1$s' dans attribut '%2$s' a donné: %3$s.
50 Nom de ldb ne doit pas être null dans %1$s.
51 Nom de l'hôte ldb ne doit pas être null dans %1$s.
53 Nom d'hôte ne doit pas être null dans %1$s.
54 Nom d'utilisateur ne doit pas être null dans %1$s.
55 Mot de passe d'utilisateur ne doit pas être null dans %1$s.
56 INSERT se traduit par une instruction vide.
57 UPDATE se traduit par une instruction vide.
58 DELETE se traduit par une instruction vide.
59 INGRES ne supporte pas les dates (%1$s) avant Janvier 1, 1582.
60 Ressources épuisées dans la génération de requêtes.
61 Rendements de requêtes sans résultat, donc variable '%1$s' n'est pas définie.
62 Avec option de vérification LOCAL n'est pas pris en charge.
63 Exception d'option CHECK sur vue %1$s.
64 Date non valide: '%1$s'.
65 Temps non valide: '%1$s'.
66 utime non valide: '%1$s'.
67 Sous-requêtes dans une spécification de requête d'une proxy ne sont pas pris en charge.
68 Argument de la fonction ldb n'est pas compatible avec ldb dans '%1$s'.
69 Invalid numérique: '%1$s'.
70 Optimisation de requête OUTER JOIN a échoué.
71         SERIALIZABLE.\n
72 VClass %1$s n'est pas modifiable. Seulement vclasses actualisables sont permis à sélectionner des VIDs.
73 Partitionnement échoué.
74 Partition n'existe pas.
75 Accés à la partition '%1$s' est interdit.
76 Le serial '%1$s' n'est pas encore défini.
77 Seulement le propriétaire ou DBA peut modifier la définition de la série cible.
78 Il est interdit aux utilisateurs de modifier l'objet serialisé créée par une contrainte d'incrémentation automatique.
79 Niveau d'isolement invalid: schéma %1$s, %2$s instances.
80 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.


$set 11 MSGCAT_SET_AUTHORIZATION
1 *** cache invalide ***
2 classe %1$s
3 de %1$s:
4 Autorisation pour l'utilisateur %1$s \n
5 Autorisation pour l'utilisateur pas défini \n
6 L'utilisateur: %1$s \n
7            Identifiant: %1$d \n
8       membres:
9        groupes:
10 L'utilisateur %1$s \n
11 l'utilisateur courant est: %1$s \n
12 Objet d'autorisation de root: \n
13    utilisateurs:
14 *** Impossible de publier toutes les droits pour l'utilisateur "%1$s"*** \n
15 AUTORISATIONS \n
16 direct_groups :

$set 12 MSGCAT_SET_HELP
1 Classe racine
2 %1$s %2$s\n
3 Super-classes : \n
4 Sous-classes : \n
5 Attributs: \n
6 Méthodes: \n
7 Attributs de classe: \n
8 Méthodes de classe: \n
9 Résolutions de conflits:\n
10 Fichiers de méthodes: \n
11 Spécification de requête: \n
12 Objet de %1$s \n
13 \nDescription:\n
14 \nStructure:\n
15 \nExample:\n
16 Nom de META CLASS
17 Nom de classe
18 Nom de VClass
$  Don't delete the space at the end of the next message
19 VClass sur ldb
20 GÉNÉRAL

$set 13 MSGCAT_SET_TRIGGER
1 TRACE: Évaluer la condition pour déclencheur '%1$s'.
2 TRACE: Exécution de l'action pour déclencheur '%1$s'.

$set 14 MSGCAT_SET_LOCK
1 \n
2 Fil d'exécution (indice %1$3d) de transaction (indice %2$3d, %3$s, %4$s@%5$s|%6$d) a été suspendu.\n
3 Fil d'exécution (indice %1$3d) de transaction (indice %2$3d, %3$s, %4$s@%5$s|%6$d) a été repris.\n
4 Indice de transaction %1$3d ACQUIS (ACQUIRED) %2$s verrou sur objet %3$d|%4$d|%5$d.\n
5 Indice de transaction %1$3d ACQUIS (ACQUIRED) %2$s verrou sur page %3$d|%4$d.\n
6 Indice de transaction %1$3d CONVERTIT (CONVERTED) son verrou à %2$s verrou sur objet %3$d|%4$d|%5$d.\n
7 Indice de transaction %1$3d CONVERTIT (CONVERTED) son verrou à %2$s verrou sur page %3$d|%4$d.\n
8 Indice de transaction %1$3d ATTEND (WAIT FOR) %2$s verrou sur objet %3$d|%4$d|%5$d.\n
9 Indice de transaction %1$3d ATTEND (WAIT FOR) %2$s verrou sur page %3$d|%4$d.\n
10 Indice de transaction %1$3d LIBÉRET (RELEASE) %2$s verrou sur objet %3$d|%4$d|%5$d.\n
11 Indice de transaction %1$3d LIBÉRET (RELEASE) %2$s verrou sur page %3$d|%4$d.\n
12 Indice de transaction %1$3d RÉTROGRADE (DEMOTE) verrou à %2$s sur objet %3$d|%4$d|%5$d.\n
13 Indice de transaction %1$3d RÉTROGRADE (DEMOTE) verrou à %2$s sur page %3$d|%4$d.\n
14 OID = %1$2d|%2$6d|%3$4d\n
15 Type d'objet: Classe racine.\n
16 Type d'objet: Classe = %1$s.\n
17 Type d'objet: Instance de class (%1$2d|%2$6d|%3$4d) = %4$s.\n
18 Type d'objet: Inconnu.\n
19 Mode total de détenteurs = %1$8s, Mode total d'attendeurs = %2$8s.\n
20 Num détenteurs=%1$3d, Num détenteurs bloqués=%2$3d, Num attendeurs=%3$3d\n
21 LOCK HOLDERS:\n
22 BLOCKED LOCK HOLDERS:\n
23 LOCK WAITERS:\n
24 NON_2PL_RELEASED:\n
25 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d\n
26 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d, Nsubgranules = %5$2d\n
27 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d\n\t%5$13s Blocked_mode = %6$8s\n\t%7$17s Start_waiting_at = %8$-30s\n\t%9$17s Wait_for_secs = %10$.2f\n
28 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d, Nsubgranules = %5$2d\n\t%6$13s Blocked_mode =%7$8s\n\t%8$13s Start_waiting_at = %9$-30s\n\t%10$13s Wait_for_secs = %11$.2f\n
29 %1$4sTran_index = %2$3d, Blocked_mode =%3$8s \n\t%4$13s Start_waiting_at = %5$-30s \n\t%6$13s Wait_for_secs = %7$.2f\n
30 %1$4sTran_index = %2$3d, Non_2_phase_lock =%3$8s\n
31 VPID (volid|pageid) = %1$3d|%2$6d.\n
32 *** Dump de table de verrouillage ***\n L'augmentation de verrous à = %1$d, interval de blocage-exec = %2$5.2f\n
33 Transaction (indice %1$2d, %2$s, %3$s@%4$s|%5$d)\n
34 Isolation %1$s\n
35 State %1$s\n
36 Timeout_period %1$s\n
37 Opérations avortées unilatéralement avec des indices:\n
38 %1$d ,
39 Transactions Timed_out avec indices:\n
40 %1$d ,
41 Résoudre le blocage en sélectionnant une transaction et une fonction \n pour sortir de le blocage: %1$d %2$s
42 , %1$d %2$s
43 Type d'objet: Clé d'index de classe (%1$2d|%2$6d|%3$4d) = %4$s.\n
44 Nom d'index: %1$s\n

$set 15 MSGCAT_SET_IO
1 \n*************************************************************************\n
2 Sauvegarde = %1$s est nécessaire. \n Il doit être monté/chargé pour continuer. \n Tapez 1 pour continuer après la sauvegarde est monté/chargé. \nTapez 0 pour quitter. \n
3 \n\n*** INFORMATIONS D'EN-TÊTE DE SAUVEGARDE ***\n\n
4 Identification de fichiers magiques de sauvegarde %1$s,\n
5              Version: %1$s\n\
    Version du disque: %2$g\n
6    Nom de la base de données: %1$s\n\
      Temps de création du DB: %2$s\
            Taille de la page: %3$d\n
7     Backup Level: %1$d (%2$s)\n\
        Start_lsa: %3$lld|%4$d\n\
         Last_lsa: %5$lld|%6$d\n
8           Temps du sauvegarde: %1$s\
  Numéro d'unité de sauvegarde: %2$d\n
9    Le nom du volume de base de données: %1$s\n\
   Identificateur de volume: %2$d, Taille: %3$lld octets (%4$d pages)\n
10   Nom de la base de données: %1$s\n\
                Nom du volume: %2$s\n\
               Numéro d'unité: %3$d\n\
         Niveau de sauvegarde: %4$d (%5$s)\n\
Entrez l'une des options suivantes:\n
11 Tapez\n \
  -  %1$d pour quitter.\n \
  -  %2$d pour continuer après le volume est monté / chargé. (réessayer) \n \
  -  %3$d pour continuer après le changement de répertoire du volume ou du périphérique.\n
12 Donnez nouveau répertoire ou d'un dispositif pour le volume de sauvegarde.\n
13 Erreur en entrée, choisissez une valeur comprise entre %1$d et %2$d.\n
14 ** ATTENTION ** volume de sauvegarde incorrecte monté.\n
15 Volume de sauvegarde %1$s est d'un autre niveau de sauvegarde \n. (Niveau: %2$d, s'attendent %3$d).\n\n
16 Volume de sauvegarde %1$s a une page ou un identification magique incorrect.\n\n
17 Volume de sauvegarde %1$s est d'une autre base de données \n. (Nom: %2$s, s'attendent %3$s).\n\n
18 Volume de sauvegarde %1$s de séquence pas valide\n (Num Unité: %2$d, s'attendant %3$d).\n\n
19 Incohérences dans l'horodatage du volume de sauvegarde %1$s \n (Horodatage: %2$s, s'attendant %3$s).\n\n
20 Erreur de fichier du volume de sauvegarde, ligne %1$d.\n
21 Étiquette de volume de sauvegarde: Niveau: %1$d, unité: %2$d,%3$s base de données, Temps de sauvegarde: %4$s \n
22     Niveau sauvegarde précédente: %1$d Temps: %2$s (start_lsa était %3$lld|%4$d)\n
23 Le volume suivant de sauvegarde de base de données est nécessaire pour continuer la restauration:\n\n
24 Destination de sauvegarde est rempli, une nouvelle destination est nécessaire pour continuer:\n\n
25  Nom du volume précédent de sauvegarde: %1$s\n
26     Nom du volume suivant de sauvegarde: %1$s\n
27  Taille de page du sauvegarde: %1$d\n
28       Méthode Zip: %1$d (%2$s)\n\
        Niveau Zip: %3$d (%4$s)\n
29 Incluent journal actif: %1$s\n

$set 16 MSGCAT_SET_LOG
1 \n*************************************************************************\n
2 Journal de transactions %1$s \n est nécessaire pour poursuivre l'exécution normale   \n \
  Tapez\n \
  -  0 pour quitter.\n \
  -  1 pour continuer sans journal actuelle. (Recouvrement partiel)\n \
  -  2 pour continuer après le journal est monté/chargé.       \n \
  -  3 pour continuer après avoir changé emplacement/nom du journal.\n
3 Fichier d'informations de sauvegarde %1$s \n est nécessaire pour continuer la restauration. \n \
  Tapez\n \
  -  0 pour quitter.\n \
  -  1 pour continuer après le journal est monté/chargé.       \n \
  -  2 pour continuer après avoir changé emplacement/nom du journal.\n
4 Tapez nouvel emplacement/nom.\n
5 COMMENTAIRE: %1$s pour la base de données %2$s\n
6 COMMENTAIRE: Journal de transactions %1$s n'est pas nécessaire plus longtemps moins qu'un accident multimédia de la base de données survient.\n
7 COMMENTAIRE: Journal de transactions de %1$s à %2$s ne sont pas nécessaires plus longtemps moins q'un accident multimédia de la base de données survient.\n
8 COMMENTAIRE: from renamed database = %1$s\n
9 ARCHIVE: %1$d %2$s %3$lld %4$lld\n
10 ARCHIVE:
11 RETIRE: %1$d %2$s to \n \
       %3$d %4$s.\n \
       RAISON: %5$s\n
12 ACTIVE: %1$s %2$d pages\n
13  Fin d'enregistrer le tran_index %1$d avec tranid = %2$d,\n avant LSA= %3$lld|%4$d et à %5$s
14  Fin d'annuler le tran_index %1$d avec tranid = %2$d,\n avant LSA= %3$lld|%4$d et à %5$s
15  Récupération incomplète des médias est en vigueur. La base de données sera \n \
 récupéré jusq'à l'adresse de point de LSA %1$lld|%2$d. Temps de dernière \n \
 enregistration/annulation est approximativement à %3$s \n \
 La base de données a perdu toutes les informations sur les transactions validées \n \
 après cette heure.\n
16 Réinitialiser du journal en raison d'une récupération incomplète.
17 Base de données de sauvegarde a été prise.
18 Accident de média n'est pas important pour l'application.
19 Sauvegarde dans le répertoire %1$s qui semble contenir une sauvegarde antérieure.\n\
Volumes de sauvegarde obsolètes de la base de données à cet emplacement seront supprimé,\n\
aussi ceux de la sauvegarde au niveau actuel et supérieur.\n
20 Tapez 'y' pour continuer ou sur une autre touche pour annuler.\n
21 Sauvegarde de la Base de données a été interrompue par l'utilisateur.\n
22 SUPPRESSION REPORTÉ: Journals des transactions %1$d %2$s à \n \
        %3$d %4$s \n \
        ne sont plus nécessaire à moins qu'une restauration à partir de sauvegarde en cours apparaît.\n
23 Un plus nouveau sauvegarde a libéré les journaux marqué précédemment comme DELETE POSTPONED.\n
24 COMMENTAIRE: journaux des transactions %1$s n'est pas encore SUPPRESSION REPORTÉ en raison de plus récente\n \
   sauvegarde et n'est donc pas nécessaire à moins que un accident survient base de données multimédia.\n
25 COMMENTAIRE: journaux des transactions de %1$s à %2$s ne sont pas encore are not SUPPRESSION REPORTÉs en raison\n \
   d'une plus récente sauvegarde et ne sont donc pas nécessaire à moins que une base de données média\n \
   accident se produit.\n
26 Une erreur s'est produite lors d'une restauration de base de données, pendant le traitement\n\
          Volume de sauvegarde: %1$s\n\
                Numéro d'unité: %2$d\n\
     Nom de la base de données: %3$s\n\
   Volid de la base de données: %4$d\n\
** AVERTISSEMENT: Ne pas utiliser la base de données jusqu'à ce qu'une restauration sans erreur est obtenu.\n
27 Erreur en entrée, choisissez une valeur comprise entre %1$d et %2$d.\n
28 Le temps (%1$s) spécifié doit être postérieure à l'heure (%2$s) de la sauvegarde spécifiée.\n
29 COMMENTAIRE: Journal de transactions %1$s, qui contient les pages du journal avant %2$lld, ne sont pas nécessaire plus longtemps par les utilitaires HA.\n
30 Nombre de journal actif de transactions a été dépassé le nombre maximum désirée.
