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
$       50      prefetchlogdb

$set 1 MSGCAT_UTIL_SET_GENERIC
1 Lungime sau caracter invalid '%1$c' în numele bazei de date '%2$s'\n
2 Fişierul de ieşire '%1$s' nu a putut fi deschis.\n
3 Nume de utilitar invalid sau absent.\n
4 Nume de utilitar invalid '%1$s'\n
5 Lista următoare conţine numele de utilitare recunoscute:\n
6 Caracter invalid '%1$c' în numele volumului '%2$s'\n
9 \
%1$s (Utilitare CUBRID)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation. Toate drepturile rezervate de către Search Solution.\n
10 \
utilitar de administrare CUBRID, versiunea %1$s\n\
utilizare: %2$s <nume-utilitar> [argumente]\n\
Tastaţi '%3$s <nume-utilitar>' pentru ajutor pentru un utilitar specific.\n\
\n\
Nume de utilitare de administrare disponibile:\n\
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
    prefetchlogdb\n\
\n\
%4$s este un utilitar pentru SGDB.\n\
Pentru informaţii suplimentare, accesaţi http://www.cubrid.org\n
12 Numele de utilitar '%1$s' este invalid.\nTastaţi 'cubrid --help' pentru informaţii despre utilizare.\n
13 comanda '%1$s' este invalidă.\nTastaţi 'cubrid --help' pentru informaţii despre utilizare.\n
14 Una sau mai multe proprietăţi obligatorii din fişierele de configurare nu au putut fi încarcate.\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' nu se află în execuţie.\n
18 ++ %1$s nu se află în execuţie.\n
19 ++ %1$s '%2$s' se află în execuţie.\n
20 ++ %1$s se află în execuţie.\n
21 ++ %1$s %2$s: %3$s\n
22 Unul sau mai multe argumente lipsesc.\nTastaţi 'cubrid --help' pentru informaţii despre utilizare.\n
23 utilitar CUBRID, versiunea %1$s\n\
utilizare: %2$s <nume-utilitar> [argumente]\n\
Tastaţi '%3$s <nume-utilitar>' pentru ajutor pentru un utilitar specific.\n\
\n\
Utilitare de serviciu disponibile:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
\n\
Utilitare de administrare disponibile:\n\
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
    applyinfo\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
\n\
%4$s este un utilitar pentru SGBD.\n\
Pentru informaţii suplimentare, accesaţi http://www.cubrid.org\n
24 utilitar CUBRID, versiunea %1$s\n\
utilizare: %2$s service <comanda>\n\
\n\
Comenzi disponibile:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 utilitar CUBRID, versiunea %1$s\n\
utilizare: %2$s server <comanda> [argumente]\n\
\n\
Comenzi disponibile:\n\
    start       [nume-baza-de-date]\n\
    stop        [nume-baza-de-date]\n\
    restart     [nume-baza-de-date]\n\
    status\n\
    acl         <status|reload> nume-baza-de-date\n
