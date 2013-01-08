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
$ utils.msg
$
$ The following set numbers have been assigned:
$
$       1       generic
$       2       backupdb
$       3       copydb
$       4       createdb
$       5       dbstatlink
$       6       deletedb
$       7       renamedb
$       9       master
$       10      optimizedb
$       11      restoredb
$       12      loaddb
$       13      unloaddb
$       14      compactdb
$       15      commdb
$       16      pathdb
$       17      addvoldb
$       18      checkdb
$       19      spacedb
$       20      estimatedb_data
$       21      estimatedb_index
$       22      installdb
$       23      migdb
$       24      diagdb
$       25      lockdb
$       26      killtran
$       27      restartevnt
$       28      prestartldb
$       29      shutdownldb
$       30      mqueueldb
$       31      migrate_v73
$       32      load_audit_trail
$       33      alterdbhost
$       37      plandump
$       38      paramdump
$       39      changemode
$       40      copylogdb
$       41      applylogdb
$       43      statdump
$       44      applyinfo
$       45      acldb
$       46      genlocale
$       47      dumplocale
$       48      synccolldb
$       49      tranlist

$set 1 MSGCAT_UTIL_SET_GENERIC
1 Longueur ou caractère non valide '%c' dans le nom de la base de données '%s'\n
2 Impossible d'ouvrir le fichier de sortie '%s'\n
3 Nom d'utilitaire manquant ou invalide. \n
4 Nom d'utilitaire ou invalide '%s'\n
5 La liste suivante contient les noms d'utilitaires reconnus: \n
6 caractère invalide '%c' dans le nom du volume '%s'\n
9 \
%s (utilitaires CUBRID)\n\
%s\n\
Copyright (C) 2008 Search Solution Corporation. Tous droits réservés par Search Solution.\n
10 \
utilitaire d'administration CUBRID, version %s\n\
usage: %s <utility-name> [args]\n\
Entrez '%s <utility-name>' pour l'aide d'un utilitaire spécifique.\n\
\n\
Nom d'utilitaires d'administration disponible:\n\
    createdb\n\
    deletedb\n\
    installdb\n\
    renamedb\n\
    copydb\n\
    backupdb\n\
    restoredb\n\
    addvoldb\n\
    spacedb\n\
    lockdb\n\
    tranlist\n\
    killtran\n\
    optimizedb\n\
    compactdb\n\
    diagdb\n\
    checkdb\n\
    alterdbhost\n\
    plandump\n\
    loaddb\n\
    unloaddb\n\
    paramdump\n\
    statdump\n\
    changemode\n\
    copylogdb\n\
    applylogdb\n\
    applyinfo\n\
    acldb\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
\n\
%s est un outil de SGBD.\n\
Pour plus d'informations, reportez-vous à http://www.cubrid.com\n
12 '%s' Nom d'utilitaire pas valid.\nEntrez 'cubrid --help' pour voir l'utilisation.\n
13 '%s' commande is invalide.\nEntrez 'cubrid --help' pour voir l'utilisation.\n
14 Impossible de charger une ou plusieurs propriétés obligatoires dans les fichiers de configuration..\n
15 @ %s %s: %s\n
16 @ %s %s\n
17 ++ %s '%s' ne s'exécute pas.\n
18 ++ %s ne s'exécute pas.\n
19 ++ %s '%s' est en cours d'exécution.\n
20 ++ %s est en cours d'exécution.\n
21 ++ %s %s: %s\n
22 Un ou plusieurs arguments sont manquants.\nEntrez 'cubrid --help' pour voir l'utilisation.\n
23 utilitaire CUBRID, version %s\n\
usage: %s <utility-name> [args]\n\
Entrez '%s <utility-name>' pour aide sur un utilitaire spécifique.\n\
\n\
Utilitaires de service disponibles\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
    shard\n\
\n\
Utilitaires d'administrateur disponibles:\n\
    addvoldb\n\
    alterdbhost\n\
    backupdb\n\
    checkdb\n\
    compactdb\n\
    copydb\n\
    createdb\n\
    deletedb\n\
    diagdb\n\
    installdb\n\
    tranlist\n\
    killtran\n\
    loaddb\n\
    lockdb\n\
    optimizedb\n\
    plandump\n\
    renamedb\n\
    restoredb\n\
    spacedb\n\
    unloaddb\n\
    paramdump\n\
    statdump\n\
    changemode\n\
    copylogdb\n\
    applylogdb\n\
    applyinfo\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
\n\
%s is a tool for DBMS.\n\
Pour plus d'informations, reportez-vous à http://www.cubrid.com\n
24 utilitaire CUBRID, version %s\n\
usage: %s service <command>\n\
\n\
Commandes disponibles:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 utilitaire CUBRID, version %s\n\
usage: %s server <command> [args]\n\
\n\
Commandes disponibles:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status\n\
    acl         <status|reload> database-name\n
37 Le serveur a été configuré pour la haute disponibilité (HA).\n
26 utilitaire CUBRID, version %s\n\
usage: %s broker <command> [args]\n\
\n\
Commandes disponibles:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [<expr>]\n\
    on          broker-name\n\
    off         broker-name\n\
    reset       broker-name\n\
    acl         <status|reload> [broker-name]\n
27 utilitaire CUBRID, version %s\n\
usage: %s manager <command>\n\
\n\
Commandes disponibles:\n\
    start\n\
    stop\n\
    status\n
28 utilitaire CUBRID, version %s\n\
usage: %s heartbeat <command> [args]\n\
\n\
Commandes disponibles:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    copylogdb   <start|stop> database-name node-name\n\
    applylogdb  <start|stop> database-name node-name\n\
    status\n\
    reload\n
31 Argument '%s' n'est pas nécessaire.\n
32 Un nom de base de données est manquante.\n
33 %s: Telle option est obsolète.\n
34 Paramètre non valide. %s %s\n
35 Ne peut pas allouer de la mémoire.\n
36 Le serveur n'a pas été configuré pour HA.\n
38 Le serveur configuré pour HA n'apparaît pas dans ha_db_list.\n
39 Le serveur configuré pour HA n'apparaît pas dans ha_node_list.\n
62 utilitaire CUBRID, version %s\n\
usage: %s shard <command> [args]\n\
\n\
Commandes disponibles:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-t] [-s <sec>] [<expr>]\n\
    on          shard-name\n\
    off         shard-name\n\
    reset       shard-name\n\
    acl         <status|reload> [shard-name]\n\
    getid       -b <broker-name> [-f] shard-key\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 AVERTISSEMENT: l'option -t est ignoré dans le mode d'exécution autonome.\n
