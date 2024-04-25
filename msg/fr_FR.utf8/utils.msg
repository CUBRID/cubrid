
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
$       51      gen_tz
$       52      dump_tz
$       53      restoreslave
$       54      delvoldb
$       56      checksumdb
$       57      tde
$       58      flashback
$       59      memmon

$set 1 MSGCAT_UTIL_SET_GENERIC
1 Longueur ou caractère non valide '%1$c' dans le nom de la base de données '%2$s'\n
2 Impossible d'ouvrir le fichier de sortie '%1$s'\n
3 Nom d'utilitaire manquant ou invalide. \n
4 Nom d'utilitaire ou invalide '%1$s'\n
5 La liste suivante contient les noms d'utilitaires reconnus: \n
6 caractère invalide '%1$c' dans le nom du volume '%2$s'\n
9 \
%1$s (utilitaires CUBRID)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation.\nCopyright (C) 2016 CUBRID Corporation.\n
10 \
utilitaire d'administration CUBRID, version %1$s\n\
usage: %2$s <utility-name> [args]\n\
Entrez '%3$s <utility-name>' pour l'aide d'un utilitaire spécifique.\n\
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
    gen_tz\n\
    dump_tz\n\
    restoreslave\n\
    vacuumdb\n\
    tde\n\
    flashback\n\
\n\
%4$s est un outil de SGBD.\n\
Pour plus d'informations, reportez-vous à http://www.cubrid.org\n
12 '%1$s' Nom d'utilitaire pas valid.\nEntrez 'cubrid --help' pour voir l'utilisation.\n
13 '%1$s' commande is invalide.\nEntrez 'cubrid --help' pour voir l'utilisation.\n
14 Impossible de charger une ou plusieurs propriétés obligatoires dans les fichiers de configuration..\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' ne s'exécute pas.\n
18 ++ %1$s ne s'exécute pas.\n
19 ++ %1$s '%2$s' est en cours d'exécution.\n
20 ++ %1$s est en cours d'exécution.\n
21 ++ %1$s %2$s: %3$s\n
22 Un ou plusieurs arguments sont manquants.\nEntrez 'cubrid --help' pour voir l'utilisation.\n
23 utilitaire CUBRID, version %1$s\n\
usage: %2$s <utility-name> [args]\n\
Entrez '%3$s <utility-name>' pour aide sur un utilitaire spécifique.\n\
\n\
Utilitaires de service disponibles\n\
    service\n\
    server\n\
    broker\n\
    gateway\n\
    manager\n\
    heartbeat\n\
    javasp\n\
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
    restoreslave\n\
    spacedb\n\
    unloaddb\n\
    paramdump\n\
    statdump\n\
    changemode\n\
    applyinfo\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
    gen_tz\n\
    dump_tz\n\
    vacuumdb\n\
    checksumdb\n\
    tde\n\
    flashback\n\
\n\
%4$s is a tool for DBMS.\n\
Pour plus d'informations, reportez-vous à http://www.cubrid.org\n
24 utilitaire CUBRID, version %1$s\n\
usage: %2$s service <command>\n\
\n\
Commandes disponibles:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 utilitaire CUBRID, version %1$s\n\
usage: %2$s server <command> [args]\n\
\n\
Commandes disponibles:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status\n\
    acl         <status|reload> database-name\n
37 Le serveur a été configuré pour la haute disponibilité (HA).\n
26 utilitaire CUBRID, version %1$s\n\
usage: %2$s broker <command> [args]\n\
\n\
Commandes disponibles:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [-u] [<expr>]\n\
    on          broker-name\n\
    off         broker-name\n\
    reset       broker-name\n\
    acl         <status|reload> [broker-name]\n\
    getid       -b <broker-name> [-f] shard-key\n\
    test        broker-name [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 utilitaire CUBRID, version %1$s\n\
usage: %2$s manager <command>\n\
\n\
Commandes disponibles:\n\
    start\n\
    stop\n\
    status\n
28 utilitaire CUBRID, version %1$s\n\
usage: %2$s heartbeat <command> [args]\n\
\n\
Commandes disponibles:\n\
    start          [database-name]\n\
    stop           [-h <host-name>] [-i] [database-name]\n\
    copylogdb      <start|stop> [-h <host-name>] database-name node-name\n\
    applylogdb     <start|stop> [-h <host-name>] database-name node-name\n\
    replication    <start|stop> node-name\n\
    status         [-v] [-h <host-name>]\n\
    reload\n
31 Argument '%1$s' n'est pas nécessaire.\n
32 Un nom de base de données est manquante.\n
33 %1$s: Telle option est obsolète.\n
34 Paramètre non valide. %1$s %2$s\n
35 Ne peut pas allouer de la mémoire.\n
36 Le serveur n'a pas été configuré pour HA.\n
38 Le serveur configuré pour HA n'apparaît pas dans ha_db_list.\n
39 Le serveur configuré pour HA n'apparaît pas dans ha_node_list.\n
40 Commande invalide.\n
41 Serveur de gestion CUBRID n'est pas installé.\n
42 Argument invalide.\n
43 Erreur d'ouverture du fichier(%1$s).\n
44 utilitaire CUBRID, version %1$s\n\
usage: %2$s javasp <command> [args]\n\
\n\
Commandes disponibles:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status      [database-name]\n
45 cubrid utility, version %1$s\n\
usage: %2$s gateway <command> [args]\n\
\n\
Available command:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-c] [-u] [<expr>]\n\
    on          gateway-name\n\
    off         gateway-name\n\
    reset       gateway-name\n\
    acl         <status|reload> [gateway-name]\n
46 La longueur de owner_name doit être inférieure à %1$d et la longueur de class_name doit être inférieure à %2$d octets.\n
47 Format non valide pour nom_propriétaire.nom_classe ('%1$s').\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 AVERTISSEMENT: l'option -t est ignoré dans le mode d'exécution autonome.\n
31 ERREUR: Le chemin de destination n'existe pas ou n'est pas un répertoire.\n
32 En utilisant l'option --separate-keys, il sépare le fichier de clé (_keys) du volume de sauvegarde.\n\
Si le fichier de clé est perdu, la restauration (restauréeb) peut ne pas fonctionner correctement même s'il existe un volume de sauvegarde.\n
33 Le volume de sauvegarde comprend le fichier de clé (_keys).\n\
Si une table est chiffrée par TDE, cela peut entraîner un problème de sécurité pour ces données.\n\
Pour séparer le fichier de clé afin de l'empêcher, utilisez l'option --separate-keys.\n
34 ERREUR: impossible d'utiliser le fichier FIFO comme chemin de destination avec une clé séparée.\n
60 \
backupdb: Sauvegarde une base de données.\n\
usage: %1$s backupdb [OPTION] database-name\n\
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
      --no-compress            ne compressez pas les volumes de sauvegarde ; par défaut : compression\n\
      --sleep-msecs=N          veille N millisecondes par 1M lit; par défaut: 0\n\
  -k, --separate-keys          fichier de clés (_keys) pour TDE n'est pas inclus dans le volume de sauvegarde, mais est séparé dans un fichier à la place\n
