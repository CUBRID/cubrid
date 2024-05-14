
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
1 Ungültige Länge oder Zeichen '%1$c' in Datenbank '%2$s'\n
2 Ausgabedatei konnte nicht geöffnet werden '%1$s'\n
3 Fehlender oder ungültiger Utilityname.\n
4 Ungültiger Utilityname '%1$s'\n
5 Die folgende Liste enthält die erkannte Utilitynamen:\n
6 Ungültiges Zeichen '%1$c' in Datenträger '%2$s'\n
9 \
%1$s (CUBRID Utility)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation.\nCopyright (C) 2016 CUBRID Corporation.\n
10 \
Cubrid Administratorutility, Version %1$s\n\
Anwendung: %2$s <utility-name> [args]\n\
'%3$s <utility-name>' für Hilfe auf einem bestimmten Utility eintippen.\n\
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
    gen_tz\n\
    dump_tz\n\
    restoreslave\n\
    vacuumdb\n\
    tde\n\
    flashback\n\
\n\
%4$s ist ein Werkzeug für DBMS.\n\
Für zusätzliche Informationen, http://www.cubrid.org besuchen\n
12 '%1$s' Utilityname ist ungültig.\n'cubrid --help' für Anwendungshilfe eintippen.\n
13 '%1$s' Befehl ist ungültig.\nType 'cubrid --help' für Anwendungshilfe eintippen.\n
14 Eine oder mehrere Pflichteigenschaften konnten in den Konfigurationsdateien nicht geladen werden.\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' läuft nicht.\n
18 ++ %1$s läuft nicht.\n
19 ++ %1$s '%2$s' läuft.\n
20 ++ %1$s läuft.\n
21 ++ %1$s %2$s: %3$s\n
22 Eins oder mehrere Argumente fehlen.\n'cubrid --help' für Anwendungshilfe eintippen.\n
23 Cubrid Utility, Version %1$s\n\
Anwendung: %2$s <utility-name> [args]\n\
'%3$s <utility-name>' für Hilfe auf einem bestimmten Utility eintippen.\n\
\n\
Vorhandene Serviceutilities:\n\
    service\n\
    server\n\
    broker\n\
    gateway\n\
    manager\n\
    heartbeat\n\
    javasp\n\
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
%4$s ist ein Werkzeug für DBMS.\n\
Für zusätzliche Informationen, http://www.cubrid.org, besuchen\n
24 Cubrid Utility, Version %1$s\n\
Anwendung: %2$s service <Befehl>\n\
\n\
Vorhandene Befehle:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 Cubrid Utility, Version %1$s\n\
Anwendung: %2$s Server <Befehl> [args]\n\
\n\
Vorhandene Befehle:\n\
    start       [Datenbanknamen]\n\
    stop        [Datenbanknamen]\n\
    restart     [Datenbanknamen]\n\
    status\n\
    acl         <status|reload> Datenbanknamen\n
