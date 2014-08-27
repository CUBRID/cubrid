
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
3 Informazioni sul copyright\n
5 Valore non intera `%1$s' per l'argomento
6 Valore non flottante `%1$s' per l'argomento
7 Argomento assente `%1$s'
8 Argomento duplicato `%1$s'
9 Valore mancante per l'argomento `%1$s'
10 Argomento sconosciuto `%1$s'
11 Aargomento posizionale Inaspettato `%1$s'
12 Nessuna definizione degli argomenti
13 Impossibile allocare memoria per il parsing dell'argomento
14 Stringa di argomento supera la lunghezza massima: %1$s.
15 Numero non valido di messaggio %1$d all'interno del catalogo per il parsing del l'argomento
16 [opzioni]
17 Opzioni:

$set 4 MSGCAT_SET_PARAMETER
2 %1$s non è una directory. Creazione di %2$s in ".".
3 Impossibile aprire %1$s. File di configurazione non creato.
4 Impossibile scrivere la linea %1$d alla file di configurazione %2$s
6 Impossibile accedere al file di configurazione di sistema "%1$s": %2$s
7 HOME non impostato
8 Parametro obbligatorio "%1$s" non ha alcun valore.
9 Impossibile aprire il file di configurazione "%1$s": %2$s
10 Il parametro '%1$s': alla linea %2$d in '%3$s': file
11 Variabile di ambiente %1$s:
12 Parola chiave non riconosciuta.
13 Tipo di valore non corrisponde al tipo di parametro.
14 Impossibile allocare memoria per immagazzinare il valore.
15 Valore della stringa non terminata.
16 Il valore è fuori intervallo.
17 Errore UNIX: %1$s
18 Nessuna informazione disponibile.
19 Il valore è fuori intervallo. Reimpostazione al valore di default.
20 Parola riservate = '%1$s':    Valore = %2$d    Valore default = %3$d\n
21 Parola riservate = '%1$s':    Valore = %2$f    Valore default = %3$f\n
22 Parametro obsoleto
23 I parametri '%1$s' e '%2$s' non possono essere utilizzati insieme.\n
31 Il parametro '%1$s':