71 Arguments incompatibles, \"%1$s\", et \"%2$s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 ERREUR: Nom de la destination est identique au nom de source\n
31 ERREUR: volext-pathname n'existe pas ou n'est pas accessible en écriture.\n
32 ERREUR: vols-tofrom-paths_file n'existe pas ou n'est pas lisible.\n
60 \
copydb: copie une base de données.\n\
usage: %1$s copydb [OPTION] src-database-name dest-database-name\n\
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
40 Groupe '%1$s' n'existe pas\n
41 L'utilisateur '%1$s' n'existe pas\n
42 Commande inconnue '%1$s'\n
43 Impossible d'ouvrir le fichier de sortie '%1$s'\n
44 Impossible de supprimer la base de données existante.\n
45 Création de base de données avec la taille %1$s  en utilisant la valeur par défaut du locale %2$s. Le montant total de l'espace disque nécessaire est %3$s.\n
46 Impossible de créer la base de données.\n
47 Problème avec le fichier '%1$s' d'utilisateur\n
48 Le donné '%1$s', %2$s est hors plage. Choisissez une taille comprise entre %3$s et %4$s.\n
49 La valeur de %1$s '%2$s' n'est pas valide.\n
60 \
createdb: crée une base de données.\n\
usage: %1$s createdb [OPTION] database-name database-locale\n\
\n\
Le format du locale de base de données est <langue>.<jeu_de_caractères>; exemples : en_US.iso88591, ko_KR.utf8\n\
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
usage: %1$s deletedb [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FICHIER    redirige les messages de sortie dans cet FICHIER; par défaut: aucun\n\
  -d, --delete-backup          supprimer des volumes de sauvegarde; par défaut: ne pas supprimer\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 ERROR: volext-pathname n'existe pas ou n'est pas accessible en écriture.\n
32 ERROR: vols-tofrom-paths_file n'existe pas ou n'est pas lisible.\n
60 \
renamedb: Renomme une base de données.\n\
usage: %1$s renamedb [OPTION] src-database-name dest-database-name\n\
\n\
options valids:\n\
  -E, --extended-volume-path=CHEMIN    CHEMIN de répertoire des volumes d'informations complémentaires;\n\
                                       par défaut: même chemin que les volumes de base de données o\n\
                                       le chemin donné dans control-file\n\
  -i, --control-file=FICHIER           renomme les volumes existants sur plusieurs sites écrites dans le FICHIER de contrôle; par défaut: aucun\n\
  -d, --delete-backup                  supprime les volumes de sauvegarde; par défaut: ne pas supprimer\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 Un processus %1$s est déjà en cours d'exécution.\n
12 SERVEUR MAÎTRE démarrant. \n
13 SERVEUR MAÎTRE sortant.\n
14 Erreur de trouver le nom d'hôte local\n
15 Impossible de trouver le fichier de paramètre\n
16 Impossible d'initialiser le processus maître\n
17 Serveur %1$s s'arrête dans %2$d minutes
18 Le serveur %1$s est informé de l 'arrêt.\nCette opération peut durer plusieurs minutes. Attendez s'il vous plaît.
19 Serveur %1$s n'est pas trouvé.
20 Maître s'arrêt dans %1$d minutes\n
21 The heartbeat feature has been deactivated. Please wait until all shutdown processes of cub_servers are completed.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Impossible d'ouvrir le fichier de sortie '%1$s'\n
60 \
optimizedb: Optimise une base de données.\n\
usage: %1$s optimizedb [OPTION] database-name\n\
\n\
options valids:\n\
  -n, --class-name=NOM        mettre à jour les statistiques de la classe ayant ce NOM; par défaut: toutes les classes\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Format de date non valide, format valide est dd-mm-yyyy:hh:mm:ss ou mot-clé 'backuptime'\n
20 Annulation du RestoreDB ou une erreur s'est produite.\n
60 \
restoredb: Restaure une de base de données.\n\
usage: %1$s restoredb [OPTION] database-name\n\
\n\
options valids:\n\
  -d, --up-to-date=DATE             restaure la base de données à son état à la date donnée. DATE est donnée comme suit: dd-mm-yyyy:hh:mm:ss ou mot-clé 'backuptime' keyword\n\
      --list                        affiche la liste des volumes de sauvegarde sans restaurer\n\
  -B, --backup-file-path=CHEMIN     CHEMIN du répertoire de volumes de sauvegarde à restaurer\n\
  -l, --level=NIVEAU                NIVEAU de sauvegarde à restaurer, consultez l'usage de sauvegarde; par défaut: 0 (sauvegarde complète)\n\
  -p, --partial-recovery            effectue une récupération partielle si une archive de journal est manquante\n\
  -o, --output-file=FICHIER         redirige les messages de sortie à FICHIER; par défaut: aucun\n\
  -u, --use-database-location-path  restaure les volumes de base de données et du journal dans le chemin spécifié dans le fichier d'emplacement de base de données\n\
  -k, --keys-file-path              Chemin du fichier de clé (_keys) pour TDE à utiliser lors de la restauration\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 Le nom de la de base de données est manquant.\n
20 Fichier '%1$s' n'a pas de %2$d lignes.\n
21 interruption ... \n
22 Interruption de chargement - transaction en cours validée\n
23 Interruption de chargement - transaction en cours abandonnée\n
24 Impossible d'ouvrir le fichier '%1$s'\n
25 \n*** Vérification de la syntaxe ***\n\n
26 %1$d erreurs détectées.\n
27 \n***  Insertion d'objets ***\n\n
28 Un total de %1$ld objets insérés.\n
29 Un total de %1$8d objets référencés inséré avec le contenu par défaut.\n
30 \n*** Validation de la transaction ***\n
31 \n*** Fermeture de la de base de données ***\n
32 Ligne %1$d:
33 Erreur d'analyse '%1$s' comme une valeur '%2$s' pour l'attribut '%3$s' de '%4$s'\n
34 Domaine manquante pour l'attribut %1$s de %2$s\n
35 Pour l'attribut %1$s de %2$s, type %3$s type est interdit dans le domaine d'ensemble\n
36 Pour l'attribut %1$s de %2$s, type attendu %3$s, reçu un ensemble constante\n
37 Pour l'attribut %1$s de %2$s, type attendu %3$s, reçu %4$s\n
38 Aucune classe nommée %1$s  référencée par l'attribut %2$s de %3$s\n
39 Aucune classe nommée %1$s, en ignorant des tuples\n
40 Aucune classe avec identifiant %1$d référencée par l'attribut %2$s de %3$s\n
41 LA classe %1$s est interdite dans le fichier d'entrée du chargeur\n
42 Arrêté avec une erreur.\n
43 AVERTISSEMENT: instance existante actualisée!\n
44 AVERTISSEMENT: redéfinition d'instance %1$d de la classe %2$s\n
45 Instance %1$d de la classe %2$s a été déjà  définie\n
46 Instance %1$d de la classe %2$s a été déjà réservée\n
47 Violation d'UNIQUE sur l'attribut '%1$s', converti en NULL\n
48 %1$s %2$ld instances\n
49 Classe %1$s\n
50 Mot de passe:
51 \n*** Mise à jour des statistiques de classe ***\n
52 %1$s Près de ligne %2$d (texte était '%3$s')\n
53 Erreur lexicale\n
54 ligne %1$d: erreur de syntaxe à \"%2$s\"
55 manquant
56 dans %1$s
57 arguments incompatible, \"%1$s\", et \"%2$s\"\n
58 %1$s %2$ld instances validées\n
59 \nImpossible d'invoquer la non-optimisé version de loaddb.\n
60 Erreur d'analyse du \"%1$s\", comme type %2$s.\n
61 La référence d'objet n'est pas prise en charge en mode CS.\n
62 Statistiques mises à jour pour la classe %1$s.\n
112   %1$ld instances\r
113 Dernière ligne validée: %1$d\n
114 error-control-file
115 fichier d'entrée pour le contrôle d'erreur(s) pendant le chargement
116 Total %1$ld objets inséré, %2$ld objets a échoué.\n
117 Chargement échoué.\n
118 Maximum length of class name is %1$d bytes.\n
119 Total des objets %1$8d vérifiés, %2$d objets d échoué.\n
120 le fichier de données ne contient pas le spécificateur '%%class' ou l'option '--table' est manquante\n
121 La classe %1$s est ignorée.\n
122 Maximum length of user name is %1$d bytes.\n
123 %1$s est un fichier vide.\n
124 \
loaddb: Importe les objets et les schémas dans la base de données.\n\
usage: %1$s loaddb [OPTION] database-name[@host]\n\
\n\
options valids:\n\
  -S, --SA-mode                      exécution en mode autonome\n\
  -C, --CS-mode                      exécution en mode client-serveur\n\
                                     ATTENTION : En mode HA, seuls les fichiers de données (-d, --data-file=FILE) peuvent être chargés en mode client-serveur.\n\
  -u, --user=ID                      ID d'utilisateur pour l'accès à la base de données, par défaut: "public"\n\
  -p, --password=MOT-DE-PASSE        MOT DE PASSE d'utilisateur; par défaut: aucun\n\
      --data-file-check-only         check syntax for data file without load; default: disabled\n\
  -l, --load-only                    load data file without prior syntax checking; SA_MODE only\n\
      --estimated-size=NOMBRE        estimation du NOMBRE des instances; par défaut: 5000\n\
  -v, --verbose                      activer les messages verbeux d'état; par défaut: désactivé\n\
  -c, --periodic-commit=COMPTE       COMPTE d'insertion pour la validation périodique; 10240\n\
      --no-oid                       utilise pas l'OID\n\
      --no-statistics                ne met pas à jour les statistiques pendant le chargement\n\
  -s, --schema-file=FICHIER[:LIGNE]  crée le schéma de la LIGNE en utilisant le FICHIER\n\
  -i, --index-file=FICHIER[:LIGNE]   crée un index de la LIGNE en utilisant le FICHIER\n\
      --trigger-file=FICHIER[:LIGNE] crée un trigger de la LIGNE en utilisant le FICHIER\n\
  -d, --data-file=FICHIER            charge des données en utilisant le FICHIER\n\
  -t, --table=TABLE                  nom de la TABLE qui se substitue à en-tête de\n\
                                     classe manquante dans le fichier de données\n\
      --error-control-file=FICHIER   FICHIER de contrôle d'erreur(s) pendant le chargement\n\
      --ignore-class-file=FICHIER    FICHIER d'entrée avec les noms de classe qui saut le chargement\n\
      --no-user-specified-name       Find classes, serials, and triggers by their object names without their owner names.\n\
                                     This is to load files generated by cubrid before version 11.2.\n\
      --schema-file-list=FILE        Le contenu de ce fichier est une liste de noms de fichiers de schéma à utiliser dans loaddb.\n\
                                     loaddb est exécuté séquentiellement à partir du haut.\n\
                                     AVERTISSEMENT : ne peut pas être utilisé avec l'option -s.\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 Valeur de cached-pages n'est pas valide.\n
42 Valeur de cached-page-size n'est pas valide.\n
43 %1$ld objects sauvegardées.\n
44 Classe %1$s
45 , %1$d instances.\n
46 Seulement %1$ld de  %2$ld objets sauvegardées avec succès.\n
47 répertoire de sortie est trop long.\n
48 LSA : %1$d %2$d \n
49 Mot de passe:
50 Maximum length of class name is %1$d bytes.\n
51 Maximum length of user name is %1$d bytes.\n
52 Class %1$s not found.\n
60 \
unloaddb: Exporte des objets et des schémas de la base de données.\n\
usage: %1$s unloaddb [OPTION] database-name\n\
\n\
options valids:\n\
  -u, --user=ID                   ID d'utilisateur pour l'accès à la base de données, par défaut: "dba"\n\
  -p, --password=MOT-DE-PASSE     MOT DE PASSE d'utilisateur; par défaut: aucun\n\
  -i, --input-class-file=FICHIER  FICHIER d'entrée des noms de tables; par défaut: sauvegarde toutes les classes\n\
      --include-reference         comprennent les tables référencées; avec l'option -i; par défaut: désactivé\n\
      --input-class-only          comprennent seulement la classe spécifiée; avec l'option -i; par défaut: désactivé\n\
      --lo-count=NOMBRE           NOMBRE de fichiers lo par un répertoire; par défaut: 0\n\
      --estimated-size=NOMBRE     estimation du NOMBRE des instances; par défaut: auto calculée\n\
      --cached-pages=NOMBRE       NOMBRE de pages mises en cache; par défaut: 100\n\
  -O, --output-path=CHEMIN        CHEMIN de répertoire de sortie, par défaut: répertoire de travail\n\
  -s, --schema-only               sauvegarde seulement le schéma; par défaut: schéma et les objets\n\
  -d, --data-only                 sauvegarde seulement les objets; par défaut: schéma et les objets\n\
      --output-prefix=PRÉFIXE     PRÉFIXE pour les fichiers de sortie, par défaut: le nom de de base de données\n\
      --hash-file=FICHIER         FICHIER pour hachage; valeur par défaut: générer par tmpnam ()\n\
      --latest-image              lors du vidage d'objets, il vide la dernière image des objets dans les volumes de données, sans se référer aux volumes de journaux (ignorant MVCC)\n\
                                  Cela peut inclure des données non validées et des données supprimées qui ne sont pas aspirées ; par défaut : désactivé\n\
  -v, --verbose                   active les messages verbeux d'état; par défaut: désactivé\n\
      --use-delimiter             utilise '"' dans les endroits où un identificateur commence ou se termine; par défaut: ne pas utiliser\n\
  -S, --SA-mode                   exécution en mode autonome\n\
  -C, --CS-mode                   exécution en mode client-serveur\n\
      --datafile-per-class        créer un fichier objet pour chaque classe; par défaut: désactivé\n\
      --split-schema-files        diviser les informations de schéma par objet et générer chaque fichier ; Par défaut : générer un fichier de schéma avec tous les objets\n\
      --skip-index-detail         N'imprimez pas les options de la clause WITH spécifiées lors de la création de l'index ; par défaut : désactivé\n\
      --as-dba                    Si l'utilisateur de connexion est membre du groupe DBA, extrayez le même fichier de schéma que le DBA.\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nVisite 1\n\n
12 %1$d objets traités.\n
13 \nVisite 2\n\n
14 Classe %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d instances.\n
17 Mise à jour de l'objet.\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 Impossible de transformer l'objet\n
20 Fichier de tas manquant pour classe\n
21 Impossible de mettre l'objet à jour\n
22 Seulement %1$d sur %2$d objets traités avec succès.\n
23 COMPACTDB déjà commencé.\n
24 Le nombre de pages doit être comprise entre %1$d et %2$d.\n
25 Le délai de verrouillage d'instance doit être comprise entre %1$d et %2$d.\n
26 Total d'objets: %1$d\n
27 Objets échoués: %1$d\n
28 Objects modifiés: %1$d\n
29 Oobjects grands: %1$d\n
30 Représentations anciennes des classes supprimés de catalogue.\n
31 Représentations anciennes des classes conservées dans le catalogue.\n
32 Impossible de définir le niveau d'isolement.\n
33 Impossible de compacter la base de données.\n
34 Le délai de verrouillage de classe doit être comprise entre %1$d et %2$d.\n
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
47 Maximum length of class name is %1$d bytes.\n
48 Maximum length of user name is %1$d bytes.\n
60 \
compactdb: Libére de l'espace inutilisé dans une base de données.\n\
usage: %1$s compactdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
options valids:\n\
  -v, --verbose                     active les messages verbeux d'état; par défaut: désactivé\n\
  -i, --input-class-file=FICHIER    FICHIER d'entrée des noms de tables\n\
                                    liste class_name doit être vide\n\
                                    par défaut: compactage de toutes les classes\n\
  -p, --pages-commited-once=NOMBRE  NOMBRE maximum de pages d'enregistrer une fois\n\
                                    par défaut: 10\n\
                                    activé seulement en mode client-serveur\n\
  -S, --SA-mode                     exécution en mode autonome\n\
  -C, --CS-mode                     exécution en mode client-serveur\n\
  -d, --delete-old-repr             supprime des représentations anciennes des classes à partir du catalogue; par défaut: désactivé\n\
  -I, --Instance-lock-timeout       délai d'attente de verrouillage d'instance\n\
                                    activé seulement en mode client-serveur\n\
  -c, --class-lock-timeout          délai d'attente de verrouillage de classes\n\
                                    activé seulement en mode client-serveur\n\
  class_name_list                   liste de noms de classe être compactés\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 Le maître CUBRID en cours d'exécution sur l'hôte %1$s \na été lancé à %2$s
22 has serviced %1$d requests. %2$d server(s) running.\n
23 ARRÊT en %1$s minutes\n
24 %1$s
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
  -p, --ping-host                  list ping hosts used for CUBRID heartbeat\n\
  -D, --dereg-process=PID          quitte le processus cible et le supprime des entrées de processus gérés\n\
  -F, --reconfig-node-list         applique la configuration du CUBRID heartbeat récemment changé\n\
  -V, --verbose                    active les messages verbeux d'état; par défaut: désactivé\n
28 Commande inconnue: %1$s\n
29 Impossible d'obtenir le nom d'hôte de l'hôte local - abandon\n
30 Impossible de trouver le fichier de paramètre\n
31 Impossible de se connecter au serveur maître sur %1$s\n
32 Combien de minutes jusqu'à l'arrêt? :
33 Maître s'arrêtera dans %1$d minutes\n
34 Erreur de réception des données de client
35 LE serverur de base de données %1$s n'est pas en cours d'exécution
36 Ne peut pas exec shell (%1$s) pour %2$s, %3$s
37 Fork du processus %1$s, a retourné %2$d, %3$s
38 Impossible d'exécuter %1$s
39 L'option -i(--immediately) doit être utilisée avec l'option --deact-stop-all.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
1 Veuillez exécuter sans l'option --recreate-log ou supprimez le volume de journal actif manuellement.\n
60 \
emergency_patchlog: Patche une base de données en cas d'urgence.\n\
usage: %1$s emergency_patchlog [OPTION] database-name [database-locale]\n\
\n\
options valids:\n\
  -r, --recreate-log           recreate log files; use carefully (needs database-locale argument)\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Valeur incorrecte %1$d pour npages\n
21 Valeur incorrecte '%1$s' dans le but de stockage de volume.\n
22 AVERTISSEMENT: option --max-writesize-in-sec est ignorée dans le mode d'exécution autonome.\n
60 \
addvoldb: Ajouter un volume à la base de données.\n\
usage: %1$s addvoldb [OPTION] database-name\n\
\n\
options valids:\n\
      --db-volume-size=TAILLE      TAILLE du volume supplémentaire; par défaut: db_volume_size dans cubrid.conf\n\
      --max-writesize-in-sec=SIZE  la quantité de données écrite par seconde; (ex. 512K, 1M, 10M);par défaut: non utilisé; minimum: 160K\n\
  -n, --volume-name=NOM            NOM du volume d'information; par défaut: générer tels que "db"_ext1)\n\
  -F, --file-path=CHEMIN           CHEMIN pour l'ajout de fichier de volume; par défaut: volume_extension_path\n\
      --comment=COMMENTAIRE        COMMENTAIRE pour ajouter du volume; par défaut: aucun\n\
  -p, --purpose=BUT                BUT pour l'ajout de fichier de volume; permis:\n\
                                           DATA - seulement pour les données\n\
                                           INDEX - seulement pour les indexes\n\
                                           TEMP - seulement pour les temporaires\n\
                                           GENERIC - à tous les buts\n\
  -S, --SA-mode                    exécution en mode autonome\n\
  -C, --CS-mode                    exécution en mode client-serveur\n\

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Certaines incohérences ont été détectées dans votre base de données.\n Consulter le fichier  error_log_file %1$s pour plus d'informations.\n
21 %1$s: On ne peut pas trouver la classe. Elle sera omis.\n
22 %1$s: On ne peut pas trouver l'indice pour classe %2$s.\n
60 \
checkdb: Vérifier la cohérence de la base de données.\n\
usage: %1$s checkdb [OPTION] database-name [class_name1 class_name2 ...]\n\
\n\
options valids:\n\
  -S, --SA-mode                   exécution en mode autonome\n\
  -C, --CS-mode                   exécution en mode client-serveur\n\
  -r, --repair                    répare quand incohérence est trouvée\n\
  -i, --input-class-file=FICHIER  FICHIER d'entrée des noms de classe\n\
                                  par défaut: vérifier toutes les classes\
  -I, --index-name                nom de l'index qui ne sera vérifié que\n\
                                  par défaut: vérifier tous les indices\n\
      --check-prev-link           vérifier uniquement les liens précédents des indices\n\
      --repair-prev-link          réparer les chaînes précédentes indices\n\
      --check-file-tracker        vérifiez toutes les pages attribuées au fichier\n\
      --check-heap                vérifiez toutes les pages des fichiers HEAP\n\
      --check-catalog             vérifier la cohérence du catalogue\n\
      --check-btree               vérifiez tous les indices btree\n\
      --check-class-name          vérifier la cohérence des noms de classe\n\
      --check-btree-entries       vérifiez les entrées de la cohérence de btree\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
10 Description de l'espace pour la base de données '%1$s' avec la taille de page de %2$s. (taille de page du journal: %3$s)\n\n
11 Impossible d'ouvrir le fichier de sortie '%1$s'\n
12 Description de l'espace LOB %1$s \n\n
20 type                purpose            volume_count        used_pages          free_pages          total_pages\n
21 type                purpose            volume_count         used_size           free_size           total_size\n
22 PERMANENT           PERMANENT DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
23 PERMANENT           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
24 TEMPORARY           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
25 -                   -                         %1$5d     %2$13s       %3$13s        %4$13s\n
30 Description de l'espace pour tous les volumes:\n
31 volid               type                purpose            used_pages          free_pages          total_pages         volume_name\n
32 volid               type                purpose             used_size           free_size           total_size         volume_name\n
33 %1$5d               PERMANENT           PERMANENT DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
34 %1$5d               PERMANENT           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
35 %1$5d               TEMPORARY           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
40 Description détaillée de l'espace pour les fichiers:\n
41 data_type           file_count          used_pages         file_table_pages    reserved_pages      total_pages\n
42 data_type           file_count           used_size          file_table_size     reserved_size       total_size\n
43 %1$-10s               %2$5d       %3$13s            %4$13s     %5$13s    %6$13s\n
50 -----------------------------------------------------------------------------------------------------------------\n
60 \
spacedb: afficha l'espace utilisé/libre de la base de données.\n\
usage: %1$s spacedb [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FICHIER    redirige les messages de sortie à FICHIER; par défaut: aucun\n\
  -S, --SA-mode                exécution en mode autonome\n\
  -C, --CS-mode                exécution en mode client-serveur\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               définir l'unité d'affichage (page, mega, giga, tera, human readable); par défaut: H\n\
  -s, --summarize              afficher seulement un sommaire pour chaque but de volume\n\
  -p, --purpose                afficher la taille utilisée de chaque objet (données/index/temp) de chaque volume\n

$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Nombre estimatif de pages = %1$d\n
60 \
estimate_data: taille estimée de données.\n\
usage: %1$s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Type incorrect de clé d'index spécifiée = %1$s\n
16 Longueur moyenne de clé spécifiée est incorrecte = %1$d lorsque la précision = %2$d\n
17 Aguments spécifiés incorrects\n
20 Estimation du nombre total des pages d'index = %1$d\n (Lorsque l'index créé, après les instances ont été peuplées.)\n
21 Estimation du nombre total des pages d'index = %1$d\n (Lorsque l'index créé, devant les instances ont été peuplées (cas moyen))\n
22 21 Estimation du nombre total des pages d'index = %1$d\n (Lorsque l'index créé, devant les instances ont été peuplées (le plus défavorable cas))\n
23 Pour l'entrée d'étant donné...\n
24 Nombre total d'instances   : %1$d\n
25 Nombre de clés différentes : %1$d\n
26 Taille moyenne des clés    : %1$d\n
27 Type de clé                : %1$s\n\n
60 \
estimate_index: Estime de la taille d'un index.\n\
usage: %1$s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Installe une base de données.\n\
usage: %1$s installdb [OPTION] database-name\n\
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
7 Erreur d'ouverture du fichier d'entrée ELO "%1$s"\n
8 CLASSES:
9 nom du fichier de hachage
10 Nom:                                    %1$s\n
11 Taille:                                 %1$d\n
12 Taille de la page:                      %1$d\n
13 Taille des données:                     %1$d\n
14 Taille d'entrée:                        %1$d\n
15 Enregistrations par page:               %1$d\n
16 Pages mises en cache:                   %1$d\n
17 Nombre d'entrées:                       %1$d\n
18 Nombre de collisions:                   %1$d\n
19 Nom du fichier de hachage:              %1$s\n
20 Entrée de débordement suivant:          %1$d\n
21 Type de clé:                            %1$s\n
22 Adresse des en-têtes de page:           %1$x\n
23 Adresse de la dernière en-tête de page: %1$x\n
24 Adresse d'en-tête de page libre:        %1$x\n
25 Adresse du bitmap des pages:            %1$x\n
26 Taille du bitmap des pages:             %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
15 Impossible d'ouvrir le fichier de sortie '%1$s'\n
60 \
diagdb: Dump a database.\n\
usage: %1$s diagdb [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FICHIER   redirige les messages de sortie à FICHIER; par défaut: aucun\n\
      --emergency             redémarrage d'urgence; par défaut: aucun\n\
  -d, --dump-type=TYPE        TYPE de sauvegarde\n\
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
15 Impossible d'ouvrir le fichier de sortie '%1$s'\n
59 lockdb ne peut pas fonctionner en mode autonome.\n
60 \
lockdb: Affiche l'état du verrouillage de la base de données.\n\
usage: %1$s lockdb [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FICHIER   redirige les messages de sortie à FICHIER; par défaut: aucun\n\
  -c, --contention             display only lock information which has contention\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Un seul argument doit être donné parmi: tran-index, utilisateur, hôte, nom du programme\n
21 Entrez mot de passe DBA:
26 Il n'ya pas de transaction avec les valeurs d'arguments spécifiés.\n
27 Prêt à arrêter les transactions suivantes:\n\n
28 Voulez-vous continuer? (Y/N)
29 Arrêter transaction associée à l'index %1$d de transaction\n
30 L'arrêt des transactions suivantes a échoué\n\n
31 La transaction n'est pas encore fini, mais il peut faire le travail de restauration.\n S'il vous plaît, attendez un peu et essayer plus tard si nécessaire.\n
32 Tranindex(%1$s) non valid.\n
59 killtran ne peut pas fonctionner en mode autonome.\n
60 \
killtran: Gère les transactions de la base de données.\n\
usage: %1$s killtran [OPTION] database-name\n\
\n\
options valids:\n\
  -i, --kill-transaction-index=INDEX arrête tous les transaction avec l'INDEX donné\n\
      --kill-user-name=ID            arrête tous les transactions avec with l'utilisateur ID\n\
      --kill-host-name=HÔTE          arrête tous les transactions avec le client HÔTE\n\
      --kill-program-name=NOM        arrête tous les transactions avec cet NOM du programme du client\n\
      --kill-sql-id=SQL_ID           arrêter toutes les transactions avec SQL_ID\n\
  -p, --dba-password=MOT-DE-PASSE    MOT DE PASSE de l'utilisateur DBA; demandé si ne pas spécifier\n\
                                     (ne fonctionne qu'avec les options -i ou --kill. Sinon, obtient une erreur)\n\
  -d, --display-information          affiche des informations sur les transactions actives\n\
  -q, --query-exec-info              afficher des informations sur l'exécution de requêtes au sein des transactions actives\n\
  -f, --force                        arrête la transaction sans demande de vérification\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 database-name
