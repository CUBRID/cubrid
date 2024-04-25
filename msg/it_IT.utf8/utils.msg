
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
1 Lunghezza non valida o un carattere '%1$c' non valido nel nome del database '%2$s'\n
2 Impossibile aprire il file di output '%1$s'\n
3 Nome di utilità mancante o non valido.\n
4 Nome di utilità non valido: '%1$s'\n
5 La seguente lista contiene i nomi di utilità riconosciute:\n
6 Carattere non valido '%1$c' nel nome del volume '%2$s'\n
9 \
%1$s (Programmi di utilità CUBRID)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation.\nCopyright (C) 2016 CUBRID Corporation.\n
10 \
utilità amministrazione cubrid, versione %1$s\n\
Uso: %2$s <nome-utilità> [args]\n\
Digitare '%3$s <nome-utilità>' per aiuto su un utilità specifico.\n\
\n\
Utilità admin disponibile:\n\
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
%4$s è un tool per DBMS.\n\
Per ulteriori informazioni, vedere http://www.cubrid.org\n
12 '%1$s' nome-utilità non è valido.\nDigitare 'cubrid --help' per aiuto.\n
13 '%1$s' comando non valido.\nDigitare 'cubrid --help' per aiuto.\n
14 Impossibile caricare una o più proprietà obbligatorie nei file di configurazione.\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' non è in esecuzione.\n
18 ++ %1$s non è in esecuzione.\n
19 ++ %1$s '%2$s' è in esecuzione.\n
20 ++ %1$s è in esecuzione.\n
21 ++ %1$s %2$s: %3$s\n
22 Uno o più argomenti sono mancanti.\nDigitare 'cubrid --help' per aiuto.\n
23 utilità cubrid, versione %1$s\n\
uso: %2$s <nome-utilità> [args]\n\
Digitare '%3$s <nome-utilità>' per informazioni su un utilità specifico.\n\
\n\
Servizi disponibili:\n\
    service\n\
    server\n\
    broker\n\
    gateway\n\
    manager\n\
    heartbeat\n\
    javasp\n\
\n\
Utilità amministrazione disponibili:\n\
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
%4$s è un tool per DBMS.\n\
Per ulteriori informazioni, vedere http://www.cubrid.org\n
24 utilità cubrid, versione %1$s\n\
uso: %2$s service <commando>\n\
\n\
Commandi disponibili:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 utilità cubrid, versione %1$s\n\
uso: %2$s server <commando> [args]\n\
\n\
Commandi disponibili:\n\
    start       [nome-database]\n\
    stop        [nome-database]\n\
    restart     [nome-database]\n\
    status\n\
    acl         <status|reload> nome-database\n
