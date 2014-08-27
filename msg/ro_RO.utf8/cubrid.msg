
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
2 Copyright (C) 2008 Search Solution Corporation. Toate drepturile rezervate.\n
3 Informaţii despre Copyright\n
5 Valoarea `%1$s' a argumentului nu este întreagă
6 Valoarea `%1$s' a argumentului nu este float
7 Argument absent `%1$s'
8 Argument duplicat `%1$s'
9 Valoarea pentru argumentul `%1$s' este absentă
10 Argument necunoscut `%1$s'
11 Argument poziţional neaşteptat `%1$s'
12 Nu s-au furnizat definiţii pentru argument(e)
13 Nu s-a putut aloca spaţiu pentru parsarea argumentelor
14 Şirul de caractere furnizat ca argument depăşeşte lungimea maximă: %1$s
15 Număr invalid de mesaj %1$d în interiorul catalogului pentru parsarea argumentelor
16 [opţiuni]
17 Opţiuni:

$set 4 MSGCAT_SET_PARAMETER
2 %1$s nu este un director. Se creeaza %2$s în ".".
3 %1$s nu a putut fi deschis. Fişierul de configurare nu a fost creat.
4 Nu s-a putut scrie linia %1$d în fişierul de configurare %2$s.
6 Nu se poate accesa fişierul de configurare a sistemului "%1$s": %2$s
7 HOME nu este setat
8 Parametrul obligatoriu "%1$s" nu are o valoare.
9 Nu s-a putut deschide fişierul de configurare "%1$s": %2$s
10 Parametrul '%1$s' de la linia %2$d din fişierul '%3$s' : 
11 Variabila de mediu %1$s: 
12 Cuvânt cheie nerecunoscut.
13 Tipul valorii nu corespunde tipului parametrului.
14 Nu s-a putut aloca memorie pentru stocarea valorii.
15 Valoare de tip string neterminată.
16 Valoarea este în afara domeniului.
17 Eroare UNIX: %1$s
18 Niciun mesaj disponibil.
19 Valoarea este în afara domeniului. Revenire la valoarea implicită.
20 Cuvânt cheie = '%1$s'    Valoare = %2$d    Valoare implicită = %3$d\n
21 Cuvânt cheie = '%1$s'    Valoare = %2$f    Valoare implicită = %3$f\n
22 Parametru depreciat
23 Parametrii '%1$s' şi '%2$s' nu pot fi utilizaţi împreună.\n
31 Parametrul '%1$s':

$set 5 MSGCAT_SET_ERROR
1 Mesaj absent pentru codul de eroare %1$d.
2 Eroare internă: nu sunt disponibile informaţii detaliate.
3 Memorie virtuală epuizată: eşec la alocarea a %1$lld bytes.
4 A fost intrerupt.
5 Cheia = "%1$s" nu a fost gasită în tabela hash "%2$s".
6 O tabelă de memorie hash nulă a fost furnizată unei funcţii peste o tabelă hash.
7 Se încearcă formatarea volumului de disc "%1$s" cu o valoare incorectă %2$d a numărului de pagini.
8 Nu se poate formata volumul de disc "%1$s" cu %2$d pagini (%3$lld bytes).
9 Nu se poate formata volumul de disc "%1$s" cu %2$d pagini (%3$lld Kbytes) datorită spaţiului insuficient. Spaţiul momentan disponibil este de %4$d pagini (%5$lld Kbytes).
10 Nu se poate monta volumul de disc "%1$s".
11 Nu se poate monta volumul de disc "%1$s". Baza de date "%2$s", căreia volumul de disc îi aparţine, este în uz de către utilizatorul %3$s pe procesul %4$d al gazdei %5$s de la %6$s.
12 Problemă la demontarea volumului "%1$s".
13 O eroare I/O s-a produs la citirea paginii %1$d din volumul "%2$s".
14 O eroare I/O s-a produs la scrierea paginii %1$d din volumul "%2$s".
15 Nu se poate scrie pagina %1$d din volumul "%2$s" datorită spaţiului insuficient în dispozitivul sistemului de operare.
16 Nu se poate redenumi volumul de disc din "%1$s" în "%2$s".
17 Eroare internă: preluarea paginii dezalocate având pageid = %1$d, din volumul "%2$s".
18 Toate bufferele de pagină sunt fixe (LRU: %1$d).
19 Eroare internă: pageptr = %1$p al paginii %2$d din volumul "%3$s" nu este fix.
20 Eroare internă: pageptr necunoscut: %1$p.
21 Eroare internă: sector necunoscut %1$d în volumul "%2$s".
22 Eroare internă: pagina necunoscuta %1$d în volumul "%2$s".
23 Eroare internă: încercare de dezalocare a paginii de disc de sistem %1$d din volumul "%2$s".
24 Spaţiu aproape epuizat în volumul "%1$s". Număr total de pagini = %2$d, număr total de pagini disponibile = %3$d.
25 Spaţiu aproape epuizat în volumele având datele ca scop principal de stocare. Număr total de pagini = %1$d, număr total de pagini disponibile = %2$d.
26 Spaţiu aproape epuizat în volumele având indecşii ca scop principal de stocare. Număr total de pagini = %1$d, număr total de pagini disponibile = %2$d.
27 Spaţiu aproape epuizat în volumele cu scop de stocare generic. Număr total de pagini = %1$d, număr total de pagini disponibile = %2$d.
28 Spaţiu aproape epuizat în volumele având datele temporare ca scop principal de stocare. Număr total de pagini = %1$d, număr total de pagini disponibile = %2$d.
29 Ultima notificare de spaţiu epuizat pentru volumul "%1$s". Număr total de pagini = %2$d, număr total de pagini disponibile = %3$d.
30 Ultima notificare de spaţiu epuizat pentru volumele având datele ca scop principal de stocare. Număr total de pagini = %1$d, număr total de pagini disponibile = %2$d.
31 Ultima notificare de spaţiu epuizat pentru volumele având indecşii ca scop principal de stocare. Număr total de pagini = %1$d, număr total de pagini disponibile = %2$d.
32 Ultima notificare de spaţiu epuizat pentru volumele cu scop de stocare generic. Număr total de pagini = %1$d, număr total de pagini disponibile = %2$d.
33 Ultima notificare de spaţiu epuizat pentru volumele având datele temporare ca scop principal de stocare. Număr total de pagini = %1$d, număr total de pagini disponibile = %2$d.
34 Eroare internă: a %1$d-a pagină este în afara domeniului pentru fişierul %2$d din volumul "%3$s". Numărul curent de pagini este %4$d.
35 Eroare internă: Identificator necunoscut pentru volum : %1$d.
36 Spaţiu de disc epuizat în baza de date. Numărul de pagini solicitate = %1$d.
37 Spaţiu de disc epuizat în volumul "%1$s". Numărul de pagini solicitate = %2$d.
38 Eroare internă: VFID de fişier necunoscut %1$d|%2$d.
39 Eroare internă: Pagina %1$d|%2$d(volumul "%3$s") din VFID-ul de fişier %4$d|%5$d(volumul "%6$s") este invalid/dezalocat comform hărţii de alocare.
40 Eroare internă: %1$d pagini aşteptate, %2$d pagini găsite pentru VFID %3$d|%4$d(volumul "%5$s").
41 Eroare internă: Pagina %1$d|%2$d(volumul "%3$s") nu face parte din VFID %4$d|%5$d(volumul "%6$s").
42 Nu se pot reordona înregistrarile ancorate din pagina %1$d din volumul "%2$s".
43 Eroare internă: Pagina %1$d din volumul "%2$s" ar putea fi coruptă. %3$d înregistrări au fost găsite când %4$d erau aşteptate.
44 Spaţiu epuizat în pagina %1$d din volumul "%2$s".
45 Slotul %1$d din pagina %2$d din volumul "%3$s" este alocat unei înregistrari ancorate. O nouă înregistrare nu poate fi inserată aici.
46 Eroare internă: slotul %1$d din pagina %2$d din volumul "%3$s" nu este alocat.
47 Nu a fost posibilă crearea unui fişier heap în volumul "%1$s".
48 Accesare a unui obiect şters: %1$d|%2$d|%3$d.
49 Eroare internă: clasa obiectului %1$d|%2$d|%3$d este necunoscută.
50 Eroare internă: înregistrarea de relocare pentru obiectul %1$d|%2$d|%3$d poate fi coruptă.
51 Eroare internă: obiectul %1$d|%2$d|%3$d poate fi corupt.
52 Eroare internă: depăşire a capacităţii pentru obiectul %1$d|%2$d|%3$d, adresa poate fi coruptă.
53 Extragerea obiectului %1$d|%2$d|%3$d când numai OIDul său a fost asignat.
54 Eroare internă: încercare de manipulare a unui obiect de %1$d pagini lungime. Mărimea maximă permisă pentru un obiect este de %2$d pagini.
55 Eroare internă: Un ciclu de referinţe de pagini a fost detectat în pagina %1$d|%2$d din fişierul de heap %3$d|%4$d|%5$d.
56 Eroare internă: pentru un fişier de hashing extensibil (Volid: %1$d Fileid: %2$d) s-a specificat o pagină invalidă (Volid: %3$d Pageid: %4$d).
57 Cheia nu există în structura de hashing extensibil.
58 Şirul de caractere "%1$s" este prea lung pentru a încape într-o pagină slotată.
59 Eroare internă: tipul de cheie specificat %1$d nu este valid pentru o structură de hashing extensibil.
60 Eroare internă: structura de hashing extensibil a fost coruptă.
61 Eroare internă: pagina radacină a structurii de hashing extensibil (Volid: %1$d Fileid: %2$d Pageid: %3$d) a fost coruptă.
62 Eroare internă: înregistrarea de sortare %1$d (dimensiune %2$d) nu încape într-o pagină; mărimea maximă permisă este %3$d.
63 Eroare internă: o pagină temporară a fost coruptă în timpul sortării.
64 Clasă necunoscută "%1$s".
65 Clasa "%1$s" deja există.
66 Eroare internă: operaţiune forţată necunoscută %1$d pentru obiectul %2$d|%3$d|%4$d.
67 Eroare internă: un fişier de heap nu a fost alocat pentru stocarea obiectului %1$d|%2$d|%3$d.
68 Eroare internă: nume de clasă diferite au fost găsite pentru clasa cu oid = %1$d|%2$d|%3$d. Numele de clasă găsite sunt "%4$s", "%5$s" utilizând tabela hash, respectiv heap-ul pentru numele de clasă.
69 Eroare internă: identificatori diferiţi pentru obiecte de tip clasă au fost găsiţi pentru clasa cu numele "%1$s". OIDurile găsite sunt %2$d|%3$d|%4$d, %5$d|%6$d|%7$d utilizând tabela hash, respectiv heap-ul pentru numele de clasă.
70 Eroare internă: Clasa cu numele "%1$s" şi oid = %2$d|%3$d|%4$d nu există în tabela de hash pentru nume de clasă.
71 Eroare internă: Clasa cu numele "%1$s" şi oid = %2$d|%3$d|%4$d nu există în propriul heap.
72 Tranzacţia dumneavoastră (index %1$d, %2$s@%3$s|%4$d) a fost unilateral anulată de către sistem.
73 A expirat timpul de aşteptare pentru tranzacţia dumneavoastră (index %1$d, %2$s@%3$s|%4$d) în aşteptarea unui lock %5$s pe obiectul %6$d|%7$d|%8$d. Aşteptaţi încheierea operaţiunilor următorilor utilizatori: %9$s.
74 A expirat timpul de aşteptare pentru tranzacţia dumneavoastră (index %1$d, %2$s@%3$s|%4$d) în aşteptarea unui lock %5$s pe clasa %6$s. Aşteptaţi încheierea operaţiunilor următorilor utilizatori: %7$s.
75 A expirat timpul de aşteptare pentru tranzacţia dumneavoastră (index %1$d, %2$s@%3$s|%4$d) în aşteptarea unui lock %5$s pe instanţa %6$d|%7$d|%8$d a clasei %9$s. Aşteptaţi încheierea operaţiunilor următorilor utilizatori: %10$s.
76 A expirat timpul de aşteptare pentru tranzacţia dumneavoastră (index %1$d, %2$s@%3$s|%4$d) în aşteptarea unui lock %5$s pe pagina %6$d|%7$d. Aşteptaţi eliberarea lock-ului de pagină de către următorii utilizatori %8$s.
77 Eroare internă: toate bufferele sunt fixe.
78 Eroare internă: o eroare I/O s-a produs la citirea paginii logice de log %1$lld (pagină fizică %2$lld) din "%3$s".
79 Eroare internă: o eroare I/O s-a produs la scrierea paginii logice de log %1$lld (pagină fizică %2$lld) din "%3$s".
80 Spaţiu insuficient în dispozitivul sistemului de operare la scrierea paginii logice de log %1$lld (pagină fizică %2$lld) din "%3$s". Nu s-au putut scrie mai mult de %4$d bytes.
81 Eroare internă: pagina logică de log %1$lld ar putea fi coruptă.
82 Nu s-a putut monta volumul/fişierul de disc de log "%1$s".
83 Lungimea căii "%1$s" plus lungimea prefixului numelui de log "%2$s" este prea mare; lungimea combinată trebuie sa fie mai mică decât %3$d.
84 Lungimea prefixului numelui de log "%1$s" este prea mare; lungimea trebuie sa fie mai mică decât %2$d.
85 Prefixul de nume "%1$s" nu este la fel ca "%2$s" în discul de log. Log-ul a fost probabil redenumit în afara domeniului bazei de date.
86 Baza de date este incompatibilă cu "%1$s" versiunea "%2$s".
87 Există acţiuni de recuperare ce trebuiesc restaurate utilizând "%1$s" versiunea "%2$s" în locul versiunii "%3$s". După recuperare, baza de date poate fi executată pe versiunea "%4$s".
88 Eroare internă: Şirul de caractere al versiunii "%1$s" nu poate depăşi %2$d. Trebuie modificat log-ul de antete.
89 Log-ul "%1$s" nu aparţine bazei de date.
90 "Redo logging" este întotdeauna o operaţie la nivel de pagină. Un pointer la o pagină de date trebuie dat ca parte a adresei.
91 "Postpone logging" este întotdeauna o operaţie la nivel de pagină. Un pointer la o pagină de date trebuie dat ca parte a adresei.
92 "Compensate logging" este întotdeauna o operaţie la nivel de pagină. Un pointer la o pagină de date trebuie dat ca parte a adresei.
93 Avertisment: "redo logging" este ignorat în timpul recuperării "redo" şi a operaţiilor obişnuite de derulare înapoi.
94 Avertisment: "postpone logging" este ignorat în timpul recuperării "redo" şi a operaţiilor obişnuite de derulare înapoi.
95 Avertisment: "undo logging" este ignorat în timpul recuperării şi a operaţiilor obişnuite de derulare înapoi.
96 Recuperarea ar putea fi necesară pentru volumul "%1$s".
97 Eroare internă: nu s-a putut localiza pagina de log %1$lld în arhivele de log.
98 Nu s-a putut crea arhiva de log "%1$s" pentru arhivarea paginilor de la %2$lld până la %3$lld.
99 Nu s-a putut crea fişierul de informaţii de director "%1$s" pentru copia de siguranţă.
100 Nu s-a putut crea o copie de siguranţă a bazei de date "%1$s".
101 Fişier de utilizator necunoscut "%1$s".
102 Fişierul "%1$s" nu conţine suficiente înregistrări: %2$d înregistrări preconizate.
103 Fişierul "%1$s" pare sa conţină înregistrări neordonate. Înregistrarea %2$d cu valorile: %3$d %4$s %5$s, a fost gasită când o înregistrare cu valorile %6$d %7$s era aşteptată.
104 Fişierul "%1$s" pare sa aibă o înregistrare incorectă pentru numele bazei de date (volumul principal). Înregistrarea %2$d cu valorile: %3$d %4$s %5$s, a fost gasită când o înregistrare cu valorile %6$d %7$s %8$s era asteptată.
105 Nu se poate accesa fişierul de rezervă "%1$s". Operaţiunile de Restore sau Backup sunt anulate.
106 Nu se poate pregăti finalizarea tranzacţiei curente cu identificatorul de tranzacţie global %1$d deoarece acest identificator este utilizat de altă tranzacţie.
107 Nu există nicio tranzacţie globală distribuită asociată cu identificatorul de tranzacţie %1$d.
108 Tranzacţia curentă %1$d trebuie finalizată sau abandonată înainte de a încerca o contopire (JOIN) cu tranzacţia distribuită cu identificatorul global %2$d.
109 Tranzacţia dumneavoastră %1$d a fost abandonată unilateral de către sistem deoarece un membru/o componentă participantă s-a oprit sau a abandonat unilateral tranzacţia.
110 Valoarea nivelului de izolare trebuie să fie cuprinsă între %1$d şi %2$d.
111 Tranzacţia dumneavoastră a fost abandonată de către sistem datorită unei erori sau a schimbării modului de operare.
112 Gazda "%1$s" nu este autorizată să execute baza de date.
113 Serverul bazei de date nu a putut fi repornit/iniţializat. %1$s
114 "%1$s" nu a putut fi interpretat ca bază de date. Fişierele/volumele bazei de date au fost probabil redenumite/copiate în afara domeniului bazei de date.
115 Baza de date "%1$s" deja există.
116 Baza de date "%1$s" nu este cunoscută sau fişierul "databases.txt" nu poate fi accesat.
117 Calea absolută la baza de date este prea lungă. Lungimea combinată a căii "%1$s" plus numele "%2$s" este %3$d; lungimea combinată trebuie să fie mai mică decât %4$d.
118 Nu s-a putut localiza directorul de lucru curent.
119 Nu s-a putut găsi numele local de gazdă.
120 Numărul maxim de volume (%1$d) a fost depăşit.
121 Tentativă de ştergere "%1$s" a unui volum permanent al bazei de date.
122 Nu se poate accesa catalogul de mesaje de sistem.
123 Nu s-a putut crea %1$s pentru baza de date "%2$s". Consultaţi "%3$s" pentru informaţii suplimentare.
124 Volumul "%1$s" deja există.
125 Un volum de extensie automată "%1$s" de %2$d pagini şi scop de stocare generic a fost creat.
126 Scop necunoscut "%1$s" furnizat la linia %2$d.
127 O valoare incorectă %1$d pentru numărul de pagini este furnizat la linia %2$d.
128 Numărul de pagini nu a fost furnizat la linia %1$d.
129 Un token necunoscut "%1$s" a fost detectat la linia %2$d.
130 Memorie virtuală epuizată.
131 Eroare de sistem.
132 Funcţionalitate neimplementată în versiunea actuală: %1$s.
133 Constanta mulţime imbricată nu este permisă.
134 Nu este permisă utilizarea împreuna a atributelor de clasă şi a altor atribute într-o singură actualizare (update).
135 Instrucţiune invalidă pentru coloanele oid: %1$s.
136 Funcţie neimplementată "%1$s".
137 Obiecte de autorizare corupte.
138 Clasa de autorizare "%1$s" nu a fost gasită.
139 Eroare de acces la atributul de autorizare "%2$s" al clasei "%1$s".
140 Operaţia "%1$s" poate fi efectuată numai de către DBA sau de către un membru al grupului DBA.
141 Nu se poate adăuga "%1$s" ca membru al "%2$s".
142 Adăugarea membrului transformă ierarhia de utilizator într-un graf ciclic.
143 S-a detectat o clasă fără proprietar.
144 Nu se poate accesa obiectul de utilizator.
145 Nu puteţi executa comenzi de GRANT/REVOKE pentru dumneavoastră.
146 Nu se pot executa comenzi de GRANT/REVOKE pentru proprietarului unei clase.
147 Opţiuni absente pentru GRANT.
148 Nu se poate obţine ub lock de scriere asupra unui obiect de autorizare.
149 Nu se poate crea o instanţă pentru un obiect de autorizare.
150 Nu vă puteti anula propriile drepturi.
151 Nu se pot anula drepturile unui proprietar de clasă.
152 GRANT nu a fost găsit.
153 Nu există drepturi de autorizare pentru baza de date.
154 Configurarea privilegiilor este incompleta - nu există privilegii de autorizare în vigoare pentru baza de date.
155 Root-uri de autorizare multiple.
156 Eroare de autorizare.
157 Eroare de autorizare la SELECT.
158 Eroare de autorizare la ALTER.
159 Eroare de autorizare la UPDATE.
160 Eroare de autorizare la INSERT.
161 Eroare de autorizare la DELETE.
162 Eroare de autorizare la INDEX.
163 Eroare de autorizare la EXECUTE.
164 Utilizatorul "%1$s" deja există.
165 Utilizatorul "%1$s" este invalid.
166 Utilizator invalid specificat.
167 DBA, membrii ai grupului DBA şi proprietarul clasei pot efectua operaţia.
168 Membru negăsit.
169 Nu se poate elimina utilizatorul %1$s din baza de date.
170 Niciun utilizator autentificat.
171 Parolă eronată sau absentă.
172 Şirul de caractere pentru parolă nu poate avea mai mult de 31 bytes.
173 Nu se poate localiza fişierul de bază de date "%1$s".
174 Nu s-a putut obţine drept de scriere în fişierul de bază de date "%1$s".
175 Fişier de bază de date "%1$s" greşit formatat.
176 Eroare de conversie în formatul de dată.
177 Nu s-a putut crea un obiect mare pentru stocare internă.
178 OID temporar detectat împreuna cu MOP.
179 Domeniul "%1$s" nu este compatibil cu domeniul "%2$s".
180 Domeniul "%1$s" al valorii furnizate nu este compatibil cu domeniul destinaţie "%2$s".
181 Nu se poate face conversie forţată a unei valori de domeniu "%1$s" la domeniul "%2$s".
182 Nu se poate face conversie forţată a unei valori de domeniu "%1$s" la domeniul "%2$s" fără a depăşi capacitatea acestuia.
183 Cantitate neaşteptată de date recepţionate; %1$d aşteptate, %2$d recepţionate.
184 Nu s-a putut aloca un buffer de comunicaţii.
185 Eroare la recepţionarea datelor de la client.
186 Eroare la recepţionarea datelor de la server.
187 Buffer de comunicaţii neutilizat.
188 Bază de date necunoscută "%1$s".
189 Nume de gazdă invalid "%1$s".
190 Server neidentificat.
191 Nu se poate realiza conexiunea la serverul "%1$s" pe "%2$s".
192 Nu s-a putut încarca fişierul de configurare al reţelei.
193 Serverul a recepţionat comanda de oprire de la client.
194 ID necunoscut %1$d de cerere de server.
195 Eroare de comunicaţie la server: %1$s.
196 Nume de server neidentificat.
197 Nu s-a putut contacta serverul primar (master server).
198 Bufer de recepţie prea mic, date trunchiate.
199 Serverul nu mai răspunde.
200 Ştergere necorespunzătoare a mulţimii din obiect după dezalocare.
201 Ştergere incorectă a înregistrării UNIQUE din tabelă.
202 Atributul "%1$s" nu a fost găsit.
203 Valoarea pentru atributul "%1$s" trebuie să fie de tipul "%2$s", nu de tipul "%3$s".
204 Funcţie apelată fără argumente sau cu argumente invalide.
205 Atributul "%1$s" nu poate fi făcut NULL.
206 Atributul "%1$s" nu are constrângere de unicitate (UNIQUE).
207 Clasa "%1$s" are un obiect de dimensiune 0.
208 Metoda "%1$s" nu a fost gasită.
209 Nu s-a găsit niciun atribut sau metodă cu numele "%1$s".
210 Eroare internă: procesarea unui şablon obiect.
211 Atributul a fost şters la construcţia şablonului obiect.
212 Atributul "%1$s" contravine constrângerii de unicitate (UNIQUE).
213 O înregistrare UNIQUE invalidă a fost detectată în tabelă.
214 Un conflict de domenii există pentru atributul "%1$s".
215 Atributul cu id %1$d nu a fost găsit.
216 O etichetă invalidă de apartenenţă a obiectului a fost detectată în valoare.
217 Operaţia poate fi efectuată numai asupra obiectelor clasă.
218 Obiect invalid în expresia cale pentru db_get.
219 Expresie cale invalidă pentru db_get.
220 Mulţime invalidă în expresia cale pentru db_get.
221 Index invalid pe mulţime în expresia cale pentru db_get.
222 Tentativă de atribuire a unui şir de caractere mai lung de %2$d caractere, atributului "%1$s".
223 Valoarea %2$d este prea mare pentru atributul smallint "%1$s".
224 O bază de date nu a fost repornită.
225 Valoare absentă pentru atributul "%1$s" cu constrângere NOT NULL.
226 Listă vidă de componente în clasă.
227 Tentativă de asignare multiplă a unei valori atributului "%1$s".
228 Metoda "%1$s" apelată cu %2$d argumente. Sistemul permite un maxim de %3$d argumente.
229 Metoda "%1$s" necesită ca argumentul %2$d sa fie în domeniul "%3$s". O valoare invalidă de domeniu "%4$s" a fost furnizată.
230 Atributului "%1$s" nu i se poate asigna un şir de caractere de lungime %2$d. Lungimea maximă permisă pentru un şir de caractere este %3$d.
231 Operaţie invalidă de API încercată asupra unui obiect temporar.
232 Nu este permisă utilizarea de şabloane obiect în tranzacţii multiple.
233 Nu s-a putut crea clasa cu numele de tip elementar "%1$s".
234 Fişierul de metode "%1$s" nu a fost găsit.
235 %1$d funcţii metodă nerezolvate după linkare.
236 Metodă nerezolvată "%1$s".
237 Probleme majore au fost depistate în timpul linkării dinamice.
238 Nu s-a putut accesa fişierul de metode "%1$s".
239 Atributul "%1$s" nu a fost găsit.
240 Metoda "%1$s" nu a fost găsită.
241 Nu există atribut sau metodă cu numele "%1$s".
242 Signatura pentru %2$s nu a fost găsită pentru metoda "%1$s".
243 Argumentul %2$d al metodei "%1$s" nu este definit.
244 Numele de domeniu "%1$s" nu este numele unei clase sau al unui tip elementar.
245 Numele "%1$s" a fost deja utilizat ca nume de atribut.
246 Numele "%1$s" a fost deja utilizat ca nume de metodă.
247 Argumente invalide pentru o funcţie internă a managerului de schemă.
248 Domeniul "%1$s" al atributului nu este adecvat utilizării împreună cu constrângerea %2$s.
249 Operaţia solicitată asupra clasei "%1$s" poate fi efectuată numai înainte de crearea instanţelor.
250 Nu s-a putut crea indexul peste atributul "%1$s", definit cu o valoare partajată.
251 Signatura %2$s există deja pentru metoda "%1$s".
252 Atributul "%1$s" trebuie sa deţină un domeniu de mulţime pentru această operaţie.
253 Atributul "%1$s" nu poate fi definit cu un domeniu mulţime imbricată.
254 Nu s-a putut elimina domeniului "%2$s" din atributul "%1$s".
255 Atributul "%1$s" nu este un domeniu cu dimensiune variabilă.
256 Superclasa deja există.
257 Adăugarea unei superclase ar provoca cicluri în ierarhia de clase.
258 Superclasa nu a fost găsită.
259 Signaturi multiple există pentru metoda "%1$s".
260 Argumentul %2$d al metodei "%1$s" nu este de domeniu mulţime.
261 Rezolvarea pentru "%1$s" nu a fost gasită.
262 Domeniul "%2$s" nu este adecvat atributului "%1$s".
263 Corupţie internă detectată în managerul de schemă.
264 Aliasul "%2$s" pentru atributul/metoda "%1$s" nu este unic.
265 Mascarea atributului "%1$s" cu domeniul incompatibil "%2$s".
266 Conflict de nume pentru atributul "%1$s" cu clasele "%2$s" si "%3$s".
267 Mascarea metodei "%1$s" cu o signatură incompatibilă.
268 Conflict de nume pentru metoda "%1$s" cu clasele "%2$s" si "%3$s".
269 Nu se poate defini un index pe domeniul "%1$s".
270 Nu se poate defini o constrângere de unicitate (UNIQUE) peste domeniul "%1$s".
271 Ciclu detectat în ierarhia de clase; "%1$s" este o superclasă a lui "%2$s".
272 Indexul "%2$s" deja definit pentru clasa "%1$s".
273 Indexul "%1$s" nu există.
274 Caractere nepermise în nume: "%1$s".
275 Operaţie nepermisă pentru atributul "%1$s", moştenit din clasa "%2$s".
276 Operaţie nepermisă pentru metoda "%1$s", moştenită din clasa "%2$s".
277 Operaţie nepermisă peste "%1$s", moştenit din clasa "%2$s".
278 Clasa "%4$s" are domenii incompatibile pentru "%1$s", moştenite din clasele "%2$s" şi "%3$s".
279 În clasa "%4$s", "%1$s" moştenit din clasa "%2$s" nu poate fi utilizat comform cerinţelor. Definiţia din clasa "%3$s" are un domeniu mai particular.
280 În clasa "%3$s", "%1$s" nu poate fi moştenit din clasa "%2$s" deoarece există o definitie locală cu un domeniu incompatibil.
281 În clasa "%3$s", "%1$s" moştenit din clasa "%2$s" are un alias dar nu s-a găsit niciun substitut din altă sursă.
282 În clasa "%4$s", "%1$s" moştenit din clasa "%3$s" nu poate servi ca substitut de alias pentru clasa "%2$s". Domeniile sunt incompatibile.
283 În clasa "%4$s", "%1$s" moştenit din clasa "%3$s" nu poate servi ca substitut de alias pentru clasa "%2$s". Are un domeniu mai puţin specific.
284 Nu se poate solicita moştenirea lui "%1$s" deoarece este definit local în clasă.
285 Nu se poate utiliza "%1$s" ca alias pentru componenta moştenită "%2$s" deoarece există deja o componentă cu acelaşi nume, definită local.
286 Nu se poate utiliza "%1$s" ca alias pentru "%2$s" din "%3$s". O componentă cu acest nume este deja moştenită din "%4$s".
287 În clasa "%1$s", nu se poate defini atributul "%2$s" deoarece ar masca o metodă moştenită din "%3$s".
288 În clasa "%1$s", nu se poate defini metoda "%2$s" deoarece ar masca un atribut moştenit din "%3$s".
289 Subclasa "%1$s" nu poate moşteni metoda "%2$s" din clasa "%3$s" deoarece există deja un atribut cu acest nume.
290 Subclasa "%1$s" nu poate moşteni atributul "%2$s" din clasa "%3$s" deoarece există deja o metodă cu acest nume.
291 "%1$s" este definit în clasa "%2$s" ca atribut, iar în clasa "%3$s" ca metodă.
292 Nu există o specificaţie de populare cu indexul "%1$s".
293 Operaţie nepermisă pentru acest tip de caz.
294 Variabilă de mediu nedefinită "%1$s" referenţiată în fişierul de metode.
295 Nu s-a putut modifica clasa "%1$s". Spaţiul este epuizat în interiorul catalogulului de sistem. Pentru a revendica spaţiu, compactaţi baza de date cu utilitarul compactdb.
296 A fost gasită o listă de proprietăţi invalidă.
297 Numele de alias "%1$s" este utilizat de mai multe ori.
298 În clasa "%1$s", o rezolvare pentru "%2$s" din "%3$s"' nu este permisă. Această clasă nu este o superclasă.
299 "%1$s" este un cuvânt rezervat şi nu poate fi utilizat ca atribut, metodă sau nume de clasă.
300 Atributul "%1$s" nu poate fi definit cu ambele constrângeri DEFAULT şi UNIQUE.
301 Atributul "%1$s" nu poate fi definit cu o constrângere de dimensiune %2$d. Maximul pentru constrângerea de lungime este %3$d.
302 Nu s-a putut adăuga elmentul la o mulţime.
303 Valoarea există deja în mulţime.
304 Indexul în mulţime, %1$d, este în afara domeniului.
305 Indexul în secventă, %1$d, este în afara domeniului.
306 Operaţia solicitată poate fi efectuată numai asupra secvenţelor.
307 Operaţia solicitată poate fi efectuată numai asupra mulţimilor.
308 Valoarea elementului nu se află în domeniul mulţimii.
309 S-a furnizat un index invalid de element în mulţime: %1$d.
310 Elementul nu a fost găsit în mulţime.
311 Elementul nu a fost găsit în secvenţă.
312 "%1$s" nu este un domeniu mulţime valid.
313 Depaşire negativă a capacităţii pentru bufferul de obiect în timpul citirii.
314 Depăşire a capacităţii pentru bufferul de obiect în timpul scrierii.
315 O definiţie nepermisă de metaclasă a fost intâlnită.
316 Nepotrivire la calculul dimensiunii la transformare, aşteptat: %1$d, calculat: %2$d.
317 Reprezentare invalidă pe disc pentru clasa "%1$s".
318 Desincronizare în timpul incărcării obiectelor. Baza de date este probabil coruptă sau învechită/neactualizată.
319 Extensie necunoscută de atribut ignorată.
320 S-a intâlnit o reprezentare coruptă pe disc a obiectului.
321 Posibilă corupţie detectată în workspace.
322 MOP negăsit în workspace.
323 Încercare de upgradare a unui MOP non-temporar în workspace.
324 Obiect fără clasă găsit în workspace.
325 Tentativă de "garbage collect" a unui MOP din lista de obiecte modificate şi nesalvate.
326 Schimbare a unui pointer de clasă pentru un obiect din workspace.
327 Nu se poate crea un MOP cu OID NULL.
328 Instanţă fără clasă găsită în workspace.
329 Listă inlănţuită de obiecte nealocată în workspace.
330 Încercare nepermisă de modificare/eliberare a unei zone de memorie din workspace aflată în uz.
331 Nu s-a putut aloca un bloc de memorie din OS pentru workspace, memorie virtuală epuizată, se abandonează tranzacţia.
332 Estimarea iniţială a dimensiunii workspace-ului a fost depăşită. O extensie automată a fost efectuată.
333 Memorie epuizată în timpul alocării de memorie pentru workspace. Se abandonează tranzacţia.
334 Tentativă de alocarea a %1$d bytes, mai mult decât dimensiunea maximă a unui bloc de memorie din workspace. Încercaţi ajustarea parametrului max_block_size. Tranzacţie abandonată.
335 Se ignoră încercarea de free pentru un obiect nealocat în interiorul workspace-ului.
336 Se ignoră încercarea de a dezaloca un obiect de mai multe ori.
337 Se ignoră încercarea de alocare a unui obiect de dimensiune negativă.
338 Depăşire pentru identificatorii înregistrărilor din harta de memorie.
339 Secţiunea critică nu poate fi părăsită deoarece nu este deţinută de tranzacţia %1$d.
340 Procesul copil nu poate fi creat (cu fork).
341 Procesul copil nu poate fi executat.
342 Grupul procesului nu poate fi schimbat.
343 IDul cererii (Request_id) este deja utilizat.
344 %1$s.
345 Eroare la citirea unui mesaj de oprire de la serverul principal (master).
346 Eroare la citirea unui mesaj de anulare a opririi de la serverul principal (master).
347 Nu s-a putut crea un pipe către serverul principal (master).
348 Trebuie specificat un serviciu sau un identificator de port.
349 Serviciu de TCP necunoscut: %1$s.
350 Eroare la preluarea adresei pe baza numelui pentru "%1$s". Numele de gazdă nu a fost găsit.
351 Nu s-a putut crea un socket TCP.
352 tcp_open: nu se poate obţine un port TCP rezervat.
353 Nu s-a putut stabili conexiunea către serverul principal (master) pe maşina "%1$s".
354 Nu se poate seta un proprietar nou de proces pentru socket.
355 Nu se poate crea fluxul de date de socket.
356 Fişierul '%1$s' pentru socketul de domeniu UNIX există.
357 Ataşarea la adresa locală nu este posibilă... abandon.
358 Operaţia de Accept a eşuat.
359 Eroare de datagramă la operaţia de "bind".
360 Eroare de datagramă la operaţia de "accept".
361 Eroare de datagramă la operaţia de "connect".
362 Eroare de datagramă pentru socket.
363 Operaţia de "Recvmsg" (recepţionare mesaj) de la socketul master a eşuat.
364 Se trimite o nouă cerere catre server.
365 Se trimite un mesaj de "broadcast" către client.
366 Eroare de la server: %1$s
367 Serverul %1$s deja există.
368 Eroare de comunicaţie în timpul conectării pentru %1$s.
369 Cod de eroare neutilizat.
370 Cod de eroare neutilizat.
371 Cod de eroare neutilizat.
372 Cod de eroare neutilizat.
373 Sfârşit de fişier (EOF) prematur în timpul citirii: %1$d.
374 Sfârşit de fişier (EOF) prematur în timpul citirii a %1$d sau %2$d.
375 Eroare de conversie la (0x%1$c%2$c) %3$d şi %4$d convertit(e) la %5$d.
376 Nicio clasă publică nu a fost gasită.
377 Eroare %1$d: eşec la adăugarea atributului "%2$s" la ierarhia multimedia.
378 Eroare %1$d: eşec la adăugarea metodei "%2$s" implementată ca "%3$s" la ierarhia multimedia.
379 Eroare %1$d: eşec la adăugarea unei superclase la ierarhia multimedia.
380 Loader dinamic deja iniţializat.
381 Loader dinamic neiniţializat.
382 Loader dinamic iniţializat necorespunzator.
383 "%1$s" nu este un fişier obiect sau o arhivă.
384 Nu se poate determina calea absolută pentru "%1$s".
385 Procesul ld s-a terminat cu codul %1$d.
386 Procesul ld s-a terminat datorită semnalului %1$d.
387 Procesul ld s-a terminat cu un waitval necunoscut 0x%1$x.
388 Imaginea iniţială pentru "%1$s" nu este accesibilă.
389 Eroare de sistem: %1$s.
390 Problemă cu "%1$s": %2$s.
391 Conflict cu un alt handler SIGPIPE.
392 Nu s-a putut lansa procesul de încărcare dinamică.
393 Procesul de încărcare dinamică nu mai este pornit sau nu este accesibil.
394 Marginea superioară a intervalului nu se află între 0 si 256.
395 O subexpresie nepermisă a fost specificată.
396 O cifră octală în afara domeniului a fost găsită în escapare.
397 Expresia regulată nu este delimitată corespunzator.
398 Căutarea de text a fost apelată înaintea compilării expresiei.
399 O paranteză fără corespondent este prezentă în expresia regulată.
400 În expresia regulată sunt prezente paranteze cu nivel de imbricare prea mare.
401 Mai mult de două numere specificate în interval.
402 O acoladă fără corespondent este prezentă în expresia regulată.
403 Primul număr din expresia interval este mai mare decât al doilea.
404 O paranteză dreaptă fără corespondent este prezentă în expresia regulată.
405 Expresia regulată este prea lungă pentru a fi compilată.
406 Identificator invalid pentru un index de tip B+arbore: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
407 O cheie necunoscută %1$s este referenţiată în indexul de tip B+arbore {vfid: (%2$d, %3$d), rt_pgid: %4$d, key_type: %5$s}.
408 O valoare necunoscută (oid) %1$d|%2$d|%3$d este referenţiată.
409 O valoare duplicată (oid) %1$d|%2$d|%3$d este referenţiată.
410 Cheie nulă referenţiată.
411 Tip invalid de cheie pentru indexul de tip B+arbore: "%1$s".
412 Specificaţie invalidă pentru cautarea pe intervale.
413 Identificator de atribut necunoscut: %1$d.
414 Identificator de clasă necunoscut: %1$d|%2$d|%3$d.
415 Identificator de atribut invalid: %1$d|%2$d|%3$d.
416 Identificator de reprezentare necunoscut: %1$d.
417 Identificator de reprezentare invalid: %1$d.
418 Descriptorul de înregistrare nu are suficient spaţiu: %1$d.
419 Clasa a atins numărul maxim de reprezentări: %1$d.
420 Clasa %1$d|%2$d|%3$d are deja reprezentări.
421 Director de reprezentări absent din catalogul pentru clasă: %1$d|%2$d|%3$d.
422 Registru de informaţii despre reprezentare absent din catalogul pentru clasă - Repr_Id: %1$d|%2$d|%3$d - %4$d.
423 Sesiune invalidă.
424 Nu există instrucţiuni de executat.
425 Tip de date incompatibil pentru atributul "%1$s" din clasa "%2$s".
426 Tip de date incompatibil pentru atributul "%1$s".
427 Depăşire a capacităţii pentru tipul de date "%1$s".
428 Instrucţiunea nu poate fi actualizată.
429 Comandă necunoscută.
430 Variablă necunoscută "%1$s".
431 Eroare în interpretorul sau în parserul SQL/X.
432 Clasă necunoscută "%1$s".
433 Atribut necunoscut "%1$s" în clasa "%2$s".
434 Nu s-au putut găsi nume de coloane valide într-o interogare.
435 Nu sunt permise instrucţiuni multiple.
436 Instrucţiunea nu este o interogare.
437 ID invalid pentru obiect lung (%1$s,%2$d, %3$s, %4$s).
438 Există un conflict în descriptorul de obiecte mari.
439 Offsetul %1$d este mai mare decât lungimea obiectului lung %2$d.
440 Poziţie invalidă a cursorului.
441 Operaţie invalidă a cursorului.
442 Poziţie necunoscută a cursorului.
443 Indexul %1$d al tuplului de valori este în afara domeniului.
444 Nume de atribut invalid: "%1$s".
445 Indexul %1$d în lista de valori este în afara domeniului.
446 Există structuri pentru rezultatele de interogare închise de către managerul de tranzacţii, dar neîchise de către aplicaţie. 
447 Tentativă de lucru cu o structură pentru rezultatul unei interogari care a fost deja închisă.
448 Dimensiunea tuplurilor este restricţionata la cel mult dimensiunea unei pagini.
449 Identificator de interogare necunoscut: %1$d.
450 Operator de mulţime invalid: %1$d.
451 Fişier de heap fără instanţe.
452 Conţinut invalid pentru un nod al arborelui XASL.
453 Nu mai există noduri cu specificaţii de acces.
454 Un tip de date invalid este referenţiat.
455 Pagini de fişier de interogare epuizate.
456 Referinţele la tipuri de date sunt incompatibile.
457 Tip invalid de rezultat al interogării.
458 Depăşire a capacităţii produsă într-un context suplimentar.
459 Rezultatul interogării conţine mai mult de un tuplu. 
460 S-au specificat prea puţine variabile gazdă de intrare.
461 Sunt furnizate mai multe variabile gazdă decât coloane în rezultat.
462 Valoarea rezultată este NULL, dar nu există variabilă indicator.
463 Instrucţiunea pentru cursor nu a fost încă preparată.
464 Cursorul ar trebui sa fie o instrucţiune SELECT.
465 Cursorul nu este încă deschis.
466 Cursorul trebuie închis înainte de a fi redeschis.
467 Rezultatul trebuie să conţină un singur obiect.
468 Nu sunt permişi marcatori poziţionali în instrucţiunile de tip EXECUTE IMMEDIATE.
469 Tip invalid de date utilizat.
470 Interogarea implică prea multe elemente pentru optimizator.
471 Memorie epuizată la linia %1$d din fişierul %2$s.
472 Conectorul %1$s pentru baza de date locală '%2$s' de pe maşina gazdă '%3$s' nu este accesibil.
473 Raportat din baza de date locală '%1$s' de pe maşina gazdă '%2$s': %3$s.
474 Eroarea %1$s detectată în timpul %2$s.
475 Nu există specificaţie de interogare pentru indexul %1$s.
476 Baza de date locală (LDB) %1$s deja există.
477 Nu s-a putut realiza conexiunea la serverul principal (master) de pe maşina gazdă %2$s pentru a accesa baza de date locală LDB %1$s.
478 Baza de date locală "%1$s" nu este înregistrată.
479 Baza de date locală "%1$s" este accesată în prezent de alţi clienţi, modificarea şi ştergerea nu sunt permise.
480 Baza de date locală "%1$s" nu este goală şi nu poate fi ştearsă.
481 "%1$s" nu este o clasă proxy virtuală.
482 "%1$s" nu este o clasă virtuală.
483 "%1$s" nu este un atribut al clasei "%2$s".
484 Câmpul object_id pentru clasa proxy virtuală "%1$s" este deja setat.
485 Unul sau mai multe atribute object_id sunt NULL pentru instanţa clasei proxy "%1$s".
486 Obiectul nu este o instanţă de clasă proxy care ar putea fi actualizată.
487 Obiectul nu este o instanţă.
488 Un atribut al unei clase nu poate fi o clasă virtuală sau o clasă proxy virtuală.
489 Un atribut al unei clase proxy virtuale nu poate fi o clasă, o clasă virtuală sau un obiect.
490 Superclasa "%1$s" trebuie să fie de acelaşi tip de clasă ca şi "%2$s".
491 Nu se poate extrage numele de entitate din specificaţia de interogare proxy "%1$s".
492 %1$s
493 Sintaxă: %1$s %2$s
494 Semantică: %1$s %2$s
495 Execuţie: %1$s %2$s
496 Cheie necunoscută referenţiată.
497 Valoare necunoscută (oid) %1$d|%2$d|%3$d referenţiată.
498 Valoare duplicată (oid) %1$d|%2$d|%3$d referenţiată.
499 Cheie nulă referenţiată.
500 Cheie duplicată referenţiată.
501 Prioritate invalidă pentru declanşator.
502 Declanşator fără clasă ţintă.
503 Declanşatorul "%1$s" nu a fost găsit.
504 Eroare internă: procesarea declanşatorului "%1$s".
505 Un declanşator cu numele "%1$s" deja există.
506 Un declanşator nu poate fi definit pe "%1$s" deoarece aceasta este o clasă virtuală.
507 Clasă ţintă neadecvată "%1$s" pentru declanşator.
508 Atribut ţintă neadecvat "%1$s" pentru declanşator.
509 Condiţie invalidă "%1$s" pentru declanşator.
510 Acţiune invalidă "%1$s" pentru declanşator.
511 Nu există drepturi de accesare a declanşatorului "%1$s".
512 Nu există drepturi de ştergere a declanşatorului "%1$s".
513 Nu există drepturi de actualizare a declanşatorului "%1$s".
514 Nu există drepturi de modificare a declanşatorului "%1$s".
515 Momentul %2$s al execuţiei acţiunii declanşatorului nu poate preceda momentul %1$s al execuţiei condiţiei.
516 Adâncimea maximă %1$d a declanşatorilor a fost depăşită la declanşatorul "%2$s".
517 Operaţia a fost refuzată de către declanşatorul "%1$s".
518 Eroare internă: procesarea declanşatorului "%1$s".
519 Tip de condiţie nepermis pentru declanşator.
520 Acţiunea de REJECT nu poate fi utilizată cu un timp de acţiune de tip AFTER sau DEFERRED.
521 Acţiunea de REJECT nu poate fi utilizată cu evenimentele ABORT sau TIMEOUT.
522 Expresia pentru condiţia declanşatorului nu a fost specificată.
523 Expresia pentru acţiunea declanşatorului nu a fost specificată.
524 Activitatea specificată nu poate fi eliminată deoarece este deţinută de un alt utilizator.
525 Eroare la compilarea condiţiei pentru "%1$s": %2$s
526 Eroare la compilarea acţiunii pentru "%1$s", %2$s
527 Eroare la evaluarea condiţiei pentru "%1$s": %2$s
528 Eroare la evaluarea acţiunii pentru "%1$s", %2$s
529 Tranzacţia nu poate fi finalizată ("commit"), a fost invalidată de către declanşatorul "%1$s".
530 Expresie regulată absentă.
531 Expresia regulată este prea lungă sau prea complexă.
532 Paranteze sau acolade dezechilibrate în expresia regulată.
533 Referinţa este în afara domeniului în expresia regulată.
534 Caracter sau sintaxă nepermisă în expresia regulată.
535 Niciun text furnizat pentru căutarea cu expresii regulate.
536 Cod de operaţie nepermis detectat în expresia regulată.
537 Cod invalid de operaţie de căutare pentru compilatorul de expresii regulate.
538 Bufferul de lucru pentru expresii regulate trebuie iniţializat.
539 Încercare de diviziune prin 0.
540 %1$s.
541 Numărul de pagini libere pentru volumul "%1$s" nu este consistent. %2$d şi %3$d au fost detectate comform antetului, respectiv  hărţii de memorie pentru volum.
542 Numărul de sectoare libere pentru volumul "%1$s" nu este consistent. %2$d şi %3$d au fost detectate comform antetului, respectiv  hărţii de memorie pentru volum.
543 Antetul de disc pentru volumul "%1$s" nu este consistent.
544 Eroare internă: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Cheia de B+arbore %6$s pentru obiectul cu OID: %7$d|%8$d|%9$d nu a fost găsită în B+arbore: %10$d|%11$d|%12$d.
545 Eroare internă: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Cheia şi OID-ul: %6$d|%7$d|%8$d din B+arbore: %9$d|%10$d|%11$d sunt incorecte. Obiectul nu există.
546 Eroare internă: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Se aşteaptă %6$d valori OID, dar %7$d au fost găsite în B+arbore: %8$d|%9$d|%10$d.
547 Versiunea de server %1$s diferă de versiunea de client %2$s.
548 Nu este definit un object_id pentru clasa proxy "%1$s".
549 Instanţa proxy nu a fost gasită.
550 Nume necunoscut pentru punctul de salvare: %1$s
551 Nu s-a putut localiza fişierul de informaţii despre volum "%1$s"... Continuă citirea din tabele interne
552 Depăşire negativă a capacităţii în artimetica datelor.
553 Depăşire negativă a capacităţii în aritmetica timpului.
$ LOADDB
554 Loader-ul a intrat într-o stare invalidă. Nu mai sunt posibile alte operaţii.
555 Eroare la alocarea de memorie în timpul încărcării bazei de date.
556 Prea multe valori furnizate. Se aşteaptă %1$d.
557 Pentru atributul %1$s din %2$s, tipul %3$s nu este permis în domeniul mulţimii.
558 Pentru atributul %1$s din %2$s, tipul aşteptat este %3$s, s-a recepţionat o constantă multime.
559 Pentru atributul %1$s din %2$s, tipul aşteptat este %3$s, s-a recepţionat %4$s.
560 Pentru atributul %1$s din %2$s, domeniul nu este suficient de specific pentru a permite \nreferinţe la obiecte incomplet definite.
561 Nu sunt permise mulţimi imbricate.
562 Clasa sistem %1$s nu poate fi populată la încarcarea bazei de date.
563 Referinţa la clasa internă %1$s a fost convertită la NULL.
564 Încălcare a constrângerii UNIQUE pentru atributul %1$s din %2$s.
565 Metoda constructor %1$s nu este definită pe clasa %2$s.
566 Nicio clasă specificată sau niciun atribut în clasă.
567 Prea multe argumente furnizate metodei constructor. Se aşteaptă %1$d.
568 Parametri absenţi pentru metoda constructor. Se aşteaptă %1$d, s-au găsit %2$d.
569 Valori absente pentru atribute. Se aşteaptă %1$d, s-au găsit %2$d.
570 Fişierul de date Glo "%1$s" nu este accesibil.
571 Instanţa a fost creată anterior printr-o referinţă "forward".\nAcest lucru nu este permis pentru instanţe create utilizând metode constructor.
572 EROARE INTERNĂ: Nu se poate construi reprezentarea pe disc pentru instanţă.
573 EROARE INTERNĂ: Nu se poate insera o nouă instanţă în baza de date.
574 EROARE INTERNĂ: Nu se poate actualiza o nouă instanţă în baza de date.
575 Pentru argumentul %1$d al constructorului %2$s, tipul aşteptat este %3$s, s-a recepţionat %4$s.
576 Referinţa la clasa %3$s nu este compatibilă cu domeniul lui %1$s din %2$s.
577 Pentru argumentul %1$d al constructorului %2$s, domeniul nu este suficient de specific \npentru a suporta referinţe la obiecte incomplet definite.
578 Referinţa la clasa %3$s nu este compatibilă cu domeniul argumentului %1$d \n aparţinând constructorului %2$s.
579 Domeniul ţintă trebuie să includă tipul "object" pentru a permite referinţe la clase.
580 Atributul %1$s nu este definit pentru clasa %2$s.
581 Tentativă de actualizare a bazei de date când actualizările sunt dezactivate.
582 Scop necunoscut de stocare %1$d. Intervalul valid este de la %2$d până la %3$d.
583 Tentativă de alocare a unui număr invalid (%1$d) de pagini.
584 Eroare internă: Un ciclu a fost detectat în fişierul %1$d din volumul "%2$s". Ciclul poate fi provocat de către pagina %3$d|%4$d.
585 Heap necunoscut %1$s|%2$d|%3$d
586 Eroare internă: Nu s-a putut actualiza antetul lanţului în VPID %1$d|%2$d pentru heap-ul %3$d din volumul "%4$s".
587 Înregistrările volumelor permanente nu sunt ordonate în fişierul volinfo "%1$s". Înregistrarea %2$d: %3$d %4$s nu este ordonată.
588 EROARE INTERNĂ: Eşec la assert '%1$s'.
589 Nume invalid de utilizator "%1$s".
590 Domeniul de atribut "%1$s" al unei clase nu poate fi \no clasă virtuală sau o clasă proxy virtuală.
591 Domeniul de atribut "%1$s" al unei clase proxy virtuale \ntrebuie să fie o clasă proxy virtuală.
592 Domeniul de atribut "%1$s" al unei clase proxy virtuale \nsi clasa proxy virtuală trebuie să fie în aceeaşi bază de date locală.
593 O instanţă virtuală neactualizabilă a fost recuperată şi nu poate fi reconstruită.
594 O clauză object_id trebuie furnizată fără a utiliza atribute (%1$s) de tip de date aproximative (float sau double).
595 Numele bazei de date "%1$s" este prea lung. Numele bazei de date trebuie să fie mai mic de %2$d caractere.
596 Cele %1$d pagini de spaţiu temporar total permis au fost depăşite.
597 Numărul de pagini pentru fişierul de heap %1$d|%2$d|%3$d nu este consistent. \n%4$d şi %5$d au fost găsite comform lanţului de heap, respectiv tabelei de fişiere.
598 Numărul de fişiere nu este consistent. %1$d erau aşteptate, %2$d au fost găsite
599 O eroare I/O s-a produs la sincronizarea stării volumului "%1$s".
600 Funcţia "%1$s" nu este implementată pe PC.
601 "%1$s".
602 Eroare dl: "%1$s" este definit de mai multe ori.
603 Eroare internă: Tabela de sectore/pagini pentru fişierul VFID %1$d|%2$d pare a fi coruptă.
604 Eroare internă: Mulţimea de pagini alocate pentru fişierul VFID %1$d|%2$d(Volumul "%3$s") pare a fi inconsistentă în %4$d pagini şterse sau marcate ca şterse şi %5$d goluri de pagină în tabelă
605 Eroare internă: Număr inconsistent de pagini marcate ca şterse pentru fişierul VFID %1$d|%2$d(Volume "%3$s"). %4$d aşteptate, %5$d găsite.
606 Nu s-a putut localiza fişierul odbc.ini cu calea "%1$s".
607 Baza de date "%1$s" nu a fost găsită în odbc.ini.
608 Baza de date "%1$s" nu are o specificaţie de maşină gazdă în fişierul odbc.ini.
609 Tranzacţia dumneavoastră nu poate fi derulată înapoi deoarece istoricul (logging) a fost ignorat. Baza dumneavoastră de date ar putea fi coruptă.
610 Baza dumneavoastră de date ar putea fi coruptă deoarece istoricul (logging) a fost dezactivat când baza de date s-a defectat.
611 %1$s nu poate fi executat în modul client/server.
612 Istoricul (Logging) nu poate fi dezactivat la acest moment deoarece există acţiuni de recuperare aflate în aşteptare.
613 În clasa "%3$s", componenta locală "%1$s" nu poate servi ca substitut de alias pentru clasa "%2$s". Domeniile sunt incompatibile.
614 Numărul maxim (%1$d) de arhive de log active a fost depăşit.
615 Nu s-a putut stabili legatura cu interfaţa Winsock. Eroare Winsock: %1$d.
616 Nu se poate determina numele maşinii utilizând interfaţa Winsock. Eroare Winsock: %1$d.
617 Nu se poate determina id-ul maşinii utilizând interfaţa Winsock. Pentru această maşină trebuie să existe o înregistrare în tabela de nume de gazdă. Eroare Winsock: %1$d.
618 Atributul nu este actualizabil.
619 Codeset sursă eronat.
620 Codeset destinaţie eronat.
621 Tip de date invalid. Argumentele conţin un tip de date invalid.
622 Codeset-uri incompatibile. Codeset-urile argumentelor nu sunt compatibile.
623 Secvenţă de escapare invalidă. O secvenţă de escapare invalidă a provocat o eroare în compilatorul de expresii regulate.
624 Caracter de escapare invalid. Caracterul de escapare este nenul cu o lungime mai mare ca 1.
625 Eroare internă. Tentativă de actualizare a unui şablon eronat de atribute obiect %1$d|%2$d|%3$d cu instanţa de obiect %4$d|%5$d|%6$d.
626 Eroare internă. %1$d atribute solicitate nu au fost găsite.
627 Operand invalid pentru trim. Operandul lui trim are o lungime mai mare decât 1.
628 Tip de valută invalid: %1$d.
629 Dimensiunea de pagină %1$d nu este o putere a lui 2 sau este prea mică. O dimensiune de pagină %2$d este utilizată în loc.
630 Conversia nu este suportată de %1$s.
631 Instrucţiunea SQL a încălcat constrângerea NOT NULL.
632 %1$s nu este un volum de rezervă.
633 %1$s este o copie de rezervă a bazei de date %2$s create la %3$s în locul bazei de date %4$s create la %5$s
634 Un volum/fişier de bază de date era aşteptat în copia de rezervă.
635 AVERTISMENT: Rezervorul de %1$d buffere de log pare prea mic pentru mediul curent.\n Este posibil ca unele buffere să nu fie eliberate de sistem datorită unei ERORI NECUNOSCUTE\n sau deoarece există multe tranzacţii concurente abandonate simultan. Rezervorul de buffere de log este mărit cu încă %2$d buffere.
636 AVERTISMENT: Num_buffers %1$d este prea mic. Se presupune valoarea minimă %2$d.
637 Eroare de sistem: solicitare de eliberare a unei zone de memorie prea mari pentru bufferul de log. Dimensiunea fixă este setată la zero.
638 Avertisment: Operaţie de flush asupra paginii de log neactualizabile cu ID = %1$d
639 Eroare de sistem: Pagina secvenţială de log anterioară %1$d nu este pagina %2$d pentru care se face flush întârziat.
640 Eroare de sistem: un punct de salvare nu poate fi adăugat când tranzacţia nu este activă (de ex. în timpul finalizării prin "commit" sau în timpul derulării înapoi).
641 Un nume trebuie furnizat pentru un punct de salvare.
642 Eroare de sistem: nu există o operaţie superioară de sistem pentru tranzacţia curentă.
643 Posibilă eroare de sistem: Finalizarea/abandonarea tranzacţiei = %1$d (index = %2$d) care are ataşate \noperaţii permanente. Acele operaţii de sistem vor fi ataşate tranzacţiei
644 Eroare fatală de log: %1$s
645 EROARE DE SISTEM: Starea = %1$s a tranzacţiei = %2$d (index = %3$d) \n nu este o stare adecvată pentru aplicarea acţiunilor de client de tip postpone/undo ... ignorat
646 EROARE DE SISTEM: Aparent, o înregistrare de compensare este absentă pentru indexul rcvindex = %1$s de log logic\n. Fără această înregistrare de compensare, recuperarea după un crash nu va fi efectuată corect.
647 Copia de rezervă "%1$s" nu aparţine bazei de date.
648 Copia de rezervă nu este compatibilă cu actuala "%1$s" versiunea "%2$s".
649 Precizia de %1$d furnizată nu este validă; trebuie să fie mai mare ca %2$d şi mai mică decât %3$d.
650 Nu se pot crea suficiente fire de execuţie pentru 'max_clients=%1$d'.
651 Nu există suficientă memorie virtuală pentru a prealoca %1$d buffere de sortare pentru fiecare dintre cele %2$d fire de execuţie.
652 Offsetul eronat %1$d a fost furnizat pentru descompunerea înregistrării de lungime %2$d stocată în slotul %3$d.
653 Offsetul eronat %1$d sau lungimea %2$d au fost furnizate pentru ştergerea unei porţiuni de date din înregistrarea de lungime %3$d stocată în slotul %4$d.
654 Offestul eronat %1$d sau lungimea %2$d au fost furnizate pentru suprascrierea unei porţiuni de date din înregistrarea de lungime %3$d stocată în slotul %4$d
655 Fişierul "%1$s" de configurare al handlerelor de evenimente nu poate fi deschis.
656 Avertisment: Id/name NULL în fişierul de configurare al handlerelor de evenimente "%1$s", linia %2$d
657 Avertisment: Nume de eveniment nepermis, "%1$s", în fişierul de configurare al handlerelor de evenimente "%2$s", linia %3$d
658 Avertisment: Id-ul de eveniment %1$d, nu se află în intervalul de IDuri de eroare %2$d <> %3$d, fişier "%4$s", linia %5$d
659 Procesul handler de evenimente "%1$s" nu este accesibil.
660 Nu se poate deschide/conecta procesul handler de evenimente, "%1$s".
661 Eşec la scrierea în procesul handler de evenimente, "%1$s".
662 Eşec la iniţializarea mecanismului de manipulare a evenimentelor.
663 Avertisment: Linie prea lungă în fişierul de configurare al handlerelor de evenimente, "%1$s", linia %2$d, se face trunchiere
664 Mecanism de manipulare a evenimentelor pornit.
665 Pipe defect pentru handlerul de evenimente, "%1$s"
666 Mecanism de manipulare a evenimentelor oprit.
667 Nu se poate face corespondenţa dintre clasa '%1$s' şi clasa C++ echivalentă.
668 Eroare internă: Tip de domeniu necunoscut '%1$d'.
669 Serverul a refuzat conexiunea client: s-a depăşit numărul maxim de clienţi (%1$d).
670 Operaţia ar fi provocat încălcarea uneia sau mai multor constrângeri de unicitate. INDEX %1$s%2$s ON CLASS %3$s%4$s. cheie: %5$s%6$s.
671 O eroare de citire/scriere din reţea s-a produs pentru socketul %1$d asociat tranzacţiei dumneavoastră (index %2$d, %3$s@%4$s|%5$d)
672 Socketul %1$d asociat tranzacţiei dumneavoastră (index %2$d, %3$s@%4$s|%5$d) este închis.
673 Conexiunea dumneavoastră la server a fost inchisă deoarece serverul se va opri imediat.
674 Nu sunt suficiente fire de execuţie active pentru a continua execuţia curentă a serverului. Numărul actual de fire de execuţie active este %1$d. Aţi putea avea nevoie de până la %2$d fire de execuţie pentru o execuţie curată a serverului în mediul dumneavoastră. Indexul de tranzacţie %3$d a expirat forţat pentru a continua execuţia serverului.
675 Nu se poate citi fişierul de localizare '%1$s' al bazei de date.
676 Baza de date '%1$s' nu poate fi găsită în fişierul de localizare '%2$s'
677 Eşec la conectarea la serverul de baze de date '%1$s', pe urmatoarele maşini gazdă: %2$s
678 Eroare internă. Modulul de repornire client a recepţionat un nume vid (NULL) de bază de date.
679 Această tranzacţie C++ deja a început.
680 Tentativă de depăşire a nivelului maxim (%1$d) de imbricare al tranzacţiilor C++.
681 Tranzacţia %1$s a fost încercata pe o tranzacţie C++ care nu a început încă.
682 Nu se poate face %1$s pentru tranzacţia C++ inainte de încheierea tuturor tranzacţiilor subordonate.
683 Tentativă de a crea un şir de caractere de lungime nepermisă - %1$d
684 Metoda a întors un cod de eroare %1$d definit de utilizator.
685 Clasa "%1$s" nu este validă.
686 Notificare: handlerul de evenimente a fost repornit.
687 Nu se poate face conversie forţată de la (%1$s *) la (%2$s *).
688 Index în afara domeniului. Încercare de a accesa elementul %1$lld. Dimensiunea colecţiei = %2$lld.
689 Eroare internă. Iteratorul se află într-o stare de corupţie.
690 Tentativă de a dereferenţia o instanţă neiniţializată de odb_Ref<>.
691 Tentativă de utilizare a unui iterator invalid.
692 Tentativă de utilizare a unui iterator cu o colecţie necorelată.
693 Depăşire a capacităţii pentru tipul de date Numeric.
694 EROARE DE SISTEM: Nu s-a putut încărca un B+arbore.
695 O interfaţă eronată a fost utilizată pentru a închide un client.
696 Tranzacţie necunoscută (index %1$d, %2$s@%3$s|%4$d).
697 Indexul de tranzacţie furnizat (index %1$d, %2$s@%3$s|%4$d) nu se potriveşte cu indexul tranzacţiei curente (index %5$d, %6$s@%7$s|%8$d). Probabil tranzacţia furnizată s-a finalizat, iar indexul său a fost atribuit altei tranzacţii.
698 Eroare internă: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Numărul de OIDuri din ierarhia de unicate: %6$d nu este egal cu numărul de OIDuri, %7$d, găsite în arborele B+ de unicate plus numărul de NULLuri, %8$d, găsite în ierarhia de unicate pentru arborele B+: %9$d|%10$d|%11$d.
699 Eroare internă: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Numărul de OIDuri din ierarhia de unicate, %6$d, nu este egal cu numărul de OIDuri, %7$d, găsite în statisticile rădăcina pentru arborele B+: %8$d|%9$d|%10$d.
700 Eroare internă: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Numărul de OIDuri: %6$d nu este egal cu numărul de NULLuri: %7$d plus numărul de chei: %8$d din arborele B+ de unicate: %9$d|%10$d|%11$d.
701 Nu există un proxy definit pe "%1$s" în baza de date locală (LDB) "%2$s".
702 Eroare internă: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Numărul de NULLuri din ierarhia de unicate, %6$d, nu este egal cu numărul de NULLuri, %7$d, găsite în statisticile rădăcina pentru arborele B+: %8$d|%9$d|%10$d.
703 Eroare internă: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). OID: %6$d|%7$d|%8$d găsit în arborele B+ de unicate: %9$d|%10$d|%11$d nu aparţine uneia din clasele cu constrângere de unicitate.
704 Evaluarea funcţiei generice de server a eşuat.
705 %1$d volume permanente inconsistente au fost găsite când %2$d erau aşteptate.
706 Nu se pot extinde volumele permanente. Volumul permanent "%1$s" cu scopul de %2$s a fost furnizat.
707 Nu se poate extinde volumul temporar "%1$s" cu %2$d pagini
708 Nu se poate extinde volumul de disc temporar "%1$s" cu %2$d pagini (%3$lld Kbytes) datorită spaţiului insuficient. Spaţiul actual disponibil este de %4$d pagini (%5$lld Kbytes).
709 Constrângerea "%1$s" nu a fost găsită.
710 Constrângere invalidă.
711 Nu există niciun obiect cu valoarea de atribut furnizată.
712 Constrângerea "%1$s" deja există.
713 Nu există un index pe acest atribut.
714 Interogarea a eşuat datorită spaţiului temporar insuficient.
715 Verificarea schemei %1$s a eşuat.
716 Constrângerile NOT NULL nu sunt permise în clase virtuale sau proxy.
717 Constrângerile de unicitate (UNIQUE) nu sunt permise în clase virtuale sau proxy.
718 Dispozitiv invalid.
719 Numărul de atribute (%1$d) si cel de valori(%2$d) nu sunt egale.
720 Nu se poate folsi un dispozitiv pentru inserarea într-o clasă virtuală sau proxy.
721 Interogarea de inserare întoarce mai mult de un rând.
722 Număr greşit de atribute pentru constrângerea NOT NULL.
723 Nu se poate modifica domeniul unui atribut existent.
724 Unul sau mai mulţi parametri invalizi pentru generarea numelui implicit pentru constrângere.
725 Eroare internă: se aşteptă un OID temporar şi s-a întâlnit un OID permanent.
726 Constrângerea de unicitate (UNIQUE) trebuie adăugătă utilizănd o instructiune ALTER separată\ndupă ce instanţele au fost create. De exemplu, în loc de:\n\talter table1 add attribute a int unique;\nutilizaţi:\n\talter table1 add attribute a int;\n\talter table1 add unique(a);
727 Eveniment invalid de declanşator.
728 Interogarea a eşuat deoarece VFIDul de fişier temporar este invalid pentru tranzacţia %1$d.
729 Overflow produs în contextul scăderii.
730 Overflow produs în contextul înmulţirii.
731 Overflow produs în contextul împarţirii.
732 Overflow produs în contextul minusului unar.
733 Overflow produs în contextul coerciţiei.
734 Eroare internă: Antetul pentru fişierul VFID %1$d|%2$d(Volum "%3$s") este corupt.
735 Index de tranzacţie necunoscut %1$d.
736 Trebuie specificat un identificator de serviciu sau de port. Eroare Winsock: %1$d.
737 Serviciu TCP necunoscut: %1$s. Eroare Winsock: %2$d.
738 Nu s-a putut crea fluxul de date pentru socket. Eroare Winsock: %1$d.
739 Imposibilă ataşarea la adresa locală... se încearcă din nou. Eroare Winsock: %1$d.
740 Imposibilă ataşarea la adresa locală... se abandonează. Eroare Winsock: %1$d.
741 Operaţia de "Accept" a eşuat. Eroare Winsock: %1$d.
742 Se trimite un mesaj de "broadcast" către client. Eroare Winsock: %1$d.
743 Eşec la procesul de handshake dintre client şi server. (peer host %1$s)
744 Eroare la reevaluarea hash-ului pentru un MOP de proxy/clasă virtuală.
745 Etapă invalidă de "garbage collection".
746 Callback invalid pentru "garbage collection".
747 Tichet de callback invalid pentru "garbage collection".
748 Pointer invalid de obiect întâlnit de către db_gc_mark_object.
749 Numărul de insţante preluate este necorespunzator: %1$d aşteptate, %2$d recepţionate.
750 Numărul de insţante preluate este necorespunzator: %1$d aşteptate, %2$d recepţionate.
751 OIDul unui obiect nou nu este disponibil într-un declanşator de tip BEFORE INSERT.
752 Eroare la restaurarea copiei de siguranţă cu unit_num %1$d.
753 Eroare la restaurarea copiei de siguranţă cu unit_num %1$d, pageid %2$d depăşeşte numărul total de pagini %3$d pentru volumul cu volid %4$d.
754 Instanţa proxy OO are cheie nulă sau un tip care nu este şir de caractere.
755 %1$s nu poate fi executat în modul independent.
756 Bufferul specificat pentru şirul de caractere este prea mic pentru a codifica acest obiect.
757 Eroare internă: nu se poate atribui un OID permanent în timpul codificării obiectelor.
758 Şirul de caractere specificat nu este valid pentru decodificarea obiectului.
759 Obiectul ţintă a fost şters.
760 Eroare internă: Instanţa proxy a obiectului are cheie NULL în timpul codificării obiectului.
761 Eroare internă: Instanţa vclass a obiectului se mapează la o instanţă reală invalidă.
762 Eroare internă la rezolvarea instanţei de vclass la o instanţă reală.
763 Eroare internă la codificarea instanţei neactualizabile a obiectului.
764 Eroare internă la codificarea obiectului sub formă de vobj intern.
765 Obiectul din argumentul de ieşire are o adresă NULL.
766 Eroare internă la decodificarea obiectului.
767 Nu se poate crea o copie de rezervă de nivel %1$d fără a crea înainte o copie de rezervă de nivel %2$d.
768 Baza de date locală "%1$s" a fost accesată în timpul acestei sesiuni, modificarea sau ştergerea(ALTER, DROP) nu sunt permise.
769 Atributul "%1$s", al clasei "%2$s" nu este un atribut valid de clasă sau partajat.
770 Nu există functii de tabele.
771 Parametru invalid pentru funcţie.
772 Clasa db_serial nu a fost găsită.
773 Serialul "%1$s" nu a fost găsit.
774 Serialul "%1$s" deja există.
775 Urmatoarea valoare depăşeste valoarea MAX(MIN).
776 Nu s-a putut prelua obiectul serial.
777 Nu s-a putut actualiza obiectul serial.
778 Nu s-a putut crea o legatură simbolică "%1$s" către fişierul "%2$s".
779 Rezultatul depăşeste limitele: anul din Date TREBUIE să fie cuprins între 1 si 9999.
780 Eroare internă de sistem: imposibil de preluat ceasul/ora sistemului.
781 Stringul de formatare este prea lung. Trebuie să fie mai mic de 16K.
782 Şir de caractere vid nepermis aici.
783 Format invalid.
784 Două argumente nu corespund.
785 Şirul de caractere din primul argument este prea lung. Trebuie să fie mai mic de 16K.
786 Formatul este duplicat.
787 Eroare de conversie în formatul de timp.
788 Eroare de conversie în formatul de timestamp.
789 Număr greşit.
790 Execuţia interogării a fost înteruptă de utilizator.
791 Valoare invalidă pentru serial.
792 Nu s-a putut aloca memorie pentru css.
793 pthread_attr_init() a eşuat.
794 pthread_attr_destroy() a eşuat.
795 pthread_attr_setdetachstate() a eşuat.
796 pthread_attr_setscope() a eşuat.
797 pthread_attr_setstacksize() a eşuat.
798 pthread_create() a eşuat.
799 pthread_join() a eşuat.
800 pthread_mutex_init() a eşuat.
801 pthread_mutex_destroy() a eşuat.
802 pthread_mutex_lock() a eşuat.
803 pthread_mutex_trylock() a eşuat.
804 pthread_mutex_unlock() a eşuat.
805 pthread_mutexattr_init() a eşuat.
806 pthread_mutexattr_destroy() a eşuat.
807 pthread_mutexattr_settype() a eşuat.
808 pthread_mutexattr_gettype() a eşuat.
809 pthread_cond_init() a eşuat.
810 pthread_cond_destroy() a eşuat.
811 pthread_cond_wait() a eşuat.
812 pthread_cond_timedwait() a eşuat.
813 pthread_cond_signal() a eşuat.
814 pthread_cond_broadcast() a eşuat.
815 pthread_key_create() a eşuat.
816 pthread_key_delete() a eşuat.
817 pthread_setspecific() a eşuat.
818 pthread_getspecific() a eşuat.
819 pthread_once() a eşuat.
820 Nu s-a putut iniţializa o conexiune.
821 Se opreşte o conexiune.
822 Operaţia peste mutex a eşuat în timpul preluării următorului ID de client.
823 Operaţia peste mutex a eşuat în timpul iniţializării listei de conexiuni.
824 Nu se poate distruge un mutex în timpul finalizării listei de conexiuni.
825 Apel eşuat la pread().
826 Apel eşuat la pwrite().
827 Funcţia de parcurgere a listei de conexiuni a întors o valoare invalidă.
828 S-a furnizat o secţiune critică invalidă pentru index.
829 Acest lock de mutex a fost deja eliberat.
830 Nu se mai pot aloca înregistrări pentru interogări noi. Numărul maxim de interogari alocate este de %1$d.
831 Toţi descriptorii (%1$d) de tranzacţie sunt în uz. Creşteţi valoarea parametrului max_clients.
832 Procesul de crearea copiilor de siguranţă se aflăa deja în execuţie şi nu poate fi executat de două procese simultan.
833 Numele de index "%1$s" este ambiguu. Specificaţi numele clasei indexului.
834 Există disconcordanţe între şirul de caractere sursă "%1$s" şi şirul de formatare "%2$s" date în to_number().
835 Element de statistică necunoscut.
836 Latch-ul pe pagina(%1$d|%2$d) a expirat.
837 Un utilizator care deţine obiecte în baza de date(clase/declanşatori/seriale etc) nu poate fi şters.
838 Nu se pot deschide simultan mai mult de %1$d indecşi.
839 "%1$s": Parametru de sistem necunoscut sau valoare invalidă.
840 Parametrul de sistem "%1$s" nu poate fi schimbat.
841 Operaţia solicitată poate fi executată de către un singur client(tranzacţie).
842 Lock-ul pentru obiectul %1$d|%2$d|%3$d nu poate fi stocat în memoria cache.
843 Argumente eronate în %1$s, un %2$s a fost furnizat.
844 Nivel de izolare necunoscut(%1$d). Indexul tranzacţiei este %2$d.
845 Tip de obiect invalid (%1$d). Obiectul blocabil este %2$d|%3$d|%4$d.
846 %1$s de tranzacţie(%2$d) nu se găseşte în lista de deţinători ai obiectului (%3$d|%4$d|%5$d).
847 %1$s pe un %2$s(%3$d|%4$d|%5$d) nu se găseşte în lista deţinătorilor tranzacţiei cu indexul %6$d (elemente în listă: %7$d).
848 %1$s pe un obiect(%2$d|%3$d|%4$d) nu se găseşte în lista non2pl a tranzacţiei(%5$d).
849 Încercare multipla de abandon a unei tranzacţii(%1$d).
850 Tranzacţia(%1$d) nu este nici lock holder, nici lock waiter al unui obiect(%2$d|%3$d|%4$d).
851 Alocarea de resurse pentru lock (%1$s) a eşuat.
852 Modul total de holders(%1$d) al unei liste vide de lock holders este NULL_LOCK.
853 Pentru un obiect(%1$d|%2$d|%3$d), tranzacţia(%4$d) de tip Uncommitted Read deţine un lock defect(%5$s).
854 AVERTISMENT: Nu există niciun lock holder, dar există lock waiters. Consultaţi fişierul %1$s.
855 AVERTISMENT: Unul sau mai multe fire de execuţie se află în starea de lock-waiting pentru o tranzacţie(%1$d).
856 AVERTISMENT: Stare lockwait(%1$d|%2$d), fir de execuţie(%3$d|%4$d) şi tranzacţie(%5$d) neobişnuite.
857 O eroare I/O s-a produs la trunchierea paginii %1$d din volumul "%2$s".
858 Volumul "%1$s" nu este cunoscut la repornirea serverului.
859 Latch-ul pe pagina(%1$d|%2$d) a fost anulat.
860 Tranzacţia curentă nu poate fi integrată în tranzacţia globală deoarece starea sa este '%1$s'.
861 Nu se pot efectua pregătirile de finalizare (commit) a tranzacţiei curente cu identificatorul %1$d deoarece nu face parte dintr-o tranzacţie globală.
862 Nu se pot seta informaţiile de utilizator ale tranzacţiei globale cu identificatorul %1$d deoarece starea sa este '%2$s'.
863 Nu se pot prelua informaţiile de utilizator ale tranzacţiei globale cu identificatorul %1$d deoarece nu sunt setate.
864 Nu se poate deschide fişierul de mesaje de ieşire '%1$s' generat de utilitarul backupdb/restoredb.
865 Instrucţiunea nu poate fi executată datorită marcatorilor de intrare de tip necunoscut.
866 Cod de eroare neutilizat.
867 Cod de eroare neutilizat.
868 Cod de eroare neutilizat.
869 Cod de eroare neutilizat.
870 Cod de eroare neutilizat.
871 Cod de eroare neutilizat.
872 Cod de eroare neutilizat.
873 Eroare de autentificare PKI "%1$s".
874 Argumentul lui power() este în afara domeniului.
875 Overflow în contextul funcţiei power.
876 Cod de eroare neutilizat.
877 Nu există date disponibile pentru unload.
878 Fişier invalid de localizare a bazei de date, '%1$s'.
879 Drepturile nu pot fi citite: '%1$s'.
880 Drepturile '%1$s' nu pot fi modificate.
881 Nu se poate obţine un lock pentru fişierul '%1$s(FILE ID:%2$d)'.
882 Nu se poate elibera lock-ul pentru fişierul 'FILE ID:%1$d'.
883 Eroare la citirea stării fişierului '%1$s'.
884 Calea Volext-pathname '%1$s' nu poate fi scrisă.
885 Eşec la mutarea de la '%1$s' la '%2$s'. Utilizaţi acelaşi dispozitiv (disc).
886 "%1$s" a provocat o încălcare a constrângerii de unicitate.
887 Procedura stocată '%1$s' deja există.
888 Numărul de parametri este invalid. %1$d asteptaţi, %2$d prezenţi.
889 Eroare de execuţie a procedurii stocate: %1$s
890 Partiţionarea a eşuat.
891 Nu există o partiţie corespunzătoare.
892 Cheia primară "%2$s" este deja definită pentru clasa "%1$s".
893 Atributul '%1$s', membru al cheii primare, nu a putut fi şters.
894 Procedura/funcţia stocată '%1$s' nu există.
895 Tip invalid de procedură stocată: '%1$s' este '%2$s'.
896 Compresie eşuată. Metoda Zip: %1$d (%2$s), Nivel Zip: %3$d (%4$s)
897 Decompresie eşuată.
898 Eroare de replicare: "%1$s"
899 Cerere invalidă către un obiect partiţionat.
900 Libraria maşinii virtuale Java nu a fost găsită: %1$s.
901 Maşina virtuală Java nu poate fi pornită: %1$s.
902 Maşina virtuală Java nu se află în execuţie.
903 Conexiunea cu maşina virtuală Java nu poate fi realizată: %1$s
904 Nume invalid de procedurăa stocată.
905 Eroare de comunicare prin reţea cu maşina virtuală Java: %1$d
906 Signatură invalidă pentru metoda Java.
907 Proprietarul sau un membru al grupului de administratori (DBA) poate şterge procedura stocată.
908 Prea multe argumente pentru procedura stocată '%1$s'.
909 Catalog invalid sau absent.
910 Accesul la partiţie nu este permis.
911 Apel invalid: nu poate întoarce un ResultSet.
912 ResultSet nu poate fi furnizat ca parametru de intrare.
913 Prea multe apeluri imbricate de proceduri stocate.
914 Un obiect serial există deja ca şi constrângere de autoincrementare.
915 Valoarea de incrementare pentru o constrângere de tip "auto increment" trebuie să fie mai mare decât 0.
916 Volumul de log activ nu este inclus în această copie de siguranţă a volumului(%1$s).
917 Volumul de log activ deja există.
918 Clasa '%1$s' referenţiată de cheia externă nu există.
919 Clasa '%1$s' referenţiată de cheia externă nu conţine o cheie primară.
920 Cheia externă '%1$s' nu include membrul cheii primare '%2$s'.
921 Domeniul membrului cheie straină '%1$s' este diferit de cel al membrului cheie primară '%2$s'.
922 Constrângerea de cheie primară '%1$s' este invalidă datorită valorii '%2$s'.
923 Cheia primară '%1$s' referenţiată de o cheie externă '%2$s' nu trebuie ştearsă.
924 Operaţiile de actualizare/ştergere (update/delete) sunt restricţionate de cheia externă '%1$s'.
925 Obţinerea unui lock pentru cheia externă '%1$s' a eşuat.
926 Instanţa având cheia externă '%1$s' nu poate fi ştearsă.
927 Numărul de chei ale cheii externe '%1$s' este diferit de cel al cheii primare '%2$s'.
928 Nu un utilizator, ci sistemul poate aloca atributul '%1$s' creat de 'ON CACHE OBJECT' al cheii externe.
929 O cheie externă nu poate fi definită într-o clasă virtuală.
930 Este posibilă ştergerea atributului '%1$s' creat de 'ON CACHE OBJECT' al cheii externe doar după ştergerea constrângerii de cheie externă.
931 Valoarea de început a unei constrângeri de tip "auto increment" trebuie sa fie mai mică decât valoarea maximă admisă.
932 Nu se poate repara o inconsistenţă a numărului de pagini libere din volumul "%1$s", deoarece %2$d pagini obtinute din antetul volumui nu sunt consistente cu %3$d pagini din harta de memorie (bitmap).
933 Nu se poate repara o inconsistenţă a numărului de sectoare libere din volumul "%1$s", deoarece %2$d sectoare obtinute din antetul volumului sunt inconsistente cu %3$d sectoare din harta de memorie (bitmap).
934 Domeniu referenţiat nul.
935 Argumentul funcţiei %1$s este în afara domeniului.
936 Overflow produs în contextul funcţiei exp.
937 Eroare de bază de date.
938 Argument invalid.
939 Prea multe conexiuni.
940 Handle invalid.
941 Operaţia nu este suportata.
942 Handle expirat în aşteptarea lock-urilor.
943 Eroare generică.
944 Instrucţiunea nu este preparată ("prepared").
945 Instrucţiunea nu are un set de rezultate.
946 Instrucţiunea nu este executată.
947 Nu mai exista seturi de rezultate.
948 Mărimea bufferului nu este suficientă.
949 Nu există informaţii disponibile.
950 Numele este invalid.
951 Acest ResultSet nu poate fi actualizat/modificat.
952 Rândul este sters.
953 Parametri instrucţiunii preparate nu sunt setaţi.
954 Instrucţiunea nu este una de tip "batch".
955 Nu se poate curăţa această instrucţiune batch.
956 Nu se poate executa în mod "batch".
957 Instrucţiunea este una preparată.
958 Instrucţiunea nu este una preparată.
959 Instrucţiunea este una de tip "batch".
960 Nu există alte erori.
961 Nu există alte rezultate.
962 Memorie epuizată
963 Eroare de Broker.
964 Hande-ul pentru ResultSet este închis.
965 Nume duplicate de atribute nu pot fi folosite în lista de chei de index. Numele de atribut '%1$s' apare mai mult de o singură dată.
966 Tranzacţia dumneavoastră (index %1$d, %2$s@%3$s|%4$d) a expirat în aşteptarea unui lock %5$s pe obiectul %6$d|%7$d|%8$d datorită unui deadlock. Aşteptaţi ca următorii utilizatori să termine: %9$s.
967 Tranzacţia dumneavoastră (index %1$d, %2$s@%3$s|%4$d) a expirat în aşteptarea unui lock %5$s pe clasa %6$s datorită unui deadlock. Aşteptaţi ca următorii utilizatori să termine: %7$s.
968 Tranzacţia dumneavoastră (index %1$d, %2$s@%3$s|%4$d) a expirat în aşteptarea unui lock %5$s pe instanţa %6$d|%7$d|%8$d a clasei %9$s datorită unui deadlock. Aşteptaţi ca următorii utilizatori să termine: %10$s.
969 Arhitectura platformei de server (%1$d bit) diferă de platforma client (%2$d bit).
970 Modul de server HA este schimbat din '%1$s' în '%2$s'.
971 Programul '%1$s' (pid %2$d) s-a conectat la serverul '%3$s' pe maşina gazdă '%4$s' (port %5$d).
972 Programul '%1$s' (pid %2$d) a fost conectat de pe maşina gazdă '%3$s'. (index de tranzacţie %4$d)
973 Starea serverului este %1$s.
974 Arhiva de log "%1$s" este creată pentru arhivarea paginilor de la %2$lld până la %3$lld.
975 Versiunea curentă de replicare nu permite schimbarea de rânduri multiple printr-o singură instrucţiune UPDATE, care ar putea incălca constrângerea de unicitate.
976 Eroare internă: Depăşire a dimensiunii tabelei (dimensiune alocată: %1$d, dimensiune accesată: %2$d) la pagina %3$d|%4$d(volumul "%5$s")
977 Punct de control pornit (IDul paginii punctului de control anterior: %1$lld, IDul de pagină "redo" anterior: %2$lld).
978 Checkpoint finished (checkpoint page id: %1$lld, checkpoint redo page id: %2$lld, flushed page count: %3$d).
979 Ciclu de CONNECT BY în datele utilizatorului.
980 Un fir de execuţie aşteaptă în %1$s pentru mai mult de %2$d msec.
981 Tranzacţia %1$d a atins pragul statisticii de monitorizare %2$s.
982 Comandă de întrerupere trimisă către tranzacţia %1$d.
983 Instanţele unei clase OID reutilizabile nu pot fi referenţiate. Această operaţie nu este permisă pentru instanţe nereferenţiabile.
984 Nu se poate seta %3$s la "%6$s" în secţiunea "[%5$s]" când este setat la "%4$s" în secţiunea "[%2$s]" a fişierului de configurare "%1$s".
985 Numele de gazdă din stringul de conexiune la baza de date trebuie specificat atunci când opţiunea de gazde multiple este setată în "databases.txt".
986 Modulul CUBRID Heartbeat a pornit.
987 Modulul CUBRID Heartbeat a fost oprit.
988 Eveniment la nod: %1$s.
989 Eveniment la proces: %1$s. %2$s
990 Execuţie de comandă: %1$s. %2$s
991 A început procesul de flush_victim_candidates pentru bufferul de pagini.
992 S-a finalizat procesul de flush_victim_candidates pentru bufferul de pagini (total: %1$d).
993 Capacitate depaşită în contextul operaţiei pe biţi.
994 Această intrucţiune nu poate fi una preparată.
995 O instrucţiune preparată cu numele %1$s nu există.
996 Număr incorect de valori furnizate. %1$d valori au fost furnizate, dar %2$d valori erau necesare.
997 Nu se poate adăuga constrângerea de cheie externă %1$s clasei partiţionate %2$s. Acţiuni referenţiale care actualizează atribute nu sunt încă suportate pe clase partiţionate.
998 Nu se poate adăuga constrângerea de cheie externă %1$s pe atributul %2$s. Atributul nu poate avea o constrângere NOT NULL deoarece trebuie să corespundă acţiunii referenţiale SET NULL.
999 Nu se poate crea tabela '%1$s' similară tabelei '%2$s' deoarece tabela existentă conţine elemente de definiţie care nu pot fi duplicate (%3$s).
1000 '%1$s' este o clasă (CLASS) sau un atribut partajat (SHARED) dar numai atribute obişnuite sunt permise în acest context.
1001 O condiţie sau o stare neaşteptată a fost atinsă. '%1$s'
1002 Tranzacţia curentă nu a putut obţine un lock pe baza de date, de care are nevoie pentru a putea continua.
1003 Constrângerea '%1$s' există, dar este de tip diferit.
1004 Vă rugăm să reţineţi faptul că la utilizarea ALTER TABLE DROP FOREIGN KEY, CUBRID şterge atât constrângerea, cât şi indexul său asociat.
1005 Cod de eroare neutilizat.
1006 Cod de eroare neutilizat.
1007 Nu se poate crea un index prefixat peste constrângeri de unicitate, de cheie primară sau externă.
1008 Nu se poate crea un index prefixat peste clase partiţionate.
1009 COMPACTDB deja pornit.
1010 Nu se poate crea un index prefixat peste domeniul "%1$s".
1011 Nu se poate adăuga constrângerea de cheie externă %1$s pe atributul partajat %2$s.
1012 Nu se poate crea o tabelă cu coloane de tip NULL.
1013 Adăugarea lui DEFAULT sau SHARED pe coloana de tip %1$s nu este permisă.
1014 Adăugarea lui NOT NULL pe coloana de tip %1$s nu este permisă.
1015 Directorul specificat '%1$s' nu există.
1016 Eroare de mediu de stocare extern (%1$s): %2$s
1017 Calea către mediul de stocare extern '%1$s' este invalidă.
1018 Nu se poate copia fişierul extern într-un un altfel de mediu de stocare. (sursa %1$s, destinaţia %2$s)
1019 Memoria externă nu este iniţializată deoarece calea nu este specificată în "databases.txt".
1020 Fişierul extern "%1$s" nu a fost găsit.
1021 S-a detectat un ciclu de deadlock. %1$s.
1022 Valoare invalidă "%1$d" pentru lungimea indexului prefixat.
1023 Cod de eroare rezervat pentru HA.
1024 log writer: failed to get log page(s) starting from page id %1$lld.
1025 HA delay info: The replication delay (%1$d secs) has exceeded the limit (%2$d secs).
1026 HA delay info: The replication delay (%1$d secs) is decreased below the acceptable level (%2$d secs).
1027 log applier: eşec la schimbarea stării de aplicare din '%1$s' în '%2$s'.
1028 log applier: înregistrare EOF neaşteptată în log. LSA: %1$lld|%2$d.
1029 log applier: pagină/offset invalid în logul de replicare. HDR de pagină: %1$1lld|%2$d, final: %3$lld|%4$d, APPEND LSA: %5$lld|%6$d, EOF LSA: %7$lld|%8$d, stare HA a fişierului: %9$d, este end-of-log: %10$d.
1030 log applier: înregistrare de replicare invalidă. LSA: %1$lld|%2$d, forw LSA: %3$lld|%4$d, backw LSA: %5$lld|%6$d, Trid: %7$d, prev tran LSA: %8$lld|%9$d, type: %10$d.
1031 log applier: failed to apply schema replication log. class: "%1$s", schema: "%2$s", server error: %3$d, %4$s. 
1032 log applier: failed to apply insert replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1033 log applier: failed to apply update replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1034 log applier: failed to apply delete replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1035 log applier: Dimensiunea memoriei(%1$d MB) aplicatorului de log este mai mare decât dimensiunea maximă admisă (%2$d MB) sau a fost extinsă de mai mult de 2 ori (%3$d MB). LSA necesar: %4$lld|%5$d. Ultimul LSA salvat prin COMMIT: %6$lld|%7$d.
1036 log applier: oprit la primirea unui semnal.
1037 log writer: oprit la primirea unui semnal.
1038 log applier: pornit. LSA necesar: %1$lld|%2$d. Ultimul LSA salvat prin COMMIT: %3$lld|%4$d. Ultimul repl-LSA salvat prin COMMIT: %5$lld|%6$d. 
1039 log writer: pornit. Mod: %1$d.
1040 Eroare generică de HA: %1$s. 
1041 Un şir de caractere a fost trunchiat în contextul "%1$s".
1042 Tentativă de a crea un şir de caractere care ocupă %1$d bytes de memorie, când maximul permis este %2$d bytes.
1043 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica proprietăţile atributului solicitat dacă clasa are subclase.
1044 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica atributele care sunt chei de partiţionare.
1045 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica atributele utilizate în chei externe (fie referinţe, fie referenţiate).
1046 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica domeniul atributului. Această schimbare de domeniu nu este suportată.
1048 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica momentan AUTO_INCREMENT-ul unui atribut dacă se modifică şi domeniul atributului.
1047 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica domeniul atributului. Noul domeniu ar putea să nu reprezinte corect toate valorile existente ale atributului.
1049 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica momentan domeniul atributului. Conversia de domeniu este posibilă, dar nu este permisă de configuraţia actuală.
1050 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica momentan domeniul atributului. Atributul are un index definit peste el.
1051 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica momentan atributul prin adăugarea unei constrângeri. Valorile existente pentru atribut ar putea invalida constrângerea.
1052 Atributul "%1$s" nu a fost modificat. CUBRID nu a detectat modificări concrete în reprezentarea atributului.
1053 Instrucţiunile REPLACE INTO... si INSERT ... ON DUPLICATE KEY UPDATE nu sunt permise pentru clase care fac parte dintr-un lanţ de moştenire.
1054 Noua valoare a unei constrângeri "auto increment" trebuie să fie mai mare sau egală cu valoarea curentă.
1055 Noua valoare a unei constrângeri "auto increment" trebuie să fie mai mică decât valoarea maximă admisă.
1056 Pentru a evita ambiguitatea, opţiunea AUTO_INCREMENT a tabelei solicită ca tabela să aibă exact o coloană AUTO_INCREMENT şi nicio valoare de increment specificată.
1057 Nu se poate adăuga un atribut NOT NULL de tip '%1$s' fără o valoare implicită (DEFAULT).
1058 ALTER TABLE .. CHANGE : schimbare la domeniu nou : conversie (cast) eşuată, configuraţia curentă nu permite trunchierea sau depăşirea domeniului de valori permise.
1059 ALTER TABLE .. CHANGE : schimbare la domeniu nou : conversie (cast) eşuată, valoarea implicită a noului domeniu a fost setată.
1060 ALTER TABLE .. CHANGE : schimbare la domeniu nou : conversie (cast) eşuata, valoarea minimă a noului domeniu a fost setată.
1061 ALTER TABLE .. CHANGE : schimbare la domeniu nou : conversie (cast) eşuata, valoarea maximă a noului domeniu a fost setată.
1062 Nu se poate modifica atributul "%1$s". Modificarea ar cauza existenţ mai multor chei primare definite.
1063 Nu se poate adăuga constrângerea NULL pentru atributul "%1$s": există valori NULL pentru acest atribut.
1064 ALTER TABLE .. CHANGE : se adaugă NOT NULL : toate valorile NULL existente au fost modificate la valoarea implicită a tipului de date.
1065 Nu se poate modifica atributul "%1$s". CUBRID nu poate adăuga constrângerea NOT NULL : o valoare implicită nu există pentru noul tip de date.
1066 Sesiunea a expirat.
1067 Coloana '%1$s' nu poate avea o constrângere NOT NULL şi valoarea implicita NULL.
1068 Adâncimea maximă a declanşatorilor nu poate depăşi %1$d.
1069 Prea multe instrucţiuni preparate.
1070 Variabilă de sesiune '@%1$s' nedefinită.
1071 Prea multe variabile de sesiune.
1072 Adresa IP (%1$s) nu este autorizată.
1073 Format invalid în fişier(%1$s).
1074 Nu se poate citi lista de acces din fişier(%1$s).
1075 Scanarea descrescătoare a indexului a fost abandonată datorită priorităţii mai mici de pe arborele B+ cu identificatorul de index: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
1076 Parametrul de sistem nu a putut fi incărcat.
1077 Parametrul '%1$s' de la linia %2$d din fişierul '%3$s' : parametru necunoscut
1078 Caracterul de la offsetul %1$d este invalid în codeset-ul curent.
1079 Codeset-urile bazei de date(%1$s) şi configuraţia serverului(%2$s) sunt incompatibile.
1080 Lungimea maximă a şirului de caractere al predicatului de filtrare trebuie să fie %1$d.
1081 Nu se poate modifica un atribut implicat într-un index funcţional sau de filtrare.
1082 Toate bufferele de pagină sunt modificate ("dirty"), LRU: %1$d.
1083 Informaţii specifice despre deadlock.\n%1$s
1084 Se ignoră o pagină invalidă din punctul de control. (ID pagina: %1$d, "%2$s", oldest_unflush_lsa: %3$lld|%4$d, lsa "redo" în punctul de control anterior: %5$lld|%6$d)
1085 Nu se poate compila expresia regulată: '%1$s'
1086 Nu se poate executa expresia regulată: '%1$s'
1087 Se creează o copie de siguranţă a logului activ '%1$s'.
1088 S-a finalizat copia de siguranţă a logului activ '%1$s'.
1089 Alocare de resurse eşuată. Creşteţi valoarea lui 'max_bestspace_entries=%1$d'.
1090 Iniţializarea localizării: %1$s.
1091 Generarea localizării: %1$s.
1092 Nu se poate prepara instrucţiunea de tip '%1$s' cu variabile de gazdă ("host variable").
1093 Argumentul '%1$s' pentru inet_aton nu este o adresă IPv4 sub formă de şir de caractere corect formatat.
1094 Argumentul '%1$lld' pentru inet_ntoa nu este o adresă IPv4 sub formă de număr corect formatat.
1095 Numele de utilizator este prea lung.
1096 Modificarea schemei de partiţionare nu este permisă cand clasa partiţionată este referenţiată de către o cheie externă.
1097 Coloana '%1$s' nu poate avea o constrângere PRIMARY KEY şi o valoare implicită NULL.
1098 Localizările pentru limba '%1$s' nu sunt disponibile cu codesetul '%2$s'.
1099 Instrucţiunile DDL nu sunt permise de către configuraţia curentă (block_ddl_statement=yes).\n Text SQL: %1$s
1100 Instrucţiunile fără clauza WHERE nu sunt permise de către configuraţia curentă (block_nowhere_statement=yes).
1101 Cod de eroare neutilizat.
1102 Parametru de sistem necunoscut sau valoare invalidă.
1103 Parametrul de sistem nu poate fi modificat.
1104 Interogare lentă (%1$d msec)\n%2$s
1105 Eroare internă: eşec parţial la salvarea prin "flush" a unui obiect modificat ("dirty"). 
1106 Rânduri multiple din tabela sursă corespund aceluiaşi rând din tabela destinaţie.
1107 Număr de bucket invalid pentru NTILE. Trebuie să fie mai mare ca zero şi mai mic ca 2,147,483,647.
1108 Număr de bucket invalid pentru WIDTH_BUCKET. Trebuie să fie mai mare ca zero şi mai mic ca 2,147,483,647.
1109 Valoare invalidă pentru definiţia partiţiei.
1110 Spaţiu insuficient în pagina frunză a arborelui B+. (lungimea cheii: %1$d)\n%2$s
1111 Valoarea rezultată este trunchiată deoarece precizia (%1$d) este mai mică decât lungimea sa(%2$d).
1112 Un caracter nu a putut fi convertit din '%1$s' în '%2$s'.
1113 Indexul elementului este în afara domeniului.
1114 Argumentul '%1$s' este în afara domeniului.
1115 A început actualizarea statisticilor (clasa "%1$s", oid : %2$d|%3$d|%4$d).
1116 S-a finalizat actualizarea statisticilor (clasa "%1$s", oid : %2$d|%3$d|%4$d, cod de eroare : %5$d).
1117 Nu se poate modifica atributul "%1$s". CUBRID nu poate modifica un atribut în SHARED şi nici reciproc.
1118 Argumentul lui "%1$s" nu poate fi convertit forţat la domeniul dorit "%2$s".
1119 Eroare internă: IDul de pagină %1$d este alocat comform harţii de alocare a volumului "%2$s", dar nu aparţine niciunui fişier. 
1120 Eroare internă: IDul de pagină %1$d din volumul "%2$s" se afla în uz, dar nu este alocat comform harţii de alocare a volumului.
1121 Constrângerile NOT NULL nu permit valoarea NULL.
1122 Timp de aşteptare expirat la recepţionarea datelor.
1123 pthread_cond_timedwait() a expirat. 
1124 Eroare de execuţie a interogării. COD DE EROARE = %1$d, /* SQL_ID: %2$s */ %3$s
1125 Crearea unui fişier de chei de overflow. INDEX %1$s%2$s ON CLASS %3$s%4$s. cheie: %5$s%6$s.
1126 Crearea unei pagini noi de overflow. INDEX %1$s%2$s ON CLASS %3$s%4$s. cheie: %5$s%6$s.
1127 Ştergerea unei pagini goale de overflow. INDEX %1$s%2$s ON CLASS %3$s%4$s. cheie: %5$s%6$s.
1128 Restaurarea logului a început. Numărul de înregistrări din log ce trebuie aplicate: %1$lld. Pagina de log: %2$lld ~ %3$lld.
1129 Restaurarea logului s-a finalizat.
1130 Tranzacţia a fost abandonată deoarece numărul de lock-uri asociate tranzacţiei depăşeşte %1$d.
1131 Eroare internă: Pagina %1$d din volumul "%2$s" ar putea fi coruptă.\n%3$s
1132 Encryption library failure: %1$s
1133 Copylogdb pentru %1$s există deja.
1134 Applylogdb pentru %1$s există deja.
1135 O legatură predecesor a arborelui B+(%1$s) este coruptă.: vpid = (%2$d, %3$d)
1136 O legatură predecesor a arborelui B+(%1$s) este reparată: vpid = (%2$d, %3$d)
1137 Interogarea a fost respinsă din cauza încercării de a depăşi nivelul maxim permis(%1$d) de imbricare.
1138 Eroare la stabilirea conexiunii (gazdă omoloagă %1$s): intreruptibilitate incompatibilă. (client: %2$s, server: %3$s)
1139 Eroare la stabilirea conexiunii (gazdă omoloagă %1$s): mod citire/scriere incompatibil. (client: %2$s, server: %3$s)
1140 Eroare la stabilirea conexiunii (gazdă omoloagă %1$s): replicare HA întârziată.
1141 Eroare la stabilirea conexiunii (gazdă omoloagă %1$s): client replica-only către server non-replica.
1142 Eroare la stabilirea conexiunii (gazdă omoloagă %1$s): versiune de server neidentificată.
1143 Eroare la stabilirea conexiunii (gazdă omoloagă %1$s): accesul la distanţă către server nu este permis.
1144 Timpul a expirat în încercarea de conectare la %1$s. (timeout: %2$d sec(s))
1145 Schimbarea proprietarului unei clase nu este permisă.
1146 Volumul cu identificatorul %1$d nu există.
1147 Dimensiunea arborelui XASL serializat(%1$d) depăseşte dimensiunea maximă permisă(%2$d). O intrare nouă este adaugată în cache deşi nu există victime, deoarece toate intrările din cache sunt folosite.
1148 Dimensiunea predicatului de filtrare serializat(%1$d) depăseşte dimensiunea maximă permisă(%2$d). O intrare nouă este adaugată în cache deşi nu există victime, deoarece toate intrările din cache sunt folosite.
1149 Pagina %1$d a volumului %2$d nu poate fi găsită.
1150 Contextul necesită colaţii compatibile.
1151 The page %1$d of volume %2$d is not a slotted page.
1152 The owner or a member of DBA group is only allowed to kill the transaction (%1$d).
1153 Attempt to use 'zero date'.
1154 MVCC row %1$d|%2$d|%3$d does not satisfies reevaluation.
1155 MVCC invalid row for delete: %1$d|%2$d|%3$d.
1156 MVCC can't get snapshot
1157 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.
1158 Serializable conflict due to concurent updates
1159 Ultima eroare

$set 6 MSGCAT_SET_INTERNAL
1 Eroare în subsistemul de erori (linia %1$d):
2 Niciun mesaj de eroare disponibil.
3 Nu se pot aloca %1$d bytes.
4 Nu se poate găsi catalogul de mesaje "%1$s".
5 Mesaj absent pentru codul de eroare %1$d.
6 ... ABANDON/IESIRE IMEDIATA ...\n
7 Doriti sa ieşiţi? 1/0
8 fişier necunoscut.
9 Niciun mesaj disponibil; acest mesaj înlocuieşte mesajul original, care este formatat eronat.
10 er_init: *** Valoare incorectă pentru exit_ask = %1$d; se va presupune valoarea %2$s în loc. ***\n
11 er_start: *** WARNING: Nu s-a putut deschide logul de mesaje "%1$s"; se va presupune stderr în loc. ***\n
12 er_study_fmt: mesaj suspect pentru codul de eroare %1$d.
13 er_estimate_size: cod de conversie necunoscut (%1$d).
14 \n\n*** Înfăşurarea logului de mesaje. Mesajele se vor continua la începutul fişierului. ***\n\n
15 \nTimp: %1$s - %2$s *** %3$s COD = %4$d, Tran = %5$d%6$s\n%7$s\n
16 CUBRID (pid: %1$d) *** %2$s *** %3$s COD = %4$d, Tran = %5$d %6$s
17 \nTimp: %1$s - %2$s *** fişier %3$s, linie %4$d %5$s COD = %6$d Tran = %7$d%8$s\n%9$s\n
18 CUBRID (pid: %1$d) *** %2$s *** fişier %3$s, linie %4$d, %5$s COD = %6$d, Tran = %7$d. %8$s
19 \n*** Mesajul de eroare anterior este ultimul. ***\n\n
20 \nTimp: %1$s - DEBUG *** fişier %2$s, linie %3$d\n
21 Mail -s "Serverul a fost oprit"
22 --->>>\n %1$s a fost oprit la solicitarea dumneavoastră când următoarea eroare era setată:\n errid = %2$d, %3$s\nUtilizator: %4$s, pid: %5$d, gazdă: %6$s, timp: %7$s<<<---\n
23 %1$s a fost oprit cu eroarea errid = %2$d. Utilizator: %3$s, pid: %4$d
24 er_init: nu se poate instala handlerul de evenimente "%1$s"\n

$set 7 MSGCAT_SET_PARSER_SYNTAX
1 vă rugăm verificaţi sintaxa la '%1$s', se aşteaptă %2$s.
2 vă rugăm verificaţi sintaxa la '%1$s', se aşteaptă %2$s în %3$s.
3 vă rugăm verificaţi corectitudinea tokenului '%1$s'.
4 'read uncommitted schema' nu este acceptat, se foloseşte \n'read committed schema' în loc.
5 Lungimea maximă pentru şiruri de caractere este 16383.
6 %1$s depăşeste limita de %2$lld.
7 Lungimea maximă în biţi pentru un şir de caractere este %1$d bits.
8 Nu se poate utiliza o subinterogare în clauza condiţie pentru JOIN.
9 Nu se poate specifica atributul "(%1$s)" în clauza de actualizarea drepturilor din instrucţiunile grant/revoke.
10 utilizare invalidă a cuvântului cheie '%1$s', se aşteaptă %2$s.
11 Lungimea tokenului este prea mare.
12 tip de date invalid
13 instrucţiune invalidă: attach\n  attach_statement: ATTACH unsigned_integer
14 instrucţiune invalidă: prepare\n  prepare_statement:  PREPARE {to} COMMIT unsigned_integer
15 instrucţiune invalidă: execute\n  execute_statement:  EXECUTE DEFERRED TRIGGER [identifier {, identifier}* | ALL TRIGGERS]
16 instrucţiune invalidă: scope\n  scope_statement: SCOPE trigger_action { FROM table_spec_list }
17 instrucţiune invalidă: set system parameter\n  set_system: SET SYSTEM PARAMETERS 'STRING' {, 'STRING_N'}*
18 instrucţiune invalidă: set transaction\n  set_transaction: SET TRANSACTION ISOLATION LEVEL expression {, expression}\n                 | SET TRANSACTION LOCK TIMEOUT timeout_spec
19 instrucţiune invalidă: set trigger trace\n  set_trigger: SET TRIGGER TRACE [ON|OFF|unsigned_integer|parameter|host_param]
20 instrucţiune invalidă: set trigger depth\n  set_trigger: SET TRIGGER {MAXIMUM} DEPTH [unsigned_integer|parameter|host_param]
21 instrucţiune invalidă: set optimization level\n  SET OPTIMIZATION LEVEL {=|to} [ON|OFF|unsigned_integer|parameter|host_param]
22 instrucţiune invalidă: set optimization cost\n  SET OPTIMIZATION COST {of} 'STRING' {=|to} literal_
23 instrucţiune invalidă: get optimization level\n  GET OPTIMIZATION LEVEL {[INTO|TO] [host_param|parameter|identifier]}
24 instrucţiune invalidă: get optimization cost\n  GET OPTIMIZATION COST {of} 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
25 instrucţiune invalidă: get transaction isol\n  GET TRANSACTION ISOL LEVEL {[INTO|TO] [host_param|parameter|identifier]}
26 instrucţiune invalidă: get transaction lock\n  GET TRANSACTION LOCK TIMEOUT {[INTO|TO] [host_param|parameter|identifier]}
27 instrucţiune invalidă: get trigger trace\n  GET TRIGGER TRACE {[INTO|TO] [host_param|parameter|identifier]}
28 instrucţiune invalidă: get trigger depth\n  GET TRIGGER {MAXIMUM} DEPTH {[INTO|TO] [host_param|parameter|identifier]}
29 instrucţiune invalidă: get stat\n  GET STATISTICS 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
30 instrucţiune invalidă: create index\n  CREATE {REVERSE} {UNIQUE} INDEX identifier ON {only} class_name index_column_name_list {WHERE where_clause}
31 instrucţiune invalidă: create user\n  CREATE USER identifier {PASSWORD 'STRING'} {GROUPS identifier {, identifier}* } {MEMBERS identifier {, identifier}* }
32 instrucţiune invalidă: create trigger\n  CREATE TRIGGER identifier {STATE [ACTIVE|INACTIVE]} {PRIORITY unsigned_real}\n    [BEFORE|AFTER|DEFERRED] event_spec {IF trigger_condition}\n    EXECUTE {AFTER|DEFERRED} trigger_action
33 instrucţiune invalidă: create serial\n  CREATE SERIAL identifier {START WITH integer} {INCREMENT BY integer}\n    serial_min_max {CYCLE|NOCYCLE} {CACHE unsigned_integer|NOCACHE}
34 instrucţiune invalidă: create procedure\n  CREATE PROCEDURE identifier '(' {sp_param_list} ')' {IS|AS} LANGUAGE JAVA NAME 'STRING'
35 instrucţiune invalidă: create function\n  CREATE FUNCTION identifier '(' {sp_param_list} ')' RETURN {data_type|CURSOR} {IS|AS} LANGUAGE JAVA NAME 'STRING'
36 instrucţiune invalidă: groups\n  groups spec: GROUPS identifier {, identifier}*
37 instrucţiune invalidă: members\n  member spec: MEMBERS identifier {, identifier}*
38 instrucţiune invalidă: password\n  password spec: PASSWORD 'STRING'
39 lista de comenzi permise este absentă\n  comenzi permise = SELECT, INSERT, INDEX, DELETE, UPDATE, ALTER, ADD, DROP, EXECUTE, REFERENCES, ALL PRIVILEGES, ALL 
40 lista specificaţiilor de clasă este absentă
41 lista de identificatori este absentă
42 expresie absentă
43 identificator invalid
44 utilizare invalidă a lui substring\n  SUBSTRING( expression FROM expression FOR expression )\n  SUBSTRING( expression FROM expression )
45 utilizare invalidă a lui trim\n  TRIM ( [LEADING|TRAILING|BOTH] expression FROM expression )\n  TRIM ( [LEADING|TRAILING|BOTH] FROM expression )\n  TRIM ( expression FROM expression )\n  TRIM ( expression )
46 utilizare invalidă a lui cast\n  CAST ( expression AS data_type )
47 funcţia add_months necesită doi parametri
48 octet_length necesită un parametru
49 bit_length necesită un parametru
50 lower necesită un parametru
51 upper necesită un parametru
52 translate necesită trei parametri
53 replace necesită doi sau trei parametri
54 utilizare invalidă a lui to_number\n  TO_NUMBER ( expression , char_string )\n  TO_NUMBER ( expression )
55 operatorul de concatenare de şiruri de caractere nu este '|', ci '||'
56 instrucţiune invalidă: alter
57 instrucţiune invalidă: drop
58 instrucţiune invalidă: create
59 operator relaţional invalid
60 operatorul de egalitate nu este '==', ci '='
61 operatorul de diferenţiere nu este '!=', ci '<>'
62 valoare cu minus invalidă (sau o valoare cu minus este prezentă în listă)
63 regulă de referenţiere duplicată
64 SYS_CONNECT_BY_PATH necesită doi parametri şi al doilea trebuie să fie un şir de caractere.
65 întreg fără semn %1$s invalid (unsigned integer).
66 IF necesită trei parametri: primul trebuie să fie o expresie logică, al doilea şi al treilea trebuie să fie expresii care se evaluează la un tip numeric sau string.
67 IFNULL necesită doi parametri care trebuie să fie expresii ce se evaluează la un tip numeric sau string.
68 utilizare invalidă a lui isnull\n  ISNULL ( expression )
69 utilizare invalidă a lui left\n  LEFT ( str , len )
70 utilizare invalidă a lui right\n  RIGHT ( str , len )
71 utilizare invalidă a lui mod\n MOD( expression, expression )
72 utilizare invalidă a lui adddate\n ADDDATE( expression, expression )
73 utilizare invalidă a lui date_add\n DATE_ADD( expression, INTERVAL expression unit)
74 utilizare invalidă a lui subdate\n SUBDATE( expression, expression )
75 utilizare invalidă a lui date_sub\n DATE_SUB( expression, INTERVAL expression unit)
76 utilizare invalidă a lui user / system_user\n USER() / SYSTEM_USER()
77 utilizare invalidă a lui timestamp\n TIMESTAMP( expression ), TIMESTAMP( expression, expression )
78 utilizare invalidă a lui database\n DATABASE()
79 utilizare invalidă a lui current_time\n CURRENT_TIME()
80 utilizare invalidă a lui current_date\n CURRENT_DATE()
81 utilizare invalidă a lui current_timestamp\n CURRENT_TIMESTAMP()
82 utilizare invalidă a lui now\n NOW()
83 utilizare invalidă a lui default\n DEFAULT(column_name)
84 utilizare invalidă a lui date\n DATE(expression)
85 utilizare invalidă a lui localtime\n LOCALTIME()
86 utilizare invalidă a lui localtimestamp\n LOCALTIMESTAMP()
87 utilizare invalidă a lui schema\n SCHEMA()
88 utilizare invalidă a lui truncate\n TRUNCATE( expression, expression )
89 utilizare invalidă a lui str_to_date\n STR_TO_DATE( expression, string_literal )
90 utilizare invalidă a lui current_datetime\n CURRENT_DATETIME()
91 utilizare invalidă a lui insert\n INSERT( expression, expression , expression, expression )
92 utilizare invalidă a lui elt\n ELT(expression [, expression]...)
93 utilizare invalidă a lui year\n YEAR( expression )
94 utilizare invalidă a lui month\n MONTH( expression )
95 utilizare invalidă a lui day\n DAY( expression )
96 utilizare invalidă a lui hour\n HOUR( expression )
97 utilizare invalidă a lui minute\n MINUTE( expression )
98 utilizare invalidă a lui second\n SECOND( expression )
99 utilizare invalidă a lui quarter\n QUARTER( expression )
100 utilizare invalidă a lui week\n WEEK( expression ), WEEK( expression, expression)
101 utilizare invalidă a lui time\n TIME(expression)
102 utilizare invalidă a lui group_concat\n GROUP_CONCAT (([DISTINCT] {col | expr} \n [ORDER BY {col | unsigned_int} [ASC | DESC] ] \n [SEPARATOR str_val])
103 utilizare invalidă a unui index hint
104 Dimensiunea maximă în bytes a unui şir de caractere este %1$d bytes.
105 Eroare de sintaxă: nu se aşteapta
106 , expecting '{'
107 , expecting SELECT or '('
108 , expecting
109 unexpected 
110 END OF STATEMENT
111 In line %1$d, column %2$d before '%3$c%4$s'\n
112 In line %1$d, column %2$d before %3$s\n 
113 before ' %1$s'\n
114 before END OF STATEMENT\n
115 utilizare invalidă a lui set names\n  set_names: SET NAMES charset [COLLATE collation]\n
116 utilizare invalidă a lui chr\n  CHR ( number [using charset] )\n
117 utilizare invalidă a lui charset\n  CHARSET ( expression )\n
118 utilizare invalidă a lui collation\n  COLLATION ( expression )\n
119 utilizare invalidă a lui clob_to_char\n  CLOB_TO_CHAR ( expression [using charset] )\n
120 utilizare invalidă a lui index_prefix\n  INDEX_PREFIX ( expression, expression, expression )\n
121 Utilizare invalidă a UNION şi ORDER BY.
122 Expecting ON. Outer Joins require join condition.

$set 8 MSGCAT_SET_PARSER_SEMANTIC
1 Memorie epuizată.
2 Funcţia %1$s nu este definită.
3 Nu se poate localiza fişierul LDB '%1$s' în baza de date.
4 Numărul de coloane, %1$d, din interogarea din stânga nu corespunde\nnumărului de coloane, %2$d, din interogarea din dreapta.
5 '%1$s' nu este compatibil la reuniune (UNION) cu '%2$s'.
6 %1$s nu este definit.
7 Domeniul lui %1$s nu poate fi o clasă virtuală (vclass).
8 Domeniul lui %1$s nu poate fi o clasă virtuală proxy.
9 Domeniul lui %1$s nu poate fi o clasă.
10 %1$s nu este un atribut al lui %2$s.
11 %1$s nu poate fi schimbat din interiorul acestei clase.\nA fost moştenit din %2$s.
12 Clasa %1$s nu există.
13 %1$s nu este un %2$s.
14 %1$s nu este o metodă sau un atribut al lui %2$s.
15 %1$s nu are şi nu trebuie sa aibă obiect de interogare.
16 %1$s nu este definit în baza de date.
17 %1$s nu este o superclasă a lui %2$s.
18 Clasa %1$s moşteneşte din non-clasa %2$s.
19 Clasa virtuală %1$s (vclass) moşteneşte din non-vclasa %2$s.
20 Clasa virtuală proxy %1$s moşteneşte din clasa virtuală non-proxy %2$s.
21 %1$s este deja o superclasă a lui %2$s.
22 %1$s nu poate fi o superclasă pentru ea însăşi.
23 %1$s este deja o subclasă a lui %2$s.
24 %1$s nu are o singură valoare. Atributele expuse în\ninterogări agregate trebuie să fie prezente şi în clauza GROUP BY.
25 Numărul de coloane(%1$d) si cel de into_names(%2$d) nu sunt egale.
26 Clasa %1$s deja există.
27 Superclasa %1$s din '%2$s' nu există.
28 Se aşteaptă o clauză SUBCLASS pentru a da sens clauzei INHERIT '%1$s'.
29 Clauza INHERIT '%1$s' referenţiază o clasă care nu se află în clauza SUBCLASS '%2$s'.
30 Clasa nu poate avea un obiect de interogare.
31 %1$s nu este un ldb înregistrat.
32 Un moştenitor proxy nu poate avea propria clauză object_id. Nu poate supraîncărca propriul său atribut moştenit object_id.
33 VClasa proxy are mai mult de o specificaţie de interogare.
34 Numărul de atribute, %1$d, nu corespunde numărului de coloane, \n%2$d, din specificaţia de interogare.
35 Lipseşte un nume de atribut aşteptat pentru %1$s.
36 Atributul '%1$s' nu este compatibil cu coloana '%2$s' din obiectul interogării.
37 %1$s nu este o clasă.
38 Nu se pot crea indecşi peste clase virtuale.
39 Indecşii pot fi creaţi numai pe un singur atribut.
40 Utilizare nepermisă a mai multor clase în clauza FROM.
41 Eroare de sistem -- Rezolvare eşuată la %1$s.
42 Nu se poate executa o ştergere dintr-o tabelă derivată.
43 Utilizatorul %1$s nu se află în baza de date.
44 UPDATE nu poate conţine agregări.
45 Numele de atribute din fiecare clasă trebuie să fie unice. Numele atributului '%1$s' din clasa '%2$s' este specificat mai mult de o singură dată.
46 Există asignări multiple pentru %1$s.%2$s.
47 Numărul din specificaţia de ordonare nu este întreg.
48 %1$d din specificaţia de ordonare este în afara domeniului.
49 Un element din ORDER BY nu este întreg.
50 %1$s din specificaţia de sortare nu corespunde niciunei coloane de selecţie.
51 Există instrucţiuni de sortare contradictorii pentru coloana %1$s.
52 %1$s din specificaţia de sortare nu este un număr, nume sau expresie de tip cale.
53 Expresia INSERT poate fi prezentă numai în instrucţiuni de tip INSERT.
54 Numărul de atribute(%1$d) şi cel de coloane selectate(%2$d) nu sunt egale.
55 Numărul de atribute(%1$d) şi cel de valori(%2$d) nu sunt egale.
56 Depăşire de capacitate pentru tipul de date %1$s.
57 Tip de date incompatibil pentru atributul %1$s.
58 %1$s\n nu este un SELECT dintr-o singură clasă.
59 `*' nu este o listă de selecţie validă dintr-o specificaţie de interogare peste o vclasa proxy.
60 %1$s nu este o clasă virtuală (vclass), nici o clasă virtuală proxy (proxy vclass).
61 Există mai multe clauze ISOLATION LEVEL.
62 Există mai multe clauze TIMEOUT.
63 Nu s-a furnizat un nivel de izolare.
64 Operatorul BETWEEN nu este definit pentru tipurile de date %1$s, %2$s şi %3$s.
65 Operatorul '%1$s' nu este definit pentru tipurile de date %2$s şi %3$s.
66 Funcţia trebuie să aibă argumente: %1$s.
67 Operanzi incompatibili: %1$s(%2$s).
68 Funcţia agregată necesită un argument: %1$s.
69 Tentativă de diviziune prin zero.
70 Depăşire de capacitate la coerciţia lui %1$s la tipul de date %2$s.
71 Nu se poate face coerciţia lui %1$s la tipul de date %2$s.
72 Tipul lui '%1$s' trebuie să fie SET, MULTISET, sau SEQUENCE.
73 '%1$s' trebuie să aibă exact un tip mulţime.
74 %1$s are mai mult de un nume de coloană derivată.
75 %1$s nu are niciun nume de coloană derivată.
76 %1$s nu are un număr corespunzător de coloane (%2$d) şi nume de coloane (%3$d).
77 Nu există atribute în %1$s.
78 Se aşteaptă o singură tabelă în %1$s.
79 Numele expus %1$s este ambiguu.
80 Numele de coloane derivate '%1$s' nu sunt permise pentru specificaţii simple de clasă.
81 Tabela derivată '%1$s' trebuie să aibă un nume de corelare.
82 Eroare de autentificare la SELECT, clasa %1$s.
83 Referinţa instanţei de clasă %1$s la un nume de tabelă derivată nu este permisă.
84 Calea de corelare (%1$s) apare în afara unui termen cale internă.
85 Referinţa la %1$s este ambiguă.
86 Clasa %1$s nu are atributul %2$s.
87 Clasele (%1$s) nu au toate atributele %2$s.
88 Clasa (%1$s) nu are atributul %2$s.
89 %1$s este un tip %2$s, nu un tip obiect.
90 Selectorul {%1$s} nu se rezolvă la o specificaţie de clasa expusă\n în contextul solicitat (%2$s).
91 Selectorul {%1$s} se referă la specificaţia de non-clasă (%2$s).
92 Selectorul {%1$s} nu poate face referire la rădăcina expresiei cale ce-l defineşte.
93 Selectorul {%1$s} este definit circular în termeni de selector {%2$s}.
94 Selectorul {%1$s} este redefinit la %2$s.
95 %1$s (obiect domeniu) nu are atributul %2$s.
96 %1$s din specificaţia EXCEPT nu este o subclasă.
97 Lista ALL ... EXCEPT exclude TOTUL din clasă.
98 '%1$s' este inconsistent cu alte\nutilizări ale selectorului de cale '%2$s' sau ale rădăcinii '%3$s'.
99 Argument nedefinit.
100 Argumentul nu este un obiect.
101 %1$s nu este o metodă sau un atribut al clasei %2$s.
102 Specificaţia de interogare trebuie sa nu aiba o clauza INTO.
103 Specificaţia de interogare proxy trebuie să nu aibă expresii agregate,\nclauză GROUP BY, sau clauză HAVING.
104 Specificaţia de interogare proxy trebuie să nu aibă cuvintele cheie \nDISTINCT sau UNIQUE în lista de selecţie.
105 Specificaţia de interogare proxy trebuie să fie o instrucţiune simplă de SELECT.
106 Specificaţia de interogare proxy trebuie să referenţieze o singură tabelă sau clasă \ndin baza de date locală.
107 Utilizatorul '%1$s' nu este proprietar pentru %2$s.
108 Proxyul '%1$s' este deja definit pe clasa '%2$s' din LDB '%3$s'.
109 CUBRID nu suportă proxyuri cu obiecte în mulţimi eterogene.
110 Clauza WHERE trebuie să conţină o expresie de tip logic.
111 Clauza LIKE-ESCAPE trebuie să conţină un şir de caractere sau o variabilă gazdă (host variable).
112 Combinaţie invalidă de precizie (%1$d) si scală (%2$d).
113 Precizia furnizată %1$d nu este validă; trebuie să fie mai mare decât %2$d şi mai mică decât %3$d.
114 Precizia (%1$d) este prea mare. Precizia maximă este %2$d.
115 Metoda %1$s trebuie să întoarcă un tip.
116 Numărul de argumente furnizat metodei nu este egal cu \nnumărul definit pentru metodă.
117 Apelul la metodă trebuie să aibă o ţintă.
118 Metoda %1$s nu este definită pentru ţinta furnizată.
119 Metodele de instanţă nu pot fi apelate pentru obiecte clasă.
120 Metodele de clasă nu pot fi apelate pentru obiecte instanţă.
121 Metodele de clasă necesită ca ţintă un obiect clasă.
122 Metodele necesită ca ţintă un obiect.
123 Metoda %1$s nu este bine definită, verificaţi definiţia metodei în schema clasei.
124 Depăşire negativă de capacitate în aritmetica tipului de date TIME.
125 Depăsire negativă de capacitate în aritmetica tipului de date DATE.
126 Membrul stâng al unei clauze de asignare conţine informaţii nepermise.
127 Apelurile la metode peste tabele derivate din mulţimi, corelate cu un scop extern, \n sunt suportate numai dacă expresia de mulţime este împachetată într-o interogare.
128 Coloana obiect de interogare, care corespunde atributului partajat\n'%1$s' trebuie să fie NULL sau NA.
129 O coloană NULL nu este permisă în lista de selecţie a obiectului interogării pentru o vizualizare daăa definiţia atributului este omisă.
130 Obiecte de tip VClass în multimi nu sunt suportate.
131 Se aşteaptă o valoare de tip şir de caractere.
132 Se aşteaptă o instrucţiune sau o valoare de tip şir de caracatere.
133 Atributul de tip object_id proxy '%1$s' nu trebuie mapat la \nexpresia coloană a obiectului interogării.
134 Constrângerea %1$s ignorată.
135 Declararea constrângerilor nu este permisă în acest context.
136 Conversia lui %1$s la tipul %2$s nu este suportată.
137 %1$s nu este un string de biţi valid.
138 Operatorul '%1$s' nu este definit pentru tipul de date %2$s.
139 Operatorul '%1$s' nu este definit pentru tipurile de date %2$s cu %3$s şi %4$s.
140 Operanzii operatorului '%1$s' nu pot fi ambii variabile de gazdă dinamice.
141 Prea multe argumente pentru funcţia generică %1$s.\nONumai %2$d argumente sunt permise pentru funcţii generice.
142 Clasa selector {%1$s} cu numele '%2$s' nu este o subclasă pentru '%3$s'.
143 Nu se poate extrage câmpul '%1$s' din tipul %2$s.
144 Specificaţia de interogare trebuie să nu aibă o clauză ORDER BY.
145 Condiţia pentru CASE trebuie să fie o expresie de tip logic.
146 Expresia trebuie să fie de tip %1$s.
147 Serialul '%1$s' deja există.
148 Serialul '%1$s' nu este definit.
149 Valoarea de incrementare trebuie să fie diferită de zero.
150 Valoarea maximă depăşeste 1.0e37.
151 Valoarea minimă este invalidă.
152 Valoarea de incrementare este invalidă.
153 Valoarea minimă este sub -1.0e36.
154 Valoarea maximă este invalidă.
155 Specificaţi opţiuni pentru ALTER SERIAL.
156 Al doilea argument pentru funcţia serial_next_value trebuie să fie mai mare ca 0.
157 Valorile serialului nu pot fi în afara intervalului definit de valoarea minimă şi cea maximă
158 Eroare I/O internă pentru serial
159 Nu se poate găsi indexul '%1$s'.
160 Numele complet dezambiguizat al indexului '%1$s' face referire la o clasă care nu este specificată în clauza FROM.
161 Doua sau mai multe clase au acelaşi index '%1$s'. Utilizati nume de indecşi complet dezambiguizate.
162 Utilizare invalidă a lui %1$s.\nExpresia %2$s nu poate fi utilizată împreună cu un atribut sau o subinterogare corelată.
163 Versiunea curentă restricţionează o expresie din lista de selecţie, expresie ce conţine ORDERBY_NUM().
164 Expresie cale '%1$s' în condiţia de JOIN.
165 '%1$s' din condiţia de JOIN nu este definit.
166 Versiunea curentă restricţioneaza utilizarea tabelelor derivate din mulţimi pentru RIGHT OUTER JOIN.
167 Nu se poate face inserarea prin interogări într-un alt LDB.
168 O singură coloană poate fi specificată în lista de selecţie atunci când subinterogarea nu este introdusă prin EXISTS.
169 Membru drept nepermis pentru o clauză de asignare. Contextul impune ca interogarea să returneze o singură coloană.
170 Nu se poate folosi o subinterogare în clauza condiţională de JOIN.
171 Definiţie de partiţionare invalidă.
172 O coloană de partiţionare trebuie specificată.
173 O singură coloană poate fi specificată ca şi coloană de partiţionare.
174 Tip nesuportat de coloană de partiţionare.
175 Definiţie duplicată pentru partiţie. '%1$s'
176 Disconcordanţă la tipurile de constantă. '%1$s'
177 Dimensiune invalidă a partiţiei.
178 '%1$s' nu este o clasă partiţionată.
179 '%1$s' este deja clasă partitionata.
180 Cereri invalide implicând partiţionare.
181 %1$s nu poate fi modificat din această clasă(coloană cheie de partiţionare).
182 Nu se pot efectua operaţii relative la partiţionare asupra entităţii %1$s atâta timp cât există declanşatori activi.
183 Partiţia %1$s nu există.
184 Tipul de partiţie este necorespunzator. '%1$s'
185 Informaţia de partiţionare este invalidă.
186 Adăugarea unei partiţii noi este făcută pe un interval mai mare decât cel existent.
187 Reorganizarea partiţiilor nu este permisă deoarece poate provoca pierderi de date.
188 Intervalul pentru partiţia '%1$s' este invalid.
189 Un atribut de clasă nu poate avea o constrângere AUTO INCREMENT.
190 Un atribut de clasă virtuală nu poate avea o constrângere AUTO INCREMENT.
191 Domeniul atributului '%1$s' având o constrângere AUTO INCREMENT este invalid.
192 Nu un utilizator, ci sistemul poate asigna atributul '%1$s' creat de către 'ON CACHE OBJECT' al unei chei externe.
193 Nu se pot utiliza nume duplicate ale atributelor în lista de chei de indexare. Numele atributului '%1$s' apare de mai multe ori.
194 Funcţia %1$s este nedefinită sau i s-a furnizat un număr greşit de parametri.
195 '%1$s' poate fi utilizat doar în interogări ierarhice. Specificaţi o clauză CONNECT BY.
196 '%1$s' nu este permis aici.
197 Utilizarea CONNECT_BY_ISCYCLE este permisă numai dacă interogarea are o clauză CONNECT BY NOCYCLE.
198 Nicio tabelă utilizată.
199 ORDER BY NULL necesită GROUP BY.
200 Numărul de stocat în CACHE trebuie să fie mai mic de un ciclu (%1$s).
201 '%1$s' nu este permis într-o interogare cu clauza LIMIT.
202 WITH ROLLUP necesită GROUP BY.
203 Un parametru nume de coloană trebuie trimis funcţiei DEFAULT. Expresii de tip cale nu sunt înca suportate.
204 Opţiune duplicată '%1$s' pentru tabelă
205 Clasa '%1$s' este marcată cu REUSE_OID şi nu poate fi referenţiată. Clasele neferenţiabile nu pot fi domeniu pentru atribute si OIDurile instanţelor acestora nu pot fi întoarse.
206 Opţiune duplicată 'serial %1$s'.
207 Coloana '%1$s' din clauza de ordonare este ambiguă.
208 Atributul '%1$s' nu are o valoare implicită.
209 Elementele ORDER BY trebuie să apară în lista de selecţie dacă SELECT DISTINCT este specificat.
210 Variabila de gazdă (host variable) nu este permisă pentru coloane din obiectul interogare.
211 %1$s nu poate fi o coloană de ORDER BY
212 %1$s nu poate fi o coloană de GROUP BY
213 Nu se poate defini '%1$s of %2$s' ca un tip de coloană.
214 Valoare invalidă "%1$s" pentru lungimea de prefix a indexului.
215 Funcţia '%1$s' nu este definită pentru tipurile %2$s, %3$s, %4$s şi %5$s.
216 Funcţia '%1$s' nu este definită pentru tipurile %2$s, %3$s şi %4$s.
217 Funcţia '%1$s' nu este definită pentru tipurile %2$s şi %3$s.
218 Funcţia '%1$s' nu este definită pentru tipul %2$s.
219 Funcţia '%1$s' nu este definită pentru un argument index de tip %2$s.
220 ALTER VIEW '%1$s' AS SELECT nu este permis pentru vizualizări care conţin subclase sau superclase. Utilizaţi ALTER VIEW CHANGE QUERY în loc.
221 Atributele vizualizării nu pot fi schimbate utilizând această instrucţiune. 'ALTER VIEW nume AS interogare' ar putea fi utilizat în loc.
222 CUBRID necesită ca elementul ORDER BY să fie aceeaşi expresie ca argumentul lui GROUP_CONCAT sau un întreg (poziţia argumentului).
223 Predicatul LIKE necesită specificarea unui caracter de escapare. Consultaţi documentaţia pentru parametrii de configurare asociaţi cu '%1$s' şi '%2$s'.
224 KEYLIMIT nu poate fi utilizat în clauza INDEX NONE.
225 Variabila de sesiune nu este permisă în acest context.
226 Conflict între clauzele de utilizare a indecşilor '%1$s' si '%2$s'.
227 Funcţia '%1$s' nu poate fi utilizată în clauza DEFAULT.
228 Actualizarea tabelelor derivate nu este permisă.
229 Actualizarea atributelor de clasă sau de instanţă nu este permisă în aceeaşi instrucţiune.
230 Actualizarea ierarhiilor claselor virtuale nu este permisă.
231 Variabilele de gazdă (host variables) nu sunt permise în instrucţiuni DDL.
232 Expresie de filtrare invalidă (%1$s) pentru index.
233 Nu sunt permise funcţii multiple sau imbricate în definiţia unui index funcţional.
234 Specificaţie invalidă pentru index.
235 SHARED, DEFAULT si AUTO_INCREMENT nu pot fi definite împreună.
236 Prea multe valori pentru enumerare (%1$d). Un tip de enumerare poate conţine cel mult %2$d valori.
237 Valori duplicate în tipul ENUM.
238 Nu se poate efectua operaţia de MERGE într-o tabelă derivată.
239 Nu se poate modifica tabela sursă printr-o instrucţiune de tip MERGE.
240 MERGE cu ierarhii de clase nu este permis.
241 Numele de utilizator este prea lung.
242 O expresie constantă nu poate fi utilizată ca index funcţional.
243 Numele de clasă sau aliasul nu este unic în lista: '%1$s'
244 Clauză DEFAULT invalidă. '%1$s' nu poate fi utilizat în expresii imbricate.
245 '%1$s' necesită argumente cu colaţii compatibile.
246 Codeset invalid.
247 Codesetul si colaţia necesare nu sunt compatibile.
248 Colaţie necunoscută '%1$s'.
249 Contextul necesită o colecţie cu elemente având codeseturi şi colaţii identice.
250 Un index prefixat nu este permis pe atributul '%1$s' (are colaţie cu expansiuni).
251 Valoarea iniţială este invalidă.
252 Nu se permite un index prefixat peste mai multe coloane.
253 Funcţia '%1$s' nu poate fi utilizată pentru un index funcţional.
254 Definiţie invalidă pentru coloana indexului: %1$s
255 '%1$s' nu este permis într-o expresie de filtrare pentru un index.
256 Nu se pot utiliza GROUPBY_NUM() şi clauza WITH ROLLUP în aceeaşi interogare.
257 Nu se poate efectua instrucţiunea MERGE UPDATE pentru coloana %1$s referenţiată în clauza ON.
258 Numărul de coloane, %1$d, din ( %2$s ) nu corespunde\nnumărului de coloane, %3$d, din ( %4$s ).
259 Prefixele de coloane nu sunt permise în expresiile indecşilor funcţionali.
260 %1$s nu poate fi utilizat drept coloană pentru PARTITION BY sau ORDER BY
261 Interogarea trebuie să evalueze la o singură valoare\n(%1$s).
262 Nu sunt permise funcţii analitice imbricate.
263 Un atribut moştenit nu poate fi utilizat ca cheie de partiţionare.
264 Dimensiunea agregată a elementelor enumerării este prea mare.
265 Nu se poate utiliza tabela ţintă în specificaţia sursei.
266 COLLATE nu este permis în acest context.
267 Modificatorul COLLATE nu poate schimba codesetul (din '%1$s' în '%2$s'). Încercaţi să utilizaţi CAST în loc.
268 Codeset invalid '%1$s' pentru valoarea partiţiei. Se aşteaptă codesetul '%2$s'.
269 Variabila de sesiune nu poate avea altă colaţie decât cea de sistem ('%1$s').
270 Expresie invalidă pentru indexul funcţional.
271 Specificaţia de interogare a lui %1$s conţine o referinţa circulară la el însuşi.
272 Expresia %1$s nu este permisă în condiţia de JOIN.
273 ORDER BY nu este permis îm funcţia MEDIAN.
274 Argumentele funcţiei '%1$s' trebuie să fie constante.
275 Funcţia '%1$s' trebuie să aibă clauza ORDER BY.
276 Pentru funcţia '%1$s', numărul de argumente nu corespunde numărului de coloane ORDER BY.
277 Pentru funcţia '%1$s', clauza ORDER BY este invalidă.
278 O singură tabelă din interiorul unei vizualizări poate fi actualizată.
279 Inserarea în mai multe tabele nu este permisă.
280 '%1$s' type is vclass, drop vclass with cascade constraints is not allowed.
281 The FOR UPDATE clause cannot be used in subqueries, unions, with GROUP BY clause, DISTINCT operator, aggregate functions.
282 Expects %1$s in argument position %2$d.
283 Too many arguments specified.
284 Too few arguments, need %1$d arguments.
285 Expects %1$s for argument %2$s.
286 Argument %1$s is missing.
287 Unknown argument %1$s.
288 Nu se pot şterge toate partiţiile, folosiţi DROP TABLE în schimb.
289 The expression including %1$s is not allowed in this context.
290 Transaction id is not an integer.
291 Subquery is not allowed in ORDER BY FOR clause.