37 Der Server wurde für HA konfiguriert.\n
26 Cubrid Utility, Version %1$s\n\
Anwendung: %2$s broker <Befehl> [args]\n\
\n\
Vorhandene Befehle:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [-u] [<expr>]\n\
    on          Brokername\n\
    off         Brokername\n\
    reset       Brokername\n\
    acl         <status|reload> [Brokername]\n\
    getid       -b Brokername [-f] shard-key\n\
    test        Brokername [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 Cubrid Utility, Version %1$s\n\
Anwendung: %2$s manager <Befehl>\n\
\n\
Vorhandene Befehle:\n\
    start\n\
    stop\n\
    status\n
28 Cubrid Utility, Version %1$s\n\
Anwendung: %2$s heartbeat <Befehl> [args]\n\
\n\
Vorhandene Befehle:\n\
    start          [database-name]\n\
    stop           [-h <host-name>] [-i] [database-name]\n\
    copylogdb      <start|stop> [-h <host-name>] database-name node-name\n\
    applylogdb     <start|stop> [-h <host-name>] database-name node-name\n\
    replication    <start|stop> node-name\n\
    status         [-v] [-h <host-name>]\n\
    reload\n
31 '%1$s' Argument ist nicht nötig.\n
32 Ein Datenbanknamen fehlt.\n
33 %1$s: Diese Option ist veraltet.\n
34 Ungültiges Parameter. %1$s %2$s\n
35 Speicher kann nicht zugewiesen werden.\n
36 Der Server wurde für HA nicht konfiguriert.\n
38 Der für HA konfigurierte Server ist nicht in der ha_db_list enthalten.\n
39 Der für HA konfigurierte Server ist nicht in der ha_node_list enthalten.\n
40 Befehl ist ungültig.\n
41 cubrid manager server nicht installiert ist.\n
42 Parameter ist Ungültig.\n
43 Datei(%1$s) offene Fehler.\n
44 Cubrid Utility, Version %1$s\n\
Anwendung: %2$s javasp <Befehl> [args]\n\
\n\
Vorhandene Befehle:\n\
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
46 Die Länge von owner_name muss kleiner als %1$d sein und die Länge von class_name muss kleiner als %2$d Bytes sein.\n
47 Ungültiges Format für Besitzername.Klassenname ('%1$s').\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 WARNUNG: -t Option wird in Stand-Alone-Ausführung ignoriert.\n
31 FEHLER: Destination-Pfad existiert nicht oder ist kein Verzeichnis.\n
32 Mit der Option --separate-keys wird die Schlüsseldatei (_keys) vom Sicherungsdatenträger getrennt.\n\
Wenn die Schlüsseldatei verloren geht, funktioniert die Wiederherstellung (restoredb) möglicherweise nicht ordnungsgemäß, selbst wenn das Sicherungsvolume vorhanden ist.\n
33 Das Sicherungsvolume enthält die Schlüsseldatei (_keys).\n\
Wenn eine Tabelle TDE-verschlüsselt ist, kann dies zu einem Sicherheitsproblem für diese Daten führen.\n\
Verwenden Sie die Option --separate-keys, um die Schlüsseldatei zu trennen, um dies zu verhindern.\n
34 FEHLER: FIFO-Datei kann nicht als Zielpfad mit separatem Schlüssel verwendet werden.\n
60 \
backupdb: Datenbank sichern.\n\
Anwendung: %1$s backupdb [OPTION] Datenbanknamen\n\
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
  -t, --thread-count=ANZAHL    Anzahl der Threads; Standard: 0(auto)\n\
      --no-compress            komprimieren Sie keine Backup-Volumes; Standard: Komprimierung\n\
      --sleep-msecs=N          N Millisekunden per gelesenen 1M schlafen; Standard: 0\n\
  -k, --separate-keys          Schlüsseldatei (_keys) für TDE ist nicht im Sicherungsvolume enthalten, sondern wird stattdessen in eine Datei aufgeteilt\n
71 Nicht kompatible Argumente, \"%1$s\", und \"%2$s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 FEHLER: Zielnamen ist gleich mit Quellname.\n
31 FEHLER: volext-Pfad existiert nicht oder ist nicht schreibbar.\n
32 FEHLER: vols-tofrom-paths_file existiert nicht oder ist nicht lesbar.\n
60 \
copydb: Datenbank kopieren.\n\
Anwendung: %1$s copydb [OPTION] Quelldatenbanknamen Zieldatenbanknamen\n\
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
40 Gruppe '%1$s' existiert nicht\n
41 Benutzer '%1$s' existiert nicht\n
42 Unbekannter Befehl '%1$s'\n
43 Ausgabedatei konnte nicht geöffnet werden '%1$s'\n
44 Vorhandene Datenbank konnte nicht gelöscht werden.\n
45 Erstellen der Datenbank mit %1$s Größe mit Standard-Locale %2$s. Der Gesamtbetrag der benötigten Speicherplatz ist %3$s.\n
46 Datenbank konnte nicht erstellt werden.\n
47 Problem mit Benutzerdatei '%1$s'\n
48 Der angegebene '%1$s', %2$s ist außerhalb des Bereichs. Eine Größe zwischen %3$s und %4$s wählen.\n
49 Der Wert von %1$s '%2$s' ist ungültig.\n
60 \
createdb: Datenbank erstellen.\n\
usage: %1$s createdb [OPTION] database-name database-locale\n\
\n\
Datenbank Lokale-format ist <language>.<charset>; Beispiele: en_US.iso88591, ko_KR.utf8\n\
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
Anwendung: %1$s deletedb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI       redirect Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -d, --delete-backup          Backupdatenträger löschen; Standard: nicht löschen\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 FEHLER: volext-pathname existiert nicht oder ist nicht schreibbar.\n
32 FEHLER: vols-tofrom-paths_file existiert oder ist nicht lesbar.\n
60 \
renamedb: Eine Datenbank umbenennen.\n\
Anwendung: %1$s renamedb [OPTION] Quelldatenbanknamen Zieldatenbanknamen\n\
\n\
gültige Optionen:\n\
  -E, --extended-volume-path=PFAD Verzeichnispfad für Datenträger erweiterter Informationen; Standard: gleicher Pfad wie Datenbankdatenträger oder der angegebene Pfad in Kontrolldatei \n\
  -i, --control-file=DATEI         Datenträger aus mehreren Orten, die in der Kontrolldatei eingetragen sind, umbenennen; Standard: keine\n\
  -d, --delete-backup             Backupdatenträger löschen; Standard: nicht löschen\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 Ein %1$s Prozess läuft bereits.\n
12 MASTER SERVER startet. \n
13 MASTER SERVER endet.\n
14 Lokaler Hostname nicht gefunden\n
15 Parameterdatei nicht gefunden\n
16 Masterprozess konnte nicht initialisiert werden \n
17 Server %1$s fährt runter in %2$d Minuten
18 Server %1$s benachrichtigt über Abschaltung.\nDas könnte mehrere Minuten dauern. Bitte warten.
19 Server %1$s nicht gefunden.
20 Master fährt runter in %1$d Minuten\n
21 Die Heartbeat-Funktion wurde deaktiviert. Bitte warten Sie, bis alle Shutdown-Prozesse von cub_servers abgeschlossen sind.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Ausgabedatei '%1$s' konnte nicht geöffnet werden\n
60 \
optimizedb: Datenbankoptimierung.\n\
Anwendung: %1$s optimizedb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -n, --class-name=NAME        Statistiken für die Klasse mit dem Namen aktualisieren; Standard: alle Klassen\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Ungültiges Datumformat, gültiges Format ist dd-mm-yyyy:hh:mm:ss oder 'backuptime' Schlüsselwort \n
20 Restoredb abgebrochen, oder ein Fehler ist aufgetreten.\n
60 \
restoredb: Datenbank wiederherstellen.\n\
Anwendung: %1$s restoredb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -d, --up-to-date=DATUM             Datenbank zum Stand eines gegebenen Datums wiederherstellen. DATUM wird wie folgt angegeben: dd-mm-yyyy:hh:mm:ss oder 'backuptime' Schlüsselwort\n\
      --list                        Liste der Backupdatenträger anzeigen (ohne wiederherstellen) \n\
  -B, --backup-file-path=PFAD       PFAD ist das Verzeichnis der zu wiederherstellenden Datenträger \n\
  -l, --level=LEVEL                 Backup-LEVEL zu wiederherstellen; siehe Backupanwendung; Standard: 0(volles Backup)\n\
  -p, --partial-recovery            partielle Wiederherstellung ausführen, wenn jene Logarchiven fehlen \n\
  -o, --output-file=DATEI            Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -u, --use-database-location-path  Datenbank und Logdatenträger in dem Ort wiederherstellen, wie in der Datenbankortdatei spezifiziert ist\n\
  -k, --keys-file-path              Pfad der Schlüsseldatei (_keys) für TDE, die beim Wiederherstellen verwendet werden soll\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 Der Datenbanknamen fehlt.\n
20 '%1$s' Datei hat %2$d Zeilen nicht.\n
21 unterbrochen ... \n
22 Ladung unterbrochen - aktuelle Transaktion festgeschrieben\n
23 Ladung unterbrochen - aktuelle Transaktion abgebrochen\n
24 Datei '%1$s' kann nicht geöffnet werden\n
25 \n*** Syntaxprüfung ***\n\n
26 %1$d Fehler erkannt.\n
27 \n*** Einfügen von Objekten ***\n\n
28 %1$ld Objekte eingefügt ins gesamt.\n
29 %1$8d referenzierte Objekte mit Standardinhalt ins gesamt eingefügt.\n
30 \n*** Transaktion wird festgeschrieben ***\n
31 \n*** Datenbank wird geschlossen ***\n
32 Zeile %1$d:
33 Fehler beim Parsen '%1$s' wie '%2$s' Wert für Attribut '%3$s' von '%4$s'\n
34 Fehlende Domain für Attribut %1$s von %2$s\n
35 Für Attribut %1$s von %2$s, Typ %3$s ist in der Domain des Sets nicht erlaubt \n
36 Für Attribut %1$s von %2$s, %3$s Typ erwartet, ein Set-Konstante wurde angegeben \n
37 Für Attribut %1$s von %2$s, %3$s Typ erwartet, %4$s angegeben\n
38 Keine Klase mit Namen %1$s referenziert vom Attribut %2$s von %3$s\n
39 Keine Klasse mit Namen %1$s, Tupel werden ignoriert\n
40 Keine Klasse mit Identifkator %1$d referenziert vom Attribut %2$s von %3$s\n
41 Klasse %1$s ist in der Lader-Eingangsdatei nicht erlaubt \n
42 Angehalten mit Fehler.\n
43 WARNUNG: vorhandene Instanz wurde aktualisiert!\n
44 WARNUNG: Instanz %1$d der Klasse %2$s wird neudefiniert\n
45 Instanz %1$d der Klasse %2$s wurde schon definiert\n
46 Instanz %1$d der Klasse %2$s wurde schon reserviert\n
47 UNIQUE Verletzung auf Attribut '%1$s', konvertiert zu NULL\n
48 %1$s %2$ld Instanzen\n
49 Klasse %1$s\n
50 Passwort:
51 \n*** Klassenstatistiken werden aktualisiert ***\n
52 %1$s bei Zeile %2$d (Text war '%3$s')\n
53 Lexik-Fehler\n
54 Zeile %1$d: Syntaxfehler bei \"%2$s\"
55 fehlt
56 in %1$s
57 Inkompatible Argumente, \"%1$s\", und \"%2$s\"\n
58 %1$s %2$ld Instanzen festgeschrieben\n
59 \nNicht optimierte Version von loaddb kann nicht aufgerufen werden.\n
60 Parse-Fehler \"%1$s\", wie %2$s Typ.\n
61 Objektreferenz wird im CS-Modus nicht unterstützt.\n
62 Aktualisierte Statistiken für die Klasse %1$s.\n
112   %1$ld Instanzen\r
113 Letzte festgeschriebene Zeile: %1$d\n
114 Fehler-Kontroll-Datei
115 Eingangsdatei für Fehlerkontrolle während Ladung
116 Insgesamt %ld Objekt(e) eingefügt, %2$ld Objekt(e) fehlgeschlagen.\n
117 Laden fehlgeschlagen.\n
118 Maximum length of class name is %1$d bytes.\n
119 Insgesamt %1$8d Objekt(e) überprüft, %2$d Objekt(e) fehlgeschlagen.\n
120 Die Objektdatei enthält keinen '%% class'-Bezeichner, oder die Option' --table 'fehlt\n
121 Klasse %1$s wird ignoriert.\n
122 Maximum length of user name is %1$d bytes.\n
123 %1$s is een leeg bestand.\n
124 \
loaddb: Import von Objekten und Schemas in die Datenbank.\n\
Anwendung: %1$s loaddb [OPTION] Datenbanknamen[@host]\n\
\n\
gültige Optionen:\n\
  -S, --SA-mode                    Stand-Alone-Ausführung\n\
  -C, --CS-mode                    Client-Server-Ausführung\n\
                                   WAARSCHUWING: In de HA-modus kunnen alleen gegevensbestanden (-d, --data-file=FILE) worden geladen in client-server-modus.\n\
  -u, --user=ID                    Benutzer ID für Datenbankzugang; Standard: "public"\n\
  -p, --password=PASSWORT          Benutzerpasswort; Standard: keinen\n\
      --data-file-check-only       Syntaxprüfung der Datei ohne Ladung; Standard: inaktiv\n\
  -l, --load-only                  Dateiladung ohne vorherige Syntaxprüfung; nur im SA_MODE\n\
      --estimated-size=ANZAHL      abgeschätzte Anzahl von Instanzen; Standard: 5000\n\
  -v, --verbose                    ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n\
  -c, --periodic-commit=ANZAHL     Einfügungsanzahl für periodische Festschreibung; Standard: 10240\n\
      --no-oid                     OID nicht anwenden\n\
      --no-statistics              Statistiken während Ladung nicht aktualisieren\n\
  -s, --schema-file=DATEI[:ZEILE]  Schema ab ZEILE mit DATEI erstellen\n\
  -i, --index-file=DATEI[:ZEILE]   Index ab ZEILE mit DATEI erstellen\n\
      --trigger-file=DATEI[:ZEILE] Trigger ab ZEILE mit DATEI erstellen\n\
  -d, --data-file=DATEI            DATEI laden\n\
  -t, --table=TABLE                Name der Tabelle, die für das fehlende Klassenheader in der Datei ersetzt wird\n\
      --error-control-file=DATEI   DATEI für Fehlerkontrolle während Ladung\n\
      --ignore-class-file=DATEI    Eingangsdatei für Klassenamen, die nicht geladen werden\n\
      --no-user-specified-name     Find classes, serials, and triggers by their object names without their owner names.\n\
                                   This is to load files generated by cubrid before version 11.2.\n\
      --schema-file-list=FILE      De inhoud van dit bestand is een lijst met schemabestandsnamen die in loaddb moeten worden gebruikt.\n\
                                   loaddb wordt sequentieel vanaf de bovenkant uitgevoerd.\n\
                                   WAARSCHUWING: kan niet worden gebruikt met de optie -s.\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 Cached-Seiten-Anzahl ungültig.\n
42 Cached-Seitengröße ungültig.\n
43 %1$ld Objekte exportiert.\n
44 Klasse %1$s
45 , %1$d Instanzen.\n
46 Nur %1$ld von %2$ld Objekten erfolgreich exportiert.\n
47 Ausgabeverzeichnis zu lang.\n
48 LSA : %1$d %2$d \n
49 Passwort:
50 Maximum length of class name is %1$d bytes.\n
51 Maximum length of user name is %1$d bytes.\n
52 Class %1$s not found.\n
60 \
unloaddb: Export Objekte und Schemas aus der Datenbank.\n\
Anwendung: %1$s unloaddb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -u, --user=ID                Benutzer ID für Datenbankzugang; Standard: "dba"\n\
  -p, --password=PASSWORT      Benutzerpasswort; Standard: keinen\n\
  -i, --input-class-file=DATEI Eingangsdatei für Tabellennamen; Standard: Alle Klassen exportieren\n\
      --include-reference      referenzierte Tabellen einfügen; mit -i Option; Standard: deaktiviert\n\
      --input-class-only       nur angegebene Klasse einfügen; mit -i Option; Standard: deaktiviert\n\
      --lo-count=ANZAHL        lo Datei ANZAHL per Verzeichnis; Standard: 0\n\
      --estimated-size=ANZAHL  abgeschätzte ANZAHL von Instanzen; Standard: autoberechnet\n\
      --cached-pages= ANZAHL   ANZAHL der cached Seiten; Standard: 100\n\
  -O, --output-path=PFAD       PFAD des Ausgangsverzeichnis; Standard: Arbeitsverzeichnis\n\
  -s, --schema-only            nur Schema exportieren; Standard: Schema und die Objekte\n\
  -d, --data-only              nur Objekte exportieren; Standard: Schema und die Objekte\n\
      --output-prefix=PRÄFIX   PRÄFIX für die Ausgangdateien; Standard: Datenbanknamen\n\
      --hash-file=DATEI        Datei für Hash; Standard: erzeugt bei tmpnam()\n\
      --latest-image           beim Dumpen von Objekten wird das neueste Image von Objekten in Datenvolumes ausgegeben, nicht auf Log-Volumes (ohne MVCC)\n\
                               Es könnte nicht festgeschriebene Daten und gelöschte Daten enthalten, die nicht bereinigt wurden; Standard: deaktiviert\n\
  -v, --verbose                ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n\
      --use-delimiter          '"' verwenden, wo ein Identifikator anfängt und endet; Standard: nicht verwenden\n\
  -S, --SA-mode                Stand-Alone-Ausführung\n\
  -C, --CS-mode                Client-Server-Ausführung\n\
      --datafile-per-class     eine Objektdatei für jede Klasse erzeugen; Standard: inaktiv\n\
      --split-schema-files     Schemainformationen nach Objekt aufteilen und jede Datei generieren; Standard: Eine Schemadatei mit allen Objekten generieren\n\
      --skip-index-detail      Geben Sie die angegebenen WITH-Klauseloptionen beim Erstellen des Index nicht aus. Standard: deaktiviert\n\
      --as-dba                 Als de ingelogde gebruiker lid is van de DBA-groep, pak dan hetzelfde schemabestand uit als de DBA.\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \n1. Durchlauf\n\n
12 %1$d Objekte verarbeitet.\n
13 \n2. Durchlauf\n\n
14 Klasse %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d Instanzen.\n
17 Das Objekt wird aktualisiert.\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 Das Objekt konnte nicht umwandelt werden \n
20 Klassen-Heapdatei fehlt\n
21 Das Objekt konnte nicht aktualisiert werden\n
22 Nur %1$d von %2$d Objekten erfolgreich verarbeitet.\n
23 COMPACTDB bereits begonnen.\n
24 Die Anzahl der Seiten muss zwischen %1$d und %2$d sein.\n
25 Das Instanz-Lock-Timeout muss zwischen %1$d und %2$d sein.\n
26 Totale Anzahl Objekten: %1$d\n
27 Fehlgeschlagene Objekte: %1$d\n
28 Geänderte Objekte: %1$d\n
29 Größe Objekte: %1$d\n
30 Alte Klassenrepräsentierungen wurden aus dem Katalog gelöscht.\n
31 Alte Klassenrepräsentierungen wurden im Katalog behalten.\n
32 Isolationslevel konnte nicht gesetzt werden.\n
33 Die Datenbank konnte nicht komprimiert werden.\n
34 Klassen-Lock-Timeout muss zwischen %1$d und %2$d sein.\n
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
47 Maximum length of class name is %1$d bytes.\n
48 Maximum length of user name is %1$d bytes.\n
60 \
compactdb: Unbenutzten Speicher in einer Datenbank befreien.\n\
Anwendung: %1$s compactdb [OPTION] Datenbanknamen [Klassenname1 Klassenname 2 ...]\n\
\n\
gültige Optionen:\n\
  -v, --verbose                     ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n\
  -i, --input-class-file=DATEI      Eingabedatei mit Tabellennamen \n\
                                    Klassenname Liste muss leer sein \n\
                                    Standard: alle Klassen komprimieren\n\
  -p, --pages-commited-once=ANZAHL  maximale Anzahl der Seiten, die einmal festgeschrieben werden dürfen \n\
                                    Standard: 10\n\
                                    Nur in CS-Modus aktiviert\n\
  -S, --SA-mode                     Stand-Alone-Ausführung\n\
  -C, --CS-mode                     Client-Server-Ausführung\n\
  -d, --delete-old-repr             alte Klasserepräsentierungen aus dem Katalog löschen; Standard: inaktiv\n\
  -I, --Instance-lock-timeout       Timeout für Instanz-Lock\n\
                                    Nur in CS-Modus aktiviert\n\
  -c, --class-lock-timeout          Timeout für Klassen-Lock \n\
                                    Nur in CS-Modus aktiviert\n\
  class_name_list                   Liste der zu komprimierenden Klassennamen\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 Der CUBRID Master auf Host %1$s \nwurde am %2$s gestartet
22 hat %1$d Requests gewartet. %2$d Server(s) läuft.\n
23 Abschalten in %1$s Minuten\n
24 %1$s
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
  -p, --ping-host                  list ping hosts used for CUBRID heartbeat\n\
  -D, --dereg-process=PID          Zielprozess verlassen und aus den verwalteten Prozessen entfernen \n\
  -F, --reconfig-node-list         CUBRID neulich aktualisierte Herzschlagkonfiguration übernehmen\n\
  -V, --verbose                    ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n
28 Unbekannter Befehl: %1$s\n
29 Konnte den Hostnamen vom lokalen Host nicht erhalten - abbgebrochen\n
30 Parameterdatei konnte nicht gefunden werden\n
31 Verbindung zum Master-Server auf %1$s nicht erfolgreich\n
32 Wie viele Minuten zum Abschalten? :
33 Master wird in %1$d Minuten abschalten\n
34 Fehler beim Datenempfang vom Client
35 der Datenbankserver %1$s läuft derzeit nicht
36 Die Shell (%1$s) für %2$s, %3$s kann nicht ausgeführt werden
37 Prozessabspaltung %1$s, %2$d zurückgegeben, %3$s
38 %1$s kann nicht ausgeführt werden
39 -i(--immediately) option muss mit --deact-stop-all verwendet werden.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
1 Bitte ohne die Option --recreate-log ausführen oder das aktive Protokoll-Volume manuell entfernen.\n
60 \
emergency_patchlog: Eine Datenbank im Notfall patchen.\n\
usage: %1$s emergency_patchlog [OPTION] database-name [database-locale]\n\
\n\
gültige Optionen:\n\
  -r, --recreate-log           recreate log files; use carefully (needs database-locale argument)\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Ungültiger Wert %1$d für npages\n
21 Ungültiger Wert '%1$s' für Datenträger.\n
22 WARNING: --max-writesize-in-sec Option wird in Stand-Alone-Modus ignoriert.\n
60 \
addvoldb: Datenträger für die Datenbank hinzufügen.\n\
Anwendung: %1$s addvoldb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
      --db-volume-size=GRÖSSE      Größe des zusätzlichen Datenträger; Standard: db_volume_size in cubrid.conf\n\
      --max-writesize-in-sec=SIZE  Speichergröße pro Sekunde; (ex. 512K, 1M, 10M); Standardwert: nicht benutzt; Minimum: 160K\n\
  -n, --volume-name=NAME           NAME des Datenträgers; Standard: erzeugt wie "db"_ext1)\n\
  -F, --file-path=PFAD             PFAD des neuen Datenträgers; Standard: volume_extension_path\n\
      --comment=KOMMENTAR          KOMMENTAR fürs neue Datenträger; Standard: keinen\n\
  -p, --purpose=ZWECK              ZWECK des neuen Datenträgers; erlaubte Werte:\n\
                                           DATA - nur für Daten\n\
                                           INDEX - nur für Indices\n\
                                           TEMP - nur für temporäre Daten\n\
                                           GENERIC - für alle Zwecke\n\
  -S, --SA-mode                    Stand-Alone-Ausführung\n\
  -C, --CS-mode                    Client-Server-Ausführung\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Einige Widersprüche wurden in der Datenbank festgestellt.\n Bitte error_log_file %1$s für zusätzliche Informationen einsehen.\n
