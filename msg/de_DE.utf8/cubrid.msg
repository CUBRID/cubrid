
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
3 Copyright Information\n
5 Non-Integer-Argument Wert `%1$s'
6 Non-Gleitzahl-Argument Wert `%1$s'
7 Fehlendes Argument `%1$s'
8 Doppeltes Argument `%1$s'
9 Fehlender Wert für Argument `%1$s'
10 Unbekannter Argument `%1$s'
11 Neudefinition des Cursors `%1$s'
12 Keine definierte Argumente
13 Speicher für Argument-Parsing konnte nicht zugeordnet werden
14 Argument Wert überschreitet die maximale Länge: %1$s
15 Ungültige Nachrichtnummer %1$d im Katalog für Argument-Parsing
16 [Options]
17 Optionen:

$set 4 MSGCAT_SET_PARAMETER
2 %1$s ist kein Ordner. Erstellung %2$s in ".".
3 Kann %1$s nicht öffnen. Konfigurationsdatei nicht erstellt.
4 Kann die Zeile %1$d in die Konfigurationsdatei %2$s nicht schreiben.
6 Kann die Systemkonfigurationsdatei "%1$s" nicht zugreifen: %2$s
7 HOME ist nicht gesetzt
8 Erforderlicher Parameter "%1$s" hat keinen Wert.
9 Kann die Konfigurationsdate "%1$s" nicht öffnen: %2$s
10 Der '%1$s' Parameter in Zeile %2$d in Datei '%3$s': 
11 Umgebungsvariable %1$s: 
12 Unbekannter Schlüsselwort.
13 Wert-typ entspricht nicht dem Parametertyp.
14 Speicher kann nicht zugewiesen werden, um den Wert zu speichern.
15 Nicht abgeschlossene String-Wert.
16 Der Wert ist außerhalb des zulässigen Bereichs.
17 UNIX-Fehler: %1$s
18 Keine Meldung vorhanden.
19 Der Wert ist außerhalb des zulässigen Bereichs. Zurücksetzen auf den Standardwert.
20 Schlüsselwort = '%1$s'    Wert = %2$d    Standardwert = %3$d\n
21 Schlüsselwort = '%1$s'    Wert = %2$f    Standardwert = %3$f\n
22 Veralteter Parameter
23 Die Parameter '%1$s' und '%2$s' dürfen nicht zusammen verwendet werden.\n
31 Der Parameter '%1$s':

$set 5 MSGCAT_SET_ERROR
1 Fehlende Nachricht für Fehlercode %1$d.
2 Interner Systemfehler: keine mehr spezifischen Informationen zur Verfügung.
3 Virtueller Speicher Fehler: kann %1$lld Speicherbytes nicht zuzuordnen.
4 Wurde unterbrochen.
5 Schlüssel = "%1$s" wurde in die Hash-Tabelle "%2$s" nicht gefunden.
6 Eine NULL-Speicher Hash-Tabelle wurde zugeführt zu einer Hash-Tabelle-Funktion.
7 Es wird versucht den Datenträger "%1$s" mit einem falschen Wert %2$d für die Seitenanzahl zu formatieren.
8 Kann den Datenträger "%1$s" mit %2$d Seiten (%3$lld Bytes) nicht formatieren.
9 Kann den Datenträger "%1$s" mit %2$d Seiten (%3$lld Kbytes) wegen ungenügend Speicher nicht formatieren. Aktuell vorhandener Speicher ist %4$d Seiten (%5$lld Kbytes).
10 Kann den Datenträger "%1$s" nicht mounten.
11 Kann den Datenträger "%1$s" nicht mounten. Die Datenbank "%2$s", an die der Datenträger gehört, wird vom Benutzer %3$s im Prozess %4$d auf Host %5$s seit %6$s benutzt.
12 Fehler beim Unmounten des Datenträgers "%1$s".
13 I/O Fehler beim Lesen der Seite %1$d des Datenträgers "%2$s".
14 I/O Fehler beim Schreiben der Seite %1$d des Datenträgers "%2$s".
15 Seite %1$d des Datenträgers "%2$s" kann wegen ungenügend Speicher auf dem Betriebssystem-Gerät nicht geschrieben werden.
16 Kann den Datenträger "%1$s" in "%2$s" nicht umbenenen.
17 Interner Fehler: Abholen des freigegebenen Page-Id %1$d des Datenträgers "%2$s".
18 Alle Seitenpuffer sind fixiert (LRU: %1$d).
19 Interner Fehler: Seitenzeiger = %1$p der Seite %2$d des Datenträgers "%3$s" ist nicht fixiert.
20 Interner Fehler: unbekannter Seitenzeiger = %1$p.
21 Interner Fehler: unbekannter Sektor %1$d des Datenträgers "%2$s".
22 Interner Fehler: unbekannte Seite %1$d des Datenträgers "%2$s".
23 Interner Fehler: beim Versuch, die Seite %1$d des Datenträgers "%2$s" freizugeben.
24 Fast außer Speicher in Datenträger "%1$s". Gesamtzahl der Seiten = %2$d, Gesamtzahl der freien Seiten = %3$d.
25 Fast außer Speicher in Datenträger für Datenspeicherung. Gesamtzahl der Seiten = %1$d, Gesamtzahl der freien Seiten = %2$d.
26 Fast außer Speicher in Datenträger für Indexspeicherung. Gesamtzahl der Seiten = %1$d, Gesamtzahl der freien Seiten = %2$d.
27 Fast außer Speicher in Datenträger für Allgemeinspeicherung. Gesamtzahl der Seiten = %1$d, Gesamtzahl der freien Seiten = %2$d.
28 Fast außer Speicher in Datenträger für Speicherung temporärer Daten. Gesamtzahl der Seiten = %1$d, Gesamtzahl der freien Seiten = %2$d.
29 Letzte außer-Speicher-Benachrichtigung für Datenträger "%1$s". Gesamtzahl der Seiten = %2$d, Gesamtzahl der freien Seiten = %3$d.
30 Letzte außer-Speicher-Benachrichtigung für Datenträger für Datenspeicherung. Gesamtanzahl der Seiten = %1$d, Gesamtanzahl der freien Seiten = %2$d.
31 Letzte außer-Speicher-Benachrichtigung für Datenträger für Indexspeicherung. Gesamtanzahl der Seiten = %1$d, Gesamtanzahl der freien Seiten = %2$d.
32 Letzte außer-Speicher-Benachrichtigung für Datenträger für Allgemeinspeicherung. Gesamtanzahl der Seiten = %1$d, Gesamtanzahl der freien Seiten = %2$d.
33 Letzte außer-Speicher-Benachrichtigung für Datenträger für Speicherung temporärer Daten. Gesamtanzahl der Seiten = %1$d, Gesamtanzahl der freien Seiten = %2$d.
34 Interner Fehler: die Setite %1$d konnte in Datei %2$d vom Datenträger "%3$s" nicht gefunden warden. Aktuelle Anzahl der Seiten ist %4$d.
35 Interner Fehler: Unbekannter Datenträger-ID %1$d.
36 Außer Speicher für die Datenbank. Anzahl der angeforderten Seite(n) = %1$d.
37 Außer Speicher in Datenträger "%1$s". Anzahl der angeforderten Seite(n) = %2$d.
38 Interner Fehler: Unbekannte Datei VFID %1$d|%2$d.
39 Interner Fehler: Seite %1$d|%2$d(Datenträger "%3$s") der Datei VFID %4$d|%5$d(Datenträger "%6$s") ist ungültig/freigegeben nach Zuordnungsliste.
40 Interner Fehler: %1$d erwartete Seiten, %2$d gefundene Seiten für Datei VFID %3$d|%4$d(Datenträger "%5$s").
41 Interner Fehler: Seite %1$d|%2$d(Datenträger "%3$s") gehört nicht zur Datei VFID %4$d|%5$d(Datenträger "%6$s").
42 Verankerte Datensätze on Seite %1$d des Datenträgers "%2$s" können nicht neu angeordnet werden.
43 Interner Fehler: Die Seite %1$d des Datenträgers "%2$s" ist möglicherweise beschädigt. %3$d Datensätze gefunden, %4$d erwartet.
44 Kein freier Speicher in Seite %1$d des Datenträgers "%2$s".
45 Slot %1$d auf Seite %2$d des Datenträgers "%3$s" ist mit einer verankerten Datensatz zugewiesen. Ein neuer Datensatz kann nicht hinzugefügt hier.
46 Interner Fehler: Slot %1$d auf Seite %2$d des Datenträgers "%3$s" ist nicht zugeordnet.
47 Kann eine Stapeldatei in Datenträger "%1$s" nicht erstellen.
48 Zugriff auf gelöschtem Objekt %1$d|%2$d|%3$d.
49 Interner Fehler: Klasse des Objekten %1$d|%2$d|%3$d ist unbekannt.
50 Interner Fehler: Umzugsdatensatz des Objekten %1$d|%2$d|%3$d ist möglicherweise beschädigt.
51 Interner Fehler: Objekt %1$d|%2$d|%3$d ist möglicherweise beschädigt.
52 Interner Fehler: Objekt Überlauf-Adresse %1$d|%2$d|%3$d ist möglicherweise beschädigt.
53 Holen des Objects %1$d|%2$d|%3$d wenn nur seine OID zugeordnet wurde.
54 Interner Fehler: Versuch, ein Objekt %1$d Seiten lang zu manipulieren. Die maximal zugelassene Größe eines Objects ist %2$d Seiten.
55 Interner Fehler: Ein Zirkelbezug wurde erkannt auf Seite %1$d|%2$d der Stapeldatei %3$d|%4$d|%5$d.
56 Interner Fehler: unbekannte erweiterbare Hash-Datei (Datentr.-Id: %1$d Datei-Id: %2$d) Seite (Datentr.-Id: %3$d Datei-Id: %4$d) wurde angegeben.
57 Schlüssel existiert in der erweiterbaren Hashing Struktur nicht.
58 String "%1$s" ist zu lang für eine Slot-Seite.
59 Interner Fehler: Der angegebenen Schlüsseltyp %1$d ist nicht gültig für die erweiterbare Hashing-Struktur.
60 Interner Fehler: die erweiterbare Hashing-Struktur wurde beschädigt.
61 Interner Fehler: die Root-Seite der erweiterbaren Hashing-Struktur (Datentr.-Id: %1$d Datei-Id: %2$d Seiten-Id: %3$d) wurde beschädigt.
62 Interner Fehler: Sortieren des Datensatzes  %1$d (Größe %2$d) passt in einer Seite nicht; die maximal zulässige Größe ist %3$d.
63 Interner Fehler: eine temporäre Seite wurde während des Sortierens beschädigt.
64 Unbekannte Klasse "%1$s".
65 Klasse "%1$s" existiert bereits.
66 Interner Fehler: unbekannter Vorgang %1$d für Objekt %2$d|%3$d|%4$d.
67 Interner Fehler: keine Stapeldatei wurde zugeordnet, um Objekt %1$d|%2$d|%3$d zu speichern.
68 Interner Fehler: unterschiedliche Klassennamen für Klasse mit OID = %1$d|%2$d|%3$d gefunden. Gefundene Namen sind "%4$s" (durch Hash-Tabelle der Klassennamen) und "%5$s" (durch Heap).
69 Interner Fehler: verschiedene Klass-Objekt-Identificatoren für Klasse mit Namen "%1$s" gefunden. Gefundene OIDS sind %2$d|%3$d|%4$d (durch Hash-Tabelle der Klassennamen) und %5$d|%6$d|%7$d (durch Heap).
70 Interner Fehler: Klasse mit Namen "%1$s" und OID = %2$d|%3$d|%4$d existiert in der Hash-Tabelle der Klassennamen nicht.
71 Interner Fehler: Klasse mit Namen "%1$s" und OID = %2$d|%3$d|%4$d existiert in ihrem Heap nicht.
72 Ihre Transaktion (Index %1$d, %2$s@%3$s|%4$d) wurde einseitig von dem System abgebrochen.
73 Ihre Transaktion (Index %1$d, %2$s@%3$s|%4$d) ist beim Warten auf dem %5$s Objektlock %6$d|%7$d|%8$d abgelaufen. Sie warten auf Benutzer(n) %9$s.
74 Ihre Transaktion (Index %1$d, %2$s@%3$s|%4$d) ist beim Warten auf dem %5$s Klassenlock %6$s abgelaufen. Sie warten auf Benutzer(n) %7$s.
75 Ihre Transaktion (Index %1$d, %2$s@%3$s|%4$d) ist beim Warten auf die %5$s lock auf Instanz %6$d|%7$d|%8$d der Klasse %9$s abgelaufen. Sie warten auf Benutzer(n) %10$s.
76 Ihre Transaktion (Index %1$d, %2$s@%3$s|%4$d) ist beim Warten auf die %5$s auf Seite %6$d|%7$d abgelaufen. Sie warten auf Benutzer(n) %8$s, die Seite zu entlocken.
77 Interner Fehler: alle Puffer sind fixiert.
78 Interner Fehler: I/O Fehler beim Lesen der logischen Protokollseite %1$lld (physische Seite %2$lld) von "%3$s".
79 Interner Fehler: I/O Fehler beim Schreiben der logischen Protokollseite %1$lld (physische Seite %2$lld) von "%3$s".
80 Zu wenig Speicherplatz im Betriebssystem Gerät beim Schreiben der logischen Protokollseite %1$lld (physische Seite %2$lld) von "%3$s". Konnte nicht mehr als %4$d Bytes schreiben.
81 Interner Fehler: logische Protokollseite %1$lld ist möglicherweise beschädigt.
82 Kann den Protokoll-Datenträger/-File "%1$s" nicht mounten.
83 Länge des Pfades "%1$s" plus Länge des Präfixes "%2$s" ist zu lang; die kombinierte Länge muss kleiner als %3$d sein.
84 Länge des Präfixes "%1$s" ist zu lang; die Länge muss kleiner als %2$d sein.
85 Der Präfixname "%1$s" ist nicht der gleiche wie "%2$s" vom Protokoll-Datenträger. Das Protokoll könnte außerhalb der Datenbank umbenannt worden sein.
86 Datenbank ist mit dem aktuellen "%1$s" Freigabe "%2$s".
87 Es gibt Wiederherstellungsaktionen, die mit "%1$s" Freigabe "%2$s" anstatt Freigabe "%3$s" ausgeführt werden müssen. Nach der Wiederherstellung kann die Datenbank auf Freigabe "%4$s" ausgeführt werden.
88 Interner Fehler: Release-String "%1$s" darf %2$d nicht überschreiten. Header Protokoll muss geändert werden.
89 Protokoll "%1$s" gehört zur angegebenen Datenbank nicht.
90 Redo-Logging muss immer an Seitenebene erfolgen. Ein Seitenzeiger muss als Teil der Adresse zugewiesen werden.
91 Postpone-Logging muss immer an Seitenebene erfolgen. Ein Seitenzeiger muss als Teil der Adresse zugewiesen werden..
92 Compensate-Logging muss immer an Seitenebene erfolgen. Ein Seitenzeiger muss als Teil der Adresse zugewiesen werden.
93 Warnung: Redo-Logging wird während Redo-Recovery und normale Rollbacks ignoriert.
94 Warnung: Postpone-Logging wird während Redo-Recovery und normale Rollbacks ignoriert.
95 Warnung: Undo-Logging wird während Rettung und normale Rollbacks ignoriert.
96 Medienrettung könnte für Datenträger "%1$s" benötigt werden.
97 Interner Fehler: kann Protokollseite %1$lld in den Protokollarchiv nicht finden.
98 Kann Archivprotokoll "%1$s", um Seiten von %2$lld bis %3$lld zu archivieren, nicht erstellen.
99 Die Sicherungsdatei "%1$s" konnte nicht erstellt werden.
100 Die Datenbank "%1$s" kann nicht gesichert werden.
101 Unbekannte Benutzerdatei "%1$s".
102 Datei "%1$s" hat genügend Einträge nicht: %2$d Einträge erwartet.
103 Datei "%1$s" scheint, ungeordnete Einträge zu haben. Eintrag %2$d mit Werten: %3$d %4$s %5$s wurde gefunden, während Eintrag mit Werten %6$d %7$s erwartet war.
104 Datei "%1$s" scheint, einen inkorrekten Eintrag für den Datenbanknamen zu haben (Hauptdatenträger). Eintrag %2$d mit Werten: %3$d %4$s %5$s wurde gefunden, während Eintrag mit Werten %6$d %7$s %8$s erwartet war.
105 Sicherungsdatei "%1$s" kann nicht zugegriffen werden. Widerherstellung oder Sicherung abgebrochen.
106 Die Verpflichtung der aktuellen Transaktion mit dem globalen Transaktionenkennzeichen %1$d kann nicht starten, weil dieses Transaktionenkennzeichen von einer anderen Transaktion benutzt wird.
107 Es gibt keine globale verteilte Transaktion mit der Transaktion Kennung %1$d assoziiert.
108 Aktuelle Transaktion %1$d muss verpflichtet oder abgebrochen werden, bevor dem Versuch, die verteilte Transaktion mit der globalen Kennung %2$d zu kombinieren.
109 Ihre Transaktion %1$d wurde einseitig von dem System abgebrochen, weil eine teilnehmende (ferne) Seite offline ging oder einseitig die Transaktion abgebrochen hat.
110 Isolationsstufe muss zwischen %1$d und %2$d sein.
111 Ihre Transaktion wurde durch das System aufgrund von Server-Ausfall oder Modus-Wechsel abgebrochen.
112 Host "%1$s" ist nicht berechtigt, die Datenbank auszuführen.
113 Starten/Initialisierung der Datenbankserver ist nicht erfolgt. %1$s
114 "%1$s" kann nicht als Datenbank interpretiert werden. Die Datenbankdatenträger/-dateien wurden möglicherweise umbenannt/kopiert außerhalb der Datenbankdomain.
115 Datenbank "%1$s" existiert bereits.
116 Datenbank "%1$s" ist unbekannt, oder die Datei "databases.txt" kann nicht zugegriffen werden.
117 Der absolute Pfad für die Datenbank ist zu lang. Die kombinierte Länge des Pfads "%1$s" plus der Name "%2$s" ist %3$d; die kombinierte Länge muss kleiner als %4$d sein.
118 Aktuelles Verzeichnis konnte nicht gefunden werden.
119 Der lokale Hostname konnte nicht gefunden werden.
120 Der maximale Zahl (%1$d) der Datenträger wurde überschritten.
121 Es wird versucht, einen permanenten Datenträger "%1$s" von der Datenbank zu entfernen.
122 Kann auf den Katalog der Systemmeldungen nicht zugreifen.
123 %1$s konnte für Datenbank "%2$s" nicht erstellt werden. Bitte beziehen Sie sich auf "%3$s" für weitere Informationen.
124 Datenträger "%1$s" existiert bereits.
125 Eine automatische Erweiterung des "%1$s" Datenträgers von %2$d Seiten generischer Lagerung wurde geschaffen.
126 Unbekannter Zweck "%1$s" on Zeile %2$d gegeben.
127 Inkorrekter Wert %1$d für die Anzahl der Seiten auf Zeile %2$d angegeben.
128 Anzahl der Seiten wurde nicht angegeben auf Zeile %1$d.
129 Unbekanntes Token "%1$s" wurde auf Zeile %2$d gefunden.
130 Außer virtuellem Speicher.
131 Datenbank-Systemfehler.
132 Diese Funktion ist in dieser Version: %1$s nicht implementiert.
133 Verschachtelter-Satz-Konstante nicht erlaubt.
134 Es ist nicht erlaubt, sowohl Klassenattribute und Non-Klassenattribute in einem einzigen Update zu verwenden.
135 Ungültige Anweisung für OID-Spalten: %1$s.
136 Funktion "%1$s" ist nicht implementiert.
137 Beschädigte Berechtigungsobjekte.
138 Berechtigungsklasse "%1$s" nicht gefunden.
139 Zugriffsfehler auf Berechtigungsattribut "%2$s" der Klasse "%1$s".
140 Die Operation "%1$s" darf nur vom DBA oder einem Mitglied der DBA-Gruppe durchgeführt werden.
141 Kann "%1$s" als Mitglied von "%2$s" nicht einfügen.
142 Durchs Einfügen dieses Mitglieds wird die Benutzerhierarchie ein zyklischer Graph.
143 Eine Klasse ohne Besitzer gefunden.
144 Kann Benutzerobjekt nicht zugreifen.
145 Sie können GRANT/REVOKE-Befehle nicht an sich selbst ausführen.
146 GRANT/REVOKE-Befehle dürfen an Besitzer einer Klasse nicht ausgeführt werden.
147 Keine GRANT Option.
148 Das Schreib-Lock aufs Berechtigungsobjekt kann nicht gesetzt werden.
149 Die Instanz des Berechtigungsobjekts kann nicht erstellt werden.
150 Sie können ihre Privilegien nicht selber widerrufen.
151 Privilegien eines Klassenbesitzers dürfen nicht widerrufen werden.
152 GRANT nicht gefunden.
153 Keine Berechtigungsprivilegien wirken auf die Datenbank.
154 Unvollständige Berechtigungsinstallation - keine Berechtigungsprivilegien wirken auf die Datenbank.
155 Der Inhalt der Berechtigungsklasse ist ungültig.
156 Berechtigungsfehler.
157 SELECT Berechtigungsfehler.
158 ALTER Berechtigungsfehler.
159 UPDATE Berechtigungsfehler.
160 INSERT Berechtigungsfehler.
161 DELETE Berechtigungsfehler.
162 INDEX Berechtigungsfehler.
163 EXECUTE Berechtigungsfehler.
164 Benutzer "%1$s" existiert bereits.
165 Benutzer "%1$s" ist ungültig.
166 Ungültiger Benutzer angegeben.
167 DBA, Mitglieder der DBA Gruppe und Klassenbesitzer dürfen diese Operation durchführen.
168 Mitglied nicht gefunden.
169 Der Benutzer %1$s kann von der Datenbank nicht entfernt werden.
170 Kein Benutzer angemeldet.
171 Fehlerhafter oder fehlender Kennwort.
172 Die Kennwort-Zeichenkette darf nicht mehr als 31 Bytes lang sein.
173 Datenbankdatei "%1$s" nicht gefunden.
174 Schreibender Zugriff auf die Datenbankdatei "%1$s" nicht erhalten.
175 Schlecht formatierte Datenbankdatei "%1$s".
176 Umrechnungsfehler im Datumsformat.
177 Großes Objekt für die interne Speicherung konnte nicht erstellt werden.
178 Temporäres OID ohne MOP gefunden.
179 Domain "%1$s" ist mit der Domain "%2$s" nicht kompatibel.
180 Domain "%1$s" ist mit der Zieldomain "%2$s" nicht kompatibel.
181 Kann den Wert der Domain "%1$s" nicht auf die Domain "%2$s" forcieren.
182 Kann den Wert der Domain "%1$s" nicht auf die Domain "%2$s" ohne Überlauf forcieren.
183 Unerwartete Menge der erhaltenen Daten; %1$d erwartet, %2$d erhalten.
184 Kommunikationspuffer kann nicht zugeordnet werden.
185 Fehler beim Datenempfang vom Kunden.
186 Fehler beim Datenempfang vom Server.
187 Kommunikationspuffer nicht benutzt.
188 Unbekannte Datenbank "%1$s".
189 Ungültiger Hostname "%1$s".
190 Server-Host nicht identifiziert.
191 Die Verbindung mit dem Server "%1$s" auf "%2$s" erfolgt nicht.
192 Die Netzwerk-Konfigurationsdatei konnte nicht geladen werden.
193 Der Server hat ein Herunterfahren-Befehl vom Kunden erhalten.
194 Unbekannter Server-Request-ID %1$d.
195 Server Kommunikationsfehler: %1$s.
196 Server-Name nicht identifiziert.
197 Verbindung mit Master-Server konnte nicht erstellt werden.
198 Empfangspuffer zu klein, Daten wurden abgeschnitten.
199 Der Server antwortet nicht mehr.
200 Das Set wurde nicht ordnungsgemäß vom Objekt entfernt.
201 Ungültiger UNIQUE-Tabelleneintrag entfernt.
202 Attribut "%1$s" wurde nicht gefunden.
203 Der Wert des Attributs "%1$s" muss "%2$s" sein, und nicht "%3$s".
204 Funktion mit fehlenden oder ungültigen Argumenten aufgerufen.
205 Attribut "%1$s" darf nicht NULL sein.
206 Attribut "%1$s" hat keine UNIQUE-Einschränkung.
207 Klasse "%1$s" enthält ein Null-size-Objekt.
208 Methode "%1$s" nicht gefunden.
209 Kein Attribut oder Methode mit Namen "%1$s" gefunden.
210 Interner Fehler: Verarbeitung der Objektvorlage.
211 Das Attribut wurde bei der Erstellung der Objektvorlage gelöscht.
212 Attribut "%1$s" übertretet die UNIQUE-Einschränkung.
213 Ungültige UNIQUE-Tabelleneingang gefunden.
214 Auf Attribut "%1$s" besteht ein Domain-Konflikt.
215 Attribut mit ID %1$d nicht gefunden.
216 Ungültiges Objektbesitzkennzeichen.
217 Diese Operation kann nur auf Klassenobjekte durchgeführt werden.
218 Ungültiges Objekt in dem db_get-Ausdruck.
219 Ungültiger db_get-Pfad-Ausdruck.
220 Ungültiges Set in dem db_get-Pfad-Ausdruck.
221 Ungültiger Set-Index in dem db_get Pfad-Ausdruck.
222 Es wurde versucht, ein String größer als %2$d Zeichen an Attribut "%1$s" zuzuweisen.
223 Der Wert %2$d ist zu groß für das smallint-Attribut "%1$s".
224 Die Datenbank wurde nicht neugestartet.
225 Fehlender Wert für Attribut "%1$s" mit der NOT-NULL-Bedingung.
226 Leere Liste der Komponente in der Klasse.
227 Es wurde versucht, dem Attribut "%1$s" einen Wert mehrmals zuzuweisen.
228 Die Methode "%1$s" wurde mit %2$d Argumenten aufgerufen. Das System erlaubt ein Maximum von %3$d Argumenten.
229 Die Methode "%1$s" setzt voraus, dass das Argument %2$d innerhalb der Domain "%3$s" ist. Eine ungültige Domain "%4$s" wurde gegeben.
230 Dem Attribut "%1$s" kann ein String mit Länge %2$d nicht zugewiesen werden. Die maximal erlaubte String-Länge ist %3$d.
231 Ungültige API-Operation für ein temporäres Objekt.
232 Es ist nicht erlaubt, Objektvorlagen über mehrere Transaktionen zu benutzen.
233 Die Klasse mit dem Namen "%1$s" konnte nicht erstellt werden.
234 Methodendatei "%1$s" nicht gefunden.
235 %1$d nicht aufgelöste Methodenfunktionen nach der Linksetzung.
236 Nicht aufgelöste Methode "%1$s".
237 Schwerwiegende Probleme während dynamischer Verknüpfung angetroffen.
238 Methodendatei "%1$s" konnte nicht zugegriffen werden.
239 Attribut "%1$s" nicht gefunden.
240 Methode "%1$s" nicht gefunden.
241 Es gibt kein Attribut oder Methode mit Namen "%1$s".
242 Unterschrift für %2$s für Methode "%1$s" nicht gefunden.
243 Argument %2$d der Methode "%1$s" ist nicht definiert.
244 Der Domainnname "%1$s" ist nicht der Name einer Klasse oder Grundtyp.
245 Der Name "%1$s" wurde schon als Attributname benutzt.
246 Der Name "%1$s" wurde schon als Methodenname benutzt.
247 Ungültige Argumente für die Schema-Manager interne Funktion.
248 Die Attributdomain "%1$s" ist für die Verwendung mit der %2$s Einschränkung nicht geeignet.
249 Der angeforderte Vorgang auf der Klasse "%1$s" kann nur bevor der Instanziierung durchgeführt werden.
250 Der Index fürs Attribut "%1$s", definiert mit einem geteilten Wert, konnte nicht erstellt werden.
251 Unterschrift %2$s existiert bereits für Methode "%1$s".
252 Attribut "%1$s" muss für diese Operation eine SET-Domain haben.
253 Attribut "%1$s" kann nicht mit einem verschachtelten Satz Domain definiert werden.
254 Domain "%2$s" kann von Attribut "%1$s" nicht entfernt werden.
255 Attribute "%1$s" gehört zu einer Domain variabler Größe nicht.
256 Superklasse existiert bereits.
257 Superklassenaddition würde zu Zyklen in der Klassenhierarchie führen.
258 Superklasse nicht gefunden.
259 Mehrere Unterschriften existieren für Methode "%1$s".
260 Das Argument %2$d der Methode "%1$s" gehört zu einer Set-Domain nicht.
261 Resolution für "%1$s" nicht gefunden.
262 Domainnwert "%2$s" ist nicht für das Attribut "%1$s" geeignet.
263 Schema-Manager Korruption festgestellt.
264 Alias "%2$s" für Attribut/Methode "%1$s" ist nicht einzig.
265 Schadowing-Attribut "%1$s" mit widersprüchlicher Domain "%2$s".
266 Namenkonflikt des Attributs "%1$s" mit Klassen "%2$s" und "%3$s".
267 Shadowing-Methode "%1$s" mit widersprüchlicher Unterschrift.
268 Namenkonflikt der Methode "%1$s" mit Klassen "%2$s" und "%3$s".
269 Der Index der Domain "%1$s" kann nicht definiert werden.
270 UNIQUE-Einschränkung auf Domain "%1$s" kann nicht definiert werden.
271 Zyklus in Klassenhierarchie festgestellt; "%1$s" ist eine Superklasse von "%2$s".
272 Index "%2$s" bereits definiert für Klasse "%1$s".
273 Index "%1$s" existiert nicht.
274 Ungültige Zeichen im Namen: "%1$s".
275 Ungültige Operation fürs Attribut "%1$s", geerbt von Klasse "%2$s".
276 Ungültige Operation für Methode "%1$s", geerbt von Klasse "%2$s".
277 Ungültige Operation für "%1$s", geerbt von Klasse "%2$s".
278 Klasse "%4$s" hat inkompatible Domain für "%1$s", geerbt von Klassen "%2$s" und "%3$s".
279 In Klasse "%4$s", "%1$s" geerbt von Klasse "%2$s" kann nicht wie angefordert verwendet werden. Die Definition in der Klasse "%3$s" hat eine spezifischere Domain.
280 In Klasse "%3$s", "%1$s" kann nicht von Klasse "%2$s" geerbt wird, weil es eine lokale Definition gibt, die eine inkompatible Domain hat.
281 In Klasse "%3$s", "%1$s" geerbt von Klasse "%2$s" ist ein Alias aber kein Ersatz von einer anderen Quelle ist gefunden.
282 In Klasse "%4$s", "%1$s" geerbt von Klasse "%3$s" kann nicht als Alias Ersatz für Klasse "%2$s" verwendet werden. Die Domains sind inkompatibel.
283 In Klasse "%4$s", "%1$s" geerbt von Klasse "%3$s" kann nicht als Alias Ersatz für Klasse "%2$s" verwendet werden. Es hat eine weniger spezifische Domain.
284 Vererbung von "%1$s" kann nicht angefordert werden, weil es lokal in der Klasse definiert ist.
285 "%1$s" kann nicht als Alias für die geerbte Komponente "%2$s" verwendet werden, weil es schon ein lokal definierte Komponente mit demselben Namen gibt.
286 "%1$s" kann nicht als Alias für "%2$s" von "%3$s" verwendet werden. Eine Komponente mit diesem Namen ist schon von "%4$s" geerbt.
287 In Klasse "%1$s", das Attribute "%2$s" kann nicht definiert werden, weil es eine von "%3$s" geerbte Methode überschreiben (abschatten) würde.
288 In Klasse "%1$s", die Methode "%2$s" kann nicht definiert werden, weil sie ein von "%3$s" geerbtes Attribut überschreiben (abschatten) würde.
289 Subklasse "%1$s" kann die Methode "%2$s" von Klasse "%3$s" nicht vererben, weil es schon ein Attribut mit diesem Namen gibt.
290 Subklasse "%1$s" kann das Attribut "%2$s" von Klasse "%3$s" nicht vererben, weil es schon eine Methode mit diesem Namen gibt.
291 "%1$s" ist in Klasse "%2$s" als Attribute und in Klasse "%3$s" als Methode definiert.
292 Keine ‚populate‘ Spezifikation mit Index "%1$s".
293 Operation nicht erlaubt für diesen Klassentyp.
294 Nicht definierte Umgebungsvariable "%1$s" referenziert in der Methodendatei.
295 Klasse "%1$s" kann nicht geändert werden. Der Systemkatalog ist außer Speicher. Um Speicher zu gewinnen, komprimieren Sie die Datenbank mit der Compactdb-Utility.
296 Ungültige Property-Liste angetroffen.
297 Alias Name "%1$s" ist mehr als einmal verwendet.
298 In Klasse "%1$s", eine Resolution für "%2$s" von "%3$s"' ist nicht erlaubt. Diese Klasse ist keine Superklasse.
299 "%1$s" ist ein reservierter Wort und kann nicht als Attribut-, Methoden- oder Klassenname verwendet werden.
300 Attribut "%1$s" darf nicht mit sowohl DEFAULT als auch UNIQUE Einschränkungen zusammen definiert werden.
301 Attribut "%1$s" darf nicht mit einer Längeneinschränkung von %2$d definiert werden. Die maximale Längeneinschränkung ist %3$d.
302 Kann Element in den Set nicht einfügen.
303 Der Wert existiert schon im Set.
304 Set-Index %1$d ist außerhalb des zulässigen Bereichs.
305 Sequenz-Index %1$d ist außerhalb des zulässigen Bereichs.
306 Angeforderte Operation kann nur in Sequenzen durchgeführt werden.
307 Angeforderte Operation kann nur in Sets durchgeführt werden.
308 Elementwert ist nicht innerhalb der Domain des Sets.
309 Illegales Set-Element-Index gegeben: %1$d.
310 Element im Set nicht gefunden.
311 Element im Sequenz nicht gefunden.
312 "%1$s" ist keinen validen Set-Domain.
313 Objekt-Pufferunterlauf während dem Lesevorgang.
314 Objekt-Pufferüberlauf während dem Schreibvorgang.
315 Illegale Metaklassendefinition angetroffen.
316 Versatz in Umwandlungsberechnung, %1$d erwartet, %2$d berechnet.
317 Ungültige Diskeintrag für Klasse "%1$s" angetroffen.
318 Out of sync beim Objektladen. Datenbank ist möglicherweise beschädigt oder veraltet.
319 Unbekannte Attributerweiterung wird ignoriert.
320 Beschädigter Diskeintrag des Objekts angetroffen.
321 Mögliche Beschädigung des Arbeitsbereichs erkannt.
322 MOP in dem Arbeitsbereich nicht gefunden.
323 Es wird versucht, ein nicht temporäres MOP in dem Arbeitsbereich zu aktualisieren.
324 Ein Objekt wurde im Arbeitsbereich ohne Klasse gefunden.
325 Es wurde Versucht, ein MOP in die Schrottliste einzuführen.
326 Klassenzeiger für Objekt im Arbeitsbereich gewechselt.
327 MOP kann mit NULL OID nicht erstellt werden.
328 Es wurde eine Instanz ohne Klasse im Arbeitsbereich angetroffen.
329 Link zur Objektliste nicht zugeordnet im Arbeitsbereich.
330 Arbeitsbereich Pin-Verletzung.
331 Arbeitsbereich konnte nicht von OS zugeordnet werden, außer virtuellem Speicher. Transaktion abgebrochen.
332 Schätzung der Arbeitsbereich Größe überschritten. Eine automatische Erweiterung wurde gemacht.
333 Außer Speicher bei der Arbeitsbereich-Zuordnung. Transaktion abgebrochen.
334 Versuchte Zuordnung von %1$d Bytes ist größer als die maximalle Block Größe. Versuchen Sie das max_block_size Parameter anzupassen. Transaktion abgebrochen.
335 Der Versuch, ein im Arbeitsbereich nicht zugeteiltes Objekt zu befreien, wurde ignoriert.
336 Der Versuch, ein Objekt mehrmals zu befreien, wurde ignoriert.
337 Der Versuch, ein Objekt mit negativer Größe zu zuteilen, wurde ignoriert.
338 Map-Eintrag-Kennung überrannt.
339 Der kritische Abschnitt kann nicht verlassen werden, da es nicht durch die Transaktion besitzt wird %1$d.
340 Unterprozess kann nicht aufgespaltet werden.
341 Unterprozess kann nicht ausgeführt werden.
342 Prozessgruppe kann nicht geändert werden.
343 Request_id ist bereits im Einsatz.
344 %1$s.
345 Fehler aufgetreten beim Lesen einen Shutdown-Meldung vom Master.
346 Fehler aufgetreten beim Lesen einen Stopp Shutdown-Meldung vom Master.
347 Es konnte keine Leitung zum Master-Server erstellt werden.
348 Entweder Dienst- oder Port-ID angegeben werden.
349 Unbekannter Dienst für TCP: %1$s.
350 Fehler beim Abrufen der lokalen Host durch Namen für Host "%1$s". Host nicht gefunden.
351 TCP-Socket konnte nicht erstellt werden.
352 tcp_open: konnte keine reservierte TCP-Port erhalten.
353 Verbindung zum Master-Server auf Host "%1$s" kann nicht erstellt werden.
354 Neuer Prozessbesitzer für Socket konnte nicht gesetzt werden.
355 Socket-Stream konnte nicht erstellt werden.
356 '%1$s' Datei für UNIX Domain Socket existiert.
357 Lokale Adresse kann nicht gebunden werden... Abbruch.
358 Accept gescheitert.
359 Datagram Bind-Fehler.
360 Datagram Accept-Fehler.
361 Datagram Verbindungsfehler.
362 Datagram Socket-Fehler.
363 Recvmsg von Master-Socket gescheitert.
364 Ein neuer Request wird an Server geschickt.
365 Eine Broadcast-Meldung wird an Client gesendet.
366 Fehler von Server: %1$s
367 Server %1$s existiert bereits.
368 Kommunikationsfehler bei der Verbindung für %1$s.
369 Unbenutzter Fehler Code.
370 Unbenutzter Fehler Code.
371 Unbenutzter Fehler Code.
372 Unbenutzter Fehler Code.
373 Vorzeitige EOF beim Lesen: %1$d.
374 Vorzeitige EOF beim Lesen von %1$d oder %2$d.
375 Konvertierungsfehler auf (0x%1$c%2$c) %3$d und %4$d konvertiert zu %5$d.
376 Keine öffentliche Klasse gefunden.
377 Fehler %1$d: Attribute "%2$s" konnte zur Multimedia-Hierarchie nicht hinzugefügt werden.
378 Fehler %1$d: Methode "%2$s" implementiert wie "%3$s" konnte zur Multimedia-Hierarchie nicht hinzugefügt werden.
379 Fehler %1$d: Superklasse konnte zur Multimedia-Hierarchie nicht hinzugefügt werden.
380 Dynamischer Lader schon initialisiert.
381 Dynamischer Lader nicht initialisiert.
382 Dynamischer Lader falsch initialisiert.
383 "%1$s" ist kein Objekt oder Archiv.
384 Absoluter Pfad für "%1$s" konnte nicht bestimmt werden.
385 ld-Prozes ist mit Code %1$d beendet.
386 ld-Prozes ist wegen Signal %1$d beendet.
387 ld-Prozes ist mit unbekanntem waitval 0x%1$x beendet.
388 Initialbild für "%1$s" kann nicht zugegriffen werden.
389 Systemfehler: %1$s.
390 Problem mit "%1$s": %2$s.
391 Konflikt mit einem anderen SIGPIPE Handler.
392 Dynamischer Loader-Daemon kann nicht gestartet werden.
393 Dynamischer Loader-Daemon ist verschwunden.
394 Endpunkt Reichweite nicht zwischen 0 und 256.
395 Ungültiger Teilausdruck angegeben.
396 Oktalziffer außerhalb des Bereichs in Escape.
397 Regulärer Ausdruck nicht richtig begrenzt.
398 Textsuche aufgerufen, bevor die Expression kompiliert wurde.
399 Eine alleinstehende Parenthese im regulären Ausdruck gefunden.
400 Klammern zu tief in den regulären Ausdruck verschachtelt.
401 Mehr als zwei Zahlen in einem Bereich angegeben.
402 Eine alleinstehende Akkolade im regulären Ausdruck gefunden.
403 Die erste Nummer im Bereichsausdruck ist größer als zweiter.
404 Eine alleinstehende Klammer im regulären Ausdruck gefunden.
405 Der reguläre Ausdruck war zu lang zu kompilieren.
406 Ungültiges B+tree Index Identifier: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
407 Unbekannter Schlüssel %1$s referenziert in B+tree Index {vfid: (%2$d, %3$d), rt_pgid: %4$d, key_type: %5$s}.
408 Unbekannter referenzierter Wert (oid) %1$d|%2$d|%3$d.
409 Doppelter referenzierter Wert (oid) %1$d|%2$d|%3$d.
410 Null-Schlüssel referenziert.
411 Ungültiger B+tree Index Schlüsseltyp "%1$s".
412 Ungültige Bereichssuche.
413 Unbekannter Attribut Identifier: %1$d.
414 Unbekannter Klassen-Identifier: %1$d|%2$d|%3$d.
415 Ungültiger Klassen-Identifier: %1$d|%2$d|%3$d.
416 Unbekannter Darstellung-Identifier: %1$d.
417 Ungültiger Darstellung-Identifier: %1$d.
418 Aufnahme-Deskriptor hat nicht genügend Speicher: %1$d.
419 Klasse hat die maximale Zahl Darstellungen erreicht: %1$d.
420 Klasse %1$d|%2$d|%3$d hatte bereits Darstellungen.
421 Darstellungsverzeichnis fehlt im Katalog für Klasse: %1$d|%2$d|%3$d.
422 Darstellungsaufnahme fehlt im Katalog für Klasse - Repr_Id: %1$d|%2$d|%3$d - %4$d.
423 Ungültige Session.
424 Keine Anweisung zur Ausführung.
425 Inkompatibler Datentyp für Attribut "%1$s" der Klasse "%2$s".
426 Inkompatibler Datentype für Attribut "%1$s".
427 Datenüberlauf für Datentyp "%1$s".
428 Anweisung ist nicht aktualisierbar.
429 Unbekannter Befehl.
430 Unbekannte Variable "%1$s".
431 Fehler in SQL/X Interpreter oder Parser.
432 Unbekannte Klasse "%1$s".
433 Unbekannter Attribut "%1$s" in Klasse "%2$s".
434 Die richtigen Spaltennamen aus einer Abfrage können nicht gefunden werden.
435 Mehrere Anweisungen sind nicht erlaubt.
436 Die Anweisung ist keine Abfrage.
437 Ungültiges langes Objekt-ID (%1$s,%2$d, %3$s, %4$s).
438 Es besteht ein Konflikt in dem langes Objekt Deskriptor.
439 Offset %1$d ist größer als die Länge des langen Objects %2$d.
440 Ungültige Cursor-Position.
441 Ungültige Cursor-Operation.
442 Unbekannte Cursor-Position.
443 Tupel WertIndex %1$d ist außerhalb des Bereichs.
444 Ungültiger Attributname: "%1$s".
445 WertelisteIndex %1$d ist außerhalb des Bereichs.
446 Es gibt Abfrageergebnisse durch den Transaktionenmanager, die aber durch die Anwendung nicht geschlossen sind. 
447 Es wurde versucht, eine Abfrage-Ergebnisstruktur .
448 Tupelgrößen sind derzeit auf eine Seite begrenzt.
449 Unbekannter Anfrage-Identifier: %1$d.
450 Ungültiger Set-Operator: %1$d.
451 Heap-Datei ohne Instanzen.
452 Ungültige XASL Baumknoteninhalte.
453 Keine Spezifikationsknoten mehr.
454 Ungültiger Datentyp wurde referenziert.
455 Keine Abfrage-Datei Seiten verfügbar.
456 Datentyp Referenzen sind inkompatibel.
457 Ungültiges Anfrageergebnis.
458 Überlauf in Additionskontext aufgetreten.
459 Anfrageergebnis enthält mehr als ein einziges Tupel. 
460 Zu wenige Eingabevariablen angegeben.
461 Mehr Host-Variablen als Ergebnisspalten angegeben.
462 Ergebniswert ist NULL, aber es gibt keine Indikatorvariable.
463 Cursoranweisung ist noch nicht erstellt.
464 Der Cursor sollte eine SELECT Anweisung.
465 Der Cursor ist noch nicht geöffnet.
466 Der Cursor sollte erst geschlossen bevor wieder geöffnet werden.
467 Das Ergebnis sollte ein einziges Objekt enthalten.
468 Keine positionelle Marker in EXECUTE IMMEDIATE Anweisungen erlaubt.
469 Ungültiger Datentyp wurde benutzt.
470 Die Anfrage Anfrage beinhaltet zu viele Elemente für den Optimierer.
471 Außer Speicher in Zeile %1$d der Datei %2$s.
472 %1$s Connector für die lokale Datenbank '%2$s' auf Host '%3$s' ist gestorben.
473 Bericht von lokaler Datenbank '%1$s' auf Host '%2$s': %3$s.
474 %1$s Fehler während %2$s erkannt.
475 Keine Abfragespezifikation mit Index %1$s.
476 LDB %1$s existiert bereits.
477 Es konnte keine Verbindung zum Master auf dem Host %2$s erstellt werden, um LDB %1$s zuzugreifen.
478 Die lokale Datenbank "%1$s" ist keine registrierte LDB.
479 Die lokale Datenbank "%1$s" wird derzeit von anderen Clients zugegriffen, Veränderung oder Löschen ist nicht erlaubt.
480 Die lokale Datenbank "%1$s" ist nicht leer und kann nicht gelöscht werden.
481 "%1$s" ist keine Proxy-Virtuelle-Klasse.
482 "%1$s" ist keine virtuelle Klasse.
483 "%1$s" ist kein Attribut der Klasse "%2$s".
484 Das object_id für die Proxy-Virtuelle-Klasse "%1$s" ist bereits gesetzt.
485 Eines oder mehrere object_id Attribute sind NULL für die Instanz der Proxy-Klasse "%1$s".
486 Das Objekt ist keine aktualisierbare Instanz der virtuellen Klasse.
487 Das Objekt ist keine Instanz.
488 Ein Attribut einer Klasse darf keine virtuelle oder Proxy-Virtuelle-Klasse sein.
489 Ein Attribut einer Proxy-Virtuellen-Klasse darf keine Klasse, virtuelle Klasse oder Objekt sein.
490 Die Superklasse "%1$s" muss der selben Klassentyp wie "%2$s" sein.
491 Einheit-Namen kann von Proxy-Anfragespezifikation nicht erhalten werden "%1$s".
492 %1$s
493 Syntax: %1$s %2$s
494 Semantik: %1$s %2$s
495 Ausführen: %1$s %2$s
496 Unbekannter Schlüssel wurde referenziert.
497 Unbekannter Wert (oid) %1$d|%2$d|%3$d wurde referenziert.
498 Doppelter Wert (oid) %1$d|%2$d|%3$d wurde referenziert.
499 Null-Schlüssel wurde referenziert.
500 Doppelter Wert wurde referenziert.
501 Ungültige Trigger-Priorität.
502 Fehlende Zielklasse im Trigger.
503 Trigger "%1$s" nicht gefunden.
504 Interner Fehler: Triggerverarbeitung "%1$s".
505 Ein Trigger mit Namen "%1$s" existiert bereits.
506 Ein Trigger kann auf "%1$s" nicht definiert werden, weil es um eine virtuelle Klasse handelt.
507 Unangemessene Trigger-Zielklasse "%1$s".
508 Unangemessene Trigger-Zielattribut "%1$s".
509 Ungültiges Trigger-Kondition "%1$s".
510 Ungültige Trigger-Aktion "%1$s".
511 Nicht berechtigt, Trigger "%1$s" zuzugreifen.
512 Nicht berechtigt, Trigger "%1$s" zu löschen.
513 Nicht berechtigt, Trigger "%1$s" zu aktualisieren.
514 Nicht berechtigt, Trigger "%1$s" zu ändern.
515 Die Ausführungszeit der Trigger-Aktion %2$s kann nicht früher als die Ausführungszeit der Kondition %1$s.
516 Maximale Trigger-Tiefe %1$d überschritten bei Trigger "%2$s".
517 Die Operation wurde von Trigger "%1$s" abgelehnt.
518 Interner Fehler: Tiggerverarbeitung "%1$s".
519 Ungültige Trigger-Kondition.
520 Die REJECT-Aktion darf mit der Einwirkzeit AFTER oder DEFERRED nicht verwendet werden.
521 Die REJECT-Aktion dar mit ABORT oder TIMEOUT Ereignisse nicht verwendet werden.
522 Trigger-Kondition Ausdruck nicht angegeben.
523 Trigger-Aktion Ausdruck nicht angegeben.
524 Die angegebene Aktivität kann nicht entfernt werden, das sie zu einem anderen Benutzer gehört.
525 Fehler bei der Kompilierung der Kondition für "%1$s": %2$s
526 Fehler bei der Kompilierung der Aktion für "%1$s", %2$s
527 Fehler bei der Auswertung der Kondition für "%1$s": %2$s
528 Fehler bei der Auswertung der Action für "%1$s", %2$s
529 Transaktion kann nicht festgeschrieben werden, da es vom Trigger "%1$s" ungültig gemacht wurde.
530 Regulärer Ausdruck fehlt.
531 Regulärer Ausdruck ist zu lang oder zu komplex.
532 Alleinstehende Parenthese oder Klammer im regulären Ausdruck.
533 Referenz in regulärem Ausdruck ist außer zulässigem Bereich.
534 Ungültiges Zeichen oder Syntax in regulärem Ausdruck.
535 Kein Text für Suche mit regulärem Ausdruck angegeben.
536 Ungültiges opcode im regulären Ausdruck gefunden.
537 Ungültiger Suchbefehl für regulären Ausdrucken Compiler.
538 Der Arbeitspuffer für reguläre Ausdrucke muss initialisiert werden.
539 Es wurde versucht, durch Null zu teilen.
540 %1$s.
541 Die Anzahl der freien Seiten der Datenträger "%1$s" ist inkorrekt. %2$d und %3$d wurden gemäß Datenträger-Header und -Bitmap, dementsprechend gefunden.
542 Anzahl der freien Sektoren der Datenträger "%1$s" ist inkorrekt. %2$d und %3$d wurden gemäß Datenträger-Header und -Bitmap, dementsprechend gefunden.
543 Disk-Header für Datenträger "%1$s" ist inkorrekt.
544 Interner Fehler: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). B+tree Schlüssel %6$s Eintrag für Objekt OID: %7$d|%8$d|%9$d wurde im B+tree: %10$d|%11$d|%12$d nicht gefunden.
545 Interner Fehler: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Schlüssel und OID: %6$d|%7$d|%8$d Eintrag in B+tree: %9$d|%10$d|%11$d ist inkorrekt. Das Objekt existiert nicht.
546 Interner Fehler: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). %6$d OID Werte erwartet, %7$d waren aber in B+tree: %8$d|%9$d|%10$d gefunden.
547 Server Release %1$s ist unterschiedlich von Client Release %2$s.
548 Kein object_id für Proxy-Klasse "%1$s" definiert.
549 Proxy-Instanz nicht gefunden.
550 Unbekannter Sicherungspunkt %1$s
551 Datenträger Dateipfad "%1$s" konnte nicht gefunden werden... Lesevorgang aus internen Tabellen wird fortgesetzt. 
552 Datumsberechnungen Unterlauf.
553 Zeitberechnungen Unterlauf.
$ LOADDB
554 Der Ladeprogramm ist in einen ungültigen Zustand gegangen. Keine weiteren Operationen sind möglich.
555 Speicherzuteilungsfehler während des Ladens der Datenbank.
556 Zu viele Werte angegeben. %1$d erwartet.
557 Typ %1$s ist für Attribut %2$s von %3$s im Domain des Sets nicht erlaubt.
558 Typ %1$s erwartet für Attribut %2$s von %3$s, eine Set-Konstante wurde angegeben.
559 Type %1$s erwartet für Attribut %2$s von %3$s, %4$s wurde angegeben.
560 Domain ist nicht spezifisch genug für Attribut %1$s von %2$s\n um uneingeschränkten Objektreferenzen zu unterstützen.
561 Verschachtelte Sets sind nicht erlaubt.
562 Systemklasse %1$s kann nicht mit dem Datenbankladeprogramm aufgefüllt werden.
563 Referenz zu interner Klasse %1$s auf NULL konvertiert.
564 Die UNIQUE Einschränkung für Attribut %1$s von %2$s wurde verletzt.
565 Konstruktor-Methode %1$s ist in Klasse %2$s nicht definiert.
566 Keine Klasse festgelegt, oder kein Attribut in der Klasse.
567 Zu viele Konstruktor übergebenen Argumente. %1$d erwartet.
568 Fehlende Konstruktor-Parameter. %1$d erwartet, %2$d gefunden.
569 Fehlende Attributwerte.  %1$d erwartet, %2$d gefunden.
570 Glo Datei "%1$s" kann nicht zugegriffen werden.
571 Instanz wurde zuvor durch eine vorausschauende Referenz erstellt.\n Dies ist für mit Konstruktor Methoden erstellten Instanzen nicht erlaubt.
572 INTERNER FEHLER: Disk-Darstellung der Instanz kann nicht erstellt werden.
573 INTERNER FEHLER: Neue Instanz kann in die Datenbank nicht eingefügt werden.
574 INTERNER FEHLER: Neue Instanz kann in der Datenbank nicht aktualisiert werden.
575 Typ %3$s erwartet für Argument %1$d des Konstruktors %2$s, %4$s angegeben.
576 Referenz zur Klasse %3$s ist mit dem Domain des %1$s des %2$s nicht kompatibel.
577 Domain ist nicht spezifisch genug für Argument %1$d des Konstruktors %2$s, \n um unqualifizierte Objektreferenzen zu unterstützen.
578 Referenz zur Klasse %3$s is mit dem Domain des Argumente %1$d des Konstructors %2$s \n nicht kompatibel.
579 Zieldomain muss den „Objekt“-Typ enthalten, um Referenzen zu Klassen zu erlauben.
580 Attribut %1$s ist für Klasse %2$s nicht definiert.
581 Es wurde versucht, die Datenbank zu aktualisieren, wenn Aktualisierungen deaktiviert sind. 
582 Unbekannter Lagerungszweck %1$d. Der gültige Bereich ist von %2$d zu %3$d.
583 Es wurde versucht, eine ungültige Anzahl (%1$d) von Seiten zuzuordnen.
584 INTERNER FEHLER: Eine Schleife wurde in der Dateitabelle %1$d in Datenträger "%2$s". Die Schleife könnte durch Seite %3$d|%4$d verursacht werden.
585 Unbekanntes Heap %1$s|%2$d|%3$d
586 INTERNER FEHLER: Kann Chain-Header in VPID %1$d|%2$d für Heap %3$d im Datenträger "%4$s" nicht aktualisieren.
587 Einträge permanenter Datenträger sind unsortiert in Ihre "%1$s" volinfo Datei. Eintrag %2$d: %3$d %4$s achtet die Reihenfolge nicht.
588 INTERNER FEHLER: Assertion '%1$s' gescheitert.
589 Ungültiger Benutzer "%1$s".
590 Das Domain  "%1$s" des Attributs einer Klasse darf keine virtuelle \noder Proxy-Virtuelle-Klasse sein.
591 Das Domain "%1$s" des Attributs einer Proxy-Virtuellen-Klasse muss \neine Proxy-Virtuelle-Klasse sein.
592 Das Domain "%1$s" des Attributs einer Proxy-Virtuellen-Klasse \nund die Proxy-Virtuelle-Klasse müssen in der gleichen lokalen Datenbank sein.
593 Ein nicht-aktualisierbare virtuelle Instanz wurde zurückgewonnen und kann nicht wieder aufgebaut werden.
594 Ein object_id Klausel muss ohne Attribute (%1$s) ungefährer Datentypen (float oder double) geliefert werden.
595 Der Datenbanknamen "%1$s" ist zu lang. Der Name soll weniger als %2$d Zeichen lang sein.
596 Die %1$d Seiten des gesamten temporären Speichers sind überschritten worden.
597 Die Seitenanzahl der Heapdatei %1$d|%2$d|%3$d ist inkonsequent. \n%4$d und %5$d wurden gemäß des Heap-Chains und der Dateitabelle, dementsprechend gefunden. 
598 Anzahl der Dateien ist inkorrect. %1$d waren erwartet, %2$d wurden gefunden
599 Ein I/O-Fehler trat beim Synchronisieren des Zustand des Datenträgers "%1$s".
600 Funktion "%1$s" nicht implementiert auf dem PC.
601 "%1$s".
602 dl Fehler: "%1$s" ist mehr als einmal definiert.
603 INTERNER FEHLER: Sektor/Seitentabelle der Datei VFID %1$d|%2$d könnte beschädigt sein.
604 INTERNER FEHLER: Ein Zuordnungsset für Datei VFID %1$d|%2$d(Volume "%3$s") erscheint inkorrekt in %4$d gelöschten oder gelöscht-markierten Seiten und %5$d Seitenlöcher in der Tabelle
605 INTERNER FEHLER: Inkorrekte Anzahl von gelöscht-markierten Seiten für Datei VFID %1$d|%2$d(Datenträger "%3$s"). %4$d erwartet, %5$d gefunden.
606 odbc.ini mit Pfad "%1$s" konnte nicht gefunden werden.
607 Datenbank "%1$s" in odbc.ini nicht gefunden.
608 Datenbank "%1$s" hat keine Host-Spezifikation in odbc.ini.
609 Ihre Transaktion kann nicht rückgängig gemacht werden, da die Protokollierung ignoriert wurde. Ihre Datenbank ist möglicherweise beschädigt.
610 Ihre Datenbank ist wahrscheinlich beschädigt, weil die Protokollierung ausgeschalten war, als die Datenbank abgestürzt ist.
611 %1$s kann nicht in Client-Server-Modus ausgeführt werden.
612 Die Protokollierung kann derzeit nicht ausgeschaltet werden, da es ausstehende Erholungsaktionen gibt.
613 In Klasse "%3$s", die lokale Komponente "%1$s" kann nicht als Alias für Klasse "%2$s" dienen. Die Domains sind inkompatibel.
614 Die Anzahl der Logarchiven überschreitet die gewünschte maximale Anzahl von %1$d.
615 Die Verbindung mit der Winsock-Schnittstelle kann nicht erstellt werden. Winsock-Fehler: %1$d.
616 Der Name des Hosts, der die Winsock-Schnittstelle benutzt, kann nicht bestimmt werden. Winsock Fehler: %1$d.
617 Der ID des Hosts, der die Winsock-Schnittstelle benutzt, kann nicht bestimmt werden. Es muss ein Eintrag in der Host-Namen-Tabelle für diese Maschine sein. Winsock Fehler: %1$d.
618 Das Attribut ist nicht aktualisierbar.
619 Schlechtes Quell-Code-Set.
620 Schlechte Ziel-Code-Set.
621 Ungültiger Datentyp. Die Argumente enthalten einen ungültigen Datentyp.
622 Inkompatible Code-Sets. Die Code-Sets der Argumente sind inkompatibel.
623 Ungültige Escape-Sequenz. Eine ungültige Escape-Sequenz hat einen Fehler in dem Kompilator regulärer Ausdrucke verursacht.
624 Ungültiges Escape-Zeichen. Das Escape-Zeichen ist non-NULL mit einer Länge größer als 1.
625 Interner Fehler. Es wurde versucht, das falsche Objekt Attribut Informationsvorlage %1$d|%2$d|%3$d mit Objekt %4$d|%5$d|%6$d.
626 Interner Fehler. %1$d angeforderte Attribute wurden nicht gefunden.
627 Ungültiges Trimm-Operand.  Das Trimm-Operand hat eine Länger größer als 1.
628 Ungültiger Währungstyp: %1$d.
629 Seitengröße %1$d ist keine Zweierpotenz oder sie ist zu klein. Eine Seitengröße von %2$d wird jedoch benutzt.
630 Konvertierung ist von %1$s nicht unterstützt.
631 SQL-Anweisung verletzt die NOT NULL Einschränkung.
632 %1$s ist kein Backupdatenträger.
633 %1$s ist eine Sicherung der Datenbank %2$s erstellt am %3$s anstatt der angegebenen Datenbank %4$s erstellt am %5$s
634 Einen Datenbankdatenträger/-datei war in der Sicherung erwartet.
635 WARNING: Log-Pufferpool von %1$d Puffern scheint zu klein fürs aktuelle Umfeld.\n Es is möglich, dass einige Puffer vom System wegen einem UNBEKANNTEM FEHLER nicht freigegeben werden, \n oder dass es viele Transaktionen zugleich abgebrochen werden. Der Log-Pufferpool wird durch %2$d mehr Puffer erweitert.
636 WARNING Num_buffers %1$d ist zu klein. Der minimale Wert von %2$d wird angenommen.
637 Systemfehler: Ein Logpuffer wird zu viel befreit. Fix-Anzahl auf Null gesetzt.
638 Warnung: Leeren eines nicht aktualisierbaren Log Archiv pageid = %1$d
639 Systemfehler: Vorherige Log Seite %1$d ist nicht die Seite verzögerter Leerung %2$d.
640 Systemfehler: Ein Sicherungspunkt kann nicht eingefügt werden, wenn die Transaktion nicht aktiv ist (e.g., während eines Commit oder Rollback).
641 Einen Name muss für den Sicherungspunkt angegeben werden.
642 Systemfehler: es gibt keine aktive Systemoperation für die aktuelle Transaktion.
643 Möglicher Systemfehler: Commit/Abbruch der Transaktion = %1$d (Index = %2$d), welche permanente Operation angehängt hat.\nDiese Systemoperationen werden an die Transaktion angehängt.
644 LOG FATAL ERROR: %1$s
645 SYSTEMFEHLER: Zustand = %1$s der Transaktion = %2$d (Index = %3$d) \n ist kein korrekter Zustand, an dem Client Verschiebung/Rückgängig-Aktionen angewendet werden können... wird ignoriert
646 SYSTEMFEHLER: Es scheint, dass ein Datensatz für logische Protokollierung rcvIndex = %1$s\n fehlt. Ohne dieses Datensatz, Crash Recovery wird nicht korrekt abschließen 
647 Sicherungsdatei "%1$s" gehört zur angegebenen Datenbank nicht.
648 Sicherungsdatei ist inkompatibel mit aktueller "%1$s" Version "%2$s".
649 Angegebene Präzision von %1$d ist ungültig; sie sollte größer als %2$d und nicht größer als %3$d sein.
650 Es können nicht genug Threads für 'max_clients=%1$d' erstellt werden.
651 Es gibt kein virtuelles Speicher für die %1$d Puffer für jedes der %2$d Threads.
652 Falscher Offset %1$d zur Splittung des Eintrags mit Länge %2$d, gespeichert in Slot %3$d, angegeben.
653 Falscher Offset %1$d oder Länge %2$d zur Entfernung eines Teils der Daten des Eintrags mit Länge %3$d, gespeichert in Slot %4$d, angegeben.
654 Falscher Offset %1$d oder Länge %2$d zur Überschreibung eines Teils der Daten des Eintrags mit Länge %3$d, gespeichert in Slot %4$d, angegeben.
655 Die Event-Handler-Konfigurationsdatei "%1$s" kann nicht geöffnet werden.
656 Warnung: Null ID/Name in Event-Handler-Konfigurationsdatei, "%1$s", Zeile %2$d
657 Warnung: Ungültiger Eventname, "%1$s", in Event-Handler-Konfigurationsdatei, "%2$s", Zeile %3$d
658 Warnung: Event ID %1$d nicht im Fehler-ID Bereich %2$d <> %3$d, Datei "%4$s", Zeile %5$d
659 Event-Handler-Programm kann nicht zugegriffen werden, "%1$s".
660 Keine Verbindung zum Event-Handler-Prozess "%1$s".
661 Es konnte an dem Event-Handler-Prozess nicht geschrieben werden "%1$s".
662 Event Handling Mechanismus konnte nicht initialisiert werden.
663 Warnung: Zu lange Zeile in der Event-Handler-Konfigurationsdatei "%1$s", Zeile %2$d wird abgeschnitten.
664 Event-Handling-Mechanismus gestartet.
665 Event-Handler-Pipe gebrochen "%1$s"
666 Event-Handling-Mechanismus gestoppt.
667 Klasse '%1$s' konnte mit der entsprechenden C++ Klass nicht abgestimmt werden.
668 Interner Fehler: Unbekannter Domaintyp '%1$d'.
669 Server verweigert Client-Verbindung: max. Clients (%1$d) überschritten.
670 Operation würde eine oder mehrere eindeutige Einschränkung-Verletzungen verursacht haben. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
671 Ein Netzwerk Lese-/Schreibfehler für Sockel %1$d, in Zusammenhang mit Ihrer Transaktion (Index %2$d, %3$s@%4$s|%5$d), ist aufgetreten.
672 Sockel %1$d, in Zusammenhang mit Ihrer Transaktion (Index %2$d, %3$s@%4$s|%5$d) ist geschlossen.
673 Ihre Verbindung zum Server wurde geschlossen, da der Server sofort herunterfahren wird.
674 Es gibt nicht genügend aktive Threads um aktuelle Server-Ausführung fortzuführen. Aktuelle Anzahl der aktiven Threads ist %1$d. Sie brauche bis zu %2$d Threads für eine saubere Server-Ausführung in Ihrer Umgebung. Transaktion-Index %3$d wurde abgebrochen wegen Time-Out, um die Serverausführung fortzusetzen.
675 Datenbankdatei kann nicht gelesen werden '%1$s'.
676 Datenbank '%1$s' kann in der Datenbankdatei '%2$s' nicht gefunden werden 
677 Es konnte keine Verbindung zum Datenbankserver '%1$s' auf Host(s) %2$s festgestellt werden.
678 Interner Fehler. Client Neustart Modul hat NULL als Datenbank Server Name empfangen.
679 Diese C++ Transaktion ist schon angefangen.
680 Es wurde versucht, die maximale C++ Transaktionenverschachtelung (%1$d) zu überschreiten.
681 %1$s Transaktion wurde auf einer C++ Transaktion versucht, die noch nicht begonnen hat.
682 Die C++ Transaktion kann nicht %1$s werden, bevor alle untergeordnete Transaktionen sind abgeschlossen.
683 Versuch ein String mit unzulässiger Länge zu erstellen - %1$d
684 Die Methode hat eine benutzerdefinierte Fehlerkode %1$d zurückgegeben.
685 Klasse "%1$s" ist ungültig.
686 Benachrichtigung: Der Event-Handler wurde neu gestartet.
687 Es ist nicht erlaubt (%1$s *) ins (%2$s *) umzuwandeln.
688 Subscript außer zulässigem Bereich. Es wurde versucht, das Element %1$lld zuzugreifen. Kollektionsgröße = %2$lld.
689 Interner Fehler. Iterator enthält einen fehlerhaften Zustand.
690 Die Dereferenzierung einer nicht initialisierten Instanz von odb_Ref<> wurde versucht.
691 Es wurde versucht, einen ungültigen Iterator zu verwenden.
692 Es wurde versucht, einen Iterator mit einer unverwandtem Sammlung zu benutzen.
693 Überlauf auf Datentyp „Numeric“.
694 SYSTEMFEHLER: B+tree kann nicht geladen werden.
695 Falsche Schnittstelle wurde verwendet, um eine Clientverbindung zu beenden.
696 Unbekannte Transaktion (Index %1$d, %2$s@%3$s|%4$d).
697 Angegebenes TransaktionenIndex (Index %1$d, %2$s@%3$s|%4$d) stimmt mit aktuellem TransaktionenIndex (Index %5$d, %6$s@%7$s|%8$d) nicht überein. Möglicherweise ist die angegebene Transaktion beendet, und ihre TransaktionenIndex wurde zu einer anderen Transaktion zugeordnet.
698 Interner Fehler: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Die Anzahl der OIDs in der eindeutigen Hierarchie: %6$d ist nicht gleich der Anzahl der OIDs: %7$d in der eindeutigem B+tree gefunden, plus die Anzahl der NULLs: %8$d in der eindeutigen Hierarchie für B+tree: %9$d|%10$d|%11$d gefunden.
699 Interner Fehler: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Die Anzahl der OIDs in der eindeutigen Hierarchie: %6$d ist nicht gleich der Anzahl der OIDs: %7$d in den eindeutigen B+tree Root-Statistiken für B+tree: %8$d|%9$d|%10$d gefunden.
700 Interner Fehler: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Die Anzahl der OIDs: %6$d ist nicht gleich der Anzahl der NULLs: %7$d plus die Anzahl der Schlüssel: %8$d in der eindeutigen B+tree: %9$d|%10$d|%11$d.
701 Kein Proxy ist auf "%1$s" an ldb "%2$s" definiert.
702 Interner Fehler: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Die Anzahl der NULLs in der eindeutigen Hierarchie: %6$d ist nicht gleich der Anzahl der NULLs: %7$d in den eindeutigen B+tree Root Statistiken für B+tree: %8$d|%9$d|%10$d gefunden.
703 Interner Fehler: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). OID: %6$d|%7$d|%8$d gefunden in dem eindeutigen B+tree: %9$d|%10$d|%11$d gehört nicht zu einer der Klassen für die eindeutige Einschränkung.
704 Bewertung der generischen Server-Funktion ist fehlgeschlagen.
705 Inkonsistente %1$d permanente Datenträger gefunden, %2$d erwartet.
706 Permanente Datenträger können nicht erweitert werden. Permanenter Datenträger "%1$s" mit dem Zweck %2$s wurde angegeben.
707 Temporärer Datenträger "%1$s" kann mit %2$d Seiten nicht erweitert werden.
708 Temporärer Datenträger "%1$s" kann mit %2$d Seiten (%3$lld Kbytes) wegen Speichermangel nicht erweitert werden. Aktuell verfügbare Speicher ist %4$d Seiten (%5$lld Kbytes).
709 Einschränkung "%1$s" nicht gefunden.
710 Ungültige Einschränkung.
711 Ein Objekt mit dem angegebenen Attributwert existiert nicht.
712 Einschränkung" %1$s" existiert bereits.
713 Es gibt kein Index auf diesem Attribut.
714 Anfrage ist wegen ungenügendem temporären Speicher gescheitert.
715 Die Schema %1$s konnte nicht überprüft werden.
716 NOT NULL Einschränkungen sind in virtuellen oder Proxy-Klassen nicht erlaubt.
717 UNIQUE Einschränkungen sind in virtuellen oder Proxy-Klassen nicht erlaubt.
718 Ungültiges Gadget.
719 Die Anzahl der Attribute (%1$d) und die Anzahl der Werte (%2$d) sind nicht gleich.
720 Ein Gadget darf nicht zum Einfügen in eine virtuelle oder Proxy-Klasse verwendet werden.
721 Insert Anfrage liefert mehr als eine Zeile.
722 Falsche Anzahl von Attributen für die NOT NULL Einschränkung.
723 Das Domain eines existierenden Attributs darf nicht geändert werden.
724 Invalides Parameter für die Default-Einschränkung Namenserzeugung.
725 Interner Fehler: ein temporäres oid erwartet, ein permanentes oid angegeben.
726 UNIQUE Einschränkung muss durch eine separate alter-Aussage hinzugefügt werden, nachdem die Instanzen erstellt sind. Zum Beispiel, anstatt:\n\talter table1 add attribute a int unique;\n\n\talter table1 add attribute a int;\n\talter table1 add unique(a) verwendet;
727 Ungültiges Eventtrigger.
728 Anfrage ist gescheitert, weil die temporäre Datei vfid ist ungültig für Transaktion %1$d.
729 Überlauf ist bei Subtraktion aufgetreten.
730 Überlauf ist bei Multiplikation aufgetreten.
731 Überlauf ist bei Division aufgetreten.
732 Überlauf ist beim unären Minus aufgetreten.
733 Überlauf ist beim Erzwang aufgetreten.
734 INTERNER FEHLER: Header der Datei VFID %1$d|%2$d(Volume "%3$s") ist korrupt.
735 Unbekanntes Transaktionenindex %1$d.
736 Entweder Dienst- oder Port-ID muss angegeben werden. Winsock Fehler: %1$d.
737 Unbekannter Dienst für TCP: %1$s. Winsock Fehler: %2$d.
738 Socket-Stream kann nicht erstellt werden. Winsock Fehler: %1$d.
739 Lokale Adresse kann nicht gebunden werden... es wird wieder versucht. Winsock Fehler: %1$d.
740 Lokale Adresse kann nicht gebunden werden... Abbruch. Winsock Fehler: %1$d.
741 Accept gescheitert. Winsock Fehler: %1$d.
742 Senden einer Broadcast-Nachricht an Client. Winsock Fehler: %1$d.
743 Der Handshake zwischen Client und Server ist gescheitert. (Peer Host %1$s)
744 Fehler bei der Streuspeicherung eines Proxy/V-Klasse MOP.
745 Ungültige Müllsammlungsphase.
746 Ungültiges Müllsammlung-Callback.
747 Ungültiges Müllsammlung-Callback-Ticket.
748 Ungültiges Objektzeiger an db_gc_mark_object angetroffen.
749 Die Anzahl der abgerufenen Instanzen stimmen nicht überein: %1$d erwartet, %2$d bekommen.
750 Die Anzahl der abgerufenen Instanzen stimmen nicht überein: %1$d erwartet, %2$d bekommen.
751 Das OID eines neuen Objekts ist in einem BEFORE INSERT Trigger nicht verfügbar.
752 Fehler beim Wiederherstellung vom Backup unit_num %1$d.
753 Fehler beim Wiederherstellung vom Backup unit_num %1$d, pageid %2$d überschreitet die Gesamtzahl der Seiten %3$d für volid %4$d.
754 OO Proxy-Instanz hat ein non-string-Typ oder null Schlüssel.
755 %1$s kann nicht im Stand-Alone-Modus ausgeführt werden.
756 Angegebene String-Puffer ist zu klein, um dieses Objekt zu kodieren.
757 INTERNER FEHLER: permanentes OID kann während Objektkodierung nicht zugewiesen werden.
758 Angegebener String ist nicht gültig für die Dekodierung des Objekts.
759 Das Zielobjekt wurde gelöscht.
760 Interner Fehler: Proxyobjektinstanz hat NULL Schlüssel während der Objektkodierung zurückgegeben.
761 Interner Fehler: V-Klasse Objekt-Instanz einzeichnet eine ungültige reale Instanz.
762 Interner Fehler bei der Übersetzung der V-Klasse Instanz zu einer realen Instanz.
763 Interner Fehler in Kodierung einer nicht aktualisierbaren Objektinstanz.
764 Interner Fehler in Kodierung des Objektes in einem internen vobj Form.
765 Output Objekt Argument hat eine Null-Adresse.
766 Interner Fehler bei der Dekodierung des Objektes.
767 Eine Sicherung Level %1$d kann nicht bevor einer Sicherung Level %2$d erfolgen.
768 Die lokale Datenbank "%1$s" wurde in dieser Session zugegriffen, Änderung oder Löschen ist nicht erlaubt.
769 Das Attribut "%1$s" der Klasse "%2$s" ist keine valide Klasse oder geteiltes Attribut.
770 Keine Tabellenfunktionen.
771 Ungültiger Parameter für die Funktion.
772 db_serial Klasse nicht gefunden.
773 Serial "%1$s" nicht gefunden.
774 Serial "%1$s" existiert bereits.
775 Folgender Wert überschreitet den MAX(MIN)-Wert.
776 Das serielle Objekt kann nicht geholt werden.
777 Das serielle Objekt kann nicht aktualisiert werden.
778 Symbolischer Link "%1$s" zur Datei "%2$s" kann nicht erstellt werden.
779 Das Ergebnis überschreitet den Grenzwert: das Jahr im Datum MUSS zwischen 1 und 9999 sein.
780 Interner Systemfehler: Systemzeit kann nicht gelesen werden.
781 Der String ist zu lang. Er muss kleiner als 16K sein.
782 Leere Strings sind hier nicht erlaubt.
783 Ungültiges Format.
784 Zwei Argumente stimmen nicht überein.
785 Der erste Argumentetring ist zu lang. Er muss kleiner als 16K sein.
786 Das Format ist gedoppelt.
787 Konversionsfehler im Zeitformat.
788 Konversionsfehler im Zeitstempelformat.
789 Falsche Zahl.
790 Anfrageausführung wurde vom Benutzer abgebrochen.
791 Ungültige Seriennummer.
792 Der Speicher für css kann nicht zugewiesen werden.
793 pthread_attr_init() gescheitert.
794 pthread_attr_destroy() gescheitert.
795 pthread_attr_setdetachstate() gescheitert.
796 pthread_attr_setscope() gescheitert.
797 pthread_attr_setstacksize() gescheitert.
798 pthread_create() gescheitert.
799 pthread_join() gescheitert.
800 pthread_mutex_init() gescheitert.
801 pthread_mutex_destroy() gescheitert.
802 pthread_mutex_lock() gescheitert.
803 pthread_mutex_trylock() gescheitert.
804 pthread_mutex_unlock() gescheitert.
805 pthread_mutexattr_init() gescheitert.
806 pthread_mutexattr_destroy() gescheitert.
807 pthread_mutexattr_settype() gescheitert.
808 pthread_mutexattr_gettype() gescheitert.
809 pthread_cond_init() gescheitert.
810 pthread_cond_destroy() gescheitert.
811 pthread_cond_wait() gescheitert.
812 pthread_cond_timedwait() gescheitert.
813 pthread_cond_signal() gescheitert.
814 pthread_cond_broadcast() gescheitert.
815 pthread_key_create() gescheitert.
816 pthread_key_delete() gescheitert.
817 pthread_setspecific() gescheitert.
818 pthread_getspecific() gescheitert.
819 pthread_once() gescheitert.
820 Die Verbindung kann nicht initialisiert werden.
821 Verbindung abgeschaltet.
822 Mutex-Operation beim Lesen des nächsten Client-ID gescheitert.
823 Mutex-Operation bei der Initialisierung der Verbindungsliste gescheitert.
824 Das Mutex kann nicht während dem Abschließen der Verbindungsliste zerstört werden.
825 pread() Aufruf gescheitert.
826 pwrite() Aufruf gescheitert.
827 Die Verbindungsliste Traverse-Funktion hat einen ungültigen Wert zurückgegeben.
828 Ungültige kritische Sektion des Index wurde angegeben.
829 Dieses Mutex-Lock ist schon entlockt worden.
830 Abfrage kann nicht mehr zugewiesen werden. Die maximale Zahl verfügbarer Eingänge ist %1$d.
831 Alle Transaktion-Deskriptors (%1$d) sind im Einsatz. Das max_clients Parameter muss größer gemacht werden.
832 Sicherung läuft bereits. Die Sicherung kann nicht von zwei Prozessen ausgeführt werden.
833 Der Indexname "%1$s" ist mehrdeutig. Geben Sie den Klassename des Indexes.
834 Es gibt einige Diskrepanzen zwischen Quell-String "%1$s" und Format-String "%2$s" in to_number() angegeben.
835 Undefiniertes Statistikelement.
836 LATCH ON PAGE(%1$d|%2$d) TIMEDOUT
837 Der Benutzer, der Datenbankobjekte besitzt, kann nicht gelöscht werden (Klasse/Trigger/Serial usw.). 
838 Es können nicht mehr als %1$d Index Scans gleichzeitig geöffnet werden.
839 "%1$s": Unbekannter System-Parameter oder falscher Wert.
840 System-Parameter "%1$s" kann nicht geändert werden.
841 Der angeforderte Vorgang kann nur von einem Client ausgeführt werden.
842 Das Objekt-Lock %1$d|%2$d|%3$d kann nicht gecached werden.
843 Falsche Argumente in %1$s, ein %2$s wurde angegeben.
844 Unbekannte Isolation (%1$d) gefunden, Transaktionenindex %2$d.
845 Ungültiger Objekttyp (%1$d) gefunden. Das absperrbare Objekt ist %2$d|%3$d|%4$d.
846 %1$s der Transaktion (%2$d) ist in der Halterliste eines Objekts nicht gefunden (%3$d|%4$d|%5$d).
847 %1$s auf ein %2$s(%3$d|%4$d|%5$d) ist in der Transaktion (%6$d) Halterliste (cnt=%7$d) nicht gefunden.
848 %1$s auf ein Objekt (%2$d|%3$d|%4$d) ist in der Transaktion (%5$d) non2pl-Liste nicht gefunden.
849 Es wird zweimal versucht, die Transaktion (%1$d) abzubrechen.
850 Transaktion (%1$d) ist weder Sperrhalter noch Sperrwarter eines Objekts (%2$d|%3$d|%4$d).
851 Konnte die gesperrte Ressource nicht zuweisen (%1$s). 
852 Totale Anzahl der Halter (%1$d) einer leeren Speerhalterliste ist nicht NULL_LOCK.
853 Für ein Objekt (%1$d|%2$d|%3$d), Transaktion (%4$d) der Uncommitted-Read verursachte die Störabschaltung (%5$s).
854 WARNING: Kein Sperrhalter, jedoch Sperrwarter existieren. Beziehen Sie sich auf %1$s.
855 WARNING: Zwei oder mehrere Threads sperrwarten für eine Transaktion (%1$d).
856 WARNING: Seltsamer LOCKWAIT Status (%1$d|%2$d), Thread (%3$d|%4$d) und Transaktion (%5$d).
857 Ein I/O-Fehler ist beim Abschneiden der Seite %1$d des Datenträgers "%2$s" aufgetreten.
858 Datenträger "%1$s" ist unbekannt nach Serverneustart.
859 LATCH ON PAGE(%1$d|%2$d) ABORTED
860 Aktuelle Transaktion kann nicht Teil einer globalen Transaktion werden, weil ihr Zustand '%1$s' ist.
861 Die aktuelle Transaktion kann mit dem Transaktion-Identifikator %1$d nicht festgeschrieben werden, weil diese Transaktion nicht teil einer globalen Transaktion sein kann.
862 Die Benutzerinformationen der globalen Transaktion mit dem Identifikator %1$d können nicht gesetzt werden, weil ihr Zustand '%2$s' ist.
863 Die Benutzerinformationen der globalen Transaktion mit dem Identifikator %1$d können nicht gelesen werden, weil diese nicht gesetzt sind.
864 Die backupdb/restoredb-Datei '%1$s' kann nicht geöffnet werden.
865 Anweisung kann wegen unbekanntem Typ der Input-Marker nicht ausgeführt werden.
866 Unused Fehler code.
867 Unused Fehler code.
868 Unused Fehler code.
869 Unused Fehler code.
870 Unused Fehler code.
871 Unused Fehler code.
872 Unused Fehler code.
873 PKI Authentifizierungsfehler "%1$s".
874 Das Argument von power() ist außer zulässigem Bereich.
875 Überlauf im Power-Kontext.
876 Unused Fehler code.
877 Keine Daten zum entladen.
878 Ungültige Datenbankdatei '%1$s'.
879 Erlaubnis '%1$s' kann nicht erhalten werden.
880 Erlaubnis '%1$s' kann nicht geändert werden.
881 Die Sperrdatei '%1$s(FILE ID:%2$d)' kann nicht gelesen werden.
882 Die Sperrdatei 'FILE ID:%1$d' kann nicht freigegeben.
883 Zustand der Datei '%1$s' konnte nicht gelesen werden.
884 Volext-Pfad '%1$s' darf nicht schreiben.
885 Umziehen von '%1$s' zu '%2$s' gescheitert. Verwenden Sie das gleiche Gerät (Disk).
886 "%1$s" verursachte die Verletzung einer eindeutigen Einschränkung.
887 Gespeicherte Prozedur '%1$s' ist bereits vorhanden.
888 Parameteranzahl ist ungültig. %1$d erwartet, %2$d bekommen.
889 Ausführungsfehler gespeicherter Prozedur: %1$s
890 Partition fehlgeschlagen.
891 Entsprechende Partition existiert nicht.
892 Primärer Schlüssel "%2$s" bereits definiert für Klasse "%1$s".
893 Attribut '%1$s', Mitglied des primären Schlüssels, konnte nicht gelöscht werden.
894 Gespeicherte Prozedur/Funktion '%1$s' existiert nicht.
895 Ungültiger Typ gespeicherter Prozedur: '%1$s' ist '%2$s'.
896 Komprimierung fehlgeschlagen. Zip-Methode: %1$d (%2$s), Zip Level: %3$d (%4$s)
897 Dekomprimierung fehlgeschlagen.
898 Nachbildungsfehler: "%1$s"
899 Ungültige Partitionabfragen.
900 Java VM Bibliothek nicht gefunden: %1$s.
901 Java VM kann nicht gestartet werden: %1$s.
902 Java VM läuft nicht.
903 Verbindung zur Java VM ausgefallen: %1$s
904 Ungültiger Name gespeicherter Prozedur.
905 Vernetzung mit JVM ausgefallen: %1$d
906 Ungültige Unterschrift der Java-Methode.
907 Der Eigner oder ein Mitglied der DBA-Gruppe kann die gespeicherte Prozedur löschen.
908 Zu viele Argumente für die gespeicherte Prozedur '%1$s'.
909 Fehlende oder ungültige Katalogklasse/-vklasse gefunden.
910 Partitionzugriff nicht gestattet.
911 Ungültiger Aufruf: ein ResultSet kann nicht geantwortet werden.
912 Ein ResultSet nicht als Eingangsparameter verwendet werden.
913 Zu verschachtelte gespeicherte Prozeduren Aufrufe.
914 Eine serielles Objekt existiert bereits als Autoinkrement-Einschränkung.
915 Der Pluswert einer Auto-Inkrement-Einschränkung sollte größer als Null sein.
916 Das aktive Protokolldatenträger ist in diesem Sicherungsdatenträger nicht enthalten (%1$s).
917 Aktiver Protokolldatenträger existiert bereits.
918 Die Klasse '%1$s', referenziert von einer Fremdschlüssel, existiert nicht.
919 Die Klasse '%1$s', referenziert von einer Fremdschlüssel, hat keinen Primärschlüssel.
920 Der Fremdschlüssel '%1$s' beinhaltet nicht den Primärschlüssel-Mitglied '%2$s'.
921 Das Domain der Fremdschlüssel-Mitglied '%1$s' ist unterschiedlich von dem des Primärschlüssel-Mitglieds '%2$s'.
922 The Einschränkung des Fremdschlüssels '%1$s' ist wegen dem Wert '%2$s' ungültig.
923 Der Primärschlüssel '%1$s', referenziert vom einem Fremdschlüssel '%2$s' soll nicht gelöscht werden.
924 Update/Delete Operationen werden durch dem Fremdschlüssel '%1$s' beschränkt.
925 Sperrprozedur für den Fremdschlüssel '%1$s' gescheitert.
926 Die Instanz mit einem Fremdschlüssel '%1$s' kann nicht gelöscht werden.
927 Die Anzahl der Schlüssel des Fremdschlüssels '%1$s' ist unterschiedlich vom der des Primärschlüssels '%2$s'.
928 Das Attribut '%1$s', erstellt von 'ON CACHE OBJECT' eines Fremdschlüssels.
929 Ein Fremdschlüssel kann nicht in einer virtuellen Klasse definiert werden.
930 Das Attribut '%1$s', erstellt von 'ON CACHE OBJECT' des Fremdschlüssels kann nur nach Entfernung der Fremdschlüsseleinschränkung gelöscht werden.
931 Anfangswert einer Autoinkrement-Einschränkung sollte kleiner als der maximale Wert.
932 Eine Inkonsistenz der Anzahl freier Seiten für den Datenträger "%1$s" kann nicht repariert werden, weil %2$d Seiten, wie Datenträgerheader, inkonsistent mit %3$d Seiten, wie Bitmap, ist.
933 Eine Inkonsistenz der Anzahl freier Sektoren für den Datenträger "%1$s" kann nicht repariert werden, weil %2$d Sektoren, wie Datenträgerheader, inkonsistent mit %3$d Sektoren, wie Bitmap, ist.
934 Null-Domain wurde referenziert.
935 Das Argument der %1$s Funktion ist außer zulässigem Bereich.
936 Überlauf trat im exp-Kontext.
937 Datenbankfehler.
938 Ungültiges Argument.
939 Es gibt zu viele Verbindungen.
940 Das Handle ist ungültig.
941 Die Operation ist nicht unterstützt.
942 Zeitüberschreitung im Handle, während auf Locks gewartet wurde.
943 Generischer Fehler.
944 Die Aussage ist nicht bereit.
945 Die Aussage hat kein Ergebnis-Set.
946 Die Aussage wurde nicht ausgeführt.
947 Es gibt kein Ergebnis-Set.
948 Die Puffergröße genügt nicht.
949 Es gibt keine verfügbaren Informationen.
950 Der Name ist ungültig.
951 Das Ergebnis-Set ist nicht aktualisierbar.
952 Die Reihe wurde gelöscht.
953 Parameter der verarbeiteten Anweisung sind nicht gesetzt.
954 Due Aussage ist keine Batch-Anweisung.
955 Diese Batch-Anweisung kann nicht geleert werden.
956 Die Batch-Anweisung kann nicht ausgeführt werden.
957 Die Anweisung ist eine vorbereitete Anweisung.
958 Die Anweisung ist keine vorbereitete Anweisung.
959 Die Anweisung ist eine Batch-Anweisung.
960 Es gibt keine anderen Fehler.
961 Es gibt keine andere Ergebnisse.
962 Außer Speicher
963 Broker-Fehler.
964 Das Ergebnis-Set-Handle ist geschlossen.
965 Attributnamen können nicht doppelt in der Index-Schlüsselliste angewendet werden. Attribut mit Namen '%1$s' ist mehr als einmal gelistet.
966 Transaktion-Timeout (Index %1$d, %2$s@%3$s|%4$d) beim Warten auf %5$s Objekt-Lock %6$d|%7$d|%8$d wegen Deadlock. Sie warten auf Benutzer(n) %9$s zu beenden.
967 Transaktion-Timeout (Index %1$d, %2$s@%3$s|%4$d) beim Warten auf %5$s Klassenlock %6$s wegen Deadlock. Sie warten auf Benutzer(n) %7$s zu beenden.
968 Transaktion-Timeout (Index %1$d, %2$s@%3$s|%4$d) beim Warten auf %5$s Instanz-Lock %6$d|%7$d|%8$d der Klasse %9$s wegen Deadlock. Sie warten auf Benutzer(n) %10$s zu beenden.
969 Server-Bit-Plattform (%1$d) unterscheidet sich vom Client-Bit-Plattform (%2$d).
970 Server HA-Modus ist geändert von '%1$s' auf '%2$s'.
971 Program '%1$s' (pid %2$d) ist mit dem Datenbankserver '%3$s' auf Host '%4$s' (Port %5$d) verbunden.
972 Program '%1$s' (pid %2$d) ist am Host '%3$s' Verbunden. (Transaktionenindex %4$d)
973 Serverzustand ist %1$s.
974 Archivprotokoll "%1$s" dient zur Archivierung der Seiten von %2$lld bis %3$lld.
975 Aktuelle Version der Nachbildung erlaubt die Änderung mehrerer Zeilen mit einer einzigen UPDATE Anweisung, die die UNIQUE-Einschränkung verletzen kann, nicht.
976 Interner Fehler: Tabellengröße Überlauf (zugewiesener Speicher: %1$d, abgerufen: %2$d) au Seite %3$d|%4$d(Datenträger "%5$s")
977 Checkpoint begonnen (voriger Checkpoint Seiten-ID: %1$lld, vorige Redo-Seiten-ID: %2$lld).
978 Checkpoint finished (checkpoint page id: %1$lld, checkpoint redo page id: %2$lld, flushed page count: %3$d).
979 CONNECT BY-Schleife in Benutzerdaten.
980 Ein Thread wartet in %1$s seit mehr als %2$d msec.
981 Transaktion %1$d hat die Schwelle der Statistik-Monitoring erreicht %2$s.
982 Set-Interrupt auf Transaktion %1$d.
983 Instanzen der wiederverwendbaren OID-Klasse sind nicht beziehbar. Diese Operation ist auf nicht beziehbaren Instanzen nicht erlaubt.
984 %3$s kann nicht auf "%6$s" in "[%5$s]" Sektion gesetzt, wenn es auf "%4$s" in "[%2$s]" Sektion der Konfigurationsdatei "%1$s" gesetzt ist.
985 Der Hostname muss in dem Datenbank-Verbindungsstring angegeben werden, wenn Multihost in „databases.txt“ eingestellt ist.
986 CUBRID Heartbeat-Funktion gestartet. 
987 CUBRID Heartbeat-Funktion ist beendet.
988 Knotenereignis: %1$s.
989 Prozessereignis: %1$s. %2$s
990 Kommandoausführung: %1$s. %2$s
991 Leeren des Seitenpuffers begonnen.
992 Leeren des Seitenpuffers beendet (gelöschte Kandidaten: %1$d).
993 Überlauf in Bit-Operation-Kontext.
994 Diese Anweisung kann keine vorbereitete Anweisung sein.
995 Eine vorbereitete Anweisung mit dem Namen %1$s existiert nicht.
996 Inkorrekter Anzahl der Werte angegeben. %1$d Werte angegeben %2$d Werte benötigt.
997 Die Fremdschlüsseleinschränkung %1$s kann nicht an dem partitioniertem %2$s hinzugefügt werden. Referentielle Aktionen, die Attribute aktualisieren, sind auf partitionierten Klassen nicht unterstützt.
998 Die Fremdschlüsseleinschränkung %1$s kann nicht an dem Attribut %2$s hinzugefügt werden. Das Attribut kann keine NOT NULL-Einschränkung haben, weil diese mit der SET NULL referentiellen Action übereinstimmen muss.
999 Tabelle '%1$s' kann nicht wie Tabelle '%2$s' erstellt werden, weil die existierende Tabelle Funktionen hat, die nicht dupliziert werden können (%3$s).
1000 '%1$s' ist ein CLASS- oder SHARED-Attribut, aber nur normale Attribute sind in diesem Kontext erlaubt.
1001 Eine unerwartete Kondition oder Zustand wurde erreicht. '%1$s'
1002 Die aktuelle Transaktion konnte die Datenbank-Lock, die die Fortsetzung der Verarbeitung erfordert, nicht erhalten.
1003 The Einschränkung '%1$s' existiert, hat aber an anderes Typ.
1004 Bitte beachten Sie, dass bei der Anwendung von ALTER TABLE DROP FOREIGN KEY, CUBRID löscht sowohl die Einschränkung als auch ihren assoziierten Index.
1005 Unbenutzter Fehlercode.
1006 Unbenutzter Fehlercode.
1007 Ein Index mit Präfixlenge kann auf eindeutige, Fremdschlüssel- der Primärschlüsseleinschränkungen nicht erstellt werden.
1008 Ein Index mit Präfixlenge kann auf partitionierte Klasse nicht erstellt werden.
1009 COMPACTDB bereits gestartet.
1010 Ein Index mit Präfixlenge kann auf Domain "%1$s" nicht erstellt werden.
1011 Die Fremdschlüsseleinschränkung %1$s kann an dem Shared-Attribut %2$s nicht hinzugefügt werden.
1012 Eine Tabelle mit NULL Spaltentyp kann nicht erstellt werden.
1013 Set DEFAULT oder SHARED auf %1$s-Typ Spalte ist nicht erlaubt.
1014 Set NOT NULL auf %1$s-Typ Spalte ist nicht erlaubt.
1015 Das angegebene Verzeichnis '%1$s' existiert nicht.
1016 %1$s externer Speicher Fehler: %2$s
1017 Pfad für externen Speicher '%1$s' ist ungültig.
1018 Die externe Datei kann auf die andere Art Speicher nicht kopiert werden. (Quelle %1$s, Ziel %2$s)
1019 Externer Speicher ist nicht initialisiert, weil der Pfad nicht in "databases.txt" angegeben ist.
1020 Externe Datei "%1$s" nicht gefunden.
1021 Ein Deadlock-Zyklus erkannt. %1$s.
1022 Ungültiger Wert "%1$d" für Präfixlänge des Indexes.
1023 Reservierter Fehlercode für HA.
1024 log writer: failed to get log page(s) starting from page id %1$lld.
1025 HA delay info: The replication delay (%1$d secs) has exceeded the limit (%2$d secs).
1026 HA delay info: The replication delay (%1$d secs) is decreased below the acceptable level (%2$d secs).
1027 Log-Applier: Apply-Zustand konnte von '%1$s' auf '%2$s' nicht geändert werden.
1028 Log-Applier: unerwarteter EOF Eintrag im Archivprotokoll. LSA: %1$lld|%2$d.
1029 Log-Applier: ungültige Replikation-Protokollseite/-offset. Seite HDR: %1$1lld|%2$d, final: %3$lld|%4$d, append LSA: %5$lld|%6$d, EOF LSA: %7$lld|%8$d, ha Dateistatus: %9$d, its end-of-log: %10$d.
1030 Log-Applier: ungültiger Replikationseintrag. LSA: %1$lld|%2$d, forw LSA: %3$lld|%4$d, backw LSA: %5$lld|%6$d, Trid: %7$d, prev tran LSA: %8$lld|%9$d, Typ: %10$d.
1031 log applier: failed to apply schema replication log. class: "%1$s", schema: "%2$s", server error: %3$d, %4$s. 
1032 log applier: failed to apply insert replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1033 log applier: failed to apply update replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1034 log applier: failed to apply delete replication log. class: "%1$s", key: "%2$s", server error: %3$d.
1035 Log-Applier: Die Speichergrösse (%1$d MB) des Log-Appliers ist größer als der gesamte Speicher (%2$d MB) oder ist mehr als zweimal (%3$d MB) gewachsen. erforderlich LSA: %4$lld|%5$d. Letzter Mal LSA festgeschrieben: %6$lld|%7$d.
1036 Log-Applier: Log-Applier hat sich selber heruntergefahren.
1037 Log-Schreiber: Der Log-Schreiber hat sich selber heruntergefahren.
1038 Log-Applier: Log-Applier ist gestartet. Erforderliche LSA: %1$lld|%2$d. Letzter Mal LSA festgeschrieben: %3$lld|%4$d. Letzter Mal repl. LSA festgeschrieben: %5$lld|%6$d.
1039 Log-Schreiber: Log-Schreiber gestartet. Modus: %1$d.
1040 Generisches HA: %1$s. 
1041 Ein String wurde im Kontext von "%1$s" verkürzt.
1042 Es wird versucht, ein String zu erstellen, der %1$d Bytes Speicher braucht, während der maximal erlaubte %2$d Bytes ist.
1043 Attribut "%1$s" kann nicht geändert werden. CUBRID kann die Eigenschaften des angeforderten Attributs nicht ändern, wenn die Klasse Subklassen hat.
1044 Attribut "%1$s" kann nicht geändert werden. CUBRID kann Attribute, die Partitionierungsschlüssel sind, nicht ändern.
1045 Attribut "%1$s" kann nicht geändert werden. CUBRID kann Attribute, die in Fremdschlüssel angewendet werden (entweder referenziert, oder referenzierend), nicht ändern.
1046 Attribut "%1$s" kann nicht geändert werden. CUBRID kann die Domain des Attributs nicht ändern. Die Änderung des Domains ist nicht unterstützt.
1047 Attribut "%1$s" kann nicht geändert werden. CUBRID kann die Domain des Attributs nicht ändern. Die neue Domain könnte einige Attributwerte nicht korrekt repräsentieren.
1048 Attribut "%1$s" kann nicht geändert werden. CUBRID kann derzeit das AUTO_INCREMENT eines Attribut nicht ändern, wenn die Domain des Attributs sowohl geändert würde.
1049 Attribut "%1$s" kann nicht geändert werden. CUBRID kann derzeit die Domain des Attributs nicht ändern. Die Domainkonvertierung ist möglich, aber diese Funktion ist in der Konfiguration nicht erlaubt.
1050 Attribut "%1$s" kann nicht geändert werden. CUBRID kann derzeit die Domain des Attributs nicht ändern. Das Attribut hat ein Index.
1051 Attribut "%1$s" kann nicht geändert werden. CUBRID kann derzeit ein Attribut nicht ändern, um eine Einschränkung hinzufügen. Bestehende Attributwerte könnten die Einschränkung nicht respektieren.
1052 Attribut "%1$s" wurde nicht geändert. CUBRID hat keine effektive Änderungen in der Attribut-Repräsentierung entdeckt.
1053 Die Aussagen REPLACE INTO... und INSERT ... ON DUPLICATE KEY UPDATE sind nicht für Klassen, die Teil einer Erbschaft-Kette sind, nicht erlaubt.
1054 Der neue Wert einer Auto-Inkrement Einschränkung sollte größer oder gleich mit dem aktuellen Wert sein.
1055 Der neue Wert einer Auto-Inkrement Einschränkung sollte kleiner als der maximale Wert sein.
1056 Um Mehrdeutigkeit zu vermeiden, die AUTO_INCREMENT TabellenOption erfordert genau eine AUTO_INCREMENT-Spalte in der Tabelle und keine Seed/Inkrement Spezifikation.
1057 Ein NOT NULL '%1$s' Attribute ohne einen DEFAULT Wert kann nicht hinzugefügt werden.
1058 ALTER TABLE .. CHANGE: Änderung zur neuen Domain: Cast gescheitert, die aktuelle Konfiguration erlaubt keine Verkürzung oder Überlauf.
1059 ALTER TABLE .. CHANGE: Änderung zur neuen Domain: Cast gescheitert, Default-Wert einer neuen Domain wurde gesetzt.
1060 ALTER TABLE .. CHANGE: Änderung zur neuen Domain: Cast gescheitert, Minimum-Wert einer neuen Domain wurde gesetzt.
1061 ALTER TABLE .. CHANGE: Änderung zur neuen Domain: Cast gescheitert, Maximum-Wert einer neuen Domain wurde gesetzt.
1062 Attribut "%1$s" kann nicht geändert werden. Die Änderung würde in mehrere Primärschlüssel.
1063 NOT NULL Einschränkung für Attribut "%1$s" kann nicht hinzugefügt werden: es gibt NULL-Werte für dieses Attribut.
1064 ALTER TABLE .. CHANGE: NOT NULL hinzufügen: alle existierende NULL-Werte wurden zur Domain-Default geändert.
1065 Attribut "%1$s" kann nicht geändert werden. CUBRID kann die NOT NULL Einschränkung nicht hinzufügen: ein Default-Wert existiert für die Domain nicht.
1066 Session abgelaufen.
1067 Spalte '%1$s' kann keine NOT NULL Einschränkung und zugleich ein NULL Default-Wert haben.
1068 Maximale Trigger-tiefe kann %1$d nicht überschreiten.
1069 Zu viele vorbereitete Anweisungen.
1070 Session-Variable '@%1$s' nicht definiert.
1071 Zu viele Session-Variablen.
1072 IP Adresse (%1$s) ist nicht zugelassen.
1073 Ungültiges Format in Datei (%1$s).
1074 Zugriff-Liste kann nicht gelesen werden (%1$s).
1075 Der Scan Absteigender Indexen wegen niedrigerer Priorität auf B+tree mit Index-Identifier: (vfid = (%1$d, %2$d), rt_pgid: %3$d) abgebrochen.
1076 Systemparameter kann nicht geladen werden.
1077 Der '%1$s' Parameter auf Zeile %2$d in Datei '%3$s' : Unbekannter Parameter
1078 Zeichen am Offset %1$d ist ungültig in aktueller Codeliste.
1079 Codelisten der Datenbank (%1$s) und Serverkonfiguration(%2$s) sind nicht kompatibel.
1080 Die maximale Größe des Filter-Prädikat-Strings muss %1$d sein.
1081 Ein Attribut, der in einem Filter oder Funktionindex beteiligt ist, darf nicht geändert.
1082 Alle Seitenpuffer sind benutzt (LRU: %1$d).
1083 Spezifische Information über Deadlock.\n%1$s
1084 Ungültige Seite in Checkpoint überspringen. (Seiten-ID: %1$d, "%2$s", oldest_unflush_lsa: %3$lld|%4$d, früherer Checkpoint Redo lsa: %5$lld|%6$d)
1085 Regulärer Ausdruck '%1$s' kann nicht kompiliert werden.
1086 Regulärer Ausdruck '%1$s' kann nicht ausgeführt werden.
1087 Sicherung des aktiven Protokolls '%1$s' angefangen.
1088 Sicherung des aktiven Protokolls '%1$s' beendet.
1089 Ressourcenzuteilung gescheitert. 'max_bestspace_entries=%1$d' soll vergrößert werden.
1090 Lokale Initialisierung: %1$s.
1091 Lokale Erzeugung: %1$s.
1092 Anweisung der Typ '%1$s' kann mit Host-Variablen nicht vorbereitet werden.
1093 Das arg '%1$s' für inet_aton ist keine valide String-IPV4-Adresse.
1094 Das arg '%1$lld' für inet_ntoa ist keine valide Nummer-IPV4-Adresse.
1095 Benutzername ist zu lang.
1096 Änderung der Partitionsschema ist nicht erlaubt, wenn die partitionierte Klasse von einer Fremdschlüssel referenziert wird.
1097 Spalte '%1$s' kann keine PRIMARY KEY Einschränkung und zugleich einen NULL Default-Wert haben.
1098 Sprachumgebungen für Sprache '%1$s' sind im Zeichenvorrat '%2$s' nicht verfügbar.
1099 DDL Anweisung wird durch die Konfiguration (block_ddl_statement=yes) nicht erlaubt.\n SQL Text: %1$s
1100 Anweisung ohne WHERE Klausel ist in der Konfiguration nicht erlaubt (block_nowhere_statement=yes).
1101 Unbenutzter Fehler Code.
1102 Unbekannter Systemparameter oder falscher Wert.
1103 Systemparameter kann nicht geändert werden.
1104 Langsame Abfrage (%1$d msec)\n%2$s
1105 Interner Fehler: wegspülen eines Objektes teilweise gescheitert.
1106 Mehrere Zeilen in Quelltabelle entsprechen der gleichen Zeile in der Zieltabelle.
1107 Ungültige Zahl für NTILE. Sie sollte größer als Null und kleiner als 2.147.483.647 sein.
1108 Ungültige Bucket-Zahl für WIDTH_BUCKET. Sie sollte größer als Null und kleiner als 2,147,483,647 sein.
1109 Ungültiger Wert für die Definierung der Partition.
1110 Ungenügend Speicher in Btree-Seite. (Schlüssellänge: %1$d)\n%2$s
1111 Ergebnis abgeschnitten, da die Genauigkeit (%1$d) kleiner als seine Länge (%2$d) ist.
1112 Ein Zeichen konnte nicht von '%1$s' auf '%2$s' umgerechnet werden.
1113 Elementindex außerhalb des Bereichs.
1114 Argument '%1$s' ist außerhalb des Bereichs.
1115 Angefangen, Statistiken zu aktualisieren (Klasse "%1$s", oid : %2$d|%3$d|%4$d).
1116 Aktualisierung der Statistiken beendet (class "%1$s", oid : %2$d|%3$d|%4$d, Fehlerkode : %5$d).
1117 Attribut "%1$s" kann nicht geändert werden. CUBRID kann ein Attribut nicht in SHARED ändern, und umgekehrt.
1118 Das Argument von "%1$s" kann nicht auf die gewünschte Domain "%2$s" gezwungen werden.
1119 Interner Fehler: Seiten-id %1$d wird nach der Zuteilung Karte von Volume "%2$s" zugeordnet, aber es gehört zu keiner Datei. 
1120 Interner Fehler: Seiten-id %1$d des Volumen "%2$s" wird gerade verwendet, aber es ist nicht nach der Zuteilung des Volumens Karte zugeordnet.
1121 NULL-Wert von NOT NULL-Einschränkungen nicht zugelassen.
1122 Zeitüberschreitung beim Empfangen von Daten.
1123 pthread_cond_timedwait() Zeitüberschreitung. 
1124 Fehler bei Ausführung der Abfrage. ERROR_CODE = %1$d, /* SQL_ID: %2$s */ %3$s
1125 Erstellung der Überlauf Schlüsseldatei. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1126 Erstellung einer neuen Überlaufseite. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1127 Eine leere Überlaufseite wird gelöscht. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1128 Log-Wiederherstellung gestartet. Anzahl der anzuwendenden Log-Einträge: %1$lld. Log Seite: %2$lld ~ %3$lld.
1129 Log-Wiederherstellung beendet.
1130 Transaktion wird abgebrochen, weil die Anzahl der Transaktionssperre %1$d überschreitet.
1131 Interner Fehler: Die Seite %1$d des Volumen "%2$s" könnte beschädigt sein.\n%3$s
1132 Encryption library failure: %1$s
1133 Copylogdb für %1$s existiert bereits.
1134 Applylogdb für %1$s existiert bereits.
1135 Ein vorhergehend Link von B+tree(%1$s) ist beschädigt: vpid = (%2$d, %3$d)
1136 Ein vorhergehend Link von B+tree(%1$s) ist repariert: vpid = (%2$d, %3$d)
1137 Die Abfrage wurde aufgrund abgelehnt wurde versucht die maximal zulässige Verschachtelungstiefe überschreiten(%1$d).
1138 Handshake-Fehler (Peer-Host %1$s): Unterbrechbarkeitsklauseln unvereinbar. (client: %2$s, server: %3$s)
1139 Handshake-Fehler (Peer-Host %1$s): unvereinbar Lese/Schreib-modus. (client: %2$s, server: %3$s)
1140 Handshake-Fehler (Peer-Host %1$s): HA-Replikation verzögert.
1141 Handshake-Fehler (Peer-Host %1$s): Nachbildung client um nicht-Nachbildung Server verbunden
1142 Handshake-Fehler (Peer-Host %1$s): nicht identifizierten Server-Version.
1143 Handshake-Fehler (Peer-Host %1$s): Remote-Zugriff auf Server nicht erlaubt.
1144 Zeitüberschreitung versuchen um %1$s zu verbinden. (Zeitüberschreitung: %2$d sec(s))
1145 Ändern der Besitzer einer Systemklasse ist nicht erlaubt.
1146 Trägerkennung %1$d ist nicht vorhanden.
1147 Die Größe der XASL Strom-Cache(%1$d) überschreitet das Maximum(%2$d). Neuer Eintrag hinzugefügt wird, obwohl nichts zu Opfern, weil alle Cache-Einträge verwendet werden.
1148 Die Größe der Filterprädikat Strom-Cache(%1$d) überschreitet das Maximum(%2$d). Neuer Eintrag hinzugefügt wird, obwohl nichts zu Opfern, weil alle Cache-Einträge verwendet werden.
1149 Kann nicht finden die Seite %1$d von Volumen %2$d.
1150 Kontext erfordert kompatible Sortierungen.
1151 The page %1$d of volume %2$d is not a slotted page.
1152 The owner or a member of DBA group is only allowed to kill the transaction (%1$d).
1153 Attempt to use 'zero date'.
1154 MVCC row %1$d|%2$d|%3$d does not satisfies reevaluation.
1155 MVCC invalid row for delete: %1$d|%2$d|%3$d.
1156 MVCC can't get snapshot
1157 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.
1158 Serializable conflict due to concurent updates
1159 Letzter Fehler

$set 6 MSGCAT_SET_INTERNAL
1 Fehler in Fehler-Subsystem (Zeile %1$d):
2 Keine Fehlermeldung vorhanden.
3 %1$d Bytes können nicht zugewiesen werden.
4 Meldungskatalog "%1$s" nicht gefunden.
5 Fehlende Meldung für Fehlerkode %1$d.
6 ... ABORT/EXIT IMMEDIATELY ...\n
7 Wollen Sie beenden? 1/0
8 Unbekannte Datei.
9 Keine Meldung vorhanden; diese Meldung ist ein Ersatz für das falsch formatierte Original.
10 er_init: *** Ungültiger exit_ask Wert = %1$d; %2$s wird stattdessen angenommen. ***\n
11 er_start: *** WARNING: Meldungsprotokoll "%1$s" kann nicht geöffnet werden; stderr wird stattdessen angewendet. ***\n
12 er_study_fmt: zweifelhafte Meldung für Fehlerkode %1$d.
13 er_estimate_size: unbekannter Konversionskode (%1$d).
14 \n\n*** Meldungsprotokollzyklus. Meldungen werden am Anfang der Datei fortgesetzt. ***\n\n
15 \nTime: %1$s - %2$s *** %3$s CODE = %4$d, Tran = %5$d%6$s\n%7$s\n
16 CUBRID (pid: %1$d) *** %2$s *** %3$s CODE = %4$d, Tran = %5$d %6$s
17 \nTime: %1$s - %2$s *** Datei %3$s, Zeile %4$d %5$s CODE = %6$d Tran = %7$d%8$s\n%9$s\n
18 CUBRID (pid: %1$d) *** %2$s *** Datei %3$s, Zeile %4$d, %5$s CODE = %6$d, Tran = %7$d. %8$s
19 \n*** Die bisherige Fehlermeldung ist die letzte. ***\n\n
20 \nTime: %1$s - DEBUG *** Datei %2$s, Zeile %3$d\n
21 Mail -s "Der Server wurde gestoppt"
22 --->>>\n %1$s wurde auf Ihre Anfrage gestoppt, wenn der folgende Fehler eintrat:\n errid = %2$d, %3$s\nBenutzer: %4$s, pid: %5$d, Host: %6$s, time: %7$s<<<---\n
23 %1$s wurde auf errid = %2$d gestoppt. Benutzer: %3$s, pid: %4$d
24 er_init: Event-Handler kann nicht installiert werden "%1$s"\n

$set 7 MSGCAT_SET_PARSER_SYNTAX
1 Bitte Syntax überprüfen am '%1$s', %2$s erwartet.
2 Bitte Syntax überprüfen am '%1$s', %2$s in %3$s erwartet.
3 Überprüfen Sie bitte die Richtigkeit des Tokens '%1$s'.
4 'read uncommitted schema' ist nicht akzeptabel, \n'read committed schema' wird anstatt benutzt.
5 Maximale Länge der Zeichenstrings ist 16383.
6 %1$s überschreitet den Grenzwert %2$lld.
7 Maximale Länge der Bitstrings ist %1$d Bits.
8 Eie Subanfrage darf in einem Join-Bedingung-Klausel nicht angewendet werden.
9 Attribut "(%1$s)" kann in Update-Privileg-Klausel der grant/revoke Anweisung nicht spezifiziert werden.
10 Ungültiger Anwendung des Schlüsselwortes '%1$s', %2$s erwartet.
11 Token zu lang.
12 Ungültiger Datentyp
13 Ungültige Anhang-Anweisung \n  attach_statement: ATTACH unsigned_integer
14 Ungültige Prepare-Anweisung \n  prepare_statement:  PREPARE {to} COMMIT unsigned_integer
15 Ungültige Ausführen-Anweisung\n  execute_statement:  EXECUTE DEFERRED TRIGGER [identifier {, identifier}* | ALL TRIGGERS]
16 Ungültige Bereich-Anweisung\n  scope_statement: SCOPE trigger_action { FROM table_spec_list }
17 Ungültige set system parameter \n  set_system: SET SYSTEM PARAMETERS 'STRING' {, 'STRING_N'}*
18 Ungültiges set transaktion\n  set_Transaktion: SET TRANSAKTION ISOLATION LEVEL expression {, expression}\n                 | SET TRANSAKTION LOCK TIMEOUT timeout_spec
19 Ungültiges set trigger trace\n  set_trigger: SET TRIGGER TRACE [ON|OFF|unsigned_integer|parameter|host_param]
20 Ungültiges set trigger depth\n  set_trigger: SET TRIGGER {MAXIMUM} DEPTH [unsigned_integer|parameter|host_param]
21 Ungültiges set optimization level\n  SET OPTIMIZATION LEVEL {=|to} [ON|OFF|unsigned_integer|parameter|host_param]
22 Ungültiges set optimization cost\n  SET OPTIMIZATION COST {of} 'STRING' {=|to} literal_
23 Ungültiges get optimization level\n  GET OPTIMIZATION LEVEL {[INTO|TO] [host_param|parameter|identifier]}
24 Ungültiges get optimization cost\n  GET OPTIMIZATION COST {of} 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
25 Ungültiges get Transaktion isol\n  GET TRANSAKTION ISOL LEVEL {[INTO|TO] [host_param|parameter|identifier]}
26 Ungültiges get Transaktion lock\n  GET TRANSAKTION LOCK TIMEOUT {[INTO|TO] [host_param|parameter|identifier]}
27 Ungültiges get trigger trace\n  GET TRIGGER TRACE {[INTO|TO] [host_param|parameter|identifier]}
28 Ungültiges get trigger depth\n  GET TRIGGER {MAXIMUM} DEPTH {[INTO|TO] [host_param|parameter|identifier]}
29 Ungültiges get stat\n  GET STATISTICS 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
30 Ungültiges create Index\n  CREATE {REVERSE} {UNIQUE} INDEX identifier ON {only} class_name Index_column_name_list {WHERE where_clause}
31 Ungültiges create Benutzer\n  CREATE BENUTZER identifier {PASSWORD 'STRING'} {GROUPS identifier {, identifier}* } {MEMBERS identifier {, identifier}* }
32 Ungültiges create trigger\n  CREATE TRIGGER identifier {STATE [ACTIVE|INACTIVE]} {PRIORITY unsigned_real}\n    [BEFORE|AFTER|DEFERRED] event_spec {IF trigger_condition}\n    EXECUTE {AFTER|DEFERRED} trigger_action
33 Ungültiges create serial\n  CREATE SERIAL identifier {START WITH integer} {INCREMENT BY integer}\n    serial_min_max {CYCLE|NOCYCLE} {CACHE unsigned_integer|NOCACHE}
34 Ungültiges create procedure\n  CREATE PROCEDURE identifier '(' {sp_param_list} ')' {IS|AS} LANGUAGE JAVA NAME 'STRING'
35 Ungültiges create Funktion\n  CREATE FUNKTION identifier '(' {sp_param_list} ')' RETURN {data_type|CURSOR} {IS|AS} LANGUAGE JAVA NAME 'STRING'
36 Ungültiges groups\n  groups spec: GROUPS identifier {, identifier}*
37 Ungültiges members\n  member spec: MEMBERS identifier {, identifier}*
38 Ungültiges password\n  password spec: PASSWORD 'STRING'
39 Fehlende Liste autorisierter Befhele \n  authorized command = SELECT, INSERT, INDEX, DELETE, UPDATE, ALTER, ADD, DROP, EXECUTE, REFERENCES, ALL PRIVILEGES, ALL 
40 class spec Liste fehlt
41 Identifikator-Liste fehlt
42 Anweisung fehlt
43 Ungültiges Identifikator
44 Ungültige Verwendung des Substrings\n  SUBSTRING(Ausdruck FROM Ausdruck FOR Ausdruck)\n  SUBSTRING(Ausdruck FROM Ausdruck)
45 Ungültige Verwendung von trim\n  TRIM ( [LEADING|TRAILING|BOTH] Ausdruck FROM Ausdruck)\n  TRIM ( [LEADING|TRAILING|BOTH] FROM Ausdruck)\n  TRIM (Ausdruck FROM Ausdruck)\n  TRIM (Ausdruck)
46 Ungültige Verwendung von cast\n  CAST (Ausdruck AS data_type )
47 Funktion add_months braucht zwei Parameter
48 octet_length braucht einen Parameter
49 bit_length braucht einen Parameter
50 lower braucht einen Parameter
51 upper braucht einen Parameter
52 translate braucht drei Parameter
53 replace braucht zwei oder drei Parameter
54 Ungültige Verwendung von to_number\n  TO_NUMBER (Ausdruck, char_string )\n  TO_NUMBER (Ausdruck)
55 String bind Operator ist nicht '|' sondern '||'
56 Ungültige alter Anweisung 
57 Ungültige drop Anweisung 
58 Ungültige create Anweisung 
59 Ungültiges relationaler Operator
60 equal Operator ist nicht '==' sondern '='
61 not equal Operator ist nicht '!=' sondern '<>'
62 Ungültiger minus Wert (oder Minus Wer in der Liste)
63 Duplizierte Referenzregel
64 SYS_CONNECT_BY_PATH braucht zwei Parameter und der zweite muss ein Zeichenstring sein.
65 Ungültiges Integer ohne Vorzeichen %1$s.
66 IF braucht drei Parameter: der erste sollte eine logische Anweisung sein, der zweite und dritte sollten Anweisungen sein, die zu einem numerischen oder Zeichenstring evaluieren.
67 IFNULL braucht zwei Parameter, die zu einem numerischen oder Zeichenstring evaluieren, haben.
68 Ungültige Verwendung von isnull\n  ISNULL ( Ausdruck )
69 Ungültige Verwendung von left\n  LEFT ( str , len )
70 Ungültige Verwendung von right\n  RIGHT ( str , len )
71 Ungültige Verwendung von mod\n MOD( Ausdruck, Ausdruck )
72 Ungültige Verwendung von adddate\n ADDDATE( Ausdruck, Ausdruck )
73 Ungültige Verwendung von date_add\n DATE_ADD( Ausdruck, INTERVAL Ausdruck unit)
74 Ungültige Verwendung von subdate\n SUBDATE( Ausdruck, Ausdruck )
75 Ungültige Verwendung von date_sub\n DATE_SUB( Ausdruck, INTERVAL Ausdruck unit)
76 Ungültige Verwendung von Benutzer / system_Benutzer\n BENUTZER() / SYSTEM_BENUTZER()
77 Ungültige Verwendung von timestamp\n TIMESTAMP( Ausdruck ), TIMESTAMP( Ausdruck, Ausdruck )
78 Ungültige Verwendung von database\n DATABASE()
79 Ungültige Verwendung von current_time\n CURRENT_TIME()
80 Ungültige Verwendung von current_date\n CURRENT_DATE()
81 Ungültige Verwendung von current_timestamp\n CURRENT_TIMESTAMP()
82 Ungültige Verwendung von now\n NOW()
83 Ungültige Verwendung von default\n DEFAULT(column_name)
84 Ungültige Verwendung von date\n DATE(Ausdruck)
85 Ungültige Verwendung von localtime\n LOCALTIME()
86 Ungültige Verwendung von localtimestamp\n LOCALTIMESTAMP()
87 Ungültige Verwendung von schema\n SCHEMA()
88 Ungültige Verwendung von truncate\n TRUNCATE( Ausdruck, Ausdruck )
89 Ungültige Verwendung von str_to_date\n STR_TO_DATE( Ausdruck, string_literal )
90 Ungültige Verwendung von current_datetime\n CURRENT_DATETIME()
91 Ungültige Verwendung von insert\n INSERT( Ausdruck, Ausdruck , Ausdruck, Ausdruck )
92 Ungültige Verwendung von elt\n ELT(Ausdruck [, Ausdruck]...)
93 Ungültige Verwendung von year\n YEAR( Ausdruck )
94 Ungültige Verwendung von month\n MONTH( Ausdruck )
95 Ungültige Verwendung von day\n DAY( Ausdruck )
96 Ungültige Verwendung von hour\n HOUR( Ausdruck )
97 Ungültige Verwendung von minute\n MINUTE( Ausdruck )
98 Ungültige Verwendung von second\n SECOND( Ausdruck )
99 Ungültige Verwendung von quarter\n QUARTER( Ausdruck )
100 Ungültige Verwendung von week\n WEEK( Ausdruck ), WEEK( Ausdruck, Ausdruck)
101 Ungültige Verwendung von time\n TIME(Ausdruck)
102 Ungültige Verwendung von group_concat\n GROUP_CONCAT (([DISTINCT] {col | expr} \n [ORDER BY {col | unsigned_int} [ASC | DESC] ] \n [SEPARATOR str_val])
103 Ungültige Verwendung von Index-Hint
104 Maximale Byte String Lenge ist %1$d Bytes.
105 Syntax Fehler: unexpected
106 , expecting '{'
107 , expecting SELECT or '('
108 , expecting
109 unexpected
110 END OF STATEMENT
111 In line %1$d, column %2$d before '%3$c%4$s'\n
112 In line %1$d, column %2$d before %3$s\n
113 before ' %1$s'\n
114 before END OF STATEMENT\n
115 ungültiges set names\n  set_names: SET NAMES charset [COLLATE collation]\n
116 Ungültige Verwendung von chr\n  CHR ( number [using charset] )\n
117 Ungültige Nutzung des Charsets\n CHARSET ( expression )\n
118 Ungültige Nutzung der Kollation\n COLLATION ( expression )\n
119 Ungültige Nutzung des clob_to_char\n CLOB_TO_CHAR ( expression [using charset] )\n
120 Ungültige Nutzung des index_prefix\n  INDEX_PREFIX ( expression, expression, expression )\n
121 Die falsche Verwendung von UNION und ORDER BY
122 Expecting ON. Outer Joins require join condition.

$set 8 MSGCAT_SET_PARSER_SEMANTIC
1 Außer Speicher.
2 Funktion %1$s ist nicht definiert.
3 Ldb '%1$s' wurde in der Datenbank nicht gefunden.
4 Die Spaltenanzahl %1$d, in der linken Anfrage ist nicht gleich\nmit der Spaltenanzahl %2$d, in der rechten Anfrage.
5 '%1$s' ist nicht Vereinigungskompatibel mit '%2$s'.
6 %1$s ist nicht definiert.
7 Die Domain von %1$s darf keine V-Klasse sein.
8 Die Domain von %1$s darf keine Proxy V-Klasse sein.
9 Die Domain von %1$s darf keine Klasse sein.
10 %1$s ist kein Attribut von %2$s.
11 %1$s dar aus dieser Klasse nicht geändert werden.\nEs wurde von %2$s geerbt.
12 Klasse %1$s existiert nicht.
13 %1$s ist kein %2$s.
14 %1$s ist keine Methode oder Attribut von %2$s.
15 %1$s hat keine, und darf keine Anfragespezifikationen haben.
16 %1$s ist in der Datenbank nicht definiert.
17 %1$s ist keine Superklasse von %2$s.
18 Klasse %1$s erbt von Nicht-Klasse %2$s.
19 V-Klasse %1$s erbt von Nicht-V-Klasse %2$s.
20 Proxy V-Klasse %1$s erbt von Non-Proxy V-Klasse %2$s.
21 %1$s ist bereits eine Superklasse von %2$s.
22 %1$s darf keine Superklasse von selbst sein.
23 %1$s ist bereits eine Unterklasse von %2$s.
24 %1$s hat keinen eindeutigen Wert. Attribute, die in \naggregierten Abfragen ausgesetzt sind, müssen auch in der Gruppe bei dem Klausel erscheinen.
25 Die Anzahl der Spalten (%1$d) und into_names (%2$d) sind nicht gleich.
26 Klasse %1$s existiert bereits.
27 Superklasse %1$s in '%2$s' existiert nicht.
28 Eine SUBKLASSEN-Klausel erwartet, um die INHERIT Klausel '%1$s' sinnvoll zu machen.
29 INHERIT Klausel '%1$s' referenziert eine Klasse, die in der SUBCLASS Klausel '%2$s' nicht beinhaltet ist.
30 Die Klasse DARF keine Anfragespezifikation haben.
31 %1$s ist kein registriertes ldb.
32 Ein Proxy-Erbe darf keine eigene object_id Klausel haben. Es darf sein(e) geerbtes(en) object_id Attribut(e) nicht überschreiben.
33 Proxy V-Klasse hat mehr als eine Anfragespezifikation.
34 Die Anzahl der Attribute %1$d ist nicht gleich mit der Anzahl der Spalten \n%2$d, in der Anfragespezifikation.
35 Ein erwarteter Attributname für %1$s fehlt.
36 Attribut '%1$s' ist mit Spalte '%2$s' der Anfragespezifikation nicht kompatibel.
37 %1$s ist keine Klasse.
38 Indices dürfen auf virtuellen Klassen nicht erstellt werden.
39 Indices können nur auf einem einzigen Attribut erstellt werden.
40 Unerlaubter Nutzung von multiplen Klassen in der from Klausel.
41 Systemfehler -- Resolution ist auf %1$s gescheitert.
42 Sie dürfen von einer abgeleiteten Tabelle nicht löschen.
43 Benutzer %1$s ist nicht in der Datenbank.
44 Das Update darf keine Aggregation haben.
45 Attributnamen in jeder Klasse müssen eindeutig sein. Attributname '%1$s' in Klasse '%2$s' ist mehr als einmal angegeben.
46 Es gibt mehrere Zuordnungen zu %1$s.%2$s.
47 Zahl in sort spec ist kein Integer.
48 %1$d in sort spec ist außer zulässigem Bereich.
49 „Order by“ ist kein Integer.
50 %1$s in sort spec entspricht keiner Select-Spalte.
51 Es gibt widersprüchliche Sortierungsrichtungen für Spalte %1$s.
52 %1$s in sort spec ist keine Zahl, Name oder Pfadausdruck.
53 Insert Ausdruck darf nur in Insert-Statements auftauchen.
54 Die Anzahl der Attributen(%1$d) und die der Select-Spalten (%2$d) sind nicht gleich.
55 Die Anzahl der Attributen(%1$d) und die der Werte(%2$d) sind nicht gleich.
56 Datenüberlauf auf Datentyp %1$s.
57 Inkompatibles Datentyp für Attribut %1$s.
58 %1$s\n ist kein SELECT einer einzigen Klasse.
59 `*' ist keine gültige Select-Liste einer Proxy V-Klasse-Anfragespezifikation.
60 %1$s ist weder eine V-Klasse noch eine Proxy V-Klasse.
61 Es gibt mehr als eine Isolationsstufe-Klausel.
62 Es gibt mehr als eine Timeout-Klausel.
63 Keine Isolationsstufe wurde eingegeben.
64 BETWEEN-Operator ist nicht definiert für Typen %1$s, %2$s und %3$s.
65 '%1$s' Operator ist nicht definiert für Typen %2$s und %3$s.
66 Die Funktion muss Argumente haben: %1$s.
67 Inkompatible Operanden: %1$s(%2$s).
68 Aggregate-Funktion muss 1 Argument haben: %1$s.
69 Sie versuchen, durch Null zu teilen.
70 Datenüberlauf bei der Zwangskonvertierung von %1$s auf dem Typ %2$s.
71 %1$s kann nicht zwangskonvertiert zu Typ %2$s sein.
72 Der Typ von '%1$s' muss SET, MULTISET, oder SEQUENCE sein.
73 '%1$s' sollge genau ein Set-Element haben.
74 %1$s hat mehr als einen abgeleiteten Spaltenname.
75 %1$s hat keinen abgeleiteten Spaltenname.
76 %1$s hat keine entsprechende Spaltenanzahl (%2$d) und Spaltennamen (%3$d).
77 Es gibt keine Attribute in %1$s.
78 Eine einzige Tabelle wird in %1$s erwartet.
79 Der Name %1$s ist nicht eindeutig.
80 Abgeleitete Spaltennamen '%1$s' sind für simple Klassenspezifikationen nicht erlaubt.
81 Abgeleitete Tabelle '%1$s' muss einen Korrelation-Namen haben.
82 Select Berechtigungsfehler, Klasse %1$s.
83 Referenz der Klasseninstanz %1$s auf eine abgeleitete Tabelle ist nicht erlaubt.
84 Pfad Korrelation (%1$s) scheint außerhalb eines Inner-Pfad-Terms zu sein.
85 Referenz zu %1$s ist mehrdeutig.
86 Klasse von %1$s hat kein Attribut %2$s.
87 Nicht alle Klassen (%1$s) haben ein Attribut %2$s.
88 Klasse (%1$s) hat kein Attribut %2$s.
89 %1$s ist ein %2$s Typ, kein Objekttyp.
90 Selektor {%1$s} übersetzt nicht zu einer exponierten Klassespezifikation in\n dem erforderlichen Kontext (%2$s).
91 Selektor {%1$s} referenziert eine Non-Klassenspezifikation (%2$s).
92 Selektor {%1$s} kann nicht rooten, der Pfadausdruck definiert sich selbst.
93 Selektor {%1$s} ist kreisförmig definiert im Bezug auf Selektor {%2$s}.
94 Selektor {%1$s} wird am %2$s neudefiniert.
95 %1$s (Domainobjekt) hat kein Attribut %2$s.
96 %1$s in EXCEPT spec ist keine Subklasse.
97 ALL ... EXCEPT Liste schließt EVERYTHING in Klasse aus.
98 '%1$s' ist inkonsistent mit\nanderer Anwendung des Pfad-Selektors '%2$s' oder des Root-Pfads '%3$s'.
99 Nicht definiertes Argument.
100 Das Argument ist kein Objekt.
101 %1$s ist keine Klassenmethode oder Attribut des %2$s.
102 Anfragespezifikation darf keine Into-Klausel haben.
103 Proxy Anfragespezifikation darf keinen Aggregatausdruck,\nkeine Group-By-Klausel und keine Having-Klausel haben.
104 Proxy Anfragespezifikation darf weder DISTINCT noch UNIQUE \nSchlüsselwörter in der Abfrageliste haben.
105 Proxy Anfragespezifikation muss eine simple SELECT Anweisung sein.
106 Proxy Anfragespezifikation darf nur eine Tabelle oder Klasse \naus der lokalen Datenbank referenzieren.
107 Benutzer '%1$s' ist nicht der Eigner von %2$s.
108 Proxy '%1$s' ist bereits in der Klasse '%2$s' von ldb '%3$s' definiert.
109 CUBRID unterstützt Proxys mit Objekten in heterogenen Sets nicht.
110 Where-Klausel muss ein logischer Ausdruck sein.
111 Like Escape-Klausel muss eine Zeichenkette oder eine Hostvariable sein.
112 Ungültige Kombination von Präzision (%1$d) und Skala (%2$d).
113 Angegebene Präzision von %1$d ist ungültig; es sollte größer als %2$d und nicht größer als %3$d sein.
114 Präzision (%1$d) zu Groß. Maximale Präzision ist %2$d.
115 Die Methode %1$s muss einen Rückgabetyp haben.
116 Anzahl der übergebenen Argumente an die Methode entspricht nicht der \nAnzahl definiert für die Methode.
117 Methodenanruf muss einen Ziel haben.
118 Methode %1$s ist für den angegebenen Ziel nicht definiert.
119 Instanz-Methoden dürfen auf Klassenobjekte nicht ausgeführt werden.
120 Klassenmethoden dürfen nicht aus Instanz-Objekten ausgeführt werden.
121 Klassenmethoden erfordern ein Objekt der Klasse als Ziel.
122 Methoden erfordern ein Objekt als Ziel.
123 Methode %1$s ist nicht vollständig eingegeben worden, überprüfen Sie die Methodendefinition in der Klassenschema.
124 Zeitberechnungen Unterlauf.
125 Datumsberechnungen Unterlauf.
126 Ungültige linke Seite einer Zuweisungsklausel.
127 Methodenanrufe auf set-abgeleiteten Tabellen korreliert mit einem äußeren Scope \nsind nur dann unterstützt, wenn der Ausdruck in einer Subanfrage eingepackt ist.
128 The Anfragespezifikationspalte, die zum shared \nAttribut '%1$s' muss NULL oder NA sein.
129 Eine NULL-Spalte ist auf der Auswahlliste der Anfragespezifikation, wenn die Attributdefinition weggelassen ist.
130 V-Klassenobjekte sind in Sets nicht unterstützt.
131 String erwartet.
132 Eine Anweisung oder ein String war erwartet.
133 Proxy object_id Attribute '%1$s' darf nicht zur \na Anfragespezifikationspalte-Ausdruck mappen.
134 %1$s Einschränkung ignoriert.
135 Einschränkungdeklaration in diesem Kontext nicht erlaubt.
136 Casting %1$s zum Tuyp %2$s ist nicht unterstützt.
137 %1$s ist kein valider Bit-String.
138 '%1$s' Operator ist für Type %2$s nicht definiert.
139 '%1$s' Operator ist für Typen %2$s mit %3$s und %4$s nicht definiert.
140 Operand des Operators '%1$s' dürfen nicht beide dynamische Hostvariablen sein.
141 Zu vielen Argumente für die generische Funktion %1$s.\nNur %2$d Argumente sind für generische Funktionen erlaubt.
142 Selektor {%1$s} Klasse '%2$s' ist keine Subklasse von '%3$s'.
143 Feld '%1$s' kann von Typ %2$s nicht extrahiert werden.
144 Anfragespezifikation darf keine ORDER BY Klausel haben.
145 Case-Bedingung muss ein logischer Ausdruck sein.
146 Ausdruck sollte Typ %1$s sein.
147 Seriennummer '%1$s' existiert bereits.
148 Seriennummer '%1$s' nicht definiert.
149 Inkrement-Wert darf nicht Null sein.
150 Der maximale Wert ist größer als 1.0e37.
151 Der minimale Wert ist ungültig.
152 Inkrement-Wert ist ungültig.
153 Der minimale Wert ist kleiner als -1.0e36.
154 Der maximale Wert ist ungültig.
155 Geben Sie eine andere Serielle Option ein.
156 Das zweite Argument der serial_next_value Funktion sollte größer als Null sein.
157 Serielle Werte dürfen nicht ihre minimale oder maximale Werte überschreiten.
158 Interner I/O Fehler für Seriell
159 Index '%1$s' kann nicht gefunden werden.
160 Der qualifizierte Indexname '%1$s' bezieht sich auf eine Klasse, er ist aber in der from-Klausel nicht angegeben.
161 Zwei oder mehrere Klassen haben denselben Index '%1$s'. Die qualifizierten Indexnamen anwenden.
162 Ungültige Anwendung von %1$s. Der \n%2$s Ausdruck kann mit einem Attribut oder einem korrelierten Subanfrage verwendet werden.
163 Aktuelle Version erlaubt keinen Ausdruck mit ORDERBY_NUM () in der Auswahlliste.
164 Pfadausdruck '%1$s' ist in Join-Kondition.
165 '%1$s' in Join-Kondition ist nicht definiert.
166 Aktuelle Version erlaubt keine set-abgeleitete Tabelle.
167 Kann nicht mit Abfrage auf anderen ldb einfügen.
168 Nur eine Spalte in der Auswahlliste kann angegeben werden, wenn die Subanfrage nicht mit EXISTS eingeleitet worden ist.
169 Ungültige rechte Seite einer Zuweisungsklausel. Kontext erfordert Anfrage auf einzelne Spalte.
170 Eine Subanfrage darf in einer join-Konditionsklausel nicht verwendet werden.
171 Ungültige Partitionsdefinition.
172 Partitionspalte muss definiert werden.
173 Nur eine Spalte kann als Partitionsspalte definiert werden.
174 Nicht unterstützter Typ von Partitionsspalte .
175 Partitionsdefinition ist mehrdeutig. '%1$s'
176 Konstanten Typ  Unterschied. '%1$s'
177 Ungültige Partitionsgröße.
178 '%1$s' ist keine partitionierte Klasse.
179 '%1$s' ist bereits eine partitionierte Klasse.
180 Ungültige Partition-Requests.
181 %1$s kann aus dieser Klasse nicht geändert (Partitionsschlüsselspalte).
182 %1$s darf unter den aktiven Triggern nicht Partition-verwaltet werden.
183 Partition %1$s existiert nicht.
184 Partitionstyp Unterschied. '%1$s'
185 Partition Information ungültig.
186 Partition-Addition ist mit einem größeren Bereich als das bestehende gesetzt.
187 Neuorganisierung der Partitionen ist wegen Datenverlustgefahr nicht erlaubt.
188 Der Bereich der Partition '%1$s' ist ungültig.
189 Ein Klassenattribut darf keine Auto-Inkrement-Einschränkung haben.
190 Ein Attribut einer virtuellen Klasse darf keine Auto-Inkrement-Einschränkung haben.
191 Die Domain des Attributs '%1$s' mit einer Auto-Inkrement-Einschränkung ist ungültig.
192 Das System kann das Attribut '%1$s' erstellt durch 'ON CACHE OBJECT' des Fremdschlüssels zuteilen, und kein Benutzer.
193 Doppelte Attributnamen können in der Indexschlüsselliste nicht verwendet werden. Attributname '%1$s' ist mehrmals gelistet.
194 Funktion %1$s ist nicht definiert oder hat eine falsche Anzahl von Parameter anbekommen.
195 '%1$s' kann nur in hierarchische Abfragen verwendet werden. Eine CONNECT BY Klausel angeben.
196 '%1$s' ist hier nicht erlaubt.
197 Das Verwenden der CONNECT_BY_ISCYCLE ist nur dann erlaubt, wenn die Anfrage eine CONNECT BY NOCYCLE Klausel hat.
198 Keine Tabellen verwendet.
199 ORDER BY NULL erfordert GROUP BY.
200 Die Nummer zum CACHE muss kleiner als ein Zyklus sein (%1$s).
201 '%1$s' ist in einer Anfrage mit einer LIMIT Klausel nicht erlaubt.
202 WITH ROLLUP erfordert GROUP BY.
203 Ein Spaltenname Parameter sollte auf die DEFAULT Funktion übergeben werden. Pfadausdrücke sind noch nicht unterstützt.
204 Duplizierte TabellenOption '%1$s'
205 Die Klasse '%1$s' ist als REUSE_OID markiert und ist nicht referenzierbar. Nicht-referenzierbare Klassen dürfen keine Domain eines Attributs sein, und ihre Instanzen OID können nicht zurückgegeben werden.
206 Duplizierte 'serial %1$s' Option.
207 Die Spalte '%1$s' in Anordnungsklausel ist mehrdeutig.
208 Attribut '%1$s' hat keinen Standardwert.
209 ORDER BY Items müssen in der Select-Liste erscheinen, wenn SELECT DISTINCT angegeben ist.
210 Hostvariable ist in Spalten der Anfragespezifikation nicht erlaubt.
211 %1$s darf keine ORDER BY Spalte sein
212 %1$s darf keine GROUP BY Spalte sein
213 '%1$s von %2$s' kann nicht als Spaltentyp definiert werden.
214 Ungültiger Wert "%1$s" für Präfixlenge des Indexes.
215 '%1$s' Funktion ist nicht definiert für Typen %2$s, %3$s, %4$s und %5$s.
216 '%1$s' Funktion ist nicht definiert für Typen %2$s, %3$s und %4$s.
217 '%1$s' Funktion ist nicht definiert für Typen %2$s und %3$s.
218 '%1$s' Funktion ist nicht definiert für Typ %2$s.
219 '%1$s' Funktion ist nicht definiert für ein Indexargument von Typ %2$s.
220 ALTER VIEW '%1$s' AS SELECT ist für Views mit Sub- oder Superklassen nicht erlaubt. ALTER VIEW CHANGE ANFRAGE anstatt verwenden.
221 View-Attribute können mit dieser Anweisung nicht geändert werden. 'ALTER VIEW Name AS Anfrage' könnte anstatt hilfreich sein.
222 CUBRID erfordert, dass das ORDER BY Item der gleiche Ausdruck wie GROUP_CONCAT Argument oder ein Integer (Argument Position) ist.
223 Das Prädikat LIKE erfordert ein Escape-Zeichen. Lesen Sie die Dokumentation für die '%1$s' und '%2$s' Konfigurationsparameter.
224 KEYLIMIT darf in der INDEX NONE Klausel nicht verwendet werden.
225 Session-Variable in diesem Kontext nicht erlaubt.
226 Indexhinweis Konflikt zwischen '%1$s' und '%2$s'.
227 '%1$s' Funktion darf in der DEFAULT Klausel nicht verwendet werden.
228 Aktualisierung abgeleiteter Tabellen ist nicht erlaubt.
229 Die Aktualisierung der Klassenattributen und der Instanz-Attributen ist in der gleiche Anweisung nicht erlaubt.
230 Die Aktualisierung der virtuellen Klassenhierarchien ist nicht erlaubt.
231 Hostvariablen nicht erlaubt in DDL-Anweisungen.
232 Ungültiger Filterausdruck (%1$s) für Index.
233 Multiple oder verschachtelte Funktionen sind in der Definition eines Funktionsindexes nicht erlaubt.
234 Ungültige Indexspezifikation.
235 SHARED, DEFAULT und AUTO_INCREMENT dürfen nicht untereinander definiert werden.
236 Zu viele Aufzählungswerte (%1$d). Ein Aufzählungstyp darf maximum %2$d Werte.
237 Doppelte Werte in enum Typ.
238 MERGE kann nicht ins eine abgeleitete Tabelle angewendet werden.
239 Eine MERGE-Einweisung kann keinen Einfluss auf eine Quelltabelle haben.
240 MERGE mit Klassenhierarchien ist nicht erlaubt.
241 Benutzername zu lang.
242 Konstanter Ausdruck kann nicht als Funktionsindex angewendet werden.
243 Klassenname oder Alias ist in der Liste: '%1$s' nicht eindeutig.
244 Ungültige DEFAULT Klausel. '%1$s' darf in einer verschachtelten Anweisung nicht angewendet werden.
245 '%1$s' erfordert Argumente mit kompatibler Sortierfolge.
246 Ungültiger Zeichensatz.
247 Erforderte Zeichensatz und Sortierfolge sind nicht kompatibel.
248 Unbekannte Sortierfolge '%1$s'.
249 Kontext erfordert Kollektion mit Elementen mit identischen Zeichensätzen und Sortierfolgen.
250 Präfixindex ist für Attribut '%1$s' nicht erlaubt (hat Sortierfolgen mit Erweiterungen).
251 Initialwert ist ungültig.
252 Multi-Spalten-Präfixindex nicht erlaubt.
253 '%1$s' Funktion kann für funktionsbasierten Indexe nicht angewendet werden.
254 Invalide Indexspaltendefinition: %1$s
255 '%1$s' ist in einem Filterausdruck für Index nicht erlaubt.
256 GROUPBY_NUM() und WITH ROLLUP Klausel dürfen in der gleichen Anfrage nicht angewendet werden.
257 Kann nicht MERGE UPDATE Spalte %1$s die in ON-Klausel referenziert ist.
258 Die Spaltenanzahl %1$d, in ( %2$s ) ist nicht gleich mit \nder Spaltenazahl %3$d, in ( %4$s ).
259 Spaltenpräfixen in Funktion-Index-Ausdrücke nicht erlaubt.
260 %1$s darf keine PARTITION BY oder ORDER BY Spalte
261 Anfrage muss einen einzigen Wert zurückgeben\n(%1$s).
262 Verschachtelte analytische Funktionen sind nicht erlaubt.
263 Ein vererbter Attribut darf nicht als Partitionierungsschlüssel verwendet werden.
264 Das Gesamtvolumen der Aufzählungselemente ist zu groß.
265 Zieltabellen können in Quelle-specs nicht verwendet werden.
266 COLLATE ist in diesem Kontext nicht erlaubt.
267 COLLATE Modifizierer kann den Codeset nicht ändern (von '%1$s' auf '%2$s'). Versuchen Sie es mit CAST.
268 Ungültiger Codeset '%1$s' für angegebene Partition. '%2$s' Codeset erwartet.
269 Die Session Variable kann nicht eine andere Kollation als die Systemkollation ('%1$s') haben.
270 Ungültige Funktion Indexausdruck.
271 Die Abfrage von %1$s hat einen zyklischen Verweis auf sich selbst.
272 %1$s Ausdruck ist in Join-Bedingung nicht erlaubt.
273 ORDER BY ist in Funktion MEDIAN nicht zugelassen.
274 Das Argument der Funktion '%1$s' muss eine Konstante sein.
275 Funktion '%1$s' muss eine ORDER BY Klausel haben.
276 Für die Funktion '%1$s', die Anzahl der Argumenten ist nicht gleich der Anzahl der ORDER BY Spalten.
277 Für Funktion '%1$s', die ORDER BY Klausel ist ungültig.
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
288 Kann nicht alle Partitionen zu entfernen, verwenden Sie stattdessen DROP TABLE.
289 The expression including %1$s is not allowed in this context.
290 Transaction id is not an integer.
291 Subquery is not allowed in ORDER BY FOR clause.