$set 9 MSGCAT_SET_PARSER_RUNTIME
1 Memorie virtuală epuizată: nu s-au putut aloca %1$d bytes.
2 Nivelul de izolare al tranzacţiilor trebuie să fie 1, 2, 3, 4, 5 or 6.
3 Valoarea pentru durata de expirare trebuie să fie -1, 0, or >0.
4 Nivel de izolare setat la:\n
5          Unknown isolation.\n
6          Unknown isolation.\n
7          Unknown isolation.\n
8          READ COMMITTED\n
9          REPEATABLE READ\n
10 O interogare neactualizabilă nu este permisă într-un apel de db_query_execute_oid.
11 Tranzacţia a expirat în aşteptarea lock-urilor.
12 Tentativă de accesare a unei variabile de gazdă cu indexul %1$d când numai %2$d \nvariabile gazdă de intrare au fost date.
13 conversie neimplementată.
14 Eroare internă, conversie nedefinită.
15 '%1$s' nu întoarce rezultate.
16 '%1$s' întoarce mai mult de un singur rând.
17 Variabila de interpretare %1$s nu este definită.
18 Nu se poate evalua '%1$s'.
19 Variabilă necunoscută %1$s.
20 Atribut partajat necunoscut %1$s.
21 '%1$s' întoarce mai mult de o coloană.
22 Utilizare imricată sau invalidă a unei funcţii agregate.
23 Funcţia %1$s nu este definită.
24 Contextul necesită ca interogarea să întoarcă o singură coloană\n(%1$s).
25 Prea multe valori au fost specificate.
26 Ţinta virtuală de asignare (%1$s) nu este inversabilă.
27 Există mai multe coloane în obiectul interogării decât atribute în clasa %1$s.
28 Există mai multe atribute în clasa %1$s decât coloane în obiectul interogării.
29 Versiunea curentă restricţionează utilizarea agregării la clase virtuale FĂRĂ \nspecificaţii de interogare agregate.
30 Versiunea curentă restricţionează utilizarea agregării la clase virtuale FĂRĂ \nspecificaţii de interogare de tip UNION.
31 Utilizarea unei clase virtuale (vclass %1$s) într-o ierarhie de compunere necesită ca clasa să fie actualizabilă.\nNu poate fi actualizată respectând definiţia clasei reale %2$s.
32 neutilizat
33 Construcţie nepermisă în expresia de tip cale '%1$s'.
34 Expresia de tip cale '%1$s' depăşeşte domeniul bazei de date până la %2$s, cu clasa virtuală %3$s.
35 Expresia de tip cale '%1$s' depăşeşte domeniul bazei de date de la %2$s, până la clasa %3$s.
36 %1$s nu are permisiuni pe %2$s.
37 Operaţia de SELECT nu are permisiuni pe %1$s.
38 Clasa virtuală %1$s nu poate fi actualizată.
39 %1$s nu este o clasă virtuală proxy.
40 obiectul interogării pentru clasa virtuală proxy trebuie sa nu fie un JOIN.
41 %1$s nu are un ID de obiect setat.
42 Clasele (%1$s) cu mai multe câmpuri object_id într-o ierarhie\nde compunere nu sunt suportate.
43 Atributul de obiect %1$s din %2$s nu este o clasă virtuală în LDBul %3$s.
44 Tipul obiectului interogării nu este compatibil cu atributul %1$s.
45 Obiectul interogării lui %1$s conţine o referinţă circulară la el însuşi.
46 Nicio expresie dată pentru evaluare.
47 Nicio clasă reală găsită pentru clasa virtuală %1$s.
48 Execuţia comenzii '%1$s' pe LDB '%2$s' \na întors: %3$s.
49 Plasarea valorii '%1$s' în atributul '%2$s' a întors: %3$s.
50 Numele de LDB trebuie să nu fie vid în %1$s.
51 Numele de LDB de pe maşina gazdă trebuie să nu fie vid în %1$s.
53 Numele de gazdă trebuie să nu fie vid în %1$s.
54 Numele de utilizator trebuie să nu fie vid în %1$s.
55 Parola de utilizator trebuie să nu fie vidă în %1$s.
56 INSERT se translatează intr-o instrucţiune vidă.
57 UPDATE se translatează intr-o instrucţiune vidă.
58 DELETE se translatează intr-o instrucţiune vidă.
59 INGRES nu suportă date (%1$s) înainte de 1 ianuarie 1582.
60 Resurse epuizate la generarea interogării.
61 Interogarea nu întoarce rezultate, deci variabila '%1$s' nu este setată.
62 WITH LOCAL CHECK OPTION nu este suportat.
63 Excepţie de la CHECK OPTION pe vizualizarea %1$s.
64 Valoare de tip DATE invalidă: '%1$s'.
65 Valoare de tip TIME invalidă: '%1$s'.
66 Valoare UTIME invalidă: '%1$s'.
67 Nu sunt suportate subinterogări în specificaţiile de interogare pentru proxy.
68 Argumentul funcţiei LDB nu este compatibil cu LDBul în '%1$s'.
69 Valoare numerică invalidă: '%1$s'.
70 Eşec la optimizarea unei interogări de tip OUTER JOIN.
71         SERIALIZABLE.\n
72 Clasa virtuală %1$s nu este actualizabilă. Numai claselor virtuale actualizabile le este permis să selecteze VID-uri.
73 Partiţionare eşuată.
74 Partiţia nu există.
75 Acces restricţionat la partiţie: '%1$s'.
76 Serialul '%1$s' nu este încă definit.
77 Numai proprietarul sau un DBA poate schimba definiţia serialului.
78 Nu este permis utilizatorilor să modifice un obiect serial creat de o constrângere AUTO INCREMENT.
79 Nivel de izolare invalid; schema: %1$s, instanţe: %2$s.
80 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.