60 \
backupdb: Sauvegarde une base de données.\n\
usage: %s backupdb [OPTION] database-name\n\
\n\
options valides:\n\
  -D, --destination-path=RÉPERTOIRE  stocke les volumes de sauvegarde dans un RÉPERTOIRE; par défaut: répertoire "log" de la base de données\n\
  -r, --remove-archive         summprime journaux inutils; utiliser avec soin et consulter le manuel\n\
  -l, --level=NIVEAU           NIVEAU de sauvegarde; par défaut: niveau complet\n\
                               NIVEAU permis:\n\
                                   0 - complet\n\
                                   1 - incrémental 1\n\
                                   2 - incrémental 2\n\
  -o, --output-file=FICHIER    affiche des messages de sauvegarde détaillés sur FICHIER\n\
  -S, --SA-mode                exécution en mode autonome\n\
  -C, --CS-mode                exécution en mode client-serveur\n\
      --no-check               ne vérifie pas la cohérence de la base de données; par défaut: vérifier\n\
  -t, --thread-count=NOMBRE    NOMBRE de fils d'exécution; par défaut: 0 (auto)\n\
  -z, --compress               comprimer les volumes de sauvegarde; par défaut: décompresser\n\
  -e, --except-active-log      ne comprennent pas un volume de journal actif; par défaut comprennent\n\
      --sleep-msecs=N          veille N millisecondes par 1M lit; par défaut: 0\n
71 Arguments incompatibles, \"%s\", et \"%s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 ERREUR: Nom de la destination est identique au nom de source\n
31 ERREUR: volext-pathname n'existe pas ou n'est pas accessible en écriture.\n
32 ERREUR: vols-tofrom-paths_file n'existe pas ou n'est pas lisible.\n
60 \
copydb: copie une base de données.\n\
usage: %s copydb [OPTION] src-database-name dest-database-name\n\
\n\
options valids:\n\
      --server-name=HÔTE            nom d'hôte du serveur; par défaut: nom d'HÔTE actuel\n\
  -F, --file-path=CHEMIN            CHEMIN du répertoire pour les volumes de base de données; par défaut: répertoire courant\n\
  -L, --log-path=CHEMIN             CHEMIN du répertoire de volumes de journal; par défaut: même que file-path\n\
  -B, --lob-base-path=CHEMIN        CHEMIN du répertoire de base pour les données LOB externes; par défaut: <file-path>/lob\n\
  -E, --extended-volume-path=CHEMIN CHEMIN du répertoire pour les volumes d'informations complémentaires; par défaut: même chemin que les volumes de base de données ou le chemin donné dans control-file\n\
  -i, --control-file=FICHIER        les volumes de copie existents sur plusieurs sites sont écrites dans le FICHIER de contrôle; par défaut: aucun\n\
  -r, --replace                     remplace une base de données existante; par défaut: ne pas remplacer\n\
  -d, --delete-source               supprime une base de données source; par défaut: ne pas supprimer\n\
      --copy-lob-path               copie le chemin de stockage externe pour les données LOB de base de données source. ne peut pas être utilisée avec l'option -B; par défaut: ne pas copier\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 Groupe '%s' n'existe pas\n
41 L'utilisateur '%s' n'existe pas\n
42 Commande inconnue '%s'\n
43 Impossible d'ouvrir le fichier de sortie '%s'\n
44 Impossible de supprimer la base de données existante.\n
45 Creating database with %s size. The total amount of disk space needed is %s.\n
46 Impossible de créer la base de données.\n
47 Problème avec le fichier '%s' d'utilisateur\n
48 The given '%s', %s is out of range. Choose a size between %s and %s.\n
49 La valeur de %s '%s' n'est pas valide.\n
60 \
createdb: crée une base de données.\n\
usage: %s createdb [OPTION] database-name\n\
\n\
options valids:\n\
      --db-volume-size=TAILLE         TAILLE de premier volume d'informations\n\
                                      par défaut:: db_volume_size dans cubrid.conf\n\
      --db-page-size=TAILLE           TAILLE de page de la base de données, en octets;  par défaut:: 16Ko\n\
      --log-volume-size=TAILLE        TAILLE des archives de journal\n\
                                      par défaut: log_volume_size dans cubrid.conf\n\
      --log-page-size=TAILLE          TAILLE de page du journal, en octets; par défaut: taille de page de la base de données\n\
      --comment=COMMENTAIRE           COMMENTAIRE pour une base de données; par défaut: aucun\n\
  -F, --file-path=CHEMIN              CHEMIN du répertoire de volumes de base de données; par défaut: répertoire courant\n\
  -L, --log-path=CHEMIN               CHEMIN du répertoire de volumes de journal; par défaut: même que file-path\n\
  -B, --lob-base-path=CHEMIN          CHEMIN du répertoire de base de données LOB externes; par défaut: <file-path>/lob\n\
      --server-name=HÔTE              Nom d'HÔTE du serveur; par défaut: nom d'hôte actuel\n\
  -r, --replace                       remplace une base de données existante; par défaut: ne pas remplacer\n\
      --more-volume-file=FICHIER      FICHIER qui contient les spécifications pour la création de volumes supplémentaires; par défaut: aucun\n\
      --user-definition-file=FICHIER     FICHIER qui contient les définitions des utilisateurs; par défaut: aucun\n\
      --csql-initialization-file=FICHIER FICHIER qui contient des requêtes SQL pour une initialisation de la base de données; par défaut: aucun\n\
  -o, --output-file=FICHIER           redirige les messages de sortie dans cet FICHIER; par défaut: aucun\n\
  -v, --verbose                       active les messages verbeux d'état; par défaut: désactivé\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: supprime une base de données.\n\
