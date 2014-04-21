
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
1 \n%s\n\n
2 Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
3 Copyright Information\n
5 Numero intero non valido `%s'
6 Numero decimale non valido (floating) `%s'
7 Mancanza argomento`%s'
8 Argomento duplicato `%s'
9 Valore mancante per argomento `%s'
10 Argomento sconosciuto `%s'
11 Argoemnto in posizione non aspettata `%s'
12 Nessuna definizione di argomento 
13 Non posso riservare spazio per interpretare l'argomento
14 Argomento supera la massiam dimensione : %s
15 Numero di messaggio invalido %d nel catalogo per interpretare l'argomento
16 [opzioni]
17 Opzioni:

$set 4 MSGCAT_SET_PARAMETER
2 %s non è una directory.  Creo %s in ".".
3 Non posso aprire il file di configuraziuone %s. .
4 Non posso scrivere la linea %d nel file di configurazione  %s.
6 Non posso accedere al file di configurazione del sistema  "%s": %s
7 HOME non impostata
8 Parametro richiesto  "%s" senza valore.
9 Non posso aprire il file di configurazione "%s": %s
10 Linea %d nel file di startup %s:
11 Variabile d'ambiente %s:
12 Comando sconosciuto.
13 Tipo valore non congruente con il tipo di parametro.
14 Non posso allocare memoria per salvare il valore.
15 Valore stringa non terminato.
16 Valore fuori dei limiti.
17 Errore UNIX: %s
18 Nessun messaggio disponibile
19 Valore fuori dei limiti. Riporto al valore di default.
20 Comando = '%s'    Valore = %d    Valore di Default = %d\n
21 Coamndo = '%s'    Valore = %f    Valore di Default = %f\n

$set 5 MSGCAT_SET_ERROR
1 Messaggio mancante per codice di errore %1$d.
2 Errore interno del sistema : nessuna altra informazione disponibile.
3 Memoria virtuale esaurita:  impossibile allocare %1$ld bytes di memoria.
4 E' stato fermato.
5 Chiave = "%1$s" non trovata in hash table "%2$s".
6 A NULL hash table in memoria è stata usata come hash table function.
7 Provo a formattare il disk volume "%1$s" con un valore non corretto %2$d per numero di pagine.
8 Problemi a formattare il disk volume "%1$s" con %2$d pagine (%3$ld bytes).
9 Problemi a formattare il disk volume "%1$s" con %2$d pagine (%3$ld Kbytes) per spazio insufficiente.Spazio disponibile %4$d pagine (%5$ld Kbytes).
10 Problemi a montare il disk volume "%1$s".
11 Problemi a montare il disk volume  "%1$s". Il database "%2$s", che utilizza il disk volume, è utilizzato dall'utente %3$s con il processo %4$d sull host %5$s da %6$s.
12 Problemi a smontare il disk volume "%1$s".
13 Rilevato un I/O error durante la lettura della pagine %1$d del volume "%2$s".
14 Rilevato un I/O error durante la scrittura della pagine %1$d del volume "%2$s".
15 Impossibile scrivere la pagina %1$d del volume "%2$s" per spazio insufficiente sul disco.
16 Impossibile rinominare il disco "%1$s" a "%2$s".
17 Errore interno: deallocazione della fetching pageid %1$d del volume "%2$s".
18 Tutti i page buffers are fissi.
19 Errore interno: pageptr = %1$p della pagina %2$d del volume "%3$s" non è fissata.
20 Errore interno: pageptr sconosciuto = %1$p.
21 Errore interno: settore %1$d sconosciuto del volume "%2$s".
22 Errore interno: pagian %1$d sconosciuto del volume "%2$s".
23 Errore interno: provando a pulire la disk system page %1$d del volume "%2$s".
24 Spazio del volume quasi esaurito "%1$s". Numero totali di pagine = %2$d, numero totale di pagine libere = %3$d.
25 Spazio del volume quasi esaurito nello storage principale. Numero totali di pagine = %1$d, numero totale di pagine libere = %2$d.
26 Spazio del volume quasi esaurito per indici. Numero totali di pagine = %1$d, numero totale di pagine libere = %2$d.
27 Spazio del volume quasi esaurito nello storage principale. Numero totali di pagine = %1$d, numero totale di pagine libere = %2$d.
28 Spazio del volume quasi esaurito nello spazio temporaneo. Numero totali di pagine = %1$d, numero totale di pagine libere = %2$d.
29 Ultimo avviso di spazio quasi esaurito sul volume "%1$s". Numero totali di pagine = %2$d, numero totale di pagine libere = %3$d.
30 Ultimo avviso di spazio quasi esaurito sul volumes nello storage principale. Numero totali di pagine %1$d, numero totale di pagine libere = %2$d.
31 Ultimo avviso di spazio quasi esaurito sul volumes per gli indici. Numero totali di pagine %1$d, numero totale di pagine libere = %2$d.
32 Ultimo avviso di spazio quasi esaurito sul volumes per spazio generico. Numero totali di pagine %1$d, numero totale di pagine libere = %2$d.
33 Ultimo avviso di spazio quasi esaurito sul volumes per lo spazio temporaneo. Numero totali di pagine = %1$d, numero totale di pagine libere = %2$d.
34 Errore interno: %1$d nthpagina è fuori valore nel file %2$d nel volume "%3$s". Numero di pagine totali al momento %4$d.
35 Errore interno: Identificatore del volume sconosciuto %1$d.
36 Spazio disco esaurito. Numero di pagine richieste = %1$d.
37 Spazio disco esaurito nel volume "%1$s". Numero di pagine richieste = %2$d.
38 Errore interno: File sconosciuto VFID %1$d|%2$d.
39 Errore interno: Pagina %1$d|%2$d(volume "%3$s") del file VFID %4$d|%5$d(volume "%6$s") è invalido in base allocation map.
40 Errore interno: %1$d pagine aspettate, %2$d pagine trovate per il file VFID %3$d|%4$d(volume "%5$s").
41 Errore interno: Pagina %1$d|%2$d(volume "%3$s") non è parte del file VFID %4$d|%5$d(volume "%6$s").
42 Non posso riordianre i records nella pagina %1$d del volume "%2$s".
43 Errore interno: numero non corretto (%1$d) di slots passati per riordinare i records nella pagina %2$d del volume "%3$s". Adesso ci sono %4$d slots.
44 Spazio esaurito nella pagina slotted  %1$d del volume "%2$s".
45 Slot %1$d nella pagina %2$d del volume "%3$s" è usata da un altro record. Un nuovo record non può essere inserito.
46 Errore interno: slot %1$d nella pagina %2$d del volume "%3$s" non è allocata.
47 Impossibile creare heap file nel volume "%1$s".
48 Accesso all'oggeto cancellato %1$d|%2$d|%3$d.
49 Errore interno: la classee dell'oggetto %1$d|%2$d|%3$d è sconosciuto.
50 Errore interno: reallocazione dell'oggetto %1$d|%2$d|%3$d potrebbe essere rovinato.
51 Errore interno: oggetto %1$d|%2$d|%3$d potrebbe essere rovinato.
52 Errore interno: oggetto eccede address %1$d|%2$d|%3$d potrebbe essere rovinato.
53 Prendo oggetto %1$d|%2$d|%3$d solo quando il suo OID è stato asseganto.
54 Errore interno: provo a elaborae un oggetto lungo %1$d pagina.  La massima dimensione è %2$d pagine.
55 Errore interno: Un errore circolare è stato trovato nella pagina %1$d|%2$d del fiel heap  %3$d|%4$d|%5$d.
56 Errore interno: extendible hashing file sconosciuto (Volid: %1$d Fileid: %2$d) pagina (Volid: %3$d Pageid: %4$d) era specificata.
57 La chiave non esiste nella struttura del extendible hashing.
58 La stringa "%1$s" è troppo luna per stare nella pagina.
59 Errore interno: la chiave specificata %1$d non è valida per la struttura del extendible hashing.
60 Errore interno: struttura corrotta del extendible hashing.
61 Errore interno: struttura corrotta della pagina directory root del extendible hashing (Volid: %1$d Fileid: %2$d Pageid: %3$d).
62 Errore interno: sorting record %1$d (grandezza %2$d) non sta nella pagina;massima dimensione possibile %3$d.
63 Errore interno: pagina temporanea corrotta durante un sort
64 classee sconosciuta "%1$s".
65 La classe "%1$s" esiste già.
66 Errore interno: Operazione sconosciuta %1$d per oggetto %2$d|%3$d|%4$d.
67 Errore interno: un file heap non è stato liberato durante il salvataggio dell'oggetto  %1$d|%2$d|%3$d.
68 Errore interno: classenames differenti per classee co oid = %1$d|%2$d|%3$d rilevati. Trovate classename con  "%4$s", "%5$s" che usano classename hash table and heap, rispettivamente.
69 Errore interno: identificatori di classei differenti trovati per classee con nome "%1$s". Trovato OIDS : %2$d|%3$d|%4$d, %5$d|%6$d|%7$d che usano classename hash table and heap, rispettivamente.
70 Errore interno: classee con nome "%1$s" e oid = %2$d|%3$d|%4$d non esistono nella classename hash table.
71 Errore interno: classee con nome "%1$s" e oid = %2$d|%3$d|%4$d non esistono nella sua heap.
72 La tua transazione (index %1$d, %2$s@%3$s|%4$d) è stata fermata dal sistema.
73 La tua transazione (index %1$d, %2$s@%3$s|%4$d) è scaduta aspettando %5$s lock su oggetto %6$d|%7$d|%8$d. Stai aspettando l'utente %9$s che finisca.
74 La tua transazione (index %1$d, %2$s@%3$s|%4$d) è scaduta aspettando %5$s lock sulla classee %6$s. Stai aspettando l'utente %9$s che finisca.
75 La tua transazione (index %1$d, %2$s@%3$s|%4$d) è scaduta aspettando %5$s lock sull istanza %6$d|%7$d|%8$d della classee %9$s. Stai aspettando l'utente %10$s che finisca.
76 La tua transazione (index %1$d, %2$s@%3$s|%4$d) è scaduta aspettando %5$s nella pagina %6$d|%7$d. Stai aspettando l'utente %8$s che rilasci la page lock.
77 Errore interno: tutti i buffers sono fissi.
78 Errore interno: un I/O errore rilevato durante la lettura della logical log page %1$d (physical page %2$d) di "%3$s".
79 Errore interno: un I/O errore rilevato durante la scrittura della logical log page %1$d (physical page %2$d) di "%3$s".
80 Spazio insufficiente nel device durante la scrittura della logical log page %1$d (physical page %2$d) di "%3$s". Could not write more than %4$d bytes.
81 Errore interno: logical log page %1$d potrebbe essere rovinata.
82 Mmount non possibile del log disk volume/file "%1$s".
83 Lunghezza path "%1$s" più lunghezza prefisso logname "%2$s" è troppo lunga ; la lunghezza totale deve essere meno di %3$d.
84 Lunghezza del prefisso logname "%1$s" è troppo lunga; la lunghezza totale deve essere meno di %2$d.
85 Il nome del prefisso "%1$s" non è lo stesso del "%2$s" sul log disk.  Il log potrebbe essere stato rinominato esternamente.
86 Database incompatibiel con la versione corrente "%1$s" release "%2$s".
87 Ci sono recovery actions che devono essere eseguite con la versione  "%1$s" release "%2$s" invece della release "%3$s". Dopo il recovery, il database potrà usare la release "%4$s".
88 Stringa di release "%1$s" è più lunga di %2$d. Errore interno .. bisogna cambiare header log.
89 Log "%1$s" non è coerente con questo database.
90 Redo logging is always a page level logging operation.  A data page pointer must be given as part of the address.
91 Postpone logging is always a page level logging operation.  A data page pointer must be given as part of the address.
92 Compensate logging is always a page level logging operation.  A data page pointer must be given as part of the address.
93 Attenzione: redo logging non considearto durante le operazioni di redo recovery e rollbacks.
94 Attenzione: postpone logging non considearto durante le operazioni di redo recovery e rollbacks.
95 Attenzione: undo logging non considearto durante le operazioni di redo recovery e rollbacks.
96 Media recovery potrebbe essere necessario sul volume  "%1$s".
97 Errore interno: pagina di log non trovata  %1$d nel log archives.
98 Impossibile creare l'archive log "%1$s" per archiviare le pagine da %2$d a %3$d.
99 Impossibile creare backup directory information file "%1$s".
100 Impossibile fare il backup del database "%1$s".
101 File sconosciuto "%1$s".
102 File "%1$s" non ha abbastanza entries:  %2$d entries aspettate.
103 File "%1$s" sembra avere entries non ordinate. Entry %2$d con valori: %3$d %4$s %5$s, trovate mentre entry con valore %6$d %7$s erano aspettate.
104 File "%1$s" seems to have an incorrect entry for database name (main volume). Entry %2$d with values: %3$d %4$s %5$s, was found when entry with values %6$d %7$s %8$s was expected.
105 Cannot access backup file "%1$s". Restore or Backup is cancelled.
106 Cannot prepare to commit the current transaction with global transaction identifier %1$d because that transaction identifier is in use by another transaction.
107 There is no global distributed transaction associated with transaction identifier %1$d.
108 Current transaction %1$d must be committed or aborted before attempting to join to the distributed transaction with global identifier %2$d.
109 La tua transazione %1$d has been unilaterally aborted by the system because a participant (remote) site went down or unilaterally aborted the transaction.
110 Isolation level value must be between %1$d and %2$d.
111 La tua transazione has been aborted by the system due to server failure or mode change.
112 Host "%1$s" is not authorized to run database.
113 Impossibile far partire il database server.
114 Valore non interpretabile "%1$s" come database.  Il volume del database potrebbe essere stato copiato/mosso da operazioni esterne.
115 Database "%1$s" già esistente.
116 Database "%1$s" sconosciuto, o il file "databases.txt" non può essere trovato.
117 Pathname assoluta per il database è troppo lunga.  La lunghezza combinata del path "%1$s" più il nome "%2$s" è %3$d; la lunghezza combinata deve essere meno di %4$d.
118 Impossibile trovare la directory di lavoro.
119 Impossibile trovare hostname.
120 Il numero massimod dei volumi (%1$d) è stato superato.
121 Provo a togliere "%1$s" come volume permanente dal database.
122 Impossibile accedere al catalogo system message.
123 Impossibile creare "%1$s" per il database "%2$s".
124 Volume "%1$s" già esistente.
125 Un estensione del volume automatica "%1$s" di %2$d pagine è stata creata.
126 Funzione sconosciuta "%1$s" trovata alla linea %2$d.
127 Valore non corretto %1$d per il numero di pagine dato alla linea %2$d.
128 Numero di pafine non dato alla liena  %1$d.
129 Token sconosciuto "%1$s" trovato alla linea %2$d.
130 Virtual memory esaurita.
131 Database Errore di sistema.
132 Funzionalità non implementata nella release corrente : %1$s.
133 set constant concatenate non permessa.
134 Non è permesso usare classe Attributos e non-classe Attributos insieme in un singolo update.
135 Comando non valido per colonne oid : %1$s.
136 Funzione non implementata "%1$s".
137 Oggetto authorization rovinato.
138 Authorization classe "%1$s" non trovata.
139 Errore di accesso su authorization Attributo "%2$s" della classee "%1$s".
140 Operazione "%1$s" può essere eseguita solo da DBA o utenti dei gruppi DBA.
141 Non posso aggiungere "%1$s" come membro di "%2$s".
142 Aggiungere un membro alla gerarchia degli utenti un riferimento circolare.
143 Trovata una classee senza owner.
144 Impossibile accedere a user object.
145 Non è possibile eseguire comandi grant/revoke su te stesso.
146 Non è possibile eseguire comandi GRANT/REVOKE sull owner della classee.
147 Nessuna opzione di GRANT.
148 Non ottengo write lock sull'oggetto authorization.
149 Non posso creare un istanza dell'oggetto authorization.
150 Non posso rimuovere privilegi su te stesso.
151 Non posso rimuovere privilegi sull owner della classee.
152 GRANT non trovato.
153 Nessun privilegio di authorization per il database.
154 Installazione non completa per authorization  - Nessun privilegio di authorization per il database.
155 Authorization roots multiple.
156 Authorization failure.
157 Errore nell'autorizzazione per SELECT.
158 Errore nell'autorizzazione per ALTER.
159 Errore nell'autorizzazione per UPDATE.
160 Errore nell'autorizzazione per INSERT.
161 Errore nell'autorizzazione per DELETE.
162 Errore nell'autorizzazione per INDEX.
163 Errore nell'autorizzazione per EXECUTE.
164 Utente "%1$s" già esistente.
165 Utente "%1$s" non valido.
166 Utente specificato non valido.
167 DBA, membri del gruppo DBA e classe owner possono eseguire l'operazione.
168 Membro non trovato.
169 Non posso rimuovere l'utente %s dal database.
170 Nessun utente loggato.
171 Password incorretta o mancante.
172 La lunghezza massima della password è 31 caratteri.
173 Non posso trovate il database file "%1$s".
174 Non posso avere accesso in scrittura al database file "%1$s".
175 Database file rovinato "%1$s".
176 Errore di conversione nel formato della data.
177 Non posso creare large object dallo storage.
178 OID temporaneo trovato encountered senza MOP.
179 Dominio "%1$s" non è compatibile con il dominio "%2$s".
180 Dominio "%1$s" passato non è compatibile con il dominio destinazione "%2$s".
181 Cannot coerce value of domain "%1$s" al dominio "%2$s".
182 Cannot coerce value of domain "%1$s" al dominio "%2$s" senza overflow.
183 Unexpected amount of received data; %1$d aspettati, %2$d ricevuti.
184 Non posso allocare il buffer di communicazione.
185 Errori ricevendo dati dal client.
186 Errori ricevendo dati dal server.
187 Buffer di communicazione non usato.
188 Database sconosciuto "%1$s".
189 Host name non valido "%1$s".
190 Server host non identificato.
191 Non posso connettermi al server "%1$s" sulla "%2$s".
192 Non posso caricare il file di configurazione della rete.
193 Il Server ha ricevuto il comando di shutdown del client.
194 Richiesta al server sconosciuta id %1$d.
195 Errore nella comunicazione del Server : %1$s.
196 Nome Server non identificato.
197 Non posso contattare il server master.
198 Receive buffer troppo piccolo, dati rovinati.
199 Server non risponde più.
200 Azione di rimozione non corretta.
201 Azione di rimozione non corretta da UNIQUE table entry.
202 Attributo "%1$s" non trovato.
203 Valore per attributo "%1$s" deve essere del tipo "%2$s", non del tipo "%3$s".
204 Chiamata alla funzione con parametri mancanti o errati.
205 Attributo "%1$s" non può esere NULL.
206 Attributo "%1$s" non può avere UNIQUE constraint.
207 classee "%1$s" è di grandezza 0.
208 Metodo "%1$s" non trovato.
209 Nessun Attributo o metodo con nome "%1$s" trovato.
210 Errore interno: processando un object template.
211 Attributo è stato cancellato costruendo l'object template.
212 Attributo "%1$s" viola UNIQUE constraint.
213 Trovata un entry nella UNIQUE table entry non valida.
214 Un conflitto di dominio esiste nell'attributo "%1$s".
215 Attributo con id %1$d non trovato.
216 Invalid object ownership tag sul valore.
217 Questa operazione può essere eseguita solo su una classee.
218 Object non valido in db_get path expression.
219 Db_get non valido in path expression.
220 Set non valido in db_get path expression.
221 Set index non valido in db_get path expression.
222 Attempt to assign string value maggiore di %2$d caratteri all'attributo "%1$s".
223 Valore %2$d è troppo grande per un attributo smallint "%1$s".
224 Il database non è ripartito.
225 Valore mancate per l'Attributo "%1$s" con NOT NULL constraint.
226 Lista componente vuota nella classee.
227 Attempt to assign value to Attributo "%1$s" più di una volta.
228 Metodo "%1$s" chiamato con %2$d argomenti.  Il sistema permette al massimo %3$d argomenti.
229 Metodo "%1$s" richiede %2$d argomenti con il dominio "%3$s".  Valore non valido per il dominio "%4$s" passato.
230 Attributo "%1$s" non può essere assegnato a una stringa di lunghezza %2$d .  La massima lunghezza per la stringa è %3$d.
231 Invalid API operation attempted on a temporary object.
232 Non è permesso usare un object templates per transazioni multiple.
233 Non posso creare una classee con basic type name "%1$s".
234 metodo file "%1$s" non trovato.
235 %1$d metodo non risolto per funzione dopo linking.
236 Metodo "%1$s" non risolto.
237 Gravi problemi durante il dynamic linking.
238 Non posso accedere al metodo file "%1$s".
239 Attributo "%1$s" non trovato.
240 Metodo "%1$s" non trovato.
241 Nessu Attributo o metodo con nome "%1$s".
242 Signature per %2$s non trovato per il metodo "%1$s".
243 Argomento %2$d del metodo "%1$s" non definito.
244 Nome Dominio "%1$s" non è il nome della classee o basic type.
245 Il nome "%1$s" è già stato usato per il nome di un Attributo.
246 Il nome "%1$s" è già stato usato per il nome di un metodo.
247 Argomenti non validi per schema manager internal function.
248 L'Attributo del dominio "%1$s" non è disponibile per un UNIQUE integrity constraint.
249 L'operazione sulla classee "%1$s" puù essere eseguito prima che l'istanza sia creata.
250 Non posso craere index sull'Attributo "%1$s", definito come uno shared value.
251 Signature %2$s già esistente per il metodo "%1$s".
252 Attributo "%1$s" deve avere un set domain per questa operazione.
253 Attributo "%1$s" non può essere definito con un nested set domain.
254 Impossibile rimuovere domain "%2$s" per l'Attributo "%1$s".
255 Attributo "%1$s" non è una variable sized domain.
256 Superclasse già esistente.
257 Superclasse addition genera un riferimento circolare nella gerarchia della classee.
258 Superclasse non trovata.
259 Signatures multiple presenti per il metodo "%1$s".
260 Argumento %2$d del metodo "%1$s" non è un set domain.
261 Risoluzione per "%1$s" non trovata.
262 Value domain "%2$s" not suitable per Attributo "%1$s".
263 Corruzione dello Schema manager internal.
264 Alias "%2$s" per Attributo/metodo "%1$s" non è unico.
265 Shadowing per Attributo "%1$s" ha conflitti con il domain "%2$s".
266 Name conflict per Attributo "%1$s" con le classei "%2$s" e "%3$s".
267 Shadowing metodo "%1$s" con conflicting signature.
268 Name conflict sul metodo "%1$s" con classei "%2$s" e "%3$s".
269 Non posso definire index sul domain "%1$s".
270 Non posso definire UNIQUE constraint sul domain "%1$s".
271 Trovato riferiemnto circolare nella gerarchia della classee ; "%1$s" è una super classe di "%2$s".
272 Index "%2$s" già definito per classee "%1$s".
273 Index "%1$s" non esiste.
274 Caratteri non permessi nel nome: "%1$s".
275 Operazione non permessa sull Attributo "%1$s", ereditato dalla classee "%2$s".
276 Operazione non permessa sul metodo "%1$s", ereditato dalla classee "%2$s".
277 Operazione non permessa su "%1$s", ereditato dalla classee "%2$s".
278 Nella classee "%4$s" ha domini incompatibili per "%1$s", ereditato dalle classei "%2$s" e "%3$s".
279 Nella classee "%4$s", "%1$s" ereditato dalla classee "%2$s" non può essere usata come richiesto. La definizione della classee "%3$s" è più specifica.
280 Nella classee "%3$s", "%1$s" non può essere ereditata dalla classee "%2$s" perchè nella definizione locale c'è un dominio incompatibile.
281 Nella classee "%3$s", "%1$s" ereditato dalla classee "%2$s" è simile ma non sostituisce l'altra trovata.
282 Nella classee "%4$s", "%1$s" ereditato dalla classee "%3$s" non può sostituire la classee "%2$s". Domini incompatibili.
283 Nella classee "%4$s", "%1$s" ereditato dalla classee "%3$s" non può sostituire la classee "%2$s". Ha un dominio meno specifico.
284 Non puoi chiedere ereditarietà di "%1$s" perchè è definito localmente nella classee.
285 Non puoi usare "%1$s" come alias per il componente ereditato "%2$s" perchè c'è una definizione locale con quel nome.
286 Non puoi usare  "%1$s" come alias per "%2$s" di "%3$s".  Un componente con quel nome sta già ereditando da "%4$s".
287 Nella classee "%1$s", non può definire Attributo "%2$s" perchè dovrebbe shadow un metodo ereditando da "%3$s".
288 Nella classee "%1$s", non può definire metodo "%2$s" perchè dovrebbe shadow un Attributo ereditando da "%3$s".
289 Subclasse "%1$s" non può ereditare il metodo "%2$s" dalla classee "%3$s" perchè c'è già un attributo con quel nome.
290 Subclasse "%1$s" non può ereditare l'Attributo "%2$s" dalla classee "%3$s" perchè c'è già un metodo con quel nome.
291 "%1$s" è definito nella classee "%2$s" come un Attributo e nella classee "%3$s" come un metodo.
292 Nessuan specifica per index "%1$s".
293 Operazione non permessa per questo tipo di classee.
294 Variabile di ambiente non definita  "%1$s" riferita nel metodo del file.
295 Non puoi modificaer la classee "%1$s".  Spazio esaurito nel system catalog .  Usa la compactdb utility per liberarne.
296 Trovata Proprietà non valida.
297 Noem Alias "%1$s" usato piu di una volta.
298 Nella classee "%1$s", un resolution per "%2$s" di "%3$s"' non è permesso.  Questa classee non è una super classe.
299 "%1$s" è una parola riservata e non può essere usata come Attributo, metodo, o nome classee.
300 Attributo "%1$s" non può essere definito con DEFAULT e UNIQUE constraints insieme.
301 Attributo "%1$s" non può essere definito con constraint di lunghezza di %2$d.  La massima lunghezza è %3$d.
302 Non puoi aggiungere un elemento alla lista.
303 Valore già presente nelal lista nella lista.
304 Settagio della lista %1$d è out of bounds.
305 Settagio della Sequence index %1$d è out of bounds.
306 L'operazione richiesta è possibiel solo sulle sequences.
307 L'operazione richiesta è possibiel solo sulel liste.
308 Valore dell'elemento non è nel domain della lista.
309 Settaggio non possibile per : %1$d.
310 Elemento non trovato in lista.
311 Elemento non trovato nella sequence.
312 "%1$s" non è un settaggio valido per la lista.
313 Object buffer underflow mentre leggevo.
314 Object buffer overflow mentre scrivevo.
315 Definizione illegale di Metaclasse.
316 Problemi di conversione di calcolo , mi aspetto %1$d calcolato %2$d.
317 Problemi su disco per la classee "%1$s".
318 Problemi di sinch caricando un oggetto. Database è rovinato o non aggiornato.
319 Estensione dell Attributo extension ignorato.
320 Problemi su disco per un object.
321 Problemi di corruzione per il workspace.
322 MOP non trovato per il workspace.
323 Prova di upgrade non-temporary MOP per il workspace.
324 Oggetto trovato nel workspace senza classee.
325 Prova di garbage collect un MOP nella dirty list.
326 Cambio di classe pointer per oggetto in workspace.
327 Non posso creare MOP con NULL OID.
328 Trovata Istanza nel workspace senza classee.
329 Lista di oggetti link non permessa nel workspace.
330 Workspace pin violation.
331 Non posso allocare workspace block per OS, memoria virtuale esaurita. Transaction fermata.
332 Grandezza originale Workspace in eccesso.  Una automatic extension è stata fatta.
333 Memoria esaurita mentre alloco spazio per workspace. Transaction fermata.
334 Sto provando ad allocare %1$d bytes in piu del maximum workspace block size. Prova a cambiare il parametro max_block_size .  Transaction fermata.
335 Ignoring attempt to free object not allocated within the workspace.
336 Ignoring attempt to free an object more than once.
337 Ignoring attempt to allocate object with negative size.
338 Map entry identifier overrun.
339 Cannot exit critical section, since it is not owned by transaction %1$d.
340 Non posso forkare child process.
341 Non posso eseguire child process.
342 Non posso cambiare process group.
343 _id richiesto è già in uso.
344 %1$s.
345 Errore leggendo il messaggio di shutdown dal master.
346 Errore leggendo il messaggio di stop shutdown dal master.
347 Non posso stabilire una connessione pipe al server master.
348 Bisogna specificare sia il servizio che la porta.
349 Servizio sconosciuto per TCP: %1$s.
350 Errore prendendo il local host per nome per host "%1$s". Host non trovato.
351 Non posso creare un TCP socket.
352 tcp_open: non posso prendere una porta TCP.
353 Non posso creare una connessione al master server.
354 Non posso settare un nuovo processo per il socket.
355 Non posso creare un socket stream.
356 '%1$s' file per UNIX domain socket già esistente.
357 Non posso ascoltare sul local address... esco.
358 Accept failed.
359 Datagram bind error.
360 Datagram accept error.
361 Datagram connect error.
362 Datagram socket error.
363 Recvmsg dal master socket fallito.
364 Mando una nuova richiesta al server.
365 Mando un broadcast message al client.
366 Errore dal server: %1$s
367 Server %1$s già esistente.
368 Errore di communicazione mentre mi connetto per %1$s.
369 Codice inusato.
370 Codice inusato.
371 Codice inusato.
372 Codice inusato.
373 Premature EOF durante la lettura : %1$d.
374 Premature EOF durante la lettura %1$d o %2$d.
375 Errore di conversione su (0x%1$c%2$c) %3$d e %4$d convertito in %5$d.
376 Nessuna classee pubblica trovata.
377 Errore %1$d: non posso aggiungere Attributo "%2$s" alla gerarchia multimedia .
378 Errore %1$d: non posso aggiungere metodo "%2$s" implementato come "%3$s" alla gerarchia multimedia.
379 Errore %1$d: non posso aggiungere una superclasse alla gerarchia multimedia.
380 Dynamic loader già inizializzato.
381 Dynamic loader non inizializzato.
382 Dynamic loader inizializzato non nel giusto modo.
383 "%1$s" non è un oggetto o un archivio.
384 Non posso determinare absolute path per "%1$s".
385 ld process uscito con codice %1$d.
386 ld process uscito con per signal %1$d.
387 ld process uscito con unknown waitval 0x%1$lx.
388 Non posso accedere alla initial image di "%1$s".
389 Errore di sistema: %1$s.
390 Problema con "%1$s": %2$s.
391 Conflitto con altro SIGPIPE handler.
392 Unable to spawn dynamic loader daemon.
393 Dynamic loader daemon non presente.
394 Range endpoint not between 0 and 256.
395 Specificata Subexpression errata.
396 Octal digit out of range exists in escape.
397 Regular expression non propriamente delimitata.
398 Text search chiamata prima della compilazione dell expression.
399 Trovate parentesi non bilanciate nella regular expression.
400 Parentesi troppo annidate nella regular expression.
401 Piu di 2 numeri specificati nel range.
402 Mancanza di una parentesi graffa nella regular expession.
403 Primo numero del range expression è maggiore del secondo.
404 Mancanza di una parentesi graffa nella regular expession.
405 Regular expression troppo lunga per la compilazione.
406 Identificare invalido B+tree index : (vfid = (%1$d, %2$d), rt_pgid: %3$d).
407 Chiave sconosciuta %1$s riferita a B+tree index {vfid: (%2$d, %3$d), rt_pgid: %4$d, key_type: %5$s}.
408 Valore sconosciuto (oid) %1$d|%2$d|%3$d referenziato.
409 Valore duplicato (oid) %1$d|%2$d|%3$d referenziato.
410 Null key referenziata.
411 B+tree index key type "%1$s" non valida.
412 Range search non valido.
413 Attributo sconosciuto : %1$d.
414 classee sconosciuta: %1$d|%2$d|%3$d.
415 classee non valida: %1$d|%2$d|%3$d.
416 Unknown representation identifier: %1$d.
417 Invalid representation identifier: %1$d.
418 Record descriptor non ha abbastanza spazion : %1$d.
419 Classe ha raggiunto il massimo per representations count: %1$d.
420 Classe %1$d|%2$d|%3$d ha già una representations.
421 Representations Directory mancante nel Catalog per classee: %1$d|%2$d|%3$d.
422 Representation Information Record mancante nel Catalog per classee - Repr_Id: %1$d|%2$d|%3$d - %4$d.
423 Sessione non valida.
424 Nessun statement da eseguire.
425 Tipo dato non compatibile per Attributo "%1$s" della classee "%2$s".
426 Tipo dato non compatibile per Attributo "%1$s".
427 Data overflow sul tipo dato "%1$s".
428 Statement non è modificabile.
429 Comando sconosciuto.
430 Variabile sconosciuta "%1$s".
431 Errore nell'interprete SQL/X.
432 classee sconosciuta "%1$s".
433 Attributo sconosciuto "%1$s" in classee "%2$s".
434 Non posso trovare il nome colonna dalla query.
435 Multiple statements non permessi.
436 Statement non è una query.
437 Long object non valido ID (%1$s,%2$d, %3$s, %4$s).
438 Conflitti nel long object descriptor.
439 Offset %1$d è più largo della lunghezza del long object %2$d.
440 Posizione del cursor non valida.
441 Operazione non valida del cursor.
442 Posizione del cursor sconosciuta.
443 Tuple value index %1$d is out of range.
444 Nome Attributo non valido: "%1$s".
445 Valore nella lista  %1$d fuori valore.
446 Ci sono risultati della query chiusi dal transaction manager ma non chiusi dall'applicazione. 
447 Operazione su una query result structure già chiusa.
448 La grandezza delle Tuple sono limitate alla grandezza della pagina.
449 Identificatore della query sconosciuto : %1$d.
450 Invalid set operator: %1$d.
451 Heap file with no instances.
452 Invalid XASL tree node content.
453 No more acess specification nodes.
454 Tipo dato referenziato non valido.
455 No more query file pages left.
456 I Tipi dato referenziati sono incompatibili.
457 Risultato della query non valido.
458 Overflow occurred in addition context.
459 Risultato della query contiente piu di una tuple. 
460 Troppe poche variabili in input per host.
461 Piu di una variabile host sono ritornate.
462 Il valore di ritorno è NULL, ma non c'è una variabile.
463 Cursor statement non ancora preparato.
464 Cursor dovrebbe essere una SELECT statement.
465 Il cursor non è ancora aperto.
466 Il cursor dovrebbe esser chiuso prima di riaprirlo.
467 Il risultato dovrebbe essere un singolo oggetto.
468 No positional markers sono permessi in  statements EXECUTE IMMEDIATE.
469 Tipo di dato non valido.
470 Query usa troppi valori per optimizer.
471 Memoria esaurita alla linea %1$d del file %2$s.
472 %1$s connector per il database locale '%2$s' su host '%3$s' è morto.
473 Riportato dal db locale '%1$s' su host '%2$s': %3$s.
474 Errore %1$s riportato durante %2$s.
475 No query specificata con index %1$s.
476 LDB %1$s già esistente.
477 Non posso connettermi al master su host %2$s per accedere LDB %1$s.
478 Il database locale "%1$s" non è un LDB registrato.
479 Il database locale "%1$s" è in uso da un altro client, modifiche e cancellazioni non permesse.
480 Il database locale "%1$s" non è vuoto e non può essere cancellato.
481 "%1$s" non è una proxy virtual classe.
482 "%1$s" non è una virtual classe.
483 "%1$s" non è una un Attributo della classee "%2$s".
484 L object_id per la classe proxy virtual  "%1$s" è già settato.
485 Uno o piu object_id Attributos sono NULL per istanza della classe proxy  "%1$s".
486 L'oggetto non è un istanza updatable virtual classe.
487 L'oggetto non è un istanza.
488 Un Attributo della classe potrebbe non essere una classe viirtuale o una classe proxy virtual.
489 Un Attributo della classe proxy virtual potrebbe non essere una classe, classe virtual o object.
490 La superclasse "%1$s" deve essere dello stesso tipo della classe come "%2$s".
491 Non posso estrarre il nome entità della query "%1$s".
492 %1$s
493 Sintassi: %1$s %2$s
494 Semantica: %1$s %2$s
495 Eseguito: %1$s %2$s
496 Key reference sconosciuta.
497 Valore sconosciuto (oid) %1$d|%2$d|%3$d referenziato.
498 Valore Duplicato (oid) %1$d|%2$d|%3$d referenziato.
499 Null key referenziato.
500 Key Duplicata referenziata.
501 Priorità trigger non valida.
502 Missing target classe nel trigger.
503 Trigger "%1$s" non trovato.
504 Errore interno: processing trigger "%1$s".
505 Trigger con nome "%1$s" già esistente.
506 Un trigger non può esser definito su "%1$s" perchè è una classe virtuale.
507 Classe trigger inappropriata "%1$s".
508 Attributo "%1$s" Inappropriate per trigger.
509 Condizione per trigger non valida "%1$s".
510 Azione non valida per trigger "%1$s".
511 Autorizzazione negata per l'accesso al trigger "%1$s".
512 Autorizzazione negata per il delete del trigger "%1$s".
513 Autorizzazione negata per update del trigger "%1$s".
514 Autorizzazione negata per alter del trigger "%1$s".
515 Trigger action execution time of %2$s cannot be earlier than condition execution time of %1$s.
516 Maximum trigger depth %1$d exceeded at trigger "%2$s".
517 Operazione negata dal trigger "%1$s".
518 Errore interno: processing trigger "%1$s".
519 Condizione del trigger Illegale.
520 L'azione REJECT non può essere usata con un azione di AFTER o DEFERRED.
521 L'azione REJECT non può essere usata con un eventi di ABORT o TIMEOUT.
522 Condizione del Trigger non data.
523 Azione del Trigger non data.
524 L'attività specificata non può esser rimossa perchè è di un altro utente.
525 Errore in compilazione per "%1$s" : %2$s
526 Errore compilazione per action per "%1$s", %2$s
527 Errore valutando la condizione per "%1$s" : %2$s
528 Errore valutando l'azione per "%1$s", %2$s
529 La Transaction nnon può essere confermata , è stata invalidata dal trigger "%1$s".
530 Regular expression mancante.
531 Regular expression è troppo lunga o complessa.
532 Parentesi non bilanciate nella regular expression.
533 Reference is out of range in regular expression.
534 Caratteri non permessi nella sintassi della regular expression.
535 Nessun carattere per la ricerca della regular expression.
536 Opcode non legale trovato nella regular expression.
537 Comando di ricerca non valido compilando la regular expression compiler.
538 Il buffer per la Regular expression deve essere inizializzato
539 Attempt to divide by zero.
540 %1$s.
541 Numero di pagine libere per il volume "%1$s" non consistente. %2$d e %3$d were found according to volume header and bitmap, respectively.
542 Numero di secttori liberi per il volume "%1$s" non consistente. %2$d e %3$d were found according to volume header and bitmap, respectively.
543 Disk header per il  volume "%1$s" non consistente.
544 Errore interno: INDEX %1$s ON classe %2$s (classe_OID: %3$d|%4$d|%5$d). B+tree key %6$s entry for object OID: %7$d|%8$d|%9$d was not found on B+tree: %10$d|%11$d|%12$d.
545 Errore interno: INDEX %1$s ON classe %2$s (classe_OID: %3$d|%4$d|%5$d). Key and OID: %6$d|%7$d|%8$d entry on B+tree: %9$d|%10$d|%11$d non corretto. Oggetto inesistente.
546 Errore interno: INDEX %1$s ON classe %2$s (classe_OID: %3$d|%4$d|%5$d). expecting %6$d OID entry values, but %7$d were found on B+tree: %8$d|%9$d|%10$d.
547 Server release %1$s differisce dal client release %2$s.
548 Non c'è la definizione object_id per la classe proxy "%1$s".
549 Istanza proxy non trovata.
550 Nome Savepoint sconosciuto %1$s
551 Impossibile trovare le informaizoni del volume path file "%1$s"... Continnuo a leggere dalle tabelle interne
552 Date arithmetic underflow.
553 Time arithmetic underflow.
$ LOADDB
554 Il loader è in uno stato non valido.  Nessuna altra operazione possibile.
555 Problemi di allocazione memoria caricando il database.
556 Troppi valori passati.  Aspettati %d.
557 Per l'Attributo %1$s di %2$s, type %3$s non è permesso per il dominio
558 Per l'Attributo %1$s di %2$s, expected type %3$s, got a set constant.
559 Per l'Attributo %1$s di %2$s, expected type %3$s, got %4$s.
560 Per l'Attributo %1$s di %2$s, domain is not specific enough to support\n unqualified object references.
561 Liste annidate non permesse.
562 System classe %1$s non può essere popolata con il database loader.
563 Reference to internal classe %1$s converted to NULL.
564 UNIQUE constraint violato per Attributo %1$s di %2$s.
565 Constructor metodo %1$s non è definito nella classe %2$s.
566 Codice inutilizzato.
567 Troppi argomenti passati al metodo construttore. Aspettati %1$d.
568 Parametri mancanti per il costruttore. Aspettati %1$d, trovati %2$d.
569 Valori mancati per Attributo.  Aspettati %1$d, trovati %2$d.
570 Non posso accedere al Glo data file "%1$s".
571 Istanza creata precedentemente.\nQuesto non è permesso per istanze create usando i costruttori.
572 Errore interno: non posso costruire l'istanza.
573 Errore interno: non posso inserire l'istanza nel database.
574 Errore interno: non posso modificare la nuova istanza nel database.
575 Per argomento %1$d del costruttore %2$s, tipo aspettato %3$s, passato %4$s.
576 Referenza alla classe %3$s non è compatibile con domain di %1$s di %2$s.
577 Per argomento %1$d del costruttore %2$s, domain non è abbastanza specifico\n Object references non qualificata.
578 Referenza alla classe %3$s non è compatibile icon il domain dell'argomento %1$d \n del costruttore %2$s.
579 Target domain deve includere il tipo "object" per permettere la reference delle classi.
580 Attributo %1$s non è definito per la classe %2$s.
581 Provato a modificare il database quando le modifiche sono disabilitate.
582 Unknown storage purpose %1$d. Valid range is from %2$d to %3$d.
583 Provato ad allocare un numero non valido (%1$d) di pagine.
584 Errore interno: A loop was detected in the file table of file %1$d in volume "%2$s". The loop may be caused by page %3$d|%4$d.
585 Unknown heap %1$s|%2$d|%3$d
586 Errore interno: Unable to update chain header in VPID %1$d|%2$d for heap %3$d in volume "%4$s".
587 Entries of permanent volumes are unsorted in your "%1$s" volinfo file. Entry %2$d: %3$d %4$s is out of sequence.
588 Errore interno: Errore nell'ottimizzatore. Ottimizzazione non usata.
589 User name non valido "%1$s".
590 L'Attributo domain "%1$s" della classe potrebbe non essere una \nclasse virtual o una classe proxy virtual.
591 L'Attributo domain "%1$s" della classe proxy virtual \ndeve essere una classe proxy virtual.
592 L'Attributo domain "%1$s" della classe proxy virtual \ne la classe proxy virtual devono essere nello stesso database locale.
593 Un istanza virtuale non modificabile viene richiesta e non può essere ricostruita.
594 Un object_id clause deve essere passato non usando Attributi (%1$s) con tipo di dati float or double.
595 Nome database "%1$s" troppo lungo. Nome Database deve essere meno di %2$d caratteri.
596 The %1$d pages of total temporary space allowed have been exceeded.
597 Numero di pagine for heap file %1$d|%2$d|%3$d non consistente. \n%4$d e %5$d trovati congruente con heap chain and file table, rispettivamente.
598 Numero dei file inconsistente. %1$d aspettati, %2$d trovati
599 Errore I/O mentre sincronizzo lo stato del volume "%1$s".
600 Funzione "%1$s" not implementata nel PC.
601 "%1$s".
602 dl error: "%1$s" è definito più di una volta.
603 Errore interno: Sector/page table del file VFID %1$d|%2$d sembra rovinato.
604 Errore interno: allocation set per il file VFID %1$d|%2$d(Volume "%3$s") sembra inconsistente in %4$d cancellato o marcato per la cancellazione di %5$d page holes nella tabella
605 Errore interno: Numero inconsistente di pagine mracate per la cancellazione del file VFID %1$d|%2$d(Volume "%3$s"). Aspettati %4$d, trovati %5$d.
606 Non posso trovare odbc.ini nella path "%1$s".
607 Database "%1$s" non trovato in odbc.ini.
608 Database "%1$s" non ha un Host specificato nel file odbc.ini.
609 La tua transazione non può essere rolled back. Il database potrebbe essere rovinato.
610 Il database probabilmente è rovinato perchè il logging è stato spento quando il database si è spento.
611 %s non può essere eseguito nella modalità client/server.
612 Logging non può essere disabilitato al momento perchè ci sono operazioni in corso da recuperare.
613 Nella classe "%3$s", il componente locale "%1$s" non può essere usato come alias per la classe "%2$s". I domini non sono compatibili.
614 Numero di log archives attivi eccede il massimo numero di %1$d.
615 Non posso contattare l'interfaccia Winsock. Winsock error: %1$d.
616 Non posso determinare il nome della macchina usando l'interfaccia Winsock. Winsock error: %1$d.
617 Non posso determinare l Id macchina usando l'interfaccia Winsock. Ci deve essere un entry nel host name per questa macchina.  Winsock error: %1$d.
618 L'Attributo non è modificabile.
619 Bad source codeset.
620 Bad destination codeset.
621 Tipo dato non valido. Gli argomenti contegono un dato non valido.
622 Code sets non compatibili. I code sets passati non sono compatibili.
623 Sequenza escape non valida. Una sequenza escape non valida causa un errore nella compilazione della regular expression.
624 Caratteri escape non valido.  Il carattere di escape è non-NULL con una lunghezza maggiore di 1.
625 Errore interno. Provando a modificare istanza sbagliata object %1$d|%2$d|%3$d Attributo information template con istanza object %4$d|%5$d|%6$d.
626 Errore interno. %1$d Attributi riciesti non trovati.
627 Operatore di trim non valido.  Operatore trim ha una lughezza maggiore di 1.
628 Tipo currency non valido : %1$d.
629 Pagesize %1$d is not a power of 2 or pagesize is too small. Pagesize of %2$d is used instead.
630 Conversione non supportata da %1$s.
631 SQL statement ha violato il constraint NOT NULL.
632 %1$s non è un Volume del Backup.
633 %1$s è un backup del database %2$s creato il %3$s invece del database dato %4$s creato il %5$s
634 Il database volume/file era aspettato come backup.
635 Attenzione: Log Buffer pool di %1$d buffers sembra troppo piccolo per l'ambiente corrente.\n E' possibile che qualche buffers non sia stato liberato dal sistema per UNKNOWN ERROR\n o ci sono tante transazioni contemporanee rilasciate nello stesso momento. The log buffer pool è espanso di %2$d buffers in più.
636 Attenzione Num_buffers %1$d è troppo piccolo. Il valore minimo di %2$d è stato settato.
637 Errore di sistema: liberando il log buffer. Fix count is set to zero.
638 Attenzione: Flushing an unupdatable log archive pageid = %1$d
639 Errore di sistema: il log sequence precedente pagina %1$d is not the delayed flush page %2$d.
640 Errore di sistema: il savepoint non può essere aggiunto con la transazione non attiva (es., durante commit o rollback).
641 Un nome è obbligatorio per un savepoint.
642 Errore di sistema: non c'è un processo attivo per la transazione corrente.
643 Probabile Errore di sistema: Committing/Aborting transaction = %1$d (index = %2$d) which has permanent operations\n attached to it. Will attach those system operations to the transaction
644 Errore di sistema: Log manager does not known either undo/redo function for rcvindex = %1$d
645 Errore di sistema: State = %1$s of Transaction = %2$d (index = %3$d) \n is not the correct state to apply client postpone/undo actions... ignored
646 Errore di sistema: It seems that a compensating record is missing for logical logging rcvindex = %s\n. Without this compensating record crash recovery will not be done correctly
647 Il Backup "%1$s" non è del database richiesto.
648 Il Backup non è compatibile con la corrente "%1$s" release "%2$s".
649 Precisione di %1$d non valida; dovrebbre essere maggiore di %2$d e nn maggiore di %3$d.
650 Non posso creare abbastanza processi per 'max_clients=%1$d'.
651 Memoria virtuale esaurita da preallocare %1$d sort buffers per ogni %2$d threads.
652 Wrong offset %1$d was given to split the record of length %2$d stored on slot %3$d
653 Wrong offset %1$d or length %2$d was given to remove a portion of the data of the record of length %3$d stored on slot %4$d
654 Wrong offset %1$d or length %2$d was given to overwrite a portion of the data of the record of length %3$d stored on slot %4$d
655 Non posso aprire il file di configurazione degli eventi, "%1$s".
656 Attenzione : Null id/name nel file di configurazione degli eventi, "%1$s", linea %2$d
657 Attenzione : Nome evento illegale, "%1$s", nel file di configurazione degli eventi, "%2$s", linea %3$d
658 Attenzione : event id %1$d, non in errore id range %2$d <> %3$d, file "%4$s", linea %5$d
659 Non posso accedere al programma di event handler, "%1$s".
660 Non posso aprire/connettermi al programma di event handler, "%1$s".
661 Scrittura fallita al programma di event handler, "%1$s".
662 Inizializzazione fallita nel programma di event handler.
663 Attenzione : Linea troppo lunga nel file di configurazione degli eventi, "%1$s", linea %2$d, taglio
664 Programma di Event handling partito.
665 Event handler pipe broken, "%1$s"
666 Programma di Event handling fermato.
667 Non posso trovare la classe '%1$s' con la corrispondente classe C++.
668 Errore interno: Tipo dominio sconosciuto '%1$d'.
669 Server rifiuta la connessione dal client : max clients, (%1$d), in eccesso.
670 Operazione potrebbe aver causato uno o più violazioni a delle unique constraint.
671 Errore di lettura/scrittura sulla rete per il socket %1$d associato alla transazione (index %2$d, %3$s@%4$s|%5$d)
672 Socket %1$d associato alla transazione (index %2$d, %3$s@%4$s|%5$d) è chiuso.
673 La connessione al server è stata chiusa per lo shutdown immediato del server.
674 Non ci sono abbastanza threads attivi per continuare l'esecuzione del server. Numero di threads attivi ora %1$d. Bisognerebbe arrivare fino a %2$d threads per una esecuzione ottimale del server nel tuo ambiente. Indice della Transazioni %3$d è stato rimosso per continuare l'esecuzione del server.
675 Non posso leggere la locazione del database file, '%1$s'.
676 Database '%1$s', non può essere trovato nella locazione del file, '%2$s'
677 Connessione fallita al database server, '%1$s', sull host(s): %2$s
678 Errore interno. Client restart module received NULL database server name.
679 Questa transazione C++ è già cominciata.
680 Errore di troppe C++ transaction annidate (%d).
681 %s transaction was attempted on a C++ transaction that hasn't begun.
682 Cannot %s C++ transaction until all subordinate transactions are completed.
683 Creazione di una stirnga con lunghezza errata - %d
684 metodo returned user-defined error code %d.
685 classe "%1$s" non valida.
686 Notifica : il programma event handler è stato fatto ripartire.
687 Conversioni non permesse da (%s *) a (%s *).
688 Subscript out of range.  Attempt to access element %ld. Collection size = %ld.
689 Errore interno. Iterator contains corrupt state.
690 Attempt to dereference an uninitialized instance of odb_Ref<>.
691 Attempt to use an invalid iterator.
692 Attempt to use an iterator with an unrelated collection.
693 Overflow in dati di tipo numerico.
694 Errore di sistema: Unable to load B+tree.
695 Wrong interface has been used to kill a client.
696 Unknown transaction (index %1$d, %2$s@%3$s|%4$d).
697 Given transaction index (index %1$d, %2$s@%3$s|%4$d) does not match with current transaction index (index %5$d, %6$s@%7$s|%8$d). Likely, the given transaction has finished and its transaction index was re-assigned to another transaction.
698 Errore interno: INDEX %1$s ON classe %2$s (classe_OID: %3$d|%4$d|%5$d). the number of OIDs in the unique hierarchy: %6$d does not equal the number of OIDs: %7$d found in the unique B+tree plus the number of NULLs: %8$d found in the unique hierarchy for B+tree: %9$d|%10$d|%11$d.
699 Errore interno: INDEX %1$s ON classe %2$s (classe_OID: %3$d|%4$d|%5$d). the number of OIDs in the unique hierarchy: %6$d does not equal the number of OIDs: %7$d found in the unique B+tree root statistics for B+tree: %8$d|%9$d|%10$d.
700 Errore interno: INDEX %1$s ON classe %2$s (classe_OID: %3$d|%4$d|%5$d). the number of OIDs: %6$d does not equal the number of NULLs: %7$d plus the number of keys: %8$d in the unique B+tree: %9$d|%10$d|%11$d.
701 No proxy è dfinito in "%1$s" at ldb "%2$s".
702 Errore interno: INDEX %1$s ON classe %2$s (classe_OID: %3$d|%4$d|%5$d). the number of NULLs in the unique hierarchy: %6$d does not equal the number of NULLs: %7$d found in the unique B+tree root statistics for B+tree: %8$d|%9$d|%10$d.
703 Errore interno: INDEX %1$s ON classe %2$s (classe_OID: %3$d|%4$d|%5$d). OID: %6$d|%7$d|%8$d found in unique B+tree: %9$d|%10$d|%11$d does not belong to one of La classees for the unique constraint.
704 Evaluation of generic server function failed.
705 Inconsistent %1$d permanent volumes were found when %2$d were expected.
706 Cannot expand permanent volumes. Permanent volume "%1$s" with purpose %2$s was given.
707 Unable to expand temporary volume "%1$s" with %2$d pages
708 Unable to expand temporary disk volume "%1$s" with %2$d pages (%3$ld Kbytes) due to insufficient space. Current space available is %4$d pages (%5$ld Kbytes).
709 Constraint "%1$s" not trovato.
710 Constraint non valido.
711 Un oggeto con l'Attributo dato non esiste.
712 Constraint" %1$s" già esistente.
713 Un indice non esiste per l'Attributo.
714 Query fallita per spazio temporaneo insufficiente.
715 Verifica dello schema fallito: %1$s
716 constraints NOT NULL non sono permessi in virtual or proxy classi.
717 constraints UNIQUE non sono permessi in virtual or proxy classees.
718 Invalid gadget.
719 Numero di Attributi (%d) e valori (%d) differenti.
720 Cannot use a gadget to insert into a virtual or proxy classe.
721 Insert query yields piu di una riga.
722 Numero sbagliato di Attributi per constraint NOT NULL .
723 Non posso modificare il domain di un Attributo esistente.
724 Parametro non valido per default constraint name generation.
725 Errore interno: expected temporary oid and encountered permanent oid.
726 UNIQUE constraint deve essere aggiunto usando una separata alter statement\n.Per esempio, invece di:\n\talter table1 add Attributo a int unique;\nusa:\n\talter table1 add Attributo a int;\n\talter table1 add unique(a);
727 Trigger event non valido.
728 Query fallita perchè file temporaneo vfid non è valido per la transazione %1$d.
729 Overflow occurred in subtraction context.
730 Overflow occurred in multiplication context.
731 Overflow occurred in division context.
732 Overflow occurred in unary minus context.
733 Overflow occurred in coercion context.
734 Errore interno: Header of file VFID %1$d|%2$d(Volume "%3$s") è rovinato.
735 Unknown transaction index %1$d.
736 Bisogan specificare il servizio o l'identificatore della porta. Winsock error: %1$d.
737 Servizio TCP sconosciuto : %1$s. Winsock error: %2$d.
738 Non posso creare socket stream. Winsock error: %1$d.
739 Non posso ascoltare local address... riprovo. Winsock error: %1$d.
740 Non posso ascoltare local address... esco. Winsock error: %1$d.
741 Accept fallito. Winsock error: %1$d.
742 Invio un broadcast message al client. Winsock error: %1$d.
743 Fallito handshake tra client e server. (peer host %s)
744 Errore in rehashing a proxy/vclasse MOP.
745 Invalid garbage collection phase.
746 Invalid garbage collection callback.
747 Invalid garbage collection callback ticket.
748 Invalid object pointer encountered by db_gc_mark_object.
749 The number of fetched instances are not matched: %1$d expected, %2$d received.
750 The number of fetched instances are not matched: %1$d expected, %2$d received.
751 The OID of a new object isn't available in a BEFORE INSERT trigger.
752 Errore durante il restore del backup unit_num %d.
753 Errore durante il restore del backup unit_num %d, pageid %d exceeds total number of pages %d for volid %d.
754 OO proxy instance has non-string-type or null key.
755 %s non può essere eseguito in modalità stand-alone.
756 String buffer specifcato è troppo picoolo per fare encode dell object.
757 Errore interno: cannot assign permanent OID during object encode.
758 La Stringa specificata non è valida per fare il decoding object.
759 The target object è stato cancellato.
760 Errore interno: istanza proxy object ha una NULL key durante object encode.
761 Errore interno: vclasse object instance maps to invalid real instance.
762 Errore interno in resolving vclasse instance to a real instance.
763 Errore interno in encoding nonupdatable object instance.
764 Errore interno in encoding object into internal vobj form.
765 Output object argument has a NULL address.
766 Errore interno in decoding object.
767 Non posso creare un level %d backup senza prima creare un livello %d backup.
768 Il database locale "%1$s" è stato usato in questa sessione , modifiche o cancellazioni non sono permesse.
769 Attributo "%s", della classe "%s" non è una classe valida o uno shared Attributo.
770 No table functions.
771 Parametri non validi per la funzione.
772 Classe db_serial non trovata.
773 Serial "%s" non trovato.
774 Serial "%s" esite già.
775 il Next value supera il valore di MAX(MIN).
776 Cannot fetch serial object.
777 Non posso modificare serial object.
778 Non posso creare un symbolic link "%s" al file "%s".
779 Limiti della Date superati. Le Date DEVONO essere da 1 a 9999 .
780 Internal system failure: impossibile prendere ora del sistema.
781 Format string is too long. It must be less than 16K.
782 Stringa vuota non permessa.
783 Formato non valido.
784 I 2 argomenti non coincidono.
785 Primo argomento di tipo stringa è troppo lungo . Deve essere meno di 16K.
786 Format è duplicato.
787 ERRORE: Errore nella conversione del tipo time.
788 ERRORE: Errore nella conversione del tipo datetime.
789 Numero sbagliato.
790 Esecuzione della Query execution è stata interrota dall'utente.
791 Valore serial non valido.
792 Non posso allocate memoria for css.
793 pthread_attr_init() failed.
794 pthread_attr_destroy() failed.
795 pthread_attr_setdetachstate() failed.
796 pthread_attr_setscope() failed.
797 pthread_attr_setstacksize() failed.
798 pthread_create() failed.
799 pthread_join() failed.
800 pthread_mutex_init() failed.
801 pthread_mutex_destroy() failed.
802 pthread_mutex_lock() failed.
803 pthread_mutex_trylock() failed.
804 pthread_mutex_unlock() failed.
805 pthread_mutexattr_init() failed.
806 pthread_mutexattr_destroy() failed.
807 pthread_mutexattr_settype() failed.
808 pthread_mutexattr_gettype() failed.
809 pthread_cond_init() failed.
810 pthread_cond_destroy() failed.
811 pthread_cond_wait() failed.
812 pthread_cond_timedwait() failed.
813 pthread_cond_signal() failed.
814 pthread_cond_broadcast() failed.
815 pthread_key_create() failed.
816 pthread_key_delete() failed.
817 pthread_setspecific() failed.
818 pthread_getspecific() failed.
819 pthread_once() failed.
820 Non posso inizializzare la connessione
821 Connessione interrotta.
822 Mutex operation failed while getting next client id.
823 Mutex operation failed while initializing connection list.
824 Cannot destroy mutex while finalizing connection list.
825 pread() call failed.
826 pwrite() call failed.
827 Connection list traverse function returned invalid value.
828 Invalid critical section of the index was given.
829 This mutex lock has been unlocked already.
830 Cannot allocate query entry any more. Maximum allocatable entries are %d.
831 All transaction descriptors(%d) are in use. Raise up max_clients parameter.
832 Backup is already running. Backup cannot be run by two processes.
833 The index name "%1$s" is ambiguous. Specify La classe name of the index.
834 There are some mismatches between source string "%1$s" and format string "%2$s" given in to_number().
835 Undefined statistics item.
836 LATCH nella pagina(%d|%d) TIMEDOUT
837 Cannot drop the user who owns database objects(classe/trigger/serial etc). 
838 Cannot open more than %d index scans simultaneously.
839 "%s": Unknown system parameter or bad value.
840 Cannot change system parameter "%s".
841 Requested operation can be executed by only sole client(transaction).
842 Cannot cache lock of object %1$d|%2$d|%3$d.
843 Wrong arguments in %s, a %s was passed.
844 Unknown isolation(%d) is found. The transaction index is %d.
845 Invalid object type(%d) is found. The lockable object is %d|%d|%d.
846 %s of tran(%d) is not found in the holder list of an object(%d|%d|%d).
847 %s on a %s(%d|%d|%d) is not found in the transaction(%d) hold list(cnt=%d).
848 %s on an object(%d|%d|%d) is not found in the transaction(%d) non2pl list.
849 Trying to abort a transaction(%d) twice.
850 Tran(%d) is neither lock holder nor lock waiter of an object(%d|%d|%d).
851 Failed to allocate lock resource (%s). 
852 Total holders mode(%d) of an empty lock holder list is not NULL_LOCK.
853 For an object(%d|%d|%d), tran(%d) of Uncommitted Read held fault lock(%s).
854 Attenzione: No lock holder, but lock waiters exist. refer to %s.
855 Attenzione: Two or more threads are lock-waiting for one transaction(%d).
856 Attenzione: Strange lockwait state(%d|%d), thread(%d|%d) and transaction(%d).
857 An I/O error occurred while truncating page %1$d del volume "%2$s".
858 Volume "%1$s" is unknown at server restart.
859 LATCH nella pagina(%d|%d) ABORTED
860 Cannot make current transaction as a part of a global transaction because its state is '%1$s'.
861 Cannot prepare to commit the current transaction with transaction identifier %1$d because that transaction is not made as a part of a global transaction.
862 Cannot set the user information of the global transaction with the identifier %1$d because its state is '%2$s'.
863 Cannot get the user information of the global transaction with the identifier %1$d because it is not set.
864 Cannot open backupdb/restoredb verbose file '%s'.
865 Cannot execute statement due to unknown type input markers.
866 Codice errore inusato.
867 Codice errore inusato.
868 Codice errore inusato.
869 Codice errore inusato.
870 Codice errore inusato.
871 Codice errore inusato.
872 Codice errore inusato.
873 PKI authentication error "%1$s".
874 Argument of power() is out of range.
875 Overflow happened in power context.
876 Codice errore inusato..
877 No data to be unloaded.
878 Invalid database location file, '%1$s'.
879 Cannot get permission: '%s'.
880 Cannot change permission '%s'.
881 Cannot get lock file '%s(FILE ID:%d)'.
882 Cannot release lock file 'FILE ID:%d'.
883 Failed to get '%s' file status.
884 Volext-pathname '%s' is not permitted to write.
885 Failure to move from '%s' to '%s'.  Use the same device(disk).
886 "%1$s" caused unique constraint violation.
887 Stored procedure '%s' is already exist.
888 Parameter count is invalid. expected: %d, actual: %d
889 Stored procedure execute error: %s
890 Partition failed.
891 Appropriate partition does not exist.
892 Primary key "%2$s" already defined for classe "%1$s".
893 Attributo '%1$s', a member of the primary key, could not be dropped.
894 Stored procedure/function '%s' is not exist.
895 Invalid stored procedure type: '%s' is the '%s'.
896 Compression failed. Zip metodo: %d (%s), Zip Level: %d (%s)
897 Decompression failed.
898 Errore di Replicazione: "%1$s"
899 Invalid partition requests.
900 Java VM library non trovata : %s. \nDi conseguenza le chiamate alle java stored procedure non sono permesse.
901 Java VM non può partire : %s. \nDi conseguenza le chiamate alle java stored procedure non sono permesse.
902 Java VM is not running.
903 Non posso connetermi alla Java VM: %s
904 Nome stored procedure name non valida.
905 Networking con JVM fallita: %d
906 Invalid java metodo signature.
907 L'owner o un membro odel gruppo DBA possono droppare le stored procedure.
908 Troppi argomenti per la stored procedure '%s'.
909 Missing or invalid catalog classe/vclasse is found.
910 Not allowed access to partition.
911 Invalid call: it can not return ResultSet.
912 ResultSet can not be used input parameter.
913 Too many nested stored procedure call.
914 A serial object già esistente as an auto increment constraint.
915 Increase value of an auto increment constraint should be greater than 0.
916 Active log volume is not included in this Volume del Backup(%s).
917 Active log volume già esistente.
918 La classe '%s' referred by the foreign key does not exist.
919 La classe '%s' referred by the foreign key does not have the primary key.
920 The foreign key '%s' does not include the primary key member '%s'.
921 The domain of the foreign key member '%s' is different from that of the primary key member '%s'.
922 The constraint of the foreign key '%s' is invalid.
923 The primary key referred by a foreign key is not supposed to be dropped.
924 Update/Delete operations are restricted by the foreign key '%s'.
925 Acquiring lock for the foreign key '%s' has failed.
926 The instance having the foreign key '%s' cannot be dropped.
927 The number of keys of the foreign key '%s' is different from that of the primary key '%s'.
928 Not a user but the system can assign the Attributo '%s' created by 'ON CACHE OBJECT' of foreign key.
929 A foreign key cannot be defined in virtual classe.
930 It is able to drop the Attributo '%s' created by 'ON CACHE OBJECT' of foreign key only after removing the constraint of the foreign key.
931 Start value of an auto increment constraint should be less then max value.
932 It is not able to repair an inconsistency of the number of free pages for the volume "%1$s".  Because %2$d pages obtained from the volume header is inconsistent with %3$d pages from the bitmap.
933 It is not able to repair an inconsistency of the number of free sectors for the volume "%1$s".  Because %2$d sectors obtained from the volume header is inconsistent with %3$d sectors from the bitmap.
934 Null domain referenced.
935 Argumento %s della funzione is out of range.
936 Overflow occurred in exp context.
937 Database error.
938 Argomento non valido.
939 Troppe connessoni.
940 The handle is invalid.
941 The operation is not supported.
942 Handle timed out waiting for locks.
943 Errore generico.
944 Lo statement non è preparato.
945 Lo statement non ha result set.
946 Lo statement non è stato eseguito.
947 There is no more result set.
948 Grandezza buffer errata , troppo piccola.
949 Non ci sono informazioni disponibili.
950 Il nome non è valido.
951 Il resultset non è updatable.
952 Riga cancellata.
953 Parametri per prepared statement non settati.
954 The statement is not batch statement.
955 Can not clear this batch statement.
956 Can not batch execute.
957 The statement is prepared statement.
958 The statement is not prepared statement.
959 The statement is batch statement.
960 There is no more error.
961 There is no more result.
962 Memoria esaurita
963 Broker error.
964 The result set handle is closed.
965 Non puoi usare nomi di Attributo duplicati in index key list. Nome Attributo '%s' presente piu di una volta.
966 La tua transazione (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on object %6$d|%7$d|%8$d because of deadlock. You are waiting for user(s) %9$s to finish.
967 La tua transazione (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on classe %6$s because of deadlock. You are waiting for user(s) %7$s to finish.
968 La tua transazione (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on instance %6$d|%7$d|%8$d of classe %9$s because of deadlock. You are waiting for user(s) %10$s to finish.
969 Server bit platform (%1$d) is different from client bit platform (%2$d).
970 Server HA mode is changed from '%1$s' to '%2$s'.
971 Program '%1$s' (pid %2$d) connected to database server '%3$s' on the host '%4$s' (port %5$d).
972 Program '%1$s' (pid %2$d) was connected from the host '%3$s'. (transaction index %4$d)
973 Lo Stato del server è %1$s.
974 Archive log "%1$s" is created to archive pages from %2$d to %3$d.
975 Current version of replication does not allow changing multiple rows with a single UPDATE statement which can violate the UNIQUE constraint.
976 Errore interno: Table size overflow (allocated size: %1$d, accessed size: %2$d) at file table page %3$d|%4$d(volume "%5$s")
977 Checkpoint started (page %d).
978 Checkpoint finished (page %d).
979 CONNECT BY loop in user data.
980 A thread is waiting in %1$s more than %2$d msec.
981 Transaction %1$d has reached to the threshold of monitoring statistics %2$s.
982 Set interrupt to the transaction %1$d.
983 Instances of a reusable OID classe are non-referable. This operation is not permitted on non-referable instances.
984 Cannot set %3$s to "%6$s" in the "[%5$s]" section when it is set to "%4$s" in the "[%2$s]" section of the configuration file "%1$s".
985 The hostname on the database connection string should be specified when multihost is set in "databases.txt".
986 CUBRID heartbeat è partita. 
987 CUBRID heartbeat fermata.
988 Node event: %1$s.
989 Process event: %1$s. %2$s
990 Command execution: %1$s. %2$s
991 Flush victim candidates of page buffer started.
992 Flush victim candidates of page buffer finished (count: %d).

993 Ultimo Errore

$set 6 MSGCAT_SET_INTERNAL
1 Errore in subsystem (linea %d):
2 Nessun messaggio di errore presente.
3 Non posso allocare %d bytes.
4 Non trovo message catalog "%s".
5 Messaggio mancante per codice di errore %d.
6 ... ABORT/EXIT IMMEDIATELY ...\n
7 Vuoi uscire? 1/0
8 File sconosciuto.
9 Nessun messaggio presente; questo errore sostituisce l'errore mancante.
10 er_init: *** Incorrect exit_ask value = %d; will assume %s instead. ***\n
11 er_start: *** Attenzione: Impossibile aprire il message log "%s"; uso stderr invece. ***\n
12 er_study_fmt: messaggio per il codice di errore %d.
13 er_estimate_size: codice di conversione sconoscituo (%d).
14 \n\n*** Message log wraparound. Messaggi continuano all'inizio del file. ***\n\n
15 \nTime: %s - %s *** %s CODE = %d, Tran = %d%s\n%s\n
16 CUBRID (pid: %d) *** %s *** %s CODE = %d, Tran = %d %s
17 \nTime: %s - %s *** file %s, line %d %s CODE = %d Tran = %d%s\n%s\n
18 CUBRID (pid: %d) *** %s *** file %s, line %d, %s CODE = %d, Tran = %d. %s
19 \n*** Il messaggio di errore precedente è l'ultimo. ***\n\n
20 \nTime: %s - DEBUG *** file %s, line %d\n
21 Mail -s "Server è stato fermato"
22 --->>>\n %s è stato fermato quando il messaggio di errore era:\n errid = %d, %s\nUser: %s, pid: %d, host: %s, time: %s<<<---\n
23 %s è stato fermato per errid = %d. User: %s, pid: %d
24 er_init: cannot install event handler "%s"\n

$set 7 MSGCAT_SET_PARSER_SYNTAX
1 controllare la sintassi '%s', mi aspetto %s.
2 controllare la sintassi '%s', mi aspetto %s in %s.
3 controllare la correttezza del token '%s'.
4 'read uncommitted schema' is not acceptable, using \n'read committed schema' instead.
5 Lunghezza massima per le stringhe è 16383.
6 %s eccede il limite di %ld.
7 Massima lunghezza per bit string è %d bits.
8 Non posso usare uan subquery in una join condition clause.
9 Non posso specificare l'Attributo "(%s)" nella modifica dei privilegi in un statement di grant/revoke.
10 uso non valido della keyword '%s', aspettato %s.
11 Lunghezza Token troppo lunga.
12 Tipo dato non valido
13 Attach statement non valido\n  attach_statement : ATTACH unsigned_integer
14 invalid prepare statement non valido\n  prepare_statement :  PREPARE {to} COMMIT unsigned_integer
15 execute statement non valido\n  execute_statement :  EXECUTE DEFERRED TRIGGER [identifier {, identifier}* | ALL TRIGGERS]
16 scope statement non valido\n  scope_statement : SCOPE trigger_action { FROM table_spec_list }
17 set system parameter non valido\n  set_system : SET SYSTEM PARAMETERS 'STRING' {, 'STRING_N'}*
18 set transaction non valido\n  set_transaction: SET TRANSACTION ISOLATION LEVEL expression {, expression}\n                 | SET TRANSACTION LOCK TIMEOUT timeout_spec
19 set trigger trace non valido\n  set_trigger: SET TRIGGER TRACE [ON|OFF|unsigned_integer|parameter|host_param]
20 set trigger depth non valido\n  set_trigger: SET TRIGGER {MAXIMUM} DEPTH [INFINITE|unsigned_integer|parameter|host_param]
21 set optimization level non valido\n  SET OPTIMIZATION LEVEL {=|to} [ON|OFF|unsigned_integer|parameter|host_param]
22 set optimization cost non valido\n  SET OPTIMIZATION COST {of} 'STRING' {=|to} literal_
23 get optimization level non valido\n  GET OPTIMIZATION LEVEL {[INTO|TO] [host_param|parameter|identifier]}
24 get optimization cost non valido\n  GET OPTIMIZATION COST {of} 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
25 get transaction isol non valido\n  GET TRANSACTION ISOL LEVEL {[INTO|TO] [host_param|parameter|identifier]}
26 get transaction lock non valido\n  GET TRANSACTION LOCK TIMEOUT {[INTO|TO] [host_param|parameter|identifier]}
27 get trigger trace non valido\n  GET TRIGGER TRACE {[INTO|TO] [host_param|parameter|identifier]}
28 get trigger depth non valido\n  GET TRIGGER {MAXIMUM} DEPTH {[INTO|TO] [host_param|parameter|identifier]}
29 get stat non valido\n  GET STATISTICS 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
30 create index non valido\n  CREATE {REVERSE} {UNIQUE} INDEX {identifier} ON {only} classe_name index_column_name_list
31 create user non valido\n  CREATE USER identifier {PASSWORD 'STRING'} {GROUPS identifier {, identifier}* } {MEMBERS identifier {, identifier}* }
32 create trigger non valido\n  CREATE TRIGGER identifier {STATE [ACTIVE|INACTIVE]} {PRIORITY unsigned_real}\n    [BEFORE|AFTER|DEFERRED] event_spec {IF trigger_condition}\n    EXECUTE {AFTER|DEFERRED} trigger_action
33 create serial non valido\n  CREATE SERIAL identifier {START WITH integer} {INCREMENT BY integer}\n    serial_min_max {CYCLE|NOCYCLE} {CACHE unsigned_integer|NOCACHE}
34 create procedure non valido\n  CREATE PROCEDURE identifier '(' {sp_param_list} ')' {IS|AS} LANGUAGE JAVA NAME 'STRING'
35 create function non valido\n  CREATE FUNCTION identifier '(' {sp_param_list} ')' RETURN {data_type|CURSOR} {IS|AS} LANGUAGE JAVA NAME 'STRING'
36 groups non valido\n  groups spec : GROUPS identifier {, identifier}*
37 members non valido\n  member spec : MEMBERS identifier {, identifier}*
38 password non valida\n  password spec : PASSWORD 'STRING'
39 Mancanza di un comando authorized\n  authorized command = SELECT, INSERT, INDEX, DELETE, UPDATE, ALTER, ADD, DROP, EXECUTE, REFERENCES, ALL PRIVILEGES, ALL 
40 Mancanza classe spec list
41 Mancanza identifier list
42 Mancanza expression
43 Identificatore non valido
44 Mancanza dell'uso of substring\n  SUBSTRING( expression FROM expression FOR expression )\n  SUBSTRING( expression FROM expression )
45 uso non valido di trim\n  TRIM ( [LEADING|TRAILING|BOTH] expression FROM expression )\n  TRIM ( [LEADING|TRAILING|BOTH] FROM expression )\n  TRIM ( expression FROM expression )\n  TRIM ( expression )
46 uso non valido di cast\n  CAST ( expression AS data_type )
47 fuunzione add_months necessita di 2 parametri
48 octet_length necessita di 1 parametro
49 bit_length necessita di 1 parametro
50 lower necessita one di 1 parametro
51 upper necessita one di 1 parametro
52 translate necessita di 3 parametri
53 replace necessita di 2 o 3 parametri
54 Uso non valido della funzione to_number\n  TO_NUMBER ( expression , char_string )\n  TO_NUMBER ( expression )
55 string bind operator is not '|' but '||'
56 alter statement non valido
57 drop statement non valido
58 create statement non valido
59 relational operator non valido
60 L'operatore di uguaglianza non è '==' ma '='
61 L'operatore di differenza non è '!=' ma '<>'
62 invalid minus value (or minus value in list)
63 Regola di reference errate
64 SYS_CONNECT_BY_PATH necessita di 2 parametri e il secondo deve essere una stringa.
65 Unsigned integer non valido %s.


$set 8 MSGCAT_SET_PARSER_SEMANTIC
1 Memoria esaurita.
2 Funzione %s non definita.
3 Non posso trovare ldb '%s' nel database.
4 Il numero di colonne, %d, nella left query non è lo stesso\ntnumero di colonne, %d, nella right query.
5 '%s' non è compatibile per una union con '%s'.
6 %s non è definito.
7 Il dominio di %s non può essere una vclasse.
8 Il dominio di %s non può essere una proxy vclasse.
9 Il dominio di %s non può essere uan classe.
10 %s non è un attribto di %s.
11 %s non può essere cambiato per questa classe.\nEra ereditata da %s.
12 classe %s non esiste.
13 %s non è un %s.
14 %s non è un metodo o Attributo di %s.
15 %s non ha e deve avere  una query specificata.
16 %s non è definito nel database.
17 %s non è una superclasse di %s.
18 classe %s eredita da non-classe %s.
19 Vclasse %s eredita da non-vclasse %s.
20 Proxy vclasse %s eredita da non-proxy vclasse %s.
21 %s è già una superclasse di %s.
22 %s potrebbe non essere una superclasse di se stessa.
23 %s è già una subclasse di %s.
24 %s non è un singolo valore. Attributo pubblicato nelle\n query aggregate deve esserci nella clausola di group by.
25 Il numero delle colonne(%d) e into_names(%d) non sono uguali.
26 classe %s già esistente.
27 Super classe %s in '%s' non esiste.
28 Expecting a SUBclasse clause to make INHERIT clause '%s' meaningful.
29 Clausola INHERIT '%s' referenzia una classe che non è nella SUBclasse clause '%s'.
30 classe non può avere una query specificata.
31 %s non è una ldb registrata.
32 A proxy heir may not have its own object_id clause.  It cannot override its inherited object_id Attributo(s).
33 Proxy vclasse ha più di una query specificata.
34 Il numero di Attributu, %d, non è uguale al numero delle colonne , \n%d, nella query specificata.
35 Mancanza del nome Attributo per %s.
36 Attributo '%s' non è compatibile con la colonna '%s' della query
37 %s non è una classe.
38 Indices potrebbero non essere creati per una virtual classees.
39 Indices possono essere creati solo per un singolo Attributo.
40 Uso non conforme di classi multiple nella clausola di from.
41 Errore di sistema -- Fallita Resolution su %s.
42 Non dovresti cancellare una tabella derivata.
43 User %s non è nel database.
44 Update non può averer aggregation.
45 Nomi Attributi in ogni classe devono essere unici. Nome Attributo  '%s' nella classe '%s' è specificato piu di una volta.
46 Ci sono assegnazioni multiple a %s.
47 Numeri nell'ordinamento non è un intero.
48 %d nell'ordinamento è fuori dei valori.
49 Order by non è un intero.
50 %s nell'ordinamento non è conforme a nessuna colonna selezionata.
51 Ci sono dei conflitti nell'ordinamento per la colonna %s.
52 %s nell'ordinamento non è un numero, nome o path expression.
53 Insert expression può essere presente solo nelle insert.
54 Il numero di Attributi (%d) e le colonne selezionate (%d) non sono uguali.
55 Il numero di Attributi (%d) e i valori (%d) non sono uguali.
56 Data overflow sul tipo dato %s.
57 Tipo dati incompatibili per Attributo %s.
58 %s\n non è una SELECT su una singola classe.
59 `*' non è una select list valida per una proxy vclasse query.
60 %s non è una vclass ne una proxy vclasse.
61 Sono presenti piu clausole di isolation level.
62 Sono presenti piu clausole timeout.
63 Isolation level deve essere specificato.
64 Operatore BETWEEN non è definito per i tipi %s, %s e %s.
65 Operatore '%s' non è definito per i tipi %s e %s.
66 Funzione deve avere come argomenti: %s.
67 Operatori non compatibili: %s(%s).
68 Funzione di aggregazione deve avere alemno un argomento : %s.
69 Attempt to divide by zero.
70 Data overflow coercing %s to type %s.
71 Cannot coerce %s to type %s.
72 Il tipo di '%s' deve essere SET, MULTISET, o SEQUENCE.
73 '%s' deve avere esattamente un set come element type.
74 %s ha più di un comando derivato.
75 %s non ha nomi di colonne derivati.
76 %s non ha il numero corrispondente di colonne (%d) e nomi colonne (%d).
77 Non ci sono Attributi in %s.
78 Singola tabella aspettata in %s.
79 Nome %s è ambiguo.
80 Nome di colonne derivate '%s' non sono permesse per la specifica della classe.
81 Tabella derivata '%s' deve avere un nome correlato.
82 Select authorization fallito, classe %s.
83 Referenza dell'istanza della classe  %s per una tabella derivata non è permessa.
84 Correlazione di Path (%s) definita fuori dall inner path term.
85 Referenza a %s è ambiguo.
86 classe di %s non ha Attributo %s.
87 classe (%s) non hanno tutti gli Attributi %s.
88 classe (%s) non ha Attributo %s.
89 %s è del tipo %s , non un tipo oggetto.
90 Selector {%s} does not resolve to an exposed classe specification in\n the required context (%s).
91 Selector {%s} si riferisce a non-classe specification (%s).
92 Selector {%s} cannot root the path expression defining itself.
93 Selector {%s} is circularly defined in terms of selector {%s}.
94 Selector {%s} è ridefinito a %s.
95 %s (domain object) non ha l'Attributo %s.
96 %s in EXCEPT spec non è una subclasse.
97 ALL ... EXCEPT list excludes EVERYTHING nella classe.
98 '%s' è inconsistente con l'uso del path selector '%s' o per path root '%s'.
99 Argomento indefinito.
100 Argomento non è un object.
101 %s non è un metodo o Attributo di classe di %s.
102 Query specification non deve avere una into-clause.
103 Proxy query specification non deve avere aggregate expression,\n group-by-clause e having-clause.
104 Proxy query specification non deve avere DISTINCT o UNIQUE \nkeyword nella select list.
105 Proxy query specification deve essere un SELECT statement.
106 Proxy query specification deve referenziare solo una tabella o classe \ndal database database.
107 User '%s' non è owner di %s.
108 Proxy '%s' è già definito nella classe '%s' di ldb '%s'.
109 CUBRID non supporta proxy con oggetti in liste eterogenee.
110 Where-clause deve essere un'espressione logica.
111 Like escape clause deve essere una stringa o una host variable.
112 Combinazione non valida di precision (%d) e scale (%d).
113 Precision data di %1$d non è valida; dovrebbe essere maggiore di 2$d e non piu grande di %3$d.
114 Precision (%d) troppo grande. Il valore massimo è %d.
115 metodo %s deve avere un valore di ritorno.
116 Numero di argomenti passati al metodo non coincide con\nil numero definito nel metodo.
117 Chiamata del metodo deve avere un target.
118 metodo %s non è definito per il target.
119 Metodi d'istanza non possono essere chiamati sull'oggetto classe.
120 Metodi della classe non possono essere chiamati nell'istanza.
121 Metodi della classe richiedono una class object come target.
122 Metodi richiedono un object come target.
123 metodo %s non è fully typed, controlla la definizione del metodo nello schema delle classi.
124 Time arithmetic underflow.
125 Date arithmetic underflow.
126 Illegal left hand side of an assignment clause.
127 metodo calls on set derived tables correlated to an outer scope are \nnot supported unless the set expression is wrapped in a subquery.
128 La colonna della query che corrisponde \nall'Attributo '%s' deve essere NULL o NA.
129 The query specification must have some non-NULL columns in the select list.
130 Vclasse objects nelle liste non è supportato.
131 Aspetto un valore di tipo stringa.
132 Aspetto uno statement o un valore stringa.
133 Attributo Proxy object_id  '%s' non deve essere mappato\na nelle colonne della query.
134 constraint %s ignorato.
135 Dichiarazione constraint non permesso in questo contesto.
136 Casting %s al tipo %s non supportato.
137 %s non è una bit string valida.
138 Operatore '%s' non è definito per il tipo %s.
139 Operatore '%s' non è definito per il tipo %s, %s, e %s.
140 Valore degli operatori '%s' non possono essere variabili dynamic host.
141 Troppi argomenti per la funzione generica %s.\nSolo %d argomenti sono permessi per le funzioni generiche.
142 Selectore {%s} per la classe '%s' non è uan subclasse di '%s'.
143 Non posso estrarre '%s' campo per il tipo %s.
144 La Query non deve contenere una clausola ORDER BY.
145 Condizione per il Case deve essere di tipo logico.
146 Espressione deve essere di tipo %s.
147 Serial '%s' già esistente.
148 Serial '%s' non definito.
149 Valore di Incremento non può essere 0.
150 Valore massimo eccede 1.0e37.
151 Valore minimo non valido.
152 Valore di Incremento non valido.
153 Valore minimo eccede -1.0e36.
154 Valore massimo non valido.
155 Specificare opzione per alter serial.
156 Creatore o DBA possono modificare il serial.
157 Valore Serial non possono eccedere il loro minimo o massimo
158 I/O error interno per serial
159 Index '%s' non trovato.
160 Il nome per index '%s' si riferisce ad una classe , ma non è specificato nella clausola di from.
161 Due o piu classi hanno lo stesso indice '%s'. Usa il nome completo con il qualificatore.
162 Uso non valido di %s.\n%s espressione non può essere usatsa con un Attributo o una subquery correlata.
163 La release corrente restringe un espressione contenete ORDERBY_NUM() nella select list.
164 Path expression '%s' in una condizione di join.
165 '%s' non è definita nella condizione di join.
166 La release corrente restringe il settaggio della tabelle derivate alla right outer join.
167 Non posso inserire con una query in un altro ldb.
168 Solo una colonna può essere specificata in una select list quando la subquery non inizia con EXISTS.
169 Illegal right hand side of an assignment clause. Context requires query to be single column.
170 Non posso usare una subquery in una join condition.
171 Definizione non valida di partizione.
172 Colonna della partizione deve essere specificata.
173 Solo una colonna può essere specificata come colonna di partizione.
174 Tipo Colonna per partizione non supportata.
175 Definizione di partizione è duplicata. '%s'
176 Mismatch di un Tipo Constante . '%s'
177 Grandezza partizione non valida.
178 '%s' non è una classe partizionata.
179 '%s' è già una classe partizionata.
180 Richiesta di Partizione non valida.
181 %s non può essere cambiata dalla sua classe (partition key column).
182 %s non può essere partizionata dal trigger attivo.
183 Partizione %s non esiste.
184 Mismatch di tipo partizione. '%s'
185 Informazione sulla Partizione non è valido.
186 Settaggio di un Aggiunta di Partizione è maggiore di quella esistente.
187 Riorganizzazione di una Partizione non è permessa perchè potrebbe portare ad una perdita di dati.
188 L'intervallo della Partizione '%s' non è valida.
189 Un Attributo di una classe non può avere un auto increment constraint.
190 Un Attributo di una classe virtuale non può avere un auto increment constraint.
191 Attributo '%s' del dominio che ha un auto increment constraint non è valido.
192 Solo il sistema e non l'utente può assegnare al'Attributo '%s' creato da 'ON CACHE OBJECT' di una foreign key.
193 Non posso usare il nome dell'Attributo duplicato in una index key list. Nome Attributo '%s' presente piu di una volta.
194 La funzione %s non è definita o sono stati passati un numero errato di parametri.
195 '%s' può essere solo usato nella gerarchia delle queries. Specificare una clausola di CONNECT BY.
196 '%s' non è permesso qui.
197 Usando CONNECT_BY_ISCYCLE è solo permesso per query con la clausola CONNECT BY NOCYCLE clause.
198 Opzione per tabella '%s' duplicata
199 La classe '%s' è marcata come REUSE_OID ed è non-referable. Le classi Non-referable non possono essere avere Attributi e le loro OIDs di istanza non può essere ritornato.
200 Il numero per CACHE deve essere minore che un ciclo (%s).