$set 11 MSGCAT_SET_AUTHORIZATION
1 *** cache invalid ***
2    clasa %1$s
3 de la %1$s :
4 Drepturi pentru utilizatorul %1$s\n
5 Drepturi pentru utilizator nedefinit\n
6 Utilizator : %1$s\n
7         ID : %1$d\n
8     membri :
9    grupuri :
10 Utilizator %1$s\n
11 Utilizatorul curent este : %1$s\n
12 Obiect de autorizare Root :\n
13   utilizatori :
14 *** Nu s-au putut acorda toate drepturile pentru utilizatorul "%1$s"*** \n
15 DREPTURI\n
16 direct_groups :

$set 12 MSGCAT_SET_HELP
1 Clasa radacină
2 %1$s %2$s\n
3 Superclase :\n
4 Subclases :\n
5 Atribute: \n
6 Metode: \n
7 Atribute de clasă: \n
8 Metode de clasă: \n
9 Rezolvări de conflicte:\n
10 Fişiere de metode:\n
11 Spec. de interogare:\n
12 OBJECT al %1$s\n
13 \nDescriere:\n
14 \nStructură:\n
15 \nExamplu:\n
16 Nume METACLASA
17 Nume clasaă
18 Nume VClasă
$  Don't delete the space at the end of the next message
19 VClass din ldb
20 GENERAL