usage: %s deletedb [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FICHIER    redirige les messages de sortie dans cet FICHIER; par défaut: aucun\n\
  -d, --delete-backup          supprimer des volumes de sauvegarde; par défaut: ne pas supprimer\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 ERROR: volext-pathname n'existe pas ou n'est pas accessible en écriture.\n
32 ERROR: vols-tofrom-paths_file n'existe pas ou n'est pas lisible.\n
60 \
renamedb: Renomme une base de données.\n\
usage: %s renamedb [OPTION] src-database-name dest-database-name\n\
\n\
options valids:\n\
  -E, --extended-volume-path=CHEMIN    CHEMIN de répertoire des volumes d'informations complémentaires;\n\
                                       par défaut: même chemin que les volumes de base de données o\n\
                                       le chemin donné dans control-file\n\
  -i, --control-file=FICHIER    renomme les volumes existants sur plusieurs sites écrites dans le FICHIER de contrôle; par défaut: aucun\n\
  -d, --delete-backup           supprime les volumes de sauvegarde; par défaut: ne pas supprimer\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 Un processus %s est déjà en cours d'exécution.\n
12 SERVEUR MAÎTRE démarrant. \n
13 SERVEUR MAÎTRE sortant.\n
14 Erreur de trouver le nom d'hôte local\n
15 Impossible de trouver le fichier de paramètre\n
16 Impossible d'initialiser le processus maître\n
17 Serveur %s s'arrête dans %d minutes
18 Le serveur %s est informé de l 'arrêt.\nCette opération peut durer plusieurs minutes. Attendez s'il vous plaît.
19 Serveur %s n'est pas trouvé.
20 Maître s'arrêt dans %d minutes\n


$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Impossible d'ouvrir le fichier de sortie '%s'\n
60 \
optimizedb: Optimise une base de données.\n\
usage: %s optimizedb [OPTION] database-name\n\
\n\
options valids:\n\
  -n, --class-name=NOM        mettre à jour les statistiques de la classe ayant ce NOM; par défaut: toutes les classes\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Format de date non valide, format valide est dd-mm-yyyy:hh:mm:ss ou mot-clé 'backuptime'\n
20 Annulation du RestoreDB ou une erreur s'est produite.\n
60 \
restoredb: Restaure une de base de données.\n\
usage: %s restoredb [OPTION] database-name\n\
\n\
options valids:\n\
  -d, --up-to-date=DATE             restaure la base de données à son état à la date donnée. DATE est donnée comme suit: dd-mm-yyyy:hh:mm:ss ou mot-clé 'backuptime' keyword\n\
      --list                        affiche la liste des volumes de sauvegarde sans restaurer\n\
  -B, --backup-file-path=CHEMIN     CHEMIN du répertoire de volumes de sauvegarde à restaurer\n\
  -l, --level=NIVEAU                NIVEAU de sauvegarde à restaurer, consultez l'usage de sauvegarde; par défaut: 0 (sauvegarde complète)\n\
  -p, --partial-recovery            effectue une récupération partielle si une archive de journal est manquante\n\
  -o, --output-file=FICHIER         redirige les messages de sortie à FICHIER; par défaut: aucun\n\
  -u, --use-database-location-path  restaure les volumes de base de données et du journal dans le chemin spécifié dans le fichier d'emplacement de base de données\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 Le nom de la de base de données est manquant.\n
20 Fichier '%s' n'a pas de %d lignes.\n
21 interruption ... \n
22 Interruption de chargement - transaction en cours validée\n
23 Interruption de chargement - transaction en cours abandonnée\n
24 Impossible d'ouvrir le fichier '%s'\n
25 \n*** Vérification de la syntaxe ***\n\n
26 %d erreurs détectées.\n
27 \n***  Insertion d'objets ***\n\n
28 Un total de %8d objets insérés.\n
29 Un total de %8d objets référencés inséré avec le contenu par défaut.\n
30 \n*** Validation de la transaction ***\n
31 \n*** Fermeture de la de base de données ***\n
32 Ligne %d:
33 Erreur d'analyse '%s' comme une valeur '%s' pour l'attribut '%s' de '%s'\n
34 Domaine manquante pour l'attribut %s de %s\n
35 Pour l'attribut %s de %s, type %s type est interdit dans le domaine d'ensemble\n
36 Pour l'attribut %s de %s, type attendu %s, reçu un ensemble constante\n
37 Pour l'attribut %s de %s, type attendu %s, reçu %s\n
38 Aucune classe nommée %s  référencée par l'attribut %s de %s\n
39 Aucune classe nommée %s, en ignorant des tuples\n
40 Aucune classe avec identifiant %d référencée par l'attribut %s de %s\n
41 LA classe %s est interdite dans le fichier d'entrée du chargeur\n
42 Arrêté avec une erreur.\n
43 AVERTISSEMENT: instance existante actualisée!\n
44 AVERTISSEMENT: redéfinition d'instance %d de la classe %s\n
45 Instance %d de la classe %s a été déjà  définie\n
46 Instance %d de la classe %s a été déjà réservée\n
47 Violation d'UNIQUE sur l'attribut '%s', converti en NULL\n
48   %d instances\n
49 Classe %s\n
50 Mot de passe:
51 \n*** Mise à jour des statistiques de classe ***\n
52 %s Près de ligne %d (texte était '%s')\n
53 Erreur lexicale
54 ligne %d: erreur de syntaxe à \"%s\"
55 manquant
56 dans %s
57 arguments incompatible, \"%s\", et \"%s\"\n
58 %d instances validées\n
59 \nImpossible d'invoquer la non-optimisé version de loaddb.\n
60 Erreur d'analyse du \"%s\", comme type %s.\n
112   %d instances\r
113 Dernière ligne validée: %d
114 error-control-file
115 fichier d'entrée pour le contrôle d'erreur(s) pendant le chargement
120 \
loaddb: Importe les objets et les schémas dans la base de données.\n\
usage: %s loaddb [OPTION] database-name\n\
\n\
options valids:\n\
  -u, --user=ID                 ID d'utilisateur pour l'accès à la base de données, par défaut: "public"\n\
  -p, --password=MOT-DE-PASSE   MOT DE PASSE d'utilisateur; par défaut: aucun\n\
      --data-file-check-only    vérifie la syntaxe pour le fichier de données sans chargement; par défaut: chargement après vérification\n\
  -l, --load-only               charge le fichier de données sans vérification de la syntaxe; par défaut: chargement après vérification\n\
      --estimated-size=NOMBRE   estimation du NOMBRE des instances; par défaut: 5000\n\
  -v, --verbose                 activer les messages verbeux d'état; par défaut: désactivé\n\
  -c, --periodic-commit=COMPTE  COMPTE d'insertion pour la validation périodique; par défaut: aucune validation pendant le chargement\n\
      --no-oid                  utilise pas l'OID\n\
      --no-statistics           ne met pas à jour les statistiques pendant le chargement\n\
  -s, --schema-file=FICHIER[:LIGNE] crée le schéma de la LIGNE en utilisant le FICHIER\n\
  -i, --index-file=FICHIER[:LIGNE]  crée un index de la LIGNE en utilisant le FICHIER\n\
  -d, --data-file=FICHIER           charge des données en utilisant le FICHIER\n\
  -t, --table=TABLE                 nom de la TABLE qui se substitue à en-tête de\n\
                                    classe manquante dans le fichier de données\n\
      --error-control-file=FICHIER  FICHIER de contrôle d'erreur(s) pendant le chargement\n\
      --ignore-class-file=FICHIER   FICHIER d'entrée avec les noms de classe qui saut le chargement\n
121 \
load_noopt: Cet utilitaire est obsolète. Utiliser avec précaution.\n\
usage: load_noopt [OPTION] database-name\n\
\n\
options valids:\n\
  -u, --user=ID                 ID d'utilisateur pour l'accès à la base de données, par défaut: "public"\n\
  -p, --password=MOT-DE-PASSE   MOT DE PASSE d'utilisateur; par défaut: aucun\n\
      --data-file-check-only    vérifie la syntaxe pour le fichier de données sans chargement; par défaut: chargement après vérification\n\
  -l, --load-only               charge le fichier de données sans vérification de la syntaxe; par défaut: chargement après vérification\n\
      --estimated-size=NOMBRE   estimation du NOMBRE des instances; par défaut: 5000\n\
  -v, --verbose                 activer les messages verbeux d'état; par défaut: désactivé\n\
  -c, --periodic-commit=COMPTE  COMPTE d'insertion pour la validation périodique; par défaut: aucune validation pendant le chargement\n\
      --no-oid                  utilise pas l'OID\n\
  -s, --schema-file=FICHIER[:LIGNE] crée le schéma de la LIGNE en utilisant le FICHIER\n\
  -i, --index-file=FICHIER[:LIGNE]  crée un index de la LIGNE en utilisant le FICHIER\n\
  -d, --data-file=FICHIER           charge des données en utilisant le FICHIER\n\
      --error-control-file=FICHIER  FICHIER de contrôle d'erreur(s) pendant le chargement\n\


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 Valeur de cached-pages n'est pas valide.\n
42 Valeur de cached-page-size n'est pas valide.\n
43 %d objects sauvegardées.\n
44 Classe %s
45 , %d instances.\n
46 Seulement %d de  %d objets sauvegardées avec succès.\n
47 répertoire de sortie est trop long.\n
48 LSA : %d %d \n
60 \
unloaddb: Exporte des objets et des schémas de la base de données.\n\
usage: %s unloaddb [OPTION] database-name\n\
\n\
options valids:\n\
  -i, --input-class-file=FICHIER   FICHIER d'entrée des noms de tables; par défaut: sauvegarde toutes les classes\n\
      --include-reference     comprennent les tables référencées; avec l'option -i; par défaut: désactivé\n\
      --input-class-only      comprennent seulement la classe spécifiée; avec l'option -i; par défaut: désactivé\n\
      --lo-count=NOMBRE       NOMBRE de fichiers lo par un répertoire; par défaut: 0\n\
      --estimated-size=NOMBRE estimation du NOMBRE des instances; par défaut: auto calculée\n\
      --cached-pages=NOMBRE   NOMBRE de pages mises en cache; par défaut: 100\n\
  -O, --output-path=CHEMIN    CHEMIN de répertoire de sortie, par défaut: répertoire de travail\n\
  -s, --schema-only           sauvegarde seulement le schéma; par défaut: schéma et les objets\n\
  -d, --data-only             sauvegarde seulement les objets; par défaut: schéma et les objets\n\
      --output-prefix=PRÉFIXE     PRÉFIXE pour les fichiers de sortie, par défaut: le nom de de base de données\n\
      --hash-file=FICHIER     FICHIER pour hachage; valeur par défaut: générer par tmpnam ()\n\
  -v, --verbose               active les messages verbeux d'état; par défaut: désactivé\n\
      --use-delimiter         utilise '"' dans les endroits où un identificateur commence ou se termine; par défaut: ne pas utiliser\n\
  -S, --SA-mode               exécution en mode autonome\n\
  -C, --CS-mode               exécution en mode client-serveur\n\
      --datafile-per-class    créer un fichier objet pour chaque classe; par défaut: désactivé



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nVisite 1\n\n
12 %d objets traités.\n
13 \nVisite 2\n\n
14 Classe %s\n
15 oid:  %d|%d|%d,\n
16 %d instances.\n
17 Mise à jour de l'objet.\n
18 ref_oid:  %d.%d.%d, ref_class_oid:  %d.%d.%d\n
19 Impossible de transformer l'objet\n
20 Fichier de tas manquant pour classe\n
21 Impossible de mettre l'objet à jour\n
22 Seulement %d sur %d objets traités avec succès.\n
23 COMPACTDB déjà commencé.\n
24 Le nombre de pages doit être comprise entre %d et %d.\n
25 Le délai de verrouillage d'instance doit être comprise entre %d et %d.\n
26 Total d'objets: %d\n
27 Objets échoués: %d\n
28 Objects modifiés: %d\n
29 Oobjects grands: %d\n
30 Représentations anciennes des classes supprimés de catalogue.\n
31 Représentations anciennes des classes conservées dans le catalogue.\n
32 Impossible de définir le niveau d'isolement.\n
33 Impossible de compacter la base de données.\n
34 Le délai de verrouillage de classe doit être comprise entre %d et %d.\n
35 Classe verrouillée ...Traitement incomplet...\n
36 Classe pas traitée...\n
37 Erreur de processus... Traitement incomplet...\n
38 Rien à traitir...\n
39 Paramètres invalides...\n
40 Nom de la classe inconnu...\n
41 Les OID de classe inutilisés et les pages ont été reconquéris.\n
42 Reconquérir les OID et les pages ignorées.\n
43 Erreur pendant le traitement de classe, classe sautée.\n
44 \nPass 3 \n \n
45 Compactage du tas a échoué ... \n
46 Compactage du tas a réussi ... \n
60 \
compactdb: Libére de l'espace inutilisé dans une base de données.\n\
usage: %s compactdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
options valids:\n\
  -v, --verbose                     active les messages verbeux d'état; par défaut: désactivé\n\
  -i, --input-class-file=FICHIER    FICHIER d'entrée des noms de tables\n\
                                    liste class_name doit être vide\n\
                                    par défaut: compactage de toutes les classes\n\
                                    activé seulement en mode client-serveur\n\
  -p, --pages-commited-once=NOMBRE  NOMBRE maximum de pages d'enregistrer une fois\n\
                                    par défaut: 10\n\
                                    activé seulement en mode client-serveur\n\
  -S, --SA-mode                     exécution en mode autonome\n\
  -C, --CS-mode                     exécution en mode client-serveur\n\
  -d, --delete-old-repr             supprime des représentations anciennes des classes à partir du catalogue; par défaut: désactivé\n\
  class_name_list                   activé seulement en mode client-serveur\n\
  -I, --Instance-lock-timeout       délai d'attente de verrouillage d'instance\n\
  -c, --class-lock-timeout          délai d'attente de verrouillage de classes\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 Le maître CUBRID en cours d'exécution sur l'hôte %s \na été lancé à %s
22 has serviced %d requests. %d server(s) running.\n
23 ARRÊT en %s minutes\n
24 %s
25 Nom de serveur d'arrêter:
26 Combien de minutes jusqu'à l'arrêt? :
27 \
cub_commdb: Cet utilitaire est obsolète. Utilisez la commande 'cubrid' au lieu.\n\
usage: cub_commdb [OPTION]\n\
\n\
options valids:\n\
  -P, --server-list                afficher les processus du serveur\n\
  -O, --all-list                   affiche tous les processus\n\
  -S, --shutdown-server=NOM        NOM de serveur de base de données à l'arrêt\n\
  -A, --shutdown-all               arrête tous les processus\n\
  -c, --server-mode=SERVEUR        afficher le mode de HA du SERVEUR de la base de données\n\
  -N, --node-list                  liste les nœuds gérés par CUBRID heartbeat\n\
  -L, --process-list               liste les processus locals géré par CUBRID heartbeat\n\
  -D, --dereg-process=PID          quitte le processus cible et le supprime des entrées de processus gérés\n\
  -F, --reconfig-node-list         applique la configuration du CUBRID heartbeat récemment changé\n\
  -U, --deactivate-heartbeat       désactiver la fonction CUBRID heartbeat et supprime le nœud actuel du groupe de nœuds\n\
  -T, --activate-heartbeat         active la fonction CUBRID heartbeat et rend le nœud actuel un membre du groupe de nœuds\n\
  -V, --verbose                    active les messages verbeux d'état; par défaut: désactivé\n
28 Commande inconnue: %s\n
29 Impossible d'obtenir le nom d'hôte de l'hôte local - abandon\n
30 Impossible de trouver le fichier de paramètre\n
31 Impossible de se connecter au serveur maître sur %s\n
32 Combien de minutes jusqu'à l'arrêt? :
33 Maître s'arrêtera dans %d minutes\n
34 Erreur de réception des données de client
35 LE serverur de base de données %s n'est pas en cours d'exécution
36 Ne peut pas exec shell (%s) pour %s, %s
37 Fork du processus %s, a retourné %d, %s
38 Impossible d'exécuter %s


$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: Patche une base de données en cas d'urgence.\n\
usage: %s emergency_patchlog [OPTION] database-name\n\
\n\
options valids:\n\
  -r, --recreate-log           recreate log files; use carefully\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Valeur incorrecte %d pour npages\n
21 Valeur incorrecte '%s' dans le but de stockage de volume.\n
22 WARNING: --max-writesize-in-sec option is ignored in stand-alone mode execution.\n
60 \
addvoldb: Ajouter un volume à la base de données.\n\
usage: %s addvoldb [OPTION] database-name\n\
\n\
options valids:\n\
      --db-volume-size=TAILLE      TAILLE du volume supplémentaire; par défaut: db_volume_size dans cubrid.conf\n\
      --max-writesize-in-sec=SIZE  the amount of volume written per second; (ex. 512K, 1M, 10M); default: not used; minimum: 160K\n\
  -n, --volume-name=NOM            NOM du volume d'information; par défaut: générer tels que "db"_ext1)\n\
  -F, --file-path=CHEMIN           CHEMIN pour l'ajout de fichier de volume; par défaut: répertoire de travail\n\
      --comment=COMMENTAIRE        COMMENTAIRE pour ajouter du volume; par défaut: aucun\n\
  -p, --purpose=BUT                BUT pour l'ajout de fichier de volume; permis:\n\
                                           DATA - seulement pour les données\n\
                                           INDEX - seulement pour les indexes\n\
                                           TEMP - seulement pour les temporaires\n\
                                           GENERIC - à tous les buts\n\
  -S, --SA-mode                    exécution en mode autonome\n\
  -C, --CS-mode                    exécution en mode client-serveur\n\

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Some inconsistencies were detected in your database.\n Please consult error_log_file %s for additional information.\n
21 %s: Cannot find the class. It will be ommitted.\n
60 \
checkdb: Check consistency of the database.\n\
usage: %s checkdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
options valids:\n\
  -S, --SA-mode                exécution en mode autonome\n\
  -C, --CS-mode                exécution en mode client-serveur\n\
  -r, --repair                 répare quand incohérence est trouvée\n\
  -i, --input-class-file=FICHIER  FICHIER d'entrée des noms de classe\n\
                               par défaut: vérifier toutes les classes


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 Description de l'espace pour la base de données '%s' avec la taille de page de %s. (taille de page du journal: %s)\n\n
16 %5d %8s  %s %s  %s\n
17 Impossible d'ouvrir le fichier de sortie '%s'\n
18 Description de l'espace pour les volumes temporaires pour la base de données '%s' avec la taille de page de %s.\n\n
19 Volid  Purpose  total_pages  free_pages  Vol Name\n\n
20 Volid  Purpose   total_size   free_size  Vol Name\n\n
21 Description de l'espace LOB %s \n\n
30 Description résumée de l'espace de base de données '%s' avec la taille de page de %s. (taille de page du journal: %s)\n\n
31 Purpose    total_pages  used_pages  free_pages  volume_count\n
32 Purpose     total_size   used_size   free_size  volume_count\n
33 %10s %s %s %s %10d\n
60 \
spacedb: afficha l'espace utilisé/libre de la base de données.\n\
usage: %s spacedb [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FICHIER    redirige les messages de sortie à FICHIER; par défaut: aucun\n\
  -S, --SA-mode                exécution en mode autonome\n\
  -C, --CS-mode                exécution en mode client-serveur\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               définir l'unité d'affichage (page, mega, giga, tera, human readable); par défaut: H\n\
  -s, --summarize              afficher seulement un sommaire pour chaque but de volume\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Nombre estimatif de pages = %d\n
60 \
estimate_data: taille estimée de données.\n\
usage: %s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Type incorrect de clé d'index spécifiée = %s\n
16 Longueur moyenne de clé spécifiée est incorrecte = %d lorsque la précision = %d\n
17 Aguments spécifiés incorrects\n
20 Estimation du nombre total des pages d'index = %d\n (Lorsque l'index créé, après les instances ont été peuplées.)\n
21 Estimation du nombre total des pages d'index = %d\n (Lorsque l'index créé, devant les instances ont été peuplées (cas moyen))\n
22 21 Estimation du nombre total des pages d'index = %d\n (Lorsque l'index créé, devant les instances ont été peuplées (le plus défavorable cas))\n
23 Pour l'entrée d'étant donné...\n
24 Nombre total d'instances   : %d\n
25 Nombre de clés différentes : %d\n
26 Taille moyenne des clés    : %d\n
27 Type de clé                : %s\n\n
60 \
estimate_index: Estime de la taille d'un index.\n\
usage: %s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Installe une base de données.\n\
usage: %s installdb [OPTION] database-name\n\
\n\
options valids:\n\
      --server-name=HÔTE   nom d'hôte du serveur; par défaut: nom d'HÔTE actuel\n\
  -F, --file-path=CHEMIN   CHEMIN du répertoire de volumes de base de données; par défaut: répertoire courant\n\
  -L, --log-path=CHEMIN    CHEMIN du répertoire de volumes de journal; par défaut: même que file-path\n\