$set 9 MSGCAT_SET_PARSER_RUNTIME
1 memoria esaurita: impossibile allocare %d bytes.
2 Livello per Transaction isolation deve essere 1, 2, 3, 4, 5 o 6.
3 Valore di Timeout deve essere -1, 0, o >0.
4 Settaggio per Isolation level :\n
5          READ COMMITTED SCHEMA, READ UNCOMMITTED INSTANCES.\n
6          READ COMMITTED SCHEMA, READ COMMITTED INSTANCES.\n
7          REPEATABLE READ SCHEMA, READ UNCOMMITTED INSTANCES.\n
8          REPEATABLE READ SCHEMA, READ COMMITTED INSTANCES.\n
9          REPEATABLE READ SCHEMA, REPEATABLE READ INSTANCES.\n
10 In una query non-updatable non è permesssa una chiamata db_query_execute_oid.
11 Transaction timed out waiting for locks.
12 Provo ad accedere alla variabile host con inidice %ld quando solo %ld \nvariabili host in input sono date.
13 Conversione non implementata.
14 Errore interno, Conversione indefinita.
15 '%s' yields nessun risultato.
16 '%s' yields con piu di una riga.
17 Variabile dell'Interprete %s non definito.
18 Non posso calcolare '%s'.
19 Variabile sconosciuta %s.
20 Attributo shared sconosciuto %s.
21 '%s' yields piu di una colonna.
22 Uso non valido o annidato delle funzioni di aggregazione.
23 Funzione %s non definita.
24 Il Contesto richiede una query con una colonna sola \n(%s).
25 Troppi valori specificati.
26 Virtual assignment target (%s) is not invertible.
27 Ci sono piu colonne nella query che Attributi nella classe %s.
28 Ci sono piu Attributi nella classe %s che colonne nella query.
29 La release corrente non permette le aggregazioni delle vclassees senza \nuna query specifica.
30 La release corrente non permette le aggregazioni alle vclasse senza \n specifare union nella query.
31 Uso della vclasse (%s) in una gerarchia composition richiede di essere updatable.\nNon può essere modificabile rispetto alla classe reale %s.
32 not used
33 Costrutore non permesso per path expression '%s'.
34 path expression '%s' crosses db boundary to %s, with vclasse %s.
35 path expression '%s' crosses db boundary from %s, to classe %s.
36 %s non è autorizzato su %s.
37 Select non autorizzata su %s.
38 Vclasse %s non è modificabile.
39 %s non è una proxy vclass.
40 query spec of proxy vclasse must not be a join.
41 %s non ha oggetti id set.
42 Classi (%s) con campi multipli object_id in una gerarchia composition \nnon sono supportate.
43 Attributo Object  %s di %s non è una  vclasse su ldb %s.
44 Tipo Query non compatibile con Attributo %s.
45 La specifica della query di %s ha un riferimento circolare.
46 Nessuna espressione da valutare.
47 Nessuna classe reale trovata per vclass %s.
48 Eseguendo '%s' su ldb '%s' \nritorna: %s.
49 Assegnato valore '%s' all'Attributo '%s' ritorna: %s.
50 Nome Ldb non può essere nullo in %s.
51 Nome Host Ldb non può essere nullo in %s.
53 Nome Host non può essere nullo in %s.
54 User name non può essere nullo in %s.
55 User password non può essere nullo in %s.
56 Insert translates to empty statement.
57 Update translates to empty statement.
58 Delete translates to empty statement.
59 INGRES non supporta date (%s) prima del 1 Gennaio 1582.
60 Problemi eseguendo la query.
61 Query yields nessun risultato , cosi variabile '%s' non asseganata.
62 Con LOCAL opzione di check non supportata.
63 Eccezione in Check option sulla view %s.
64 Data invalida: '%s'.
65 Ora invalida: '%s'.
66 Utime invalida: '%s'.
67 Subqueries in una query proxy non sono supportate.
68 Argomento di una funzione Ldb non è compatibile con ldb in '%s'.
69 Numero non valido: '%s'.
70 Ottimizzazione della Query Outer join fallita.
71         SERIALIZABLE.\n
72 Vclasse %s non è modificabile. Solo modifiche alla vclass sono permesse selezionando il VIDs.
73 Partizionamento fallito.
74 Partizionamento non esistente.
75 Acessso non consentito alla partizione: '%s'.
76 Il serial '%s' non è ancora definito.
77 Solo l'owner o DBA possono modificare la definizione del serial.
78 Modifica del serial creati auto increment non permesssa agli utenti.
79 Isolation level non valido : %s schema, %s istanze.