$set 13 MSGCAT_SET_TRIGGER
1 TRACE: Se evaluează condiţia pentu declanşatorul '%1$s'.
2 TRACE: Se execută acţiunea pentru declanşatorul '%1$s'.

$set 14 MSGCAT_SET_LOCK
1 \n
2 Firul de execuţie (index %1$3d) al tranzacţiei (index %2$3d, %3$s, %4$s@%5$s|%6$d) a fost suspendat.\n
3 Firul de execuţie (index %1$3d) al tranzacţiei (index %2$3d, %3$s, %4$s@%5$s|%6$d) a fost reluat.\n
4 Indexul de tranzacţie %1$3d a OBŢINUT lock-ul %2$s pe obiectul %3$d|%4$d|%5$d.\n
5 Indexul de tranzacţie %1$3d a OBŢINUT lock-ul %2$s pe pagina %3$d|%4$d.\n
6 Indexul de tranzacţie %1$3d a CONVERTIT lock-ul său în lock-ul %2$s pe obiectul %3$d|%4$d|%5$d.\n
7 Indexul de tranzacţie %1$3d a CONVERTIT lock-ul său în lock-ul %2$s pe pagina %3$d|%4$d.\n
8 Indexul de tranzacţie %1$3d AŞTEAPTĂ lock-ul %2$s pe obiectul %3$d|%4$d|%5$d.\n
9 Indexul de tranzacţie %1$3d AŞTEAPTĂ lock-ul %2$s pe pagina %3$d|%4$d.\n
10 Indexul de tranzacţie %1$3d a ELIBERAT lock-ul %2$s pe obiectul %3$d|%4$d|%5$d.\n
11 Indexul de tranzacţie %1$3d a ELIBERAT lock-ul %2$s pe pagina %3$d|%4$d.\n
12 Indexul de tranzacţie %1$3d a RETROGRADAT lock-ul la %2$s pe obiectul %3$d|%4$d|%5$d.\n
13 Indexul de tranzacţie %1$3d a RETROGRADAT lock-ul la %2$s pe pagina %3$d|%4$d.\n
14 OID = %1$2d|%2$6d|%3$4d\n
15 Tip de obiect: clasă Root.\n
16 Tip de obiect: Clasă = %1$s.\n
17 Tip de obiect: Instanţă a clasei (%1$2d|%2$6d|%3$4d) = %4$s.\n
18 Tip de obiect: Necunoscut.\n
19 Modul total de holders = %1$8s, Modul total de waiters = %2$8s.\n
20 Număr de holders=%1$3d, Număre de blocked-holders=%2$3d, Număr de waiters=%3$3d\n
21 LOCK HOLDERS:\n
22 LOCK HOLDERS BLOCATI:\n
23 LOCK WAITERS:\n
24 NON_2PL_RELEASED:\n
25 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Total = %4$3d\n
26 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Total = %4$3d, Nsubgranules = %5$2d\n
27 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Total = %4$3d\n\t%5$13s Blocked_mode = %6$8s\n\t%7$17s Start_waiting_at = %8$-30s\n\t%9$17s Wait_for_secs = %10$.2f\n
28 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d, Nsubgranules = %5$2d\n\t%6$13s Blocked_mode =%7$8s\n\t%8$13s Start_waiting_at = %9$-30s\n\t%10$13s Wait_for_secs = %11$.2f\n
29 %1$4sTran_index = %2$3d, Blocked_mode =%3$8s \n\t%4$13s Start_waiting_at = %5$-30s \n\t%6$13s Wait_for_secs = %7$.2f\n
30 %1$4sTran_index = %2$3d, Non_2_phase_lock =%3$8s\n
31 VPID (volid|pageid) = %1$3d|%2$6d.\n
32 *** Vărsarea tabelei de lock-uri ***\n Promovare de lock la = %1$d, Interval de aşteptare până la căutarea unui deadlock = %2$5.2f\n
33 Tranzacţie (index %1$2d, %2$s, %3$s@%4$s|%5$d)\n
34 Izolare %1$s\n
35 Stare %1$s\n
36 Durată de expirare (Timeout_period) %1$s\n
37 S-au abandonat unilateral tranzacţiile cu indicii:\n
38 %1$d ,
39 Au expirat (Timed_out) tranzacţiile cu indicii:\n
40 %1$d ,
41 Se rezolvă deadlock-ul prin selecţia unei tranzacţii şi a unei funcţii \n care să întrerupă deadlock-ul: %1$d %2$s
42 , %1$d %2$s
43 Tip de obiect: Cheie de index a clasei (%1$2d|%2$6d|%3$4d) = %4$s.\n
44 Nume de index: %1$s\n