21 %1$s: Klasse konnte nicht gefunden werden. Es wird weggelassen werden.\n
22 %1$s: Der Index kann nicht gefunden werden. Class: %2$s.\n
60 \
checkdb: Konsistenz der Datenbank prüfen.\n\
Anwendung: %1$s checkdb [OPTION] Datenbanknamen [Klassenname1 Klassenname 2 ...]\n\
\n\
gültige Optionen:\n\
  -S, --SA-mode                Stand-Alone-Ausführung\n\
  -C, --CS-mode                Client-Server-Ausführung\n\
  -r, --repair                 Inkonsistenzen reparieren, wenn gefunden\n\
  -i, --input-class-file=DATEI  Eingangsdatei mit Klassennamen \n\
                               Standard: alle Klassen prüfen\n\
  -I, --index-name             Indexnamen, die nur überprüft werden\n\
                               Standard: überprüfen Sie alle Indizes\n\
      --check-prev-link        überprüfen bisher einzigen Links-Indizes\n\
      --repair-prev-link       reparieren vorherigen Links von Indizes\n\
      --check-file-tracker     überprüfen Sie alle zugeordneten Seiten der Datei\n\
      --check-heap             überprüfen Sie alle Seiten des Heap-Dateien\n\
      --check-catalog          Check-Katalog Konsistenz\n\
      --check-btree            überprüfen Sie alle btree Indizes\n\
      --check-class-name       überprüfen Klassennamen Konsistenz\n\
      --check-btree-entries    überprüfen Einträge btree Konsistenz\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
