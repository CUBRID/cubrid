
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
$ csql.msg
$

$set 1 MSGCAT_CSQL_SET_CSQL
40 \
utilità interattiva SQL, versione %1$s\n\
uso: %2$s [OPZIONE] nome-base-di-dati[@host]\n\
\n\
opzioni valide:\n\
  -S, --SA-mode                modalità di esecuzione autonoma\n\
  -C, --CS-mode                modalità di esecuzione client-server\n\
  -u, --user=ARG               nome alternativo utente\n\
  -p, --password=ARG           password, usa "" per nessuna\n\
  -e, --error-continue         non uscire su errore\n\
  -i, --input-file=ARG         nome-file-input\n\
  -o, --output-file=ARG        nome-file-output\n\
  -s, --single-line            mode esecuzione singola riga\n\
  -c, --command=ARG            commandi-CSQL\n\
  -l, --line-output            ogni valore visualizzato in una riga\n\
  -r, --read-only              modalità read-only\n\
      --string-width           visualizzare ogni colonna che è un tipo stringa in questa larghezza\n\
      --no-auto-commit         disattivare modalità di esecuzione auto-commit\n\
      --no-pager               non utilizzare pager\n\
      --no-single-line         disattivare l'esecuzione orientata singola riga\n\
      --no-trigger-action      disattivare l'azione del trigger\n\
\n\
Per ulteriori informazioni, vedere http://www.cubrid.org\n
41 %1$s: non è corretto il valore di ambiente di %2$s_MODE. \nDovrebbe essere %3$s o %4$s.\n
42 %1$s: troppi argomenti trovati. massimo è %2$d.\n
43 %1$s: Deve essere impostata la variabile di ambiente di root directory ($CUBRID).\n
44 %1$s: il tentativo di esecuzione "%2$s" è fallito.\n
45 %1$s: non è possibile utilizzare opzioni -sa e -cs contemporaneamente.\n
46 %1$d comando(i) elaborato(i) correttamente.
47 Nel comando dalla linea %1$d,
48 In linea %1$d, colonna %2$d,
49 Digitare `;help' per i messaggi di aiuto.
50 ERRORE: 
51 Interprete CUBRID SQL
52 Ci sono stati aggiornamenti. Si desidera eseguire il commit? (`Y' o `N') 
53 Immettere 'Y' per commit, 'N' per annullare. 
54 Committed.
55 Rolled back.
56 Buffer dei comandi è stato salvato.
57 Il file è stato letto nel buffer dei comandi.
58 Buffer di comando è stato inviato alla stampante.
59 Directory corrente è cambiato in 
61 Inserire la Password: 
62 <Risultato di %1$s Comando in Linea %2$d>
63 Non ci sono risultati.
64 Checkpoint è stato emesso.
65 Il database è stato riavviato.
66 Tran index            Utente         Nome Host      Processo        Nome programma\n
67 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
68 La transazione specificata è stata cancellata.
69 Annullamento della transazione specificata non riuscita.
70 %1$d righe %2$s.
71 %1$d riga %2$s.
$ Questi 3 dovrebbe essere vuoto
75 Automated API tester
76 Invalid Automated API tester
79 csql>
80 csql
81 sysadm>
82 %1$d transactions %2$s.
83 %1$d transaction %2$s.

145 <Aiuto: Schema di Classe>
146 nessuno
147 <Aiuto: Tutti i Trigger>
148 <Aiuto: Trigger non Definiti>
150 <Aiuto: Definizione Trigger>
151 <Aiuto: Sintassi del Comando>
152 <Aiuto: Riepilogo dei Comandi Sessione>
178 Un nome di file è necessario.
179 Impossibile eseguire il pager.
180 Non è sufficiente memoria allocata.
184 La riga inserita è troppo lunga.
185 Troppe righe sono state inserite.
188 Troppi nomi di file sono stati inseriti.
190 Tale comando sessione. Digitare '; aiuto ' per i messaggi di aiuto.
191 Più di un comando di sessione corrispondenza. Si prega di specificare più.
193 Più di un comando corrispondenza. Si prega di specificare più.
194 Combinazione non valida di argomenti della riga di comando (opzioni).
196 Un errore sconosciuto si è verificato.
197 ';edit' non è consentita quando l'output è stato reindirizzato.