$set 15 MSGCAT_SET_IO
1 \n*************************************************************************\n
2 O copie de siguranţă = %1$s este necesară. \n Trebuie să fie montată/încărcată pentru a continua.\n Tastaţi 1 pentru a continua după ce copia de siguranţă este montata/încărcată.\n Tastaţi 0 pentru ieşire.\n
3 \n\n*** INFORMAŢII DESPRE ANTETUL COPIEI DE SIGURANŢĂ ***\n\n
4 Identificator magic pentru fişierul de siguranţă %1$s,\n
5                        Versiune: %1$s\n\
                    Versiune disc: %2$g\n
6            Numele bazei de date: %1$s\n\
       Data creării bazei de date: %2$s\
                  Mărimea paginii: %3$d\n
7                 Nivel de backup: %1$d (%2$s)\n\
                        Start_lsa: %3$lld|%4$d\n\
                         Last_lsa: %5$lld|%6$d\n
8 Ora creării copiei de siguranţă: %1$s\
             Nr. volum de rezervă: %2$d\n
9  Numele volumului bazei de date: %1$s\n\
           Identificator de volum: %2$d, Mărime: %3$lld bytes (%4$d pages)\n
10           Numele bazei de date: %1$s\n\
                 Numele volumului: %2$s\n\
                    Număr unitate: %3$d\n\
                  Nivel de Backup: %4$d (%5$s)\n\
