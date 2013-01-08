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
1 Ungültige Länge oder Zeichen '%c' in Datenbank '%s'\n
2 Ausgabedatei konnte nicht geöffnet werden '%s'\n
3 Fehlender oder ungültiger Utilityname.\n
4 Ungültiger Utilityname '%s'\n
5 Die folgende Liste enthält die erkannte Utilitynamen:\n
6 Ungültiges Zeichen '%c' in Datenträger '%s'\n
9 \
%s (CUBRID Utility)\n\
%s\n\
Copyright (C) 2008 Search Solution Corporation. Alle Rechte vorbehalten.\n
10 \
Cubrid Administratorutility, Version %s\n\
Anwendung: %s <utility-name> [args]\n\
'%s <utility-name>' für Hilfe auf einem bestimmten Utility eintippen.\n\
\n\
Vorhandene Admin-Utilitynamen:\n\
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
%s ist ein Werkzeug für DBMS.\n\
Für zusätzliche Informationen, http://www.cubrid.com besuchen\n
12 '%s' Utilityname ist ungültig.\n'cubrid --help' für Anwendungshilfe eintippen.\n
13 '%s' Befehl ist ungültig.\nType 'cubrid --help' für Anwendungshilfe eintippen.\n
14 Eine oder mehrere Pflichteigenschaften konnten in den Konfigurationsdateien nicht geladen werden.\n
15 @ %s %s: %s\n
16 @ %s %s\n
17 ++ %s '%s' läuft nicht.\n
18 ++ %s läuft nicht.\n
19 ++ %s '%s' läuft.\n
20 ++ %s läuft.\n
21 ++ %s %s: %s\n
22 Eins oder mehrere Argumente fehlen.\n'cubrid --help' für Anwendungshilfe eintippen.\n
23 Cubrid Utility, Version %s\n\
Anwendung: %s <utility-name> [args]\n\
'%s <utility-name>' für Hilfe auf einem bestimmten Utility eintippen.\n\
\n\
Vorhandene Serviceutilities:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
    shard\n\
\n\
Vorhandene Administratorutilities:\n\
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
%s ist ein Werkzeug für DBMS.\n\
Für zusätzliche Informationen, http://www.cubrid.com, besuchen\n
24 Cubrid Utility, Version %s\n\
Anwendung: %s service <Befehl>\n\
\n\
Vorhandene Befehle:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 Cubrid Utility, Version %s\n\
Anwendung: %s Server <Befehl> [args]\n\
\n\
Vorhandene Befehle:\n\
    start       [Datenbanknamen]\n\
    stop        [Datenbanknamen]\n\
    restart     [Datenbanknamen]\n\
    status\n\
    acl         <status|reload> Datenbanknamen\n
37 Der Server wurde für HA konfiguriert.\n
26 Cubrid Utility, Version %s\n\
Anwendung: %s broker <Befehl> [args]\n\
\n\
Vorhandene Befehle:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [<expr>]\n\
    on          Brokername\n\
    off         Brokername\n\
    reset       Brokername\n\
    acl         <status|reload> [Brokername]\n
27 Cubrid Utility, Version %s\n\
Anwendung: %s manager <Befehl>\n\
\n\
Vorhandene Befehle:\n\
    start\n\
    stop\n\
    status\n
28 Cubrid Utility, Version %s\n\
Anwendung: %s heartbeat <Befehl> [args]\n\
\n\
Vorhandene Befehle:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    copylogdb   <start|stop> database-name node-name\n\
    applylogdb  <start|stop> database-name node-name\n\
    status\n\
    reload\n
31 '%s' Argument ist nicht nötig.\n
32 Ein Datenbanknamen fehlt.\n
33 %s: Diese Option ist veraltet.\n
34 Ungültiges Parameter. %s %s\n
35 Speicher kann nicht zugewiesen werden.\n
36 Der Server wurde für HA nicht konfiguriert.\n
38 Der für HA konfigurierte Server ist nicht in der ha_db_list enthalten.\n
39 Der für HA konfigurierte Server ist nicht in der ha_node_list enthalten.\n
62 Cubrid Utility, Version %s\n\
Anwendung: %s shard <Befehl> [args]\n\
\n\
Vorhandene Befehle:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-t] [-s <sec>] [<expr>]\n\
    on          shard-name\n\
    off         shard-name\n\
    reset       shard-name\n\
    acl         <status|reload> [shard-Name]\n\
    getid       -b <broker-name> [-f] shard-key\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 WARNUNG: -t Option wird in Stand-Alone-Ausführung ignoriert.\n
60 \
backupdb: Datenbank sichern.\n\
Anwendung: %s backupdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -D, --destination-path=PFAD  Backup-Datenträger in Verzeichnis-PFAD speichern; Standard: Verzeichnis des Datenbanklogs \n\
  -r, --remove-archive         unnötige Logarchiven löschen; sorgfältig anwenden, Anleitungen beachten \n\
  -l, --level=LEVEL            Backup-Level; Standard: Volles Level\n\
                               LEVEL ist erlaubt:\n\
                                   0 - Voll\n\
                                   1 - Inkrementell 1\n\
                                   2 - Inkrementell 2\n\
  -o, --output-file=DATEI      detaillierte Backupmeldungen in DATEI drucken\n\
  -S, --SA-mode                Stand-Alone-Ausführung\n\
  -C, --CS-mode                Client-Server-Ausführung\n\
      --no-check               Konsistenz der Datenbank nicht überprüfen; Standard: prüfen\n\
  -t, --thread-count=ANZAHL     Anzahl der Threads; Standard: 0(auto)\n\
  -z, --compress               Backupdatenträger komprimieren; Standard: nicht komprimiert\n\
  -e, --except-active-log      einen aktiven Logdatenträger nicht einfügen; Standard: einfügen\n\
      --sleep-msecs=N          N Millisekunden per gelesenen 1M schlafen; Standard: 0\n