37 Serverul a fost configurat pentru HA.\n
26 utilitar CUBRID, versiunea %1$s\n\
utilizare: %2$s broker <comanda> [argumente]\n\
\n\
Comenzi disponibile:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [<expr>]\n\
    on          nume-broker\n\
    off         nume-broker\n\
    reset       nume-broker\n\
    acl         <status|reload> [nume-broker]\n\
    getid       -b <nume-broker> [-f] cheie-shard\n\
    test        nume-broker [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 utilitar CUBRID, versiunea %1$s\n\
utilizare: %2$s manager <command>\n\
\n\
Comenzi disponibile:\n\
    start\n\
    stop\n\
    status\n
28 utilitar CUBRID, versiunea %1$s\n\
utilizare: %2$s heartbeat <command> [args]\n\
\n\
Comenzi disponibile:\n\
    start          [database-name]\n\
    stop           [-h <hostname>] [-i] [database-name]\n\
    copylogdb      <start|stop> database-name node-name\n\
    applylogdb     <start|stop> database-name node-name\n\
    prefetchlogdb  <start|stop> database-name node-name\n\
    replication    <start|stop> node-name\n\
    status         [-v]\n\
    reload\n
31 Argumentul '%1$s' nu este necesar.\n
32 Un nume de bază de date lipseşte.\n
33 %1$s: Această optiune este depreciată.\n
34 Parametru invalid. %1$s %2$s\n
35 Nu s-a putut aloca memorie.\n
36 Serverul nu a fost configurat pentru HA.\n
38 Serverul configurat pentru HA nu se regăseşte în ha_db_list.\n
39 Serverul configurat pentru HA nu se regăseşte în ha_node_list.\n
40 Comandă invalidă.\n
41 cubrid manager server nu este instalat.\n
42 Argument invalid.\n
43 Eroare la deschiderea fişierului(%1$s).\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 AVERTIESMENT: opţiunea -t este ignorată în modul de execuţie independent.\n
31 EROARE: Calea destinaţie nu există sau nu este un director.\n
60 \
backupdb: Creează o copie de rezervă a bazei de date.\n\
utilizare: %1$s backupdb [optiune] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -D, --destination-path=CALE  stochează volumele de backup in directorul indicat prin CALE;\n\
                               implicit: directorul de log al bazei de date\n\
  -r, --remove-archive         şterge arhive de log inutile; a se utiliza cu atenţie şi cu consultarea manualului\n\
  -l, --level=NIVEL            nivel de backup; implicit: nivelul complet\n\
                               valori permise pentru NIVEL:\n\
                                   0 - complet\n\
                                   1 - incremental 1\n\
                                   2 - incremental 2\n\
  -o, --output-file=FISIER     scrie mesaje de backup detaliate în FISIER\n\
  -S, --SA-mode                mod de execuţie independent\n\
  -C, --CS-mode                mod de execuţie client-server\n\
      --no-check               nu se efectuează o verificare de consistenţă a bazei de date; implicit: verificare\n\
  -t, --thread-count=NUMAR     număr de fire de excuţie; implicit: 0(auto)\n\
  -z, --compress               compresia volumelor de backup; implicit: decompresie\n\
  -e, --except-active-log      nu se include un volum de log activ; implicit: se include\n\
      --sleep-msecs=N          asteapta N milisecunde la fiecare 1MB citit; implicit: 0\n
71 Argumente incompatibile, \"%1$s\", şi \"%2$s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 EROARE: Numele destinaţiei este identic cu numele sursei.\n
31 EROARE: Calea către volumul de extensie (volext-pathname) nu există sau nu poate fi scrisă.\n
32 EROARE: vols-tofrom-paths_file nu există sau nu poate fi citit.\n
60 \
copydb: Copiază o bază de date.\n\
utilizare: %1$s copydb [OPTION] nume-bază-de-date-sursă nume-bază-de-date-destinaţie\n\
\n\
opţiuni valide:\n\
      --server-name=HOST          numele HOST al serverului; implicit: numele de gazdă actual\n\
  -F, --file-path=CALE            CALE către directorul cu volumele bazei de date; implicit: directorul curent\n\
  -L, --log-path=CALE             CALE către directorul cu volumele de log; implicit: la fel ca file-path\n\
  -B, --lob-base-path=CALE        CALE către directorul de bază al datelor LOB externe; implicit: <file-path>/lob\n\
  -E, --extended-volume-path=CALE CALE către directorul volumelor extinse; implicit: aceeasi cale ca pentru\n\
                                  volumele bazei de date, sau calea dată în control-file\n\
  -i, --control-file=FISIER       copierea volumelor în mai multe locaţii specificate în fişierul\n\
                                  de control (control-file); implicit: absent\n\
  -r, --replace                   înlocuieşte o bază de date; implicit: nu se înlocuieşte\n\
  -d, --delete-source             şterge o bază de date sursă; implicit: nu se şterge\n\
      --copy-lob-path             copiază, din baza de date sursă, calea către suportul de stocare extern pentru\n\
                                  datele LOB. Nu poate fi utilizat cu opţiunea -B; implicit: nu se copiază\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 Grupul '%1$s' nu există\n
41 Utilizatorul '%1$s' nu există\n
42 Comandă necunoscută '%1$s'\n
43 Nu s-a putut deschide fişierul de ieşire '%1$s'\n
44 Baza de date existentă nu a putut fi eliminată.\n
45 Se creează o bază de date de dimensiune %1$s si localizare %2$s. Spaţiul total necesar este %3$s.\n
46 Baza de date nu a putut fi creată.\n
47 S-a întâmpinat o problemă cu fişierul '%1$s' dat de utilizator\n
48 S-a furnizat '%1$s', %2$s este în afara domeniului. Alegeţi o dimensiune cuprinsă între %3$s şi %4$s.\n
49 Valoarea lui %1$s '%2$s' nu este validă.\n
60 \
createdb: Creează o baza de date.\n\
utilizare: %1$s createdb [OPTIUNE] <nume-bază-de-date> <localizare>\n\
\n\
localizarea este in formatul <limbă>.<charset>; exemple : en_US.iso88591, ko_KR.utf8\n\
opţiuni valide:\n\
      --db-volume-size=MĂRIME            mărimea primului volum de informaţii\n\
                                         implicit: valoarea lui db_volume_size din cubrid.conf\n\
      --db-page-size=MĂRIME              MĂRIMEA in bytes a unei pagini din baza de date; implicit: 16K\n\
      --log-volume-size=MĂRIME           MĂRIMEA arhivelor de log\n\
                                         implicit: valoarea lui log_volume_size din cubrid.conf\n\
      --log-page-size=MĂRIME             MĂRIMEA în bytes a unei pagini de log;\n\
                                         implicit: mărimea unei pagini a bazei de date\n\
      --comment=COMENTARIU               COMENTARIU pentru baza de date; implicit: nul\n\
  -F, --file-path=CALE                   CALEA către directorul cu volumele bazei de date; implicit: directorul curent\n\
  -L, --log-path=CALE                    CALEA către directorul cu volumele de log; implicit: la fel ca file-path\n\
  -B, --lob-base-path=CALE               CALEA către directorul de bază pentru datele LOB externe;\n\
                                         implicit: <file-path>/lob\n\
      --server-name=HOST                 numele de HOST al serverului; implicit: numele curent de host\n\
  -r, --replace                          înlocuieşte o bază de date existentă; implicit: nu se înlocuieşte\n\
      --more-volume-file=FISIER          un FIŞIER ce conţine specificaţii pentru crearea de volume\n\
                                         suplimentare; implicit: nul\n\
      --user-definition-file=FISIER      un FIŞIER ce conţine definiţii date de utilizator; implicit: nul\n\
      --csql-initialization-file=FISIER  un FIŞIER ce conţine interogări SQL pentru o iniţializare a bazei de date;\n\
                                         implicit: nul\n\
  -o, --output-file=FISIER               redirecţionează mesajele de ieşire către un FISIER; implicit: nul\n\
  -v, --verbose                          activează mesaje detaliate de stare; implicit: dezactivat\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: Şterge o baza de date.\n\
utilizare: %1$s deletedb [OPTION] nume-baza-de-date\n\
\n\
opţiuni valide:\n\
  -o, --output-file=FISIER     redirecţionează mesajele de ieşire către FISIER; implicit: nul\n\
  -d, --delete-backup          şterge volumele de rezervă; implicit: nu şterge\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 EROARE: volext-pathname nu există sau nu poate fi scris.\n
32 EROARE: vols-tofrom-paths_file nu există sau nu poate fi citit.\n
60 \
renamedb: Redenumeşte o baza de date.\n\
utilizare: %1$s renamedb [OPTION] nume-bază-de-date-sursa nume-bază-de-date-destinaţie\n\
\n\
opţiuni valide:\n\
  -E, --extended-volume-path=CALE   CALE către directorul pentru volume de informaţii extinse; implicit: aceeaşi cale\n\
                                    ca şi volumele bazei de date, sau calea dată în control-file\n\
  -i, --control-file=FISIER         redenumirea mai multor volume din locaţii diferite se înregistrează în\n\
                                    FIŞIERUL de control; implicit: nul\n\
  -d, --delete-backup               şterge volumele de rezervă; implicit: nu şterge\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 Un proces %1$s se află deja în execuţie.\n
12 MASTER SERVER porneşte. \n
13 MASTER SERVER se opreşte.\n
14 Eroare la căutarea numelui de gazdă (host) local\n
15 Nu s-a găsit fişierul de parametri\n
16 Nu s-a putut iniţializa procesul principal\n
17 Serverul %1$s se va opri în %2$d minute
18 Serverul %1$s a primit notificarea de oprire.\nAceastă acţiune poate dura cateva miute. Vă rugăm aşteptaţi.
19 Serverul %1$s nu a fost găsit.
20 Procesul principal (master) se va opri în %1$d minute\n
21 Heartbeat failover was finished. Please wait until all shutdown processes of cub_servers are completed.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Fişierul de ieşire '%1$s' nu a putut fi deschis.\n
60 \
optimizedb: Optimizează o bază de date.\n\
utilizare: %1$s optimizedb [OPTIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -n, --class-name=NUME        actualizează statisticile pentru clasa cu numele specificat; implicit: toate clasele\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Data în format invalid, formatul valid pentru dată este dd-mm-yyyy:hh:mm:ss sau 'backuptime'\n
20 Restoredb a fost anulat sau s-a produs o eroare.\n
60 \
restoredb: Restaurează o bază de date.\n\
utilizare: %1$s restoredb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -d, --up-to-date=DATA             restaurează baza de date la starea de la DATA furnizată. DATA este furnizată\n\
                                    sub forma dd-mm-yyyy:hh:mm:ss sau folosind cuvântul cheie 'backuptime'\n\
      --list                        afişează o listă cu volume de rezervă, fără a face restaurarea\n\
  -B, --backup-file-path=CALE       CALE către un director de volume de rezervă ce trebuie restaurate\n\
  -l, --level=NIVEL                 NIVEL de backup pentru restaurare; consultaţi utilizarea lui 'backup'; implicit: 0(complet)\n\
  -p, --partial-recovery            efectueaza restaurare parţială dacă există arhive de log lipsă\n\
  -o, --output-file=FIŞIER          redirecţionează mesajele de ieşire către FISIER; implicit: nul\n\
  -u, --use-database-location-path  restaurează baza de date şi volumele de log în calea specificată\n\
                                    în fişierul de locaţie al bazei de date\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 Numele bazei de date lipseşte.\n
20 Fişierul '%1$s' nu are %2$d linii.\n
21 întrerupere ... \n
22 Încărcare întreruptă - tranzacţia curentă a fost finalizată\n
23 Încărcare întreruptă - tranzacţia curentă a fost abandonată\n
24 Fişierul '%1$s' nu poate fi deschis\n
25 \n*** Verificarea sintaxei ***\n\n
26 %1$d erori detectate.\n
27 \n*** Inserarea de obiecte ***\n\n
28 Un total de %1$8d obiecte inserate.\n
29 Un total de %1$8d obiecte referenţiate au fost inserate cu conţinut implicit.\n
30 \n*** Se finalizează tranzactia ***\n
31 \n*** Se închide baza de date ***\n
32 Linia %1$d:
33 Eroare la parsarea lui '%1$s' ca o valoare '%2$s' pentru atributul '%3$s' din '%4$s'\n
34 Domeniu lipsă pentru atributul %1$s din %2$s\n
35 Pentru atributul %1$s din %2$s, tipul %3$s nu este permis în domeniul mulţimii\n
36 Pentru atributul %1$s din %2$s, tipul %3$s aşteptat, s-a recepţionat o constantă mulţime\n
37 Pentru atributul %1$s din %2$s, tipul %3$s aşteptat, s-a recepţionat %4$s\n
38 Nicio clasa cu numele %1$s referenţiată de atributul %2$s din %3$s\n
39 Nicio clasa cu numele %1$s, se ignoră tupluri\n
40 Nicio clasa cu identificatorul %1$d referenţiată de atributul %2$s din %3$s\n
41 Clasa %1$s nu este permisă în fişierul de intrare al loader-ului\n
42 Oprit cu eroare.\n
43 AVERTISMENT: s-a actualizat o instanţă existentă!\n
44 AVERTISMENT: se redefineşte instanţa %1$d a clasei %2$s\n
45 Instanţa %1$d a clasei %2$s a fost deja definită\n
46 Instanţa %1$d a clasei %2$s a fost deja rezervată\n
47 Încălcare a constrângerii UNIQUE pentru atributul '%1$s', conversie la NULL\n
48   %1$d instanţe\n
49 Clasa %1$s\n
50 Parola:
51 \n*** Se actualizează statisticile claselor ***\n
52 %1$s lânga linia %2$d (textul a fost '%3$s')\n
53 Eroare lexicală
54 linia %1$d: eroare de sintaxă la \"%2$s\"
55 absent
56 în %1$s
57 Argumente incompatibile, \"%1$s\", şi \"%2$s\"\n
58 %1$d instanţe commise (salvate)\n
59 \nNu se poate invoca versiunea neoptimizată a lui loaddb.\n
60 Eroare la parsarea lui \"%1$s\", ca fiind de tip %2$s.\n
112   %1$d instanţe\r
113 Ultima linie salvata (prin commit) : %1$d
114 error-control-file
115 una sau mai multe erori la încărcarea fişierului de intrare de control
116 Total %8d object(s) inserted, %d object(s) failed.\n
117 Load failed.\n
118 Maximum length of class name is %1$d bytes.\n
120 \
loaddb: Importă obiecte şi scheme în baza de date.\n\
utilizare: %1$s loaddb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -u, --user=ID                     ID de utilizator pentru acces la baza de date; implicit: "public"\n\
  -p, --password=PAROLA             parola de utilizator; implicit: nulă\n\
      --data-file-check-only        verifică sintaxa pentru fişierul de date fără a-l încarca;\n\
                                    implicit: încarcarea se face dupa verificare\n\
  -l, --load-only                   încarcă fişierul de date fără a verifica sintaxa;\n\
                                    implicit: încărcarea se face după verificare\n\
      --estimated-size=NUMĂR        NUMĂR estimat de instanţe; implicit: 5000\n\
  -v, --verbose                     activează mesaje detaliate de stare; implicit: dezactivate\n\
  -c, --periodic-commit=CONTOR      CONTOR de inserare pentru salvare periodică;\n\
                                    implicit: nu se face commit în timpul încărcării\n\
      --no-oid                      nu se va folosi OID\n\
      --no-statistics               nu se vor actualiza statisticile în timpul încărcării\n\
  -s, --schema-file=FIŞIER[:LINIE]  creează schema de la linia, respectiv din fişierul specificat\n\
  -i, --index-file=FIŞIER[:LINIE]   creeaza index de la linia, respectiv din fişierul specificat\n\
  -d, --data-file=FIŞIER            incarca datele din FIŞIER\n\
  -t, --table=TABELA                numele tabelei înlocuite pentru antetul de clasă absent din fişierul de date\n\
      --error-control-file=FIŞIER   FIŞIER de control al erorilor în timpul încărcării\n\
      --ignore-class-file=FIŞIER    FIŞIER de intrare cu numele claselor ce nu vor fi încărcate\n
121 \
load_noopt: Acest utilitar este învechit. A se folosi cu atenţie.\n\
utilizare: load_noopt [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -u, --user=ID                     ID de utilizator pentru acces la baza de date; implicit: "public"\n\
  -p, --password=PAROLĂ             parola de utilizator; implicit: nulă\n\
      --data-file-check-only        verifică suntaxa pentru fişierul de date fără a-l încărca;\n\
                                    implicit: încărcarea se face după verificare\n\
  -l, --load-only                   încarcă fişierul de date fără a verifica sintaxa;\n\
                                    implicit: încărcarea se face dupa verificare\n\
      --estimated-size=NUMĂR        NUMĂR estimat de instanţe; implicit: 5000\n\
  -v, --verbose                     activează mesaje detaliate de stare; implicit: dezactivate\n\
  -c, --periodic-commit=CONTOR      CONTOR de inserare pentru salvare periodică;\n\
                                    implicit: nu se face commit in timpul încărăarii\n\
      --no-oid                      nu se va folosi OID\n\
  -s, --schema-file=FIŞIER[:LINIE]  creează schema de la linia, respectiv din fişierul specificat\n\
  -i, --index-file=FIŞIER[:LINIE]   creează un index de la linia, respectiv din fişierul specificat\n\
  -d, --data-file=FIŞIER            încarcă datele din FIŞIER\n\
      --error-control-file=FIŞIER   FIŞIER de control al erorilor în timpul încărcării\n\


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 Valoarea pentru cached-pages este invalidă.\n
42 Valoarea pentru cached-page-size este invalidă.\n
43 %1$d obiecte vărsate.\n
44 Clasa %1$s
45 , %1$d instanţe.\n
46 Doar %1$d din %2$d obiecte vărsate cu succes.\n
47 directorul de ieşire este prea lung.\n
48 LSA : %1$d %2$d \n
49 Parola:
60 \
unloaddb: Exportă obiecte şi scheme din baza de date.\n\
utilizare: %1$s unloaddb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -u, --user=ID                  ID de utilizator pentru acces la baza de date; implicit: "dba"\n\
  -p, --password=PAROLĂ          parola de utilizator; implicit: nulă\n\
  -i, --input-class-file=FIŞIER  fişier de intrare cu numele tabelelor; implicit: varsă toate clasele\n\
      --include-reference        include tabele referenţiate; cu opţiunea -i; implicit: dezactivat\n\
      --input-class-only         include doar clasa specificată; cu opţiunea -i; implicit: dezactivat\n\
      --lo-count=NUMĂR           numărul de fişier LO pentru un director; implicit: 0\n\
      --estimated-size=NUMĂR     numărul estimat de instanţe; implicit: calculat automat\n\
      --cached-pages=NUMĂR       numărul de pagini încărcate în memoria cache; implicit: 100\n\
  -O, --output-path=CALE         calea către directorul de ieşire; implicit: directorul de lucru\n\
  -s, --schema-only              varsă doar schema; implicit: schema şi obiectele\n\
  -d, --data-only                varsă doar obiectele; implicit: schema şi obiectele\n\
      --output-prefix=PREFIEX    PREFIX pentru fişierele de ieşire; implicit: numele bazei de date\n\
      --hash-file=FIŞIER         FIŞIER pentru hash; implicit: generat cu tmpnam()\n\
  -v, --verbose                  activează mesaje de stare detaliate; implicit: dezactivate\n\
      --use-delimiter            foloseşte '"' unde începe şi se termina un identificator;\n\
                                 implicit: nu se foloseşte delimitator\n\
  -S, --SA-mode                  mod de execuţie independent\n\
  -C, --CS-mode                  mod de execuţie client-server\n\
      --datafile-per-class       creează un fişier obiect pentru fieacre clasă; implicit: dezactivat\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nTrecerea 1\n\n
12 %1$d obiecte procesate.\n
13 \nTrecerea 2\n\n
14 Clasa %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d instanţe.\n
17 Se actualizează obiectul.\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 Nu s-a putut aplica transformarea obiectului\n
20 Fişier heap absent pentru clasă\n
21 Nu s-a putut actualiza obiectul\n
22 Doar %1$d din %2$d obiecte au fost procesate cu succes.\n
23 COMPACTDB deja pornit.\n
24 Numărul de pagini trebuie să fie cuprins între %1$d şi %2$d.\n
25 Timpul de expirare al blocajelor de instanţă trebuie să fie cuprins între %1$d şi %2$d.\n
26 Obiecte în total: %1$d\n
27 Obiecte eşuate: %1$d\n
28 Obiecte modificate: %1$d\n
29 Obiecte mari: %1$d\n
30 Reprezentările vechi ale claselor au fost şterse din catalog.\n
31 Reprezentările vechi ale claselor au fost păstrate în catalog.\n
32 Nivelul de izolare nu poate fi setat.\n
33 Baza de date nu a putut fi compactată.\n
34 Timpul de expirare al blocajelor pe clasă trebuie să fie cuprins între %1$d şi %2$d.\n
35 Clasa blocată... Procesare incompletă...\n
36 Clasa neprocesată...\n
37 Eroare de procesare... Procesare incompletă...\n
38 Nimic de procesat...\n
39 Parametri invalizi...\n
40 Nume de clasă necunoscut...\n
41 OID-urile şi paginile de clasă neutilizate au fost recuperate.\n
42 S-a omis recuperarea OID-urilor şi a paginilor.\n
43 Eroare la procesarea clasei, clasă omisă.\n
44 \nTrecerea 3\n\n
45 Compactare eşuată a heap-ului...\n
46 Compactarea heap-ului s-a realizat cu succes...\n
60 \
compactdb: Eliberează spaţiul neutilizat dintr-o bază de date.\n\
utilizare: %1$s compactdb [OPŢIUNI] nume-bază-de-date [nume_clasă1 nume_clasă2 ...]\n\
\n\
opţiuni valide:\n\
  -v, --verbose                     activează mesaje de stare detaliate; implicit: dezactivate\n\
  -i, --input-class-file=FIŞIER     fişier de intrare cu numele tabelelor\n\
                                    Lista de nume de clase (class_name) trebuie sa fie vidă\n\
                                    implicit: se compactează toate clasele\n\
                                    activat numai în modul client-server (CS)\n\
  -p, --pages-commited-once=NUMĂR   numărul maxim de pagini ce pot fi comise/salvate o dată\n\
                                    implicit: 10\n\
                                    activat numai în modul client-server\n\
  -S, --SA-mode                     mod de execuţie independent\n\
  -C, --CS-mode                     mod de execuţie client-server\n\
  -d, --delete-old-repr             şterge reprezentările vechi ale claselor din catalog; implicit: dezactivat\n\
  class_name_list                   activat numai în modul client-server\n\
  -I, --Instance-lock-timeout       timp de expirare pentru blocajele de instanţă\n\
  -c, --class-lock-timeout          timp de expirare pentru blocajele de clasă\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 Procesul principal CUBRID master, aflat în funcţiune pe maşina gazdă %1$s \na fost pornit la %2$s
22 a servit %1$d cereri. %2$d server(e) aflate în funcţiune.\n
23 OPRIREA se va efectua în %1$s minute\n
24 %1$s
25 Numele serverului ce va fi oprit :
26 Câte minute până la oprire? :
27 \
cub_commdb: Acest utilitar este învechit. Utilizaţi comanda 'cubrid' în loc.\n\
utilizare: cub_commdb [OPŢIUNI]\n\
\n\
opţiuni valide:\n\
  -P, --server-list                afişează procesele server\n\
  -O, --all-list                   afişează toate procesele\n\
  -S, --shutdown-server=NUME       opreşte serverul de bază de date specificat prin NUME\n\
  -A, --shutdown-all               opreşte toate procesele\n\
  -c, --server-mode=NUME           afişează modul de execuţie HA al serverului specificat prin NUME\n\
  -N, --node-list                  afişează nodurile gestionate de către CUBRID heartbeat\n\
  -L, --process-list               afişează procesele locale gestionate de către CUBRID heartbeat\n\
  -p, --ping-host                  list ping hosts used for CUBRID heartbeat\n\
  -D, --dereg-process=PID          încheie procesul ţintă şi îl elimină din lista de procese gestionate\n\
  -F, --reconfig-node-list         aplică configuraţia recent modificată pentru CUBRID heartbeat\n\
  -V, --verbose                    activează emsaje de stare detaliate; implicit: dezactivate\n
28 Regretăm, comandă necunoscută: %1$s\n
29 Numele de gazdă pentru maşina locală nu a putut fi recuperat - abandon\n
30 Parametrul fişier nu a putut fi găsit\n
31 Nu s-a putut realiza conexiunea la serverul principal/master de la %1$s\n
32 Câte minute până la oprire? :
33 Master-ul se va opri în %1$d minute\n
34 Eroare la recepţionarea datelor de la client
35 serverul de baze de date %1$s nu se afla în funcţiune
36 nu se poate executa shell-ul (%1$s) pentru %2$s, %3$s
37 creare de proces (fork) %1$s, a întors %2$d, %3$s
38 nu se poate executa %1$s
39 Opţiunea -i(--immediately) trebuie folosită împreună cu opţiunea --deact-stop-all.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: Efectuează un patch asupra bazei de date în caz de urgenţă.\n\
utilizare: %1$s emergency_patchlog [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -r, --recreate-log           recreează fişierele de log; a se utiliza cu atenţie\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Valoare greşită %1$d pentru n-pagini\n
21 Valoare greşită '%1$s' pentru scopul volumului de stocare.\n
22 AVERTISMENT: opţiunea --max-writesize-in-sec este ignorată în modul de execuţie independent.\n
60 \
addvoldb: Adaugă un fişier de volum pentru baza de date.\n\
utilizare: %1$s addvoldb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
      --db-volume-size=MĂRIME        mărimea volumelor adiţionale; implicit: valoarea lui db_volume_size din cubrid.conf\n\
      --max-writesize-in-sec=MĂRIME  dimensiunea volumului de date scris pe secundă; (ex. 512K, 1M, 10M);\n\
                                     implicit: neutilizat; minim: 160K\n\
  -n, --volume-name=NUME             NUMELE volumului de informaţii; implicit: generat (după modelul "db"_ext1)\n\
  -F, --file-path=CALE               CALEA unde se va adăuga fişierul pentru noul volum; implicit: directorul de lucru\n\
      --comment=COMENTARIU           COMENTARIU pentru adăugarea fişierului de volum; implicit: absent\n\
  -p, --purpose=SCOP                 SCOP pentru adăugarea fişierului de volum; valori permise:\n\
                                           DATA - doar pentru date\n\
                                           INDEX - doar pentru indecşi\n\
                                           TEMP - doar pentru date temporare\n\
                                           GENERIC - pentru toate scopurile\n\
  -S, --SA-mode                      mod de execuţie independent\n\
  -C, --CS-mode                      mod de execuţie client-server\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Au fost detectate unele inconsistenţe în baza de date.\n Vă rugăm accesaţi jurnalul de erori din \n\
fişierul %1$s pentru informaţii suplimentare.\n
21 %1$s: Clasa nu poate fi găsită. Va fi omisă.\n
22 %1$s: Cannot find the index on class %2$s.\n
60 \
checkdb: Efectuează o verificare a consistenţei bazei de date.\n\
utilizare: %1$s checkdb [OPŢIUNI] nume-bază-de-date [nume_clasă1 nume_clasă2 ...]\n\
\n\
opţiuni valide:\n\
  -S, --SA-mode                  mod de execuţie independent\n\
  -C, --CS-mode                  mod de execuţie client-server\n\
  -r, --repair                   repară inconsistenţele detectate\n\
  -i, --input-class-file=FIŞIER  fişier de intrare cu numele claselor\n\
                                 implicit: se verifică toate clasele\n\
  -I, --index-name             index name that will be checked only\n\
                               default: check all indices\n\
      --check-prev-link        verifică doar legăturile predecesor ale indecşilor\n\
      --repair-prev-link       repararea legăturilor predecesor ale indecşilor\n\
      --check-file-tracker     check all allocated pages of file\n\
      --check-heap             check all pages of heap files\n\
      --check-catalog          check catalog consistency\n\
      --check-btree            check all btree indices\n\
      --check-class-name       check class name consistency\n\
      --check-btree-entries    check entries of btree consistency\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 Descrierea spaţiului pentru baza de date '%1$s' cu pagini de dimensiune %2$s. (dimensiunea unei pagini de log: %3$s)\n\n
16 %1$5d %2$9s  %3$s %4$s  %5$s\n
17 Nu s-a putut deschide fişierul de ieşire '%1$s'\n
18 Descrierea spaţiului pentru volumele temporare ale bazei de date '%1$s' cu pagini de dimensiune %2$s.\n\n
19 Volid  Purpose   total_pages  free_pages  Vol Name\n\n
20 Volid  Purpose    total_size   free_size  Vol Name\n\n
21 Descrierea spaţiului pentru LOB %1$s \n\n
22 -------------------------------------------------------------------------------\n
30 Descrierea sumarizată a spaţiului pentru baza de date '%1$s' cu pagini de dimensiune %2$s. (dimensiunea unei pagini de log: %3$s)\n\n
31 Purpose    total_pages  used_pages  free_pages  volume_count\n
32 Purpose     total_size   used_size   free_size  volume_count\n
33 %1$10s %2$s %3$s %4$s %5$10d\n
34 -------------------------------------------------------------\n
40 Volid  Purpose   total_pages  free_pages  data_pages index_pages  temp_pages  Vol Name\n\n
41 Volid  Purpose    total_size   free_size   data_size  index_size   temp_size  Vol Name\n\n
42 %1$5d %2$9s  %3$s %4$s %5$s %6$s %7$s  %8$s\n
43 ----------------------------------------------------------------------------------------------------\n
50 Purpose    total_pages  used_pages  free_pages  data_pages index_pages  temp_pages  volume_count\n
51 Purpose     total_size   used_size   free_size   data_size  index_size   temp_size  volume_count\n
52 %1$10s %2$s %3$s %4$s %5$s %6$s %7$s %8$10d\n
53 -------------------------------------------------------------------------------------------------\n
60 \
spacedb: Afişează spaţiul utilizat/liber al bazei de date.\n\
utilizare: %1$s spacedb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -o, --output-file=FIŞIER     redirecţionează mesajele de ieşire către un FIŞIER; implicit: niciunul\n\
  -S, --SA-mode                mod de execuţie independent\n\
  -C, --CS-mode                mod de execuţie client-server\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               setează unitatea de afişare (pagina, mega, giga, tera, format lizibil); implicit: H\n\
  -s, --summarize              afişează doar un sumar pentru fiecare scop de volum\n\
  -p, --purpose                afişează dimensiunea fiecărui scop(data/index/temp) al fiecărui volum\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Numărul estimat de pagini = %1$d\n
60 \
estimate_data: Estimează dimensiunea datelor.\n\
utilizare: %1$s date_estimate nr-instanţe dimensiune-medie-insţantă nr-de-atribute nr-variabil-de-atribute\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Tipul de cheie de index este invalid= %1$s\n
16 Lungimea medie de cheie este invalida= %1$d când precizia = %2$d\n
17 Argumente invalide\n
20 Numar total estimat de pagini de index = %1$d\n (La crearea indexului, dupa popularea instanţelor.)\n
21 Număr total estimat de pagini de index = %1$d\n (La crearea indexului, înainte de popularea instanţelor (caz mediu))\n
22 Număr total estimat de pagini de index = %1$d\n (La crearea indexului, înainte de popularea instanţelor (cazul cel mai dezavantajos))\n
23 Pentru datele de intrare...\n
24 Număr total de instanţe : %1$d\n
25 Număr de chei distincte : %1$d\n
26 Dimensiunea medie a cheilor : %1$d\n
27 Tip de cheie                : %1$s\n\n
60 \
estimate_index: Estimează dimensiunea unui index.\n\
utilizare: %1$s index_estimat nr_instante nr_chei-distincte dimensiune-medie-chei tip-de-cheie\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Instalează o bază de date.\n\
utilizare: %1$s installdb [OPŢIUNI] nume-bază-de-date\n\
\n\
optiuni valide:\n\
      --server-name=HOST       numele HOST al serverului; implicit: numele curent de gazdă\n\
  -F, --file-path=CALE         CALEA către directorul volumelor bazei de date; implicit: directorul curent\n\
  -L, --log-path=CALE          CALEA către directorul volumelor de log; implicit: la fel ca file-path\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 OID de clasă temporară\n
2 Nu se pot lista valorile ELO\n
3 Eroare la accesarea obiectelor lungi pentru migrare\n
4 Eroare la deschiderea fişierului de ieşire pentru migrarea obiectelor lungi \n
5 Eroare de citire\n
6 Eroare de scriere\n
7 Eroare la deschiderea fişierului ELO de intrare "%1$s"\n
8 CLASE:
9 nume de fişier hash
10 Nume:                         %1$s\n
11 Dimensiune:                   %1$d\n
12 Dimensiunea paginii:          %1$d\n
13 Dimensiunea datelor:          %1$d\n
14 Dimensiunea înregistrărilor:  %1$d\n
15 Înregistrări per pagina:      %1$d\n
16 Pagini în memoria cache:      %1$d\n
17 Număr de înregistrări:        %1$d\n
18 Număr de coliziuni:           %1$d\n
19 Numele fişierului hash:       %1$s\n
20 Urmatoarea înregistrare cu overflow: %1$d\n
21 Tip de cheie:                 %1$s\n
22 Adresa antetelor de pagină:   %1$x\n
23 Adresa ultimului antet de pagină:    %1$x\n
24 Adresa antetului de pagină liberă:   %1$x\n
25 Adresa bitmap-ului de pagini: %1$x\n
26 Dimensiunea bitmap-ului de pagini:   %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Varsă o baza de date.\n\
utilizare: %1$s diagdb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -d, --dump-type=TIP      TIP de vărsare\n\
                            -1 varsă tot\n\
                             1 varsă tabelele fişier\n\
                             2 varsă capacităţile fişierelor\n\
                             3 varsă capacităţile heap-ului\n\
                             4 varsă capacităţile indecşilor\n\
                             5 varsă numele claselor\n\
                             6 varsă bitmap-urile de disc\n\
                             7 varsă catalogul\n\
                             8 varsă log-ul\n\
                             9 varsă heap-ul\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 Fişierul de ieşire '%1$s' nu a putut fi deschis\n
59 lockdb nu poate fi executat în modul independent.\n
60 \
lockdb: Afişează situaţia lock-urilor bazei de date.\n\
utilizare: %1$s lockdb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -o, --output-file=FIŞIER    redirecţionează mesajele de ieşire către un FIŞIER; implicit: nul\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Trebuie furnizat un singur argument din urmatoarele: tran-index, user, host, progname\n
21 Introduceţi parola de DBA:
26 Nu există tranzacţie pentru valorile specificate în argumente.\n
27 Gata de a întrerupe urmatoarele tranzacţii:\n\n
28 Doriţi să continuaţi ? (Y/N)
29 Se întrerupe tranzacţia asociată indexului de tranzacţie %1$d\n
30 Întreruperea următoarelor tranzacţii a eşuat\n\n
31 Tranzacţia încă nu s-a finalizat, probabil execută o derulare înapoi (rollback).\n Vă rugăm asteptaţi şi încercaţi mai târziu dacă este necesar.\n
32 Index de tranzacţie invalid(%1$s).\n
59 killtran nu poate rula independent.\n
60 \
killtran: Administrează tranzacţiile bazei de date.\n\
utilizare: %1$s killtran [OPŢIUNI] nume-bază-de-date\n\
\n\
valid options:\n\
  -i, --kill-transaction-index=INDEX întrerupe tranzacţia cu indexul specificat\n\
      --kill-user-name=ID            întrerupe toate tranzacţiile asociate utilizatorului cu IDul specificat\n\
      --kill-host-name=NUME-CLIENT   întrerupe toate tranzacţiile asociate CLIENTULUI\n\
      --kill-program-name=NUME       întrerupe toate tranzacţiile cu numele programului client specificat\n\
      --kill-sql-id=SQL_ID           întrerupe toate tranzacţiile cu SQL_ID\n\
  -p, --dba-password=PAROLA          PAROLA utilizatorului DBA; va fi solicitată dacă nu este specificată\n\
  -d, --display-information          afişează informaţii despre tranzacţiile active\n\
  -q, --query-exec-info              afişează informaţii despre execuţia interogărilor din tranzacţiile active\n\
  -f, --force                        intrerupe fortat tranzacţia fără a solicita vreo verificare\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 database-name
2 argument obligatoriu, fără valoare implicită
3 compatibility-check
4 compatibility check only
5 backup-destination
6 calea către un director sau dispozitiv pentru destinaţia volumelor de rezervă
20 baza de date '%1$s' nu există.
21 '%1$s' nu necesită migrare la %2$s, deoarce versiunea este prea veche.\n
22 '%1$s' nu necesită migrare.\n
23 '%1$s' necesita migrare.\n
24 După migrare se recomandă crearea unei copii de rezervă. Doriţi să faceţi acum o copie de rezervă a bazei de date? (y/n):
25 Se recomandă crearea unei copii de rezervă pentru noua bază de date cu ajutorul lui backupdb dupa migrare.
26 \n*** AVERTISMENT: Creaţi o copie de rezervă a bazei de date înainte să faceţi migrarea la %1$s ***\n\nPentru a putea preîntâmpina posibilele erori din timpul migrării, se impune crearea unei copii de rezervă a bazei de date.\nAţi creat deja o copie de rezervă a bazei de date? (y/n):
27 \n1. Executaţi backupdb \n2. Introduceţi noua cale a copiei de rezervă pentru noua bază de date.(calea curentă: %1$s)\n3. Ieşire \n
28 \nNoua cale către copia de rezervă a bazei de date:
29 \nMigrarea şi copia de rezervă au fost realizate cu succes.
30 Crearea copiei de rezervă a eşuat. Vă rugăm încercaţi încă o dată să creaţi o copie de rezervă a bazei dvs. de date.
31 \n\n*** AVERTISMENT : Dupa ce migrarea va fi completă, nu veţi mai putea restaura baza de date cu volumele de rezervă anterioare.\nObservaţi că versiunile anterioare ale fişierelor de rezervă pot fi restaurate numai cu versiunea corespunzătoare de  restoredb.\nVa rugăm asiguraţi-vă că vechile volume de rezervă sunt păstrate în siguranţă.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Modifica numele de host al bazei de date.\n\
utilizare: %1$s alterdbhost [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -h, --host=HOST              nume de HOST nou; implicit: numele curent\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 database-name
2 argument obligatoriu, fără valoare implicită
3 java-class-file
4 argument obligatoriu, fără valoare implicită
5 force-overwrite
6 suprascrie fişierul de clasă dacă exista, implicit nu

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Fişierul de ieşire '%1$s' nu a putut fi deschis\n
59 plandump nu poate fi executat în modul independent.\n
60 \
plandump: Afişeaza şi controlează planurie de interogare din cache.\n\
utilizare: %1$s plandump [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -d, --drop                   şterge toate planurile din memoria cache a serverului\n\
  -o, --output-file=FIŞIER     redirecţionează mesajele de ieşire către FIŞIER; implicit: nul\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Fişierul de ieşire '%1$s' nu a putut fi deschis\n
21 Parametri de sistem ce vor fi folosiţi de către aplicaţia client.\n
22 Parametri de sistem încărcati de serverul '%1$s'\n
23 Parametri de sistem folosiţi în modul independent.\n
60 \
paramdump: Varsă configuraţia parametrilor de sistem.\n\
utilizare: %1$s paramdump [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -o, --output-file=FIŞIER     redirecţionează mesajele de ieşire către FIŞIER; implicit: nul\n\
  -b, --both                   varsă atât parametri de client, cât şi pe cei de sistem\n\
  -S, --SA-mode                mod de execuţie independent\n\
  -C, --CS-mode                mod de execuţie client-server\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Valoare greşită \"%1$s\" pentru modul de execuţie HA al serverului.\n
12 Nu se poate schimba modul de execuţie HA al serverului.\n
21 Introduceţi parola de administrator (DBA):
22 Pentru serverul '%1$s', modul curent de execuţie HA este %2$s.\n
23 Pentru serverul '%1$s', modul de execuţie HA este modificat în \"%2$s\".\n
24 Serverul nu a fost configurat pentru HA.\n
58 Funcţionalitatea HA nu este suportată de această platformă.\n
59 changemode nu poate fi executat în modul independent.\n
60 \
changemode: Afişează sau modifică modul de execuţie HA pentru server.\n\
utilizare: %1$s changemode [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -m, --mode=MOD         schimbă modul serverului în cel specificat; implicit: niciunul\n\
  -f, --force            forţează schimbarea\n\
  -t, --timeout=S        aşteaptă S secunde pentru finalizarea normală a tranzacţiei\n\
                         opţiunea -t are efect doar la schimbarea modului serverului în mentenanţă\n\
                         implicit: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Valoare eronată \"%1$s\" pentru modul de execuţie al lui copylogdb.\n
21 Introduceţi parola de administrator (DBA):
22 Serverul nu a fost configurat pentru HA.\n
58 Funcţionalitatea HA nu este suportată pe această platformă.\n
59 copylogdb nu poate fi executat în modul independent.\n
60 \
copylogdb: Copiază pagini de log de pe server.\n\
utilizare: %1$s copylogdb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -L, --log-path=CALE       CALEA către directorul pentru volumele de log copiate\n\
  -m, --mode=MODE           mod de operare; implicit: sync\n\
                            MOD: sync sau semisync sau async\n\
  -S, --start-page-id=ID    first log page to copy\n 

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 Introduceţi parola de administrator (DBA):
22 Serverul nu a fost configurat pentru HA.\n
58 Funcţionalitatea HA nu este suportată pe această platformă.\n
59 applylogdb nu poate fi executat în modul independent.\n
60 \
applylogdb: Aplică pe server log-urile copiate.\n\
utilizare: %1$s applylogdb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -L, --log-path=CALE         CALEA către directorul volumelor de log copiate\n\
      --max-mem-size=MĂRIME   mărimea maximă de procesare în MB; max 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Fişierul de ieşire '%1$s' nu a putut fi deschis\n
59 statdump nu poate fi executat în modul independent.\n
60 \
statdump: Varsă statisticile globale.\n\
utilizare: %1$s statdump [OPŢIUNI] nume-baza-de-date\n\
\n\
opţiuni valide:\n\
  -o, --output-file=FIŞIER     redirecţionează mesajele de ieşire către un FIŞIER; implicit: nul\n\
  -c, --cumulative             afişează statistici cumulative\n\
  -i, --interval=S             afişează statisticile la fiecare S secunde\n\
  -s, --substr=STRING          afişează un element de statistică ce conţine şirul de caractere specificat\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 Introduceţi parola de administrator (DBA):
22 Serverul nu a fost configurat pentru HA.\n
58 Funcţionalitatea HA nu este suportată pe această platformă.\n
59 applyinfo nu poate fi executat în modul independent.\n
60 \
applyinfo: afişează informaţii despre CUBRID HA Apply.\n\
utilizare: %1$s applyinfo [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -r, --remote-host-name      numele gazdei remote; afişează informaţii despre logul activ al nodului departat \n\
  -a, --applied-info 	      afişează informaţiile aplicate \n\
  -L, --copied-log-path=CALE  calea către volumele de log copiate; afişează informaţii despre logurile copiate\n\
  -p, --page=ID               id de pagina; implicit : 0\n\
  -v, --verbose               activează mesaje de stare detaliate; implicit : dezactivate\n\
  -i, --interval=S            afişează informaţii la fiecare S secunde\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb nu poate fi executat în modul independent.\n
60 \
acldb: Afişeaza sau reîncărca configuraţia de control al accesului la baza de date.\n\
utilizare: %1$s acldb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -r, --reload       reîncărca fişierul de control al ip-urilor\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Generează librării partajate pentru locale-uri utilizand fişier(e) LDML de intrare.\n\
utilizare: %1$s genlocale [OPŢIUNI] [string-limbă]\n\
\n\
string-limbă   identificator de locale pentru care se va genera librăria partajată.\n\
		       Dacă identificatorul nu este furnizat, toate limbile din 'cubrid_locales.txt'\n\
		       vor fi procesate.\n\
opţiuni valide:\n\
  -v, --verbose	              activează mesaje de stare detaliate; implicit : dezactivate\n\
  -i, --input-ldml-file       calea către un fişier LDML de intrare.\n\
                              Este valabil doar când se furnizează un identificator de limbă/localizare.\n
61 Numele de localizare dat ca argument este invalid.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 Opţiunea -i/--input-file şi identificatorul de locale nu pot fi utilizate simultan.\n
58 Interval de codepoint-uri invalid (-s -e).\n
59 Numele de localizare dat ca argument este invalid.\n
60 \
dumplocale: Varsă conţinutul librăriei partajate de localizări, generată anterior utilizând fişier(e) LDML de intrare.\n\
utilizare: %1$s dumplocale [OPŢIUNI] [string-limbă]\n\
\n\
string-limba   identificator de limbă, de tip string, pentru care se va vărsa conţinutul din librăria partajată.\n\
		       Dacă identificatorul nu este furnizat, toate limbile din 'cubrid_locales.txt'\n\
		       vor fi procesate.\n\
opţiuni valide:\n\
  -i, --input-file    calea către librăria partajată de intrare (generată anterior).\n\
                      Opţiunea poate fi utilizată numai dacă identificatorul de limbă nu este furnizat.\n\
  -d, --calendar      varsă informaţii despre calendar şi data/ora; implicit : dezactivat\n\
  -n, --numeric		  varsă informaţii despre numere; implicit : dezactivat\n\
  -a, --alphabet=l|lower|u|upper|both	     varsă informaţii despre alfabet şi despre casing; implicit : dezactivat\n\
      --identifier-alphabet=l|lower|u|upper	 varsă informaţii despre alfabet şi despre casing pentru identificatori;\n\
                                             implicit : dezactivat\n\
  -c, --codepoint-order	 varsă informaţii despre colaţie, ordonate după valorile codepoint-urilor\n\
                         (codepoint, caracter, ponderi, următorul caracter, caracter şi ponderi);\n\
                         implicit : dezactivat\n\
  -w, --weight-order  varsă informaţii despre colaţie, ordonate după valorile ponderilor\n\
                      (pondere, codepoint, caracter); implicit : dezactivat\n\
  -s, --start-value	  opţiune de filtrare; codepoint-ul de început pentru\n\
                      -a, --identifier-alphabet, -c, -w; implicit : 0\n\
  -e, --end-value     opţiune de filtrare; codepoint-ul de sfârşit pentru\n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      implicit : valoarea maximă citită din librăria de partajată de localizări\n\
  -k, --console-conversion  varsă datele de conversie a consolei; implicit : dezactivat\n\
  -z, --normalization   varsă datele de normalizare; implicit : dezactivat\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 Partiţionarea trebuie eliminată înainte de a schimba colaţia curentă '%1$s' a atributului de partiţionare.\nNumele clasei\n
44 Cheile străine peste atriubtele având colaţia '%1$s' trebuie şterse înainte de a schimba colaţia atributelor.\nTabelă | Cheie Externă\n
45 Clase având colaţia '%1$s'; trebuie redefinite :\nNume de clasă\n
46 Indecşi funcţionali ce conţin în expresia de definiţie colaţia '%1$s'; trebuie redefiniţi :\n\
Tabelă | Index | Expresia functiei\n
47 Sincronizarea colaţiilor nu a fost efectuată. Baza de date nu a fost modificată.\n
48 Conţinutul tabelei de sistem '%1$s' a fost actualizat cu noi colaţii de sistem.\n
49 Actualizarea colaţiilor de sistem ar putea produce daune bazei de date. Continuaţi (y/n) ?\n
50 Colaţia '%1$s' (Id: %2$d) nu a fost găsită în baza de date sau a fost modificată în noua configuraţie de sistem.\n
51 Declanşatori ce conţin colaţia '%1$s' în condiţie; trebuie redefiniţi :\nDeclanşator | Condiţie\n
52 Vederi (view) ce conţin colaţia '%1$s' în interogarea SQL; trebuie redefinite :\nVedere/View | Interogare SQL\n
53 Atribute având colaţia '%1$s'; trebuie redefinite.\nTabela | Domeniu atribut\n
54 Este necesară intervenţia manuală asupra bazei de date.\n\
Vă rugăm consultaţi fişierul '%1$s' şi executaţi-l înainte de a actualiza/adăuga forţat colaţii în baza de date.\n
55 Sincronizarea nu este necesară.\n
56 Este necesară sincronizarea colaţiilor de sistem în baza de date.\n\
Executaţi 'cubrid synccolldb -f %1$s'\n
57 Există %1$d colaţii suplimentare în configuraţia sistemului, în comparaţie cu baza de date:\n
58 Toate colaţiile din baza de date '%1$s' se regăsesc în noua configuraţie a sistemului.\n
59 Există %1$d colaţii în baza de date, care nu se regăsesc în configuraţia sistemului sau care s-au modificat în raport cu colaţiile de sistem.\n
60 \
synccolldb: Sincronizează colaţiile din baza de date cu cele ale sistemului.\n\
utilizare: %1$s synccolldb [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -c, --check-only    Verifică dacă există diferenţe între colaţiile bazei de date şi cele\n\
                      din configuraţia sistemului. Dacă este necesară sincronizarea colaţiilor,\n\
                      codul de eroare 1 este returnat.\n\
                      Se afişează un raport al colaţiilor incompatibile.\n\
  -f, --force-only    Actualizează colaţiile din baza de date, să fie identice cu cele din noua\n\
                      configuraţie a sistemului.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Introduceţi parola pentru %1$s: 
22 Index tranzacţie   Nume utilizator   Nume gazda   ID proces      Nume program\n
23 -------------------------------------------------------------------------------\n
24 %1$4d%2$-10s %3$14s %4$14s %5$15d %6$17s\n
25 Nu există tranzacţii\n
26 tranlist poate fi executat doar de către DBA sau un membru al grupului DBA.\n
27 Numărul coloanei din cheia de sortare(%1$d) este invalid.\n
32 Index tranzacţie    ID proces    Nume program         Durată interogare    Durată tranzacţie      Aşteaptă deţinătorul de lock      SQL_ID       Text SQL\n
33 %1$4d%2$-10s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Index tranzacţie   Nume utilizator   Nume gazdă   ID proces      Nume program      Durată interogare  Durată tranzacţie        Aşteaptă deţinător de lock      SQL_ID       Text SQL\n
43 %1$4d%2$-10s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Index tranzacţie : %1$d
46 SQL_ID: %1$s\n
59 tranlist nu poate fi executat în modul independent.\n
60 \
tranlist: Varsă informaţii despre tranzacţiile active.\n\
utilizare: %1$s tranlist [OPŢIUNI] nume-bază-de-date\n\
\n\
opţiuni valide:\n\
  -u, --user=ID                ID de utilizator pentru accesul la baza de date; implicit: utilizator DBA\n\
  -p, --password=PAROLA        parola de utilizator; default: nul\n\
  -s, --summary                afişează informaţii sumare despre tranzacţii\n\
      --sort-key=NUMAR_COLOANA numărul coloanei de sortare\n\
      --reverse                inversează rezultatul sortării\n

$set 50 MSGCAT_UTIL_SET_PREFETCHLOGDB
22 Serverul nu a fost configurat pentru HA.\n
23 Funcţionalitatea de prefetch este dezactivată.\n
58 HA nu este suportat pe această platformă.\n
59 prefetchlogdb nu poate rula în modul de execuţie independent.\n
60 \
prefetchlogdb: Pre-citirea paginilor de log de la server.\n\
usage: %1$s prefetchlogdb [OPTION] nume-baza-de-date\n\
\n\
valid options:\n\
  -L, --log-path=CALE         calea catre directorul pentru volume de log copiate\n