$set 11 MSGCAT_SET_AUTHORIZATION
1 *** cache non valida ***
2    classe %s
3 da %s :
4 Autorizzazione per utente %s\n
5 Autorizzazione non definita\n
6 User : %s\n
7            id : %d\n
8       members :
9        groups :
10 User %s\n
11 Utente attuale è : %s\n
12 Root authorization object :\n
13   utenti :
14 *** Non posso eseguire all grants per utente "%s"*** \n
15 AUTORIZZAZIONI\n
16 direct_groups :

$set 12 MSGCAT_SET_HELP
1 Rootclasse
2 %s %s\n
3 Super classi :\n
4 Sub classi :\n
5 Attributi : \n
6 Metodi: \n
7 Attributi classe: \n
8 Metodi classe : \n
9 Conflict resolutions:\n
10 metodo files:\n
11 Query specification:\n
12 OBJECT of %s\n
13 \nDescrizione:\n
14 \nStruttura:\n
15 \nEsempio:\n
16 META classe Name
17 Nome classe
18 Nome Vclass
$  Don't delete the space at the end of the next message
19 Vclass su ldb
20 GENERALE

$set 13 MSGCAT_SET_TRIGGER
1 TRACE: Condizione per trigger '%s'.
2 TRACE: Azione per trigger '%s'.

