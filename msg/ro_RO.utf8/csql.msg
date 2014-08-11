
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
utilitar SQL interactiv, versiunea %1$s\n\
utilizare: %2$s [OPTION] database-name[@host]\n\
\n\
optiuni valide:\n\
  -S, --SA-mode                mod de execuţie independent\n\
  -C, --CS-mode                mod de execuţie client-server\n\
  -u, --user=ARG               nume alternativ de utilizator\n\
  -p, --password=ARG           parola, folosiţi "" pentru niciuna\n\
  -e, --error-continue         continuarea execuţiei la erori de instrucţiune\n\
  -i, --input-file=ARG         fişier de intrare\n\
  -o, --output-file=ARG        fişier de ieşire\n\
  -s, --single-line            execuţie linie cu linie\n\
  -c, --command=ARG            comenzi CSQL\n\
  -l, --line-output            afişarea fiecărei valori pe o singură linie\n\
  -r, --read-only              mod read-only\n\
      --string-width           lăţimea de afişare a fiecărei coloane de tip string\n\
      --no-auto-commit         dezactivarea modului de execuţie auto-commit\n\
      --no-pager               dezactivarea paginatorilor\n\
      --no-single-line         dezactivarea modului de execuţie linie cu linie\n\
      --no-trigger-action      dezactivarea acţiunilor declanşatorilor\n\
\n\
Pentru informaţii suplimentare accesaţi http://www.cubrid.org\n
41 %1$s: valoarea de mediu pentru %2$s_MODE este incorectă. \nTrebuie să fie %3$s sau %4$s.\n
42 %1$s: prea multe argumente date. Maximul este %2$d.\n
43 %1$s: Variabila de mediu ce indică directorul rădăcină ($CUBRID) trebuie setată.\n
44 %1$s: încercarea de a executa "%2$s" a eşuat.\n
45 %1$s: opţiunile -sa şi -cs nu pot fi folosite simultan.\n
46 %1$d comandă/comenzi a(u) fost procesate cu succes.
47 În comanda de la linia %1$d,
48 La linia %1$d, coloana %2$d,
49 Tastaţi `;help' pentru mesaje de ajutor.
50 EROARE: 
51 CUBRID SQL Interpreter
52 Au fost efectuate actualizări. Doriţi să executaţi un commit ? (`Y' sau `N') 
53 Introduceţi `Y' pentru commit, `N' pentru anulare. 
54 Finalizat(ă).
55 Derulat(ă) înapoi.
56 Bufferul comenzii a fost salvat.
57 Fişierul a fost citit în bufferul de comenzi.
58 Bufferul de comenzi a fost trimis spre printare.
59 Directorul curent a fost schimbat în 
61 Introduceţi parola: 
62 <Rezultatul comenzii %1$s de la linia %2$d>
63 Nu există rezultate.
64 Un punct de salvare (checkpoint) a fost creat.
65 Baza de date a fost repornită.
66 Index tran.            Nume utilizator      Nume gazdă      ID proces      Nume program\n
67 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
68 Tranzacţia specificată a fost înreruptă.
69 Întreruperea tranzacţiei specificate a eşuat.
70 %1$d rânduri %2$s.
71 %1$d rând %2$s.
$ These 3 should be blank
75 Verificator automat de API
76 Verificator automat de API invalid
79 csql>
80 csql
81 sysadm>
82 %1$d transactions %2$s.
83 %1$d transaction %2$s.

