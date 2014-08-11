
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
Etkileşimli SQL programı, sürüm %1$s\n\
kullanım: %2$s [SEÇENEK] veritabanı-adı[@host]\n\
\n\
geçerli ayarlar:\n\
  -S, --SA-mode                bağımsız modu yürütme\n\
  -C, --CS-mode                istemci-sunucu modunda yürütme\n\
  -u, --user=ARG               alternatif kullanıcı adı\n\
  -p, --password=ARG            şifre dizini, "" kimseye vermemek için \n\
  -e, --error-continue         ifade hatası üzerinde iken çıkış yapmayın\n\
  -i, --input-file=ARG         dosya-adı-girin\n\
  -o, --output-file=ARG        dosya-adı-çıkın\n\
  -s, --single-line            tek satır odaklı yürütülmesi\n\
  -c, --command=ARG            CSQL-komutları\n\
  -l, --line-output            bir çizgi üzerinde her değeri görüntüle\n\
  -r, --read-only              sadece-oku modu\n\
      --string-width           bu genişlikte bir dizin tipi olan her sütunu görüntüler\n\
      --no-auto-commit         Otomatik tamamlama modu yürütme devre dışı\n\
      --no-pager               pager kullanmayın\n\
      --no-single-line         tek satır odaklı yürütmeyi kapatın\n\
      --no-trigger-action      tetikleyicileri devre dışı bırak\n\
\n\
Ek bilgi için, adresine bakın http://www.cubrid.org\n
41 %1$s: doğru olmayan %2$s_MODE ortam değeri. \n%3$s veya %4$s olmalıdır.\n
42 %1$s: çok fazla argüman verildi.en fazla %2$d.\n
43 %1$s: Kök dizin ortam değişkeni ($CUBRID) ayarlanmalıdır.\n
44 %1$s: "%2$s" çalıştırılma denemesi başarısız oldu.\n
45 %1$s: aynı anda -sa ve -cs seçeneklerini kullanamazsınız.\n
46 %1$d komut (lar) başarıyla işlendi.
47 kommand içinde %1$d satırından,
48 %1$d satırında , sütun %2$d,
49 Tür `;help' yardım mesajları için.
50 HATA: 
51 CUBRID SQL Yorumlayıcı
52 Güncellemeler bulunmaktadır. İşlemek istiyormusunuz ? (`E' or `H') 
53 Giriş `E' İşlemek, `H' İptal etmk. 
54 İşlenen.
55 Geri alındı.
56 Komut buffera kaydedildi.
57 Komut buffer içinden dosya okundu.
58 Komut buffer yazıyıcıya gönderildi.
59 Geçerli dizin değiştirildi 
61 Şifre girin : 
62 <Sonuçlar %1$s komut satırları içinde %2$d>
63 Sonuç bulunmamaktadır.
64 Kontrol noktası yayımlanmıştır
65 Veritabanı yeniden başlatıldı.
66 İşlem indeks          Kullanıcı Adı      Host Adı      Süreç id      Program Adı\n
67 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
68 Özelleıtirilmiı İşlem iptal edildi.
69 Özelleıtirilmiı İşlem iptal etme başarısız.
70 %1$d Satırlar %2$s.
71 %1$d Satır %2$s.
$  Bu 3 boş bırakılmalı
75 Otomatik APİ test edici
76 Geçersiz Otomatik APİ test edici
79 csql>
80 csql
81 sysadm>
82 %1$d transactions %2$s.
83 %1$d transaction %2$s.