Introduceţi una din următoarele opţiuni:\n
11 Tastati\n \
  -  %1$d pentru a renunţa.\n \
  -  %2$d pentru a continua după ce copia de siguranţă este montată/încăracată. (reîncercare) \n \
  -  %3$d pentru a continua după schimbarea directorului sau dispozitivului volumului.\n
12 Introduceţi un director sau dispozitiv nou pentru volumul de rezervă.\n
13 Eroare la intrare, alegeţi o valoare între %1$d şi %2$d.\n
14 ** AVERTISMENT ** Volum incorect de rezervă montat/încărcat.\n
15 Volumul de rezervă %1$s a fost creat cu un alt nivel\n   (Nivel: %2$d, se aşteaptă %3$d).\n\n
16 Volumul de rezervă %1$s are un identificator magic sau de pagină incorect.\n\n
17 Volumul de rezervă %1$s face parte din altă bază de date\n  (Nume: %2$s, se aşteaptă %3$s).\n\n
18 Volumul de rezervă %1$s nu se află în ordinea corectă\n  (Număr unitate: %2$d, se aşteaptă %3$d).\n\n
19 Nepotrivire de oră si dată (timestamp) pentru volumul de rezervă %1$s\n  (Timestamp: %2$s, se aşteaptă %3$s).\n\n
20 Eroare în fişierul cu informaţii despre volumul de rezervă, linia %1$d.\n
21 Eticheta volumului de rezervă: Nivel: %1$d, Unitate: %2$d, Bază de Date %3$s, Data si ora creării: %4$s\n
22     Nivel anterior al copiei de rezervă: %1$d Ora: %2$s (start_lsa a fost %3$lld|%4$d)\n
23 Următorul volum de rezervă al bazei de date este necesar pentru a putea continua restaurarea:\n\n
24 Destinaţia de rezervă este plină, o nouă destinaţie este necesară pentru a continua:\n\n
25         Numele volumului de rezervă anterior: %1$s\n
26          Numele următorului volum de rezervă: %1$s\n
27  Dimensiunea paginii pentru copia de rezervă: %1$d\n
28                                   Metoda Zip: %1$d (%2$s)\n\
                                      Nivel Zip: %3$d (%4$s)\n