$set 23 MSGCAT_UTIL_SET_MIGDB
1 OID de classe temporaire\n
2 Impossible d'afficher les valeurs elo\n
3 Erreur lors de l'accès à un long objet pour la migration\n
4 Erreur lors de l'ouverture du fichier de sortie pour la migration d'objet long \n
5 Erreur de lecture\n
6 Erreur d'écriture\n
7 Erreur d'ouverture du fichier d'entrée ELO "%s"\n
8 CLASSES:
9 nom du fichier de hachage
10 Nom:                                    %s\n
11 Taille:                                 %d\n
12 Taille de la page:                      %d\n
13 Taille des données:                     %d\n
14 Taille d'entrée:                        %d\n
15 Enregistrations par page:               %d\n
16 Pages mises en cache:                   %d\n
17 Nombre d'entrées:                       %d\n
18 Nombre de collisions:                   %d\n
19 Nom du fichier de hachage:              %s\n
20 Entrée de débordement suivant:          %d\n
21 Type de clé:                            %s\n
22 Adresse des en-têtes de page:           %x\n
23 Adresse de la dernière en-tête de page: %x\n
24 Adresse d'en-tête de page libre:        %x\n
25 Adresse du bitmap des pages:            %x\n
26 Taille du bitmap des pages:             %d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Dump a database.\n\
usage: %s diagdb [OPTION] database-name\n\
\n\
options valids:\n\
  -d, --dump-type=TYPE         TYPE de sauvegarde\n\
                               -1 sauvegarde tout\n\
                                1 sauvegarde les tables\n\
                                2 sauvegarde capacités des fichiers\n\
                                3 sauvegarde capacités des tas\n\
                                4 sauvegarde capacités des indexes\n\
                                5 sauvegarde noms des classes\n\
                                6 sauvegarde bitmaps du disque\n\
                                7 sauvegarde catalogue\n\
                                8 sauvegarde journal\n\
                                9 sauvegarde tas\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 Impossible d'ouvrir le fichier de sortie '%s'\n