10 Speicherbeschreibung für Datenbank '%1$s' mit Seitengröße %2$s. (Log-Seitengröße: %3$s)\n\n
11 Ausgangdatei konnte nicht geöffnet werden '%1$s'\n
12 LOB Speicherbeschreibung %1$s \n\n
20 type                purpose            volume_count        used_pages          free_pages          total_pages\n
21 type                purpose            volume_count         used_size           free_size           total_size\n
22 PERMANENT           PERMANENT DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
23 PERMANENT           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
24 TEMPORARY           TEMPORARY DATA            %1$5d     %2$13s       %3$13s        %4$13s\n
25 -                   -                         %1$5d     %2$13s       %3$13s        %4$13s\n
30 Raumbeschreibung für alle Bände:\n
31 volid               type                purpose            used_pages          free_pages          total_pages         volume_name\n
32 volid               type                purpose             used_size           free_size           total_size         volume_name\n
33 %1$5d               PERMANENT           PERMANENT DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
34 %1$5d               PERMANENT           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
35 %1$5d               TEMPORARY           TEMPORARY DATA  %2$13s       %3$13s        %4$13s         %5$-64s\n
40 Detaillierte Raumbeschreibung für Dateien:\n
41 data_type           file_count          used_pages         file_table_pages    reserved_pages      total_pages\n
42 data_type           file_count           used_size          file_table_size     reserved_size       total_size\n
43 %1$-10s               %2$5d       %3$13s            %4$13s     %5$13s    %6$13s\n
50 -----------------------------------------------------------------------------------------------------------------\n
60 \
spacedb: Belegter/freier Speicher der Datenbank anzeigen.\n\
Anwendung: %1$s spacedb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI       Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -S, --SA-mode                Stand-Alone-Ausführung\n\
  -C, --CS-mode                Client-Server-Ausführung\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               Anzeigeeinheit setzen (Seite, mega, giga, terra, Mensch-lesbar); Standard: H\n\
  -s, --summarize              nur eine Zusammenfassung für jedes Datenträgerzweck anzeigen\n\
  -p, --purpose                Anzeige des verwendeten Größe der einzelnen Zwecke (data/index/temp) jedes Volumen\n