$set 14 MSGCAT_SET_LOCK
1  \n
2  Thread (index %3d) of transaction (index %3d, %s, %s@%s|%d) has been suspended.\n
3  Thread (index %3d) of transaction (index %3d, %s, %s@%s|%d) has been resumed.\n
4  Transaction index %3d ACQUIRED %s lock on object %d|%d|%d.\n
5  Transaction index %3d ACQUIRED %s lock nella pagina %d|%d.\n
6  Transaction index %3d CONVERTED its lock to %s lock on object %d|%d|%d.\n
7  Transaction index %3d CONVERTED its lock to %s lock nella pagina %d|%d.\n
8  Transaction index %3d WAIT FOR %s lock on object %d|%d|%d.\n
9  Transaction index %3d WAIT FOR %s lock nella pagina %d|%d.\n
10 Transaction index %3d RELEASE %s lock on object %d|%d|%d.\n
11 Transaction index %3d RELEASE %s lock nella pagina %d|%d.\n
12 Transaction index %3d DEMOTE lock to %s on object %d|%d|%d.\n
13 Transaction index %3d DEMOTE lock to %s nella pagina %d|%d.\n
14 OID = %2d|%6d|%4d\n
15 Object type: Root classe.\n
16 Object type: classe = %s.\n
17 Object type: Instance of classe (%2d|%6d|%4d) = %s.\n
18 Object type: Unknown.\n
19 Total mode of holders = %8s, Total mode of waiters = %8s.\n
20 Num holders=%3d, Num blocked-holders=%3d, Num waiters=%3d\n
21 LOCK HOLDERS:\n
22 BLOCKED LOCK HOLDERS:\n
23 LOCK WAITERS:\n
24 NON_2PL_RELEASED:\n
25 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d\n
26 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d, Nsubgranules = %2d\n
27 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d\n\t%13s Blocked_mode = %8s\n\t%17s Start_waiting_at = %-30s\n\t%17s Wait_for_nsecs = %d\n
28 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d, Nsubgranules = %2d\n\t%13s Blocked_mode =%8s\n\t%13s Start_waiting_at = %-30s\n\t%13s Wait_for_nsecs = %d\n
29 %4sTran_index = %3d, Blocked_mode =%8s \n\t%13s Start_waiting_at = %-30s \n\t%13s Wait_for_nsecs = %d\n
30 %4sTran_index = %3d, Non_2_phase_lock =%8s\n
31 VPID (volid|pageid) = %3d|%6d.\n
32 *** Lock Table Dump ***\n Lock Escalation at = %d, Run Deadlock interval = %d\n
33 Transaction (index %2d, %s, %s@%s|%d)\n
34 Isolation %s\n
35 State %s\n
36 Timeout_period %d\n
37 Unilaterally aborted transactions with indices:\n
38 %d ,
39 Timed_out transactions with indices:\n
40 %d ,
41 Solving the deadlock by selecting a transaction and a function \n to break the deadlock: %d %s
42 , %d %s