$set 5 MSGCAT_SET_ERROR
1 Messaggio mancante per il codice di errore %1$d.
2 Guasto interno del sistema: nessuna informazione più specifiche è disponibile.
3 Memoria virtuale esaurita: Impossibile allocare %1$lld byte di memoria.
4 è stato interrotto.
5 Chiave = "%1$s" non è stato trovato in tabella hash "%2$s".
6 Una tabella di memoria hash è stato fornito a una funzione della tabella hash.
7 Cercando di formattare il volume del disco "%1$s" con una %2$d valore non corretto per il numero di pagine.
8 Impossibile formattare il volume del disco "%1$s" con %2$d pagine (%3$lld byte).
9 Impossibile formattare il volume del disco "%1$s" con %2$d pagine (%3$lld Kbytes) a causa di spazio insufficiente. Lo spazio attuale disponibile è %4$d pagine (%5$lld Kbytes).
10 Impossibile montare il volume del disco "%1$s".
11 Impossibile montare il volume del disco "%1$s". La database "%2$s", a cui appartiene il volume del disco, è in uso da utente %3$s su processo %4$d dell'host %5$s dal %6$s.
12 Problemi smontaggio del volume "%1$s".
13 Un errore I/O durante la lettura della pagina %1$d del volume "%2$s".
14 Un errore I/O durante la scrittura della pagina %1$d del volume "%2$s".
15 Impossibile scrivere pagina %1$d del volume "%2$s" a causa di spazio insufficiente nel dispositivo del sistema operativo.
16 Impossibile rinominare il volume del disco "%1$s" a "%2$s".
17 Errore interno: il recupero di pageid deallocato %1$d del volume "%2$s".
18 Tutti i bufferi di pagina sono fissi (LRU: %1$d).
19 Errore interno: pageptr = %1$p da pagina %2$d del volume "%3$s" non è fisso.
20 Errore interno: pageptr = %1$p sconosciuto.
21 Errore interno: settore sconosciuto %1$d del volume "%2$s"
22 Errore interno: pagina sconosciuta %1$d del volume "%2$s"
23 Errore interno: tentativo di deallocare di pagina %1$d di sistema del disco del volume "%2$s"
24 Pressoché esaurito lo spazio in volume "%1$s" Numero totale di pagine = %2$d, il numero totale di pagine libere = %3$d.
25 Pressoché esaurito lo spazio in volumi con dati come lo scopo principale di stoccaggio. Numero totale di pagine = %1$d, il numero totale di pagine libere = %2$d.
26 Pressoché esaurito lo spazio in volumi con l'index come lo scopo principale di stoccaggio. Numero totale di pagine = %1$d, il numero totale di pagine libere = %2$d.
27 Pressoché esaurito lo spazio in volumi con scopo di stoccaggio generico. Numero totale di pagine = %1$d, il numero totale di pagine libere = %2$d.
28 Pressoché esaurito lo spazio dei volumi con dati temporanei come lo scopo di stoccaggio principale. Numero totale di pagine = %1$d, il numero totale di pagine libere = %2$d.
29 All'ultima notificazione dell'esaurimento dello spazio per volume "%1$s". Numero totale di pagine = %2$d, il numero totale di pagine libere = %3$d.
30 All'ultima notificazione dell'esaurimento dello spazio per i volumi con dati come lo scopo di stoccaggio principale. Numero totale di pagine = %1$d, il numero totale di pagine libere = %2$d.
31 All'ultima notificazione dell'esaurimento dello spazio per i volumi con index come lo scopo principale di stoccaggio. Numero totale di pagine = %1$d, il numero totale di pagine libere = %2$d.
32 All'ultima notificazione dell'esaurimento dello spazio per i volumi con scopo di stoccaggio generico. Numero totale di pagine = %1$d, il numero totale di pagine libere = %2$d.
33 All'ultima notificazione dell'esaurimento dello spazio per i volumi con i dati provvisori come lo scopo di stoccaggio principale. Numero totale di pagine = %1$d, il numero totale di pagine libere = %2$d.
34 Errore interno: %1$d nthpage è fuori intervallo per il file %2$d in volume "%3$s". Numero attuale di pagine è %4$d.
35 Errore interno: identificativo del volume sconosciuto %1$d.
36 Spazio su disco esaurito nel database. Numero di pagine richieste = %1$d.
37 Spazio su disco esaurito in volume "%1$s". Numero di pagine richieste = %2$d.
38 Errore interno: file sconosciuto VFID %1$d|%2$d.
39 Errore interno: Pagina %1$d|%2$d (volume "%3$s") del file VFID %4$d|%5$d (volume "%6$s") non è valido/è deallocato in base alla mappa di allocazione.
40 Errore interno: %1$d pagine attesi, %2$d pagine trovate per il file VFID %3$d|%4$d (volume "%5$s").
41 Errore interno: Pagina %1$d|%2$d ("%3$s") volume non fa parte del file VFID %4$d|%5$d (volume "%6$s").
42 Non è possibile riordinare le registrazioni ancorate alla pagina %1$d del volume "%2$s".
43 Errore interno: la pagina %1$d del volume "%2$s" potrebbe essere corrotto. %3$d registrazioni sono stato trovati quando %4$d si aspettava.
44 Nessuno spazio disponibile nella slotted page %1$d del volume "%2$s".
45 Slot %1$d alla pagina %2$d del volume "%3$s" è assegnato a una voce ancorata. Una nuova voce non può essere inserito qui.
46 Errore interno: slot %1$d alla pagina %2$d del volume "%3$s" non è allocato.
47 Impossibile creare un file di heap in volume "%1$s".
48 Accesso all'oggetto eliminato %1$d|%2$d|%3$d.
49 Errore interno: classe dell'oggetto %1$d|%2$d|%3$d è sconosciuto.
50 Errore interno: record di trasferimento dell'oggetto %1$d|%2$d|%3$d potrebbe essere corrotto.
51 Errore interno: oggetto %1$d|%2$d|%3$d potrebbe essere corrotto.
52 Errore interno: indirizzo dell'oggetto di overflow %1$d|%2$d|%3$d potrebbe essere corrotto.
53 Recupero dell'oggetto %1$d|%2$d|%3$d quando solo il suo OID è stato assegnato.
54 Errore interno: cercando di manipolare un oggetto del %1$d pagine. La dimensione massima permessa per un oggetto è %2$d pagine.
55 Errore interno: Un ciclo di riferimento dei pagini rilevato nella pagina %1$d|%2$d di file di heap %3$d|%4$d|%5$d.
56 Errore interno: file sconosciuto del hashing estendibile (VOLID: %1$d fileid: %2$d) pagina (VOLID: %3$d pageid: %4$d) è stato specificato.
57 Chiave non esiste nella struttura di hashing estendibile.
58 Stringa "%1$s" è troppo lunga per rientrare in una slotted page.
59 Errore interno: il tipo di chiave specificato %1$d non è valida per la struttura di hashing estendibile.
60 Errore interno: la struttura di hashing estendibile è stata corrotta.
61 Errore interno: la pagina di directory radice della struttura di hashing estendibile (VOLID: %1$d fileid: %2$d pageid: %3$d) è stata corrotta.
62 Errore interno: record di ordinamento %1$d (dimensione %2$d) non rientra in una pagina, la dimensione massima permessa è di %3$d.
63 Errore interno: una pagina temporanea e stata corotta durante l'ordinamento.
64 Classe "%1$s" sconosciuta.
65 Classe "%1$s" esiste già.
66 Errore interno: operazione forzata sconosciuta %1$d per oggetto %2$d|%3$d|%4$d.
67 Errore interno: un file di heap non è stata allocata per stoccaggio dell'oggetto %1$d|%2$d|%3$d.
68 Errore interno: nomi di classi differenti per classe con oid = %1$d|%2$d|%3$d sono stati trovati. Le nomi di classi trovati sono "%4$s", "%5$s" utilizzando l'heap e la tabella di hash per lei nomi di classi, rispettivamente.
69 Errore interno: identificatori differenti di oggetti di classe sono stati trovati per la classe con il nome "%1$s". Le OID trovati sono %2$d|%3$d|%4$d, %5$d|%6$d|%7$d utilizzando l'heap e la tabella hash per lei nomi di classi, rispettivamente.
70 Errore interno: Classe con il nome "%1$s" e oid = %2$d|%3$d|%4$d non esiste nella della tabella hash di nome di classe.
71 Errore interno: Classe con il nome "%1$s" e oid = %2$d|%3$d|%4$d non esiste nel suo heap.
72 Vostra transazione (indice %1$d, %2$s @ %3$s|%4$d) è stato interrotto unilateralmente da parte del sistema.
73 Vostra transazione (indice %1$d, %2$s @ %3$s|%4$d) scaduto in attesa sul blocco %5$s sull'oggetto %6$d|%7$d|%8$d. State aspettando per l'utente (s) %9$s per terminare.
74 Vostra transazione (indice %1$d, %2$s @ %3$s|%4$d) scaduto in attesa sul blocco %5$s sulla classe %6$s. State aspettando per l'utente (s) %7$s per terminare.
75 Vostra transazione (indice %1$d, %2$s @ %3$s|%4$d) scaduto in attesa sul blocco %5$s nell'istanza %6$d|%7$d|%8$d di classe %9$s. State aspettando per l'utente (s) %10$s per terminare.
76 Vostra transazione (indice %1$d, %2$s @ %3$s|%4$d) scaduto in attesa sulla %5$s alla pagina %6$d|%7$d. State aspettando per l'utente (s) %8$s per rilasciare il blocco della pagina.
77 Errore interno: tutti i buffer sono fissi.
78 Errore interno: un errore I/O durante la lettura della pagina logica di log %1$lld (pagina fisica %2$lld) del "%3$s".
79 Errore interno: un errore I/O durante la scrittura pagina logica di log %1$lld (pagina fisica %2$lld) del "%3$s".
80 Spazio insufficiente nel dispositivo del sistema operativo durante la scrittura di pagina logica di log %1$lld (pagina fisica %2$lld) del "%3$s". Impossibile scrivere più di %4$d bytes.
81 Errore interno: Pagina di log logico %1$lld potrebbe essere corrotto.
82 Impossibile montare il volume del disco di log/file "%1$s".
83 Lunghezza del path "%1$s" più la lunghezza del nome del log del prefisso "%2$s" è troppo lungo, la lunghezza cumulata deve essere inferiore a %3$d.
84 Lunghezza del nome del log del prefisso "%1$s" è troppo lungo, la lunghezza deve essere inferiore a %2$d.
85 Il nome del prefisso "%1$s" non è lo stesso di "%2$s" sul disco di log. Il log è stato probabilmente rinominato all'esterno del dominio del database.
86 Database è incompatibile con il corrente "%1$s", rilascio "%2$s".
87 Ci sono azioni di recupero che devono essere recuperati utilizzando "%1$s" rilascio "%2$s" invece di rilascio "%3$s". Dopo il recupero, il database può essere eseguito sul rilascio "%4$s".
88 Errore interno: La "%1$s" stringa di rilascio non può eccedere %2$d. Deve cambiare log del colpo di testa
89 Log "%1$s" non appartiene al database indicato.
90 Redo di logging è sempre una operazione di logging a livello di pagina. Un puntatore a una pagina di dati deve essere dato come parte dell'indirizzo.
91 Postpone logging è sempre un'operazione di logging a livello di pagina. Un puntatore a una pagina di dati deve essere dato come parte dell'indirizzo.
92 Compensate logging è sempre una operazione di logging a livello di pagina. Un puntatore a una pagina di dati deve essere dato come parte dell'indirizzo.
93 Attenzione: logging redo viene ignorato durante il recupero redo e rollback normali.
94 Attenzione: postpone logging viene ignorato durante il recupero redo e rollback normali.
95 Attenzione: undo logging viene ignorato durante il recupero e rollback normali.
96 Ripristino dei supporti può essere necessario sul volume "%1$s"
97 Errore interno: impossibile trovare la pagina di log %1$lld nel log delle transazioni.
98 Impossibile creare il log delle transazioni "%1$s" alle pagine di archivio %2$lld-%3$lld.
99 Impossibile creare il file di backup per le informazioni per directory "%1$s"
100 Impossibile eseguire il backup del database "%1$s".
101 File sconosciuto "%1$s" dell'utente.
102 File "%1$s" non dispone di voci abbastanza: %2$d voci previsto.
103 "%1$s" file sembra avere le voci non ordinate. Entrata %2$d con valori: %3$d %4$s %5$s, è stato trovato quando la voce con i valori %6$d %7$s era atteso.
104 File "%1$s" sembra avere una voce non corretta per il nome del database (volume principale). Entrata %2$d con valori: %3$d %4$s %5$s, è stato trovato quando la voce con i valori %6$d %7$s %8$s era atteso.
105 Impossibile accedere al file di backup "%1$s". Restore o backup viene annullato.
106 Impossibile preparare di commit della transazione corrente con identificatore di transazione globale, %1$d perché questo identificatore di transazione è utilizzato da un'altra transazione.
107 Non c'è alcuna transazione globale distribuita con identificatore di transazione %1$d.
108 Transazione corrente %1$d deve essere comessa o interrotta prima di tentare di partecipare alla transazione distribuita con identificatore globale %2$d.
109 Vostra transazione %1$d è stato unilateralmente interrotto dal sistema perché una (remota) sito partecipante è andato giù o unilateralmente interrotto la transazione.
110 Valore del livello di isolamento deve essere compreso tra %1$d e %2$d.
111 Vostra transazione è stata interrotta dal sistema a causa di un errore del server o di di cambiamento di modo.
112 host "%1$s" non è autorizzato per l'esecuzione del database.
113 Impossibile riavviare/inizializzare il server di database. %1$s
114 Impossibile interpretare "%1$s" come un database. Le volumi di database/file potrebbero essere stati rinominati/copiati all'esterno del dominio del database.
115 Database "%1$s" esiste già.
116 Database "%1$s" è sconosciuto, o la "databases.txt" file non è accessibile.
117 Percorso assoluto per il database è troppo lungo. La lunghezza cumulata del path "%1$s" più il "%2$s" nome è %3$d, la lunghezza combinata deve essere inferiore a %4$d.
118 Impossibile localizzare directory di lavoro corrente.
119 Impossibile trovare nome dell'host locale.
120 Il numero massimo di volumi (%1$d) è stato superato.
121 Cercando di rimuovere un volume "%1$s" permanente dal database.
122 Impossibile accedere al catalogo di messaggi di sistema.
123 Impossibile creare %1$s per il database "%2$s". Si prega di fare riferimento a "%3$s" per ulteriori informazioni.
124 Volume "%1$s" esiste già.
125 Una estensione automatica di volume "%1$s" di %2$d pagine e scopo di stoccaggio generico è stato creato.
126 scopo sconosciuto "%1$s" dato sulla linea %2$d.
127 Valore non corretto per il numero di %1$d pagine indicate sulla linea %2$d.
128 Numero di pagine non è stata data sulla linea %1$d.
129 Token sconosciuto "%1$s" è stato trovato sulla linea %2$d.
130 Memoria virtuale esaurita.
131 Errore di sistema per il database.
132 Funzionalità non implementata nella versione corrente: %1$s.
133 Set annidato costante non permessa.
134 Non è permesso di usare entrambi gli attributi di classe e gli attributi di non-classe in un unico aggiornamento.
135 Istruzione non valida per le colonne di oid: %1$s.
136 Funzione non implementata "%1$s".
137 Oggetti di autorizzazione corrotti.
138 Classe di autorizzazione "%1$s" non trovata.
139 Errore di accesso sull'attributo di autorizzazione "%2$s" di classe "%1$s".
140 Operazione "%1$s" può essere eseguita solo dal DBA o un membro del gruppo DBA.
141 Impossibile aggiungere "%1$s" come membro del "%2$s".
142 Aggiungendo membro provoca la gerarchia degli utenti a diventare un grafico ciclico.
143 Rilevato una classe senza proprietario.
144 Impossibile accedere all'oggetto utente.
145 Non è possibile eseguire i comandi GRANT/REVOKE a te stesso.
146 Non è possibile eseguire i comandi GRANT/REVOKE al proprietario di una classe.
147 Nessuna opzione GRANT.
148 Impossibile ottenere blocco in scrittura sull'oggetto di autorizzazione.
149 Impossibile creare l'istanza dell'oggetto di autorizzazione.
150 Non è possibile revocare i privilegi da te stesso.
151 Non è possibile revocare i privilegi del proprietario di una classe.
152 GRANT non trovato.
153 Non ci sono dei privilegi di autorizzazione in vigore per il database.
154 Autorizzazione incompleta dell'installazione - senza privilegi di autorizzazione in vigore per il database.
155 Il contenuto della classe di autorizzazione non è valido (conti di root multipli).
156 Guasto di autorizzazione
157 Errore di autorizzazione di SELECT.
158 Errore di autorizzazione dell'ALTER.
159 Errore di autorizzazione dell'UPDATE.
160 Errore di autorizzazione del INSERT.
161 Errore di autorizzazione di DELETE.
162 Errore di autorizzazione del INDEX.
163 Errore di autorizzazione di EXECUTE.
164 Utente "%1$s" esiste già.
165 Utente "%1$s" non è valido.
166 Utente specificato non valido.
167 DBA, i membri del gruppo DBA e proprietario di classe possono eseguire l'operazione.
168 Utente non trovato.
169 Non è possibile rimuovere l'utente %1$s dal database.
170 Nessun utente loggato.
171 Password errata o mancante.
172 Stringa di password non può avere più di 31 byte.
173 Impossibile individuare il file di database "%1$s".
174 Impossibile ottenere l'accesso in scrittura al file di database "%1$s".
175 File di database malamente formattato. "%1$s"
176 Errore di conversione nel formato di data.
177 Impossibile creare l'oggetto di grandi dimensioni per stoccaggio interno.
178 Temporaneo OID incontrato senza MOP.
179 Dominio "%1$s" non è compatibile con dominio "%2$s".
180 Dominio "%1$s" di valore fornito non è compatibile con il dominio di destinazione "%2$s".
181 Non è possibile convertire il valore del dominio "%1$s" al dominio "%2$s".
182 Non è possibile convertire il valore del dominio "%1$s" al dominio "%2$s" senza overflow.
183 Quantità inattesa di dati ricevuti, %1$d previsto, %2$d ricevuto.
184 Impossibile allocare il buffer di comunicazione.
185 Errore di ricezione dei dati dal client.
186 Errore di ricezione dei dati dal server.
187 Buffer di comunicazione non utilizzato.
188 Database "%1$s" sconosciuto.
189 Nome non valido per l'host "%1$s".
190 Host del server non identificato.
191 Impossibile connettersi al server "%1$s" "%2$s".
192 Impossibile caricare il file di configurazione della rete.
193 Server ha ricevuto il comando di arresto dal client.
194 Server di richiesta sconosciuto id %1$d.
195 Errore di comunicazione del server: %1$s.
196 Nome del server non identificato.
197 Impossibile entrare in contatto con il server principale.
198 Buffer di ricezione troppo piccole, i dati troncati.
199 Server non risponde.
200 Non rimuovendo correttamente il set dall'oggetto dopo libero.
201 Rimosso una voce UNIQUE non corretta nella tabella.
202 Attributo "%1$s" non è stato trovato.
203 Valore per l'attributo "%1$s" deve essere di tipo "%2$s", non di tipo "%3$s".
204 Funzione chiamata con argomenti mancanti o non validi.
205 Attributo "%1$s" non può essere fatto NULL.
206 Attributo "%1$s" non ha il vincolo UNIQUE.
207 Classe "%1$s" ha un oggetto di dimensione zero.
208 Il metodo "%1$s" non trovato.
209 Nessun attributo o metodo con il nome "%1$s" è stato trovato.
210 Errore interno: elaborazione del modello di oggetto.
211 Attributo è stato eliminato durante la costruzione di modello di oggetto.
212 Attributo "%1$s" viola il vincolo UNIQUE.
213 Voce UNIQUE non valido di tabella è stato incontrato.
214 Un conflitto di dominio esiste sull'attributo "%1$s".
215 Attributo con ID %1$d non trovato.
216 Tag non valido di proprietà dell'oggetto trovato sul valore.
217 L'operazione può essere eseguita solo su oggetti di classe.
218 Oggetto non valido nell'espressione del percorso db_get.
219 Espressione di percorso non valida db_get.
220 Insieme non valida nell'espressione del percorso db_get.
221 Indice non valido dell'insieme nell'espressione del percorso db_get.
222 Tentativo di assegnare il valore stringa maggiore di %2$d caratteri per attributo "%1$s".
223 Valore %2$d è troppo grande per attributo smallint "%1$s".
224 Una database non è stata riavviata.
225 Valore mancante per attributo "%1$s" con il vincolo NOT NULL.
226 Lista dei componenti vuota nella classe.
227 Tentativo di assegnare il valore per attributo "%1$s" più di una volta.
228 Metodo "%1$s" chiamato con %2$d argomenti. Il sistema permette un massimo di %3$d argomenti.
229 Metodo "%1$s" richiede argomento %2$d a trovarsi nel dominio "%3$s". Un valore non valido di dominio "%4$s" è stata data.
230 Attributo "%1$s" non può essere attribuito una stringa di lunghezza %2$d. La lunghezza massima permessa per la stringa è %3$d.
231 Operazione non valida dell'API tentata su un oggetto temporaneo.
232 Non è permesso utilizzare i modelli di oggetti sulle transazioni multiple.
233 Impossibile creare una classe con nome di tipo di base "%1$s".
234 File di metodo "%1$s" non è stato trovato.
235 %1$d metodo funziona irrisolti dopo il linking.
236 Metodo "%1$s" irrisolto.
237 Gravi problemi sono stati riscontrati durante il linking dinamico.
238 Impossibile accedere al file di metodi "%1$s"
239 Attributo "%1$s" non è stato trovato.
240 Metodo "%1$s" non è stato trovato.
241 Non esiste un attributo o un metodo con il nome "%1$s"
242 Signature per %2$s non trovata per metodo "%1$s"
243 Argomento %2$d di metodo "%1$s" non è definito.
244 Il nome di dominio "%1$s" non è il nome di un tipo di classe o di base.
245 Il nome "%1$s" è già stato utilizzato come un nome di attributo.
246 Il nome "%1$s" è già stato utilizzato come un nome di metodo.
247 Argomenti non validi alla funzione interna del gestore dello schema.
248 Il dominio "%1$s" dell'attributo non è adatto per l'uso con il vincolo %2$s.
249 L'operazione richiesta sulla classe "%1$s" può essere eseguita solo prima le istanze vengono create.
250 Impossibile creare l'index sull'attributo "%1$s", definito con un valore condiviso.
251 Signature %2$s già esiste per metodo "%1$s"
252 Attributo "%1$s" deve avere un dominio dell'insieme per tale operazione.
253 Attributo "%1$s" non può essere definito con un dominio dell'insieme annidato.
254 Impossibile rimuovere il dominio "%2$s" dall'attributo "%1$s".
255 Attributo "%1$s" non è di un dominio di dimensioni variabili.
256 Superclasse esiste già.
257 Addizione della superclasse causerebbe cicli nella gerarchia di classi.
258 Superclasse non trovato.
259 Signature multipli esistono per metodo "%1$s"
260 Argomento %2$d del metodo "%1$s" non è di un dominio dell'insieme.
261 Risoluzione per "%1$s" non trovato.
262 Dominio "%2$s" non è adatto per attributo "%1$s".
263 Corruzione interna dello schema rilevata nel gestore.
264 Alias "%2$s"  per l'attributo/metodo "%1$s" non è unico
265 Oscurare attributo "%1$s" con dominio conflittualo "%2$s".
266 Conflitto di nome sull'attributo "%1$s" con le classi "%2$s" e "%3$s".
267 Oscurare metodo "%1$s" con signature conflittuale.
268 Conflitto di nome sul metodo "%1$s" con le classi "%2$s" e "%3$s".
269 Impossibile definire index sul dominio "%1$s".
270 Impossibile definire vincolo UNIQUE sul dominio "%1$s".
271 Ciclo rilevato nella gerarchia di classi, "%1$s" è una superclasse di "%2$s".
272 Index "%2$s" già definito per la classe "%1$s"
273 Index "%1$s" non esiste.
274 Caratteri non validi nel nome: "%1$s".
275 Operazione non valida su attributo "%1$s", ereditato dalla classe "%2$s".
276 Operazione non valida su metodo "%1$s", ereditato dalla classe "%2$s".
277 Operazione non valida su "%1$s", ereditato dalla classe "%2$s".
278 Classe "%4$s" ha domini incompatibili per "%1$s", ereditati dalle classi "%2$s" e "%3$s".
279 Nella classe "%4$s", "%1$s" ereditato dalla classe "%2$s" non può essere utilizzato come richiesto. La definizione di classe "%3$s" ha un dominio più specifico.
280 Nella classe "%3$s", "%1$s" non può essere ereditato dalla classe "%2$s" perché existe una definizione locale con un dominio incompatibile.
281 Nella classe "%3$s", "%1$s" ereditato dalla classe "%2$s" ha un alias, ma alcun sostituto da un'altra origine è stato trovato.
282 Nella classe "%4$s", "%1$s" ereditato dalla classe "%3$s" non può servire come un sostituto di alias per classe "%2$s". I domini sono incompatibili.
283 Nella classe "%4$s", "%1$s" ereditato dalla classe "%3$s" non può servire come un sostituto di alias per classe "%2$s". Ha un dominio meno specifico.
284 Non è possibile richiedere l'eredità del "%1$s" perché è definito a livello locale nella classe.
285 Non è possibile utilizzare "%1$s" come alias per il componente ereditaria "%2$s", perché c'è già un componente definita a livello locale con lo stesso nome.
286 Non è possibile utilizzare "%1$s" come alias per "%2$s" del "%3$s". Un componente con quel nome è già ereditato dal "%4$s".
287 Nella classe "%1$s", non è possibile definire l'attributo "%2$s" perché sarebbe oscurato da un metodo ereditato dal "%3$s".
288 Nella classe "%1$s", non è possibile definire il metodo "%2$s" perché sarebbe oscurato da un attributo ereditato dal "%3$s".
289 Sottoclasse "%1$s" non può ereditare il metodo "%2$s" dalla classe "%3$s" perché existe già un attributo con lo stesso nome.
290 Sottoclasse "%1$s" non può ereditare il attributo "%2$s" della classe "%3$s" perché existe già un metodo con lo stesso nome.
291 "%1$s" è definito nella classe "%2$s" come attributo e nella classe "%3$s" come metodo.
292 Nessuna specificazione per popolare con index "%1$s".
293 Operazione non permessa per questo tipo di classe.
294 Variabile di ambiente non definita "%1$s" viene fatto riferimento nel file di metodo
295 Impossibile modificare la classe "%1$s". Lo spazio è esaurito all'interno del catalogo di sistema. Per recuperare spazio, compattare il database con l'utilità compactdb.
296 Lista di proprietà non valido incontrata.
297 Nome dell'alias "%1$s" viene utilizzato più di una volta.
298 Nella classe "%1$s", una risoluzione per "%2$s" del "%3$s"' non è permessa. Questa classe non è una superclasse.
299 "%1$s" è una parola riservata e non può essere utilizzata come attributo, metodo, o nome di una classe.
300 Il attributo "%1$s" non può essere definito con DEFAULT e vincoli UNIQUE insieme.
301 Il attributo "%1$s" non può essere definito con un vincolo di lunghezza di %2$d. Il vincolo di lunghezza massima è di %3$d.
302 Impossibile aggiungere elementi a un insieme.
303 Valore già esiste nell'insieme.
304 Index dell'insieme %1$d è fuori dai limiti.
305 Index di sequenza %1$d è fuori dei limiti.
306 L'operazione richiesta può essere eseguita solo su sequenze.
307 L'operazione richiesta può essere eseguita solo su collezioni.
308 Valore dell'elemento non è all'interno del dominio dell'insieme.
309 Indice dell'elemento illegale dell'insieme è stato dato: %1$d.
310 Elemento non trovato nell'insieme.
311 Elemento non trovato nella sequenza.
312 "%1$s" non è un dominio valido per l'insieme.
313 underflow del buffer dell'oggetto durante la lettura.
314 overflow del buffer dell'oggetto durante la scrittura.
315 Definizione di metaclasse illegale incontrato.
316 Discordanza di dimensione di calcolo del trasformatore, previsto %1$d calcolato %2$d.
317 Rappresentazione non valida del disco incontrato per classe "%1$s".
318 Non sincronizzato durante il caricamento dell'oggetto. Il database è probabilmente danneggiato o non aggiornate.
319 Ignorando estensione sconosciuta dell'attributo.
320 Incontrato rappresentazione danneggiata di oggetto sul disco.
321 Possibile corruzione nello spazio di lavoro è stata rilevata.
322 MOP non trovato nello spazio di lavoro.
323 Tentativo di aggiornare un MOP non temporaneo nello spazio di lavoro.
324 Oggetto senza classe è stato trovato nello spazio di lavoro.
325 Tentativo di garbage collect uno MOP nella lista dirty
326 Cambiare puntatore alla classe per oggetto nello spazio di lavoro.
327 Impossibile creare MOP con NULL OID.
328 Istanza senza classe incontrata nel spazio di lavoro.
329 Link per elenco di oggetti non allocato nello spazio di lavoro.
330 Violazione di blocco per slot di memoria nello spazio di lavoro.
331 Impossibile allocare spazio di lavoro blocco di dal sistema operativo, saturato la memoria virtuale. Interruzione dell'operazione.
332 Le dimensioni originali stimati dello spazio di lavoro sono stati superati. Un'estensione automatica è stato fatto.
333 Memoria esaurita durante l'allocazione di memoria di lavoro. Interruzione dell'operazione.
334 Tentativo di assegnazione del %1$d bytes è più grande di dimensione massima blocco di di lavoro. Provate a regolare il parametro max_block_size. Transazione interrotta.
335 Ignorando tentativo di liberare un oggetto non allocato all'interno dell'area di lavoro.
336 Ignorando tentativo di liberare un oggetto più di una volta.
337 Ignorando tentativo di allocare oggetto con dimensioni negative.
338 Sovraccarico dell'identificatore dell'entrata nella mappa.
339 Impossibile uscire dalla sezione critica, dal momento che non è di proprietà di transazione %1$d.
340 fork del processo figlio non è possibile.
341 exec del processo figlio non è possibile.
342 Non è possibile cambiare il gruppo di processo.
343 Request_id è già in uso.
344 %1$s.
345 Errore durante la lettura di un messaggio di arresto da master.
346 Errore durante la lettura di un messaggio di interruzione dell'arresto da master.
347 Impossibile stabilire una pipe per il server master.
348 è necessario specificare un servizio o un identificatore di porta.
349 Servizio sconosciuto per TCP: %1$s.
350 Errore durante il recupero dell'host locale per nome per host "%1$s". Host non trovato.
351 Impossibile creare un socket TCP.
352 tcp_open: non è possibile ottenere una porta TCP riservata.
353 Impossibile stabilire una connessione al server master sul host "%1$s".
354 Impossibile impostare il nuovo proprietario del processo per il socket.
355 Impossibile creare il flusso di socket.
356 File '%1$s' per UNIX domain socket esiste.
357 Impossibile associare l'indirizzo locale ... interrompere.
358 Accept non è riuscito.
359 Bbind non è riuscito per datagramma.
360 Accept non è riuscito per datagramma.
361 Connect non è riuscito per datagramma.
362 Socket non è riuscito per datagramma.
363 Recvmsg dal socket di master non riuscito.
364 Invio di una nuova richiesta al server.
365 Invio di un messaggio broadcast al client.
366 Errore dal server: %1$s
367 Server %1$s esiste già.
368 Errore di comunicazione durante la connessione per %1$s.
369 Codice di errore non utilizzato.
370 Codice di errore non utilizzato.
371 Codice di errore non utilizzato.
372 Codice di errore non utilizzato.
373 EOF prematuro durante la lettura: %1$d.
374 EOF prematuro durante la lettura del %1$d o %2$d.
375 Errore di conversione a (0x%1$c%2$c) %3$d e %4$d convertiti in %5$d.
376 Nessuna classe pubblica trovata.
377 Errore %1$d: Impossibile aggiungere l'attributo "%2$s" alla gerarchia multimedia.
378 Errore %1$d: Impossibile aggiungere il metodo "%2$s" implementato come "%3$s" alla gerarchia multimedia.
379 Errore %1$d: impossibile aggiungere una superclasse alla gerarchia multimedia.
380 Caricatore dinamico già inizializzato.
381 Caricatore dinamico non inizializzato.
382 Caricatore dinamico inizializzato impropriamente.
383 "%1$s" non è un file oggetto o un archivio.
384 Impossibile determinare il percorso assoluto per "%1$s".
385 processo ld terminato con il codice %1$d.
386 processo ld terminato a causa di segnale %1$d.
387 processo ld terminato con waitval sconosciuto 0x%1$x.
388 Impossibile accedere immagine iniziale per "%1$s".
389 Errore di sistema: %1$s.
390 Problemi con "%1$s": %2$s.
391 Conflitto con altro handler SIGPIPE.
392 Impossibile generare demone del caricamento dinamico.
393 Demone del caricatore dinamico è scomparso.
394 Punto finale dell'intervallo non compreso tra 0 e 256.
395 Sottoespressione illegale specificato.
396 Cifra ottale fuori dei limiti esiste nell'escape.
397 Espressione regolare non delimitato in modo corretto.
398 Ricerca di testo chiamato prima che l'espressione è stato compilato.
399 Una parentesi senza pari si trova nell'espressione regolare.
400 Parentesi troppo profondamente annidati nell'espressione regolare.
401 Più di due numeri specificati in un intervallo.
402 Una parentesi graffa senza pari si trova nel expession regolare.
403 Il primo numero nell'espressione di intervallo è maggiore del secondo.
404 Una staffa senza pari si trova nell'espressione regolare.
405 L'espressione regolare è troppo lunga per la compilazione.
406 Identificatore dell'index del B+tree non valido: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
407 Chiave %1$s sconosciuto si fa riferimento nell'index del B+tree {vfid: (%2$d, %3$d), rt_pgid: %4$d, key_type: %5$s}.
408 Valore sconisciuto (oid) %1$d|%2$d|%3$d riferimento.
409 Valore duplicato (oid) %1$d|%2$d|%3$d riferimento.
410 Chiave NULL è stato fatto riferimento.
411 Tipo di chiave non valido "%1$s" nell'index del B+tree.
412 Campo non valido delle specifiche di ricerca.
413 Identificatore di attributo sconosciuto: %1$d.
414 Identificatore di classe sconosciuto: %1$d|%2$d|%3$d.
415 Identificatore di classe non valido: %1$d|%2$d|%3$d.
416 Identificatore di rappresentazione sconosciuto: %1$d.
417 Identificatore di rappresentazione non valido: %1$d.
418 Descrittore di voce non ha abbastanza spazio: %1$d.
419 Classe ha raggiunto il massimo numero di rappresentazioni: %1$d.
420 Classe %1$d|%2$d|%3$d già avuto rappresentazioni.
421 Repertorio dei rappresentazioni mancante nel catalogo per la classe: %1$d|%2$d|%3$d.
422 Voce d'informazione del repertorio dei rappresentazioni mancanta nel catalogo per la classe - Repr_Id: %1$d|%2$d|%3$d-%4$d.
423 Sessione non valido.
424 Nessuna dichiarazione da eseguire.
425 Tipo di dati incompatibile per l'attributo "%1$s" della classe "%2$s".
426 Tipo di dati incompatibile per l'attributo "%1$s".
427 Overflow dei dati su tipo di dati "%1$s".
428 Dichiarazione non è aggiornabile.
429 Comando sconosciuto.
430 Variabile sconosciuta "%1$s".
431 Errore dell'interprete o parser di SQL/X.
432 Classe sconosciuta "%1$s".
433 l'attributo sconosciuto "%1$s" nella classe "%2$s".
434 Non riesci a trovare i nomi di colonna corretti da una query.
435 Istruzioni multiple non sono permessi.
436 Dichiarazione non è una query.
437 ID dell'oggetto lungo non valido (%1$s, %2$d, %3$s, %4$s).
438 Conflitto esiste nel descrittore dell'oggetto lungo.
439 Offset %1$d è maggiore della lunghezza di oggetto lungo %2$d.
440 Posizione di cursore non valida.
441 Operazione del cursore non valida.
442 Posizione del cursore sconosciuta.
443 Indice di valore per tupla %1$d è fuori portata.
444 Nome non valido per l'attributo: "%1$s".
445 Indice della lista dei valori %1$d è fuori portata.
446 Ci sono delle strutture di risultati delle query chiuse dal gestore delle transazioni, ma non chiuse dall'applicazione.
447 Tentativo di utilizzare una struttura di risultati della query già chiusa.
448 Dimensioni delle tuple sono attualmente limitate ad essere non più grande di una pagina.
449 Identificatore dell'interrogazione sconosciuto: %1$d.
450 Operatore dell'insieme non valido: %1$d.
451 Heapfile senza istanze.
452 Contenuto del nodo non valido dell'albero XASL.
453 Senza più di nodi di specificazione dell'accesso.
454 Tipo di dati non valido è stato fatto riferimento.
455 Senza più delle pagine del file di query.
456 Riferimenti a tipi di dati sono incompatibili.
457 Tipo non valido per il risultato della query.
458 Overflow nel contesto di aggiunta.
459 Risultato della query contiene più di una singola tupla.
460 Troppo poche variabili host di input sono forniti.
461 Le variabili host indicate sono più che le colonne dei risultati.
462 Il risultato è il valore NULL, ma non c'è nessuna variabile indicatore.
463 Istruzione del cursore non è stato ancora preparato.
464 Il cursore dovrebbe essere un'istruzione SELECT.
465 Il cursore non è ancora aperto.
466 Il cursore deve essere chiuso prima di riapertura.
467 Risultato dovrebbe tornare un singolo oggetto.
468 Nessun marcatore di posizione non è ammesso nelle istruzioni EXECUTE IMMEDIATE.
469 Tipo di dati non valido viene utilizzato.
470 Query implica troppo molti articoli per ottimizzatore.
471 Memoria esaurita, in linea %1$d nella file %2$s.
472 Connettore %1$s per database locale '%2$s' su un host '%3$s' è morto.
473 Segnalato dalla database locale '%1$s' su host '%2$s': %3$s.
474 Errore %1$s rilevato durante %2$s.
475 Nessuna specificazione della query con index %1$s.
476 LDB %1$s esiste già.
477 Impossibile connettersi al master su l'host host %2$s per accedere LDB %1$s.
478 Il database locale "%1$s" non è un LDB registrato.
479 Il database locale "%1$s" è attualmente in corso l'accesso da altri clienti, la modifica o l'eliminazione non è permessa.
480 Il database locale "%1$s" non è vuoto e non può essere eliminata.
481 "%1$s" non è una classe proxy virtuale.
482 "%1$s" non è una classe virtuale.
483 "%1$s" non è un attributo della classe "%2$s".
484 Il object_id per il classe proxy virtuale "%1$s" è già impostato.
485 Uno o più dei attributi dell'object_id sono NULL per l'istanza della classe proxy "%1$s".
486 L'oggetto non è un'istanza aggiornabile di classe virtuale.
487 L'oggetto non è un'istanza.
488 Un attributo di una classe non può essere una classe virtuale o una classe proxy virtuale.
489 Un attributo di una classe proxy virtuale non può essere una classe, una classe virtuale, o di un oggetto.
490 La superclasse "%1$s" deve essere dello stesso tipo di classe come "%2$s".
491 Impossibile estrarre il nome dell'ente dalla specificazione proxy della query "%1$s".
492 %1$s
493 Sintassi: %1$s %2$s
494 Semantica: %1$s %2$s
495 Eseguire: %1$s %2$s
496 Chiave sconosciuta viene fatto riferimento.
497 Valore sconosciuto (oid) %1$d|%2$d|%3$d viene fatto riferimento.
498 Valore duplicato (oid) %1$d|%2$d|%3$d viene fatto riferimento.
499 Chiave NULL viene fatto riferimento.
500 Duplicare chiave viene fatto riferimento.
501 Priorità non valido per il trigger.
502 Manca classe target nel trigger.
503 Trigger "%1$s" non è stato trovato.
504 Errore interno: elaborazione del trigger "%1$s".
505 Un trigger con il nome "%1$s" esiste già.
506 Un trigger non può essere definito su "%1$s" perché è una classe virtuale.
507 Classe "%1$s" di destinazione di trigger non adatta.
508 Attributo "%1$s" di destinazione di trigger non adatto.
509 Condizione di trigger non valida: "%1$s".
510 Azione di trigger non valida: "%1$s".
511 Non autorizzato ad accedere trigger "%1$s".
512 Non autorizzato ad eliminare trigger "%1$s".
513 Non autorizzato ad aggiornare trigger "%1$s".
514 Non autorizzato a modificare trigger "%1$s".
515 Tempo di esecuzione %2$s per l'azione del trigger non può essere antecedente a quella del tempo di esecuzione %1$s di condizione.
516 Profondità massima %1$d di trigger superato al trigger "%2$s".
517 L'operazione è stata respinta dal trigger "%1$s".
518 Errore interno: elaborazione del trigger "%1$s".
519 Tipo di condizione di trigger non ammesso.
520 L'azione di rifiuto non può essere utilizzato con un tempo di azione di AFTER o DEFFERED.
521 L'azione di rifiuto non può essere utilizzato con gli eventi ABORT o TIMEOUT.
522 Espressione per la condizione di trigger non è stata data.
523 Espressione per l'azione di trigger non è stata data.
524 L'attività specificato non può essere rimosso perché è di proprietà di un altro utente.
525 Errore nella compilazione di condizione per "%1$s": %2$s
526 Errore nella compilazione d'azione per "%1$s", %2$s
527 Errore nella valutazione di condizione per "%1$s": %2$s
528 Errore nella valutazione dell'azione per "%1$s", %2$s
529 Transazione non può essere commesso, è stata invalidata da trigger "%1$s".
530 Espressione regolare assente.
531 Espressione regolare è troppo lunga o complessa.
532 Sbilanciato parentesi o staffe nel un'espressione regolare.
533 Di riferimento è fuori portata nel un'espressione regolare.
534 Carattere o sintassi non ammesso nel un'espressione regolare.
535 Nessun testo dato per la ricerca di espressioni regolari.
536 Codice operativo non valido trovato in un'espressione regolare.
537 Codice non ammesso di comando di ricerca per compilatore dei espressioni regolari.
538 Buffer di lavoro dei espressioni regolari ha bisogno di essere inizializzato.
539 Tentativo di dividere per zero.
540 %1$s.
541 Numero di pagine libere per volume "%1$s" è incoerente. %2$d e %3$d sono stati trovati secondo il colpo di testa del volume e bitmap, rispettivamente.
542 Numero di settori liberi per volume "%1$s" è incoerente. %2$d e %3$d sono stati trovati secondo il colpo di testa del volume e bitmap, rispettivamente.
543 Colpo di testa del disco per volume "%1$s" è incoerente.
544 Errore interno: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Chiave %6$s del B+tree per OID dell'oggetto: %7$d|%8$d|%9$d  non è stato trovato sul B+tree: %10$d|%11$d|%12$d.
545 Errore interno: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Chiave e OID: %6$d|%7$d|%8$d sul B+tree: %9$d|%10$d|%11$d non sono corretti. L'oggetto non esiste.
546 Errore interno: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d).  In attesa di %6$d valori delle voce OID, ma %7$d sono stati trovati sul B+tree: %8$d|%9$d|%10$d.
547 Versione %1$s del server è diverso dal rilascio %2$s del client.
548 Non vi è alcun object_id definito per classe proxy "%1$s".
549 L'istanza proxy non è stata trovata.
550 Nome sconosciuto %1$s del punto di salvataggio
551 Impossibile trovare il file di percorso "%1$s" dei informazioni sul volume ... Continua a leggere dalle tabelle interne
552 Underflow aritmetico per data.
553 Underflow aritmetico per tempo.
$ LOADDB
554 Il caricatore è entrato in uno stato non valido. Nessuna altra operazione è possibile.
555 Errore di allocazione della memoria durante il caricamento del database.
556 Troppi valori forniti. In attesa di %1$d.
557 Per attributo %1$s di %2$s, %3$s tipo non è ammesso nel dominio dell'insieme.
558 Per attributo %1$s di %2$s, tipo previsto %3$s, ha ottenuto una serie costante.
559 Per attributo %1$s di %2$s, tipo previsto %3$s, ha ottenuto %4$s.
560 Per attributo %1$s di %2$s, dominio non è abbastanza specifico per\n supportare riferimenti a oggetti non qualificati.
561 Collezioni annidati non sono ammessi.
562 Classe System %1$s non può essere popolato con il caricamento del database.
563 Riferimento alla classe interna %1$s convertito a null.
564 Violato il vincolo UNIQUE per attributo %1$s di %2$s.
565 Metodo di costruttore %1$s non è definito sulla classe %2$s.
566 Nessuna classe specificata o nessun attributo nella classe.
567 Troppi argomenti di metodo costruttore forniti. Previsto %1$d.
568 Parametri mancanti per il metodo costruttore. Previsto %1$d, trovata %2$d.
569 Valori mancanti degli attributi. Previsto %1$d, trovata %2$d.
570 Impossibile accedere al file di dati Glo "%1$s".
571 Istanza è stata precedentemente creata tramite un riferimento forward.\nQuesto non è permesso per le istanze create con metodi costruttore.
572 Errore interno: Impossibile costruire la rappresentazione del disco di istanza.
573 Errore interno: Impossibile inserire nuova istanza nel database.
574 Errore interno: Impossibile aggiornare nuova istanza nel database.
575 Per l'argomento %1$d di construttore %2$s, tipo previsto %3$s, ricevuto %4$s.
576 Riferimento alla classe %3$s non è compatibile con il dominio di %1$s di %2$s.
577 Per argomento %1$d del costruttore %2$s, dominio non è abbastanza specifico\n per supportare dei riferimenti non qualificati agli oggetti.
578 Riferimento alla classe %3$s non è compatibile con il dominio dell'argmento %1$d \n del construttore %2$s.
579 Dominio di destinazione deve includere tipo di "oggetto" per permettere riferimenti a classi.
580 Attributo %1$s non è definito per la classe %2$s.
581 Tentativo di aggiornare il database quando gli aggiornamenti sono disabilitati.
582 Target di stoccaggio sconosciuto %1$d. L'intervallo valido è %2$d-%3$d.
583 Cercando di assegnare un numero non valido (%1$d) di pagine.
584 Errore interno: Un ciclo è stato rilevato nella tabella file di file %1$d nel volume "%2$s". Il ciclo può essere causato da pagina %3$d|%4$d.
585 Heap sconosciuto %1$s|%2$d|%3$d
586 Errore interno: impossibile aggiornare il colpo di testa di catena in VPID %1$d|%2$d per l'heap %3$d nel volume "%4$s".
587 Voci di volumi permanenti non sono ordinati nel file volinfo "%1$s". Voce %2$d: %3$d %4$s è fuori sequenza.
588 Errore interno: Asserzione non riuscita '%1$s'.
589 Nome dell'utente non valido "%1$s".
590 Il dominio "%1$s" dell'attributo di una classe non \npuò essere una classe virtuale o una classe proxy virtuale.
591 Il dominio "%1$s" dell'attributo di una classe proxy virtuale \ndeve essere una classe proxy virtuale.
592 Il dominio "%1$s" dell'attributo di una classe proxy virtuale \ne la classe proxy virtuale deve essere sullo stesso database locale.
593 Una istanza virtuale non-aggiornabile è stata recuperata e non può essere ricostruito.
594 Una clausola object_id deve essere fornita senza l'utilizzo di attributi (%1$s) di tipi di dati approssimativi (float o double).
595 Il nome del database "%1$s" è troppo lungo. Nome del database deve essere inferiore a %2$d caratteri.
596 Gli %1$d pagine del totale spazio temporaneo permesso sono stati superati.
597 Numero di pagine per heapfile %1$d|%2$d|%3$d è incoerente. \n%4$d e %5$d sono stati trovati seconda la catena heap e tabella dei file, rispettivamente.
598 Numero di file non è coerente. Ci si aspettava %1$d, %2$d sono stati trovati.
599 Un errore I/O durante lo stato di sincronizzazione del volume "%1$s".
600 Funzione "%1$s" non implementata sul PC.
601 "%1$s".
602 Errore dl: "%1$s" è definito più di una volta.
603 Errore interno: Tabella a settore/pagina di file VFID %1$d|%2$d sembra corrotto.
604 Errore interno: L'allocazione impostato per il file VFID %1$d|%2$d (volume "%3$s") sembra inconsistente nel %4$d pagine eliminate o marcate come eliminate e %5$d fori di pagina nella tabella.
605 Errore interno: numero inconsistente di pagine marcate come eliminate per file VFID %1$d|%2$d (volume "%3$s"). Previsto %4$d, Trovato %5$d.
606 Impossibile trovare odbc.ini con percorso "%1$s".
607 Database "%1$s" non è stato trovato nel odbc.ini.
608 Database "%1$s" non ha una specificazione Host nel file odbc.ini.
609 La transazione non può essere annullato in quanto logging è stato ignorato. Il database potrebbe essere corrotta.
610 Il database è suscettibile di essere corrotta da quando logging è stato disattivato quando il database si è crollato.
611 %1$s non può essere eseguito in modo client/server.
612 Logging non può essere disattivato in questo momento in quanto ci sono le azioni in sospeso per recuperare.
613 Nella classe "%3$s", componente locale "%1$s" non può servire come un sostituto di alias per classe "%2$s". I domini sono incompatibili.
614 Numero di archivi attivi di log è stato superato per il numero massimo desiderato di %1$d.
615 Impossibile stabilire un contatto con l'interfaccia Winsock. Errore di Winsock: %1$d.
616 Impossibile determinare il nome del computer utilizzando l'interfaccia Winsock. Errore di Winsock: %1$d.
617 Impossibile determinare l'ID della macchina utilizzando l'interfaccia Winsock. Ci deve essere una voce nella tabella dei nomi di host per questa macchina. Errore di Winsock: %1$d.
618 Attributo non è aggiornabile.
619 Codeset di origine è male
620 Codeset di destinazione è male
621 Tipo di dati non valido. Gli argomenti contiene un tipo di dati non valido.
622 Insiemi di codici incompatibili. Gli insiemi di codici degli argomenti sono incompatibili.
623 Sequenza di escape non valido. Una sequenza di escape non valida ha causato un errore nel compilatore espressione regolare.
624 Carattere di escape non valido. Il carattere di escape non è NULL con una lunghezza maggiore di 1.
625 Errore interno. Cercando di aggiornare l'istanza sbagliata per l'oggetto %1$d|%2$d|%3$d dal modello di informazioni relative agli attributi di oggetto istanza %4$d|%5$d|%6$d.
626 Errore interno. %1$d attributi richiesti non sono stati trovati.
627 Operando non valido TRIM. L'operando del TRIM ha una lunghezza maggiore di 1.
628 Valuta di tipo non valido: %1$d.
629 Pagesize %1$d non è una potenza di 2 o pagesize è troppo piccolo. PageSize pari a %2$d è usato al posto.
630 La conversione non è supportata da %1$s.
631 Istruzione SQL violato vincolo NOT NULL.
632 %1$s non è un volume di backup.
633 %1$s è un backup del database creato nel %2$s %3$s invece di database specificato %4$s creato su %5$s
634 Un volume/di database file è stato atteso nel backup.
635 ATTENZIONE: pool di Log Buffer di %1$d tamponi sembra molto piccolo per l'ambiente corrente.\n E 'possibile che alcuni buffer non vengono liberati dal sistema a causa di un errore sconosciuto\n o che vi sono diverse transazioni concorrenti di essere abortiti, allo stesso tempo. Lo pool di buffer di log è espanso con %2$d più tamponi.
636 ATTENZIONE: Num_buffers %1$d è troppo piccolo. La valore minimo di %2$d stata assunta.
637 Errore di sistema: liberando di memoria eccessiva per un buffer di log. Fisso conteggio viene impostato a zero.
638 Attenzione: Flush per una pagina di log non aggiornabile dei tranzationi, pageid = %1$d
639 Errore di sistema: Pagina precedente %1$d di sequenza di log non è la pagina di flush ritardato %2$d.
640 Errore di sistema: un punto di salvataggio non può essere aggiunto quando la transazione non è attivo (ad esempio, durante il commit o rollback).
641 Il nome deve essere dato per un punto di salvataggio.
642 Errore di sistema: non c'è un'operazione attiva del sistemo superiore per la transazione corrente.
643 Può essere un errore di sistema: Commit/Interruzione di transazione = %1$d (indice = %2$d), che ha dei operazxioni\n permanenti collegati ad esso. Allegherà le operazioni di sistema per la transazione
644 ERRORE FATALE DEL LOG: %1$s
645 ERRORE DI SISTEMA: Stato = %1$s di transazione = %2$d (indice = %3$d) \n non è lo stato corretto di applicare dei azioni di postpone/cancel del client ... ignorato
646 ERRORE DI SISTEMA: Sembra che un record di compensazione manca per rcvindex %1$s di logging logico.\n Senza questo record di compensazione, crash recovery non viene eseguita correttamente
647 Backup "%1$s" non appartiene al database indicato.
648 Backup non è compatibile con l'attuale "%1$s" versione "%2$s".
649 Precisione indicate di %1$d non è valido, ma dovrebbe essere maggiore di %2$d e non superiore a %3$d.
650 Impossibile creare abbastanza threads per 'max_clients = %1$d'.
651 Non c'è memoria virtuale per preallocare %1$d buffers di ordinamento per ciascuno dei %2$d threads.
652 Offset errato %1$d è stato dato per dividere il record di lunghezza %2$d memorizzato sullo slot %3$d
653 Offset errato %1$d o lunghezza %2$d è stata data per rimuovere una porzione dei dati del record della lunghezza %3$d memorizzato sullo slot %4$d
654 Offset errato %1$d o lunghezza %2$d è stata data a sovrascrivere una porzione dei dati del record della lunghezza %3$d memorizzati sulla slot %4$d
655 Impossibile aprire il file di configurazione del handler dei eventi, "%1$s".
656 Attenzione: Null id/nome nel file di configurazione dell'Event Manager, "%1$s", linea %2$d
657 Attenzione: nome dell'evento "%1$s" non permesso, nel file di configurazione dell'Event Manager, "%2$s", linea %3$d
658 Attenzione: ID di evento %1$d, non nel campo di errore id %2$d <> %3$d, file "%4$s", linea %5$d
659 Non è possibile accedere al programma di Event Manager, "%1$s".
660 Impossibile aprire il/collegare al processo di Event Manager, "%1$s".
661 Impossibile scrivere al processo di Event Manager, "%1$s".
662 Impossibile inizializzare il meccanismo di gestione di eventi.
663 Attenzione: Linea troppo lunga nel file di configurazione dell'Event Manager, "%1$s", linea %2$d, troncamento
664 Meccanismo di gestione di eventi iniziato.
665 Pipe rotto per Event Manager, "%1$s"
666 Meccanismo di gestione degli eventi arrestato.
667 Classe '%1$s' non può corrispondere a classe C++ corrispondente.
668 Errore interno: tipo di dominio sconosciuto '%1$d'.
669 Server ha rifiutato connessione client: numero massimo dei client, (%1$d), superato.
670 Operazione avrebbe causato uno o più violazioni dei vincoli UNIQUE. INDEX %1$s%2$s ON CLASS %3$s%4$s. Chiave: %5$s%6$s.
671 Una errore di nete durante la lettura/scrittura per il socket %1$d associato con la transazione (indice %2$d, %3$s @ %4$s|%5$d)
672 Socket %1$d associato con la transazione (indice %2$d, %3$s @ %4$s|%5$d) è chiuso.
673 La connessione al server è stata chiusa in quanto il server sta andando giù immediatamente.
674 Non ci sono abbastanza threads attivi per continuare l'esecuzione corrente del server. Numero attuale di threads attivi è %1$d. Si può richiedere fino a %2$d threads per l'esecuzione pulita del server nel vostro ambiente. Indice della transazione %3$d è inibito per continuare l'esecuzione del server.
675 Impossibile leggere il file di percorso del database, '%1$s'.
676 Database '%1$s', non può essere trovato nel file di percorso del database, '%2$s'
677 Impossibile connettersi al server di database, '%1$s', sul host(i) seguente: %2$s
678 Errore interno. modulo di riavvio del cliente a ricevuto il nome del server database NULL.
679 La transazione C++ è già iniziata.
680 Ha tentato di superare il massimo livello di nidificazione dei transazioni C++ (%1$d).
681 Transazione %1$s è stata tentata su una transazione C++ che non è iniziato.
682 Impossibile %1$s la transazione C++ fino a quando tutte le transazioni subordinati sono stati completati.
683 Tentativo di creare stringa con lunghezza illegale - %1$d
684 Metodo restituito codice di errore definito dall'utente %1$d.
685 Classe "%1$s" non è valido.
686 Notifica: il Event Manager è stato riavviato.
687 Non permette di lanciare (%1$s *) a (%2$s *).
688 Indice fuori intervallo. Tentativo di accedere all'elemento %1$lld. Dimensione dell'insieme : %2$lld.
689 Errore interno. Iterator contiene lo stato corrotto.
690 Tentativo di dereferenziare un'istanza non inizializzata odb_Ref <>.
691 Tentativo di utilizzare un iteratore non valido.
692 Tentativo di utilizzare un iteratore con un insieme non correlata.
693 Overflow avvenuto nel tipo di dati numerico.
694 Errore di sistema: Impossibile caricare B+tree.
695 Interfaccia errata è stata usata per uccidere un cliente.
696 Transazione sconosciuta (indice %1$d, %2$s @ %3$s|%4$d).
697 L'indice di transazione fornito (indice %1$d, %2$s @ %3$s|%4$d) non corrisponde all'indice della transazione corrente (indice %5$d, %6$s @ %7$s|%8$d). Probabilmente, la transazione fornita è terminata e il suo indice di transazione è stato ri-assegnato ad un'altra transazione.
698 Errore interno: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). il numero di OID nella gerarchia unica: %6$d non è uguale al numero di OID: %7$d trovato nel B+tree unico più il numero di NULL: %8$d trovato nella gerarchia unico per il B+tree: %9$d|%10$d|%11$d .
699 Errore interno: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). il numero di OID nella gerarchia unica: %6$d non è uguale al numero di OID: %7$d trovato nella statistiche del B+tree radice unico: %8$d|%9$d|%10$d.
700 Errore interno: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). il numero di OID: %6$d non è uguale al numero di NULL: %7$d più il numero di chiavi: %8$d nel B+tree unico: %9$d|%10$d|%11$d.
701 Nessun proxy è definita sul "%1$s" a LDB "%2$s".
702 Errore interno: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Il numero di NULL nella gerarchia unica: %6$d non è uguale al numero di NULL: %7$d trovato nella statistiche del B+tree radice unico per il B+tree: %8$d|%9$d|%10$d.
703 Errore interno: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). OID: %6$d|%7$d|%8$d trovato nel B+tree unico: %9$d|%10$d|%11$d non appartiene a una delle classi per il vincolo UNIQUE.
704 Valutazione della funzione server generico non riuscita.
705 %1$d volumi permanenti inconsistenti sono stati trovati quando %2$d erano attesi.
706 Non è possibile espandere i volumi permanenti. volume permanente "%1$s" con scopo %2$s è stato dato.
707 Impossibile espandere volume temporaneo "%1$s" con %2$d pagine
708 Impossibile espandere volume temporaneo del disco "%1$s" con %2$d pagine (%3$lld Kbytes) a causa di spazio insufficiente. Lo spazio attuale disponibile è %4$d pagine (%5$lld Kbytes).
709 Vincolo "%1$s" non trovato.
710 Vincolo non valido.
711 Un oggetto con il valore dell'attributo fornito non esiste.
712 Vincolo "%1$s" esiste già.
713 Un index non esiste su questo attributo.
714 Query non riuscita per mancanza di spazio di file temporanei.
715 Impossibile verificare schema: %1$s
716 Vincoli NOT NULL non sono ammessi nelle classi virtuali o proxy.
717 Vincoli UNIQUE non sono ammessi nelle classi virtuali o proxy.
718 Gadget non valido.
719 Il numero di attributi (%1$d) e valori (%2$d) non sono uguali.
720 Non è possibile utilizzare un gadget da inserire nel una classe virtuale o proxy.
721 Query d'INSERT cede più di una riga.
722 Numero errato di attributi per vincolo NOT NULL.
723 Non è possibile modificare il dominio di un attributo esistente.
724 Parametro non valido/parametri non validi per la generazione di nome del vincolo predefinito.
725 Errore interno: oid temporaneo atteso ma incontrato oid permanente.
726 Vincolo UNIQUE deve essere aggiunto con una dichiarazione separata\ndopo che le istanze sono state create. Ad esempio, invece di: \n\tALTER TABLE1 ADD ATTRIBUTE A INT UNIQUE;\nuso:\nALTER TABLE1 ADD ATTRIBUTE A INT;\n\tALTER TABLE1 ADD UNIQUE(A);
727 Evento di trigger non valido.
728 Query non riuscita perché vfid di file temporanei non è valido per la transazione %1$d.
729 Overflow avvenuto nel contesto di sottrazione.
730 Overflow avvenuto nel contesto di moltiplicazione.
731 Overflow avvenuto nel contesto di divisione.
732 Overflow avvenuto nel contesto di minus unario.
733 Overflow avvenuto nel contesto di coercizione.
734 Errore interno: Colpo di testa del file VFID %1$d|%2$d (Volume "%3$s") è danneggiato.
735 Indice di transazione sconosciuto %1$d.
736 è necessario specificare il servizio o il identificatore di porta. Errore di Winsock: %1$d.
737 Servizio sconosciuto per TCP: %1$s. Errore di Winsock: %2$d.
738 Impossibile creare il flusso di socket. Errore di Winsock: %1$d.
739 Impossibile associare l'indirizzo locale ... riprovare. Errore di Winsock: %1$d.
740 Impossibile associare l'indirizzo locale ... interruzione. Errore di Winsock: %1$d.
741 Accetta non riuscita. Errore di Winsock: %1$d.
742 Invio di un messaggio broadcast al client. Errore di Winsock: %1$d.
743 Handshake impossibilo tra il client e il server. (host peer %1$s)
744 Errore in ri-hashing un MOP di proxy/vclass.
745 Fase di Garbage Collection non valida.
746 Callback di Garbage Collection non valido.
747 Ticket non valido di callback di Garbage Collection.
748 Puntatore non valido dell'oggetto incontrato dal db_gc_mark_object.
749 Il numero d'istanze recuperati non corrispondano: %1$d previsto, %2$d ricevuto.
750 Il numero d'istanze recuperati non corrispondano: %1$d previsto, %2$d ricevuto.
751 L'OID di un oggetto nuovo non è disponibile nel un trigger BEFORE INSERT.
752 Errore durante il ripristino di backup unit_num %1$d.
753 Errore durante il ripristino di backup unit_num %1$d, pageid %2$d supera il numero totale di pagine %3$d per VOLID %4$d.
754 Istanza del proxy OO ha di tipo non-string o chiave null.
755 %1$s non può essere eseguito in modo stand-alone.
756 Buffer di stringa specificato è troppo piccolo per codificare questo oggetto.
757 Errore interno: impossibile assegnare OID permanente durante la codifica dell'oggetto.
758 Stringa specificata non è valida per la decodifica dell'oggetto.
759 L'oggetto di destinazione è stato eliminato.
760 Errore interno: istanza di un oggetto proxy ha chiave NULL durante la codifica dell'oggetto.
761 Errore interno: istanza di un oggetto vclass corrisponde a un'istanza non valido reale.
762 Errore interno nel risolvere un'istanza vclass a un'istanza reale.
763 Errore interno nella codifica dell'istanza non aggiornabile di oggetto.
764 Errore interno nella codifica dell'oggetto nella forma vobj interna.
765 Oggetto argomento di uscita ha un indirizzo NULL.
766 Errore interno nella decodifica dell'oggetto.
767 Impossibile creare un backup a livello %1$d senza prima creare un backup a livello %2$d.
768 Il "%1$s" database locale è stata letta in questa sessione, la modifica o la soppressione non è permesa.
769 "%1$s" attributo, di classe "%2$s" non è un'attributo condiviso o di una classe valida.
770 Nessuna funzione di tabella.
771 Parametro non valido per la funzione.
772 db_serial di classe non trovato.
773 Serial "%1$s" non trovato.
774 Serial "%1$s" esiste già.
775 Prossimo valore supera la valore MAX (MIN).
776 Impossibile recuperare l'oggetto serial.
777 Impossibile aggiornare l'oggetto serial.
778 Non è possibile creare un link simbolico "%1$s" al file "%2$s".
779 Risultato supera il limite: anno della data deve essere nel 1 e 9999.
780 Errore interno di sistema: impossibile ottenere il tempo di sistema.
781 Stringa di formato è troppo lunga. Deve essere inferiore a 16K.
782 Stringa vuota non ammessa qui.
783 Formato non valido.
784 Due argomenti non corrispondono.
785 Stringa del primo argomento è troppo lungo. Deve essere inferiore a 16K.
786 Formato è duplicato.
787 Errore di conversione nel formato tempo.
788 Errore di conversione nel formato timestamp.
789 Numero errato.
790 L'esecuzione della query è stata interrotta dall'utente.
791 Valore seriale non valida.
792 Impossibile allocare la memoria per css.
793 pthread_attr_init() non riuscito.
794 pthread_attr_destroy() non riuscito.
795 pthread_attr_setdetachstate() non riuscito.
796 pthread_attr_setscope() non riuscito.
797 pthread_attr_setstacksize() non riuscito.
798 pthread_create() non riuscito.
799 pthread_join() non riuscito.
800 pthread_mutex_init() non riuscito.
801 pthread_mutex_destroy() non riuscito.
802 pthread_mutex_lock() non riuscito.
803 pthread_mutex_trylock() non riuscito.
804 pthread_mutex_unlock() non riuscito.
805 pthread_mutexattr_init() non riuscito.
806 pthread_mutexattr_destroy() non riuscito.
807 pthread_mutexattr_settype() non riuscito.
808 pthread_mutexattr_gettype() non riuscito.
809 pthread_cond_init() non riuscito.
810 pthread_cond_destroy() non riuscito.
811 pthread_cond_wait() non riuscito.
812 pthread_cond_timedwait() non riuscito.
813 pthread_cond_signal() non riuscito.
814 pthread_cond_broadcast() non riuscito.
815 pthread_key_create() non riuscito.
816 pthread_key_delete() non riuscito.
817 pthread_setspecific() non riuscito.
818 pthread_getspecific() non riuscito.
819 pthread_once() non riuscito.
820 Impossibile inizializzare una connessione.
821 Arresto di una connessione.
822 Operazione del mutex non riuscita durante la prendere dell'id del prossimo cliente.
823 Operazione del mutex non riuscita durante l'inizializzazione della lista dei connessioni.
824 Non è possibile distruggere il mutex durante la finalizzazione della lista dei connessioni.
825 Chiamata di pread() non riuscita.
826 Chiamata di pwrite() non riuscita.
827 Funzioni di trasversa della lista dei conessioni ha data una valore non valido.
828 Sezione critica non valida dell'indice è stato dato.
829 Questo blocco mutex è già stato sbloccato.
830 Impossibile allocare più dei voci della query. Numero massimo di voci definibili è %1$d.
831 Tutti i descrittori delle transazioni (%1$d) sono in uso. Sollevare il parametro max_clients.
832 Backup è già in esecuzione. Di backup non può essere eseguito da due processi.
833 Il nome dell'index "%1$s" è ambiguo. Specificati il nome della classe dell'index.
834 Ci sono delle discrepanze tra stringa di origine "%1$s" e la stringa di formato "%2$s" dato nel to_number().
835 Artocolo non definito di statistiche.
836 Blocco sulla pagina (%1$d|%2$d) è scaduto.
837 Impossibile eliminare l'utente che possiede oggetti di database (classe/trigger/seriale etc.)
838 Non è possibile aprire più di %1$d scansioni di index contemporaneamente.
839 "%1$s": parametro di sistema sconosciuto o valore non valido.
840 Non è possibile modificare il parametro di sistema "%1$s".
841 L'operazione richiesta può essere eseguita solo da un unico cliente (transazione).
842 Non è possibile memorizzare nella cache il blocco dell'oggetto %1$d|%2$d|%3$d.
843 Argomenti errati nel %1$s, un %2$s è stato passato.
844 Isolamento sconosciuto (%1$d) è stato trovato. L'indice di transazione è %2$d.
845 Tipo di oggetto non valido (%1$d) è stato trovato. L'oggetto bloccabile è %2$d|%3$d|%4$d.
846 %1$s di transazione (%2$d) non si trova nell'elenco dei possessori dell'oggetto (%3$d|%4$d|%5$d).
847 %1$s su un %2$s (%3$d|%4$d|%5$d) non è stato trovato nella transazione (%6$d) nella sua lista dei possessori (cnt = %7$d).
848 %1$s su un oggetto (%2$d|%3$d|%4$d) non si trova nella lista non2pl della transazione (%5$d).
849 Cercando di interrompere una transazione (%1$d) due volte.
850 Transazione (%1$d) non è né titolare del blocco né waiter di blocco di un oggetto (%2$d|%3$d|%4$d).
851 Impossibile allocare risorsa di blocco (%1$s).
852 Modo dei possessori totali (%1$d) di una lista vuota dei possessori di blocco non è NULL_LOCK.
853 Per un oggetto (%1$d|%2$d|%3$d), transazione (%4$d) di una lettura non commessa stava tenendo uno blocco diffetoso (%5$s).
854 ATTENZIONE: Nessun possessore di blocco, ma waiters di blocco esistono. Fare riferimento a %1$s.
855 ATTENZIONE: Due o più dei threads sono in attesa di blocco per una operazione (%1$d).
856 ATTENZIONE: Strano stato di lockwait (%1$d|%2$d), thread (%3$d|%4$d) e transazione (%5$d).
857 Un errore I/O durante il troncamento della pagina %1$d di volume "%2$s".
858 Volume "%1$s" è sconosciuto al riavvio del server.
859 Blocco sulla pagina (%1$d|%2$d) è annulatto.
860 Non è possibile effettuare transazione in corso come parte di una transazione globale, perché il suo stato è '%1$s'.
861 Impossibile preparare il commit della transazione corrente con identificatore di transazione %1$d, perché tale operazione non è fatta come parte di una transazione globale.
862 Impossibile impostare le informazioni utente della transazione globale con l'identificatore %1$d, perché il suo stato è '%2$s'.
863 Impossibile ottenere le informazioni di utente della transazione globale con l'identificativo %1$d, perché non è impostato.
864 Impossibile aprire la file verbose '%1$s' di BackupDB/RestoreDB.
865 Non è possibile eseguire l'istruzione a causa di marcatori di ingresso del tipo sconosciuto.
866 Codice di errore non utilizzato.
867 Codice di errore non utilizzato.
868 Codice di errore non utilizzato.
869 Codice di errore non utilizzato.
870 Codice di errore non utilizzato.
871 Codice di errore non utilizzato.
872 Codice di errore non utilizzato.
873 Errore di autenticazione PKI: "%1$s".
874 Argomento di power() è fuori portata.
875 Overflow è successo nel contesto di power.
876 Codice di errore non utilizzato.
877 Non ci sono dei dati a scaricare.
878 File del percorso di database non valido, '%1$s'.
879 Non è possibile ottenere l'autorizzazione: '%1$s'.
880 Impossibile modificare l'autorizzazione. '%1$s'
881 Non è possibile ottenere il file '%1$s di blocco (ID file: %2$d) '.
882 Non è possibile rilasciare file di blocco 'FILE ID:%1$d'.
883 Impossibile ottenere lo stato del file '%1$s'.
884 Percorso del volext '%1$s' non è permesso a scrivere.
885 Errore di passare da '%1$s' a '%2$s'. Utilizza lo stesso dispositivo (disco).
886 "%1$s" causato violazione del vincolo UNIQUE.
887 Stored procedure '%1$s' già esiste.
888 Conteggio dei parametri non è valido. Attesi: %1$d, effettivi: %2$d
889 Errore: L'esecuzione del stored procedure %1$s
890 Partizione non riuscita.
891 Partizione adeguata non esiste.
892 Chiave primaria "%2$s" già definita per la classe "%1$s".
893 Attributo '%1$s', un membro della chiave primaria, non può essere eliminato.
894 Stored procedure/funzione '%1$s' non esiste.
895 Tipo di stored procedure non valido: '%1$s' è il '%2$s'.
896 Compressione non riuscita. Metodo Zip: %1$d (%2$s), Livello Zip: %3$d (%4$s)
897 Decompressione non riuscita.
898 Errore di replicazione: "%1$s"
899 Richieste di partizione non valide.
900 Java VM non si trova: %1$s.\n Di conseguenza, chiamata della stored procedure Java non è permessa.
901 Java VM non può essere avviato: %1$s.\n Di conseguenza, chiamata alla stored procedure Java non è permessa.
902 Java VM non è in esecuzione.
903 Non è possibile connettersi a Java VM: %1$s
904 Nome non valido del stored procedure.
905 Connessioni di rete con JVM non riusciti: %1$d
906 Firma del metodo Java non valida.
907 Il proprietario o un membro del gruppo DBA può eliminare stored procedure.
908 Troppi argomenti di stored procedure '%1$s'.
909 Catalogo classe/vclass è mancante o non valido.
910 L'accesso alla partizione non è permesso.
911 Chiamata non valida: non può tornare ResultSet.
912 ResultSet non può essere utilizzato come parametro d'ingresso.
913 Troppi chiamati nidificati del stored procedure.
914 Un oggetto serial esiste già come un vincolo di incremento automatico.
915 Valore di incremento di un vincolo di incremento automatico deve essere maggiore di 0.
916 Volume di log attivo non è incluso in questo volume di backup (%1$s).
917 Volume di log attivo esiste già.
918 La classe '%1$s' referita dalla chiave esterna non esiste.
919 La classe '%1$s' referita dalla chiave esterna non ha la chiave primaria.
920 La chiave esterna '%1$s' non comprende il membro '%2$s' del chiave primaria.
921 Il dominio del membro '%1$s' della chiave esterna è diverso dal quello del membro '%2$s' della chiave primaria.
922 Il vincolo della chiave esterna '%1$s' non è valido, a causa di valore '%2$s'.
923 La chiave primaria '%1$s' referita dal una chiave esterna '%2$s' non dovrebbe essere eliminata.
924 Operazioni di UPDATE/DELETE sono limitate dalla chiave esterna '%1$s'.
925 L'acquisizione di blocco per la chiave esterna '%1$s' non è riuscita.
926 L'istanza avente la chiave esterna '%1$s' non può essere eliminata.
927 Il numero di chiavi della chiave esterna '%1$s' è diverso da quello della chiave primaria '%2$s'.
928 Non un utente, ma il sistema può assegnare l'attributo '%1$s' creato da 'ON CACHE OBJECT' di chiave esterna.
929 Una chiave esterna non può essere definita nella classe virtuale.
930 è possibile eliminare la attributo '%1$s' creato da 'ON CACHE OJECT' di chiave esterna solo dopo aver rimosso il vincolo della chiave esterna.
931 Valore iniziale di un vincolo di incremento automatico dovrebbe essere inferiore al valore massimo.
932 Non è possibile riparare un inconsistenza del numero di pagine libere per il volume "%1$s" perché %2$d pagine ottenuti dal colpo di testa del volume non sono consistenti con %3$d pagine del bitmap.
933 Non é possibile riparare un'incoerenza del numero di settori liberi per il volume "%1$s" perché %2$d settori ottenuti dal colpo di testa del volume non sono consistenti con %3$d settori della bitmap.
934 Dominio NULL viene fatto riferimento.
935 L'argomento della funzione %1$s è fuori portata.
936 Overflow è avvenuto nel contesto di exp.
937 Errore di database.
938 Argomento non valido.
939 Ci sono troppi di connessioni.
940 Il handle non è valido.
941 L'operazione non è supportata.
942 Handle è scaduto in attesa per i blocchi.
943 Errore generico.
944 L'istruzione non è preparata.
945 L'istruzione non ha alcun ResultSet.
946 L'istruzione non viene eseguita.
947 Non vi è alcun ResultSet più.
948 La dimensione del buffer non è sufficiente.
949 Non ci sono informazioni disponibili.
950 Il nome non è valido.
951 Il ResultSet non è aggiornabile.
952 La riga viene eliminata.
953 Parametri del'istruzione preparata non sono impostati.
954 L'istruzione non è un'istruzione batch.
955 Impossibile cancellare questa istruzione batch.
956 L'esecuzione in batch non è possibile.
957 L'istruzione è un'istruzione preparata.
958 L'istruzione non è un'istruzione preparata.
959 L'istruzione è un'istruzione batch.
960 Non ci sono più di errori.
961 Non ci sono più di risultati.
962 Memoria esaurita.
963 Errore di broker.
964 L'handle di ResultSet è chiuso.
965 Non è possibile utilizzare i nomi degli attributi duplicati nella lista delle chiave di index. Nome dell'attributo '%1$s' elencato più di una volta.
966 La transazione (indice %1$d, %2$s @ %3$s|%4$d) scaduto in attesa sul blocco %5$s sull'oggetto %6$d|%7$d|%8$d a causa della situazione di stallo. Si è in attesa per l'utente %9$s per terminare.
967 La transazione (indice %1$d, %2$s @ %3$s|%4$d) scaduto in attesa sul blocco %5$s sulla classe %6$s a causa della situazione di stallo. Si è in attesa per l'utente %7$s per terminare.
968 La transazione (indice %1$d, %2$s @ %3$s|%4$d) scaduto in attesa sul blocco %5$s nell'istanza %6$d|%7$d|%8$d di classe %9$s a causa della situazione di stallo. Si è in attesa per l'utente %10$s alla fine.
969 Piattaforma del server (%1$d) è differente dal piattaforma del client (%2$d).
970 Server in modo HA passa da '%1$s' a '%2$s'.
971 Programma '%1$s' (pid %2$d) si è connesso al server del database '%3$s' sull'host '%4$s' (porta %5$d).
972 Programma '%1$s' (pid %2$d) si è connesso dall'host '%3$s'. (indice di transazione %4$d)
973 Stato del server è %1$s.
974 Log "%1$s" dei transazioni è stato creato per le pagine di archivio %2$lld-%3$lld.
975 La versione attuale di replicazione non permette modificare righe multiple con una singola istruzione UPDATE che può violare il vincolo UNIQUE.
976 Errore interno: tabella troppo piena (dimensione allocata: %1$d, dimensione accessibile: %2$d) a pagina della tabella di file %3$d|%4$d (volume "%5$s")
977 Checkpoint ha iniziato (id della pagina precedente di checkpoint: %1$lld, id della pagina REDO precedente: %2$lld).
978 Checkpoint finished (checkpoint page id: %1$lld, checkpoint redo page id: %2$lld, flushed page count: %3$d).
979 Ciclo CONNECT BY nelle dati dell'utente.
980 Un thread è in attesa nel %1$s più di %2$d msec.
981 Transazione %1$d ha raggiunto alla soglia di statistiche di monitoraggio %2$s.
982 Impostare l'interruzione alla transazione %1$d.
983 Le istanze di una classe OID riutilizzabile non sono riferibili. Questa operazione non è permessa sulle istanze non riferibile.
984 Impossibile impostare %3$s a "%6$s" nella sezione "[%5$s]" quando è impostato a "%4$s" nella sezione "[%2$s]" del file di configurazione "%1$s".
985 L'hostname nella stringa di connessione al database deve essere specificato quando multihost è impostato in "databases.txt".
986 Funzionalità di CUBRID heartbeat iniziato.
987 Funzionalità di CUBRID heartbeat fermato.
988 Evento del nodo: %1$s.
989 Evento di processo: %1$s. %2$s
990 Esecuzione di comando: %1$s. %2$s
991 Candidati vittimi del Flush del buffer di pagina sono iniziati.
992 Candidati vittimi del Flush del buffer di pagina sono finiti (conteggio: %1$d).
993 Overflow è successo nel contesto dell'operazione bit.
994 Questa istruzione non può essere una istruzione preparata.
995 Un'istruzione preparata con il nome %1$s non esiste.
996 Numero errato di valori dati. %1$d valori sono stati dati, ma %2$d valori sono necessari.
997 Impossibile aggiungere il vincolo di chiave esterna %1$s al %2$s partizionato. Azioni referenziale che aggiorno degli attributi non sono ancora supportati sulle classi partizionati.
998 Impossibile aggiungere il vincolo di chiave esterna %1$s sull'attributo. L'attributo %2$s non può avere un vincolo NOT NULL perché deve corrispondere all'azione referenziale SET NULL.
999 Impossibile creare tabella '%1$s' come tabella '%2$s' perché la tabella esistente ha caratteristiche che non saranno duplicati (%3$s).
1000 '%1$s' è un attributo SHARED o del CLASS, ma solo gli attributi normali sono ammessi nel questo contesto.
1001 Una condizione imprevista o uno stato sono stati raggiunti. '%1$s'
1002 L'operazione corrente non può acquisire un blocco di database che è necessario per continuare l'elaborazione.
1003 Il vincolo '%1$s' esiste ma ha un diverso tipo.
1004 Si prega di notare che quando si usa ALTER TABLE DROP FOREIGN KEY, CUBRID elimina sia il vincolo e l'index associato.
1005 Codice di errore non utilizzato.
1006 Codice di errore non utilizzato.
1007 Impossibile creare l'index con lunghezza del prefisso sul UNIQUE, sul vincolo di chiave esterna o di chiave primaria.
1008 Impossibile creare l'index con lunghezza del prefisso su una classe partizionata.
1009 COMPACTDB già cominciato.
1010 Impossibile definire l'index con lunghezza del prefisso sul dominio "%1$s".
1011 Impossibile aggiungere il vincolo di chiave esterna %1$s sull'attributo shared %2$s.
1012 Impossibile creare tabella con tipo di colonna NULL.
1013 Impostare DEFAULT o SHARED su colonna di tipo %1$s non è permesso.
1014 Impostare NOT NULL sulla colonna di tipo %1$s non è permesso.
1015 Directory specificato '%1$s' non esiste.
1016 Errori di storage esterno %1$s: %2$s
1017 Percorso per storage esterno '%1$s' non è valido.
1018 Impossibile copiare il file esterno sui diversi tipi di memoria. (origine %1$s dest %2$s)
1019 Storage esterno non è inizializzato perché il percorso non è specificato nel "databases.txt".
1020 File esterno "%1$s" non è stato trovato.
1021 Un ciclo di deadlock viene rilevato. %1$s.
1022 Valore "%1$d" non valido per lunghezza del prefisso dell'index.
1023 Codice di errore riservato per HA.
1024 log writer: failed to get log page(s) starting from page id %1$lld.
1025 HA delay info: The replication delay (%1$d secs) has exceeded the limit (%2$d secs).
1026 HA delay info: The replication delay (%1$d secs) is decreased below the acceptable level (%2$d secs).
1027 log applier: non è riuscito a cambiare stato dell'applicare dal '%1$s' a '%2$s'.
1028 log applier: voce EOF imprevisto nel log dei transazioni. LSA: %1$lld|%2$d.
1029 log applier: offset/pagina non valida del log di replicazione . HDR della pagina: %1$1lld|%2$d, final: %3$lld|%4$d, Append LSA: %5$lld|%6$d, EOF LSA: %7$lld|%8$d, stato del file HA: %9$d, is end-of-log: %10$d.
1030 log applier: voce di replicazione don valida. LSA: %1$lld|%2$d, forw LSA: %3$lld|%4$d, backw LSA: %5$lld|%6$d, Trid: %7$d, prev tran LSA: %8$lld|%9$d, type: %10$d.
1031 log applier: non riuscito ad applicare il log del replicazione della schema. classe: "%1$s", schema: "%2$s", Errore del server: %3$d, %4$s. 
1032 log applier: non riuscito ad applicare il log del replicazione della schema. classe: "%1$s", chiave: "%2$s", Errore del server: %3$d.  
1033 log applier: non riuscito ad applicare il log del replicazione dell'UPDATE. classe: "%1$s", chiave: "%2$s", Errore del server: %3$d.  
1034 log applier: non riuscito ad applicare il log del replicazione del DELETE. classe: "%1$s", chiave: "%2$s", Errore del server: %3$d.  
1035 log applier: dimensione della memoria (%1$d MB) dell'applicatore di log è maggiore della dimensione massima di memoria (%2$d MB) o è stato aumentato più di 2 volte (%3$d MB). LSA richiesto: %4$lld|%5$d. ultimo LSA commesso : %6$lld|%7$d.
1036 log applier: log applier sé arrestato dal segnale.
1037 log writer: log writer sé arrestato dal segnale.
1038 log applier: log applier iniziato. LSA necessario: %1$lld|%2$d. ultimo LSA commesso: %3$lld|%4$d. ultimo repl LSA comesso: %5$lld|%6$d.
1039 log writer: log writer iniziato. Modo: %1$d.
1040 HA generico: %1$s. 
1041 Una stringa è stata troncata in un contesto di "%1$s".
1042 Cercando di creare una stringa che richiede %1$d byte di memoria, mentre il massimo permesso è di %2$d byte.
1043 Impossibile modificare attributo "%1$s". CUBRID non può modificare le proprietà dell'attributo richiesto, se la classe ha sottoclassi.
1044 Impossibile modificare attributo "%1$s". CUBRID non può modificare gli attributi che sono chiavi di partizionamento.
1045 Impossibile modificare attributo "%1$s". CUBRID non può modificare gli attributi utilizzati nelle chiavi esterne (sia riferimento o di riferimento).
1046 Impossibile modificare attributo "%1$s". CUBRID non può cambiare il dominio dell'attributo. Questo cambiamento di dominio non è supportato.
1047 Impossibile modificare attributo "%1$s". CUBRID non può cambiare il dominio dell'attributo. Il nuovo dominio non può correttamente rappresentare tutti i valori degli attributi esistenti.
1048 Impossibile modificare attributo "%1$s". CUBRID non può attualmente modificare un attributo AUTO_INCREMENT se il dominio dell'attributo cambia anche.
1049 Impossibile modificare attributo "%1$s". CUBRID non può attualmente cambiare il dominio dell'attributo. La conversione del dominio è possibile, ma questa funzione non è permessa dalla configurazione.
1050 Impossibile modificare attributo "%1$s". CUBRID non può attualmente cambiare il dominio dell'attributo. L'attributo ha un index su esso.
1051 Impossibile modificare attributo "%1$s". CUBRID non può attualmente modificare l'attributo per aggiungere un vincolo. Valori degli attributi esistenti non possono soddisfare il vincolo.
1052 Attributo "%1$s" non è stato modificato. CUBRID non ha riscontrato cambiamenti effettivi nella rappresentazione dell'attributo.
1053 Le instruzioni REPLACE INTO... and INSERT ... ON DUPLICATE KEY UPDATE non sono ammessi per le classi che fanno parte di una catena di ereditarietà.
1054 Il nuovo valore di un vincolo di incremento automatico deve essere maggiore o uguale al valore corrente.
1055 Il nuovo valore di un vincolo di incremento automatico deve essere inferiore al valore massimo.
1056 Per evitare ambiguità, l'opzione AUTO_INCREMENT di tabella richiede che la tabella ha esattamente una colonna AUTO_INCREMENT e nessun specificazione per seed/increment.
1057 Non è possibile aggiungere un attributo '%1$s' di tipo NOT NULL senza un valore predefinito.
1058 ALTER TABLE .. CHANGE: cambiando al nuovo dominio: cast non riuscito, la configurazione attuale non permette di troncamento o di overflow.
1059 ALTER TABLE .. CHANGE: cambiando al nuovo dominio: cast non riuscito, il valore predefinito di nuovo dominio è stato impostato.
1060 ALTER TABLE .. CHANGE: cambiando al nuovo dominio: cast non riuscito, il valore minimo del nuovo dominio è stato impostato.
1061 ALTER TABLE .. CHANGE: cambiando al nuovo dominio: cast non riuscito, il valore massimo del nuovo dominio è stato impostato.
1062 Impossibile modificare l'attributo "%1$s". Il cambiamento si tradurrebbe in più chiavi primarie definite.
1063 Impossibile aggiungere il vincolo NOT NULL per l'attributo "%1$s": ci sono già dei valori NULL per questo attributo.
1064 ALTER TABLE .. CHANGE: aggiungendo NOT NULL: tutti i valori NULL esistenti sono stati modificati per il valore di default fisso del dominio.
1065 Impossibile modificare l'attributo "%1$s". CUBRID non può aggiungere il vincolo NOT NULL: il valore di default fisso non esiste per il nuovo dominio.
1066 Sessione scaduta.
1067 Colonna '%1$s' non può avere un vincolo NOT NULL e un valore predefinito NULL.
1068 Profondità massima di trigger non può superare %1$d.
1069 Troppi d'instruzioni preparati
1070 Variabile di sessione '@%1$s' non definita.
1071 Troppi di variabili di sessione.
1072 Indirizzo IP (%1$s) non è autorizzato.
1073 Formato non valido nel file (%1$s).
1074 Impossibile leggere la fila dell'elenco di accesso (%1$s).
1075 Index di scansione discendente interrotto a causa di priorità inferiore sul B+tree con l'identificatore di index: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
1076 Impossibile caricare i parametri del sistema.
1077 Il parametro '%1$s' alla linea %2$d nel file '%3$s': parametro sconosciuto
1078 Carattere all'offset %1$d non è valido con il codeset corrente.
1079 Codesets di database(%1$s) e per la configurazione del server(%2$s) sono incompatibili.
1080 La lunghezza massima della stringa del predicato del filtro deve essere %1$d.
1081 Non è possibile modificare un attributo coinvolto nell'un index di filtro o di funzione.
1082 Tutti i tamponi di pagina sono sporchi (LRU: %1$d).
1083 Informazioni specifiche sulla situazione di stallo.\n%1$s
1084 Ignorando pagina non valida nel checkpoint. (pagina id: %1$d, "%2$s", oldest_unflush_lsa: %3$lld|%4$d, LSA REDO precedente del checkpoint: %5$lld|%6$d)
1085 Impossibile compilare l'espressione regolare: '%1$s'
1086 Impossibile eseguire l'espressione regolare: '%1$s'
1087 Log attivo di backup '%1$s' cominciato.
1088 Log attivo di backup '%1$s' finito.
1089 Allocazione delle risorse non riuscito. Aumentate 'max_bestspace_entries= %1$d'.
1090 Inizializzazione dei impostazioni internazionali: %1$s.
1091 Generazione dei impostazioni internazionali: %1$s.
1092 Impossibile preparare l'instruzione di tipo '%1$s' con variabili host.
1093 L'argomento '%1$s' per inet_aton non è una stringa valida formattata come un'indirizzo IPv4.
1094 L'argomento '%1$lld' per inet_ntoa non è un numero valido formattato come un'ndirizzo IPv4.
1095 Nome di utente è troppo lungo.
1096 Modificazione dello schema di partizionamento non è permessa quando la classe partizionata fa riferimento a una chiave esterna.
1097 Colonna '%1$s' non può avere un vincolo PRIMARY KEY e un valore predefinito NULL.
1098 Impostazioni internazionali per la lingua '%1$s' non sono disponibili con charset '%2$s'.
1099 Instruzione DDL non è consentito in base alla configurazione (block_ddl_statement=yes).\n SQL Testo: %1$s
1100 Instruzione senza clausola WHERE non è permessa dalla configurazione (block_nowhere_statement=yes).
1101 Codice di errore non utilizzato.
1102 Parametro di sistema sconosciuto o valore non valido.
1103 Impossibile modificare i parametri del sistema.
1104 Query lenta (%1$d msec)\n%2$s
1105 Errore interno: Impossibile scaricare ogetto flush dirty.
1106 Più righe nella tabella di origine corrispondono alla stessa riga nella tabella di destinazione.
1107 Bucket numero non valido per NTILE. Dovrebbe essere maggiore di zero e non più di 2.147.483.647.
1108 Bucket numero non valido per WIDTH_BUCKET. Dovrebbe essere maggiore di zero e non più di 2.147.483.647.
1109 Valore non valido per la definizione delle partizioni.
1110 Spazio insufficiente nella pagina btree leaf. (key len: %1$d)\n%2$s
1111 Valore risultante viene troncato, poiché la precisione(%1$d) è inferiore alla sua lunghezza(%2$d).
1112 Un carattere non può essere convertito da '%1$s' a '%2$s'.
1113 Indice elemento fuori intervallo.
1114 Argomento '%1$s' fuori intervallo.
1115 Iniziato da aggiornare le statistiche (class "%1$s", oid : %2$d|%3$d|%4$d).
1116 Finito di aggiornare le statistiche (class "%1$s", oid : %2$d|%3$d|%4$d, codice errore : %5$d).
1117 Impossibile modificare l'attributo "%1$s". CUBRID non può cambiare un attributo come SHARED e viceversa.
1118 L'argomento di "%1$s" non può essere costretto a dominio desiderato "%2$s".
1119 Errore interno: Pagina id %1$d è assegnato secondo la mappa di allocazione del volume di "%2$s", ma non appartiene a qualsiasi file. 
1120 Errore interno: Pagina id %1$d del volume "%2$s" è attualmente utilizzata, ma non è attribuito secondo la mappa di allocazione di volumi.
1121 NOT NULL non consente valore NULL.
1122 Timeout in ricezione di dati.
1123 pthread_cond_timedwait() timeout. 
1124 Errore di esecuzione query. ERROR_CODE = %1$d, /* SQL_ID: %2$s */ %3$s
1125 Creare il file chiave di overflow. INDEX %1$s%2$s ON CLASS %3$s%4$s. chiave: %5$s%6$s.
1126 Creare una nuova pagina di overflow. INDEX %1$s%2$s ON CLASS %3$s%4$s. chiave: %5$s%6$s.
1127 Eliminare una pagina di overflow vuota. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1128 Recupero log è avviato. Il numero di record del log da applicare: %1$lld. Log page: %2$lld ~ %3$lld.
1129 Recupero log è finito.
1130 Transazione viene interrotta perché numero di lock counts supera il valore %1$d.
1131 Errore interno: La pagina %1$d di volume "%2$s" potrebbe essere danneggiata.\n%3$s
1132 Encryption library failure: %1$s
1133 Copylogdb per %1$s esiste già.
1134 Applylogdb per %1$s esiste già.
1135 Un link precedente di B+tree(%1$s) è danneggiato: vpid = (%2$d, %3$d)
1136 Un link precedente di B+tree(%1$s) è riparato: vpid = (%2$d, %3$d)
1137 La query è stata respinta a causa di tentare di superare il livello di annidamento massimo consentito(%1$d).
1138 Errore Handshake (peer host %1$s): interrompibilità incompatibila. (cliente: %2$s, server: %3$s)
1139 Errore Handshake (peer host %1$s): modo incompatibile read/write. (cliente: %2$s, server: %3$s)
1140 Errore Handshake (peer host %1$s): HA replica ritardata.
1141 Errore Handshake (peer host %1$s): replica-only cliente to non-replica server.
1142 Errore Handshake (peer host %1$s): versione server non identificato.
1143 Errore Handshake (peer host %1$s): non è consentito l'accesso remoto al server.
1144 Tempo scaduto durante il tentativo di connettersi a %1$s. (timeout: %2$d sec(s))
1145 non è consentito cambiare il proprietario di una classe di sistema.
1146 identificatore di volume %1$d non esiste.
1147 Le dimensioni del XASL stream cache(%1$d) supera il max(%2$d). È stata aggiunta una nuova voce anche se nulla è vittimizzato perché tutte le voci della cache sono già utilizzate.
1148 Le dimensioni del filter predicate stream cache(%1$d) supera il max(%2$d). È stata aggiunta una nuova voce anche se nulla è vittimizzato perché tutte le voci della cache sono già utilizzate.
1149 Impossibile trovare la pagina %1$d del volume %2$d.
1150 Contesto richiede collations compatibili.
1151 The page %1$d of volume %2$d is not a slotted page.
1152 The owner or a member of DBA group is only allowed to kill the transaction (%1$d).
1153 Attempt to use 'zero date'.
1154 MVCC row %1$d|%2$d|%3$d does not satisfies reevaluation.
1155 MVCC invalid row for delete: %1$d|%2$d|%3$d.
1156 MVCC can't get snapshot
1157 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.
1158 Serializable conflict due to concurent updates
1159 Ultimo errore

