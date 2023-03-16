
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
  -t, --plain-output           sonuçları komut dosyası için uygun bir formatta görüntülemek (-c ve -i sadece çalışır)\n\
  -q, --query-output           sonuçları sorguyla uyumlu bir biçimde göster (-c ve -i sadece çalışır)\n\
  -d, --loaddb-output          sonuçları loaddb için uygun bir formatta görüntüleyin (-c ve -i sadece çalışır)\n\
  -N, --skip-column-names      sonuçlarında sütun adları gösterilmeyecek (-c ve -i sadece çalışır)\n\
      --string-width           bu genişlikte bir dizin tipi olan her sütunu görüntüler\n\
      --no-auto-commit         Otomatik tamamlama modu yürütme devre dışı\n\
      --no-pager               pager kullanmayın\n\
      --no-single-line         tek satır odaklı yürütmeyi kapatın\n\
      --no-trigger-action      tetikleyicileri devre dışı bırak\n\
      --delimiter=ARG          sütunlar arası sınırlayıcı (-q sadece çalışır)\n\
      --enclosure=ARG          bir sonuç dizesi için kapsam (-q sadece çalışır)\n\
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
198 Biçimlendirme başarısız oldu.

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
   ;EDIT   [format/fmt]         - komut arabelleği ile [biçimlendiriciden sonra] sistem düzenleyicisini çağırın.\n\
   ;LISt                        - komut buffwe içerik görüntüle.\n\
\n\
   ;RUn                         - komut buffer içinde SQL yürüt.\n\
   ;Xrun                        - komut buffer içinde SQL yürüt,\n\
                                  ve komut buffer temizle.\n\
   ;COMmit                      - geçerli işlemleri iıle.\n\
   ;ROllback                    - geçerli işlemleri geri iıle.\n\
   ;AUtocommit [ON|OFF]         - otomatik tamamlama modunu etkinleştirme / devre dışı bırakma.\n\
   ;REStart                     - CSQL oturumunda hedef veritabanına yeniden bağlanın.\n\
   ;CHeckpoint                  - kontrol noktasını yürütün (yalnızca --sysadm ile CSQL).\n\
   ;Killtran                    - işlem durumu bilgilerini kontrol edin veya belirli bir işlemi sonlandırın. (yalnızca --sysadm ile CSQL).\n\
\n\
   ;SHELL_Cmd     [shell-cmd]   - varsayılan dış kabuğu ayarla, editör, yazdır, çağrı cihazı ve biçimlendirici\n\
   ;EDITOR_Cmd    [editor-cmd]    yeni bir tane komut ver veya geçerli olanı görüntüle\n\
   ;PRINT_Cmd     [print-cmd]     bir, sırasıyla.\n\
   ;PAger_cmd     [pager-cmd]\n\
   ;FOrmatter_cmd [formatter-cmd]\n\
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
   ;TRAce [ON/OFF] [text/json]  - sql otomatik iz etkinleştirmek/devre dışı.\n\
   ;SIngleline [ON|OFF]         - tek hat modunu etkinleştir/devre dışı bırak.\n\
   ;CONnect username [dbname | dbname@hostname]\n\
                                - mevcut veya diğer veritabanlarına bir kullanıcı adı olarak bağlanın.\n\ 
   ;.Hist [ON/OFF]              - CSQL'de istatistik bilgisi toplamayı başlat/durdur (yalnızca DBA mevcuttur).\n\
   ;.Clear_hist                 - tampondaki CSQL istatistik bilgilerini temizler.\n\
   ;.Dump_hist                  - CSQL istatistik bilgilerini CSQL'de görüntüler.\n\
   ;.X_hist                     - CSQL'de CSQL istatistik bilgilerini, istatistik verileri başlatıldı.\n\
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
237 <Comment>