203 <%1$s> 
204 <Super Classes> 
205 <Sub Classes> 
206 <Attributi> 
207 <Attributi di classe> 
208 <Metodi> 
209 <Metodi della classe> 
210 <Risoluzioni> 
211 <Method files> 
212 <Specifiche_query> 
213 <Triggers> 
214 <Nome> 
215 <Evento> 
216 <Tempo di condizione> 
217 <Condizione> 
218 <Tempo dell'azione> 
219 <Azione> 
220 <Stato> 
221 <Priorità> 
222 <Nome> 
223 <Descrizione> 
224 <Sintassi> 
225 <Esempio> 

$ messaggi di aiuto csql
231 \
\n\
   Tutti i comandi della sessione devono essere preceduti da ';' e solo spazi vuoti/tabs\n\
   può precedere il prefisso. Caratteri in maiuscolo rappresentano l'abbreviazione minima\n\
   che deve inserire per eseguire il comando specificato.\n\
\n\
   ;REAd   [<file-name>]        - leggere un file nel buffer dei comandi.\n\
   ;Write  [<file-name>]        - (sovra)scrivere il buffer dei comandi in un file.\n\
   ;APpend [<file-name>]        - aggiungere buffer dei comandi in un file.\n\
   ;PRINT                       - stampa buffer dei comandi.\n\
   ;SHELL                       - richiamare shell.\n\
   ;CD                          - cambiare directory di lavoro corrente.\n\
   ;EXit                        - uscire dal programma.\n\
\n\
   ;CLear                       - pulire il buffer di comando.\n\
   ;EDIT                        - invocare editor di sistema con buffer di comando.\n\
   ;LISt                        - visualizzare il contenuto del buffer dei comandi.\n\
\n\
   ;RUn                         - eseguire sql nel buffer dei comandi.\n\
   ;Xrun                        - eseguire sql nel buffer dei comandi,\n\
                                  e pulire il buffer di comando.\n\
   ;COMmit                      - commit della transazione corrente.\n\
   ;ROllback                    - roll back della transazione corrente.\n\
   ;AUtocommit [ON|OFF]         - attivare/disattivare la modalità di auto-commit.\n\
   ;REStart                     - riavviare il database.\n\
\n\
   ;SHELL_Cmd    [shell-cmd]    - impostare la shell predefinito, editor, stampa e pager\n\
   ;EDITOR_Cmd   [editor-cmd]     comando a uno nuovo, o la visualizzazione corrente\n\
   ;PRINT_Cmd    [print-cmd]      uno, rispettivamente.\n\
   ;PAger_cmd    [pager-cmd]\n\
\n\
   ;DATE                        - visualizzare l'ora locale, data.\n\
   ;DATAbase                    - visualizzare il nome del database a cui si accede.\n\
   ;SChema nome-classe          - visualizzare le informazioni sullo schema di una classe.\n\
   ;TRIgger [`*'|nome-trigger]  - visualizzare la definizione di trigger.\n\
   ;Get parametro_sistema       - ottenere il valore di un parametro di sistema.\n\
   ;SEt parametro_sistema=value - impostare il valore di un parametro di sistema.\n\
   ;STring-width [width]        - impostare la larghezza di che ogni colonna che è un tipo string viene visualizzata.\n\
   ;COLumn-width [name]=[width] - impostare la larghezza di una colonna specifica che viene visualizzato.\n\
   ;PLan [simple/detail/off]    - visualizza il piano di esecuzione di query.\n\
   ;Info <comando>              - visualizzare informazioni interne.\n\
   ;TIme [ON/OFF]               - attivare/disattivare visualizzazione di query\n\
                                  tempo di esecuzione.\n\
   ;LINe-output [ON/OFF]        - abilitare/disabilitare la visualizzazione di ogni valore in una riga\n\
   ;HISTORYList                 - visualizzare l'elenco delle query eseguite.\n\
   ;HISTORYRead <history_num>   - leggere la voce del numero storia in buffer dei comandi.\n\
   ;TRAce [ON/OFF] [text/json]  - enable/disable sql auto trace.\n\
   ;HElp                        - visualizza questo messaggio di aiuto.\n\n

232 <Vincoli> 
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - informazioni sullo schema\n\
\ttrigger [trigger-name] - informazioni di trigger\n\
\tdeferred               - deferred trigger information\n\
\tworkspace              - informazioni dell'workspace\n\
\tlock                   - informazioni di lock (modalità client-server)\n\
\tstats class-name       - informazioni statistiche di classe\n\
\ttrantable              - informazioni sulla transazione\n\
\tlogstat                - informazioni del log\n\
\tcsstat                 - informazioni sezione critica\n\
\tplan                   - informazioni del cache plan\n\
\tqcache                 - informazioni sulla cache di query\n
235 <Partizioni>
236 È necessario un nome di classe.