59 lockdb ne peut pas fonctionner en mode autonome.\n
60 \
lockdb: Affiche l'état du verrouillage de la base de données.\n\
usage: %s lockdb [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FICHIER   redirige les messages de sortie à FICHIER; par défaut: aucun\n\


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Un seul argument doit être donné parmi: tran-index, utilisateur, hôte, nom du programme\n
21 Entrez mot de passe DBA:
26 Il n'ya pas de transaction avec les valeurs d'arguments spécifiés.\n
27 Prêt à arrêter les transactions suivantes:\n\n
28 Voulez-vous continuer? (Y/N)
29 Arrêter transaction associée à l'index %d de transaction\n
30 L'arrêt des transactions suivantes a échoué\n\n
31 La transaction n'est pas encore fini, mais il peut faire le travail de restauration.\n S'il vous plaît, attendez un peu et essayer plus tard si nécessaire.\n
32 Invalid tranindex(%s).\n
59 killtran ne peut pas fonctionner en mode autonome.\n
60 \
killtran: Gère les transactions de la base de données.\n\
usage: %s killtran [OPTION] database-name\n\
\n\
options valids:\n\
  -i, --kill-transaction-index=INDEX arrête tous les transaction avec l'INDEX donné\n\
      --kill-user-name=ID            arrête tous les transactions avec with l'utilisateur ID\n\
      --kill-host-name=HÔTE          arrête tous les transactions avec le client HÔTE\n\
      --kill-program-name=NOM        arrête tous les transactions avec cet NOM du programme du client\n\
      --kill-sql-id=SQL_ID           kill all transactions with SQL_ID\n\
  -p, --dba-password=MOT-DE-PASSE    MOT DE PASSE de l'utilisateur DBA; demandé si ne pas spécifier\n\
  -d, --display-information          affiche des informations sur les transactions actives\n\
  -q, --query-exec-info              display information about query execution information of active transactions\n\
  -f, --force                        arrête la transaction sans demande de vérification\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 database-name