$set 6 MSGCAT_SET_INTERNAL
1 Errore nel sottosistema di errore (linea %1$d):
2 Nessun messaggio di errore disponibile.
3 Impossibile allocare %1$d bytes.
4 Non riesci a trovare catalogo dei messaggi "%1$s".
5 Messaggio mancante per il codice di errore %1$d.
6 ... ANNULLA/ESCI IMMEDIATAMENTE ...\n
7 Vuoi uscire? 1/0
8 File sconosciuto.
9 Nessun messaggio disponibile; questo messaggio è stato sostituito con il messaggio originale erroneamente formattato.
10 er_init: *** Valore errato di exit_ask = %1$d; assumerà invece %2$s. ***\n
11 er_start: *** ATTENZIONE: Impossibile aprire il messaggio di log "%1$s"; assumerà invece stderr. ***\n
12 er_study_fmt: messaggio sospetto per il codice di errore %1$d.
13 er_estimate_size: codice di conversione sconosciuto (%1$d).
14 \n\n*** Wraparound di log dei messaggi.  I messaggi continueranno nella parte superiore del file. ***\n\n
15 \nOra: %1$s - %2$s *** %3$s CODICE = %4$d, Tran = %5$d%6$s\n%7$s\n
16 CUBRID (pid: %1$d) *** %2$s *** %3$s CODICE = %4$d, Tran = %5$d %6$s
17 \nOra: %1$s - %2$s *** file %3$s, linea %4$d %5$s CODICE = %6$d Tran = %7$d%8$s\n%9$s\n
18 CUBRID (pid: %1$d) *** %2$s *** file %3$s, linea %4$d, %5$s CODICE = %6$d, Tran = %7$d. %8$s
19 \n*** Il messaggio di errore precedente è l'ultimo. ***\n\n
20 \nOra: %1$s - DEBUG *** file %2$s, linea %3$d\n
21 Mail -s "Server è stato fermato"
22 --->>>\n %1$s è stato fermato su vostra richiesta quando l'errore seguente è stato fissato:\n errid = %2$d, %3$s\nUtente: %4$s, pid: %5$d, host: %6$s, ora: %7$s<<<---\n
23 %1$s è stato fermato su on errid = %2$d. User: %3$s, pid: %4$d
24 er_init: non è possibile installare handler per evento "%1$s"\n