2 argument obligatoire, aucun défaut
3 compatibility-check
4 Seulement verification du compatibilité
5 backup-destination
6 un chemin de répertoire ou d'un dispositif pour la destination des volumes de sauvegarde
20 Base de données '%1$s' n'existe pas.
21 '%1$s' ne peut pas être migrées vers %2$s, car sa version de base est trop petite.\n
22 '%1$s' ne doivent pas être migrés.\n
23 '%1$s' doit être migré.\n
24 La sauvegarde est recommandée après la migration. Voulez-vous sauvegarder votre base de données maintenant? (y/n):
25 Nous vous recommandons de sauvegarder votre nouvelle base de données avec la commande backupdb après la migration.
26 \n*** ATTENTION: Sauvegardez votre base de données avant de commencer la migration vers %1$s ***\n\nPour récupérer des erreurs qui pourraient être survenus au cours de la migration, nous vous recommandons vivement de sauvegarder votre base de données.\nAvez-vous déjà sauvegardé votre base de données? (y/n):
27 \n1. Exécutez backupdb \n2. Entrez le chemin de sauvegarde pour la nouvelle base de données (chemin actuel: %1$s)\n3. Quttez \n
28 \nLe nouveau chemin de sauvegarde pour la base de données::
29 \nMigration et sauvegarde ont été effectuées avec succès.
30 Échec de la sauvegarde. S'il vous plaît, essayez de sauvegarder votre base de données.
31 \n\n*** ATTENTION : Lorsque la migration est terminée, vous ne pouvez pas restaurer la base de données avec les volumes anciens de sauvegarde.\nNotez que la version précédente des fichiers de sauvegarde peuvent être restaurées uniquement sur la version correspondante de restoredb.\nS'il vous plaît vérifier les volumes de sauvegarde anciens sont conservés en sécurité.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Modifie un hôte de la base de données.\n\
usage: %1$s alterdbhost [OPTION] database-name\n\
\n\
options valids:\n\
  -h, --host=HÔTE     nouveau nom d'HÔTE, par défaut: nom d'hôte actuel\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 database-name