$set 9 MSGCAT_SET_PARSER_RUNTIME
1 Außer virtuellem Speicher: %1$d Bytes können nicht zugewiesen werden.
2 Transaktion Isolationslevel muss 1, 2, 3, 4, 5 oder 6 sein.
3 Timeout Wert muss -1, 0, oder >0 sein.
4 Isolationslevel ist:\n
5          Unknown isolation.\n
6          Unknown isolation.\n
7          Unknown isolation.\n
8          READ COMMITTED\n
9          REPEATABLE READ\n
10 Eine nicht aktualisierbare Anfrage ist in einem db_Anfrage_execute_oid Aufruf nicht erlaubt.
11 Transaktion Timeout beim Warten auf Sperren.
12 Es wurde versucht, die Hostvariable mit Index %1$d zuzugreifen, wenn nurr %2$d \nInput Hostvariablen angegeben worden sind.
13 Nicht implementierte Umwandlung.
14 Interner Fehler, undefinierte Umwandlung.
15 '%1$s' liefert keine Ergebnisse.
16 '%1$s' liefert mehr als eine Zeile.
17 Interpreter Variable %1$s ist nicht definiert.
18 Kann '%1$s' nicht evaluieren.
19 Unbekannte Variable %1$s.
20 Unbekannter Shared-Attribut %1$s.
21 '%1$s' liefert mehr als eine Spalte.
22 Verschachtelte oder ungültige Anwendung der aggregierten Funktion.
23 Funktion %1$s is not defined.
24 Kontext erfordert, dass die Anfrage eine einzige Spalte hat \n(%1$s).
25 Zu viele Werte angegeben.
26 Virtueller Zuordnungsziel (%1$s) ist nicht invertierbar.
27 Es gibt mehrere Spalten in der Anfragespezifikation als Attribute in der Klasse %1$s.
28 Es gibt mehrere Attribute in der Klasse %1$s als Spalten in der Anfragespezifikation.
29 Aktueller Version erlaubt die Aggregation auf V-Klassen OHNE \n aggregierte Anfragespezifikationen nicht.
30 Aktueller Version erlaubt die Aggregation auf V-Klassen OHNE \n Vereinigung der Anfragespezifikationen nicht.
31 Die Verwendung der V-Klasse (%1$s) in einer Zusammensetzung Hierarchie erfordert, dass diese aktualisierbar ist.\nSie kann im Bezug auf die reale Klasse %2$s nicht aktualisiert werden.
32 nicht verwendet
33 Ungültiger Konstrukt im Pfadausdruck '%1$s'.
34 Pfadausdruck '%1$s' überquert die DB-Grenze zu %2$s, mit V-Klasse %3$s.
35 Pfadausdruck '%1$s' überquert die DB-Grenze von %2$s, zu Klasse %3$s.
36 %1$s ist auf %2$s nicht autorisiert.
37 Select ist auf %1$s nicht autorisiert.
38 V-Klasse %1$s ist nicht aktualisierbar.
39 %1$s ist keine Proxy V-Klasse.
40 Anfrage spec der Proxy-V-Klasse darf kein join sein.
41 %1$s hat keinen Objekt-ID.
42 Klassen (%1$s) mit mehreren object_id Feldern in einer Komposition\n Hierarchie sind nicht unterstützt.
43 Objektattribut %1$s von %2$s ist keine V-Klasse auf ldb %3$s.
44 Anfrage spec Typ ist mit Attribut %1$s nicht kompatibel.
45 Die Anfragespezifikation von %1$s hat einen cyclischen Verweis auf sich selbst.
46 Kein Ausdruck auszuwerten.
47 Keine reale Klasse für V-Klasse %1$s gefunden.
48 Ausführung von '%1$s' auf ldb '%2$s' \nhat: %3$s geliefert.
49 Verwendung des Wertes '%1$s' als Attribut '%2$s' lieferte: %3$s.
50 Ldb-Name darf nicht Null in %1$s sein.
51 Ldb-Name beim Host darf nicht Null in %1$s sein.
53 Hostname darf nicht Null in %1$s sein.
54 Benutzer darf nicht Null in %1$s sein.
55 Benutzerpasswort darf nicht Null in %1$s sein.
56 Insert übersetzt zu einer leeren Anweisung.
57 Update übersetzt zu einer leeren Anweisung.
58 Delete übersetzt zu einer leeren Anweisung.
59 INGRES unterstützt kein Datum vor (%1$s) vor dem 1. Januar 1582.
60 Ressourcen bei der Generation der Anfragen erschöpft.
61 Anfrage liefert kein Ergebnis, also die Variable '%1$s' ist nicht gesetzt.
62 With LOCAL check-Option ist nicht unterstützt.
63 Check-Option Ausnahme auf View %1$s.
64 Ungültiges Datum: '%1$s'.
65 Ungültige Zeit: '%1$s'.
66 Ungültiges utime: '%1$s'.
67 Subabfragen in einer Proxy-Anfragespezifikation sind nicht unterstützt.
68 Ldb-Funktion-Argument ist mit ldb in '%1$s' nicht kompatibel.
69 Ungültige Zahl: '%1$s'.
70 Outer join Anfrageoptimierung gescheitert.
71         SERIALISIERBAR.\n
72 V-Klasse %1$s ist nicht aktualisierbar. Nur aktualisierbarer V-Klassen ist es erlaubt, VIDs zu selektieren.
73 Partitionierung gescheitert.
74 Partition existiert nicht.
75 Zugang zur Partition: '%1$s' verweigert.
76 Das Serial '%1$s' ist noch nicht definiert.
77 Nur der Eigner oder DBA darf die Definition des Ziel-Serials ändern.
78 Es ist Benutzers nicht erlaubt, ein von einer auto-inkrement Einschränkung  erstelltes Serial-Objekt zu ändern.
79 Ungültiges Isolationslevel: %1$s Schema, %2$s Instanzen.
80 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.