71 Nicht kompatible Argumente, \"%s\", und \"%s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 FEHLER: Zielnamen ist gleich mit Quellname.\n
31 FEHLER: volext-Pfad existiert nicht oder ist nicht schreibbar.\n
32 FEHLER: vols-tofrom-paths_file existiert nicht oder ist nicht lesbar.\n
60 \
copydb: Datenbank kopieren.\n\
Anwendung: %s copydb [OPTION] Quelldatenbanknamen Zieldatenbanknamen\n\
\n\
gültige Optionen:\n\
      --Server-name=HOST          Server's Hostname; Standard: aktueller Hostname\n\
  -F, --file-path=PFAD            Verzeichnispfad für Datenbankdatenträger; Standard: aktuelles Verzeichnis\n\
  -L, --log-path=PFAD             Verzeichnispfad für Logdatenträger; Standard: wie Datenpfad\n\
  -B, --lob-base-path=PFAD        Basisverzeichnispfad für externe LOB Daten; Standard: <Datenpfad>/lob\n\
  -E, --extended-volume-path=PFAD Verzeichnispfad für Datenträger erweiterter Informationen; Standard: gleicher Pfad wie Datenbankdatenträger oder der angegebene Pfad in der Kontrolldatei \n\
  -i, --control-file=DATEI        Kopierung der Datenträger in mehrere Orten, die in der Kontrolldatei eingetragen sind; Standard: keine\n\
  -r, --replace                   vorhandene Datenbank ersetzen; Standard: nicht ersetzen\n\
  -d, --delete-source             eine Quelldatenbank löschen; Standard: nicht löschen\n\
      --copy-lob-path             externen Speicherpfad für LOB Daten aus der Quelldatenbank kopieren. Darf mit Option -B nicht angewendet werden; Standard: nicht kopieren\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 Gruppe '%s' existiert nicht\n
41 Benutzer '%s' existiert nicht\n
42 Unbekannter Befehl '%s'\n
43 Ausgabedatei konnte nicht geöffnet werden '%s'\n
44 Vorhandene Datenbank konnte nicht gelöscht werden.\n
45 Creating database with %s size. The total amount of disk space needed is %s.\n
46 Datenbank konnte nicht erstellt werden.\n
47 Problem mit Benutzerdatei '%s'\n
48 The given '%s', %s is out of range. Choose a size between %s and %s.\n
49 Der Wert von %s '%s' ist ungültig.\n
60 \
createdb: Datenbank erstellen.\n\
Anwendung: %s createdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
      --db-volume-size=GRÖSSE           Größe des ersten Informationsdatenträger \n\
                                      Standard: db_volume_size in cubrid.conf\n\
      --db-page-size=GRÖSSE             Datenbank Seitengröße in bytes; Standard: 16K\n\
      --log-volume-size=GRÖSSE          Größe der Logarchiven \n\
                                      Standard: log_volume_size in cubrid.conf\n\
      --log-page-size=GRÖSSE            Größe der Log-Seite in bytes; Standard: Datenbank Seitengröße\n\
      --comment=KOMMENTAR               KOMMENTAR für eine Datenbank; Standard: keinen\n\
  -F, --file-path=PFAD                Verzeichnispfad für Datenbankdatenträger; Standard: aktuelles Verzeichnis\n\
  -L, --log-path=PFAD                 Verzeichnispfad für Logdatenträger; Standard: wie Datenpfad\n\
  -B, --lob-base-path=PFAD            Basisverzeichnispfad für externe LOB Daten; Standard: <Datenpfad>/lob\n\
      --Server-name=HOST              Server Hostname; Standard: aktueller Hostname\n\
  -r, --replace                       vorhandene Datenbank ersetzen; Standard: nicht ersetzen\n\
      --more-volume-file=DATEI         DATEI enthält Spezifikationen für die Erstellung zusätzlicher Datenträger; Standard: keine\n\
      --user-definition-file=DATEI     DATEI enthält Benutzerdefinitionen; Standard: keine\n\
      --csql-initialization-file=DATEI DATEI enthält SQLs für die Initialisierung der Datenbank; Standard: keine\n\
  -o, --output-file=DATEI              Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -v, --verbose                       ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: Eine Datenbank löschen.\n\
Anwendung: %s deletedb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI       redirect Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -d, --delete-backup          Backupdatenträger löschen; Standard: nicht löschen\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 FEHLER: volext-pathname existiert nicht oder ist nicht schreibbar.\n
32 FEHLER: vols-tofrom-paths_file existiert oder ist nicht lesbar.\n
60 \
renamedb: Eine Datenbank umbenennen.\n\
Anwendung: %s renamedb [OPTION] Quelldatenbanknamen Zieldatenbanknamen\n\
\n\
gültige Optionen:\n\
  -E, --extended-volume-path=PFAD Verzeichnispfad für Datenträger erweiterter Informationen; Standard: gleicher Pfad wie Datenbankdatenträger oder der angegebene Pfad in Kontrolldatei \n\
  -i, --control-file=DATEI         Datenträger aus mehreren Orten, die in der Kontrolldatei eingetragen sind, umbenennen; Standard: keine\n\
  -d, --delete-backup             Backupdatenträger löschen; Standard: nicht löschen\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 Ein %s Prozess läuft bereits.\n
12 MASTER SERVER startet. \n
13 MASTER SERVER endet.\n
14 Lokaler Hostname nicht gefunden\n
15 Parameterdatei nicht gefunden\n
16 Masterprozess konnte nicht initialisiert werden \n
17 Server %s fährt runter in %d Minuten
18 Server %s benachrichtigt über Abschaltung.\nDas könnte mehrere Minuten dauern. Bitte warten.
19 Server %s nicht gefunden.
20 Master fährt runter in %d Minuten\n