$set 15 MSGCAT_SET_IO
1 \n*************************************************************************\n
2 Backup = %s is needed. \n Deve essere mounted/caricato per continuare.\n Digita 1 per continuare dopo che il backup è mounted/caricato.\n Digita 0 per uscire.\n
3 \n\n*** BACKUP HEADER INFORMATION ***\n\n
4 Backup magic file identification %s,\n
5          Release: %s\n\
     Disk Version: %g\n
6    Database Name: %s\n\
 DB Creation Time: %s\
         Pagesize: %d\n
7     Backup Level: %d (%s)\n\
        Start_lsa: %d|%d\n\
         Last_lsa: %d|%d\n
8      Backup Time: %s\
  Backup Unit Num: %d\n
9  Database Volume name: %s\n\
   Volume Identifier: %d, Size: %ld bytes (%d pages)\n
10   Database Name: %s\n\
     Volume Name: %s\n\
        Unit Num: %d\n\
    Backup Level: %d (%s)\n\
Digita una delle seguenti opzioni:\n
11 Digita\n \
  -  %d per uscire.\n \
  -  %d per continuare dopo che il backup è mounted/caricato. (riprova) \n \
  -  %d per continuare dopo aver cambiato la directory o il device.\n
12 Digita la nuova directory o device per il volume di backup.\n
13 Errore in input, scegli un valore valido tra  %d e %d.\n
14 ** Attenzione ** Volume del Volume del Backup montato non corretto.\n
15 Volume del Backup %s è di un altro livello di backup\n   (Livello: %d, aspettato %d).\n\n
16 Volume del Backup %s ha dei problemi (incorrect page or magic identifier sbagliato).\n\n
17 Volume del Backup %s è di un altro database\n  (Nome: %s, aspettato %s).\n\n
18 Volume del Backup %s fuori sequenza\n  (Unit Num: %d, aspettato %d).\n\n
19 Volume del Backup %s problemi con la data\n  (Data: %s, aspettato %s).\n\n
20 Volume del Backup  : errore nelle information del file, linea %d.\n
21 Volume del Backup Label: Level: %d, Unit: %d, Database %s, Backup Time: %s\n
22     Backup level precedente: %d Time: %s (start_lsa era %d|%d)\n
23 Per continuare il restore necessita il prossimo volume del Backup:\n\n
24 La destinazione del Backup è piena, una nuova destinazione è richiesta per continuare:\n\n
25 Preceding Volume del Backup Name: %s\n
26     Next Volume del Backup Name: %s\n
27  Backup Pagesize: %d\n
28       Zip metodo: %d (%s)\n\
        Zip Level: %d (%s)\n