$set 11 MSGCAT_SET_AUTHORIZATION
1 *** Ungültiges Cache ***
2    Klasse %1$s
3 von %1$s :
4 Berechtigung für Benutzer %1$s\n
5 Berechtigung für undefinierter Benutzer \n
6 Benutzer : %1$s\n
7            ID : %1$d\n
8       Mitglieder:
9        Gruppen:
10 Benutzer %1$s\n
11 Aktueller Benutzer ist : %1$s\n
12 Hauptautorisationsobjekt  :\n
13   Benutzer :
14 *** Konnte nicht alle Zuschüsse für Benutzer erteilen "%1$s"*** \n
15 AUTHORISIERUNGEN\n
16 direct_groups :

$set 12 MSGCAT_SET_HELP
1 Rootkllasse
2 %1$s %2$s\n
3 Superklassen:\n
4 Subklassen:\n
5 Attribute: \n
6 Methoden: \n
7 Klassenattribute: \n
8 Klassenmethode: \n
9 Konfliktlösungen:\n
10 Methodendateien:\n
11 Anfragespezifikation:\n
12 OBJECT von %1$s\n
13 \nBeschreibunt:\n
14 \nStruktur:\n
15 \nBeispiel:\n
16 META KLASSE Name
17 Klassenname
18 V-Klassenname
$  Don't delete the space at the end of the next message
19 V-Klasse auf ldb 
20 ALLGEMEIN