$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Geschätzte Anzahl der Seiten = %1$d\n
60 \
estimate_data: Geschätzte Datengröße.\n\
Anwendung: %1$s estimate_data Anzahl-der-Instanzen durschnittliche-Instanzgröße Anzahl-der-Attribute Variable-anzahl-der-Attribute \n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Ungültige Indexschlüsseltyp angegeben = %1$s\n
16 Ungültige durchschnittliche Schlüssellänge angegeben = %1$d wenn Präzision = %2$d\n
17 Ungültige Argumente angegeben \n
20 Geschätzte Anzahl aller Indexseiten = %1$d\n (Wenn Index erstellt wird, nachdem die Instanzen eingepflegt worden sind.)\n
21 Geschätzte Anzahl aller Indexseiten = %1$d\n (Wenn Index erstellt wird, bevor die Instanzen eingepflegt worden sind. (durchschnittlicher en Fall))\n
22 Geschätzte Anzahl aller Indexseiten = %1$d\n (Wenn Index erstellt wird, bevor die Instanzen eingepflegt worden sind. (schlechter Fall))\n
23 Für das angegebene Input...\n
24 Anzahl aller Instanzen: %1$d\n
25 Anzahl unterschiedlicher Schlüssel: %1$d\n
26 Durchschnittliche Schlüsselgröße         : %1$d\n
27 Schlüssel-Typ                 : %1$s\n\n
60 \
estimate_index: Geschätzte Größe eines Index.\n\
Anwendung: %1$s estimate_index Anzahl-der-Instanzen Anzahl-unterschiedlicher-Schlüssel durchschnittliche-Schlüsselgröße Schlüssel-Typ \n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Datenbank installieren.\n\
Anwendung: %1$s installdb [OPTION] Datenbanknamen\n\
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
7 Fehler beim Öffnen der ELO Eingangsdatei "%1$s"\n
8 KLASSEN:
9 Hash-Dateiname
10 Name:                         %1$s\n
11 Größe:                         %1$d\n
12 Seitengröße:                    %1$d\n
13 Datengröße:                    %1$d\n
14 Eintragsgröße:                   %1$d\n
15 Einträge per Seite:             %1$d\n
16 Seiten gecached:                 %1$d\n
17 Anzahl der Einträge:            %1$d\n
18 Anzahl der Kollisionen:         %1$d\n
19 Hash Dateiname:                %1$s\n
20 Nächster überlaufene Eintrag:          %1$d\n
21 Schlüsseltyp:                     %1$s\n
22 Seitenheaderadresse:      %1$x\n
23 Adresse des letzten Seitenheader:  %1$x\n
24 Adresse des freien Seitenheader:  %1$x\n
25 Adresse der Seitenbitmap:       %1$x\n
26 Seitenbitmap Größe:             %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
15 Ausgangsdatei '%1$s' konnte nicht geöffnet werden \n
60 \
diagdb: Database exportieren.\n\
Anwendung: %1$s diagdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI     Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
      --emergency             Notfall neu starten; Standard: keine\n\
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
15 Ausgangsdatei '%1$s' konnte nicht geöffnet werden \n
59 lockdb kann in Stan-Alone-Modus nicht ausgeführt werden.\n
60 \
lockdb: Lockstatus der Datenbank anzeigen.\n\
Anwendung: %1$s lockdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI       Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -c, --contention             display only lock information which has contention\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Nur ein Argument unter: tran-index, Benutzer, Host, Program darf angegeben werden\n
21 DBA-Passwort eingeben:
26 Es gibt keine Transaktion mit den angegebenen Argumentwerte.\n
27 Folgende Transaktionen abbrechen:\n\n
28 Möchten Sie fortfahren? (Y/N)
29 Transaktion mit Index %1$d wird abgebrochen\n
30 Abbrechen der folgenden Transaktion fehlgeschlagen\n\n
31 Die Transaktion ist noch nicht abgeschlossen, aber es könnte ein Rollback ausführen.\n Bitte warten Sie einen Augenblick, versuchen Sie bei Bedarf später.\n
32 Ungültiges tranindex(%1$s).\n
59 killtran darf nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
killtran: Transaktionen der Datenbank verwalten.\n\
Anwendung: %1$s killtran [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -i, --kill-transaction-index=INDEX Transaktion mit Index INDEX abbrechen\n\
      --kill-user-name=ID            alle Transaktionen mit Benutzer ID abbrechen\n\
      --kill-host-name=HOST          alle Transaktionen mit Client HOST abbrechen\n\
      --kill-program-name=NAME       alle Transaktionen mit Client-Programm NAME abbrechen\n\
      --kill-sql-id=SQL_ID           alle Transaktionen mit SQL_ID schließen\n\
  -p, --dba-password=PASSWORT        Passwort des DBA-Benutzers; es wird angefordert, wenn nicht angegeben\n\
                                     (Arbeiten Sie nur mit den Optionen -i oder --kill. Andernfalls wird ein Fehler angezeigt)\n\
  -d, --display-information          Information über aktive Transaktionen anzeigen \n\
  -q, --query-exec-info              Informationen zur Abfrageausführung anzeigen; Informationen zur aktiven Transaktionen\n\
  -f, --force                        Transaktion ohne Bestätigung abbrechen \n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 Datenbanknamen
2 erforderlicher Argument, keinen Standardwert
3 Kompatibilitätsprüfung
4 nur Kompatibilitätsprüfung
5 Backup-Ziel
6 ein Verzeichnispfad oder ein Gerät als Ziel des Backupdatenträgers
20 Datenbank '%1$s' existiert nicht.
21 '%1$s' kann zu %2$s nicht migriert werden, weil die DB-Version zu niedrig ist.\n
22 '%1$s' brauchen keine Migration.\n
23 '%1$s' muss migriert werden.\n
24 Ein Backup wird nach der Migration empfohlen. Wollen Sie ein Backup Ihrer Datenbank jetzt machen? (y/n):
25 Wir empfehlen, Ihre neue Datenbank nach Migration mit dem backupdb Befehl zu sichern.
26 \n*** WARNUNG: Sichern Sie Ihre Datenbank, bevor die Migration zu %1$s angefangen wird ***\n\nUm Fehler zu erholen, die bei der Migration auftreten können, empfehlen wir dringend, ein Backup der Datenbank zu machen.\nHaben Sie bereits Ihre Datenbank gesichert? (y/n):
27 \n1. backupdb ausführen\n2. Geben Sie den neuen Backuppfad für die neue Datenbank. (aktueller Pfad: %1$s)\n3. Exit \n
28 \nDer neue Backuppfad für die Datenbank:
29 \nMigration und Backup erfolgreich abgeschlossen.
30 Backup fehlgeschlagen. Bitte wiederholen Sie die Backupprozedere.
31 \n\n*** WARNUNG: Sobald die Migration abgeschlossen ist, können Sie die Datenbank nicht mehr mit den bisherigen Backupdatenträger wiederherstellen.\nBeachten Sie, dass die bisherigen Version der Backupdateien nur auf der entsprechenden Version von restoredb wiederhergestellt werden können.\nBitte überprüfen Sie, dass die bisherigen Backupdatenträger sicher aufbewahrt werden.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Host der Datenbank ändern.\n\
Anwendung: %1$s alterdbhost [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -h, --host=HOST              neuer HOST Name; Standard: aktueller Hostname\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 Datenbanknamen
2 erforderlicher Argument, kein Standardwert
3 Java-Klassendatei
4 erforderlicher Argument, kein Standardwert
60 \
Usage: loadjava [OPTION] database-name java-class-file\n\
\n\
valid options:\n\
  -y, --overwrite              Klassendatei, wenn vorhanden, überschreiben, Standard nein\n\
  -j, --jni                    add a Class or Jar to static loading for JNI\n

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Ausgangdatei '%1$s' kann nicht geöffnet werden\n
59 plandump kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
plandump: Cached Abfragepläne anzeigen oder kontrollieren.\n\
Anwendung: %1$s plandump [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -d, --drop                   alle Pläne ins Server-Cache exportieren \n\
  -o, --output-file=DATEI       Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Ausgangdatei '%1$s' kann nicht geöffnet werden\n
21 Systemparameter, die vom Client-Programm angewendet werden.\n
22 Systemparameter vom Server '%1$s' geladen\n
23 Systemparameter, die in Standalone-Modus angewendet werden.\n
60 \
paramdump: Systemparameter-Einstellungen exportieren.\n\
Anwendung: %1$s paramdump [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI       Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -b, --both                   sowohl Client- als auch Serverparameter exportieren \n\
  -S, --SA-mode                Stand-Alone-Ausführung\n\
  -C, --CS-mode                Client-Server-Ausführung\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Ungültiger Wert \"%1$s\" für das HA-Ausführung-Modus des Servers.\n
12 Ausführung-Modus des Servers kann nicht geändert werden.\n
21 DBA-Passwort eingeben:
22 Das '%1$s' des Servers aktuellen HA-Ausführung-Modus ist %2$s.\n
23 Das '%1$s' des Servers HA-Ausführung-Modus ist auf \"%2$s\" geändert worden.\n
24 Der Server wurde für HA nicht konfiguriert.\n
58 HA-Funktion wird auf dieser Plattform nicht unterstützt.\n
59 changemode kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
changemode: Das HA-Ausführung-Modus des Servers anzeigen oder ändern.\n\
Anwendung: %1$s changemode [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -m, --mode=MODUS             Das Modus des Servers zu MODUS ändern; Standard: kein\n\
  -f, --force                 Änderung zwingen\n\
  -t, --timeout=S             es wird S Sekunden gewartet, bis die aktive Transaktion normal beendet wird\n\
                              -t Option ist nur dann wirksam, wenn der Benutzer das Server-Modus auf Wartung ändert\n\
                              Standard: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Ungültiger Wert \"%1$s\" für das copylogdb's Ausführung-Modus.\n
21 DBA-Passwort eingeben:
22 Der Server wurde für HA nicht konfiguriert.\n
58 HA-Funktion wird auf dieser Plattform nicht unterstützt.\n
59 copylogdb kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
copylogdb: Logseiten vom Server kopieren.\n\
Anwendung: %1$s copylogdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -L, --log-path=PFAD         Verzeichnispfad für kopierten Logdatenträger\n\
  -m, --mode=MODE             Operationsmodus; Standard: sync\n\
                              MODUS: sync oder semisync oder async\n\
  -S, --start-page-id=ID      first log page to copy\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 DBA-Passwort eingeben:
22 Der Server wurde für HA nicht konfiguriert.\n
58 HA-Funktion wird auf dieser Plattform nicht unterstützt.\n
59 applylogdb kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
applylogdb: Das kopierte Log auf dem Server übernehmen.\n\
Anwendung: %1$s applylogdb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -L, --log-path=PFAD         Verzeichnispfad der kopierten Logdatenträger\n\
      --max-mem-size=GRÖSSE   maximale Prozessgröße GRÖSSE in MB; max. 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Ausgangdatei kann nicht geöffnet werden '%1$s'\n
59 statdump kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
statdump: Globale Statistiken exportieren.\n\
Anwendung: %1$s statdump [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -o, --output-file=DATEI     Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n\
  -c, --cumulative            kumulative Statistiken anzeigen \n\
  -i, --interval=S            Informationen jede S Sekunden drucken\n\
  -s, --substr=STRING         nur die Statistiken anzeigen, die zum String anpassen\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 DBA-Passwort eingeben:
22 Der Server wurde für HA nicht konfiguriert.\n
57 Die angewendeten Informationen können aufgrund eines ungültigen Pfads der Option -L oder aufgrund fehlender zugehöriger Informationen in der Katalogtabelle db_ha_apply_info nicht generiert werden.\n
58 HA-Funktion wird auf dieser Plattform nicht unterstützt.\n
59 applyinfo kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
applyinfo: CUBRID HA-Übernehmen Information anzeigen.\n\
Anwendung: %1$s applyinfo [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -r, --remote-host-name      Remote-Hostnamen; aktiven Log-Information des Remoteknoten anzeigen\n\
  -L, --copied-log-path=PFAD  Pfad der kopierten Logdatenträger; kopierte Loginformation anzeigen \n\
  -a, --applied-info          Übernommene Information anzeigen; Die Option -L ist erforderlich \n\
  -p, --page=ID               Seiten-ID; Die Option -L ist erforderlich; Standard: 0\n\
  -v, --verbose               ausführliche Statusmeldungen aktivieren; Standard: inaktiv\n\
  -i, --interval=S            Druckinformationen alle S secs\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb kann nicht in Stand-Alone-Modus ausgeführt werden.\n
60 \
acldb: Zugangskontrolleinstellungen der Datenbank anzeigen oder neu laden.\n\
Anwendung: %1$s acldb [OPTION] Datenbanknamen\n\
\n\
gültige Optionen:\n\
  -r, --reload       IP-Kontrolldatei neu laden \n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Generiert die lokale gemeinsame Bibliothek anhand der LDML Eingangsdatei(en).\n\
Anwendung: %1$s genlocale [OPTION] [Sprachenstring]\n\
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
Anwendung: %1$s dumplocale [OPTION] [Sprachenstring]\n\
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
43 Die Partitionierung muss entfernt werden, bevor die aktuelle Kollation '%1$s' des Partitionierungsattribut geändert wird.\n Klassenname\n
44 Fremdschlüssel auf Attribute mit Kollation '%1$s' müssen vor Änderung der Kollation der Attributen, gelöscht werden.\n Tabelle | Fremdschlüssel\n
45 Klassen mit Kollation '%1$s'; sie sollten neu definiert werden:\n Klassenname\n
46 Funktion Indexen mit Kollation '%1$s'; sie sollten neu definiert werden:\n\
Tabelle | Index | Funktion Ausdruck\n
47 Synchronisierung der Kollationen wurde nicht durchgeführt. Die Datenbank wurde nicht geändert.\n
48 Inhalt der '%1$s' Systemtabelle wurde mit den neuen Systemkollationen aktualisiert.\n
49 Die Aktualisierung der Systemkollationen kann zur Korruption der Datenbank durchführen. Weiter (y/n)?\n
50 Kollation '%1$s' (Id: %2$d) nicht gefunden, oder sie wurde in der neuen Systemkonfiguration geändert.\n
51 Trigger mit Bedingungen mit Kollation '%1$s'; sie sollten neu definiert werden:\nTrigger | Bedingung\n
52 Abfrageanzeige mit Kollation '%1$s'; sie sollten neu definiert werden :\nAnzeige | Abfrage\n
53 Attribute mit Kollation '%1$s'; sie sollten neu definiert werden.\nTabelle | Attributdomäne\n
54 Manueller Eingriff in der Datenbank ist erforderlich.\n\
Bitte Datei '%1$s' überprüfen und ausführen, bevor Sie Kollationen in die Datenbank zwingen.\n
55 Synchronisierung nicht erforderlich.\n
56 Synchronisierung der Systemkollationen der Datenbank erforderlich.\n\
'cubrid synccolldb -f %1$s' ausführen\n
57 Es gibt %1$d neue Kollationen in der Systemkonfiguration, in Vergleich zur Datenbank:\n
58 Alle Kollationen in der Datenbank '%1$s' sind in der neuen Systemkonfiguration gefunden.\n
59 Es gibt %1$d Kollationen in der Datenbank, die nicht konfiguriert sind, oder wurden in Vergleich zu den Systemkollationen geändert.\n
60 \
synccolldb: Synchronisiert die Datenbank- mit den Systemkollationen.\n\
Anwendung: %1$s synccolldb [OPTION] database-name\n\
\n\
gültige Optionen:\n\
  -c, --check-only    Überprüft die Kollationen der Datenbank mit der Systemkollation.\n\
              Wenn Kollationen Synchronisierung  ist erforderlich, Fehlerkode 1 wird zurückgegeben.\n\
              Zeigt einen Bericht der inkompatiblen Kollationen.\n\
  -f, --force-only    Aktualisiert die Kollationen in der Datenbank, so dass sie gleich mit den Kollationen einer neuen Systemkonfiguration sind.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Passwort %1$s eingeben:
22 Tran index            Benutzername      Hostname      Prozess ID      Programmname\n
23 ---------------------------------------------------------------------------------\n
24 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
25 Es gibt keine Transaktionen\n
26 tranlist darf nur von DBA oder einem Mitglied der DBA Gruppe durchgeführt werden.\n
27 sort-key Spalte (%1$d) ist ungültig.\n
32 Tran index           Prozess ID    Programmname              Abfragezeit   Tran Zeit              Warten auf Sperrhalter    SQL_ID       SQL Text\n
33 %1$4d%2$-12s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 ----------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index            Benutzername   Hostname       Prozess ID    Programmname              Abfragezeit   Tran Zeit              Warten auf Sperrhalter    SQL_ID       SQL Text\n
43 %1$4d%2$-12s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %1$d
46 SQL_ID: %1$s\n
59 tranlist dar nicht in Stand-Alone-Modus durchgeführt werden.\n
60 \
tranlist: Informationen der aktiven Transaktion wegwerfen.\n\
usage: %1$s tranlist [OPTION] Datenbankname\n\
\n\
gültige Optionen:\n\
  -s, --summary                zusammengefasste Transaktionsinformationen anzeigen\n\
      --sort-key=SPALTENNUMMER Spalte zu sortieren\n\
      --reverse                Ergebnisse umgekehrt sortieren\n

$set 51 MSGCAT_UTIL_SET_GEN_TZ
59 Erzeugungsmodus Ungültig geliefert.\n
60 Ungültige oder Eingangsordner beschädigte.\n
61 \
gen_tz: Generieren Sie Quelldateien enthält, Zeitzone Daten, die in einer gemeinsamen\n\
        Bibliothek kompiliert werden.\n\
Anwendung: %s gen_tz -g [new|extend] [Datenbank-Name] (Nur für Erweiterungsmodus)\n\
\n\
gültigen Optionen:\n\
  -i, --input-folder    Pfad zu einem Eingangsordner enthält eine Version von IANA\n\
                        Zeitzone-Datenbankdateien.\n\
                        Standard: $CUBRID/timezones/tzdata\n\
  -g, --gen-mode=TYPE   Der Ausgang des gen_tz, basierend auf der TYPE:\n\
                        TYPE:\n\
                        1. NEW (Standard) : Zeitzone zu erzeugen Quelldateien für die\n\
                        gemeinsame Bibliothek ohne Rücksicht auf frühere Versionen;\n\
                        eine Quelldatei mit allen Ländern und Zeitzone-Namen zu \n\
                        generieren auch (für Entwicklerzwecke bestimmt ist, um in\n\
                        Zukunft CUBRID Versionen integriert werden).\n\
                        2. EXTEND : Generiere Zeitzonen-Quelldateien mit dem\n\
                        Daten aus dem Eingabeordner; Neue Zeitzonen in der Eingabe gefunden\n\
                        Dateien werden hinzugefügt; GMT-Offsets und Sommerzeit\n\
                        Informationen werden aktualisiert (oder hinzugefügt, für neue Zeitzonen);\n\
                        Zeitzonen werden nie entfernt, wenn eine Zeitzone verschlüsselt wird\n\
                        CUBRID fehlt in den Eingabedateien Daten werden aus der\n\
                        vorhandenen Zeitzonenbibliothek abgerufen.\n\
                        Wenn die erste Phase der Erweiterung nicht alle Daten rückwärts kompatibel macht,\n\
                        Alle Zeilen in allen Tabellen mit Zeitzonendatentypen werden aktualisiert.\n\

$set 52 MSGCAT_UTIL_SET_DUMP_TZ
59 Die mitgelieferte Zonen-ID außerhalb des Bereichs ist.\n\
 Verwenden Sie -z, um alle Zeitzonen und deren IDs zu sehen.\n
60 \
dump_tz: Anzeigezeitzone bezogene Informationen.\n\
Anwendung: %s dump_tz [OPTION]\n\
\n\
gültigen Optionen:\n\
  -c, --list-countries      zeigt die Liste der Länder, die derzeit nicht unterstützt\n\
  -l, --leap-seconds        zeigt die Liste der Schaltsekunden\n\
  -s, --summary             zeigt eine Zusammenfassung der Dump-Zeitzone-Daten\n\
  -z, --list-zones          zeigt die Liste der aktuell unterstützten Zeitzonen\n\
  -d, --zone-id=ZONE_ID|ALL Anzeigen vorhanden Zeitzoneninformationen zu einer\n\
                            Zeitzone mit dem mitgelieferten zone_id oder für alle\n\
                            timzones verwandt. Die ID einer Zeitzone kann aus der\n\
                            angezeigten Liste mit der Option -z erhalten werden.\n

$set 53 MSGCAT_UTIL_SET_RESTORESLAVE
20 Restoreslave abgebrochen oder ein Fehler aufgetreten ist.\n
21 Datenbankwiederherstellung abgeschlossen ist. Aber konnte nicht initialisiert applyinfo werden. (db_creation_time='%1$s', pageid=%2$lld, offset=%3$d)\n
22 Der Datenbankname oder der Host-Name war nicht für HA konfiguriert.\n
23 Falscher Wert "%1$s" für Stand der Sicherungsquellenknoten.\n
60 \
restoreslave: Wiederherstellen einer Slave-Datenbank.\n\
Anwendung: %1$s restoreslave [OPTION] Datenbank-Name\n\
\n\
gültigen Optionen:\n\
  -s, --source-state=STATE          Zustand des Backup-Quellknoten; verpflichtend\n\
                                    STATE kann 'master', 'slave' oder 'replica' zu sein\n\
  -m, --master-host-name=NAME       Hostname des aktuellen Master-Knoten; verpflichtend\n\
      --list                        eine Liste der Sicherungsdatenträger angezeigt werden, ohne die Wiederherstellung\n\
  -B, --backup-file-path=PATH       PATH ein Verzeichnis der Sicherungsdatenträger wiederhergestellt werden\n\
  -o, --output-file=FILE            umleiten Ausgabemeldungen in FILE; Standard: Keine\n\
  -u, --use-database-location-path  Stellen Sie die Datenbank und Log-Volumes auf die in der Datenbank-Datei Speicherort angegebenen Pfad\n\
  -k, --keys-file-path              Pfad der Schlüsseldatei (_keys) für TDE, die beim Wiederherstellen verwendet werden soll\n

$set 54 MSGCAT_UTIL_SET_DELVOLDB
21 Bereit, die folgenden Mengen zu entfernen:\n\n
22 Möchten Sie fortfahren? (Y/N)
31 System-Volume oder den ersten Band nicht entfernen (Volid: %1$d).\n
32 Kann das Volume nicht gefunden (volid : %1$d).\n
33 Es gibt zu viele Volids (Max Volids: %1$d).\n
34 Ungültige VOLID(%1$s).\n
60 \
delvoldb: Entfernen eines Leervolumen-Datei der Datenbank.\n\
Anwendung: %1$s delvoldb [OPTION] Datenbank-Name -i Volid\n\
\n\
gültigen Optionen:\n\
  -c, --clear-cache                Sie alle Cache-Dateien im temporären Temp Volumen deaktivieren \n\
  -f, --force                      die Lautstärke zu entfernen, ohne eine Aufforderung zur Überprüfung\n\
  -p, --dba-password=PASS          Kennwort des DBA-Benutzer; fordert, wenn nicht angegeben\n\
  -S, --SA-mode                    Stand-alone-Modus Ausführung\n\
  -C, --CS-mode                    Client-Server-Modus Ausführung\n

$set 55 MSGCAT_UTIL_SET_VACUUMDB
20 vacuumdb vorübergehend nicht verfügbar ist für Client-Server-Modus\n
21 Der Vakuumbetrieb gescheitert\n
22 Ausgangsdatei '%1$s' konnte nicht geöffnet werden \n
60 \
vacuumdb: Vakuum Toten Datensätze aus der Datenbank.\n\
Anwendung: %1$s vacuumdb [OPTION] Datenbank-Name\n\
\n\
gültigen Optionen:\n\
  -S, --SA-mode                    Stand-alone-Modus Ausführung\n\
  -C, --CS-mode                    Client-Server-Modus Ausführung\n\
      --dump                       aktuellen Vakuumzustand entleeren\n\
  -o, --output-file=DATEI          Umleiten der Ausgabemeldungen in DATEI; Standard: keine\n

$set 56 MSGCAT_UTIL_SET_CHECKSUMDB
1 Fehler beim Tabellennamen in %1$s zu laden.\n
2 checksumdb muss auf einem HA-Server ausführen aktiven. %1$s (aktuellen Status: %2$s)\n
3 Der Eigentümer der Tabelle zum Speichern des Prüfsummenergebnisses muss DBA sein.\n
58 HA Feature ist um dieser Plattform nich unterstützt.\n
59 checksumdb kann nicht laufen als Standalone-Modus.\n
60 \
checksumdb: Check-Replikationsintegrität.\n\
Anwendung: %1$s checksumdb [OPTION] datenbankname@hostname\n\
\n\
gültigen Optionen:\n\
  -c, --chunk-size=NUMBER       die Anzahl der Zeilen für jede Prüfsummenberechnung wählen\n\
                                Standard: 500 Zeilen Mindest: 100 Zeilen\n\
  -s, --sleep=NUMBER            schlafen Zeit zwischen Prüfsummenberechnung; Standard: 100 ms\n\
  -i, --include-class-file=FILE Eingabedatei von Tabellennamen schließen zu\n\
  -e, --exclude-class-file=FILE Eingabedatei der Tabellennamen, um auszuschließen; entweder -i oder -e,\n\
                                verwendet werden, jedoch nicht beides\n\
  -t, --timeout=NUMBER          Abfrage-Timeout in Millisekunden; Standard: 1000 ms\n\
  -n, --table-name=NAME         Tabellennamen, um Prüfsumme Ergebnisse zu speichern; Standard: dba.db_ha_checksum\n\
  -r, --report-only             Bericht Replikationsintegrität auf der Grundlage Prüfsummenberechnung\n\
                                alle Optionen außer --table-name werden ignoriert\n\
      --resume                  Wiederaufnahme der Berechnung Prüfsumme\n\
      --schema-only             überprüfen nur Schemakonsistenz\n\
      --cont-on-error           weiter auf Berechnungsfehler\n

$set 57 MSGCAT_UTIL_SET_TDE
21 Geben Sie das DBA-Passwort ein:
25 Die in der Datenbank festgelegten Schlüsselinformationen konnten nicht abgerufen werden\n
26 Versuch, den Schlüssel vom Schlüssel (Index: %1$d) zum Schlüssel (Index: %2$d) zu ändern ..\n
27 Der Schlüssel wurde vom Schlüssel (index: %1$d) zum Schlüssel (index: %2$d) geändert\n
28 Der in der Datenbank festgelegte Schlüssel kann nicht gelöscht werden\n
29 Der Schlüssel (index: %1$d) wurde gelöscht\n
30 Ein neuer Schlüssel wurde generiert - Schlüsselindex: %1$d erstellt auf%2$s
60 \
tde: Verwaltung der transparenten Datenverschlüsselung (TDE)\n\
Verwendung: %1$s tde OPERATION (-s|-n|-d|-c) [OPTION] Datenbankname\n\
\n\
gültige Optionen:\n\
    -p, --dba-password=PASS-Passwort    des DBA-Benutzers; wird gefragt, wenn nicht angegeben wird\n\
\n\
OPERATIONEN: Eine der Operationen muss angegeben werden und sie sind exklusiv\n\
    -s, --show-keys                     druckt Schlüsselinformationen, die in der Datenbank und in der Schlüsseldatei (_keys) festgelegt sind\n\
    -n, --generate-new-key              generiert einen neuen Schlüssel in der Schlüsseldatei (_keys); maximale Anzahl: 128\n\
    -d, --delete-key=KEY_INDEX          löscht einen Schlüssel in der Schlüsseldatei (_keys)\n\
    -c, --change-key=KEY_INDEX          Ändern Sie den in der Datenbank festgelegten Schlüssel für einen anderen Schlüssel in der Schlüsseldatei (_keys)\n

$set 58 MSGCAT_UTIL_SET_FLASHBACK
1 DBA-Passwort eingeben:
2 Ungültiges Datumsformat, gültiges Format ist tt-mm-jjjj:hh:mm:ss\n
3 Startdatum (%1$s) muss kleiner sein als Enddatum (%2$s)\n
4 Bitte versuchen Sie es mit einem Datum vor dem aktuellen (%1$s) oder nachdem die Datenbank erstellt wurde (%2$s)\n
5 Tabelle (%1$s) existiert nicht in Datenbank\n
6 Zu viele Transaktionen für Flashback. Bitte verkürzen Sie das Intervall zwischen Startdatum und Enddatum\n
7 Flashback kann nicht abgerufen werden, da das Schema (%1$s) geändert wurde.\n
8 Log-Volume wurde entfernt. Bitte versuchen Sie es mit dem späteren Datum\n
9 Ausgabedatei '%1$s' konnte nicht geöffnet werden\n
10 Transaktions-ID (%1$d) existiert nicht\n
11 Bitte setzen Sie 'supplemental_log' in 'cubrid.conf'. Nur die SQL-Protokolle, die nach dem Setzen von „supplemental_log“ ausgeführt werden, können per Flashback gesichert werden.\n
12 Transaktions-ID sollte in %1$d Sekunden eingegeben werden. Das Timeout kann über die Einstellung „flashback_timeout“ in „cubrid.conf“ angepasst werden.\n
13 Flashback läuft bereits. Flashback kann nicht von zwei Prozessen ausgeführt werden.\n
14 Flashback kann nicht im eigenständigen Modus ausgeführt werden.\n
15 Die Systemkatalogklasse (%1$s) wird in Flashback nicht unterstützt.\n
60 \
flashback: Stellt Anweisungen zum Flashback einer bestimmten Transaktion bereit.\n\
Verwendung: %1$s flashback [OPTION] Datenbankname Besitzername.Klassenname1 [Besitzername.Klassenname2 ...]\n\
\n\
gültige Optionen:\n\
    -o, --output-file=DATEI             Ausgabenachricht an DATEI umleiten; Standard: keine.\n\
    -u, --user=ID                       gibt den Benutzer an, der die SQL-Anweisung für die Rückblende ausgeführt hat; Standard: ALLE.\n\
    -p, --dba-password                  Passwort des DBA-Benutzers; wird aufgefordert, wenn nicht angegeben\n\
    -s, --start-date=DATUM              gibt das Startdatum für die Rückblende an. SQL-Protokolle werden aus DATE extrahiert.\n\
                                        DATUM wird wie folgt angegeben: tt-mm-jjjj:hh:mm:ss.\n\
    -e, --end-date=DATUM                Enddatum für die Rückblende angeben. SQL-Protokolle werden bis zum DATE extrahiert.\n\
                                        wenn start-date nicht angegeben ist, dann wird ((END - 10min) ~ END) auf range gesetzt,\n\
                                        und sonst, wenn END nicht angegeben ist, dann wird (start ~ (start + 10min)) gesetzt.\n\
                                        Und weder ein Startdatum noch ein Enddatum ist angegeben,\n\
                                        Die SQL-Protokolle, die zwischen der (aktuellen Uhrzeit - 10 Minuten) und der aktuellen Uhrzeit\n\
                                        ausgeführt wurden, werden extrahiert.\n\
    --detail                            Gibt detaillierte Informationen für jede SQL-Anweisung aus.\n\
                                        Transaktions-ID, Benutzername und ursprüngliche SQL-Anweisung werden mit der SQL-Anweisung\n\
                                        zurückgegeben für Rückblende; Standard: gibt nur SQL-Anweisungen für Flashback aus\n\
    --oldest                            Flashback wird in chronologischer Reihenfolge für SQL-Anweisungen durchgeführt,\n\
                                        die von der Startzeit bis zur Endzeit ausgeführt werden; Standard: neueste\n

$set 59 MSGCAT_UTIL_SET_MEMMON
1 Bitte setzen Sie den Systemparameter 'enable_memory_monitoring' in 'cubrid.conf' und starten Sie den Datenbankserver neu.\n
2 memmon kann nicht im eigenständigen Modus ausgeführt werden.\n
3 Fehler beim Öffnen der Ausgabedatei mit dem Dateinamen %1$s.\n
60 \
memmon: Bietet Informationen zur Speicherauslastung eines Datenbankservers.\n\
Verwendung: %1$s memmon [OPTION] Datenbankname\n\
\n\
Gültige Optionen:\n\
    -o, --output-file=DATEI    Ergebnisse in DATEI ausgeben\n