2 argument obligatoire, aucun défaut
3 java-class-file
4 argument obligatoire, aucun défaut
60 \
Usage: loadjava [OPTION] database-name java-class-file\n\
\n\
valid options:\n\
  -y, --overwrite              remplace le fichier de classe si lui existe, par défaut non\n\
  -j, --jni                    add a Class or Jar to static loading for JNI\n

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Impossible d'ouvrir le fichier de sortie '%1$s'\n
59 plandump ne peut pas fonctionner en mode autonome.\n
60 \
plandump: Affiche ou contrôle les plans de requêtes mis en cache.\n\
usage: %1$s plandump [OPTION] database-name\n\
\n\
options valids:\n\
  -d, --drop                   supprime tous les plans dans le cache du serveur\n\
  -o, --output-file=FICHIER    redirige les messages de sortie à FICHIER; par défaut: aucun\n\

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Impossible d'ouvrir le fichier de sortie '%1$s'\n
21 Paramètres du système pour être utilisé par le programme client.\n
22 Paramètres système chargés par le serveur '%1$s'\n
23 Paramètres du système pour être utilisés en mode autonome.\n
60 \
paramdump: Sauvegarde les réglages des paramètres du système.\n\
usage: %1$s paramdump [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FILE     redirige les messages de sortie à FICHIER; par défaut: aucun\n\
  -b, --both                 sauvegarder les paramètres client et serveur\n\
  -S, --SA-mode              exécution en mode autonome\n\
  -C, --CS-mode              exécution en mode client-serveur\n\

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Valeur incorrecte \"%1$s\" pour le mode d'exécution HA du serveur.\n
12 Impossible de changer le mode d'exécution HA du serveur.\n
21 Entrez mot de passe DBA:
22 Le mode HA courant d'exécution de '%1$s' du serveur est %2$s.\n
23 Le mode d'exécution HA du serveur '%1$s' est modifié pour \"%2$s\".\n
24 Le serveur n'a pas été configuré pour HA.\n
58 HA fonctionnalité n'est pas supportée par cette plate-forme.\n
59 changemode ne peut pas fonctionner en mode autonome.\n
60 \
changemode: Affiche ou modifie le mode de exécution HA du serveur.\n\
usage: %1$s changemode [OPTION] database-name\n\
\n\
options valids:\n\
  -m, --mode=MODE             changer le mode du serveur à MODE; par défaut: aucun\n\
  -f, --force                 force à changer\n\
  -t, --timeout=S             d'attente S seconde pendant transaction active se termine normalement\n\
                              option -t option est efficace seulement lorsque l'utilisateur modifie le mode serveur pour la maintenance\n\
                              par défaut: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Valeur incorrecte \"%1$s\" pour le mode de fonctionnement de copylogdb\n
21 Entrez mot de passe DBA:
22 Le serveur n'a pas été configuré pour HA.\n
58 HA fonctionnalité n'est pas supportée par cette plate-forme.\n
59 copylogdb ne peut pas fonctionner en mode autonome.\n
60 \
copylogdb: Copiez les pages du journal du serveur.\n\
usage: %1$s copylogdb [OPTION] database-name\n\
\n\
options valids:\n\
  -L, --log-path=CHEMIN     CHEMIN vers un répertoire pour les volumes de journaux copiés\n\
  -m, --mode=MODE           mode de fonctionnement; par défaut: sync\n\
                            MODE: sync ou semisync ou async\n\
  -S, --start-page-id=ID    first log page to copy\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 Entrez mot de passe DBA:
22 Le serveur n'a pas été configuré pour HA.\n
58 HA fonctionnalité n'est pas supportée par cette plate-forme.\n
59 applylogdb ne peut pas fonctionner en mode autonome.\n
60 \
applylogdb: Applique journal copié sur le serveur.\n\
usage: %1$s applylogdb [OPTION] database-name\n\
\n\
options valids:\n\
  -L, --log-path=CHEMIN       CHEMIN vers un répertoire des volumes copiés de journal\n\
      --max-mem-size=SIZE     TAILLE maximum de processus en Mo; max 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Impossible d'ouvrir le fichier de sortie '%1$s'\n
59 statdump ne peut pas fonctionner en mode autonome.\n
60 \
statdump: Sauvegarde les statistiques globales.\n\
usage: %1$s statdump [OPTION] database-name\n\
\n\
options valids:\n\
  -o, --output-file=FICHIER    redirige les messages de sortie à FICHIER; par défaut: aucun\n\
  -c, --cumulative             affiche les statistiques cumulatives\n\
  -i, --interval=S             affiche les statistiques toutes les S secondes\n\
  -s, --substr=CHAÎNE          affiche seulement les articles statistiques qui correspondent à le CHAÎNE\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 Entrez mot de passe DBA:
22 Le serveur n'a pas été configuré pour HA.\n
57 Impossible de générer les informations appliquées en raison d'un chemin non valide de l'option -L ou de l'absence d'informations associées dans la table de catalogue db_ha_apply_info.\n
58 HA fonctionnalité n'est pas supportée par cette plate-forme.\n
59 applyinfo ne peut pas fonctionner en mode autonome.\n
60 \
applyinfo: affiche information de CUBRID HA Apply.\n\
usage: %1$s applyinfo [OPTION] database-name\n\
\n\
options valids:\n\
  -r, --remote-host-name        nom d'hôte distant; affiche les informations du journal actif du noeud distant\n\
  -L, --copied-log-path=CHEMIN  CHEMIN de volumes du journal copiés; affiche les informations du journal copié\n\
  -a, --applied-info            affiche infomation appliquée; l'option -L est obligatoire\n\
  -p, --page=ID                 id de page; l'option -L est obligatoire; par défaut : 0\n\
  -v, --verbose                 active les messages verbeux d'état; par défaut: désactivé\n\
  -i, --interval=S              print information every S secs\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb ne peut pas fonctionner en mode autonome.\n
60 \
acldb: Affiche ou recharge les paramètres de contrôle d'accès de la base de données.\n\
usage: %1$s acldb [OPTION] database-name\n\
\n\
options valids:\n\
  -r, --reload       recharger le fichier de commande d'IP\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Génère bibliothèque partagée locale à l'aide du fichier(s) d'entrée LDML.\n\
usage: %1$s genlocale [OPTION] [language-string]\n\
\n\
language-string       chaîne de paramètres régionaux pour laquelle générer la bibliothèque partagée.\n\
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
usage: %1$s dumplocale [OPTION] [language-string]\n\
\n\
language-string       chaîne de paramètres régionaux pour laquelle générer la bibliothèque partagée.\n\
                      Si aucune chaîne de paramètres régionaux est donné, toutes les langues dans\n\
                      'cubrid_locales.txt' seront traitées.\n\
options valids:\n\
  -i, --input-file    chemin de bibliothèque partagée d'entrée avec des\n\
                      paramètres régionaux (généré précédemment).\n\
                      Peut être utilisé seulement lorsque language-string n'est pas donné..\n\
  -d, --calendar      affiche les informations de calendrier et de date/heure;\n\
                      par défaut: désactivé\n\
  -n, --numeric       affiche des informations numériques; par défaut: désactivé\n\
  -a, --alphabet=l|lower|u|upper|both          affiche d'informations d'alphabet et de casse;\n\
                                               par défaut: désactivé\n\
      --identifier-alphabet=l|lower|u|upper    affiche d'informations d'alphabet et de casse pour\n\
                                               identificateurs; par défaut: désactivé\n\
  -c, --codepoint-order  affiche d'information de classement triée sur la base des valeurs\n\
                         des caractères codés\n\
                         (cp, char, valeur pondérée, next-cp, char et valeur pondérée);\n\
                         par défaut: désactivé\n\
  -w, --weight-order  affiche d'information de classement triée sur la base des\n\
                      valeurs pondérées des caractères\n\
                      (valeurs pondérées, cp, char); par défaut: désactivé\n\
  -s, --start-value   option de filtrage; caractères codés initiale pour\n\
                      -a, --identifier-alphabet, -c, -w; par défaut : 0\n\
  -e, --end-value     filtering option; caractères codés de fin pour \n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      par défaut : valeur maximale luée à partir de la bibliothèque partagée\n\
  -k, --console-conversion  affiche les données de conversion de la console; par défaut: désactivé\n\
  -z, --normalization       affiche les données de normalisation; par défaut: désactivé\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 Le partitionnement doit être enlevé avant de modifier le classement actuel '%1$s' de leur attribut de partitionnement.\nNom de la classe\n
44 Les clés étrangères sur les attributs ayant classement '%1$s' doit être supprimée avant de changer de collation des attributs.\nTable | Clé étrangère\n
45 Classes ayant classement '%1$s'; elles doivent être redéfinies:\nNom de la classe\n
46 Indices de fonction ayant classement '%1$s' pour l'expression contenant; Ils doivent être redéfinis:\n\
Table | Index | Fonction expression\n
47 Synchronisation des classements n'a pas été effectuée. Base de données n'a pas été modifié.\n
48 Contenu de la table système '%1$s'  a été mis à jour avec de nouveaux classements du système.\n
49 Mise à jour des classements du système peut entraîner une corruption de base de données. Continuer (y/n)?\n
50 Classement '%1$s' (Id: %2$d) ne figure pas dans la base de données ou modifiés dans la nouvelle configuration.\n
51 Déclencheurs ayant une condition qui contient collation '%1$s'; ils doivent être redéfinis:\nDéclencheur | Condition\n
52 Les vues de requête contenant classement '%1$s'; elles doivent être redéfinies :\nVue | Requête\n
53 Attributs ayant classement '%1$s'; ils doivent être redéfinis.\nTable | Domaine d'attribut\n
54 Une intervention manuelle sur base de données est nécessaire.\n\
S'il vous plaît vérifiez le fichier '%1$s' et l'exécuter avant de forcer les classements en base de données.\n
55 Synchronisation n'est pas nécessaire.\n
56 Synchronisation de classement système dans la base de données est nécessaire.\n\
Exécutez 'cubrid synccolldb -f %1$s'\n
57 Il ya %1$d nouveaux classements dans la configuration du système par rapport à la base de données:\n
58 Tous les classements dans base de données '%1$s' se trouvent dans la nouvelle configuration du système.\n
59 Il ya %1$d classements dans la base de données qui ne sont pas configurés ou sont changés par rapport à classements du système.\n
60 \
synccolldb: Synchronise les classements de base de données avec les classements du système.\n\
usage: %1$s synccolldb [OPTION] database-name\n\
\n\
options valides:\n\
  -c, --check-only    Vérifie les classements des bases de données avec collation du système.\n\
                      Si la synchronisation des classements est nécessaire, code d'erreur 1 est retourné.\n\
                      Affiche un rapport des classements incompatibles.\n\
  -f, --force-only    Mises à jour des classements dans les bases de données d'être les mêmes que les classements de sytem nouvelle configuration.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Entrez le mot de passe %1$s:
22 Indice de tran.         Utilisateur         Nom d'hôte      ID de processus   Nom du programme\n
23 ---------------------------------------------------------------------------------\n
24 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
25 Il n'ya pas de transactions\n
26 tranlist peuvent être effectuées uniquement par le DBA ou un membre du groupe DBA.\n
27 Numéro de la colonne de clé de tri(%1$d) n'est pas valid.\n
32 Indice de tran.      Processus id  Nom du programme          Temps de req. Temps de tran.         Attendre le dét. de verr. SQL_ID       SQL Texte\n
33 %1$4d%2$-12s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 ----------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Indice de tran.     Utilisateur    Nom d'hôte     Processus id  Nom du programme          Temps de req. Temps de tran.         Attendre le dét. de verr. SQL_ID       SQL Texte\n
43 %1$4d%2$-12s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Indice de transaction: %1$d
46 SQL_ID: %1$s\n
59 tranlist ne peut pas fonctionner en mode autonome.\n
60 \
tranlist: Affiche les informations de transaction active.\n\
usage: %1$s tranlist [OPTION] database-name\n\
\n\
options valides:\n\
  -s, --summary                affichage des informations résumées de(s) transaction(s)\n\
      --sort-key=COLUMN_NUMBER numéro de colonne à trier\n\
      --reverse                inverser le résultat du tri\n

$set 51 MSGCAT_UTIL_SET_GEN_TZ
59 Mode de génération non valide indiqué.\n
60 Dossier d'entrée non valide ou corrompu.\n
61 \
gen_tz: Générer les fichiers source contenant des données de fuseau horaire à être compilés dans\n\
        une bibliothèque partagée.\n\
usage: %s gen_tz -g [new|extend] [database-name] (only for extend mode)\n\
\n\
options valides:\n\
  -i, --input-folder    chemin d'accès à un dossier d'entrée contenant une version des\n\
                        fichiers du base de données d'IANA pour les fuseaux horaires.\n\
                        Valeur par défaut: $CUBRID/timezones/tzdata\n\
  -g, --gen-mode=TYPE   contrôle la sortie de gen_tz, en se basant sur le TYPE:\n\
            TYPE:\n\
            1. NEW (par défaut): générer les fichiers source de fuseau(x) horaire(s)\n\
                        pour la bibliothèque partagée, sans tenir compte de versions précédentes;\n\
                        également générer un fichier source contenant tous les noms des pays\n\
                        et des fuseaux horaires (destiné pour l'usage du développeur,\n\
                        à être intégré dans une future version de CUBRID).\n\
            2. EXTEND : Générer un fichier source de timezone en utilisant le données\n\
                        du dossier d'entrée; nouveaux fuseaux horaires trouvés dans l'entrée\n\
                        des fichiers sont ajoutés; Offsets GMT et daylight saving informations\n\
                        sont mises à jour (ou ajoutées pour les nouveaux fuseaux horaires);\n\
                        les fuseaux horaires ne sont jamais supprimés si un fuseau horaire\n\
                        encodé dans CUBRID manque aux fichiers d'entrée, les données associés\n\
                        sont récupérées à partir de la bibliothèque de timezone existante.\n\
                        Si la première phase d'extension ne permet pas de rendre toutes\n\
                        les données compatibles avec l'arrière, toutes les lignes de toutes\n\
                        les tables ayant des types de données avec timezone seront mises à jour.\n