$set 13 MSGCAT_SET_TRIGGER
1 TRACE: Auswertung der Bedingung für Trigger '%1$s'.
2 TRACE: Auswertung der Aktion für Trigger '%1$s'.

$set 14 MSGCAT_SET_LOCK
1 \n
2 Thread (Index %1$3d) der Transaktion (Index %2$3d, %3$s, %4$s@%5$s|%6$d) wurde ausgesetzt.\n
3 Thread (Index %1$3d) der Transaktion (Index %2$3d, %3$s, %4$s@%5$s|%6$d) wurde fortgesetzt.\n
4 Transaktionenindex %1$3d ERHIELTE %2$s Lock auf Objekt %3$d|%4$d|%5$d.\n
5 Transaktionenindex %1$3d ERHIELTE %2$s Lock auf Seite %3$d|%4$d.\n
6 Transaktionenindex %1$3d KONVERTIERTE sein Lock auf %2$s Lock auf Objekt %3$d|%4$d|%5$d.\n
7 Transaktionenindex %1$3d KONVERTIERTE sein Lock auf %2$s Lock auf Seite %3$d|%4$d.\n
8 Transaktionenindex %1$3d WARTET AUF %2$s Lock auf Objekt %3$d|%4$d|%5$d.\n
9 Transaktionenindex %1$3d WARTET AUF %2$s Lock auf Seite %3$d|%4$d.\n
10 Transaktionenindex %1$3d FREIGIBT %2$s Lock auf Objekt %3$d|%4$d|%5$d.\n
11 Transaktionenindex %1$3d FREIGIBT %2$s Lock auf Seite %3$d|%4$d.\n
12 Transaktionenindex %1$3d DEGRADIERT Lock auf %2$s auf Objekt %3$d|%4$d|%5$d.\n
13 Transaktionenindex %1$3d DEGRADIERT Lock auf %2$s auf Seite %3$d|%4$d.\n
14 OID = %1$2d|%2$6d|%3$4d\n
15 Objekttyp: Root class.\n
16 Objekttyp: Class = %1$s.\n
17 Objekttyp: Instance of class (%1$2d|%2$6d|%3$4d) = %4$s.\n
18 Objekttyp: Unknown.\n
19 Total mode of holders = %1$8s, Total mode of waiters = %2$8s.\n
20 Num holders=%1$3d, Num blocked-holders=%2$3d, Num waiters=%3$3d\n
21 LOCK HOLDERS:\n
22 BLOCKED LOCK HOLDERS:\n
23 LOCK WAITERS:\n
24 NON_2PL_RELEASED:\n
25 %1$4sTran_Index = %2$3d, Granted_mode =%3$8s, Anzahl = %4$3d\n
26 %1$4sTran_Index = %2$3d, Granted_mode =%3$8s, Anzahl = %4$3d, Nsubgranules = %5$2d\n
27 %1$4sTran_Index = %2$3d, Granted_mode =%3$8s, Anzahl = %4$3d\n\t%5$13s Blocked_mode = %6$8s\n\t%7$17s Start_waiting_at = %8$-30s\n\t%9$17s Wait_for_secs = %10$.2f\n
28 %1$4sTran_Index = %2$3d, Granted_mode =%3$8s, Anzahl = %4$3d, Nsubgranules = %5$2d\n\t%6$13s Blocked_mode =%7$8s\n\t%8$13s Start_waiting_at = %9$-30s\n\t%10$13s Wait_for_secs = %11$.2f\n
29 %1$4sTran_Index = %2$3d, Blocked_mode =%3$8s \n\t%4$13s Start_waiting_at = %5$-30s \n\t%6$13s Wait_for_secs = %7$.2f\n
30 %1$4sTran_Index = %2$3d, Non_2_phase_lock =%3$8s\n
31 VPID (volid|pageid) = %1$3d|%2$6d.\n
32 *** Locktabelle Dump ***\n Lock Eskalation bei = %1$d, Ausführung Deadlock Interval = %2$5.2f\n
33 Transaktion (Index %1$2d, %2$s, %3$s@%4$s|%5$d)\n
34 Isolation %1$s\n
35 Zustand %1$s\n
36 Timeout_period %1$s\n
37 Einseitig abgebrochene Transaktion mit Indizes:\n
38 %1$d ,
39 Timed_out Transaktionen mit Indizes:\n
40 %1$d ,
41 Deadlock-Lösung durch Auswählen einer Transaktion und einer Funktion: %1$d %2$s
42 , %1$d %2$s
43 Objekttyp: Index Schlüssel der Klasse (%1$2d|%2$6d|%3$4d) = %4$s.\n
44 Indexname: %1$s\n