145 <Ajutor: Schema unei clase>
146 vid
147 <Ajutor: Toţi declanşatorii>
148 <Ajutor: Niciun declanşator definit>
150 <Ajutor: Definiţie declanşator>
151 <Ajutor: Sintaxa comenzii>
152 <Ajutor: Sumarul comenzilor de sesiune>
178 Un nume de fişier este necesar.
179 Paginatorul nu poate fi executat.
180 Memoria alocată este insuficientă.
184 Linia introdusă este prea lungă.
185 Prea multe linii au fost introduse.
188 Prea multe nume de fisier au fost introduse.
190 Comanda de sesiune inexistentă. Tastaţi `;help' pentru mesaje de ajutor.
191 Mai multe comenzi de sesiune se potrivesc. Specificaţi un şir de caractere mai lung.
193 Mai multe comenzi se potrivesc. Specificaţi un şir de caractere mai lung.
194 Combinaţie invalidă de argumente (optiuni) de linie de comandă.
196 O eroare nedefinită s-a produs.
197 ';edit' nu este permis când output-ul este redirecţionat.

203 <%1$s> 
204 <SuperClase> 
205 <SubClase> 
206 <Atribute> 
207 <Atribute de clasă> 
208 <Metode> 
209 <Metode de clasă> 
210 <Rezoluţii> 
211 <Fişiere de metode> 
212 <Specificaţii de interogare> 
213 <Declanşatori> 
214 <Nume> 
215 <Eveniment> 
216 <Timp de condiţie> 
217 <Condiţie> 
218 <Timp de acţiune> 
219 <Acţiune> 
220 <Stare> 
221 <Prioritate> 
222 <Nume> 
223 <Descriere> 
224 <Sintaxă> 
225 <Exemplu> 

$ csql help messages
231 \
\n\
   Toate comenzile de sesiune se prefixează cu `;' şi doar spaţiile goale\n\
   sau tab-urile pot preceda prefixul. Caracterele majuscule reprezintă abrevierea\n\
   minimă necesară la intrare pentru a executa comanda specificată.\n\
\n\
   ;REAd   [<file-name>]        - citeşte un fisier în buffer-ul de comenzi.\n\
   ;Write  [<file-name>]        - (supra)scrie buffer-ul de comenzi într-un fişier.\n\
   ;APpend [<file-name>]        - adaugă buffer-ul de comenzi într-un fişier, la final.\n\
   ;PRINT                       - afişează buffer-ul de comenzi.\n\
   ;SHELL                       - invocă shell-ul.\n\
   ;CD                          - schimbă directorul curent de lucru.\n\
   ;EXit                        - închide aplicaţia.\n\
\n\
   ;CLear                       - goleşte buffer-ul de comenzi.\n\
   ;EDIT                        - invocă editorul sistemului pentru buffer-ul de comenzi.\n\
   ;LISt                        - afişează conţinutul buffer-ului de comenzi.\n\
\n\
   ;RUn                         - execută instrucţiunile SQL din buffer-ul de comenzi.\n\
   ;Xrun                        - execută instrucţiunile SQL din buffer-ul de comenzi,\n\
                                  şi goleşte buffer-ul de comenzi.\n\
   ;COMmit                      - finalizează(commit) tranzacţia curentă.\n\
   ;ROllback                    - derulează înapoi tranzacţia curentă.\n\
   ;AUtocommit [ON|OFF]         - activează/dezactivează modul "auto commit".\n\
   ;REStart                     - reporneşte baza de date.\n\
\n\
   ;SHELL_Cmd    [shell-cmd]    - schimbă comenzile implicite pentru shell, editor,\n\
   ;EDITOR_Cmd   [editor-cmd]     afişare şi paginator cu comenzi noi, respectiv \n\
   ;PRINT_Cmd    [print-cmd]      afişează comanda curentă.\n\
   ;PAger_cmd    [pager-cmd]\n\
\n\
   ;DATE                        - afişează ora şi data locală.\n\
   ;DATAbase                    - afişează numele bazei de date accesate.\n\
   ;SChema class-name           - afişează informaţii despre schema unei clase.\n\
   ;TRIgger [`*'|trigger-name]  - afişează definiţia unui declanşator.\n\
   ;Get system_parameter        - preia valoarea unui parametru de sistem.\n\
   ;SEt system_parameter=value  - setează valoarea unui parametru de sistem.\n\
   ;STring-width [width]        - setează lăţimea de afişare a coloanelor de tip string.\n\
   ;COLumn-width [name]=[width] - setează laţimea de afişare a unei anumite coloane.\n\
   ;PLan [simple/detail/off]    - afişeaza planul de execuţie al interogarilor.\n\
   ;Info <command>              - afişeaza informaţii interne.\n\
   ;TIme [ON/OFF]               - activează/dezactivează afişarea timpului de execuţie \n\
                                  al interogărilor.\n\
   ;LINe-output [ON/OFF]        - activează/dezactivează afişarea fiecărei valori pe o linie\n\
   ;HISTORYList                 - afişează lista interogărilor executate.\n\
   ;HISTORYRead <history_num>   - citeşte în buffer-ul de comenzi înregistrarea din istoric\n\
                                  specificată prin parametru.\n\
   ;TRAce [ON/OFF] [text/json]  - enable/disable sql auto trace.\n\
   ;HElp                        - afişează acest mesaj de ajutor.\n\n

232 <Constrângeri> 
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - informaţii despre schemă\n\
\ttrigger [trigger-name] - informaţii despre declanşatori\n\
\tdeferred               - informaţii despre declanşatorii întârziaţi\n\
\tworkspace              - informaţii despre workspace\n\
\tlock                   - informaţii despre lock-uri (modul client-server)\n\
\tstats class-name       - informaţii despre statisticile unei clase\n\
\ttrantable              - informaţii despre tranzacţii\n\
\tlogstat                - informaţii despre jurnal\n\
\tcsstat                 - informaţii despre secţiunile critice\n\
\tplan                   - informaţii despre cache-ul planurilor de execuţie\n\
\tqcache                 - informaţii despre cache-ul interogărilor\n
235 <Partiţii>
236 Este necesar un nume de clasă.