$set 52 MSGCAT_UTIL_SET_DUMP_TZ
59 L'ID du fuseau indiqué n'est pas compris dans la plage admissible. Utilisez -z pour voir tous\n\
les fuseaux horaires et leur IDs.\n
60 \
dump_tz: Afficher des informations sur les fuseaux horaires.\n\
usage: %s dump_tz [OPTION]\n\
\n\
options valides:\n\
  -c, --list-countries      montre la liste des pays actuellement supportées\n\
  -l, --leap-seconds        montre la liste des secondes bissextiles\n\
  -s, --summary             montre un récapitulatif de données fuseau horaire\n\
  -z, --list-zones          montre la liste des fuseaux horaires actuellement supportées\n\
  -d, --zone-id=ZONE_ID|ALL montre les informations disponibles relatives au fuseau horaire\n\
                            ayant le fourni ZONE_ID ou pour tous les fuseaux horaires. L'ID\n\
                            d'un fuseau horaire peut être obtenu à partir de la liste laquelle\n\
                            s'affiche à l'aide de l'option -z.\n

$set 53 MSGCAT_UTIL_SET_RESTORESLAVE
20 Restauration d'esclave a été annulée ou un erreur est survenu.\n
21 Restauration du base de données a été complétée. Mais initialiser l'applyinfo a échoué. (db_creation_time='%1$s', pageid=%2$lld, offset=%3$d)\n
22 Le nom de la base de données ou celui de l'hôte a pas été configuré pour HA.\n
23 Valeur incorrecte '%1$s' pour l'état du noeud source de sauvegarde.\n
60 \
restoreslave: Restaurer une base de données esclave.\n\
usage: %1$s restoreslave [OPTION] nom-base-de-données\n\
\n\
options valides:\n\
  -s, --source-state=ÉTAT          état du noeud source de sauvegarde; obligatoire\n\
                                   ÉTAT peut être 'master', 'slave', ou 'replica'\n\
  -m, --master-host-name=NOM       nom d'hôte du nœud maître actuel; obligatoire\n\
      --list                       afiche la liste des volumes de sauvegarde sans restauration\n\
  -B, --backup-file-path=VOIE      VOIE est un répertoire de volumes de sauvegarde a restaurer\n\
  -o, --output-file=FICHIER        rediriger les messages de sortie vers le FICHIER; par défaut: aucun\n\
  -u, --use-database-location-path restaurer la base de données et loguer les volumes au chemin spécifié dans le ficher d'emplacement de la base de données\n\
  -k, --keys-file-path             Chemin du fichier de clé (_keys) pour TDE à utiliser lors de la restauration\n