29                            Include log activ: %1$s\n

$set 16 MSGCAT_SET_LOG
1 \n*************************************************************************\n
2 Arhiva de log %1$s \n este necesară pentru a continua execuţia normală.   \n \
  Tastati\n \
  -  0 pentru a renunţa.\n \
  -  1 pentru a continua fără arhiva curentă. (Restaurare parţială)\n \
  -  2 pentru a continua după ce arhiva este montată/încărcată.    \n \
  -  3 pentru a continua după schimbarea numelui/locaţiei arhivei.   \n
3 Fişierul de informaţii de rezervă %1$s \n este necesar pentru a continua procesul de restaurare. \n \
  Tastaţi\n \
  -  0 pentru a renunţa.\n \
  -  1 pentru a continua după ce fişierul este montat/încărcat.\n \
  -  2 pentru a continua după schimbarea numelui/locaţiei fişierului.   \n
4 Introduceţi nume/locaţie nouă.\n
5 COMENTARIU: %1$s pentru baza de date %2$s\n
6 COMENTARIU: Arhiva de log %1$s nu mai este necesară decât dacă apare o problemă gravă cu mediul de stocare al bazei de date.\n
7 COMENTARIU: Arhivele de log de la %1$s până la %2$s nu mai sunt necesare decât dacă apare o problemă gravă cu mediul de stocare al bazei de date.\n
8 COMENTARIU: din baza de date redenumită = %1$s\n
9 ARHIVA: %1$d %2$s %3$lld %4$lld\n
10 ARHIVA:
11 ŞTERGERE: %1$d %2$s la \n \
       %3$d %4$s.\n \
       MOTIV: %5$s\n