$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Ausgabedatei '%s' konnte nicht geöffnet werden\n
60 \
optimizedb: Datenbankoptimierung.\n\
Anwendung: %s optimizedb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -n, --class-name=NAME        Statistiken für die Klasse mit dem Namen aktualisieren; Standard: alle Klassen\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Ungültiges Datumformat, gültiges Format ist dd-mm-yyyy:hh:mm:ss oder 'backuptime' Schlüsselwort \n
20 Restoredb abgebrochen, oder ein Fehler ist aufgetreten.\n
60 \
restoredb: Datenbank wiederherstellen.\n\
Anwendung: %s restoredb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -d, --up-to-date=DATUM             Datenbank zum Stand eines gegebenen Datums wiederherstellen. DATUM wird wie folgt angegeben: dd-mm-yyyy:hh:mm:ss oder 'backuptime' Schlüsselwort\n\
      --list                        Liste der Backupdatenträger anzeigen (ohne wiederherstellen) \n\
  -B, --backup-file-path=PFAD       PFAD ist das Verzeichnis der zu wiederherstellenden Datenträger \n\
  -l, --level=LEVEL                 Backup-LEVEL zu wiederherstellen; siehe Backupanwendung; Standard: 0(volles Backup)\n\
  -p, --partial-recovery            partielle Wiederherstellung ausführen, wenn jene Logarchiven fehlen \n\
  -o, --output-file=DATEI            Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -u, --use-database-location-path  Datenbank und Logdatenträger in dem Ort wiederherstellen, wie in der Datenbankortdatei spezifiziert ist\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 Der Datenbanknamen fehlt.\n
20 '%s' Datei hat %d Zeilen nicht.\n
21 unterbrochen ... \n
22 Ladung unterbrochen - aktuelle Transaktion festgeschrieben\n
23 Ladung unterbrochen - aktuelle Transaktion abgebrochen\n
24 Datei '%s' kann nicht geöffnet werden\n
25 \n*** Syntaxprüfung ***\n\n
26 %d Fehler erkannt.\n
27 \n*** Einfügen von Objekten ***\n\n
28 %8d Objekte eingefügt ins gesamt.\n
29 %8d referenzierte Objekte mit Standardinhalt ins gesamt eingefügt.\n
30 \n*** Transaktion wird festgeschrieben ***\n
31 \n*** Datenbank wird geschlossen ***\n
32 Zeile %d:
33 Fehler beim Parsen '%s' wie '%s' Wert für Attribut '%s' von '%s'\n
34 Fehlende Domain für Attribut %s von %s\n
35 Für Attribut %s von %s, Typ %s ist in der Domain des Sets nicht erlaubt \n
36 Für Attribut %s von %s, %s Typ erwartet, ein Set-Konstante wurde angegeben \n
37 Für Attribut %s von %s, %s Typ erwartet, %s angegeben\n
38 Keine Klase mit Namen %s referenziert vom Attribut %s von %s\n
39 Keine Klasse mit Namen %s, Tupel werden ignoriert\n
40 Keine Klasse mit Identifkator %d referenziert vom Attribut %s von %s\n
41 Klasse %s ist in der Lader-Eingangsdatei nicht erlaubt \n
42 Angehalten mit Fehler.\n
43 WARNUNG: vorhandene Instanz wurde aktualisiert!\n
44 WARNUNG: Instanz %d der Klasse %s wird neudefiniert\n
45 Instanz %d der Klasse %s wurde schon definiert\n
46 Instanz %d der Klasse %s wurde schon reserviert\n
47 UNIQUE Verletzung auf Attribut '%s', konvertiert zu NULL\n
48   %d Instanzen\n
49 Klasse %s\n
50 Passwort:
51 \n*** Klassenstatistiken werden aktualisiert ***\n
52 %s bei Zeile %d (Text war '%s')\n
53 Lexik-Fehler 
54 Zeile %d: Syntaxfehler bei \"%s\"
55 fehlt
56 in %s
57 Inkompatible Argumente, \"%s\", und \"%s\"\n
58 %d Instanzen festgeschrieben\n
59 \nNicht optimierte Version von loaddb kann nicht aufgerufen werden.\n
60 Parse-Fehler \"%s\", wie %s Typ.\n
112   %d Instanzen\r
113 Letzte festgeschriebene Zeile: %d
114 Fehler-Kontroll-Datei
115 Eingangsdatei für Fehlerkontrolle während Ladung 
120 \
loaddb: Import von Objekten und Schemas in die Datenbank.\n\
Anwendung: %s loaddb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -u, --user=ID                 Benutzer ID für Datenbankzugang; Standard: "public"\n\
  -p, --password=PASSWORT           Benutzerpasswort; Standard: keinen\n\
      --data-file-check-only    Syntaxprüfung für Daten ohne Ladung; Standard: Ladung nach Prüfung\n\
  -l, --load-only               Daten ohne Syntaxprüfung laden; Standard: Ladung nach Prüfung\n\
      --estimated-size=ANZAHL   abgeschätzte Anzahl von Instanzen; Standard: 5000\n\
  -v, --verbose                 ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n\
  -c, --periodic-commit=ANZAHL   Einfügungsanzahl für periodische Festschreibung; Standard: keine Festschreibung während der Ladung\n\
      --no-oid                  OID nicht anwenden\n\
      --no-statistics           Statistiken während Ladung nicht aktualisieren \n\
  -s, --schema-file=DATEI[:ZEILE] Schema ab ZEILE mit DATEI erstellen \n\
  -i, --index-file=DATEI[:ZEILE]  Index ab ZEILE mit DATEI erstellen \n\
  -d, --data-file=DATEI          DATEI laden \n\
  -t, --table=TABLE             Name der Tabelle, die für das fehlende Klassenheader in der Datei ersetzt wird \n\
      --error-control-file=DATEI DATEI für Fehlerkontrolle während Ladung\n\
      --ignore-class-file=DATEI  Eingangsdatei für Klassenamen, die nicht geladen werden\n