$set 54 MSGCAT_UTIL_SET_DELVOLDB
21 Prêt à éliminer les volumes suivantes:\n\n
22 Voulez-vous continuer ? (Y/N)
31 Pas possible de supprimer le volume système ou le premier volume (Volid : %1$d).\n
32 Impossible de trouver le volume (Volid : %1$d).\n
33 Il y a trop des Volids (Max Volids : %1$d).\n
34 Volid(%1$s) pas valide.\n
60 \
delvoldb: Supprime un volume-fichier vide depuis la base de données.\n\
usage: %1$s delvoldb [OPTION] database-name -i Volid\n\
\n\
options valides:\n\
  -c, --clear-cache                effacer tous les fichiers mises en cache dans le volume temporaire temp\n\
  -f, --force                      supprimer le volume sans un invite de vérification\n\
  -p, --dba-password=PASSE         mot de passe d'utilisateur DBA; incitera si non spécifié\n\
  -S, --SA-mode                    mode d'exécution autonome\n\
  -C, --CS-mode                    mode d'exécution client-serveur\n

$set 55 MSGCAT_UTIL_SET_VACUUMDB
20 Vacuumdb est temporairement pas disponible pour le mode client-serveur\n
21 L'operation de vidage à échoué\n
22 Impossible d'ouvrir le fichier de sortie '%1$s'\n
60 \
vacuumdb: Supprime les entrées expirés depuis la base de données.\n\
usage: %1$s vacuumdb [OPTION] database-name\n\
\n\
options valides:\n\
  -S, --SA-mode                mode d'exécution autonome\n\
  -C, --CS-mode                mode d'exécution client-serveur\n\
      --dump                   vidage de l'état actuel du vide\n\
  -o, --output-file=FICHIER    redirige les messages de sortie à FICHIER; par défaut: aucun\n