145 <Help: Bir Sınıf Şeması>
146 none
147 <Help: Tüm Tetikleyiciler>
148 <Help: Tanımlı Tetikleyici Yok>
150 <Help: Tetikleyici Tanımı>
151 <Help: Komut Sözdizini>
152 <Help: Özet Komut Oturumu>
178 Bir dosya adı gereklidir.
179 Çağrı cihazı yürütülemiyor.
180 Yeterli bellek tahsisi yok.
184 Girilen satır çok uzun.
185 Çok fazla satır kaydedilmiıtir.
188 Çok fazla dosya adı kaydedilmiıtir.
190 Gibi oturum komutu bulunmamakta.Tür `;help' yardım mesajları için.
191 Birden fazla oturum komutu eşleşti. Lütfen daha fazla özelleştirin.
193 Birden fazla komut eşleşti. Lütfen daha fazla özelleştirin.
194 Komut satırı argümanlarının geçersiz kombinasyonu (seçenekler).
196 Tanımlanamayan bir hata oluıtu.
197 Çıkış yönlendirildişinde ';edit' izin verilmez.

203 <%1$s> 
204 <Super Classes> 
205 <Sub Classes> 
206 <Attributes> 
207 <Class Attributes> 
208 <Methods> 
209 <Class Methods> 
210 <Resolutions> 
211 <Method files> 
212 <Query_specifications> 
213 <Triggers> 
214 <Name> 
215 <Event> 
216 <Condition Time> 
217 <Condition> 
218 <Action Time> 
219 <Action> 
220 <Status> 
221 <Priority> 
222 <Name> 
223 <Description> 
224 <Syntax> 
225 <Example> 

$ csql help messages
231 \
\n\
   Tüm oturum komutları `;' ve sadece boşluklar/sekmeler ile başlayan olmalıdır\n\
   önek önce gelebilir. Aktifleıtirilen karakterleri az temsil eder.\n\
   Belirtilen komutu çalıştırmak için girmeniz gereken kısaltma.\n\
\n\
   ;REAd   [<file-name>]        - komut buffer içinden bir dosyayı oku.\n\
   ;Write  [<file-name>]        - bir dosya içinden komut buffer (üzerine)yaz.\n\
   ;APpend [<file-name>]        - bir dosya içinden komut buffer açılmış.\n\
   ;PRINT                       - komut buffer yazdır.\n\
   ;SHELL                       - dış kabuk çağırma.\n\
   ;CD                          - Geçerli çalışma dizini değiştirin.\n\
   ;EXit                        - programdan çık.\n\
\n\
   ;CLear                       - komut buffer temizle.\n\
   ;EDIT                        - komut buffer ile sistem editörü çağırmak.\n\
   ;LISt                        - komut buffwe içerik görüntüle.\n\
\n\
   ;RUn                         - komut buffer içinde SQL yürüt.\n\
   ;Xrun                        - komut buffer içinde SQL yürüt,\n\
                                  ve komut buffer temizle.\n\
   ;COMmit                      - geçerli işlemleri iıle.\n\
   ;ROllback                    - geçerli işlemleri geri iıle.\n\
   ;AUtocommit [ON|OFF]         - otomatik tamamlama modunu etkinleştirme / devre dışı bırakma.\n\
   ;REStart                     - veritabanını yeniden başlat.\n\
\n\
   ;SHELL_Cmd    [shell-cmd]    - varsayılan dış kabuğu ayarla, editör, yazdır ve çağrı cihazı\n\
   ;EDITOR_Cmd   [editor-cmd]     yeni bir tane komut ver veya geçerli olanı görüntüle\n\
   ;PRINT_Cmd    [print-cmd]      bir, sırasıyla.\n\
   ;PAger_cmd    [pager-cmd]\n\
\n\
   ;DATE                        - yerel saat görüntüle, tarih.\n\
   ;DATAbase                    - veritabanı erişilen adını görüntüler.\n\
   ;SChema class-name           - bir sınıfın şema bilgilerini görüntüler.\n\
   ;TRIgger [`*'|trigger-name]  - tetik tanımı görüntüler.\n\
   ;Get system_parameter        - bir sistem parametre değeri edinin.\n\
   ;SEt system_parameter=value  - bir sistem parametre değeri ayarla.\n\
   ;STring-width [width]        - bir dizin tipi görüntülenmiş olan her sütunun genişliğini ayarlayın.\n\
   ;COLumn-width [name]=[width] - belirli sütunu görüntülenmiş olan genişlik ayarla.\n\
   ;PLan [simple/detail/off]    - sorgu yürütme planı göster.\n\
   ;Info <command>              - iç bilgileri görüntüler.\n\
   ;TIme [ON/OFF]               - Sorgu görüntülemek için etkin / devre dışı\n\
                                  yürütme zamanı.\n\
   ;LINe-output [ON/OFF]        - bir satırda her bir değeri görüntülemek için etkinleştir/devre dışı bırak\n\
   ;HISTORYList                 - çalıştırılan sorguların listesini görüntüleyebilir.\n\
   ;HISTORYRead <history_num>   - komut buffer içine geçmiş numarası üzerinde giriş okuyun.\n\
   ;TRAce [ON/OFF] [text/json]  - enable/disable sql auto trace.\n\
   ;HElp                        - yardım mesajını görüntüler.\n\n

232 <Constraints> 
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - şema bilgileri\n\
\ttrigger [trigger-name] - tetikleyici bilgileri\n\
\tdeferred               - ertelenen tetikleyici bilgileri\n\
\tworkspace              - çalışma alanı bilgileri\n\
\tlock                   - kilit bilgileri (istemci-sunucu modunda)\n\
\tstats class-name       - sınıf istatistik bilgileri\n\
\ttrantable              - işlem bilgileri\n\
\tlogstat                - log bilgileri\n\
\tcsstat                 - kritik bölüm bilgileri\n\
\tplan                   - önbellek plan bilgileri\n\
\tqcache                 - sorgu önbellek bilgileri\n
235 <Partitions>
236 Bir sınıf adı gereklidir.