$set 7 MSGCAT_SET_PARSER_SYNTAX
1 si prega di controllare la sintassi a '%1$s', aspettandosi %2$s.
2 si prega di controllare la sintassi a '%1$s', aspettandosi %2$s nel %3$s.
3 si prega di controlare la correttezza del token '%1$s'.
4 'read uncommitted schema' non è accettabile, usando\n 'read committed schema' al posto.
5 Massima lunghezza della stringa di caratteri è 16383.
6 %1$s supera il limite di %2$lld.
7 La lunghezza massima della stringa di bit è %1$d.
8 Non è possibile utilizzare una sottoquery nella condizione di clausola JOIN.
9 Non è possibile specificare l'attributo "(%1$s)" nella clausola di aggiornamento dei privilegi dell'instruzione grant/revoke.
10 Uso di parola chiave '%1$s' non valido. Aspettavo %2$s.
11 Lunghezza del token è troppo lungo.
12 Tipo di dati non valido.
13 Istruzione non valida "attach statement"\n  attach_statement: ATTACH unsigned_integer
14 Istruzione non valida "prepare statement"\n  prepare_statement:  PREPARE {to} COMMIT unsigned_integer
15 Istruzione non valida "execute statement"\n  execute_statement:  EXECUTE DEFERRED TRIGGER [identifier {, identifier}*|ALL TRIGGERS]
16 Istruzione non valida "scope statement"\n  scope_statement: SCOPE trigger_action { FROM table_spec_list }
17 Istruzione non valida "set system parameter"\n  set_system: SET SYSTEM PARAMETERS 'STRING' {, 'STRING_N'}*
18 Istruzione non valida "set transaction"\n  set_transaction: SET TRANSACTION ISOLATION LEVEL expression {, expression}\n                |SET TRANSACTION LOCK TIMEOUT timeout_spec
19 Istruzione non valida "set trigger trace"\n  set_trigger: SET TRIGGER TRACE [ON|OFF|unsigned_integer|parameter|host_param]
20 Istruzione non valida "set trigger depth"\n  set_trigger: SET TRIGGER {MAXIMUM} DEPTH [unsigned_integer|parameter|host_param]
21 Istruzione non valida "set optimization level"\n  SET OPTIMIZATION LEVEL {=|to} [ON|OFF|unsigned_integer|parameter|host_param]
22 Istruzione non valida "set optimization cost"\n  SET OPTIMIZATION COST {of} 'STRING' {=|to} literal_
23 Istruzione non valida "get optimization level"\n  GET OPTIMIZATION LEVEL {[INTO|TO] [host_param|parameter|identifier]}
24 Istruzione non valida "get optimization cost"\n  GET OPTIMIZATION COST {of} 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
25 Istruzione non valida "get transaction isol"\n  GET TRANSACTION ISOL LEVEL {[INTO|TO] [host_param|parameter|identifier]}
26 Istruzione non valida "get transaction lock"\n  GET TRANSACTION LOCK TIMEOUT {[INTO|TO] [host_param|parameter|identifier]}
27 Istruzione non valida "get trigger trace"\n  GET TRIGGER TRACE {[INTO|TO] [host_param|parameter|identifier]}
28 Istruzione non valida "get trigger depth"\n  GET TRIGGER {MAXIMUM} DEPTH {[INTO|TO] [host_param|parameter|identifier]}
29 Istruzione non valida "get stat"\n  GET STATISTICS 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
30 Istruzione non valida "create index"\n  CREATE {REVERSE} {UNIQUE} INDEX identifier ON {only} class_name index_column_name_list {WHERE where_clause}
31 Istruzione non valida "create user"\n  CREATE USER identifier {PASSWORD 'STRING'} {GROUPS identifier {, identifier}* } {MEMBERS identifier {, identifier}* }
32 Istruzione non valida "create trigger"\n  CREATE TRIGGER identifier {STATE [ACTIVE|INACTIVE]} {PRIORITY unsigned_real}\n    [BEFORE|AFTER|DEFERRED] event_spec {IF trigger_condition}    EXECUTE {AFTER|DEFERRED} trigger_action
33 Istruzione non valida "create serial"\n  CREATE SERIAL identifier {START WITH integer} {INCREMENT BY integer}\n    serial_min_max {CYCLE|NOCYCLE} {CACHE unsigned_integer|NOCACHE}
34 Istruzione non valida "create procedure"\n  CREATE PROCEDURE identifier '(' {sp_param_list} ')' {IS|AS} LANGUAGE JAVA NAME 'STRING'
35 Istruzione non valida "create function"\n  CREATE FUNCTION identifier '(' {sp_param_list} ')' RETURN {data_type|CURSOR} {IS|AS} LANGUAGE JAVA NAME 'STRING'
36 Istruzione non valida "groups"\n  groups spec: GROUPS identifier {, identifier}*
37 Istruzione non valida "members"\n  member spec: MEMBERS identifier {, identifier}*
38 Istruzione non valida "password"\n  password spec: PASSWORD 'STRING'
39 manca l 'elenco dei comandi autorizzati\n  authorized command = SELECT, INSERT, INDEX, DELETE, UPDATE, ALTER, ADD, DROP, EXECUTE, REFERENCES, ALL PRIVILEGES, ALL 
40 Manca l'elenco dei specificazioni della classe
41 Manca l'elenco d'identificatori
42 Manca espressione
43 identificatore non valido
44 uso non valido di substring\n  SUBSTRING( expression FROM expression FOR expression )\n  SUBSTRING( expression FROM expression )
45 uso non valido di trim\n  TRIM ( [LEADING|TRAILING|BOTH] expression FROM expression )\n  TRIM ( [LEADING|TRAILING|BOTH] FROM expression )  TRIM ( expression FROM expression )  TRIM ( expression )
46 uso non valido di cast\n  CAST ( expression AS data_type )
47 function add_months ha bisogno di due parametri
48 octet_length ha bisogno di un parametro
49 bit_length ha bisogno di un parametro
50 lower ha bisogno di un parametro
51 upper ha bisogno di un parametro
52 translate ha bisogno di tre parametro
53 replace ha bisogno di due o tre parametro
54 uso non valido di to_number\n  TO_NUMBER ( expression , char_string )\n  TO_NUMBER ( expression )
55 operatore di stringa di concatenazione non è '|' ma '||'
56 Istruzione "alter" non valida
57 Istruzione "drop" non valida
58 Istruzione "create" non valida
59 operatore relazionale non valido
60 operatore di uguaglianza non è '==' ma '='
61 operatore di disuguaglianza non è '!=' ma '<>'
62 valore negativo non valido(o il valore negativo nella lista)
63 regola duplicata di riferimento
64 SYS_CONNECT_BY_PATH ha bisogno di due parametri e il secondo deve essere una stringa letterale.
65 intero senza segno %1$s non valido.
66 IF ha bisogno di tre parametri: il primo deve essere un'espressione logica, il secondo e il terzo devono essere espressioni valutando sia ad un valore numerico o un tipo stringa.
67 IFNULL ha bisogno di due parametri che devono essere espressioni valutando sia un valore numerico o un tipo stringa.
68 uso non valido di "isnull"\n  ISNULL ( expression )
69 uso non valido di "left"\n  LEFT ( str , len )
70 uso non valido di "right"\n  RIGHT ( str , len )
71 uso non valido di "mod"\n MOD( expression, expression )
72 uso non valido di "adddate"\n ADDDATE( expression, expression )
73 uso non valido di "date_add"\n DATE_ADD( expression, INTERVAL expression unit)
74 uso non valido di "subdate"\n SUBDATE( expression, expression )
75 uso non valido di "date_sub"\n DATE_SUB( expression, INTERVAL expression unit)
76 uso non valido di "user"/"system_user"\n USER()/SYSTEM_USER()
77 uso non valido di "timestamp"\n TIMESTAMP( expression ), TIMESTAMP( expression, expression )
78 uso non valido di "database"\n DATABASE()
79 uso non valido di "current_time"\n CURRENT_TIME()
80 uso non valido di "current_date"\n CURRENT_DATE()
81 uso non valido di "current_timestamp"\n CURRENT_TIMESTAMP()
82 uso non valido di "now"\n NOW()
83 uso non valido di "default"\n DEFAULT(column_name)
84 uso non valido di "date"\n DATE(expression)
85 uso non valido di "localtime"\n LOCALTIME()
86 uso non valido di "localtimestamp"\n LOCALTIMESTAMP()
87 uso non valido di "schema"\n SCHEMA()
88 uso non valido di "truncate"\n TRUNCATE( expression, expression )
89 uso non valido di "str_to_date"\n STR_TO_DATE( expression, string_literal )
90 uso non valido di "current_datetime"\n CURRENT_DATETIME()
91 uso non valido di "insert"\n INSERT( expression, expression , expression, expression )
92 uso non valido di "elt"\n ELT(expression [, expression]...)
93 uso non valido di "year"\n YEAR( expression )
94 uso non valido di "month"\n MONTH( expression )
95 uso non valido di "day"\n DAY( expression )
96 uso non valido di "hour"\n HOUR( expression )
97 uso non valido di "minute"\n MINUTE( expression )
98 uso non valido di "second"\n SECOND( expression )
99 uso non valido di "quarter"\n QUARTER( expression )
100 uso non valido di "week"\n WEEK( expression ), WEEK( expression, expression)
101 uso non valido di "time"\n TIME(expression)
102 uso non valido di "group_concat"\n GROUP_CONCAT (([DISTINCT] {col|expr}\n  [ORDER BY {col|unsigned_int} [ASC|DESC] ] \n [SEPARATOR str_val])
103 uso non valido di "index hint"
104 Lunghezza massima stringa di byte è %1$d byte.
105 Errore di sintassi: unexpected
106 , expecting '{'
107 , expecting SELECT or '('
108 , expecting
109 unexpected
110 END OF STATEMENT
111 In line %d, column %d before '%c%s'\n
112 In line %d, column %d before %s\n
113 before ' %1$s'\n
114 before END OF STATEMENT\n
115 utilizzo non valido del set names\n  set_names: SET NAMES charset [COLLATE collation]\n
116 utilizzo non valido del chr\n  CHR ( number [using charset] )\n
117 utilizzo non valido del charset\n CHARSET ( expression )\n
118 utilizzo non valido del collation\n COLLATION ( expression )\n
119 utilizzo non valido del clob_to_char\n CLOB_TO_CHAR ( expression [using charset] )\n
120 utilizzo non valido del index_prefix\n INDEX_PREFIX ( expression, expression, expression )\n
121 Uso scorretto di UNION e ORDER BY
122 Expecting ON. Outer Joins require join condition.

$set 8 MSGCAT_SET_PARSER_SEMANTIC
1 Memoria esaurita.
2 Funzione %1$s non è definita.
3 Impossibile trovare LDB '%1$s' nel database.
4 Il numero di colonne, %1$d, nella query di sinistra non corrisponde\nal numero di colonne, %2$d, nella query a destra.
5 '%1$s' non è unione-compatibile con '%2$s'.
6 %1$s non è definito.
7 Il dominio di %1$s non può essere un vclass.
8 Il dominio di %1$s non può essere un vclass proxy.
9 Il dominio di %1$s non può essere una classe.
10 %1$s non è un attributo di %2$s.
11 %1$s non può essere modificato da questa classe.\nE 'stato ereditato da %2$s.
12 Classe %1$s non esiste.
13 %1$s non è un %2$s.
14 %1$s non è un metodo o un attributo di %2$s.
15 %1$s non ha e non deve avere alcuna specificazione della query.
16 %1$s non è definito nel database.
17 %1$s non è una superclasse di %2$s.
18 Classe %1$s eredita dalla non classe %2$s.
19 VClass %1$s eredita dal non vclass %2$s.
20 Vclass proxy %1$s eredita dal vclass non proxy %2$s.
21 %1$s è già una superclasse di %2$s.
22 %1$s non può essere una superclasse di se stesso.
23 %1$s è già una sottoclasse di %2$s.
24 %1$s non è valore singolo. Attributi esposti in query\ndi aggregazione devono inoltre figurare nella clausola GROUP BY.
25 Il numero di colonne (%1$d) e into_names (%2$d) non sono uguali.
26 Classe %1$s esiste già.
27 Superclasse %1$s nel '%2$s' non esiste.
28 In attesa di una clausola SUBCLASS per rendere significativa la clausola INHERIT '%1$s'.
29 Clausola INHERIT '%1$s' fa riferimento a una classe che non è nel clausola SUBCLASS '%2$s'.
30 Classe non può avere una specificazione della query.
31 %1$s non è un LDB registrato.
32 Un erede delegato non può avere la sua propria clausola di object_id. Non si può sovrascrivere sui attributi ereditati dall'object_id.
33 Vclass proxy ha più di una specificazione della query.
34 Il numero di attributi, %1$d, non corrisponde al numero di colonne, \n%2$d, nella specificazione della query.
35 Manca un nome di attributo previsto per %1$s.
36 L'attributo '%1$s' non è compatibile con la colonna '%2$s' della specificazione della query.
37 %1$s non è una classe.
38 Gli indici non possono essere creati su classi virtuali.
39 Indici possono essere creati solo su un singolo attributo.
40 Utilizzo non valido di più classi nella clausola FROM.
41 Errore di sistema - Risoluzione non riuscita su %1$s.
42 L'utente non può eliminare da una tabella derivata.
43 utente %1$s non è nel database.
44 Aggiornamento non può avere l'aggregazione.
45 I nomi degli attributi di ciascuna classe devono essere unici. Nome dell'attributo '%1$s' nella classe '%2$s' viene specificato più di una volta.
46 Ci sono più assegnazioni per %1$s.%2$s.
47 Numero nella specificazione di ordinamento non è un numero intero.
48 %1$d nella specificazione di ordinamento è fuori portata.
49 Oggetto nell'ORDER BY non è un numero intero.
50 %1$s nella specificazione di ordinamento non corrisponde a nessuna colonna di selezione.
51 Esistono indicazioni di ordinamento in conflitto per colonna %1$s.
52 %1$s nella specificazione di ordinamento non è un'espressione di numero, nome o percorso.
53 Espressione d'inserire può apparire solo nell'instruzione INSERT.
54 Il numero di attributi (%1$d) e delle colonne di selezione (%2$d) non sono uguali.
55 Il numero di attributi (%1$d) e dei valori (%2$d) non sono uguali.
56 Overflow dei dati sul tipo di dati %1$s.
57 Tipo di dati incompatibile sull'attributo %1$s.
58 %1$s\n non è un SELECT da una sola classe.
59 `*' Non è un elenco valido di selezione nel una specificazione della query di vclass proxy.
60 %1$s non è né un vclass né un vclass proxy.
61 C'è più di una clausola ISOLATION LEVEL.
62 C'è più di una clausola TIMEOUT.
63 Nessun livello di isolamento è stato inserito.
64 Operatore BETWEEN non è definito su tipi %1$s, %2$s e %3$s.
65 Operatore '%1$s' non è definito sui tipi %2$s e %3$s.
66 Funzione deve avere argoment: %1$s.
67 Operandi incompatibili: %1$s(%2$s).
68 Funzione di aggregazione deve avere 1 argomento: %1$s.
69 Tentativo di dividere per zero.
70 Overflow dei dati alla coercizione di %1$s al tipo %2$s.
71 Coercizione di %1$s al tipo %2$s non è possibile.
72 Il tipo di '%1$s' deve essere SET, MULTISET, o SEQUENCE.
73 '%1$s' dovrebbe avere esattamente un tipo di elemento d'insieme.
74 %1$s ha più di un nome di colonna derivata.
75 %1$s non ha un nome di colonna derivata.
76 %1$s non ha un numero delle colonne corrispondente(%2$d) e nomi di colonna (%3$d).
77 Non ci sono dei attributi nel %1$s.
78 In attesa di una singola tabella nel %1$s.
79 Nome esposto %1$s è ambiguo.
80 Nomi delle colonne derivati '%1$s' non sono ammessi per le specificazioni semplici delle classe.
81 Tabella derivata '%1$s' deve avere un nome di correlazione.
82 Errore di selezionare dell'autorizzazione, classe %1$s.
83 Referenza a l'istanza della classe %1$s a un nome di tabella derivata non è permesso.
84 Correlazione di percorso (%1$s) appare all'esterno di un termine interiore di percorso.
85 Riferimento a %1$s è ambiguo.
86 Classe di %1$s non ha l'attributo %2$s.
87 Classi (%1$s) non tutti hanno l'attributo %2$s.
88 Classe (%1$s) non ha l'attributo %2$s.
89 %1$s è un tipo %2$s, non un tipo di oggetto.
90 Selettore {%1$s} non risolve ad una specifica classe esposta nel\n contesto necessario (%2$s).
91 Selettore {%1$s} si riferisce alla non-classe specifica (%2$s).
92 Selettore {%1$s} non può fare riferimento alla radice dell'espressione di percorso si definisce.
93 Selettore {%1$s} è circolarmente definito in termini di selettore {%2$s}.
94 Selettore {%1$s} viene ridefinito a %2$s.
95 %1$s (oggetto di dominio) non ha l'attributo %2$s.
96 %1$s nel specificazione EXCEPT non è una sottoclasse.
97 Lista dell'instruzione ALL ... EXCEPT esclude TUTTO della classe.
98 '%1$s' è incompatibile con altro uso di\nselettore del percorso '%2$s' o di percorso principale '%3$s'.
99 Argomento non definito.
100 L'argomento non è un oggetto.
101 %1$s non è un metodo o un attributo della classe di %2$s.
102 Specificazione della query non deve avere un clausola INTO.
103 Specificazione della query proxy non deve avere un'espressione di aggregazione,\nnessuna clausola GRUOP BY, e nessuna clausola HAVING.
104 Specificazione della query proxy non deve avere la parola \nchiave DISTINCT, né UNIQUE nell'elenco di selezione.
105 Specificazione della query proxy deve essere una semplice istruzione SELECT.
106 Specificazione della query proxy deve fare riferimento a una sola tabella o classe dal database locale.
107 Utente '%1$s' non è il proprietario di %2$s.
108 Proxy '%1$s' è già stato definito sulla classe '%2$s' di LDB '%3$s'.
109 CUBRID non supporta il proxy con gli oggetti nei gruppi eterogenei.
110 Clausola WHERE deve essere un'espressione di tipo logico.
111 Token di escape della clausola LIKE deve essere una stringa letterale o una variabile host.
112 Combinazione non valida di precisione (%1$d) e scala (%2$d).
113 Precisione indicate di %1$d non è valido, ma dovrebbe essere maggiore di %2$d e non superiore a %3$d.
114 Di precisione (%1$d) troppo grande. Precisione massima è di %2$d.
115 Metodo %1$s deve avere un tipo di ritorno.
116 Numero di argomenti forniti al metodo non è uguale al numero \ndefinito per il metodo.
117 Chiamata al metodo deve avere un target.
118 Metodo %1$s non è definito per il target fornito.
119 I metodi di istanza non possono essere chiamati su gli oggetti di classe.
120 I metodi di classe non possono essere chiamati su gli oggetti d'istanza.
121 I metodi di classe richiedono un'oggetto di classe come loro target.
122 Metodi richiedono un'oggetto come loro target.
123 Metodo %1$s non è completamente tipizzato, controlla definizione di metodo nello schema di classe.
124 underflow di aritmetica per la data.
125 underflow di aritmetica per il tempo.
126 Lato sinistro di una clausola di assegnazione non è valida.
127 Le chiamate a metodo su tabelle derivati, correlati a un scopo esterno,\nsono supportate solo se l'espressione dell'insieme è fasciata nel una sottoquery.
128 La colonna di specificazione della query che corrisponde\nall'attributo shared '%1$s' deve essere NULL o NA.
129 Una colonna NULL non è permessa nel l'elenco di selezione della specificazione della query del vista se il definizione di attributo viene omesso.
130 Oggetti Vclass nel'insiemi non sono supportati.
131 In attesa di un valore stringa.
132 In attesa di un'istruzione o un valore di stringa.
133 L'attributo object_id proxy '%1$s' non deve essere mappato\na una colonna di specificazione d'una espressione di una query.
134 Vincolo %1$s ignorato.
135 Dichiarazioni di vincolo non sono ammessi in questo contesto.
136 Conversione di tipo di %1$s al tipo %2$s non è supportata.
137 %1$s non è una stringa di bit valido.
138 Operatore '%1$s' non è definito sul tipo %2$s.
139 Operatore '%1$s' non è definito su tipi %2$s con %3$s e %4$s.
140 Operandi di operatore '%1$s' non possono essere entrambi variabili host dinamici.
141 Troppo molti argomenti per funzione generica %1$s.\nSolo %2$d gli argomenti sono permessi per le funzioni generiche.
142 Selettore {%1$s} di classe '%2$s' non è una sottoclasse di '%3$s'.
143 Impossibile estrarre campo '%1$s' dal tipo %2$s.
144 Specificazione della query non deve avere una clausola ORDER BY.
145 Condizione di CASE deve essere un'espressione di tipo logico.
146 L'espressione deve essere di tipo %1$s.
147 Serial '%1$s' esiste già.
148 Serial '%1$s' non definito.
149 Valore di incremento non deve essere zero.
150 Valore massimo supera 1.0e37.
151 Il valore minimo non è valido.
152 Valore di incremento non è valido.
153 Valore minimo supera -1.0e36.
154 Il valore massimo non è valido.
155 Specificate l'opzione per ALTER SERIAL.
156 Il secondo argomento della funzione serial_next_value dovrebbe essere maggiore di 0.
157 I valori seriali non possono supera il minimo o massimo.
158 Errore I/O interni per serial.
159 Impossibile trovare il index '%1$s'.
160 Il nome qualificato dell'index '%1$s' si riferisce a una classe, ma non è specificato nella clausola FROM.
161 Due o più classi hanno lo stesso index di '%1$s'. Utilizzate i nomi qualificati dei index.
162 Utilizzo non valido di %1$s.\n Espressione %2$s non può essere utilizzato con attributo o una subquery correlata.
163 La versione attuale limita un'espressione contenente ORDERBY_NUM() nella lista di selezione.
164 Espressione del percorso '%1$s' nella condizione di JOIN.
165 '%1$s' nel condizione di JOIN non è definita.
166 La versione attuale limita dei tabelle derivati dell'insieme per il RIGHT OUTER JOIN.
167 Non è possibile inserire con query su altro LDB.
168 Una sola colonna può essere specificata nell'elenco di selezione quando la sottoquery non è introdotta da EXISTS.
169 Lato destro non permessa per una clausola di assegnazione. Contesto richiede query per essere singola colonna.
170 Non è possibile utilizzare una sottoquery nella condizione di clausola JOIN.
171 Definizione di partizione non valida.
172 Colonna di partizione deve essere specificato.
173 Solo una colonna può essere specificata come colonna di partizione.
174 Tipo di colonna dipartizione non supportato.
175 Definizione della partizione è duplicato. '%1$s'
176 Discordanza di tipo costante. '%1$s'
177 Dimensione della partizione non valido.
178 '%1$s' è una classe non partizionata.
179 '%1$s' è una classe già partizionato.
180 Richieste di partizione non valide.
181 %1$s non può essere modificato da questa classe (colonna di chiave di partizione).
182 %1$s non può essere gestita come una partizione sotto trigger attivi.
183 Partizione %1$s non esiste.
184 Discordanza di tipo di partizione. '%1$s'
185 Informazioni di partizione non sono validi.
186 Aggiunta del partizione può essere fatto solo con una gamma più grande di quello esistente.
187 Riorganizzazione delle partizioni non è permesso perché può causare la perdita di dati.
188 La gamma del partizione '%1$s' non è valida.
189 Un attributo di classe non può avere un vincolo di incremento automatico.
190 Un attributo di classe virtuale non può avere un vincolo di incremento automatico.
191 Il dominio dell'attributo '%1$s' con un vincolo di incremento automatico non è valido.
192 Non un utente, ma il sistema può assegnare l'attributo '%1$s' creato da 'ON CACHE OBJECT' di chiave esterna.
193 Non è possibile utilizzare i nomi duplicati degli attributi nell'elenco delle chiave di index. Nome dell'attributo '%1$s' elencato più di una volta.
194 Funzione %1$s non è definito o ha il numero sbagliato di parametri.
195 '%1$s' può essere utilizzato solo nelle query gerarchiche. Specificate una clausola CONNECT BY.
196 '%1$s' non è permesso qui.
197 Utilizzando CONNECT_BY_ISCYCLE è permesso soltanto se la query ha una clausola CONNECT BY NOCYCLE.
198 Nessuna tabella utilizzata.
199 ORDER BY NULL richiede GROUP BY.
200 Il numero a CACHE deve essere meno di un ciclo (%1$s).
201 '%1$s' non è permesso in una query con clausola LIMIT.
202 WITH ROLLUP richiede GROUP BY.
203 Un parametro nome di colonna deve essere passato alla funzione DEFAULT. Le espressioni di percorso non sono ancora supportati.
204 L'opzione '%1$s' della tabella è duplicata.
205 La classe '%1$s' viene contrassegnata come REUSE_OID e non è riferibile. Le classi non riferibili non possono essere il dominio di un attributo e loro istanze OID non possono essere ritornate.
206 L'opzione serial %1$s' e duplicato.
207 La colonna '%1$s' nella clausola di ordinamento è ambiguo.
208 L'attributo '%1$s' non ha un valore predefinito.
209 Elementi ORDER BY devono apparire nell'elenco di selezione, se si specifica SELECT DISTINCT.
210 Variabile host non è permessa dalle colonne della specificazione della query.
211 %1$s non può essere una colonna ORDER BY
212 %1$s non può essere un colonna GROUP BY
213 Impossibile definire '%1$s di %2$s' come tipo di colonna.
214 Valore non valida "%1$s" per la lunghezza del prefisso dell'index.
215 Funzione '%1$s' non è definita su tipi %2$s, %3$s, %4$s e %5$s.
216 Funzione '%1$s' non è definita su tipi %2$s, %3$s e %4$s.
217 Funzione '%1$s' non è definita sui tipi %2$s e %3$s.
218 Funzione '%1$s' non è definita sul tipo %2$s.
219 Funzione '%1$s' non è definita per un argomento dell'index di tipo %2$s.
220 ALTER VIEW '%1$s' AS SELECT non è permesso per le viste che hanno sottoclassi o superclassi. Utilizzate ALTER VIEW CHANGE QUERY al posto.
221 Attributi della vista non possono essere modificati utilizzando questa dichiarazione. 'ALTER VIEW name AS QUERY' potrebbe aiutare invece.
222 CUBRID richiede l'elemento ORDER BY di essere la stessa espressione come l'argomento di GROUP_CONCAT o un numero intero (posizione di argomento).
223 Il predicato LIKE richiede un carattere di escape da specificare. Vedeti la documentazione per '%1$s' e i parametri di configurazione '%2$s'.
224 KEYLIMIT non può essere utilizzato nella clausola INDEX NONE.
225 Le variabili di sessione non sono permesse nel questo contesto.
226 Conflitto tra hint dell'index '%1$s' e '%2$s'.
227 Funzione '%1$s' non può essere utilizzata nella clausola DEFAULT.
228 Aggiornamento delle tabelle derivate non è permesso.
229 Aggiornamento dei attributi di classe e gli attributi dell'istanza non è permesso nella stessa istruzione.
230 Aggiornamento delle gerarchie di classi virtuali non è permesso.
231 Le variabili host non sonno permesse nel un'istruzione DDL.
232 Espressione di filtro non valida (%1$s) per index.
233 Funzioni multiple o nidificati non sono permessi nel una definizione di index di funzione.
234 Specificazione di index non valida.
235 SHARED, DEFAULT e AUTO_INCREMENT non possono essere definiti uno con l'altro.
236 Troppi valori di enumerazione (%1$d). Un tipo di enumerazione può avere un massimo di %2$d valori.
237 Valori duplicati per il tipo enum.
238 MERGE impossibile in una tabella derivata.
239 Non è possibile modificare la tabella di origine nel un'istruzione MERGE.
240 MERGE con gerarchie di classi non è permesso.
241 Nome di utente è troppo lungo.
242 Espressione costante non può essere utilizzata come index di funzione.
243 Nome della classe o l'alias non è unico nella lista: '%1$s'
244 Clausola DEFAULT non valida. '%1$s' non può essere utilizzato nel una espressione nidificata.
245 '%1$s' richiede argomenti con regole di confronto compatibili.
246 Insieme di caratteri Non valido.
247 Insieme di caratteri e regole di confronto richiesti non sono compatibili.
248 Regole di confronto '%1$s' sconosciuto.
249 Contesto richiede un insieme con elementi che hanno dei codeset e di regole di confronto identici.
250 Index di prefisso non è ammesso per l'attributo '%1$s' (ha regola di confronto con espansioni).
251 Il valore iniziale non è valido.
252 Index multi-colonna di prefisso non è permesso.
253 La funzione '%1$s' non può essere utilizzata per index di funzione.
254 Definizione non valida della colonna dell'index: %1$s
255 '%1$s' non è ammesso nel un'espressione di filtro per index.
256 Impossibile utilizzare GROUPBY_NUM() e clausola WITH ROLLUP nella stessa query.
257 Impossibile MERGE UPDATE della colonna %1$s fatto riferimento nella clausola ON.
258 Il numero di colonne, %1$d, nel ( %2$s ) non corrisponde\nal numero di colonne, %3$d, nel ( %4$s ).
259 Prefissi di colonne non sonno permessi nel un'espressione d'index di funzione.
260 %1$s non può essere una colonne di PARTITION BY o ORDER BY.
261 Query deve restituire un valore singolo\n(%1$s).
262 Funzioni analitiche nidificati non sono ammessi.
263 Un attributo ereditato non può essere utilizzato come chiave di partizionamento.
264 La dimensione aggregata degli elementi di enumerazione è troppo grande.
265 Impossibile utilizzare la tabella di destinazione nella specificazione di origine.
266 COLLATE non è consentito in questo contesto.
267 COLLATE modifier non può cambiare codeset (from '%1$s' to '%2$s'). Provate ad usare CAST invece.
268 Codeset '%1$s' non valido per il valore di partizione. In attesa di codeset '%2$s'.
269 Variabile di sessione non può avere regole di confronto diverse rispetto delle regole di confronto del sistema ('%1$s').
270 Non valido espressione di funzione indice.
271 La specifica query %1$s ha un riferimento ciclico a se stesso.
272 Espressione %1$s non consentito in condizione di join.
273 ORDER BY non è consentito in funzione MEDIAN.
274 Gli argomenti della funzione '%1$s' deve essere costanti.
275 Funzione '%1$s' deve avere la clausola ORDER BY.
276 Per la funzione '%1$s', il numero di argomenti non corrisponde al numero di colonne ORDER BY.
277 Per la funzione '%1$s', la clausola ORDER BY non è valida.
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
288 Cannot remove all partitions, use DROP TABLE instead.
289 The expression including %1$s is not allowed in this context.
290 Transaction id is not an integer.
291 Subquery is not allowed in ORDER BY FOR clause.

$set 9 MSGCAT_SET_PARSER_RUNTIME
1 Fuori di memoria virtuale: Impossibile allocare %1$d byte.
2 Livello di isolamento di transazione deve essere 1, 2, 3, 4, 5 o 6.
3 Il valore di timeout deve essere -1, 0 o >0.
4 Livello di isolamento impostato a:\n
5          Unknown isolation.\n
6          Unknown isolation.\n
7          Unknown isolation.\n
8          READ COMMITTED\n
9          REPEATABLE READ\n
10 Un query non-aggiornabile non è permesso nel una chiamata db_query_execute_oid.
11 Transazione scaduto in attesa per i blocchi.
12 Tentativo di accesso variabile host con indice %1$d in cui solo\n%2$d variabili host di input sono stati dati.
13 Conversione non implementata.
14 Errore interno, conversione non definito.
15 '%1$s' produce alcun risultato.
16 '%1$s' produce più di una riga.
17 Variabile %1$s d'interpreter non è definita.
18 Non può valutare '%1$s'.
19 Variabile sconosciuta %1$s.
20 Attributo shared sconosciuto %1$s
21 '%1$s' produce più di una colonna.
22 Uso nidificato o non valido di funzione di aggregazione.
23 Funzione %1$s non è definita.
24 Contesto richiede query per essere singola colonna\n(%1$s).
25 Troppi valori sono stati specificati.
26 Target di assegnazione virtuale (%1$s) non è invertibilo.
27 Ci sono più colonne nella specificazione della query che attributi nella classe %1$s.
28 Ci sono più attributi nella classe %1$s che colonne nella specificazione della query.
29 La versione attuale limita aggregazione per vclasses SENZA \nspecificazioni della query dell'aggregazione.
30 La versione attuale limita aggregazione per vclasses SENZA \nspecificazioni della query del unione.
31 Uso di vclass (%1$s) in una gerarchia di composizione richiede che sia aggiornabile.\nNon può essere aggiornate per quanto riguarda di vera classe %2$s.
32 non usato
33 Costrutto illegale nell'espressione di percorso '%1$s'.
34 Espressione di percorso '%1$s' attraversa db limite a %2$s, con vclass %3$s.
35 Espressione di percorso '%1$s' attraversa db limite da %2$s, alla classe %3$s.
36 %1$s non è autorizzato sul %2$s.
37 Select non è autorizzato sul %1$s.
38 Vclass %1$s non è aggiornabile.
39 %1$s non è un vclass proxy.
40 Specificazione della query di vclass proxy non deve essere un JOIN.
41 %1$s non ha una ID oggetto.
42 Classi (%1$s) con più campi object_id nel una gerarchia di \ncomposizione non sono supportati.
43 Attributo dell'oggetto %1$s di %2$s non è un vclass su LDB %3$s.
44 Tipo di specificazione della query non è compatibile con attributo %1$s
45 La specificazione della query di %1$s ha un riferimento ciclico a se stesso.
46 Nessuna espressione dato da valutare.
47 Nessuna classe vera trovata per vclass %1$s.
48 Esecuzione di '%1$s' su LDB '%2$s' \ntornato: %3$s.
49 Mettere valore '%1$s' nel attributo '%2$s' tornato: %3$s.
50 Nome dell'LDB non deve essere nullo nel %1$s.
51 Nome dell'LDB a host non deve essere nullo in %1$s.
53 Nome host non deve essere nullo in %1$s.
54 Nome utente non deve essere nullo in %1$s.
55 Password utente non deve essere nullo in %1$s.
56 Inserire si traduce in un'istruzione vuota.
57 Aggiorna traduce in un'istruzione vuota.
58 Elimina si traduce in un'istruzione vuota.
59 INGRES non supporta le date (%1$s) prima del 1 gennaio 1582.
60 Risorse esaurisce nella generazione della query.
61 Query produce alcun risultato, quindi variabile '%1$s' non è impostata.
62 L'opzione di controllo WITH LOCAL non è supportato.
63 Eccezione dell'opzione CHECK sulla vista %1$s.
64 Data non valida: '%1$s'.
65 Ora non valida: '%1$s'.
66 Non valido utime: '%1$s'.
67 Le sottoquery nel specificazione della query proxy non sono supportati.
68 Argomento della funzione dell'LDB non è compatibile con LDB in '%1$s'.
69 Numerico non valido: '%1$s'.
70 Ottimizzazione delle query OUTER JOIN non è riuscita.
71         SERIALIZABLE.\n
72 Vclass %1$s non è aggiornabile. Solo le vclasse aggiornabile sono autorizzati a selezionare VID.
73 Partizionamento non è riuscita.
74 Partizione non esiste.
75 Non è permesso l'accesso a partizioni: '%1$s'.
76 Il serial '%1$s' non è ancora definito.
77 Il proprietario o DBA può modificare la definizione di target di serial.
78 Non é permesso agli utenti di modificare l'oggetto serial creato da un vincolo di incremento automatico.
79 Livello di isolamento non valido, schema %1$s, istanze %2$s.
80 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.


$set 11 MSGCAT_SET_AUTHORIZATION
1 *** cache non valido ***
2    classe %1$s
3 da %1$s :
4 Autorizzazione per l'utente %1$s\n
5 Autorizzazione per un utente non definito\n
6 Utente : %1$s\n
7            id : %1$d\n
8        membri :
9        gruppi :
10 Utente %1$s\n
11 Utente attuale è: %1$s\n
12 Oggetto di autorizzazione Root:\n
13   gli utenti :
14 *** Impossibile rilasciare tutti i diritti per utente "%1$s"*** \n
15 AUTORIZZAZIONI\n
16 direct_groups :

$set 12 MSGCAT_SET_HELP
1 Rootclass
2 %1$s %2$s\n
3 Superclassi:\n
4 Sottoclassi:\n
5 Attributi:\n
6 Metodi:\n
7 Attributi di classe:\n
8 Metodi di classe:\n
9 Risoluzioni del conflitto:\n
10 File di metodo:\n
11 Specificazione della query:\n
12 OGGETTO di %1$s\n
13 Descrizione:\n
14 Struttura:\n
15 Esempio:\n
16 Nome di META CLASS
17 Nome di classe
18 Nome di VClass
$  Don't delete the space at the end of the next message
19 VClass su LDB
20 GENERALE

$set 13 MSGCAT_SET_TRIGGER
1 TRACE: Valutare condizione per trigger '%1$s'.
2 TRACE: Esecuzione di azione per trigger '%1$s'.

$set 14 MSGCAT_SET_LOCK
1 \n
2 Thread (indice %1$3d) della transazione (indice %2$3d, %3$s, %4$s@%5$s|%6$d) è stato sospeso.\n
3 Thread (indice %1$3d) della transazione (indice %2$3d, %3$s, %4$s@%5$s|%6$d) è stato ripreso.\n
4 Indice di transazione %1$3d ACQUIRED %2$s blocco sull'oggetto %3$d|%4$d|%5$d.\n
5 Indice di transazione %1$3d ACQUIRED %2$s blocco a pagina %3$d|%4$d.\n
6 Indice di transazione %1$3d CONVERTED suo blocco to %2$s blocco sull'oggetto %3$d|%4$d|%5$d.\n
7 Indice di transazione %1$3d CONVERTED suo blocco to %2$s blocco a pagina %3$d|%4$d.\n
8 Indice di transazione %1$3d WAIT FOR %2$s blocco sull'oggetto %3$d|%4$d|%5$d.\n
9 Indice di transazione %1$3d WAIT FOR %2$s blocco a pagina %3$d|%4$d.\n
10 Indice di transazione %1$3d RELEASE %2$s blocco sull'oggetto %3$d|%4$d|%5$d.\n
11 Indice di transazione %1$3d RELEASE %2$s blocco a pagina %3$d|%4$d.\n
12 Indice di transazione %1$3d DEMOTE blocco a %2$s sull'oggetto %3$d|%4$d|%5$d.\n
13 Indice di transazione %1$3d DEMOTE blocco a %2$s a pagina %3$d|%4$d.\n
14 OID = %1$2d|%2$6d|%3$4d\n
15 Tipo di oggetto: Classe Root.\n
16 Tipo di oggetto: Classe = %1$s.\n
17 Tipo di oggetto: Instanza della classe (%1$2d|%2$6d|%3$4d) = %4$s.\n
18 Tipo di oggetto: sconosciuto.\n
19 Modo totale di holders = %1$8s, modo totale di waiters = %2$8s.\n
20 Numero di holders=%1$3d, Numero di holders bloccati=%2$3d, Numero di waiters=%3$3d\n
21 LOCK HOLDERS:\n
22 BLOCKED LOCK HOLDERS:\n
23 LOCK WAITERS:\n
24 NON_2PL_RELEASED:\n
25 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Conteggio = %4$3d\n
26 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Conteggio = %4$3d, Nsubgranules = %5$2d\n
27 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Conteggio = %4$3d\n\t%5$13s Blocked_mode = %6$8s\n\t%7$17s Start_waiting_at = %8$-30s\n\t%9$17s Wait_for_secs = %10$.2f\n
28 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Conteggio = %4$3d, Nsubgranules = %5$2d\n\t%6$13s Blocked_mode =%7$8s\n\t%8$13s Start_waiting_at = %9$-30s\n\t%10$13s Wait_for_secs = %11$.2f\n
29 %1$4sTran_index = %2$3d, Blocked_mode =%3$8s \n\t%4$13s Start_waiting_at = %5$-30s \n\t%6$13s Wait_for_secs = %7$.2f\n
30 %1$4sTran_index = %2$3d, Non_2_phase_lock =%3$8s\n
31 VPID (volid|pageid) = %1$3d|%2$6d.\n
32 *** Lock Table Dump ***\n Lock Escalation at = %1$d, Run Deadlock interval = %2$5.2f\n
33 Transazione  (index %1$2d, %2$s, %3$s@%4$s|%5$d)\n
34 Isolamento %1$s\n
35 Stato %1$s\n
36 Timeout_period %1$s\n
37 Transazioni unilateralmente interrotte con indici:\n
38 %1$d ,
39 Transazioni scadute con indici:\n
40 %1$d ,
41 Risolvere la situazione di stallo, selezionando una transazione \ne una funzione per superare lo stallo: %1$d %2$s
42 , %1$d %2$s
43 Tipo di oggetto: chiave dell'index di classe (%1$2d|%2$6d|%3$4d) = %4$s.\n
44 Nome dell'index: %1$s\n

$set 15 MSGCAT_SET_IO
1 \n*************************************************************************\n
2 Backup = %1$s è necessario.\n Deve essere montato/caricato per continuare.\n Immettere 1 per continuare dopo che il backup è montato/caricato.\n Digitare 0 per uscire.\n
3 \n\n*** INFORMAZIONI DEL COLPO DI TESTA DI BACKUP ***\n\n
4 File di identificazione magica di backup %1$s,\n
5                      Rilascio: %1$s\n\
            Versione di disco: %2$g\n
6             Nome del Database: %1$s\n\
      Ora di creazione del DB: %2$s\
                     Pagesize: %3$d\n
7             Livello di backup: %1$d (%2$s)\n\
                    Start_lsa: %3$lld|%4$d\n\
                     Last_lsa: %5$lld|%6$d\n
8               Tempo de backup: %1$s\
  Numero dell'unità di backup: %2$d\n
9         Nome di volume del DB: %1$s\n\
     Identificatore di volume: %2$d, Size: %3$lld bytes (%4$d pages)\n
10             Nome del Database: %1$s\n\
              Nome del volume: %2$s\n\
            Numero dell'unità: %3$d\n\
            Livello di backup: %4$d (%5$s)\n\
Inserisci una delle seguenti opzioni:\n
11 Premi\n \
  -  %1$d per uscire.\n \
  -  %2$d per continuare dopo il volume è montato/caricato. (riprova)\n \
  -  %3$d per continuare dopo aver cambiato directory del volume o il dispositivo.\n
12 Inserisci nuova directory o dispositivo per il volume di backup.\n
13 Errore nell'input, scegliere un valore compreso tra %1$d e %2$d.\n
14 ** ATTENZIONE ** volume di backup errato montato.\n
15 Volume di backup %1$s è di un altro livello di backup\n   (Livello: %2$d, aspettandosi %3$d).\n\n
16 Volume di backup %1$s ha identificatore magico o di pagina incorreto.\n\n
17 Volume di backup %1$s è di un altro database\n  (nome: %2$s, aspettandosi %3$s).\n\n
18 Volume di backup %1$s fuori sequenza\n  (numero dell'unità: %2$d, aspettando %3$d).\n\n
19 Discordanza di timestamp per il volume di backup %1$s\n  (Timestamp: %2$s, aspettandosi %3$s).\n\n
20 Errore di file per volume di backup, linea %1$d.\n
21 Etichetta del volume di backup: Livello: %1$d, Unità: %2$d, Database %3$s, Tempo di backup: %4$s\n
22     Livello precedente di backup: %1$d Tempo: %2$s (start_lsa era %3$lld|%4$d)\n
23 Il seguente volume di backup del database è necessario per continuare il ripristino:\n\n
24 Destinazione di backup è pieno, una nuova destinazione è necessaria per continuare:\n\n
25   Nome del volume precedente di backup: %1$s\n
26   Nome del volume successivo di backup: %1$s\n
27                   Pagesize del basckup: %1$d\n
28                            Methodo Zip: %1$d (%2$s)\n\
                           Livello Zip: %3$d (%4$s)\n
29                     Includi Log Attivo: %1$s\n

$set 16 MSGCAT_SET_LOG
1 \n*************************************************************************\n
2 Log dei transazioni %1$s\n è necessario per continuare l'esecuzione normale.\n \
  Premi\n \
  -  0 per uscire.\n \
  -  1 per continuare senza archivio presente. (Recupero parziale)\n \
  -  2 per continuare dopo che l'archivio è montato/caricato.\n \
  -  3 per continuare dopo aver cambiato posizione/nome dell'archivio.\n
3 File di informazioni di basckup %1$s\n è necessario per continuare il processo di ripristino.\n \
  Premi\n \
  -  0 per uscire.\n \
  -  1 per continuare dopo che il file è montato/caricato.\n \
  -  2 per continuare dopo aver cambiato posizione/nome del file.  \n
4 Inserisci nuova posizione/nome.\n
5 COMMENTO: %1$s per il database %2$s\n
6 COMMENTO: Log dei transazioni %1$s non serve più a meno che un crash di media di database avviene.\n
7 COMMENTO: Log dei transazioni da %1$s a %2$s non servono più a meno che un incidente per la media della database avviene.\n
8 COMMENTO: dal database rinominato = %1$s\n
9 ARCHIVIO: %1$d %2$s %3$lld %4$lld\n
10 ARCHIVIO:
11 RIMUOVERE: %1$d %2$s a \n \
       %3$d %4$s.\n \
       RAGIONE: %5$s\n
12 ATTIVO: %1$s %2$d pagine\n
13 Fine di commit di tran_index %1$d con tranid = %2$d,\n poco prima LSA = %3$lld|%4$d e %5$s
14 Fine d'annullare di tran_index %1$d con tranid = %2$d,\n poco prima LSA = %3$lld|%4$d e %5$s
15 Ripristino di supporti incompleto è in vigore. Il database sarà\n \
 recuperato fino al indirizzo del punto LSA di %1$lld|%2$d. Ultimo \n \
 punto tempo di commit/abort è approssimativamente %3$s\n \
 Il database ha perso tutte le informazioni delle operazioni\n \
 comessi dopo tale orario.\n
16 Reset del log a causa di recupero incompleto.
17 Backup del database è stato preso.
18 Crash di media non è importante per l'applicazione.
19 Backup su directory %1$s che sembra avere un backup precedente in esso.\n\
Volumi di backup obsoleti di questo database nel questa posizione saranno rimossi,\n\
tra cui quelli del livello di backup corrente e superiore.\n
20 Premi 'y' per continuare o qualsiasi altro tasto per annullare.\n
21 Backup del database è stato interrotto dall'utente.\n
22 DELETE POSTPONED: Archivio log %1$d %2$s a \n \
        %3$d %4$s \n \
        non sono più necessari a meno che un ripristino da backup corrente avviene.\n
23 Un backup più recente ha rilasciato gli archivi di log precedentemente segnati come DELETE POSTPONED.\n
24 COMMENTO: archivio di log %1$s non è DELETE POSTPONED più a causa di backup più \n \
   recente e quindi non è necessaria a meno che un crash del database multimediale avviene.\n
25 COMMENTO: gli archivi di log da %1$s a %2$s non sono DELETE POSTPONED più\n \
   a causa di backup più recente e quindi non sono necessari a meno che un crash\n \
   del database multimediale avviene.\n
26 Errore durante un ripristino del database, durante l'elaborazione\n\
     Volume di basckup: %1$s\n\
    Numero dell'unità : %2$d\n\
    Volume di Database: %3$s\n\
     Volid di Database: %4$d\n\
** ATTENZIONE: Non utilizzate il database fino a quando un ripristino privo di errori è realizzato.\n
27 Errore nell'input, scegliere un valore compreso tra %1$d e %2$d.\n
28 Il tempo (%1$s) deve essere specificato dopo il tempo (%2$s) del backup specificato.\n
29 COMMENTO: archivio di log %1$s, che contiene pagine di log prima di %2$lld, non è necessario più di alcuna utilità HA.\n
30 Numero di archivi di log dei transazioni è stato superato il numero massimo desiderato.