121 \
load_noopt: Veraltet. Vorsicht bei Anwendung.\n\
Anwendung: load_noopt [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -u, --user=ID                 Benutzer ID für Datenbankzugang; Standard: "public"\n\
  -p, --password=PASSWORT           Benutzerpasswort; Standard: keinen\n\
      --data-file-check-only    Syntaxprüfung für Daten ohne Ladung; Standard: Ladung nach Prüfung\n\
  -l, --load-only               Datenladung ohne Syntaxprüfung; Standard: Ladung nach Prüfung \n\
      --estimated-size=NUMBER   geschätzte Anzahl der Instanzen; Standard: 5000\n\
  -v, --verbose                 ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n\
  -c, --periodic-commit=ANZAHL   Einfügungsanzahl für periodische Festschreibung; Standard: keine Festschreibung während der Ladung\n\
      --no-oid                  OID nicht anwenden\n\
  -s, --schema-file=DATEI[:LINE] Schema ab ZEILE mit DATEI erstellen\n\
  -i, --index-file=DATEI[:ZEILE] Index ab ZEILE mit DATEI erstellen\n\
  -d, --data-file=DATEI          Datei laden\n\
      --error-control-file=DATEI DATEI für Fehlerkontrolle während Ladung\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 Cached-Seiten-Anzahl ungültig.\n
42 Cached-Seitengröße ungültig.\n
43 %d Objekte exportiert.\n
44 Klasse %s
45 , %d Instanzen.\n
46 Nur %d von %d Objekten erfolgreich exportiert.\n
47 Ausgabeverzeichnis zu lang.\n
48 LSA : %d %d \n
60 \
unloaddb: Export Objekte und Schemas aus der Datenbank.\n\
Anwendung: %s unloaddb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -i, --input-class-file=DATEI Eingangsdatei für Tabellennamen; Standard: Alle Klassen exportieren\n\
      --include-reference     referenzierte Tabellen einfügen; mit -i Option; Standard: deaktiviert\n\
      --input-class-only      nur angegebene Klasse einfügen; mit -i Option; Standard: deaktiviert\n\
      --lo-count=ANZAHL        lo Datei ANZAHL per Verzeichnis; Standard: 0\n\
      --estimated-size=ANZAHL abgeschätzte ANZAHL von Instanzen; Standard: autoberechnet\n\
      --cached-pages= ANZAHL  ANZAHL der cached Seiten; Standard: 100\n\
  -O, --output-path=PFAD      PFAD des Ausgangsverzeichnis; Standard: Arbeitsverzeichnis\n\
  -s, --schema-only           nur Schema exportieren; Standard: Schema und die Objekte\n\
  -d, --data-only             nur Objekte exportieren; Standard: Schema und die Objekte\n\
      --output-prefix=PRÄFIX PRÄFIX für die Ausgangdateien; Standard: Datenbanknamen\n\
      --hash-file=DATEI        Datei für Hash; Standard: erzeugt bei tmpnam()\n\
  -v, --verbose               ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n\
      --use-delimiter         '"' verwenden, wo ein Identifikator anfängt und endet; Standard: nicht verwenden\n\
  -S, --SA-mode               Stand-Alone-Ausführung\n\
  -C, --CS-mode               Client-Server-Ausführung\n\
      --datafile-per-class    eine Objektdatei für jede Klasse erzeugen; Standard: inaktiv\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \n1. Durchlauf\n\n
12 %d Objekte verarbeitet.\n
13 \n2. Durchlauf\n\n
14 Klasse %s\n
15 oid:  %d|%d|%d,\n
16 %d Instanzen.\n
17 Das Objekt wird aktualisiert.\n
18 ref_oid:  %d.%d.%d, ref_class_oid:  %d.%d.%d\n
19 Das Objekt konnte nicht umwandelt werden \n
20 Klassen-Heapdatei fehlt\n
21 Das Objekt konnte nicht aktualisiert werden\n
22 Nur %d von %d Objekten erfolgreich verarbeitet.\n
23 COMPACTDB bereits begonnen.\n
24 Die Anzahl der Seiten muss zwischen %d und %d sein.\n
25 Das Instanz-Lock-Timeout muss zwischen %d und %d sein.\n
26 Totale Anzahl Objekten: %d\n
27 Fehlgeschlagene Objekte: %d\n
28 Geänderte Objekte: %d\n
29 Größe Objekte: %d\n
30 Alte Klassenrepräsentierungen wurden aus dem Katalog gelöscht.\n
31 Alte Klassenrepräsentierungen wurden im Katalog behalten.\n
32 Isolationslevel konnte nicht gesetzt werden.\n
33 Die Datenbank konnte nicht komprimiert werden.\n
34 Klassen-Lock-Timeout muss zwischen %d und %d sein.\n
35 gelockte Klasse... Unvollständige Verarbeitung...\n
36 Unverarbeitete Klasse...\n
37 Verarbeitungsfehler... Unvollständige Verarbeitung...\n
38 Nichts zum Verarbeiten...\n
39 Ungültige Parameter...\n
40 Unbekannter Klassenname...\n
41 Die unbenuzte Klassen-OIDs und Seiten wurden zurückgefordert.\n
42 Zurückforderung der OIDs und Seiten ausgelassen.\n
43 Fehler bei Klassenverarbeitung, Klasse ausgelassen.\n
44 \n3. Durchlauf\n\n
45 Heap Komprimierung fehlgeschlagen...\n
46 Heap Komprimierung erfolgreich...\n
60 \
compactdb: Unbenutzten Speicher in einer Datenbank befreien.\n\
Anwendung: %s compactdb [OPTION] Datenbanknamen [Klassenname1 Klassenname 2 ...]\n\
\n\
gültige Optionen:\n\
  -v, --verbose                     ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n\
  -i, --input-class-file=DATEI      Eingabedatei mit Tabellennamen \n\
                                    Klassenname Liste muss leer sein \n\
                                    Standard: alle Klassen komprimieren\n\
                                    Nur in CS-Modus aktiviert\n\
  -p, --pages-commited-once=ANZAHL  maximale Anzahl der Seiten, die einmal festgeschrieben werden dürfen \n\
                                    Standard: 10\n\
                                    Nur in CS-Modus aktiviert\n\
  -S, --SA-mode                     Stand-Alone-Ausführung\n\
  -C, --CS-mode                     Client-Server-Ausführung\n\
  -d, --delete-old-repr             alte Klasserepräsentierungen aus dem Katalog löschen; Standard: inaktiv\n\
  class_name_list                   Nur in CS-Modus aktiviert\n\
  -I, --Instance-lock-timeout       Timeout für Instanz-Lock\n\
  -c, --class-lock-timeout          Timeout für Klassen-Lock \n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 Der CUBRID Master auf Host %s \nwurde am %s gestartet
22 hat %d Requests gewartet. %d Server(s) läuft.\n
23 Abschalten in %s Minuten\n
24 %s
25 Name des Servers zum Abschalten:
26 Wie viele Minuten bis zum Abschalten? :
27 \
cub_commdb: Dieses Dienstprogramm ist veraltet. 'cubrid' Befehl anstatt verwenden.\n\
Anwendung: cub_commdb [OPTION]\n\
\n\
gültige Optionen:\n\
  -P, --Server-list                alle Serverprozesse drucken\n\
  -O, --all-list                   alle Prozesse drucken\n\
  -S, --shutdown-Server=NAME       Abschalten des Datenbankservers NAME\n\
  -A, --shutdown-all               alle Prozesse abschalten\n\
  -c, --Server-mode=NAME           HA-Modus des Datenbankservers NAME anzeigen \n\
  -N, --node-list                  Knoten anzeigen, die von dem CUBRID Herzschlag verwaltet sind\n\
  -L, --process-list               lokale Prozesse anzeigen, die von dem CUBRID Herzschlag verwaltet sind\n\
  -D, --dereg-process=PID          Zielprozess verlassen und aus den verwalteten Prozessen entfernen \n\
  -F, --reconfig-node-list         CUBRID neulich aktualisierte Herzschlagkonfiguration übernehmen\n\
  -U, --deactivate-heartbeat       CUBRID Herzschlag deaktivieren und das aktuelle Knoten aus der Knotengruppe entfernen \n\
  -T, --activate-heartbeat         CUBRID Herzschlag aktivieren und das aktuelle Knoten wird Mitglied der Knotengruppe \n\
  -V, --verbose                    ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n
28 Unbekannter Befehl: %s\n
29 Konnte den Hostnamen vom lokalen Host nicht erhalten - abbgebrochen\n
30 Parameterdatei konnte nicht gefunden werden\n
31 Verbindung zum Master-Server auf %s nicht erfolgreich\n
32 Wie viele Minuten zum Abschalten? :
33 Master wird in %d Minuten abschalten\n
34 Fehler beim Datenempfang vom Client
35 der Datenbankserver %s läuft derzeit nicht
36 Die Shell (%s) für %s, %s kann nicht ausgeführt werden
37 Prozessabspaltung %s, %d, %s zurückgegeben 
38 %s kann nicht ausgeführt werden


$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: Eine Datenbank im Notfall patchen.\n\
Anwendung: %s emergency_patchlog [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -r, --recreate-log           Logdateien wiederherstellen; sorgfältig anwenden\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Ungültiger Wert %d für npages\n
21 Ungültiger Wert '%s' für Datenträger.\n
22 WARNING: --max-writesize-in-sec option is ignored in stand-alone mode execution.\n
60 \
addvoldb: Datenträger für die Datenbank hinzufügen.\n\
Anwendung: %s addvoldb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
      --db-volume-size=GRÖSSE      Größe des zusätzlichen Datenträger; Standard: db_volume_size in cubrid.conf\n\
      --max-writesize-in-sec=SIZE  the amount of volume written per second; (ex. 512K, 1M, 10M); default: not used; minimum: 160K\n\
  -n, --volume-name=NAME           NAME des Datenträgers; Standard: erzeugt wie "db"_ext1)\n\
  -F, --file-path=PFAD             PFAD des neuen Datenträgers; Standard: Arbeitsverzeichnis\n\
      --comment=KOMMENTAR          KOMMENTAR fürs neue Datenträger; Standard: keinen\n\
  -p, --purpose=ZWECK              ZWECK des neuen Datenträgers; erlaubte Werte:\n\
                                           DATA - nur für Daten\n\
                                           INDEX - nur für Indices\n\
                                           TEMP - nur für temporäre Daten\n\
                                           GENERIC - für alle Zwecke\n\
  -S, --SA-mode                    Stand-Alone-Ausführung\n\
  -C, --CS-mode                    Client-Server-Ausführung\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Einige Widersprüche wurden in der Datenbank festgestellt.\n Bitte error_log_file %s für zusätzliche Informationen einsehen.\n
21 %s: Klasse konnte nicht gefunden werden. Es wird weggelassen werden.\n
60 \
checkdb: Konsistenz der Datenbank prüfen.\n\
Anwendung: %s checkdb [OPTION] Datenbanknamen [Klassenname1 Klassenname 2 ...]\n\
\n\
gültige Optionen:\n\
  -S, --SA-mode                Stand-Alone-Ausführung\n\
  -C, --CS-mode                Client-Server-Ausführung\n\
  -r, --repair                 Inkonsistenzen reparieren, wenn gefunden\n\
  -i, --input-class-file=DATEI  Eingangsdatei mit Klassennamen \n\
                               Standard: alle Klassen prüfen\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 Speicherbeschreibung für Datenbank '%s' mit Seitengröße %s. (Log-Seitengröße: %s)\n\n
16 %5d %8s  %s %s  %s\n
17 Ausgangdatei konnte nicht geöffnet werden '%s'\n
18 Speicherbeschreibung für temporäre Datenträger der Datenbank '%s' mit Seitengröße %s.\n\n
19 Volid  Zweck  total_pages  free_pages  Vol Name\n\n
20 Volid  Zweck   total_size   free_size  Vol Name\n\n
21 LOB Speicherbeschreibung %s \n\n
30 Zusammengefasste Speicherbeschreibung für Datenbank '%s' mit Seitengröße %s. (Log-Seitengröße: %s)\n\n
31 Zweck    total_pages  used_pages  free_pages  volume_count\n
32 Zweck     total_size   used_size   free_size  volume_count\n
33 %10s %s %s %s %10d\n
60 \
spacedb: Belegter/freier Speicher der Datenbank anzeigen.\n\
Anwendung: %s spacedb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI       Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -S, --SA-mode                Stand-Alone-Ausführung\n\
  -C, --CS-mode                Client-Server-Ausführung\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               Anzeigeeinheit setzen (Seite, mega, giga, terra, Mensch-lesbar); Standard: H\n\
  -s, --summarize              nur eine Zusammenfassung für jedes Datenträgerzweck anzeigen\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Geschätzte Anzahl der Seiten = %d\n
60 \
estimate_data: Geschätzte Datengröße.\n\
Anwendung: %s estimate_data Anzahl-der-Instanzen durschnittliche-Instanzgröße Anzahl-der-Attribute Variable-anzahl-der-Attribute \n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Ungültige Indexschlüsseltyp angegeben = %s\n
16 Ungültige durchschnittliche Schlüssellänge angegeben = %d wenn Präzision = %d\n
17 Ungültige Argumente angegeben \n
20 Geschätzte Anzahl aller Indexseiten = %d\n (Wenn Index erstellt wird, nachdem die Instanzen eingepflegt worden sind.)\n
21 Geschätzte Anzahl aller Indexseiten = %d\n (Wenn Index erstellt wird, bevor die Instanzen eingepflegt worden sind. (durchschnittlicher en Fall))\n
22 Geschätzte Anzahl aller Indexseiten = %d\n (Wenn Index erstellt wird, bevor die Instanzen eingepflegt worden sind. (schlechter Fall))\n
23 Für das angegebene Input...\n
24 Anzahl aller Instanzen: %d\n
25 Anzahl unterschiedlicher Schlüssel: %d\n
26 Durchschnittliche Schlüsselgröße         : %d\n
27 Schlüssel-Typ                 : %s\n\n
60 \
estimate_index: Geschätzte Größe eines Index.\n\
Anwendung: %s estimate_index Anzahl-der-Instanzen Anzahl-unterschiedlicher-Schlüssel durchschnittliche-Schlüsselgröße Schlüssel-Typ \n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Datenbank installieren.\n\
Anwendung: %s installdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
      --Server-name=HOST       Server HOST Name; Standard: aktueller Hostname\n\
  -F, --file-path=PFAD         Verzeichnispfad für die Datenbank Datenträger; Standard: aktueller Verzeichnis\n\
  -L, --log-path=PFAD          Verzeichnispfad für Log-Datenträger; Standard: wie Datenpfad\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 OID temporärer Klasse\n
2 elo-Werte konnten nicht angezeigt werden\n
3 Fehler beim Zugriff auf langem Objekt für die Migration\n
4 Fehler beim Öffnen der Ausgabedatei für die Migration des langen Objekten \n
5 Lesefehler\n
6 Schreibfehler\n
7 Fehler beim Öffnen der ELO Eingangsdatei "%s"\n
8 KLASSEN:
9 Hash-Dateiname
10 Name:                         %s\n
11 Größe:                         %d\n
12 Seitengröße:                    %d\n
13 Datengröße:                    %d\n
14 Eintragsgröße:                   %d\n
15 Einträge per Seite:             %d\n
16 Seiten gecached:                 %d\n
17 Anzahl der Einträge:            %d\n
18 Anzahl der Kollisionen:         %d\n
19 Hash Dateiname:                %s\n
20 Nächster überlaufene Eintrag:          %d\n
21 Schlüsseltyp:                     %s\n
22 Seitenheaderadresse:      %x\n
23 Adresse des letzten Seitenheader:  %x\n
24 Adresse des freien Seitenheader:  %x\n
25 Adresse der Seitenbitmap:       %x\n
26 Seitenbitmap Größe:             %d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Database exportieren.\n\
Anwendung: %s diagdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -d, --dump-type=TYP         Export TYP \n\
                               -1 alles exportieren\n\
                                1 Dateitabellen exportieren \n\
                                2 Dateikapazitäten exportieren\n\
                                3 Heapkapazitäten exportieren\n\
                                4 Indexkapazitäten exportieren \n\
                                5 Klassennamen exportieren\n\
                                6 Disk-Bitmaps exportieren\n\
                                7 Katalog exportieren \n\
                                8 Log exportieren \n\
                                9 Heap exportieren \n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 Ausgangsdatei '%s' konnte nicht geöffnet werden \n
59 lockdb kann in Stan-Alone-Modus nicht ausgeführt werden.\n
60 \
lockdb: Lockstatus der Datenbank anzeigen.\n\
Anwendung: %s lockdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI       Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Nur ein Argument unter: tran-index, Benutzer, Host, Program darf angegeben werden\n
21 DBA-Passwort eingeben:
26 Es gibt keine Transaktion mit den angegebenen Argumentwerte.\n
27 Folgende Transaktionen abbrechen:\n\n
28 Möchten Sie fortfahren? (Y/N)
29 Transaktion mit Index %d wird abgebrochen\n
30 Abbrechen der folgenden Transaktion fehlgeschlagen\n\n
31 Die Transaktion ist noch nicht abgeschlossen, aber es könnte ein Rollback ausführen.\n Bitte warten Sie einen Augenblick, versuchen Sie bei Bedarf später.\n
32 Invalid tranindex(%s).\n
59 killtran darf nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
killtran: Transaktionen der Datenbank verwalten.\n\
Anwendung: %s killtran [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -i, --kill-transaction-index=INDEX Transaktion mit Index INDEX abbrechen\n\
      --kill-user-name=ID            alle Transaktionen mit Benutzer ID abbrechen\n\
      --kill-host-name=HOST          alle Transaktionen mit Client HOST abbrechen\n\
      --kill-program-name=NAME       alle Transaktionen mit Client-Programm NAME abbrechen\n\
      --kill-sql-id=SQL_ID           kill all transactions with SQL_ID\n\
  -p, --dba-password=PASSWORT        Passwort des DBA-Benutzers; es wird angefordert, wenn nicht angegeben\n\
  -d, --display-information          Information über aktive Transaktionen anzeigen \n\
  -q, --query-exec-info              display information about query execution information of active transactions\n\
  -f, --force                        Transaktion ohne Bestätigung abbrechen \n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 Datenbanknamen
2 erforderlicher Argument, keinen Standardwert
3 Kompatibilitätsprüfung
4 nur Kompatibilitätsprüfung 
5 Backup-Ziel
6 ein Verzeichnispfad oder ein Gerät als Ziel des Backupdatenträgers
20 Datenbank '%s' existiert nicht.
21 '%s' kann zu %s nicht migriert werden, weil die DB-Version zu niedrig ist.\n
22 '%s' brauchen keine Migration.\n
23 '%s' muss migriert werden.\n
24 Ein Backup wird nach der Migration empfohlen. Wollen Sie ein Backup Ihrer Datenbank jetzt machen? (y/n):
25 Wir empfehlen, Ihre neue Datenbank nach Migration mit dem backupdb Befehl zu sichern.
26 \n*** WARNUNG: Sichern Sie Ihre Datenbank, bevor die Migration zu %s angefangen wird ***\n\nUm Fehler zu erholen, die bei der Migration auftreten können, empfehlen wir dringend, ein Backup der Datenbank zu machen.\nHaben Sie bereits Ihre Datenbank gesichert? (y/n):
27 \n1. backupdb ausführen\n2. Geben Sie den neuen Backuppfad für die neue Datenbank. (aktueller Pfad: %s)\n3. Exit \n
28 \nDer neue Backuppfad für die Datenbank:
29 \nMigration und Backup erfolgreich abgeschlossen.
30 Backup fehlgeschlagen. Bitte wiederholen Sie die Backupprozedere.
31 \n\n*** WARNUNG: Sobald die Migration abgeschlossen ist, können Sie die Datenbank nicht mehr mit den bisherigen Backupdatenträger wiederherstellen.\nBeachten Sie, dass die bisherigen Version der Backupdateien nur auf der entsprechenden Version von restoredb wiederhergestellt werden können.\nBitte überprüfen Sie, dass die bisherigen Backupdatenträger sicher aufbewahrt werden.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Host der Datenbank ändern.\n\
Anwendung: %s alterdbhost [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -h, --host=HOST              neuer HOST Name; Standard: aktueller Hostname\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 Datenbanknamen
2 erforderlicher Argument, kein Standardwert
3 Java-Klassendatei
4 erforderlicher Argument, kein Standardwert
5 force-overwrite
6 Klassendatei, wenn vorhanden, überschreiben, Standard nein

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Ausgangdatei '%s' kann nicht geöffnet werden\n
59 plandump kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
plandump: Cached Abfragepläne anzeigen oder kontrollieren.\n\
Anwendung: %s plandump [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -d, --drop                   alle Pläne ins Server-Cache exportieren \n\
  -o, --output-file=DATEI       Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Ausgangdatei '%s' kann nicht geöffnet werden\n
21 Systemparameter, die vom Client-Programm angewendet werden.\n
22 Systemparameter vom Server '%s' geladen\n
23 Systemparameter, die in Standalone-Modus angewendet werden.\n
60 \
paramdump: Systemparameter-Einstellungen exportieren.\n\
Anwendung: %s paramdump [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI       Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -b, --both                   sowohl Client- als auch Serverparameter exportieren \n\
  -S, --SA-mode                Stand-Alone-Ausführung\n\
  -C, --CS-mode                Client-Server-Ausführung\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Ungültiger Wert \"%s\" für das HA-Ausführung-Modus des Servers.\n
12 Ausführung-Modus des Servers kann nicht geändert werden.\n
21 DBA-Passwort eingeben:
22 Das '%s' des Servers aktuellen HA-Ausführung-Modus ist %s.\n
23 Das '%s' des Servers HA-Ausführung-Modus ist auf \"%s\" geändert worden.\n
24 Der Server wurde für HA nicht konfiguriert.\n
58 HA-Funktion wird auf dieser Plattform nicht unterstützt.\n
59 changemode kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
changemode: Das HA-Ausführung-Modus des Servers anzeigen oder ändern.\n\
Anwendung: %s changemode [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -m, --mode=MODUS             Das Modus des Servers zu MODUS ändern; Standard: kein\n\
  -f, --force                 Änderung zwingen\n\
  -t, --timeout=S             es wird S Sekunden gewartet, bis die aktive Transaktion normal beendet wird\n\
                              -t Option ist nur dann wirksam, wenn der Benutzer das Server-Modus auf Wartung ändert\n\
                              Standard: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Ungültiger Wert \"%s\" für das copylogdb's Ausführung-Modus.\n
21 DBA-Passwort eingeben:
22 Der Server wurde für HA nicht konfiguriert.\n
58 HA-Funktion wird auf dieser Plattform nicht unterstützt.\n
59 copylogdb kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
copylogdb: Logseiten vom Server kopieren.\n\
Anwendung: %s copylogdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -L, --log-path=PFAD         Verzeichnispfad für kopierten Logdatenträger\n\
  -m, --mode=MODE             Operationsmodus; Standard: sync\n\
                              MODUS: sync oder semisync oder async\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 DBA-Passwort eingeben:
22 Der Server wurde für HA nicht konfiguriert.\n
58 HA-Funktion wird auf dieser Plattform nicht unterstützt.\n
59 applylogdb kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
applylogdb: Das kopierte Log auf dem Server übernehmen.\n\
Anwendung: %s applylogdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -L, --log-path=PFAD         Verzeichnispfad der kopierten Logdatenträger\n\
      --max-mem-size=GRÖSSE     maximale Prozessgröße GRÖSSE in MB; max. 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Ausgangdatei kann nicht geöffnet werden '%s'\n
59 statdump kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
statdump: Globale Statistiken exportieren.\n\
Anwendung: %s statdump [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI       Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -c, --cumulative             kumulative Statistiken anzeigen \n\
  -i, --interval=S             Statistiken jede S Sekunden anzeigen\n\
  -s, --substr=STRING          nur die Statistiken anzeigen, die zum String anpassen\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 DBA-Passwort eingeben:
22 Der Server wurde für HA nicht konfiguriert.\n
58 HA-Funktion wird auf dieser Plattform nicht unterstützt.\n
59 applyinfo kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
applyinfo: CUBRID HA-Übernehmen Information anzeigen.\n\
Anwendung: %s applyinfo [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -r, --remote-host-name      Remote-Hostnamen; aktiven Log-Information des Remoteknoten anzeigen\n\
  -a, --applied-info 	      Übernommene Information anzeigen \n\
  -L, --copied-log-path=PFAD  Pfad der kopierten Logdatenträger; kopierte Loginformation anzeigen \n\
  -p, --pageid=ID             Seiten-ID; Standard: 0(aktive Seite)\n\
  -v, --verbose               ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
acldb: Zugangskontrolleinstellungen der Datenbank anzeigen oder neu laden.\n\
Anwendung: %s acldb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -r, --reload       IP-Kontrolldatei neu laden \n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Generiert die lokale gemeinsame Bibliothek anhand der LDML Eingangsdatei(en).\n\
Anwendung: %s genlocale [OPTION] [Sprachenstring]\n\
\n\
language-string	      lokaler Sprachenstring in der die Bibliothek generiert werden soll.\n\
		      Wenn keinen Sprachenstring angegeben wird, alle Sprachen von 'cubrid_locales.txt'\n\
		      werden bearbeitet.\n\
gültige Optionen:\n\
  -v, --verbose	              ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n\
  -i, --input-ldml-file       Pfad der LDML Eingangsdatei. Nur wenn der Sprachenstring angegeben wird.\n
61 Ungültiges lokalen Namen Argument.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 -i/--input-file und lokale Identifikatoren können nicht zugleich angewendet werden.\n
58 Ungültiger Codepoint-Bereich (-s -e).\n
59 Ungültiges lokalen Name Argument.\n
60 \
dumplocale: Exportiert den Inhalt der lokalen gemeinsamen Bibliothek, die vorher anhand der LDML Eingangsdatei(en) erzeugt wurde.\n\
Anwendung: %s dumplocale [OPTION] [Sprachenstring]\n\
\n\
language-string	 lokale Sprache in der die lokale gemeinsame Bibliothek exportiert werden soll.\n\
		         Wenn keinen Sprachenstring angegeben wird, alle Sprachen von 'cubrid_locales.txt'\n\
		         werden bearbeitet.\n\
gültige Optionen:\n\
  -i, --input-file    Pfad der lokalen gemeinsamen Bibliothek (vorher erzeugt).\n\
                      Kann nur angewendet werden, wenn der Sprachenstring nicht angegeben wird.\n\
  -d, --calendar      Kalender und Datum-/Zeitinformationen exportieren; Standard: inaktiv\n\
  -n, --numeric		  numerische Informationen exportieren; Standard: inaktiv\n\
  -a, --alphabet=l|lower|u|upper|both	     Alphabet und Groß- und Kleinschreibung Information exportieren; Standard: inaktiv\n\
      --identifier-alphabet=l|lower|u|upper	 Alphabet und Groß- und Kleinschreibung Information für Identifikatoren exportieren; Standard: inaktiv\n\
  -c, --codepoint-order	 Kollation-Information sortiert nach Codepoint-Werte exportieren\n\
                         (cp, char, weight, next-cp, char und weight);\n\
                         Standard: inaktiv\n\
  -w, --weight-order  Kollation-Information sortiert nach Gewichtwerte exportieren \n\
                      (weight, cp, char); Standard: inaktiv\n\
  -s, --start-value	  Filteroption; Anfangscodepoint für\n\
                      -a, --identifier-alphabet, -c, -w; Standard: 0\n\
  -e, --end-value     Filteroption; Endcodepoint für \n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      Standard: der maximale Wert aus der lokalen gemeinsamen Bibliothek gelesen \n\
  -k, --console-conversion  Konsole-Konvertierungsdaten exportieren; Standard: inaktiv\n\
  -z, --normalization   Normalisierungsdaten exportieren; Standard: inaktiv\n

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
22 Tran index          Benutzername      Hostname      Prozes-ID      Programname\n
23 -------------------------------------------------------------------------------\n
24 %4d%-10s %14s %14s %15d %17s\n
25 Es gibt keine Transaktionen\n
26 tranlist can only be performed by the DBA or a DBA group member.\n
32 Tran index         Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %4d%-10s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index          User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %4d%-10s %14s %14s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %d
46 SQL_ID: %s\n
59 tranlist darf nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
tranlist: Dump active transaction information.\n\
usage: %s tranlist [OPTION] database-name\n\
\n\
valid options:\n\
  -u, --user=ID                user ID for database access; default: DBA user\n\
  -p, --password=PASS          user password; default: none\n\
  -s, --summary                print summarized information of transaction\n
  