$set 15 MSGCAT_SET_IO
1 \n*************************************************************************\n
2 Sicherungsdatei = %1$s wird gebraucht. \n Sie muss geladen werden, um fortzufahren.\n 1 eingeben, um nach dem Laden der Backupdatei fortzufahren  to continue.\n 0 eingeben um abzubrechen.\n
3 \n\n*** SICHERUNG HEADER INFORMATION ***\n\n
4 Sicherung magische Dateiidentifizierung %1$s,\n
5          Version: %1$s\n\
     Disk Version: %2$g\n
6    Datenbankname: %1$s\n\
 DB Erstellungszeit: %2$s\
         Seitengröße: %3$d\n
7     Sicherungsebene: %1$d (%2$s)\n\
        Start_lsa: %3$lld|%4$d\n\
         Last_lsa: %5$lld|%6$d\n
8      Sicherung Zeit: %1$s\
  SicherungsUnit Num: %2$d\n
9  Datenbank Datenträgername: %1$s\n\
   Datenträger-Identifikator: %2$d, Größe: %3$lld Bytes (%4$d Seiten)\n
10   Datenbankname: %1$s\n\
     Datenträgername: %2$s\n\
        Unit Num: %3$d\n\
    Sicherungsebene: %4$d (%5$s)\n\