2 argument obligatoire, aucun défaut
3 compatibility-check
4 Seulement verification du compatibilité
5 backup-destination
6 un chemin de répertoire ou d'un dispositif pour la destination des volumes de sauvegarde
20 Base de données '%s' n'existe pas.
21 '%s' ne peut pas être migrées vers %s, car sa version de base est trop petite.\n
22 '%s' ne doivent pas être migrés.\n
23 '%s' doit être migré.\n
24 La sauvegarde est recommandée après la migration. Voulez-vous sauvegarder votre base de données maintenant? (y/n):
25 Nous vous recommandons de sauvegarder votre nouvelle base de données avec la commande backupdb après la migration.
26 \n*** ATTENTION: Sauvegardez votre base de données avant de commencer la migration vers %s ***\n\nPour récupérer des erreurs qui pourraient être survenus au cours de la migration, nous vous recommandons vivement de sauvegarder votre base de données.\nAvez-vous déjà sauvegardé votre base de données? (y/n):
27 \n1. Exécutez backupdb \n2. Entrez le chemin de sauvegarde pour la nouvelle base de données (chemin actuel: %s)\n3. Quttez \n
28 \nLe nouveau chemin de sauvegarde pour la base de données::
29 \nMigration et sauvegarde ont été effectuées avec succès.
30 Échec de la sauvegarde. S'il vous plaît, essayez de sauvegarder votre base de données.
31 \n\n*** ATTENTION : Lorsque la migration est terminée, vous ne pouvez pas restaurer la base de données avec les volumes anciens de sauvegarde.\nNotez que la version précédente des fichiers de sauvegarde peuvent être restaurées uniquement sur la version correspondante de restoredb.\nS'il vous plaît vérifier les volumes de sauvegarde anciens sont conservés en sécurité.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Modifie un hôte de la base de données.\n\
usage: %s alterdbhost [OPTION] database-name\n\
\n\
options valids:\n\
  -h, --host=HÔTE     nouveau nom d'HÔTE, par défaut: nom d'hôte actuel\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 database-name