29 Include Active Log: %s\n

$set 16 MSGCAT_SET_LOG
1 \n*************************************************************************\n
2 Il Log Archive %s \n server per continuare la normale esecuzione.   \n \
  Digita\n \
  -  0 per finire.\n \
  -  1 per continuare senza l'archivio corrente. (Partial recovery)\n \
  -  2 per continuare dopo che l'archivio è mounted/caricato.       \n \
  -  3 per continuare dopo il cambio di locazione/nome dell'archivio.   \n
3 Informazioni File Backup %s \n sono necessarie per continuare il processo di restore. \n \
  Digita\n \
  -  0 per finire.\n \
  -  1 per continuare dopo che l'archivio è mounted/caricato.       \n \
  -  2 per continuare dopo il cambio di locazione/nome dell'archivio.   \n
4 Digita la nuova locazione/nome dell'archivio.\n
5 COMMENTO: %s per database %s\n
6 COMMENTO: Log archive %s non necessario fino ad un database media crash.\n
7 COMMENTO: Log archive da %s a %s non necessari fino ad un database media crash.\n
8 COMMENTO: da il database rinominato = %s\n
9 ARCHIVE: %d %s %d %d\n
10 ARCHIVE:
11 RIMUOVI: %d %s a\n \
       %d %s.\n \
       RAGIONE: %s\n