Eine der folgenden Optionen wählen:\n
11 Geben Sie \n \
  -  %1$d ein, um abzubrechen.\n \
  -  %2$d ein, um nach dem Laden der Datenträger fortzufahren. (wiederholen) \n \
  -  %3$d ein, um nach der Änderung des Datenträgerordner oder -unit fortzufahren.\n
12 Geben Sie das neue Verzeichnis oder Unit für den Sicherungsdatenträger.\n
13 Eingangsfehler, ein Wert zwischen %1$d und %2$d wählen.\n
14 ** WARNUNG ** Ungültiger Sicherungsdatenträger geladen.\n
15 Sicherungsdatenträger %1$s hat eine andere Sicherungsebene \n   (Ebene: %2$d, %3$d erwartet).\n\n
16 Sicherungsdatenträger %1$s hat eine ungültige Seite oder magischen Identifikator.\n\n
17 Sicherungsdatenträger %1$s gehört einer anderen Datenbank \n  (Name: %2$s, %3$s erwartet).\n\n
18 Sicherungsdatenträger %1$s außer Reihe\n  (Unit Num: %2$d, %3$d erwartet).\n\n
19 Sicherungsdatenträger %1$s Zeitversatz\n  (Zeit: %2$s, %3$s erwartet).\n\n
20 Sicherungsdatenträger Dateifehler, Zeile %1$d.\n
21 Sicherungsdatenträger Label: Ebene: %1$d, Unit: %2$d, Datenbank %3$s, Sicherungszeit: %4$s\n
22     Voriges Sicherungsebene: %1$d Zeit: %2$s (start_lsa war %3$lld|%4$d)\n
23 Der folgende Datenbank-Sicherungsdatenträger wird gebracht, um wiederherstellen zu können:\n\n
24 Sicherungsziel voll, ein neues Ziel wird gebraucht, um fortzufahren:\n\n
25 Voriger Sicherungsdatenträger Name: %1$s\n
26     Nächster Sicherungsdatenträger Name: %1$s\n
27  Sicherung Seitengröße: %1$d\n
28       Zip Methode: %1$d (%2$s)\n\
        Zip Ebene: %3$d (%4$s)\n