37 Il server è stato configurato per HA.\n
26 utilità cubrid, versione %1$s\n\
uso: %2$s broker <commando> [args]\n\
\n\
Commandi disponibili:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [-u] [<expr>]\n\
    on          nome-broker\n\
    off         nome-broker\n\
    reset       nome-broker\n\
    acl         <status|reload> [nome-broker]\n\
    getid       -b nome-broker [-f] shard-key\n\
    test        nome-broker [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 utilità cubrid, versione %1$s\n\
uso: %2$s manager <commando>\n\
\n\
Commandi disponibili:\n\
    start\n\
    stop\n\
    status\n
28 utilità cubrid, versione %1$s\n\
uso: %2$s heartbeat <commando> [args]\n\
\n\
Commandi disponibili:\n\
    start          [database-name]\n\
    stop           [-h <host-name>] [-i] [database-name]\n\
    copylogdb      <start|stop> [-h <host-name>] database-name node-name\n\
    applylogdb     <start|stop> [-h <host-name>] database-name node-name\n\
    replication    <start|stop> node-name\n\
    status         [-v] [-h <host-name>]\n\
    reload\n
31 argomento '%1$s' non è necessaria.\n
32 Nome di database mancante.\n
33 %1$s: Tale opzione è deprecata.\n
34 Parametro non valido. %1$s %2$s\n
35 Impossibile allocare memoria.\n
36 Il server non è stato configurato per HA.\n
38 Il server configurato per HA non è registrato nel ha_db_list.\n
39 Il server configurato per HA non è registrato nel ha_node_list.\n
40 Comando non valido.\n
41 Cubrid Manager server non è installato.\n
42 Argomento non valido.\n
43 Errore di apertura file(%1$s).\n
44 utilità cubrid, versione %1$s\n\
uso: %2$s javasp <commando> [args]\n\
\n\
Commandi disponibili:\n\
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
46 La lunghezza di nome_proprietario deve essere inferiore a %1$d e la lunghezza di nome_classe deve essere inferiore a %2$d byte.\n
47 Formato non valido per nome_proprietario.nome_classe ('%1$s').\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 ATTENZIONE: opzione -t viene ignorata in modalità di esecuzione stand-alone.\n
31 ERRORE: Destinazione non esiste o non è un folder.\n
32 Utilizzando l'opzione --separate-keys, separa il file della chiave (_keys) dal volume di backup.\n\
Se il file della chiave viene perso, il ripristino (restoreb) potrebbe non funzionare correttamente anche se è presente il volume di backup.\n
33 Il volume di backup include il file della chiave (_keys).\n\
Se una tabella è crittografata con TDE, può causare un problema di sicurezza per quei dati.\n\
Per separare il file della chiave per impedirlo, utilizzare l'opzione --separate-keys.\n
34 ERRORE: impossibile utilizzare il file FIFO come percorso di destinazione con chiave separata.\n
60 \
backupdb: Backup di un database.\n\
uso: %1$s backupdb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -D, --destination-path=PATH  memorizzare volumi di backup nell directory PATH; predefinito: database's log directory\n\
  -r, --remove-archive         eliminare inutili log-archives; usare con attenzione e consultare il manuale\n\
  -l, --level=LEVEL            livello backup; predefinito: livello full\n\
                               LEVEL consentito:\n\
                                   0 - full\n\
                                   1 - incremental 1\n\
                                   2 - incremental 2\n\
  -o, --output-file=FILE       stampare i messaggi dettagliati di backup su FILE\n\
  -S, --SA-mode                modalità di esecuzione stand-alone\n\
  -C, --CS-mode                modalità di esecuzione client-server\n\
      --no-check               non controllare la consistenza del database; predefinito: controlla\n\
  -t, --thread-count=COUNT     COUNT numero threads; predefinito: 0(auto)\n\
      --no-compress            non comprimere i volumi di backup; default: compressione\n\
      --sleep-msecs=N          aspetta N millisecondi per 1M read; predefinito: 0\n\
  -k, --separate-keys          keys file (_keys) per TDE non è incluso nel volume di backup, ma è invece separato in un file\n
71 Argomenti incompatibili, \"%1$s\", e \"%2$s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 ERRORE: Nome destinazione è identico al nome di origine.\n
31 ERRORE: volext-pathname non esiste o non è scrivibile.\n
32 ERRORE: vols-tofrom-paths_file non esiste o non è scrivibile.\n
60 \
copydb: Fa copia di un database.\n\
uso: %1$s copydb [OPZIONE] src-nome-database dest-nome-database\n\
\n\
opzioni valide:\n\
      --server-name=HOST          nome HOST di server; predefinito: nome host corrente\n\
  -F, --file-path=PATH            directory PATH per i volumi di database; predefinito: directory corrente\n\
  -L, --log-path=PATH             directory PATH per volumi di log; predefinito: stesso file-path\n\
  -B, --lob-base-path=PATH        Base directory per i dati LOB esterni; predefinito: <file-path>/lob\n\
  -E, --extended-volume-path=PATH PATH di directory per i volumi di informazioni estese; predefinito: nello stesso percorso dei volumi del database o il percorso specificato nel control-file\n\
  -i, --control-file=FILE         copia di diversi volumi di log scritti nel control-file; predefinito: nessuno\n\
  -r, --replace                   sostituire un database esistente; predefinito: no\n\
  -d, --delete-source             eliminare un database di origine; predefinito: no\n\
      --copy-lob-path             copiare PATH di archiviazione esterna per i dati LOB dal database di origine. non può essere utilizzato con l'opzione -B; predefinito: no\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 Gruppo '%1$s' non esiste\n
41 Utente '%1$s' non esiste\n
42 Comando sconosciuto '%1$s'\n
43 Impossibile aprire il file di output '%1$s'\n
44 Impossibile rimuovere database esistente.\n
45 Creazione di database con dimensioni %1$s utilizzando locale predefinito %2$s. La quantità totale di spazio su disco necessario è %3$s.\n
46 Impossibile creare database.\n
47 Problema con file utente '%1$s'\n
48 La data '%1$s', %2$s è fuori portata. Scegliere una dimensione tra %3$s e %4$s.\n
49 Il valore di %1$s '%2$s' non è valido.\n
60 \
createdb: Creare un database.\n\
usage: %1$s createdb [OPTION] database-name database-locale\n\
\n\
database-locale formato è <language>.<charset>; Esempi : en_US.iso88591, ko_KR.utf8\n\
opzioni valide:\n\
      --db-volume-size=SIZE           dimensioni del primo volume di informazioni\n\
                                      predefinito: db_volume_size nel cubrid.conf\n\
      --db-page-size=SIZE             log page SIZE in bytes; predefinito: 16K\n\
      --log-volume-size=SIZE          dimensione degli archivi di log\n\
                                      predefinito: log_volume_size nel cubrid.conf\n\
      --log-page-size=SIZE            log page SIZE in bytes; predefinito: database page size\n\
      --comment=COMMENT               COMMENT per un database; predefinito: nessuno\n\
  -F, --file-path=PATH                directory PATH per i volumi dei database; predefinito: current directory\n\
  -L, --log-path=PATH                 directory PATH per volumi di log; predefinito: same as file-path\n\
  -B, --lob-base-path=PATH            base directory PATH per esterno LOB data; predefinito: <file-path>/lob\n\
      --server-name=HOST              server's HOST name; predefinito: il nome host corrente\n\
  -r, --replace                       sostituire un database esistente; predefinito: don't replace\n\
      --more-volume-file=FILE         FILE contengono specifiche per la creazione di volumi supplementari; predefinito: nessuno\n\
      --user-definition-file=FILE     FILE contengono definizioni utenti; predefinito: nessuno\n\
      --csql-initialization-file=FILE FILE contengono SQLs per un'inizializzazione del database; predefinito: nessuno\n\
  -o, --output-file=FILE              reindirizzare i messaggi di output al FILE.; predefinito: nessuno\n\
  -v, --verbose                       attivare i messaggi dettagliati di stato; predefinito: non attivo\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: Eliminazione di un database.\n\
uso: %1$s deletedb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -o, --output-file=FILE       reindirizzare i messaggi di output a FILE; predefinito: nessuno\n\
  -d, --delete-backup          eliminare volumi di backup; predefinito: non eliminare\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 ERROR: volext-pathname non esiste o non è scrivibile.\n
32 ERROR: vols-tofrom-paths_file non esiste o non è scrivibile.\n
60 \
renamedb: Rinominare un database.\n\
uso: %1$s renamedb [OPZIONE] src-nome-database dest-nome-database\n\
\n\
opzioni valide:\n\
  -E, --extended-volume-path=PATH directory PATH per volumi di informazioni estese; predefinito: same path as the database volumes or the given path in control-file\n\
  -i, --control-file=FILE         ridenominazione di volumi diversi luoghi scritti nel control file; predefinito: nessuno\n\
  -d, --delete-backup             eliminare volumi di backup; predefinito: don't delete\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 Un processo %1$s è già in esecuzione.\n
12 MASTER SERVER in inizio. \n
13 MASTER SERVER in uscita.\n
14 Impossibile individuare il nome host locale\n
15 Impossibile trovare il file dei parametri\n
16 Impossibile inizializzare il processo master\n
17 Server %1$s si sta chiudendo in %2$d minuti
18 Server %1$s notificato di arresto.\nQuesta operazione potrebbe richiedere alcuni minuti. Si prega di attendere.
19 Server %1$s non trovato.
20 Chiusura Master in %1$d minuti\n
21 The heartbeat feature has been deactivated. Please wait until all shutdown processes of cub_servers are completed.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Impossibile aprire il file di output '%1$s'\n
60 \
optimizedb: Ottimizzare un database.\n\
uso: %1$s optimizedb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -n, --class-name=NOME        aggiornare le statistiche per la classe NOME; predefinito: tutte le classi\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Formato della data non valido, formato valido è dd-mm-yyyy:hh:mm:ss o 'backuptime'\n
20 annullato o si è verificato un errore.\n
60 \
restoredb: Ripristina un database.\n\
uso: %1$s restoredb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -d, --up-to-date=DATE             ripristinare il database fino alla sua condizione di certa DATE. DATE è dato come segue: dd-mm-yyyy:hh:mm:ss o 'backuptime' keyword\n\
      --list                        visualizzare un elenco di volumi di backup senza ripristinare\n\
  -B, --backup-file-path=PATH       PATH è una directory di volumi di backup da ripristinare\n\
  -l, --level=LIVELLO               LIVELLO di backup da ripristinare, vedere l'utilizzo di backup; predefinito: 0(backup completo)\n\
  -p, --partial-recovery            eseguire un ripristino parziale se qualsiasi archivio log è assente\n\
  -o, --output-file=FILE            reindirizzare i messaggi di output a FILE; predefinito: nessuno\n\
  -u, --use-database-location-path  ripristinare i volumi di database e di log nel percorso specificato nel file percorso del database\n\
  -k, --keys-file-path              Percorso del file chiave (_keys) per TDE da utilizzare durante il ripristino\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 Manca il nome del database.\n
20 file '%1$s' non dispone di %2$d linee.\n
21 interrompere ... \n
22 Carico interrotto - transazione corrente committed\n
23 Carico interrotto - transazione corrente aborted\n
24 Impossibile aprire il file '%1$s'\n
25 \n*** Controllo sintassi ***\n\n
26 %1$d errori rilevati.\n
27 \n*** Inserimento di oggetti ***\n\n
28 Totale %1$ld oggetti inseriti.\n
29 Totale %1$8d riferimento a oggetti inseriti con contenuto predefiniti.\n
30 \n*** Commit della transazione ***\n
31 \n*** Chiusura del database ***\n
32 Linea %1$d:
33 Errore durante l'analisi '%1$s' come '%2$s' valore per l'attributo '%3$s' di '%4$s'\n
34 Dominio mancante per attributo %1$s di %2$s\n
35 Per attributo %1$s di %2$s, tipo %3$s non è consentito nel dominio del set\n
36 Per attributo %1$s di %2$s, previsto tipo %3$s, ha ottenuto set constant\n
37 Per attributo %1$s di %2$s, previsto tipo %3$s, ha ottenuto %4$s\n
38 Nessuna classe di nome %1$s a cui fa riferimento l'attributo %2$s di %3$s\n
39 Nessuna classe di nome %1$s, tuples ignorati\n
40 Nessuna classe con identificatore %1$d viene fatto riferimento dall'attributo %2$s di %3$s\n
41 Classe %1$s non è consentito nel file di input loader\n
42 Fermato con errore.\n
43 ATTENZIONE: istanza esistente aggiornato!\n
44 ATTENZIONE: ridefinizione di istanza %1$d della classe %2$s\n
45 Instanza %1$d della classe %2$s è già stato definito\n
46 Instanza %1$d della classe %2$s è già stata riservata\n
47 UNIQUE violation da attributo '%1$s', convertito a NULL\n
48 %1$s %2$ld istanze\n
49 Classe %1$s\n
50 Password:
51 \n*** Aggiornamento statistiche di classe ***\n
52 %1$s nella linea %2$d (testo era '%3$s')\n
53 Errore lessicale\n
54 linea %1$d: errore di sintassi in \"%2$s\"
55 mancante
56 in %1$s
57 Argomenti incompatibili, \"%1$s\", e \"%2$s\"\n
58 %1$s %2$ld instanzioni committed\n
59 \nImpossibile richiamare la versione non ottimizzata di loaddb.\n
60 Errore durante l'analisi di \"%1$s\", come tipo %2$s.\n
61 Riferimento oggetto non è supportato in modalità CS.\n
62 Statistiche aggiornate per la classe %1$s.\n
112   %1$ld instanze\r
113 Ultima linea committed : %1$d\n
114 error-control-file
115 file di input per il controllo di errori durante il carico
116 Totale oggetti %1$ld inseriti, %2$ld oggetti non riusciti.\n
117 Caricamento fallito.\n
118 Maximum length of class name is %1$d bytes.\n
119 Totale %1$8d oggetti controllati, %2$d oggetti non riusciti.\n
120 il file di dati non contiene l'identificatore '%%class' o l'opzione '--table' mancante\n
121 La classe %1$s viene ignorata.\n
122 Maximum length of user name is %1$d bytes.\n
123 %1$s è un file vuoto.\n
124 \
loaddb: Importare oggetti e gli schemi al database.\n\
uso: %1$s loaddb [OPZIONE] nome-database[@host]\n\
\n\
opzioni valide:\n\
  -S, --SA-mode                  modalità di esecuzione autonoma\n\
  -C, --CS-mode                  modalità di esecuzione client-server\n\
                                 ATTENZIONE: In modalità HA, solo i file di dati (-d, --data-file=FILE) possono essere caricati in modalità client-server.\n\
  -u, --user=ID                  user ID per l'accesso al database; predefinito: "public"\n\
  -p, --password=PASS            password utente; predefinito: nessuno\n\
      --data-file-check-only     check syntax for data file without load; default: disabled\n\
  -l, --load-only                load data file without prior syntax checking; SA_MODE only\n\
      --estimated-size=NUMBERO   Stima del NUMERO di instanze; predefinito: 5000\n\
  -v, --verbose                  attivare i messaggi dettagliati di stato; predefinito: non attivo\n\
  -c, --periodic-commit=COUNT    inserimento COUNT per periodico commit; predefinito: 10240\n\
      --no-oid                   non usare OID\n\
      --no-statistics            non aggiornare le statistiche durante il caricamento\n\
  -s, --schema-file=FILE[:LINE]  creare schema da LINE con FILE\n\
  -i, --index-file=FILE[:LINE]   creare un indice da LINE con FILE\n\
      --trigger-file=FILE[:LINE] creare un trigger da LINE con FILE\n\
  -d, --data-file=FILE           dati di carico con FILE\n\
  -t, --table=TABLE              nome della tabella che viene sostituito con manca intestazione di classe nel file di dati\n\
      --error-control-file=FILE  FILE per il controllo di errore (s) durante il carico\n\
      --ignore-class-file=FILE   ifile di input di nomi di classe che saltino carico\n\
      --no-user-specified-name   Find classes, serials, and triggers by their object names without their owner names.\n\
                                 This is to load files generated by cubrid before version 11.2.\n\
      --schema-file-list=FILE    Il contenuto di questo file è un elenco di nomi di file di schema da utilizzare in loaddb.\n\
                                 loaddb viene eseguito in sequenza dall'alto.\n\
                                 ATTENZIONE: non può essere utilizzato con l'opzione -s.\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 cached-pages valore non è valido.\n
42 cached-page-size valore non è valido.\n
43 %1$ld oggetti dumped.\n
44 Class %1$s
45 , %1$d instanze.\n
46 Solo %1$ld di %2$ld objects dumped ok.\n
47 directory di output è troppo lungo.\n
48 LSA : %1$d %2$d \n
49 Password:
50 Maximum length of class name is %1$d bytes.\n
51 Maximum length of user name is %1$d bytes.\n
52 Class %1$s not found.\n
60 \
unloaddb: Esportare gli oggetti e gli schemi del database.\n\
uso: %1$s unloaddb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -u, --user=ID               user ID per l'accesso al database; predefinito: "dba"\n\
  -p, --password=PASS         password utente; predefinito: nessuno\n\
  -i, --input-class-file=FILE FILE di input di nomi di tabelle; predefinito: dump tutte le classi\n\
      --include-reference     includi le tabelle di riferimento, con opzione -i; predefinito: non attivo\n\
      --input-class-only      includi solo le classe specificate, con opzione-i; predefinito: non attivo\n\
      --lo-count=COUNT        file COUNT per directory; predefinito: 0\n\
      --estimated-size=NUMBER stima del NUMERO di instanze; predefinito: auto computed\n\
      --cached-pages=NUMBERO  NUMERO di pagine memorizzate nella cache; predefinito: 100\n\
  -O, --output-path=PATH      PATH della directory di output; predefinito: directory di lavoro\n\
  -s, --schema-only           dump solo schema; predefinito: schema e gli oggetti\n\
  -d, --data-only             dump solo gli oggetti; predefinito: schema e gli oggetti\n\
      --output-prefix=PREFIEX PREFIX per i file di output; predefinito: nome database\n\
      --hash-file=FILE        FILE per hash; predefinito: generato da tmpnam()\n\
      --latest-image          durante il dump degli oggetti, scarica l'ultima immagine degli oggetti nei volumi di dati, senza fare riferimento ai volumi di registro (ignorando MVCC)\n\
                              Potrebbe includere dati non impegnati e dati cancellati che non vengono aspirati; predefinito: disabilitato\n\
  -v, --verbose               attivare i messaggi dettagliati di stato; predefinito: non attivo\n\
      --use-delimiter         usa '"' dove un identificatore inizia e termina; predefinito: don't use\n\
  -S, --SA-mode               modalità di esecuzione stand-alone\n\
  -C, --CS-mode               modalità di esecuzione client-server\n\
      --datafile-per-class    creare un file oggetto per ogni classe; predefinito: non attivo\n\
      --split-schema-files    dividere le informazioni sullo schema per oggetto e generare ciascun file; Predefinito: genera un file schema con tutti gli oggetti\n\
      --skip-index-detail     Non stampare le opzioni della clausola WITH specificate durante la creazione dell'indice; predefinito: disabilitato\n\
      --as-dba                Se l'utente di accesso è un membro del gruppo DBA, estrai lo stesso file di schema del DBA.\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nPass 1\n\n
12 %1$d oggetti trasformati.\n
13 \nPass 2\n\n
14 Classe %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d instanze.\n
17 Aggiornamento oggetto.\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 Non puo trasformare l'oggetto\n
20 Manca file heap class\n
21 Impossibile aggiornare l'oggetto\n
22 Solo %1$d su oggetti %2$d trattati con successo.\n
23 COMPACTDB già iniziato.\n
24 Il numero di pagine deve essere compreso tra %1$d e %2$d.\n
25 Il lock timeout della instanza deve essere compreso tra %1$d e %2$d.\n
26 Totale oggetti: %1$d\n
27 Oggetti non riusciti: %1$d\n
28 Oggetti modificati: %1$d\n
29 Oggetti grandi: %1$d\n
30 Rappresentazioni vecchia classe eliminato dal catalogo.\n
31 Rappresentazioni vecchia classe conservati in catalogo.\n
32 Impossibile impostare isolation level.\n
33 Non si puo fare compact database.\n
34 Il classe lock timeout deve essere compreso tra %1$d e %2$d.\n
35 Locked class... Elaborazione incompleta...\n
36 Classe non trasformata...\n
37 Errore processo... Elaborazione incompleta...\n
38 Niente di elaborare...\n
39 Parametri non validi...\n
40 Nome classe sconosciuto...\n
41 Gli OID classe non utilizzati e le pagine sono stati recuperati.\n
42 Recupero di pagine e OID saltato.\n
43 Errore durante l'elaborazione di classe, classe saltato.\n
44 \nPass 3\n\n
45 Heap compact fallito...\n
46 Heap compact è riuscito...\n
47 Maximum length of class name is %1$d bytes.\n
48 Maximum length of user name is %1$d bytes.\n
60 \
compactdb: Libera lo spazio inutilizzato in un database.\n\
uso: %1$s compactdb [OPZIONE] nome-database [nome_class1 nome_class2 ...]\n\
\n\
opzioni valide:\n\
  -v, --verbose                     attivare i messaggi dettagliati di stato; predefinito: non attivo\n\
  -i, --input-class-file=FILE       input FILE di nomi di tabella\n\
                                    class_name lista deve essere vuoto\n\
                                    predefinito: compact tutte le classi\n\
  -p, --pages-commited-once=NUMBER  numero massimo di pagine per commettere ogni volta\n\
                                    predefinito: 10\n\
                                    abilitato solo nel modo CS\n\
  -S, --SA-mode                     modalità di esecuzione stand-alone\n\
  -C, --CS-mode                     modalità di esecuzione client-server\n\
  -d, --delete-old-repr             eliminare vecchie rappresentazioni di classe dal catalogo; predefinito: non attivo\n\
  -I, --Instance-lock-timeout       timeout per instance lock\n\
                                    abilitato solo nel modo CS\n\
  -c, --class-lock-timeout          timeout per class lock\n\
                                    abilitato solo nel modo CS\n\
  class_name_list                   lista dei nomi di classe da comprimere\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 Il CUBRID master in esecuzione su host %1$s \nera iniziata a %2$s
22 ha servito %1$d richieste. %2$d server in esecuzione.\n
23 SHUTTING DOWN in %1$s minuti\n
24 %1$s
25 Nome del server p[er chiudere :
26 Quanti minuti fino allo spegnimento? :
27 \
cub_commdb: Questo programma è deprecato. Utilizzare 'cubrid' comando al posto.\n\
uso: cub_commdb [OPZIONE]\n\
\n\
opzioni valide:\n\
  -P, --server-list                stampa processi server\n\
  -O, --all-list                   stampa tutti i processi\n\
  -S, --shutdown-server=NOME       shutdown NOME database server\n\
  -A, --shutdown-all               shutdown tutti i processi\n\
  -c, --server-mode=NOME           stampa NOME database server's modo HA\n\
  -N, --node-list                  list nodes gestito da CUBRID heartbeat\n\
  -L, --process-list               list local processo gestito da CUBRID heartbeat\n\
  -p, --ping-host                  list ping host utilizzati per CUBRID heartbeat\n\
  -D, --dereg-process=PID          uscire dal processo di destinazione e rimuoverlo dalle voci del processi managed\n\
  -F, --reconfig-node-list         applicare la configurazione CUBRID heartbeat recentemente cambiato\n\
  -V, --verbose                    attivare i messaggi dettagliati di stato; predefinito: non attivo\n
28 Siamo spiacente, comando sconosciuto: %1$s\n
29 Non puo ottenere il nome host per local host - annullare\n
30 Impossibile trovare il file dei parametri\n
31 Impossibile connettersi al server master %1$s\n
32 Quanti minuti fino allo shutdown? :
33 Master sarà shutdown in %1$d minuti\n
34 Errore di ricezione dei dati dal client
35 il server di database %1$s non è in esecuzione
36 non può eseguire shell (%1$s) per %2$s, %3$s
37 process fork %1$s, ritorno %2$d, %3$s
38 non può eseguire %1$s
39 Opzione -i(--immediately) deve essere utilizzata con l'opzione --deact-stop-all.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
1 Eseguire senza l'opzione --recreate-log o rimuovere manualmente il volume di log attivo.\n
60 \
emergency_patchlog: Patch di un database in caso di emergenza.\n\
usage: %1$s emergency_patchlog [OPTION] database-name [database-locale]\n\
\n\
opzioni valide:\n\
  -r, --recreate-log           recreate log files; use carefully (needs database-locale argument)\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Valore errato %1$d for npages\n
21 Valore errato '%1$s' per volume storage scopo.\n
22 ATTENZIONE: opzione --max-writesize-in-sec viene ignorato in modalità di esecuzione stand-alone.\n
60 \
addvoldb: Aggiungere un file-volume per il database.\n\
uso: %1$s addvoldb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
      --db-volume-size=SIZE        dimensione del volume addizionale; predefinito: db_volume_size in cubrid.conf\n\
      --max-writesize-in-sec=SIZE  la quantità di volume scritti al secondo; (ex. 512K, 1M, 10M); predefinito: non utilizzato; minimo: 160K\n\
  -n, --volume-name=NAME           NAME del volume di informazioni; predefinito: generate such as "db"_ext1)\n\
  -F, --file-path=PATH             PATH per l'aggiunta di file di volume; predefinito: volume_extension_path\n\
      --comment=COMMENT            COMMENT per aggiungere file di volume; predefinito: nessuno\n\
  -p, --purpose=PURPOSE            PURPOSE per l'aggiunta di file di volume; consentito:\n\
                                           DATA - solo per data\n\
                                           INDEX - solo per indices\n\
                                           TEMP - solo per temporary\n\
                                           GENERIC - per tutti gli scopi\n\
  -S, --SA-mode                    modalità di esecuzione stand-alone\n\
  -C, --CS-mode                    modalità di esecuzione client-server\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Nel database sono state rilevate alcune incongruenze.\n Si prega di consultare error_log_file %1$s per ulteriori informazioni.\n
21 %1$s: Impossibile trovare la classe. Verranno omesse.\n
22 %1$s: Cannot find the index on class %2$s.\n
60 \
checkdb: Verificare la coerenza del database.\n\
uso: %1$s checkdb [OPZIONE] nome-database [class_name1 class_name2 ...]\n\
\n\
opzioni valide:\n\
  -S, --SA-mode                modalità di esecuzione stand-alone\n\
  -C, --CS-mode                modalità di esecuzione client-server\n\
  -r, --repair                 riparare quando incoerenza si trova\n\
  -i, --input-class-file=FILE  FILE di input di nomi di classe\n\
                               predefinito: verificare tutte le classi\n\
  -I, --index-name             nome dell'indice che verrà controllato\n\
                               default: controlla tutti gli indici\n\
      --check-prev-link        controlla solo link precedenti di indici\n\
      --repair-prev-link       riparare precedente collegamenti degli indici\n\
      --check-file-tracker     controllare tutte le pagine assegnate di file\n\
      --check-heap             controllare tutte le pagine dei file heap\n\
      --check-catalog          controllare la consistenza del catalogo\n\
      --check-btree            controllare tutti gli indici btree\n\
      --check-class-name       Controllare la coerenza del nome della classe\n\
      --check-btree-entries    Controllare la consistenza delle registrazioni btree\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
10 Descrizione dello spazio per il database '%1$s' con pagesize %2$s. (log pagesize: %3$s)\n\n
11 Impossibile aprire il file di output '%1$s'\n
12 Descrizione dello spazio LOB %1$s \n\n
20 type                purpose            volume_count        used_pages          free_pages          total_pages\n
21 type                purpose            volume_count         used_size           free_size           total_size\n
22 PERMANENT           PERMANENT DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
23 PERMANENT           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
24 TEMPORARY           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
25 -                   -                         %1$5d     %2$13s       %3$13s        %4$13s\n
30 Descrizione dello spazio per tutti i volumi:\n
31 volid               type                purpose            used_pages          free_pages          total_pages         volume_name\n
32 volid               type                purpose             used_size           free_size           total_size         volume_name\n
33 %1$5d               PERMANENT           PERMANENT DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
34 %1$5d               PERMANENT           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
35 %1$5d               TEMPORARY           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
40 Descrizione spaziale dettagliata per i file:\n
41 data_type           file_count          used_pages         file_table_pages    reserved_pages      total_pages\n
42 data_type           file_count           used_size          file_table_size     reserved_size       total_size\n
43 %1$-10s               %2$5d       %3$13s            %4$13s     %5$13s    %6$13s\n
50 -----------------------------------------------------------------------------------------------------------------\n
60 \
spacedb: Visualizzazione utilizzo / spazio libero del database.\n\
uso: %1$s spacedb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -o, --output-file=FILE       reindirizzare i messaggi di output a FILE; predefinito: nessuno\n\
  -S, --SA-mode                modalità di esecuzione stand-alone\n\
  -C, --CS-mode                modalità di esecuzione client-server\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               impostare l'unità di visualizzazione (page, mega, giga, tera, human readable); predefinito: H\n\
  -s, --summarize              visualizzare solo un riepilogo per ogni scopo del volume\n\
  -p, --purpose                visualizzare la dimensione di ciascun utilizzato scopo(data/index/temp) di ciascun volume\n

$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Stima del numero di pagine = %1$d\n
60 \
estimate_data: Stimare la dimensione dei dati.\n\
uso: %1$s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Tipo di chiave di indice è sbagliato = %1$s\n
16 Male media lunghezza della chiave specificata = %1$d quando precisione = %2$d\n
17 Argomenti errati specificati\n
20 Stima del numero totale di pagine di indice = %1$d\n (Dopo indice creato, dopo i casi sono state popolate.)\n
21 Stima del numero totale di pagine di indice = %1$d\n (Dopo indice creato, prima che le istanze sono state popolate (caso average))\n
22 Stima del numero totale di pagine di indice = %1$d\n (Dopo indice creato, prima che le istanze sono state popolate (caso worst))\n
23 Per l'input dato...\n
24 Numero totale di istanze      : %1$d\n
25 Numero di chiavi diverse      : %1$d\n
26 Media dimensione della chiave : %1$d\n
27 Tipo di chiave                : %1$s\n\n
60 \
estimate_index: Stima dimensioni di un indice.\n\
uso: %1$s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Installazione di un database.\n\
uso: %1$s installdb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
      --server-name=HOST       server HOST name; predefinito: nome host corrente\n\
  -F, --file-path=PATH         PATH di directory per i volumi dei database; predefinito: directory corrente\n\
  -L, --log-path=PATH          PATH di directory per i volumi di log; predefinito: lo stesso file-path\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 Classe temporanea OID\n
2 Non è possibile stampare i valori ELO\n
3 Errore durante l'accesso long object per la migrazione\n
4 Errore di apertura del file di output per la migrazione long object \n
5 Errore di lettura\n
6 Errore di scrittura\n
7 Errore durante l'apertura del file di input ELO "%1$s"\n
8 CLASSE:
9 nome file hash
10 Nome:                                        %1$s\n
11 Dimensione:                                  %1$d\n
12 Dimensione pagina:                           %1$d\n
13 Dimensione data:                             %1$d\n
14 Dimensione entry:                            %1$d\n
15 Voci per pagina:                             %1$d\n
16 Pagine memorizzate nella cache:              %1$d\n
17 Numero di voci:                              %1$d\n
18 Numero di collisions:                        %1$d\n
19 Hash filename:                               %1$s\n
20 voce successiva overflow:                    %1$d\n
21 Tipo chiave:                                 %1$s\n
22 Indirizzo di intestazioni di pagina:         %1$x\n
23 Indirizzo di intestazione ultima pagina   :  %1$x\n
24 Indirizzo di intestazione di pagina libera:  %1$x\n
25 Indirizzo di pagina bitmap:                  %1$x\n
26 Dimensione pagina bitmap:                    %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
15 Impossibile aprire il file di output '%1$s'\n
60 \
diagdb: Discarica database.\n\
uso: %1$s diagdb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -o, --output-file=FILE       reindirizzare i messaggi di output a FILE; predefinito: nessuno\n\
      --emergency              riavvio di emergenza; predefinito: nessuno\n\
  -d, --dump-type=TIPO         TIPO di dump\n\
                               -1 dump tutto\n\
                                1 dump file tables\n\
                                2 dump file capacities\n\
                                3 dump heap capacities\n\
                                4 dump index capacities\n\
                                5 dump classnames\n\
                                6 dump disk bitmaps\n\
                                7 dump catalog\n\
                                8 dump log\n\
                                9 dump heap\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 Impossibile aprire il file di output '%1$s'\n
59 lockdb non è possibile eseguire come modalità standalone.\n
60 \
lockdb: Visualizzare lock-status del database.\n\
uso: %1$s lockdb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -o, --output-file=FILE       reindirizzare i messaggi di output a FILE; predefinito: nessuno\n\
  -c, --contention             display only lock information which has contention\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Solo un argomento deve essere dato: tran-index, user, host, progname\n
21 Inserire la password di DBA:
26 Non non c'è nessuna transazione con i valori dell'argomenti specificati.\n
27 Pronto a chiudere le seguenti transazioni\n\n
28 Vuoi procedere ? (Y/N)
29 Chiusura transazione associati con indice di transazione %1$d\n
30 Chiusura delle seguenti transazioni non è riuscito\n\n
31 La transazione non ancora finito, ma può essere fare il rollback lavoro.\n si prega, attendere un secondo e provarlo in seguito se necessario.\n
32 Non valido tranindex(%1$s).\n
59 killtran non è possibile eseguire come modalità standalone.\n
60 \
killtran: Gestire le transazioni del database.\n\
uso: %1$s killtran [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -i, --kill-transaction-index=INDEX chiudere la transazione con transazione INDEX\n\
      --kill-user-name=ID            chiudere tutte le transazioni con l'ID utente\n\
      --kill-host-name=HOST          chiudere tutte le transazioni con client HOST\n\
      --kill-program-name=NOME       chiudere tutte le transazioni con programma client NOME\n\
      --kill-sql-id=SQL_ID           chiudere tutte le transazioni con SQL_ID\n\
  -p, --dba-password=PASS            la password dell'utente DBA; richiesta se non specificherà\n\
                                     (funziona solo con le opzioni -i o --kill. Altrimenti, ottiene l'errore)\n\
  -d, --display-information          visualizzare le informazioni sulle transazioni attive\n\
  -q, --query-exec-info              visualizzare le informazioni sulle esecuzione query di transazioni attive\n\
  -f, --force                        chiudere la transazione senza una richiesta per la verifica\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 nome-database
2 argomento richiesto, nessun predefinito
3 compatibility-check
4 compatibility check solo
5 backup-destination
6 un directory o un dispositivo per la destinazione dei volumi di backup
20 database '%1$s' non esiste.
21 '%1$s' non può essere migrato a %2$s, perché la sua versione DB è troppo bassa.\n
22 '%1$s' non ha bisogno di essere migrato.\n
23 '%1$s' ha bisogno di essere migrato.\n
24 Backup è raccomandato dopo la migrazione. Vuoi eseguire il backup del database ora? (y/n):
25 Si consiglia di eseguire il backup del nuovo database con il comando backupdb dopo la migrazione.
26 \n*** ATTENZIONE: Eseguire il backup del database prima di iniziare la migrazione a %1$s ***\n\nPer recuperare gli errori verificatisi durante la migrazione, si consiglia di eseguire il backup del database.\nHa già eseguito il backup del database? (y/n):
27 \n1. Esegui backupdb \n2. Digitare il nuovo percorso di backup per il nuovo database.(path corente: %1$s)\n3. Exit \n
28 \n Il nuovo backup path del database:
29 \nLa migrazione e il backup sono stati completati con successo.
30 Backup non riuscito. Per favore, riprovare a eseguire il backup del database.
31 \n\n*** ATTENZIONE : Una volta completata la migrazione, è non è possibile ripristinare il database con i volumi di backup precedenti.\nSi noti che la versione precedente dei file di backup può essere ripristinata solo sulla versione corrispondente di restoredb.\nVerificare i volumi di backup precedenti è tenuto saldamente.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Alterare un host del database.\n\
uso: %1$s alterdbhost [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -h, --host=HOST              nuovo nome HOST; predefinito: nome host corrente\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 nome-database
2 argomento richiesto, niente predefinito
3 java-class-file
4 argomento richiesto, niente predefinito
60 \
Usage: loadjava [OPTION] database-name java-class-file\n\
\n\
valid options:\n\
  -y, --overwrite              sovrascrivere class file, se esiste, niente predefinito\n\
  -j, --jni                    add a Class or Jar to static loading for JNI\n

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Impossibile aprire il file di output '%1$s'\n
59 plandump non è possibile eseguire come modalità standalone.\n
60 \
plandump: Visualizzare o controllare i piani di query memorizzati nella cache.\n\
uso: %1$s plandump [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -d, --drop                   eliminare tutti i piani nella cache del server\n\
  -o, --output-file=FILE       reindirizzare i messaggi di output a FILE; predefinito: nessuno\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Impossibile aprire il file di output '%1$s'\n
21 Parametri di sistema per essere utilizzato dal programmi client.\n
22 Parametri di sistema caricati dal server '%1$s'\n
23 Parametri di sistema per essere usato in modalità standalone.\n
60 \
paramdump: Dump impostazioni dei parametri di sistema.\n\
uso: %1$s paramdump [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -o, --output-file=FILE       reindirizzare il messaggio di output a FILE; predefinito: nessuno\n\
  -b, --both                   scarica parametri sia client che server\n\
  -S, --SA-mode                modalità di esecuzione stand-alone\n\
  -C, --CS-mode                modalità di esecuzione client-server\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Errato valore \"%1$s\" per modalità di esecuzione HA.\n
12 Non è possibile modificare la modalità di esecuzione HA.\n
21 Inserire la password di DBA:
22 Per il server '%1$s' modalità di esecuzione HA corrente è %2$s.\n
23 Per il server '%1$s' HA modalità di esecuzione è cambiato in \"%2$s\".\n
24 Il server non è stato configurato per HA.\n
58 Funzionalità HA  non è supportata in questa piattaforma.\n
59 changemode non è possibile eseguire come modalità standalone.\n
60 \
changemode: Visualizzare o modificare modalità di esecuzione HA del server.\n\
uso: %1$s changemode [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -m, --mode=MODO             modificare la modalità server a MODO; predefinito: nessuno\n\
  -f, --force                 forza di cambiamento\n\
  -t, --timeout=S             aspettare S secondi mentre transazione attiva è conclusa normalmente\n\
                              opzione -t è efficace solo quando l'utente cambia modalità server a manutenzione\n\
                              predefinito: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Valore errato \"%1$s\" per la modalità di esecuzione di copylogdb.\n
21 Inserire la password di DBA:
22 Il server non è stato configurato per HA.\n
58 Funzionalità HA non è supportata in questa piattaforma.\n
59 copylogdb non è possibile eseguire come modalità standalone.\n
60 \
copylogdb: Copiare pagine di log dal server.\n\
uso: %1$s copylogdb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -L, --log-path=PATH         directory PATH per volumi di log copiati\n\
  -m, --mode=MODE             modalità di funzionamento; predefinito: sync\n\
                              MODE: sync o semisync o async\n\
  -S, --start-page-id=ID      first log page to copy\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 Inserire la password di DBA:
22 Il server non è stato configurato per HA.\n
58 Funzionalità HA  non è supportata in questa piattaforma.\n
59 applylogdb non è possibile eseguire come modalità standalone.\n
60 \
applylogdb: Applicare log copiato sul server.\n\
uso: %1$s applylogdb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -L, --log-path=PATH         directory PATH di volumi di log copiati\n\
      --max-mem-size=SIZE     processo di massima dimensione SIZE in MB; max 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Impossibile aprire il file di output '%1$s'\n
59 statdump non è possibile eseguire come modalità standalone.\n
60 \
statdump: Dump statistiche globali.\n\
uso: %1$s statdump [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -o, --output-file=FILE       reindirizzare il messaggio di output al FILE; predefinito: nessuno\n\
  -c, --cumulative             stampa cumulativo stats\n\
  -i, --interval=S             stampa stats ogni S secs\n\
  -s, --substr=STRING          stampa stat item solo che corrisponde la sottostringa STRING\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 Inserire la password DBA:
22 Il server non è stato configurato per HA.\n
57 Impossibile generare le informazioni applicate a causa di un percorso non valido dell'opzione -L o di nessuna informazione correlata nella tabella del catalogo db_ha_apply_info.\n
58 Funzionalità HA non è supportata in questa piattaforma.\n
59 applyinfo non è possibile eseguire come modalità standalone.\n
60 \
applyinfo: visualizzare le informazioni di CUBRID HA.\n\
uso: %1$s applyinfo [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -r, --remote-host-name      nome dell'host remoto; visualizzare le informazioni di log attivo del nodo remote \n\
  -L, --copied-log-path=PATH  path di volumi di registrazione copiate; informazioni del log copiate \n\
  -a, --applied-info          visualizzare applied infomation; l'opzione -L è richiesta \n\
  -p, --page=ID               page id; l'opzione -L è richiesta; predefinito : 0\n\
  -v, --verbose               attivare i messaggi dettagliati di stato; predefinito : non attivo\n\
  -i, --interval=S            print information every S secs\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb non è possibile eseguire come modalità standalone.\n
60 \
acldb: Visualizzare o ricaricare il controllo di accesso della base dati.\n\
uso: %1$s acldb [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -r, --reload       ricarica il file di controllo IP\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Genera locale shared library usando file di input LDML.\n\
uso: %1$s genlocale [OPZIONE] [language-string]\n\
\n\
language-string       locale language string per quale si genera shared library.\n\
                      Se language-string no è specificato, tutte le lingue da 'cubrid_locales.txt'\n\
                      saranno trattati.\n\
opzioni valide:\n\
  -v, --verbose               attivare i messaggi dettagliati di stato; predefinito : non attivo\n\
  -i, --input-ldml-file       percorso di file di input LDML. Solo quando la language-string è dato.\n
61 Argumento nome locale non valido.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 Non è possibile utilizzare contemporaneamente -i/--input-file e locale identifier.\n
58 Non valido codepoint range (-s -e).\n
59 Non valido argumento nome locale.\n
60 \
dumplocale: Scarica il contenuto di locale shared library precedentemente generato utilizzando il file di input LDML.\n\
uso: %1$s dumplocale [OPZIONE] [language-string]\n\
\n\
language-string       locale language string per quale si genera shared library.\n\
                      Se language-string no è specificato, tutte le lingue da 'cubrid_locales.txt'\n\
                      saranno trattati.\n\
opzioni valide:\n\
  -i, --input-file    path per input locale shared library (precedentemente generato).\n\
                      Può essere utilizzato solo quando il language-string non è dato.\n\
  -d, --calendar      scarica informazioni calendario e data / ora; predefinito : non attivo\n\
  -n, --numeric       dump informazioni numeriche; predefinito : non attivo\n\
  -a, --alphabet=l|lower|u|upper|both        dump di alfabeto e informazioni caso; predefinito : non attivo\n\
      --identifier-alphabet=l|lower|u|upper  dump di alfabeto e informazioni caso per identificatori; predefinito : non attivo\n\
  -c, --codepoint-order  discarica informazioni collation ordinati in base ai valori di codepoint\n\
                         (cp, char, weight, next-cp, char e weight);\n\
                         predefinito : non attivo\n\
  -w, --weight-order  discarica info collation ordinati in base ai valori di peso\n\
                      (weight, cp, char); predefinito : non attivo\n\
  -s, --start-value   opzione di filtraggio; inizio codepoint per\n\
                      -a, --identifier-alphabet, -c, -w; predefinito : 0\n\
  -e, --end-value     opzione di filtraggio; fine codepoint per \n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      predefinito : massimo valore letto dal locale shared library\n\
  -k, --console-conversion  scaricare i dati console di conversione; predefinito : non attivo\n\
  -z, --normalization   scaricare i dati di normalizzazione; predefinito : non attivo\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 Partizionamento deve essere rimosso prima di cambiare la collations corrente '%1$s' del loro attributo partizionamento.\nNome classe\n
44 Foreign keys su attributi con collation '%1$s' deve essere eliminato prima di cambiare la collation.\nTable | Foreign Key\n
45 Classi con collation '%1$s'; devono essere ridefinite :\nNome classe\n
46 Function indexes avendo espressioni con collation '%1$s'; devono essere ridefinite :\n\
Table | Index | Function expression\n
47 Sincronizzazione delle collations non è stata eseguita. Database non è stato modificato.\n
48 Contenuti di '%1$s' tabella di sistema è stato aggiornato con collations di nuovo sistema.\n
49 L'aggiornamento delle collations può causare la corruzione del database. Continuare (y/n)?\n
50 Collation '%1$s' (Id: %2$d) non trovato nel database o modificata in nuova configurazione del sistema.\n
51 Triggers avendo condizione con collation '%1$s'; devono essere ridefinite :\nTrigger | Condition\n
52 Query views avendo condizione con collation '%1$s'; devono essere ridefinite :\nView | Query\n
53 Attributes con collation '%1$s'; devono essere ridefinite.\nTable | Attribute Domain\n
54 Intervento manuale sul database è necessario.\n\
Controlla il file '%1$s' e eseguirlo prima di forzare le collations nel database.\n
55 La sincronizzazione non è necessaria.\n
56 Sincronizzazione di collations di sistema nel database è necessario.\n\
Eseguire 'cubrid synccolldb -f %1$s'\n
57 Ci sono %1$d nuovi collations in configurazione di sistema rispetto al database:\n
58 Tutte le collations di database '%1$s' si trovano nella nuova configurazione del sistema.\n
59 Ci sono %1$d collations nel database che non sono configurati o sono state modificate rispetto a collation del sistema.\n
60 \
synccolldb: Sincronizza le collations del database con collations di sistema.\n\
usage: %1$s synccolldb [OPTION] nome-database\n\
\n\
opzioni valide:\n\
  -c, --check-only    Controlla le collations del database con collations di sistema.\n\
                      Se la sincronizzazione di collations è necessario, il codice di errore 1 viene restituito.\n\
                      Visualizza un rapporto di collations incompatibili.\n\
  -f, --force-only    Aggiornamenti delle collations di database per essere lo stesso le collations di nuova configurazione del sistema.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Inserire la password %1$s:
22 Tran index            User name      Host name      Process id      Program name\n
23 ---------------------------------------------------------------------------------\n
24 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
25 Non ci sono transazioni\n
26 tranlist può essere eseguita solo dal DBA o un membro del gruppo DBA.\n
27 sort-key numero di colonna(%1$d) non è valido.\n
32 Tran index           Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %1$4d%2$-12s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 ----------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index            User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %1$4d%2$-12s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %1$d
46 SQL_ID: %1$s\n
59 tranlist non può essere eseguito nella modalità standalone.\n
60 \
tranlist: Dump le informazioni delle transazioni attive.\n\
usage: %1$s tranlist [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
  -s, --summary                stampa informazioni riepilogative della transazione\n\
      --sort-key=COLUMN_NUMBER numero di colonna da ordinare\n\
      --reverse                invertire l'ordinamento\n

$set 51 MSGCAT_UTIL_SET_GEN_TZ
59 Modalità generazione non valido.\n
60 Cartella di input non valido o danneggiato.\n
61 \
gen_tz: Generare file sorgente contenenti dati fuso orario da elaborare in\n\
        una libreria condivisa.\n\
uso: %s gen_tz -g [new|extend] [database-name] (only for extend mode)\n\
\n\
opzioni valide:\n\
  -i, --input-folder    path di una cartella di input che contiene una versione di\n\
                        IANA's database timezone file(s).\n\
                        Predefinito: $CUBRID/timezones/tzdata\n\
  -g, --gen-mode=TYPE   controlla l'uscita del gen_tz, basato sul TYPE:\n\
                        TYPE:\n\
                        1. NEW (predefinito) : generare file(s) sorgente fuso orario per\n\
                        la libreria condivisa senza alcun riguardo per le versioni precedenti;\n\
                        generare anche un file sorgente contenente tutti i paesi\n\
                        e nomi di fuso orario (destinato all'uso sviluppatore, a\n\
                        essere integrato nelle future versioni CUBRID).\n\
                        2. EXTEND : Generare file sorgente di timezone utilizzando dati\n\
                        dalla cartella di input; nuovi valori di timezone  trovati nelli file\n\
                        del'ingresso vengono aggiunti; gli offset GMT e le informazioni del'ora\n\
                        legale vengono aggiornate (o aggiunte, per i nuovi campi temporali);\n\
                        gli timezones non vengono mai rimossi se un fuso orario codificato in CUBRID\n\
                        manca da un file di input, quelli dati vengono recuperati dalla libreria\n\
                        di timezone esistente. Se la prima fase dell'estensione non riesce a rendere\n\
                        compatibili tutti i dati indietro, verranno aggiornate tutte le righe in\n\
                        tutte le tabelle con tipi di dati timezone.\n

$set 52 MSGCAT_UTIL_SET_DUMP_TZ
59 L'ID di area fornito è fuori portata. Utilizzare - z per vedi tutto\n\
 timezones e loro IDs.\n
60 \
dump_tz: Visualizza informazioni relative al fuso orario.\n\
uso: %s dump_tz [OPTION]\n\
\n\
opzioni valide:\n\
  -c, --list-countries      mostra la lista dei paesi attualmente supportati\n\
  -l, --leap-seconds        mostra la lista dei secondi bisestili\n\
  -s, --summary             mostra una sintesi dei dati di fuso orario discarica\n\
  -z, --list-zones          mostra l'elenco dei fusi orari attualmente supportati\n\
  -d, --zone-id=ZONE_ID|ALL visualizzazione delle informazioni sul fuso orario disponibili correlate\n\
                            a un fuso orario con la ZONE_ID o per\n\
                            ALL - tutti i fusi orari. L'ID di un fuso orario può essere ottenuto\n\
                            dall'elenco visualizzato utilizzando l'opzione -z.\n

$set 53 MSGCAT_UTIL_SET_RESTORESLAVE
20 Restoreslave annullata o si è verificato un errore.\n
21 Database di ripristino è stato completato. Ma non è riuscito a inizializzare applyinfo. (db_creation_time='%1$s', pageid=%2$lld, offset=%3$d)\n
22 Il nome del database o il nome host non è stato configurato per HA.\n
23 Valore errato '%1$s' per stato di backup source node.\n
60 \
restoreslave: Ripristinare un database slave\n\
uso: %1$s restoreslave [OPTION] database-name\n\
\n\
opzioni valide:\n\
  -s, --source-state=STATE          stato di nodo di origine del backup; obbligatorio\n\
                                    STATE può essere 'master', 'slave', o 'replica'\n\
  -m, --master-host-name=NAME       nome host del nodo master attuale; obbligatorio\n\
      --list                        visualizzare un elenco di volumi di backup senza ripristinare\n\
  -B, --backup-file-path=PATH       PATH è un elenco di volumi di backup da ripristinare\n\
  -o, --output-file=FILE            messaggi di output redirect a FILE; predefinito: nessuno\n\
  -u, --use-database-location-path  ripristinare i volumi di database e di registro per il percorso specificato nel file di posizione del database\n\
  -k, --keys-file-path              Percorso del file chiave (_keys) per TDE da utilizzare durante il ripristino\n

$set 54 MSGCAT_UTIL_SET_DELVOLDB
21 Pronti a rimuovere i seguenti volumi:\n\n
22 Vuoi proseguire ? (Y/N)
31 Impossibile rimuovere volume di sistema o il primo volume (Volid : %1$d).\n
32 Impossibile trovare il volume (Volid : %1$d).\n
33 Ci sono troppi Volids (Max Volids : %1$d).\n
34 Non valido Volid(%1$s).\n
60 \
delvoldb: Rimuovere un volume-file vuoto del database.\n\
uso: %1$s delvoldb [OPTION] database-name -i Volid\n\
\n\
opzioni valide:\n\
  -c, --clear-cache                cancellare tutti i file memorizzati nella cache del volume temp\n\
  -f, --force                      togliere il volume senza una richiesta per la verifica\n\
  -p, --dba-password=PASS          password dell'utente DBA; vi verrà chiesto se non si specifica\n\
  -S, --SA-mode                    esecuzione in modalità stand-alone\n\
  -C, --CS-mode                    esecuzione in modalità client-server\n

$set 55 MSGCAT_UTIL_SET_VACUUMDB
20 Vacuumdb non è temporaneamente disponibile per la modalità client-server\n
21 L'operazione vacuum non è riuscita\n
22 Impossibile aprire il file di output '%1$s'\n
60 \
vacuumdb: Pulisce record morti di database.\n\
uso: %1$s vacuumdb [OPTION] database-name\n\
\n\
opzioni valide:\n\
  -S, --SA-mode                esecuzione in modalità stand-alone\n\
  -C, --CS-mode                esecuzione in modalità client-server\n\
      --dump                   scaricare lo stato attuale del vuoto\n\
  -o, --output-file=FILE       reindirizzare i messaggi di output a FILE; predefinito: nessuno\n

$set 56 MSGCAT_UTIL_SET_CHECKSUMDB
1 Impossibile caricare nomi di tabella in %1$s.\n
2 checksumdb deve essere eseguito su un server attivo HA. %1$s (stato attuale: %2$s)\n
3 Il proprietario della tabella per salvare il risultato del checksum deve essere DBA.\n
58 HA non è supportato in questa piattaforma.\n
59 checksumdb non può funzionare come modalità standalone.\n
60 \
checksumdb: verificare l'integrità della replica.\n\
uso: %1$s checksumdb [OPTION] database-name@hostname\n\
\n\
opzioni valide:\n\
  -c, --chunk-size=NUMBER       il numero di righe per selezionare per ogni calcolo del checksum\n\
                                predefinito: 500 rows, minimo: 100 rows\n\
  -s, --sleep=NUMBER            sleep time tra il calcolo del checksum; impostazione predefinita: 100 ms\n\
  -i, --include-class-file=FILE input FILE di nomi di tabella da includere\n\
  -e, --exclude-class-file=FILE input FILE di nomi di tabella da escludere; o -i o -e può essere utilizzato, non entrambi\n\
  -t, --timeout=NUMBER          query di timeout in millisecondi; predefinito: 1000 ms\n\
  -n, --table-name=NAME         nome della tabella per salvare i risultati di controllo; predefinito: dba.db_ha_checksum\n\
  -r, --report-only             apporto di integrità di replica basata su calcolo del checksum\n\
                                tutte le opzioni tranne --table-name saranno ignorati\n\
      --resume                  riprendere il calcolo del checksum\n\
      --schema-only             solo controllare la coerenza dello schema\n\
      --cont-on-error           continuare in caso di errore di calcolo\n

$set 57 MSGCAT_UTIL_SET_TDE
21 Immettere la password DBA:
25 Impossibile ottenere le informazioni sulla chiave impostate nel database\n
26 Tentativo di cambiare la chiave dalla chiave (indice: %1$d) alla chiave (indice: %2$d) ..\n
27 La chiave è stata cambiata dalla chiave (indice: %1$d) alla chiave (indice: %2$d)\n
28 La chiave impostata sul database non può essere eliminata\n
29 La chiave (indice: %1$d) è stata eliminata\n
30 È stata generata una nuova chiave - indice chiave: %1$d creato su%2$s
60 \
tde: gestione TDE (Transparent Data Encryption)\n\
utilizzo: %1$s tde OPERAZIONE (-s|-n|-d|-c) [OPZIONE] nome-database\n\
\n\
opzioni valide:\n\
    -p, --dba-password=PASS    password dell'utente DBA; chiederà se non si specifica\n\
\n\
OPERAZIONI: una delle operazioni deve essere assegnata e sono esclusive\n\
    -s, --show-keys            stampa le informazioni sulla chiave impostate nel database e il file della chiave (_keys)\n\
    -n, --generate-new-key     genera una nuova chiave nel file chiave (_keys); conteggio massimo: 128\n\
    -d, --delete-key=KEY_INDEX elimina una chiave nel file della chiave (_keys)\n\
    -c, --change-key=KEY_INDEX cambia il set di chiavi sul database per un'altra chiave nel file di chiavi (_keys)\n

$set 58 MSGCAT_UTIL_SET_FLASHBACK
1 Immettere la password DBA:
2 Formato data non valido, il formato valido è gg-mm-aaaa:hh:mm:ss\n
3 La data di inizio (%1$s) deve essere inferiore alla data di fine (%2$s)\n
4 Prova con una data precedente a quella corrente (%1$s) o dopo la creazione del database (%2$s)\n
5 La tabella (%1$s) non esiste nel database\n
6 Troppe transazioni per il flashback. Riduci l'intervallo tra la data di inizio e la data di fine\n
7 Impossibile ottenere il flashback perché lo schema (%1$s) è stato modificato.\n
8 Il volume del registro è stato rimosso. Prova con la data successiva\n
9 Impossibile aprire il file di output '%1$s'\n
10 L'ID transazione (%1$d) non esiste\n
11 Impostare 'supplemental_log' in 'cubid.conf'. Solo i log SQL eseguiti dopo aver impostato 'supplemental_log' possono essere sottoposti a flashback.\n
12 L'ID transazione deve essere inserito tra %1$d secondi. Il timeout può essere regolato tramite l'impostazione 'flashback_timeout' in 'cubrid.conf'.\n
13 Flashback è già in esecuzione. Flashback non può essere eseguito da due processi.\n
14 Flashback non può essere eseguito in modalità standalone.\n
15 La classe del catalogo di sistema (%1$s) non è supportata nel flashback.\n
60 \
flashback: fornisce istruzioni per eseguire il flashback di una transazione specifica.\n\
utilizzo: %1$s flashback [OPZIONE] nome-database nome_proprietario.nome_classe1 [nome_proprietario.nome_classe2 ...]\n\
\n\
opzioni valide:\n\
    -o, --output-file=FILE       reindirizza il messaggio di output a FILE; predefinito: nessuno.\n\
    -u, --user=ID                specifica l'utente che ha eseguito l'istruzione SQL per il flashback; predefinito: TUTTI.\n\
    -p, --dba-password           password dell'utente DBA; verrà richiesto se non specifichi\n\
    -s, --start-date=DATE        specifica la DATA di inizio per il flashback. I log SQL vengono estratti da DATE.\n\
                                 La DATA è data come segue: gg-mm-aaaa:hh:mm:ss.\n\
    -e, --end-date=DATE          specifica la DATA di fine per il flashback. I log SQL vengono estratti fino alla DATA.\n\
                                 se la data di inizio non è specificata, allora ((END - 10min) ~ END) è impostato su intervallo,\n\
                                 e altrimenti se END non è specificato, allora (start ~ (inizio + 10min)) è impostato.\n\
                                 E non viene specificata né una data di inizio né una data di fine,\n\
                                 vengono estratti i log SQL, eseguiti tra (ora corrente - 10min) e ora corrente.\n\
    --detail                     restituisce informazioni dettagliate per ciascuna istruzione SQL.\n\
                                 L'ID transazione, il nome utente e l'istruzione SQL originale verranno restituiti con l'istruzione SQL\n\
                                 per flashback; default: restituisce solo istruzioni SQL per il flashback\n\
    --oldest                     flashback viene eseguito in ordine cronologico per le istruzioni SQL eseguite dall'ora di inizio all'ora\n\
                                 di fine; predefinito: più recente\n

$set 59 MSGCAT_UTIL_SET_MEMMON
1 Si prega di impostare il parametro di sistema 'enable_memory_monitoring' in 'cubrid.conf' e riavviare il server del database.\n\
2 memmon non può essere eseguito in modalità standalone.\n\
3 Impossibile aprire il file di output con il nome %1$s.\n\
60 \
memmon: Fornisce informazioni sull'utilizzo della memoria di un server di database.\n\
Utilizzo: %1$s memmon [OPZIONE] nome-database\n\
\n\
Opzioni valide:\n\
    -o, --output-file=FILE    invia i risultati a FILE\n