12 ATTIVO: %s %d pagine\n
13  Finish commit of tran_index %d with tranid = %d,\n just before LSA= %d|%d and at %s
14  Finish abort of tran_index %d with tranid = %d,\n just before LSA= %d|%d and at %s
15  Media Recovery non completa in atto. Il database sarà \n \
 recovered da LSA point address di %d|%d. L'Ultimo punto di commit/abort \n \
 è approssitivamente il %s \n \
 Il database ha perso tutte le informazioni delle transazioni salvate \n \
 dopo questa ora.\n
16 Reset del log per un recovery incompleto.
17 Backup del Database preso.
18 Media crash non è importante per l'applicazione.
19 Sto esegendo il Backup nella directory %s che sembra contenere un backup precedente.\n\
Volume del Backup obsoleto per questo database in questa locazione sarà rimosso,\n\
compreso quello del backup level corrente e maggiore.\n
20 Digita 'y' per continuare o qualsiasi altro tasto per cancellare l'operazione.\n
21 Backup del Database cancellata dall'utente.\n
22 DELETE POSTPONED: Archivi %d %s a \n \
        %d %s \n \
        non servono più fino ad un restore del backup corrente.\n
23 Un backup più nuovo ha rilasciato gli archivi precedentemente marcati come DELETE POSTPONED.\n
24 COMMENTO: Log archive %s non è DELETE POSTPONED fino al più recente\n \
   backup e quindi non sono necessari fino ad un database media crash.\n
25 COMMENTO: Log archives da %s a %s non sono più DELETE POSTPONED per \n \
   il backup recente e quindi non sono necessari fino ad un database media\n \
   crash.\n
26 Errore durante il restore del database, mentre processavo\n\
     Volume del Backup: %s\n\
          Unit Num: %d\n\
   Database Volume: %s\n\
    Database Volid: %d\n\
** Attenzione: Non usare il database fino a che un restore senza problemi è avvenuto.\n
27 Errore in input, scegli un valore tra %d e %d.\n
28 L'ora(%s) specificata deve essere dopo l'ora (%s) del backup specificato.\n
29 COMMENTO: Log archive %s, che contiente le log pages prima %d, non è più necessario per nessuna utilità HA.\n
30 Numero di archives log attivi eccedono il numero massimo desiderato.