29 Aktiver Log einfügen: %1$s\n

$set 16 MSGCAT_SET_LOG
1 \n*************************************************************************\n
2 Protokollarchiv %1$s \n wird gebraucht, um die normale Ausführung fortzufahren.   \n \
  Geben Sie\n \
  -  0 ein, um abzubrechen.\n \
  -  1 ein, um ohne dem jetzigen Archiv fortzufahren. (Teilwiederherstellung)\n \
  -  2 ein, um nach dem Laden des Archivs fortzufahren.       \n \
  -  3 ein, um nach der Änderung der Archivstelle fortzufahren.   \n
3 Sicherungsdate %1$s \n wird gebraucht, um die Wiederherstellung fortzufahren. \n \
  Geben Sie\n \
  -  0, ein um abzubrechen.\n \
  -  1, ein um nach dem Laden der Datei fortzufahren.       \n \
  -  2, ein um nach der Änderung der Dateistelle/-name fortzufahren.   \n
4 Geben Sie eine neue Stelle /einen neuen Namen ein.\n
5 ANMERKUNG: %1$s für Datenbank %2$s\n
6 ANMERKUNG: Protokollarchiv %1$s wird nicht mehr gebraucht, wenn die Datenbankmedien nicht abstürzen.\n
7 ANMERKUNG: Protokollarchive von %1$s bis %2$s werden nicht mehr gebraucht, wenn die Datenbankmedien nicht abstürzen.\n
8 ANMERKUNG: von umbenannter Datenbank = %1$s\n
9 ARCHIV: %1$d %2$s %3$lld %4$lld\n
10 ARCHIV:
11 LÖSCHEN: %1$d %2$s bis \n \
       %3$d %4$s.\n \
       GRUND: %5$s\n