$set 56 MSGCAT_UTIL_SET_CHECKSUMDB
1 Impossible de charger les noms de table dans %1$s.\n
2 checksumdb doit être exécuté sur un serveur HA actif. %1$s (état courant: %2$s)\n
3 Le propriétaire de la table pour enregistrer le résultat de la somme de contrôle doit être DBA.\n
58 Fonctionnalité HA est pas supporté sur cette plate-forme.\n
59 checksumdb ne peut pas être exécuté dans le mode autonome.\n
60 \
checksumdb: vérifier l'intégrité de la réplication\n\
usage: %1$s checksumdb [OPTION] database-name@hostname\n\
\n\
options valides:\n\
  -c, --chunk-size=NUMÉRO          le nombre de lignes à sélectionner pour chaque calculation de somme de contrôle\n\
                                   par défaut: 500 lignes, minimum: 100 lignes\n\
  -s, --sleep=NUMÉRO               durée de la suspension d'exécution entre calculations de somme de contrôle; par défaut: 100 ms\n\
  -i, --include-class-file=FICHIER entrée FICHIER de noms des tables à inclure\n\
  -e, --exclude-class-file=FICHIER entrée FICHIER de noms des tables à exclure; seulement -i ou -e peuvent être utilisé, pas le deux\n\
  -t, --timeout=NUMÉRO             délai d'attente de requête en millisecondes; par défaut: 1000ms\n\
  -n, --table-name=NOM             NOM de la table  pour enregistrer les résultats de somme de contrôle; par défaut: dba.db_ha_checksum\n\
  -r, --report-only                rapporter l'intégrité de la replication en se basant sur la calculation des sommes de contrôle\n\
                                   tous les options, avec l'exception de --table-name, seront ignorés\n\
      --resume                     reprendre la calculation des sommes de contrôle\n\
      --schema-only                vérifier seulement la cohérence du schéma\n\
      --cont-on-error              continuer en cas d'erreur de calcul\n