2 argument obligatoire, aucun défaut
3 java-class-file
4 argument obligatoire, aucun défaut
5 force-overwrite
6 remplace le fichier de classe si lui existe, par défaut non

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Impossible d'ouvrir le fichier de sortie '%s'\n
59 plandump ne peut pas fonctionner en mode autonome.\n
60 \
plandump: Affiche ou contrôle les plans de requêtes mis en cache.\n\
usage: %s plandump [OPTION] database-name\n\
\n\
options valids:\n\
  -d, --drop                   supprime tous les plans dans le cache du serveur\n\
  -o, --output-file=FICHIER    redirige les messages de sortie à FICHIER; par défaut: aucun\n\

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Impossible d'ouvrir le fichier de sortie '%s'\n
21 Paramètres du système pour être utilisé par le programme client.\n
22 Paramètres système chargés par le serveur '%s'\n
23 Paramètres du système pour être utilisés en mode autonome.\n
60 \
paramdump: Sauvegarde les réglages des paramètres du système.\n\
usage: %s paramdump [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FILE     redirige les messages de sortie à FICHIER; par défaut: aucun\n\
  -b, --both                 sauvegarder les paramètres client et serveur\n\
  -S, --SA-mode              exécution en mode autonome\n\
  -C, --CS-mode              exécution en mode client-serveur\n\

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Valeur incorrecte \"%s\" pour le mode d'exécution HA du serveur.\n
12 Impossible de changer le mode d'exécution HA du serveur.\n
21 Entrez mot de passe DBA:
22 Le mode HA courant d'exécution de '%s' du serveur est %s.\n
23 Le mode d'exécution HA du serveur '%s' est modifié pour \"%s\".\n
24 Le serveur n'a pas été configuré pour HA.\n
58 HA fonctionnalité n'est pas supportée par cette plate-forme.\n
59 changemode ne peut pas fonctionner en mode autonome.\n
60 \
changemode: Affiche ou modifie le mode de exécution HA du serveur.\n\
usage: %s changemode [OPTION] database-name\n\
\n\
options valids:\n\
  -m, --mode=MODE             changer le mode du serveur à MODE; par défaut: aucun\n\
  -f, --force                 force à changer\n\
  -t, --timeout=S             d'attente S seconde pendant transaction active se termine normalement\n\
                              option -t option est efficace seulement lorsque l'utilisateur modifie le mode serveur pour la maintenance\n\
                              par défaut: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Valeur incorrecte \"%s\" pour le mode de fonctionnement de copylogdb\n
21 Entrez mot de passe DBA:
22 Le serveur n'a pas été configuré pour HA.\n
58 HA fonctionnalité n'est pas supportée par cette plate-forme.\n
59 copylogdb ne peut pas fonctionner en mode autonome.\n
60 \
copylogdb: Copiez les pages du journal du serveur.\n\
usage: %s copylogdb [OPTION] database-name\n\
\n\
options valids:\n\
  -L, --log-path=CHEMIN     CHEMIN vers un répertoire pour les volumes de journaux copiés\n\
  -m, --mode=MODE           mode de fonctionnement; par défaut: sync\n\
                            MODE: sync ou semisync ou async\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 Entrez mot de passe DBA:
22 Le serveur n'a pas été configuré pour HA.\n
58 HA fonctionnalité n'est pas supportée par cette plate-forme.\n
59 applylogdb ne peut pas fonctionner en mode autonome.\n
60 \
applylogdb: Applique journal copié sur le serveur.\n\
usage: %s applylogdb [OPTION] database-name\n\
\n\
options valids:\n\
  -L, --log-path=CHEMIN       CHEMIN vers un répertoire des volumes copiés de journal\n\
      --max-mem-size=SIZE     TAILLE maximum de processus en Mo; max 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Impossible d'ouvrir le fichier de sortie '%s'\n
59 statdump ne peut pas fonctionner en mode autonome.\n
60 \
statdump: Sauvegarde les statistiques globales.\n\
usage: %s statdump [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FICHIER    redirige les messages de sortie à FICHIER; par défaut: aucun\n\
  -c, --cumulative             affiche les statistiques cumulatives\n\
  -i, --interval=S             affiche les statistiques de S à S secondes\n\
  -s, --substr=CHAÎNE          affiche seulement les articles statistiques qui correspondent à le CHAÎNE\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 Entrez mot de passe DBA:
22 Le serveur n'a pas été configuré pour HA.\n
58 HA fonctionnalité n'est pas supportée par cette plate-forme.\n
59 applyinfo ne peut pas fonctionner en mode autonome.\n
60 \
applyinfo: affiche information de CUBRID HA Apply.\n\
usage: %s applyinfo [OPTION] database-name\n\
\n\
options valids:\n\
  -r, --remote-host-name        nom d'hôte distant; affiche les informations du journal actif du noeud distant\n\
  -a, --applied-info 	        affiche infomation appliquée\n\
  -L, --copied-log-path=CHEMIN  CHEMIN de volumes du journal copiés; affiche les informations du journal copié\n\
  -p, --pageid=ID               id de page; par défaut : 0(page active)\n\
  -v, --verbose                 active les messages verbeux d'état; par défaut: désactivé\n\
  -i, --interval=S            print information every S secs\n	

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb ne peut pas fonctionner en mode autonome.\n
60 \
acldb: Affiche ou recharge les paramètres de contrôle d'accès de la base de données.\n\
usage: %s acldb [OPTION] database-name\n\
\n\
options valids:\n\
  -r, --reload       recharger le fichier de commande d'IP\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Génère bibliothèque partagée locale à l'aide du fichier(s) d'entrée LDML.\n\
usage: %s genlocale [OPTION] [language-string]\n\
\n\
language-string	      chaîne de paramètres régionaux pour laquelle générer la bibliothèque partagée.\n\
		      Si aucune chaîne de paramètres régionaux est donné, toutes les langues dans\n\
		      'cubrid_locales.txt' seront traitées.\n\
options valids:\n\
  -v, --verbose            active les messages verbeux d'état; par défaut: désactivé\n\
  -i, --input-ldml-file    chemin de fichier LDML d'entrée. Valide seulement lorsque le\n\
                           chaîne paramètres régionaux est donnée.\n
61 Argument de nom de paramètre régional est invalid.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 Impossible d'utiliser avec -i/--input-file et language-string simultanément.\n
58 Plage invalide des caractères codés (-s -e).\n
59 Argument de nom de paramètre régional est invalid.\n
60 \
dumplocale: affiche le contenu de la bibliothèque partagée précédemment généré à l'aide du fichier(s) LDML d'entrée.\n\
usage: %s dumplocale [OPTION] [language-string]\n\
\n\
language-string	      chaîne de paramètres régionaux pour laquelle générer la bibliothèque partagée.\n\
                      Si aucune chaîne de paramètres régionaux est donné, toutes les langues dans\n\
                      'cubrid_locales.txt' seront traitées.\n\
options valids:\n\
  -i, --input-file    chemin de bibliothèque partagée d'entrée avec des\n\
                      paramètres régionaux (généré précédemment).\n\
                      Peut être utilisé seulement lorsque language-string n'est pas donné..\n\
  -d, --calendar      affiche les informations de calendrier et de date/heure;\n\
                      par défaut: désactivé\n\
  -n, --numeric	      affiche des informations numériques; par défaut: désactivé\n\
  -a, --alphabet=l|lower|u|upper|both	       affiche d'informations d'alphabet et de casse;\n\
                                               par défaut: désactivé\n\
      --identifier-alphabet=l|lower|u|upper    affiche d'informations d'alphabet et de casse pour\n\
                                               identificateurs; par défaut: désactivé\n\
  -c, --codepoint-order	 affiche d'information de classement triée sur la base des valeurs\n\
                         des caractères codés\n\
                         (cp, char, valeur pondérée, next-cp, char et valeur pondérée);\n\
                         par défaut: désactivé\n\
  -w, --weight-order  affiche d'information de classement triée sur la base des\n\
                      valeurs pondérées des caractères\n\
                      (valeurs pondérées, cp, char); par défaut: désactivé\n\
  -s, --start-value	  option de filtrage; caractères codés initiale pour\n\
                      -a, --identifier-alphabet, -c, -w; par défaut : 0\n\
  -e, --end-value     filtering option; caractères codés de fin pour \n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      par défaut : valeur maximale luée à partir de la bibliothèque partagée\n\
  -k, --console-conversion  affiche les données de conversion de la console; par défaut: désactivé\n\
  -z, --normalization       affiche les données de normalisation; par défaut: désactivé\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 Partitioning must be removed before changing the current collation '%s' of their partitioning attribute.\nClass name\n
44 Foreign keys on attributes having collation '%s' must be dropped before changing collation of attributes.\nTable | Foreign Key\n
45 Classes having collation '%s'; they should be redefined :\nClass name\n
46 Function indexes having expression containing collation '%s'; they should be redefined :\n\
Table | Index | Function expression\n
47 Synchronization of collations was not performed. Database was not changed.\n
48 Contents of '%s' system table was updated with new system collations.\n
49 Updating system collations may cause corruption of database. Continue (y/n) ?\n
50 Collation '%s' (Id: %d) not found in database or changed in new system configuration.\n
51 Triggers having condition containing collation '%s'; they should be redefined :\nTrigger | Condition\n
52 Query views containing collation '%s'; they should be redefined :\nView | Query\n
53 Attributes having collation '%s'; they should be redefined.\nTable | Attribute Domain\n
54 Manual intervention on database is required.\n\
Please check file '%s' and execute it before forcing collations into database.\n
55 Synchronization is not needed.\n
56 Synchronization of system collation into database is required.\n\
Run 'cubrid synccolldb -f %s'\n
57 There are %d new collations in system configuration compared to database:\n
58 All collations in database '%s' are found in the new system configuration.\n
59 There are %d collations in database which are not configured or are changed compared to system collations.\n
60 \
synccolldb: Synchronizes the collations from database with collations of system.\n\
usage: %s synccolldb [OPTION] database-name\n\
\n\
valid options:\n\
  -c, --check-only    Checks the collations from databases with collation of system.\n\
		      If collations synchronization is required, error code 1 is returned.\n\
		      Displays a report of incompatible collations.\n\
  -f, --force-only    Updates the collations in databases to be same as the collations of new sytem configuration.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Enter %s password: 
22 Index de tran.       Nom d'utilisateur   Nom de l'hôte   ID de processus   Nom du programme\n
23 -------------------------------------------------------------------------------\n
24 %4d%-10s %14s %14s %15d %17s\n
25 Il n'existe pas de transactions\n
26 tranlist can only be performed by the DBA or a DBA group member.\n
32 Tran index         Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %4d%-10s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index          User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %4d%-10s %14s %14s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %d
46 SQL_ID: %s\n
59 tranlist ne peut pas fonctionner en mode autonome.\n
60 \
tranlist: Dump active transaction information.\n\
usage: %s tranlist [OPTION] database-name\n\
\n\
valid options:\n\
  -u, --user=ID                user ID for database access; default: DBA user\n\
  -p, --password=PASS          user password; default: none\n\
  -s, --summary                print summarized information of transaction\n