12 AKTIVE: %1$s %2$d Seiten\n
13  tran_Index %1$d mit tranid = %2$d festgeschrieben,\n genau vor LSA= %3$lld|%4$d und am %5$s
14  tran_Index %1$d mit tranid = %2$d abgebrochen,\n genau for LSA= %3$lld|%4$d und am %5$s
15  Unvollständige Mediawiederherstellung wird durchgeführt. Die Datenbank wird \n \
 bis zur LSA Adresse von %1$lld|%2$d wiederherstellt. Letzte Festschreibung-/letzter \n \
 Abbruchszeit ist ungefähr %3$s \n \
 Die Datenbank hat alle Informationen verloren \n \
 die nach dieser Zeit festgeschrieben worden sind.\n
16 Protokollzurücksetzung wegen unvollständiger Wiederherstellung.
17 Datenbanksicherung wurde genommen.
18 Medien-Absturz ist nicht wichtig für die Anwendung.
19 Ordner %1$s, der eine frühere Sicherung zu enthalten scheint, wird gesichert.\n\
Veraltete Sicherungsdatenträgers diese Datenbank werden aus dieser Stelle gelöscht,\n\
einschließlich derjenigen aus aktueller Backupebene und höher.\n
20 'y' um fortzufahren oder eine beliebige Taste um abzubrechen eingeben.\n
21 Datenbanksicherung wurde vom Benutzer abgebrochen.\n
22 LÖSCHEN VERSCHOBEN: Archivs %1$d %2$s bis \n \
        %3$d %4$s \n \
        werden nicht mehr gebraucht, wenn eine Wiederherstellung aus dieser Sicherung stattfindet.\n
23 Eine neuere Sicherung hat Archive freigegeben, die vorher DELETE POSTPONED markiert worden sind.\n
24 KOMMENTAR: Protokollarchiv %1$s ist nicht mehr DELETE POSTPONED wegen einer neueren\n \
   Sicherung und wird deshalb nicht mehr gebraucht, wenn kein Medien-Absturz stattfindet.\n
25 ANMERKUNG: Protokollarchivs von %1$s bis %2$s sind nicht mehr DELETE POSTPONED wegen\n \
   einer neueren Sicherung und werden deshalb nicht mehr gebraucht, \n \
   wenn kein Medien-Absturz stattfindet.\n
26 Ein Fehler ist während der Datenbankwiederherstellung eingetreten: bei der Bearbeitung des Sicherungsdatenträgers:\n\
           Backup Volume: %1$s\n\
                Unit Num: %2$d\n\
    Datenbankdatenträger: %3$s\n\
   Datenbank Datentr.-Id: %4$d\n\
** WARNUNG: Die Datenbank nicht anwenden, bis eine fehlerfreie Wiederherstellung stattfindet.\n
27 Eingangsfehler, einen Wert zwischen %1$d und %2$d wählen.\n
28 Die angegebene Zeit (%1$s) muss später als (%2$s), die Zeit der angegebenen Sicherung.\n
29 KOMMENTAR: Protokollarchiv %1$s, das Seiten vor %2$lld enthält, wird von HA Utilities nicht mehr benötigt.\n
30 Anzahl der aktiven Log-Archiven hat die maximal gewünschte Anzahl überschritten.