12 ACTIVE: %1$s %2$d pagini\n
13  Finalizarea salvării (prin COMMIT) a tran_index %1$d cu tranid = %2$d,\n înainte de LSA= %3$lld|%4$d şi la %5$s
14  Finalizarea abandonării lui tran_index %1$d cu tranid = %2$d,\n înainte de LSA= %3$lld|%4$d şi la %5$s
15  O recuperare incompletă de media este în vigoare. Baza de date \n \
 va fi recuperată până la punctul LSA cu adresa %1$lld|%2$d. Momentul \n \
 ultimei acţiuni de finalizare/abandon (commit/abort) este aproximativ\n \
 la %3$s \n \
 Baza de date a pierdut toate informaţiile despre tranzacţiile finalizate \n \
 după acest moment.\n
16 Resetare a logului datorită recuperării incomplete.
17 Copia de rezervă a bazei de date a fost preluată.
18 Problemele mediului de stocare nu sunt importante pentru aplicaţie.
19 Se creează o copie de siguranţă în directorul %1$s \n\
care pare să conţină o copie de rezervă anterioară.\n\
Volumele de rezervă mai vechi ale acestei baze de date, aflate în acest \n\
director vor fi şterse, inclusiv cele de acelaşi nivel sau superioare.\n
20 Tastaţi 'y' pentru a continua sau orice altă tastă pentru anulare.\n
21 Crearea copiei de rezervă a bazei de date a fost abandonată de utilizator.\n
22 DELETE POSTPONED: Arhivele %1$d %2$s până la \n \
        %3$d %4$s \n \
        nu mai sunt necesare decât în cazul unei restaurări din copia de rezervă curentă.\n
23 O copie de rezervă mai nouă a eliberat arhivele anterior marcate cu DELETE POSTPONED.\n
24 COMENTARIU: Ahiva de log %1$s nu mai este de tip DELETE POSTPONED datorită unei copii de\n \
   siguranţă mai noi, astfel nemaifiind necesară decât în cazul producerii unei erori grave\n \
   la nivelul mediului de stocare.\n
25 COMENTARIU: Arhivele de log de la %1$s până la %2$s nu mai sunt marcate ca DELETE POSTPONED\n \
   datorită unei copii de rezervă mai noi, astfel nemaifiind necesare decât în cazul producerii\n \
   unei erori grave la nivelul mediului de stocare.\n
26 O eroare s-a produs în timpul unei restaurări a bazei de date, în timpul procesării volumului\n\
        Volum de rezervă: %1$s\n\
           Număr unitate: %2$d\n\
      Volum bază de date: %3$s\n\
   ID volum bază de date: %4$d\n\
** AVERTISMENT: Nu utilizaţi baza de date înainte de realizarea unei restaurări fără erori.\n
27 Eroare la intrare, alegeţi o valoare cuprinsă între %1$d şi %2$d.\n
28 Momentul specificat (%1$s) trebuie să îl succeadă pe cel al copiei de rezervă specificate(%2$s).\n
29 COMENTARIU: Arhiva de log %1$s, care conţine pagini de log anterioare lui %2$lld, nu mai \n \
 este necesar niciunui utilitar HA.\n
30 Numărul de arhive de log active a depăşit numărul maxim admis.