$set 57 MSGCAT_UTIL_SET_TDE
21 Entrez le mot de passe DBA:
25 Impossible d'obtenir les informations clés définies dans la base de données\n
26 Tentative de modification de la clé de la clé (index: %1$d) à la clé (index: %2$d) ..\n
27 La clé est passée de la clé (index: %1$d) à la clé (index: %2$d)\n
28 La clé définie sur la base de données ne peut pas être supprimée\n
29 La clé (index: %1$d) a été supprimée\n
30 Une nouvelle clé a été générée - index de clé: %1$d créé sur%2$s
60 \
tde: gestion du chiffrement transparent des données (TDE)\n\
utilisation: %1$s tde OPERATION (-s|-n|-d|-c) [OPTION] nom-base de données\n\
\n\
options valides:\n\
    -p, --dba-password=PASS 	mot de passe de l'utilisateur DBA; demandera si ne spécifiez pas\n\
\n\
OPÉRATIONS: l'une des opérations doit être donnée, et elles sont exclusives\n\
    -s, --show-keys             imprime les informations de clé définies sur la base de données et le fichier de clé (_keys)\n\
    -n, --generate-new-key      génère une nouvelle clé dans le fichier de clé (_keys); nombre maximum: 128\n\
    -d, --delete-key=KEY_INDEX  supprime une clé dans le fichier de clés (_keys)\n\
    -c, --change-key=KEY_INDEX  modifier le jeu de clés de la base de données pour une autre clé dans le fichier de clés (_keys)\n

$set 58 MSGCAT_UTIL_SET_FLASHBACK
1 Saisissez le mot de passe DBA :
2 Format de date non valide, le format valide est jj-mm-aaaa:hh:mm:ss\n
3 La date de début (%1$s) doit être inférieure à la date de fin (%2$s)\n
4 Veuillez essayer avec une date antérieure à la date actuelle (%1$s) ou postérieure à la création de la base de données (%2$s)\n
5 La table (%1$s) n'existe pas dans la base de données\n
6 Trop de transactions pour revenir en arrière. Veuillez raccourcir l'intervalle entre la date de début et la date de fin\n
7 Impossible d'obtenir le flashback car le schéma (%1$s) a été modifié.\n
8 Le volume du journal a été supprimé. Veuillez essayer avec une date ultérieure\n
9 Impossible d'ouvrir le fichier de sortie '%1$s'\n
10 L'ID de transaction (%1$d) n'existe pas\n
11 Veuillez définir 'supplemental_log' dans 'cubrid.conf'. Seuls les journaux SQL exécutés après la définition de 'supplemental_log' peuvent faire l'objet d'un flashback.\n
12 L'ID de transaction doit être saisi dans %1$d secondes. Le délai peut être ajusté via le paramètre 'flashback_timeout' dans 'cubrid.conf'.\n
13 Flashback est déjà en cours d'exécution. Flashback ne peut pas être exécuté par deux processus.\n
14 Flashback ne peut pas s'exécuter en mode autonome.\n
15 La classe de catalogue système (%1$s) n'est pas prise en charge dans Flashback.\n
60 \
flashback : fournit des instructions pour revenir en arrière sur une transaction spécifique.\n\
utilisation : %1$s flashback [OPTION] nom-base de données nom_propriétaire.nom_classe1 [nom_propriétaire.nom_classe2 ...]\n\
\n\
options valides :\n\
    -o, --output-file=FILE        redirige le message de sortie vers FILE ; par défaut : aucun.\n\
    -u, --user=ID                 spécifiez l'utilisateur qui a exécuté l'instruction SQL à flashback ; par défaut : TOUT.\n\
    -p, --dba-password            mot de passe de l'utilisateur DBA ; demandera si vous ne spécifiez pas\n\
    -s, --start-date=DATE         spécifiez la DATE de début du flashback. Les journaux SQL sont extraits de la DATE.\n\
                                  La DATE est donnée comme suit : jj-mm-aaaa:hh:mm:ss.\n\
    -e, --end-date=DATE           spécifiez la DATE de fin du flashback. Les journaux SQL sont extraits jusqu'au DATE.\n\
                                  si la date de début n'est pas spécifiée, alors ((END - 10min) ~ END) est défini sur la plage,\n\
                                  et sinon si END n'est pas spécifié, alors (start ~ (start + 10min)) est défini.\n\
                                  Et ni une date de début ni une date de fin n'est spécifiée,\n\
                                  les logs SQL, exécutés entre le (heure actuelle - 10min) et l'heure actuelle, sont extraits.\n\
    --detail                      affiche des informations détaillées pour chaque instruction SQL.\n\
                                  L'ID de transaction, le nom d'utilisateur et l'instruction SQL d'origine seront renvoyés avec l'instruction SQL\n\
                                  pour le flash-back ; par défaut : génère uniquement des instructions SQL pour le flashback\n\
    --oldest                      flashback le plus ancien est exécuté dans l'ordre chronologique pour les instructions SQL exécutées\n\
                                  de l'heure de début à l'heure de fin ; par défaut : le plus récent\n

$set 59 MSGCAT_UTIL_SET_MEMMON
1 Veuillez définir le paramètre système 'enable_memory_monitoring' dans 'cubrid.conf' et redémarrer le serveur de base de données.\n\
2 memmon ne peut pas fonctionner en mode autonome.\n\
3 Échec de l'ouverture du fichier de sortie avec le nom de fichier %1$s.\n\
60 \
memmon: Fournit des informations sur l'utilisation de la mémoire d'un serveur de base de données.\n\
utilisation: %1$s memmon [OPTION] nom-de-la-base-de-données\n\
\n\
Option valide:\n\
    -o, --output-file=FILE    résultats de sortie vers un fichier\n
