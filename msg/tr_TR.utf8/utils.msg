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
$ Aşağıdaki dizi sayıları tahsis edilmiştir:
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
$       50      prefetchlogdb

$set 1 MSGCAT_UTIL_SET_GENERIC
1 Geçersiz uzunluk veya karakter '%1$c' veritabanı adı '%2$s' \n
2 Çıktı dosyası '%1$s' açılamadı \n
3 Eksik veya geçersiz yardımcı programı adı.\n
4 Geçersiz yardımcı programı adı  '%1$s'\n
5 Aşağıdaki liste tanınan yardımcı programı adlarını içerir:\n
6 Geçersiz karakter '%1$c'ses adına'%2$s'\n
9 \
%1$s (CUBRID yardımcı programları)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
cubrid yönetici yardımcı programı, sürüm %1$s\n\
kullanım: %2$s <yardımcı programı-adı> [args]\n\
Tür '%3$s <yardımcı programı-adı>' belirli bir yardımcı programı hakkında yardım alın.\n\
\n\
Mevcut yönetici yardımcı programı-adı:\n\
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
    prefetchlogdb\n\
\n\
%4$s DBMS için bir araçtır.\n\
Daha fazla bilgi için bkz:http://www.cubrid.org\n
12 '%1$s' yardımcı programı-adı geçersiz.\nTür 'cubrid --help' kullanıma bak.\n
13 '%1$s' komutu geçersiz.\nTür 'cubrid --help' kullanıma bak.\n
14 Yapılandırma dosyaları bir veya daha fazla zorunlu özellikleri yüklenemedi.\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' çalışmıyor.\n
18 ++ %1$s çalışmıyor.\n
19 ++ %1$s '%2$s' çalışıyor.\n
20 ++ %1$s çalışıyor.\n
21 ++ %1$s %2$s: %3$s\n
22 Bir veya daha fazla bağımsız değişkeni eksik.\nTür 'cubrid --help' kullanıma bak.\n
23 cubrid yardımcı programı, sürüm %1$s\n\
kullanım: %2$s <yardımcı programı-adı> [args]\n\
Tür '%3$s <yardımcı programı-adı>' belirli bir yardımcı programı hakkında yardım alın.\n\
\n\
Mevcut hizmetin programları:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
\n\
Mevcut yönetici programları:\n\
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
    applyinfo\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
\n\
%4$s DBMS için bir araçtır.\n\
Daha fazla bilgi için bkz: http://www.cubrid.org\n
24 cubrid yardımcı programı, sürüm %1$s\n\
kullanım: %2$s service <komut>\n\
\n\
Mevcut komut:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubrid yardımcı programı, sürüm %1$s\n\
kullanım: %2$s server <komut> [args]\n\
\n\
Mevcut komut:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status\n\
    acl         <status|reload> database-name\n
37 Sunucu HA için yapılandırıldı.\n
26 cubrid yardımcı programı, sürüm %1$s\n\
kullanım: %2$s broker <komut> [args]\n\
\n\
Mevcut komut:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [<expr>]\n\
    on          broker-name\n\
    off         broker-name\n\
    reset       broker-name\n\
    acl         <status|reload> [broker-name]\n\
    getid       -b <broker-name> [-f] shard-key\n\
    test        broker-name [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 cubrid yardımcı programı, sürüm %1$s\n\
kullanım: %2$s manager <komut>\n\
\n\
Mevcut komut:\n\
    start\n\
    stop\n\
    status\n
28 cubrid yardımcı programı, sürüm %1$s\n\
kullanım: %2$s heartbeat <komut> [args]\n\
\n\
Mevcut komut:\n\
    start          [database-name]\n\
    stop           [-h <hostname>] [-i] [database-name]\n\
    copylogdb      <start|stop> database-name node-name\n\
    applylogdb     <start|stop> database-name node-name\n\
    prefetchlogdb  <start|stop> database-name node-name\n\
    replication    <start|stop> node-name\n\
    status         [-v]\n\
    reload\n
31 '%1$s' argümanı gerekli değildir.\n
32 Bir veritabanı-adı eksik.\n
33 %1$s: Böyle seçeneği önerilmiyor.\n
34 Geçersiz Parametre. %1$s %2$s\n
35 Bellek ayrılamadı.\n
36 Sunucu HA için yapılandırıldı.\n
38 HA için ayarlanan sunucu ha_db_list.\n görülmedi. \n Listesinde
39 HA için ayarlanan sunucu in_ ha_node_list.\n görülmedi. \n Listesinde
40 Yanlış kumanda.\n
41 CUBRID sunucu yöneticisi yüklü değildir.\n
42 Değişken geçersiz.\n
43 (%1$s) dosya açma hatası.\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 UYARI: -t seçeneği stand-alone modu yürütülmesine göz ardı edilir.\n
31 HATA: Hedef-yolu yok veya bir dizin değil.\n
60 \
backupdb: Bir veritabanı yedeklemesi.\n\
kullanım: %1$s backupdb [SEÇENEK] veritabanı-adı\n\
\n\
geçerli opsiyonlar:\n\
  -D, --destination-path=PATH  dizin PATH yedekleme birimleri saklamak; varsayılan: veritabanının günlük dizini\n\
  -r, --remove-archive         dikkatli kullanın ve başvuru kılavuzu; gereksiz log -arşivleri sil\n\
  -l, --level=LEVEL            yedekleme düzeyi; varsayılan: tam düzey\n\
                               LEVEL izin verilir:\n\
                                   0 - tam\n\
                                   1 - artımlı 1\n\
                                   2 - artımlı 2\n\
  -o, --output-file=FILE       DOSYA detaylı yedekleme mesaj yayınlar\n\
  -S, --SA-mode                stand-alone modu yürütme\n\
  -C, --CS-mode                istemci-sunucu modunda yürütme\n\
      --no-check               veritabanının tutarlılığını kontrol etmez; varsayılan: kontrol edin\n\
  -t, --thread-count=COUNT     Konu sayısı; Varsayılan olarak: 0 (otomatik)\n\
  -z, --compress               yedekleme birimleri sıkıştırmak; varsayılan: sıkıştırmayı açma\n\
  -e, --except-active-log      şunlardır: varsayılan; aktif bir log yoğunluğu içermez\n\
      --sleep-msecs=N          sleep N millisecond per 1M read; default: 0\n
71 Uyumsuz bağımsız değişkenleri, \"%1$s\", ve \"%2$s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 HATA: Hedef adı kaynak adı ile aynıdır.\n
31 HATA: volext-pathadı mevcut değil veya yazılabilir değildir.\n
32 HATA: vols-tofrom-paths_file yok veya okunabilir değil değil.\n
60 \
copydb: Bir veritabanı kpyala.\n\
kullanım: %1$s copydb [SEÇENEK] src-database-name dest-database-name\n\
\n\
geçerli seçenekler:\n\
      --server-name=HOST          Sunucunun HOST adı; Varsayılan olarak: geçerli ana bilgisayar adı\n\
  -F, --file-path=PATH            veritabanı yoğunlukları için dizin PATH; varsayılan: geçerli dizin\n\
  -L, --log-path=PATH             log yoğunlukları için dizin PATH; varsayılan: dosya-path aynı\n\
  -B, --lob-base-path=PATH        Dış LOB veri tabanı dizin PATH; varsayılan: <file-path>/lob\n\
  -E, --extended-volume-path=PATH genişletilmiş bilgi yoğunlukları için dizin PATH; varsayılan: veritabanı yoğunlukları aynı path veya verilen path kontrol-dosyasında\n\
  -i, --control-file=FILE         kontrol dosyası yazılmış birkaç yerle yoğunlukları kopyalama; varsayılan: hiçbir\n\
  -r, --replace                   yerine geçmez: varsayılan; mevcut bir veritabanını değiştirme\n\
  -d, --delete-source             bir kaynak veritabanı silme ;varsayılan :silmeyin \n\
      --copy-lob-path             kaynak veritabanından LOB veri için harici depolama PATH kopyalayın.seçeneği-B ile kullanılamaz;varsayılan : kopya değil \n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 Group '%1$s' bulunmamaktadır\n
41 User '%1$s' bulunmamaktadır\n
42 Bilinmeyen komut '%1$s'\n
43 Çıktı dosyası '%1$s' açılamadı\n
44 Varolan veritabanı kaldırılamadı\n
45 Varsayılan yerel %2$s kullanarak %1$s boyutu ile veritabanı oluşturmak. Gerekli disk alanın toplam miktarı ise %3$s.\n
46 Veritabanı oluşturulamadı.\n
47 '%1$s' Kullanıcı dosya ile ilgili sorun\n
48 Verilen '%1$s', %2$s kapsam dışı. %3$s ve %4$s arasında bir boyut seçin.\n
49 %1$s sayısal değeri '%2$s' geçersiz. \n
60 \
createdb: Bir veritabanı oluştur.\n\
kullanım: %1$s createdb [SEÇENEK] database-name database-locale\n\
\n\
Veritabanı-yerel biçimi ise <language>.<charset>; örnekler : en_US.iso88591, ko_KR.utf8\n\
geçerli seçenekler:\n\
      --db-volume-size=SIZE           ilk bilgi yoğunluğunun büyüklüğün\
                                      varsayılan: cubrid.conf içinde db_volume_size\n\
      --db-page-size=SIZE             byte olarak veritabanı sayfası BOYUTU; varsayılan: 16KB\n\
      --log-volume-size=SIZE          log arşiv boyutu\n\
                                      varsayılan: cubrid.conf içinde log_volume_size\n\
      --log-page-size=SIZE            veritabanı sayfa boyutu: varsayılan; byte olarak log sayfa BOYUTU\n\
      --comment=COMMENT               Bir veritabanı için YORUM; varsayılan: hiçbir\n\
  -F, --file-path=PATH                veritabanı yoğunlukları için dizin PATH; varsayılan: geçerli dizin\n\
  -L, --log-path=PATH                 log yoğunlukları için dizin PATH; varsayılan: dosya-path aynı\n\
  -B, --lob-base-path=PATH            Dış LOB veri tabanı dizin PATH; varsayılan: <file-path>/lob\n\
      --server-name=HOST              Sunucunun HOST adı; varsayılan: geçerli ana bilgisayar adı\n\
  -r, --replace                       yerine geçmez: varsayılan; mevcut bir veritabanını değiştirme\n\
      --more-volume-file=FILE         Ek birimler oluşturulması için özellikleri içeren FILE; varsayılan: hiçbir\n\
      --user-definition-file=FILE     Kullanıcı tanımlarını içeren DOSYA; varsayılan: hiçbir\n\
      --csql-initialization-file=FILE Veritabanının başlatılması için sqls içeren DOSYA; varsayılan: hiçbir\n\
  -o, --output-file=FILE              DOSYA çıktı mesajları yönlendirmek; varsayılan: hiçbir\n\
  -v, --verbose                       Ayrıntılı durum mesajları sağlamak; varsayılan: devre dışı\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: Bir veritabanı sil.\n\
kullanım: %1$s deletedb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE       DOSYA çıktı mesajları yönlendirme; varsayılan: hiçbir\n\
  -d, --delete-backup          yedekleme birimleri silin; varsayılan: silmeyin\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 HATA: volext-pathadı mevcut değil veya yazılabilir değildir.\n
32 HATA: vols-tofrom-paths_file yok veya okunabilir değil değil.\n
60 \
renamedb: Bir veritabanı yeniden adlandırın.\n\
kullanım: %1$s renamedb [SEÇENEK] src-database-name dest-database-name\n\
\n\
geçerli seçenekler:\n\
  -E, --extended-volume-path=PATH genişletilmiş bilgi yoğunlukları için dizin PATH; varsayılan: veritabanı yoğunlukları aynı path veya verilen path kontrol- dosyasında\n\
  -i, --control-file=FILE         kontrol dosyası yazılmış birkaç yerle adlandırma yoğunlukları; varsayılan: hiçbir\n\
  -d, --delete-backup             yedekleme birimleri silin; varsayılan: silmeyin\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 A %1$s süreç zaten çalışıyor.\n
12 MASTER SERVER başlangıç​​. \n
13 MASTER SERVER çıkarken.\n
14 Hata yerel ana bilgisayar adı bulmakta\n
15 Parametre dosyası bulunamadı\n
16 Ana işlemi başlatılamadı\n
17 Sunucu %1$s %2$d dakika içinde aşağı gidiyor.
18 Sunucu %1$s kapatma bildirilir. \n Bu işlem birkaç dakika sürebilir. Lütfen bekleyiniz.
19 Sunucu %1$s bulunamadı.
20 Master %1$d dakika aşağı gidiyor\n
21 Heartbeat failover was finished. Please wait until all shutdown processes of cub_servers are completed.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Çıktı dosyası '%1$s' açılamadı\n
60 \
optimizedb: Bir veritabanı optimize edin.\n\
kullanım: %1$s optimizedb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -n, --class-name=NAME        NAME sınıf için güncelleştirme istatistikleri; varsayılan: tüm sınıflar\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Geçersiz tarih biçimi, geçerli biçimi dd-mm-yyyy:hh:mm:ss veya 'backuptime'\n
20 Restoredb iptal edildi veya bir hata oluştu.\n
60 \
restoredb: Bir veritabanı geri yükleyin.\n\
kullanım: %1$s restoredb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -d, --up-to-date=DATE             Verilen TARİH yukarı durumuna veritabanı geri yükleme. TARİHİ aşağıdaki gibi verilir: gg-aa-yyyy:ss:dd:ss veya 'yedekleme zamanı' anahtar kelime\n\
      --list                        geri yüklemeden yedek birimlerin listesini görüntüler\n\
  -B, --backup-file-path=PATH       PATH geri yüklenecek yedek yoğunluk bir dizinidir\n\
  -l, --level=LEVEL                 Yedekleme SEVİYE geri yüklenecek; yedekleme kullanımını görmek; varsayılan: 0 (tam yedekleme)\n\
  -p, --partial-recovery            herhangi bir log arşiv bulunmayışı halinde kısmi kurtarma işlemi\n\
  -o, --output-file=FILE            DOSYA çıktı mesajları yönlendirme; varsayılan: hiçbir\n\
  -u, --use-database-location-path  veritabanı konumu dosyasında belirtilen yoluna veritabanı ve günlük yoğunlukları geri yükleme.\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 Tha veritabanı adı eksik.\n
20 '%1$s' dosyası %2$d satır yoktur.\n
21 ara verme ... \n
22 ara verilen yük - mevcut işlem tamamlanmış\n
23 ara verilen yük - durduruldu mevcut işlem\n
24 '%1$s' dosyası açılamıyor\n
25 \n*** syntax kontrolü ***\n\n
26 %1$d hataları algılandı.\n
27 \n*** Ekleme nesneleri ***\n\n
28 Toplam %1$8d nesneleri eklenir.\n
29 Toplam %1$8d varsayılan içeriği ile eklenen nesneleri başvurulan.\n
30 \n*** İşlem İşlemek ***\n
31 \n*** Veritabanı Kapama ***\n
32 Satır %1$d:
33 Hata '%1$s' özniteliği '%3$s' için '%4$s' değer olarak '%2$s' ayrıştırma\n
34 %1$s özniteliği %2$s için eksik alanı\n
35 %1$s özniteliği %2$s için, tip %3$s set etki alanında izin verilmez\n
36 %1$s özniteliği %2$s için, tip %3$s beklenen, bir dizi sabiti var\n
37 %1$s özniteliği %2$s için, tip %3$s beklenen, %4$s var\n
38 %2$s özniteliği %3$s %1$s başvurulan adında bir sınıf\n
39 Hiçbir sınıf dizilerini görmezden gelerek, %1$s adlı\n
40 Tanımlayıcısı %1$d ile hiçbir sınıf %2$s özniteliği %3$s tarafından başvurulan\n
41 Class %1$s yükleyici girdi dosyasında izin verilmiyor\n
42 Hata ile Durduruldu.\n
43 UYARI: varolan örneği güncellendi!\n
44 UYARI: Sınıf %2$s Derece %1$d yeniden tanımlıyor\n
45 Sınıf %2$s Derece %1$d zaten tanımlanmış\n
46 Sınıf %2$s Derece %1$d zaten ayrılmıştır\n
47 Özniteliği '%1$s 'EŞSİZ aykırı, NULL'a dönüştürülür\n
48   %1$d örnekleri\n
49 Sınıf %1$s\n
50 Şifre:
51 \n*** Sınıf istatistikleri güncelleniyor ***\n
52 %2$d hat yakınlarındaki %1$s (metin '%3$s' oldu)\n
53 Sözcüksel hata
54 satır %1$d: \"%2$s\" içinde syntax hata
55 eksik
56 %1$s de
57 Uyumsuz bağımsız değişkenleri, \"%1$s\", ve \"%2$s\"\n
58 %1$d taahhüt örnekleri\n
59 \nLoaddb olmayan-optimize edilmiş sürümü çağırılamaz.\n
60 Hata ayrıştırma \"%1$s\", gibi %2$s tür.\n
112   %1$d örnekleri\r
113 Son kararlı hattı: %1$d
114 hata-kontrol-dosyası
115 yükleme sırasında bir hata(lar) kontrol etmek için giriş dosyası
116 Toplam %1$8d nesne (ler) eklenen, %2$d nesne (ler) başarısız oldu.\n
117 Başarısız yük.\n
118 Sınıf adının uzunluğu en fazla %1$d bayttır.\n
120 \
loaddb: Veritabanına veri alma nesneler ve şemalar.\n\
kullanım: %1$s loaddb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -u, --user=ID                 veritabanı erişimi için kullanıcı kimliği; varsayılan: "public"\n\
  -p, --password=PASS           kullanıcı şifresi; varsayılan: hiçbir\n\
      --data-file-check-only    yük olmadan veri dosyası için sözdizimi kontrol edin; varsayılan: yükleme kontrol ettikten sonra\n\
  -l, --load-only               sözdizimi denetimi olmadan veri dosyası yükleyebilir; varsayılan: yükleme kontrol ettikten sonra\n\
      --estimated-size=NUMBER   örneklerini tahmin NUMBER; varsayılan: 5000\n\
  -v, --verbose                 Ayrıntılı durum mesajları sağlamak; varsayılan: devre dışı\n\
  -c, --periodic-commit=COUNT   Periyodik tamamlama için ekleme COUNT; varsayılan: yükleme sırasında taahhüt yok\n\
      --no-oid                  OID kullanmayın\n\
      --no-statistics           yüklerken istatistikleri güncelleştirmeyin\n\
  -s, --schema-file=FILE[:LINE] FILE ile LINE şeması oluşturun\n\
  -i, --index-file=FILE[:LINE]  FILE ile LINE dizin oluşturun\n\
  -d, --data-file=FILE          FILE ile yük verileri\n\
  -t, --table=TABLE             Veri belgeleri içi kaybolan düzeyindeki şeflerin forum adıdır\n\
      --error-control-file=FILE Yükleme sırasında bir hata (lar) kontrol etmek için FILE\n\
      --ignore-class-file=FILE  yük atlamak sınıf adları girdi dosyası\n
121 \
load_noopt: Bu yardımcı önerilmiyor. Kullanırken dikkatli olun.\n\
kullanım: load_noopt [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -u, --user=ID                 veritabanı erişimi için kullanıcı kimliği; varsayılan: "public"\n\
  -p, --password=PASS           kullanıcı şifresi; varsayılan: hiçbir\n\
      --data-file-check-only    yük olmadan veri dosyası için sözdizimi kontrol edin; varsayılan: yükleme kontrol ettikten sonra\n\
  -l, --load-only               sözdizimi denetimi olmadan veri dosyası yükleyebilir; varsayılan: yükleme kontrol ettikten sonra\n\
      --estimated-size=NUMBER   örneklerini tahmin NUMBER; varsayılan: 5000\n\
  -v, --verbose                 Ayrıntılı durum mesajları sağlamak; varsayılan: devre dışı\n\
  -c, --periodic-commit=COUNT   Periyodik tamamlama için ekleme COUNT; varsayılan: yükleme sırasında taahhüt yok\n\
      --no-oid                  OID kullanmayın\n\
  -s, --schema-file=FILE[:LINE] FILE ile LINE şeması oluşturun\n\
  -i, --index-file=FILE[:LINE]  FILE ile LINE dizin oluşturun\n\
  -d, --data-file=FILE          FILE ile yük verileri\n\
      --error-control-file=FILE Yükleme sırasında bir hata(lar) kontrol etmek için FILE\n\


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 önbelleğe alınmış sayfaları değeri geçersiz.\n
42 önbelleğe alınmış sayfa boyutu değeri geçersiz.\n
43 %1$d nesneler atılıyor.\n
44 Class %1$s
45 , %1$d örnekleri.\n
46 %2$d nesneleri yalnızca %1$d tanesinde başarılı atılıyor.\n
47 çıkış dizin çok uzun.\n
48 LSA : %1$d %2$d \n
49 Şifre:
60 \
unloaddb: Veritabanındaki nesneleri ve şemalar verin.\n\
kullanım: %1$s unloaddb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -u, --user=ID               veritabanı erişimi için kullanıcı kimliği; varsayılan: "dba"\n\
  -p, --password=PASS         kullanıcı şifresi; varsayılan: hiçbir\n\
  -i, --input-class-file=FILE tablo adlarının giriş FILE varsayılan: tüm sınıflar dökümü\n\
      --include-reference     başvurulan tablolar arasında; ile-i seçeneği; varsayılan: Engelli\n\
      --input-class-only      sadece belirtilen sınıf arasında; ile-i seçeneği; varsayılan: devre dışı\n\
      --lo-count=COUNT        Bir dizin başına lo dosya COUNT; varsayılan: 0\n\
      --estimated-size=NUMBER örnekleri tahmini NUMBER; varsayılan: otomatik bilgisayarlı\n\
      --cached-pages=NUMBER   Önbelleğe alınmış sayfaları NUMBER; varsayılan: 100\n\
  -O, --output-path=PATH      Çıktı dizininin PATH; varsayılan: çalışma dizini\n\
  -s, --schema-only           şema dökümü sadece; varsayılan: şema ve objeler\n\
  -d, --data-only             dökümü nesneleri yalnızca; varsayılan: şema ve objeler\n\
      --output-prefix=PREFIEX Çıktı dosyaları için PREFIEX; varsayılan: veritabanı adı\n\
      --hash-file=FILE        Karma için FILE varsayılan: tmpnam tarafından oluşturmak ()\n\
  -v, --verbose               Ayrıntılı durum mesajları sağlamak; varsayılan: devre dışı\n\
      --use-delimiter         kullanımı '"' bir tanımlayıcı nereden başlayıp nerede bittiği; varsayılan: kullanmayın\n\
  -S, --SA-mode               stand-alone modu yürütme\n\
  -C, --CS-mode               istemci-sunucu modunda yürütme\n\
      --datafile-per-class    her sınıf için bir nesne dosyası oluşturmak; varsayılan: devre dışı\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nPass 1\n\n
12 %1$d işlenmiş nesneler.\n
13 \nPass 2\n\n
14 Class %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d örnekleri.\n
17 nesne güncelleniyor.\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 Nesne dönüşümü bulunamadı\n
20 Eksik sınıf yığın dosyası\n
21 Nesneyi güncelleştirme bulunamadı\n
22 Başarıyla işlenmiş %2$d nesnelerin sadece %1$d tanesi.\n
23 COMPACTDB Zaten başladı.\n
24 Sayfa sayısı %1$d ve %2$d arasında olmalıdır.\n
25 Örneğin kilit zaman aşımı %1$d ile %2$d arasında olmalıdır.
26 Toplam nesneler: %1$d\n
27 Başarısız nesneler: %1$d\n
28 Değiştirilmiş nesneler: %1$d\n
29 Büyük nesneler: %1$d\n
30 Alt sınıf temsilleri katalog silinir.\n
31 Alt sınıf temsilleri katalogda korunmuş.\n
32 Yalıtım düzeyi ayarlanamıyor.\n
33 Veritabanını düzenleme bulunamadı.\n
34 Sınıf kilit zaman aşımı %1$d ile %2$d arasında olmalıdır.\n
35 Kilitli sınıf ... Eksik işlem ...\n
36 İşlenmemiş sınıf ...\n
37 İşlem hatası ... Eksik işlem ...\n
38 Sürecine hiçbir şey ...\n
39 Geçersiz parametreler ...\n
40 Bilinmeyen sınıf ismi ...\n
41 Kullanılmayan sınıf OSB ile sayfaları geri edilmiştir.\n
42 OSB ile sayfaları geri istemek atlanır.\n
43 Sınıf işlenirken hata, sınıf atlandı.\n
44 \nPass 3\n\n
45 Öbek kompakt başarısız oldu ...\n
46 Öbek kompakt başarıldı ...\n
60 \
compactdb: Bir veritabanı Serbest kullanılmayan alanı.\n\
kullanım: %1$s compactdb [SEÇENEK] database-name [class_name1 class_name2 ...]\n\
\n\
geçerli seçenekler:\n\
  -v, --verbose                     Ayrıntılı durum mesajları sağlamak; varsayılan: devre dışı\n\
  -i, --input-class-file=FILE       tablo adlarının girişi FILE\n\
                                    class_name liste boş olmalıdır\n\
                                    varsayılan: kompakt tüm sınıflar\n\
                                    CS-mode sadece etkinleştirilmiş,\n\
  -p, --pages-commited-once=NUMBER  maksimum sayfa sayısı bir kez işlemek için\n\
                                    varsayılan: 10\n\
                                    CS-mode sadece etkinleştirilmiş\n\
  -S, --SA-mode                     stand-alone modu yürütme\n\
  -C, --CS-mode                     istemci-sunucu modunda yürütme\n\
  -d, --delete-old-repr             katalogdan eski sınıf temsilleri silmek; varsayılan: devre dışı\n\
  class_name_list                   CS-mode sadece etkinleştirilmiş\n\
  -I, --Instance-lock-timeout       Örneğin kilidi için zaman aşımı\n\
  -c, --class-lock-timeout          sınıf kilidi için zaman aşımı\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 Host üzerinde çalışan CUBRID Master %1$s \n başlandı %2$s
22 servisli %1$d  isteklerde bulunur. %2$d  sunucu (lar) çalışıyor.\n
23 %1$s dakika AŞAĞI KAPATIYOR\n
24 %1$s
25 Kapatmak için sunucunun adı:
26 Kaç dakika kapatılmasına dek? :
27 \
cub_commdb: Bu yardımcı programı önerilmiyor. Yerine 'cubrid' komutunu kullanın.\n\
kullanım: cub_commdb [SEÇENEK]\n\
\n\
geçerli seçenekler:\n\
  -P, --server-list                sunucu işlemleri yazdırma\n\
  -O, --all-list                   Tüm süreçlerde yazdırmak\n\
  -S, --shutdown-server=NAME       kapanma ADI veritabanı sunucusu\n\
  -A, --shutdown-all               Kapatma tüm süreçleri\n\
  -c, --server-mode=NAME           Yazdırma NAME veritabanı sunucusunun HA modunda\n\
  -N, --node-list                  CUBRID sinyal tarafından yönetilen listesi düğümleri\n\
  -L, --process-list               CUBRID sinyal tarafından yönetilen listesi yerel süreç\n\
  -p, --ping-host                  list ping hosts used for CUBRID heartbeat\n\
  -D, --dereg-process=PID          hedef işlemi çıkmak ve yönetilen süreç girdileri kaldırmak\n\
  -F, --reconfig-node-list         yakın zamanda değişenleri CUBRID sinyal yapılandırmayı uygulamak\n\
  -V, --verbose                    Ayrıntılı durum Mesajını sağlamak; varsayılan: Devre Dışı\n
28 Üzgünüz, bilinmeyen komut: %1$s\n
29 Yerel bilgisayar için ana bilgisayar adı alınamadı - durduruluyor\n
30 Parametre dosyası bulunamadı\n
31 %1$s ana sunucuya bağlantı kurulamadı\n
32 Kaç dakika kapatılmasına dek? :
33 Master %1$d dakika içinde kapanacaktır\n
34 İstemciden veri alma hatası
35 veritabanı sunucusu %1$s şu anda çalışmıyor
36 %2$s kabuk çalıştırma (%1$s), %3$s yapamam
37 %1$s süreç çatalı, %2$d, %3$s döndürülen
38 %1$s yürütemez
39 -i(--immediately) opsiyon seçeneği --deact-stop-all ile birlikte kullanılmalıdır.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: Patch acil durumlarda bir veritabanı.\n\
kullanım: %1$s emergency_patchlog [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -r, --recreate-log           log dosyalarını yeniden; dikkatli bir şekilde kullanın\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Npages için yanlış değer %1$d\n
21 Yoğunlaştırma depolama amacıyla yanlış bir değeri '%1$s'.\n
22 UYARI: --max-writesize-in-sec seçeneği bağımsız mod çalıştırmada yok sayıldı.\n
60 \
addvoldb: Add a volume-file for the database.\n\
kullanım: %1$s addvoldb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
      --db-volume-size=SIZE        ilave yoğunluk büyüklüğü; varsayılan: cubrid.conf içinde db_volume_size\n\
      --max-writesize-in-sec=SIZE  the amount of volume written per second; (ex. 512K, 1M, 10M); default: not used; minimum: 160K\n\
  -n, --volume-name=NAME           Bilgi yoğunluğunun NAME; varsayılan: örneğin, "db"_ext1 olarak oluşturmak\n\
  -F, --file-path=PATH             yoğunluk dosyası eklemek için PATH; varsayılan: çalışma dizini\n\
      --comment=COMMENT            yoğunluk dosyası eklemek için COMMENT; varsayılan: hiçbir\n\
  -p, --purpose=PURPOSE            yoğunluk dosyası eklemek için PURPOSE; izin:\n\
                                           DATA - sadece veri için\n\
                                           INDEX - yalnızca endeksler için\n\
                                           TEMP - sadece geçici için\n\
                                           GENERIC - tüm amaçlar için\n\
  -S, --SA-mode                    stand-alone modu yürütme\n\
  -C, --CS-mode                    istemci-sunucu modunda yürütme\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Bazı tutarsızlıklar veritabanı tespit edildi.\n ek bilgi için error_log_file %1$s başvurun.\n
21 %1$s: Sınıf bulamıyor. Bu dahil edilmeyecektir.\n
22 %1$s: Cannot find the index on class %2$s.\n
60 \
checkdb:Veritabanının tutarlılığını denetleyin.\n\
kullanım: %1$s checkdb [SEÇENEK] database-name [class_name1 class_name2 ...]\n\
\n\
geçerli seçenekler:\n\
  -S, --SA-mode                stand-alone modu yürütme\n\
  -C, --CS-mode                istemci-sunucu modunda yürütme\n\
  -r, --repair                 tutarsızlık bulunduğu zaman onar\n\
  -i, --input-class-file=FILE  sınıf adları girişi FILE\n\
                               varsayılan: tüm sınıfları kontrol edin\n\
  -I, --index-name             index name that will be checked only\n\
                               default: check all indices\n\
      --check-prev-link        check only previous links of indices\n\
      --repair-prev-link       önceki indeks bağlantıları onar\n\
      --check-file-tracker     check all allocated pages of file\n\
      --check-heap             check all pages of heap files\n\
      --check-catalog          check catalog consistency\n\
      --check-btree            check all btree indices\n\
      --check-class-name       check class name consistency\n\
      --check-btree-entries    check entries of btree consistency\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 Sayfa boyutu %2$s ile veritabanı '%1$s' için alan açıklaması. (log sayfa boyutu:%3$s)\n\n
16 %1$5d %2$9s  %3$s %4$s  %5$s\n
17 Çıktı dosyası '%1$s' açılamadı\n
18 Veritabanı '%1$s' sayfa boyutu %2$s için geçici yoğunlukları için alan açıklaması.\n\n
19 Volid Amacı  toplam_sayfa free_pages Vol Name\n\n
20 Volid Amacı  total_size free_size Vol Name\n\n
21 LOB alanı açıklaması %1$s\n\n
22 -------------------------------------------------------------------------------\n
30 sayfa boyutu %2$s ile veritabanı '%1$s' için özet alan açıklama. (sayfa boyutu log:%3$s)\n\n
31 Amaç toplam_sayfa used_pages free_pages volume_count\n
32 Amaç total_size used_size free_size volume_count\n
33 %1$10s %2$s %3$s %4$s %5$10d\n
34 -------------------------------------------------------------\n
40 Volid  Purpose   total_pages  free_pages  data_pages index_pages  temp_pages  Vol Name\n\n
41 Volid  Purpose    total_size   free_size   data_size  index_size   temp_size  Vol Name\n\n
42 %1$5d %2$9s  %3$s %4$s %5$s %6$s %7$s  %8$s\n
43 ----------------------------------------------------------------------------------------------------\n
50 Purpose    total_pages  used_pages  free_pages  data_pages index_pages  temp_pages  volume_count\n
51 Purpose     total_size   used_size   free_size   data_size  index_size   temp_size  volume_count\n
52 %1$10s %2$s %3$s %4$s %5$s %6$s %7$s %8$10d\n
53 -------------------------------------------------------------------------------------------------\n
60 \
spacedb: Kullanım / veritabanı boş alanı gösterir.\n\
kullanım: %1$s spacedb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE       FILE çıktı mesajları yönlendirme; varsayılan: hiçbir\n\
  -S, --SA-mode                stand-alone modu yürütme\n\
  -C, --CS-mode                istemci-sunucu modunda yürütme\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               gösterge ünitesi (sayfa, mega, giga, tera, insanların okuyabileceği); varsayılan: H\n\
  -s, --summarize              her birim bir amaç için sadece özet görüntüler\n\
  -p, --purpose                Her birimin her amacı (data/index/temp) kullanılan boyutunu görüntülemek\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Tahmini Sayfa Sayısı = %1$d\n
60 \
estimate_data: Veri Tahmini boyutu.\n\
kullanım: %1$s estimate_data sayı-örnekleri ortalama örnekli-boyut numarası-öznitelikleri değişken sayı-öznitelikleri\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Belirtilen Kötü indeks anahtar türü = %1$s\n
16 Kötü ortalama anahtar uzunluğu belirtilen = %1$d olduğunda hassasiyet = %2$d\n
17 Belirtilen Kötü bağımsız değişkenleri\n
20 Toplam dizin sayfaları Tahmini sayısı = %1$d\n (Endeksi yarattığı zaman, örnekler sonra doldurulacaktır edilmiştir.)\n
21 Toplam dizin sayfaları Tahmini sayısı = %1$d\n (Endeksi yarattığı zaman, örnekler önce doldurulması edilmiştir (ortalama durumda))\n
22 Toplam dizin sayfaları Tahmini sayısı = %1$d\n (Örnekleri doldurulan endeksi yarattığı zaman, önce (en kötü durumda))\n
23 Verilen giriş için...\n
24 Örneklerinin sayısı: %1$d\n
25 Farklı anahtar numarası: %1$d\n
26 Ortalama anahtar boyutu: %1$d\n
27 Anahtar Türü			  : %1$s\n\n
60 \
estimate_index: Bir dizinin boyutu tahmin.\n\
kullanım: %1$s estimate_index sayı-örnekleri sayısı-farklı-tuşları ortalama-key-size anahtar-tipi\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Bir veritabanı yükleyin.\n\
kullanım: %1$s installdb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
      --server-name=HOST       Sunucu HOST adı; varsayılan: geçerli ana bilgisayar adı\n\
  -F, --file-path=PATH         veritabanı yoğunlukları için dizin PATH; varsayılan: geçerli dizin\n\
  -L, --log-path=PATH          log yoğunlukları için dizin PATH; varsayılan: file-path aynı\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 Geçici sınıf OID\n
2 Elo değerleri yazdırılamıyor\n
3 Geçiş için uzun nesne erişim hatası\n
4 Uzun nesne geçiş için açılırken hata çıktı dosyası\n
5 Okuma hatası\n
6 Yazma hatası\n
7 Açılırken hata ELO girdi dosyası "%1$s"\n
8 CLASSES:
9 karma Dosya adı
10 Ad:                         		%1$s\n
11 Boyut:                         	%1$d\n
12 Sayfa Boyutu:                    %1$d\n
13 Veri Boyutu:                   	%1$d\n
14 Giriş Boyutu:                  	%1$d\n
15 Her Sayfa İçin Giriş:            %1$d\n
16 Önbelleğe alınan sayfaları:      %1$d\n
17 Girişlerinin sayısı:             %1$d\n
18 Çarpışmalar sayısı:              %1$d\n
19 Karma Dosya:                     %1$s\n
20 Sonraki taşma girişi:            %1$d\n
21 Anahtar türü:                    %1$s\n
22 Sayfa başlıkları Adresi:         %1$x\n
23 Son sayfa başlığının Adresi:     %1$x\n
24 Ücretsiz sayfa başlığının Adresi:%1$x\n
25 Sayfa bitmap Adresi:             %1$x\n
26 Sayfa bitmap boyutu:             %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Bir veritabanı dökümü.\n\
kullanım: %1$s diagdb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -d, --dump-type=TYPE         TYPE of dump\n\
                               -1 tüm dökümü\n\
                                1 dosya tabloları dökümü\n\
                                2 dosya kapasiteleri dökümü\n\
                                3 yığın kapasiteleri dökümü\n\
                                4 endeksi kapasiteleri dökümü\n\
                                5 sınıf adları dökümü\n\
                                6 disk bitmapler dökümü\n\
                                7 katalog dökümü\n\
                                8 log dökümü\n\
                                9 yığın dökümü\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 Çıktı dosyası '%1$s' açılmaadı \n
59 lockdb bağımsız modu olarak çalıştıramazsınız.\n
60 \
lockdb: veritabanının kilit durumunu görüntüler.\n\
kullanım: %1$s lockdb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE      FILE çıktı mesajları yönlendirme; varsayılan: hiçbir\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Sadece tek bir bağımsız değişkeni arasında verilmelidir: tran-index, user, host, progname\n
21 DBA şifrenizi girin:
26 Belirtilen bağımsız değişken değerleri ile bir işlem yoktur.\n
27 Aşağıdaki işlemleri iptal etmek için hazır:\n\n
28 Devam etmek istiyor musunuz? (Y/N)
29 İptal etme işlemi işlemin endeksi %1$d ile ilişkili\n
30 Aşağıdaki işlemleri İptal etme başarısız oldu\n\n
31 işlemin henüz bitmiş değil, ama geri alma işi olabilir.\n bir saniye bekleyin ve gerekirse daha sonra deneyin.\n
32 Geçersiz tranindex(%1$s).\n
59 killtran bağımsız modu olarak çalıştıramazsınız.\n
60 \
killtran: Veritabanı işlemleri yönetin.\n\
kullanım: %1$s killtran [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -i, --kill-transaction-index=INDEX işlemi İNDEX ile işlemi iptal etme\n\
      --kill-user-name=ID            kullanıcı ID ile tüm işlemleri iptal etme\n\
      --kill-host-name=HOST          istemci HOST ile tüm işlemleri iptal etme\n\
      --kill-program-name=NAME       istemci programı NAME ile tüm işlemleri iptal etme\n\
      --kill-sql-id=SQL_ID           li tüm geçişleri sonlandırır\n\
  -p, --dba-password=PASS            DBA kullanıcı şifresi; belirtmezseniz isteyecektir\n\
  -d, --display-information          etkin işlemler hakkında bilgi görüntülemek\n\
  -q, --query-exec-info              aktif geçişlerin sorgulama çalışması bilgisi hakkında bilgiyi görüntüler\n\
  -f, --force                        doğrulaması için bir istemi olmadan işlemi iptal etme\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 database-name
2 Gerekli bağımsız değişkeni, varsayılan yok
3 uyumluluk - denetimi
4 uyumluluk yalnızca kontrol edin
5 yedekleme-hedefi
6 yedekleme yoğunluk hedef için bir dizin veya aygıt
20 veritabanı '%1$s' yok.
21 DB sürüm çok düşük olduğundan, '%1$s', %2$s geçirilemez.\n
22 '%1$s' taşınmasına olanak gerekmez.\n
23 '%1$s' taşıma gerekir.\n
24 Yedekleme geçişten sonra önerilir. Şimdi veritabanınızı yedeklemek ister misiniz? (e/ h):
25 Bu taşınmanın ardından backupdb komutu ile yeni bir veritabanı yedeklemek için önerilir.
26 \n*** UYARI:taşıma başlamadan önce veritabanı yedekleme %1$s*** \n\nBu taşıması sırasında meydana gelen olabilecek hataları kurtarabilirsiniz, şiddetle veritabanınızı yedeklemenizi önerilir \n zaten veritabanı yedeklendiyse Varmı.? (e/h):
27 \n1. Backupdb\n2 çalıştırın..\n3: yeni veritabanı için yeni bir yedekleme path (%1$s geçerli path) girin. Çıkış
28 \nVeritabanı için yeni bir yedekleme path:
29 \nTaşınması ve yedekleme başarıyla tamamlandı.
30 Yedekleme başarısız oldu. Lütfen , Veritabanı yedeklemenizi yeniden deneyin.
31 \n\n*** UYARI: Bir kez geçiş tamamlandığında, eski yedekleme yoğunlukları ile veritabanını geri yüklemek yapamazsınız yedekleme dosyalarının önceki sürümü sadece restoredb ilgili sürümü restore edilebilir \n NOT..\n Lütfen, güvenli bir şekilde tutulmasını önceki yedekleme yoğunlukları kontrol edin.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Veritabanının bir ana bilgisayar değiştirebilir.\n\
kullanım: %1$s alterdbhost [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -h, --host=HOST              Yeni HOST adı; varsayılan: geçerli ana bilgisayar adı\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 database-name
2 Gerekli bağımsız değişkeni, bir varsayılanı yok
3 java sınıf dosyası
4 Gerekli bağımsız değişkeni, bir varsayılanı yok
5 zorlamak - üzerine yazmak
6 varsa sınıf dosyanın üzerine yazmak, varsayılan yok

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Çıktı dosyası '%1$s' açılamıyor \n
59 plandump bağımsız modu olarak çalıştıramazsınız.\n
60 \
plandump: Önbelleğe alınan sorgu planlarını görüntüleme ya da kontrol etmek.\n\
kullanım: %1$s plandump [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -d, --drop                   sunucunun önbelleğine tüm planları bırakın\n\
  -o, --output-file=FILE       FILE çıktı mesajları yönlendirme; varsayılan: hiçbir\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Çıktı dosyası açılamıyor'%1$s'\n
21 Sistem parametreleri istemci programı tarafından kullanılacak.\n
22 Sistem parametreleri sunucu '%1$s' tarafından yüklenen\n
23 Sistem parametrelerinin bağımsız modda kullanılır.\n
60 \
paramdump: Sistem parametre ayarları dökümü.\n\
kullanım: %1$s paramdump [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE       FILE çıktı iletiyi yönlendirmek; varsayılan: hiçbir\n\
  -b, --both                   istemci ve sunucu parametreleri tüm dökümü\n\
  -S, --SA-mode                stand-alone modu yürütme\n\
  -C, --CS-mode                istemci-sunucu modunda yürütme\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 yanlış bir değeri \"%1$s\" sunucunun HA çalışan modu için.\n
12 Sunucunun HA modunda çalışması değiştirilemez.\n
21 DBA şifrenizi girin:
22 Modunda çalışırken sunucu '%1$s' şu andaki HA %2$s dir.\n
23 Sunucu '%1$s' HA modunda çalışırken \"%2$s\"olarak değiştirildi.\n
24 Sunucu modunda HA ayarlı değil.\n
58 HA özelliği bu platformda desteklenmiyor.\n
59 changemode bağımsız modu olarak çalıştıramazsınız.\n
60 \
changemode: Sunucunun HA çalışma modunda görüntülemek veya değiştirmek.\n\
kullanım: %1$s changemode [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -m, --mode=MODE             MODE sunucu modunu değiştirmek; varsayılan: hiçbir\n\
  -f, --force                 değiştirmeye zorlamak\n\
  -t, --timeout=S             etkin işlem normalde sonlandırılmış iken S saniye bekliyor\n\
                              kullanıcı bakımı için sunucu modunda değiştirir sadece -t seçeneği etkilidir\n\
                              varsayılan: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 yanlış bir değeri \"%1$s\" copylogdb çalışan modu.\n
21 DBA şifrenizi girin:
22 Sunucu modunda HA ayarlı değil.\n
58 HA özelliği bu platformda desteklenmiyor.\n
59 copylogdb bağımsız modu olarak çalıştıramazsınız.\n
60 \
copylogdb: Sunucu günlük sayfaları kopyalayın.\n\
kullanım: %1$s copylogdb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -L, --log-path=PATH         Kopyalanan log yoğunlukları için dizin PATH\n\
  -m, --mode=MODE             çalışma modu, varsayılan: sync\n\
                              MODE: sync veya semisync veya async\n\
  -S, --start-page-id=ID      first log page to copy\n 

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 DBA şifrenizi girin:
22 Sunucu modunda HA ayarlı değil.\n
58 HA özelliği bu platformda desteklenmiyor.\n
59 applylogdb bağımsız modu olarak çalıştıramazsınız.\n
60 \
applylogdb: sunucuya kopyalanmaz log uygulayın.\n\
kullanım: %1$s applylogdb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -L, --log-path=PATH         Kopyalanan log yoğunlukların dizininde PATH\n\
      --max-mem-size=SIZE     MB maksimum işlem SIZE; max 1000\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Çıktı dosyası '%1$s' açılamıyor\n
59 statdump bağımsız modu olarak çalıştıramazsınız.\n
60 \
statdump: Genel istatistiklere dökümü.\n\
kullanım: %1$s statdump [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE       FILE çıktı iletiyi yönlendirmek; varsayılan: hiçbir\n\
  -c, --cumulative             kümülatif istatistikler Yazdırma\n\
  -i, --interval=S             bilgiyi her S saniye görüntüler\n\
  -s, --substr=STRING          substring eşleşen, stat öğeyi Yazdırma\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 DBA şifrenizi girin:
22 Sunucu modunda HA ayarlı değil.\n
58 HA özelliği bu platformda desteklenmiyor.\n
59 applyinfo standlone modu olarak çalıştıramazsınız.\n
60 \
applyinfo: Ekran CUBRID HA bilgileri uygulayın.\n\
kullanım: %1$s applyinfo [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -r, --remote-host-name      Uzak ana bilgisayar adı; görüntü uzak düğüm aktif log bilgileri\n\
  -a, --applied-info 	      uygulamalı bilgilerde görüntü \n\
  -L, --copied-log-path=PATH  Kopyalanan log yoğunlukların path; görüntü kopyalanan log bilgileri \n\
  -p, --page=ID               sayfa id; varsayılan: 0 \n\
  -v, --verbose               Ayrıntılı durum mesajları sağlamak; varsayılan: devre dışı bırakmak\n\
  -i, --interval=S            print information every S secs\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb bağımsız modu olarak çalıştıramazsınız.\n
60 \
acldb: Görüntüleme veya veritabanı erişim kontrol ayarı yeniden yükleyin.\n\
kullanım: %1$s acldb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -r, --reload       ip kontrol dosyası yeniden yükleyin\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
Mevcut komutlar:konum oluşturarak paylaşan kütüphane, Bu kütüphane LDML Giriş belgesi(s).\n\
Kullanımı: %1$s Mevcut komutlar[seçmiş] [Dil-Dizeler]\n\
\n\
Dil –Dizeler	      Konum dil dizeleri, Paylaşmak için kullanacak olan kütüphane.\n\
		      Eğer Dil dizeleri verilmemiş se, 'cubrid_konum.txt'\n\
		      içindeki tüm diller işlenecektir\n\
geçerli seçenekler:\n\
  -v, --verbose               Çok uzun durumlardaki bilgi işlemlerini yapabilen; Hata: Hayır\n\
  -i, --input-ldml-file       LDML dosya yolunu giriş yapın, Ama sadece Dil dizileri verdiği zamanda \n
61 Geçersiz konum adı tartışması. \n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 -i/--input-file vkonum onaylayıcı aynı  zamanda kullanılmaz.\n
58 Geçersiz kod noktası alanı (-s -e).\n
59 Geçersiz nokta dı tartışması.\n
60 \
Kopyalama yeri: yeri bırakıp kütüphane içeklerini paylaşmak, önce LDMLgiriş yapılan doyalar neden olmuştur\n\
Kullanımı: %1$s dumplocale [seçmiş] [Dil-Dizeler]\n\
\n\
Dil-Dizeler	 yeri ve dil dizeleri, yeri bırakıp kütüphane paylaşmak.\n\
		         Ele çalıştığında hiçbir dil dizesi vermemiştir, 'cubrid_konum.txt' \n\
			 deki tüm diller için işlem yapılacaktır\n\
geçerli seçenekler: \n\
  -i, --input-file    giriş yerinde kütüphane paylaşmanın yolu (Daha önce neden olmuştur). \n\
                      sadece dil dizeler ortaya çıkmadığı zamanda kullanabilir. \n\
  -d, --calendar      takvim ve Tarih/zaman bilgisini bırakmak; Hata: Hayır\n\
  -n, --numeric		  sayım bilgilerini bırakmak; Hata: Hayır\n\
  -a, --alphabet=l|lower|u|upper|both	     alfabe bırakılır,bilgilere neden olur; Hata: Hayır\n\
      --identifier-alphabet=l|lower|u|upper	 alfabe bırakılır, Onaylayıcı bilgilere neden olur; Hata: Hayır\n\
  -c, --codepoint-order	 kodlama noktası sayısal değeri temeli üzerinde kontrol edilecektir \n\
                         (cp, char,Ağırlık miktarı, yakından-cp, char veAğırlık miktarı);\n\
                         hata: hayır\n\
  -w, --weight-order  ağırlık miktarı temelini bırakma üzerinde kontrol edilmelidr\n\
                      (ağırlık, cp, char); Hata: Hayır\n\
  -s, --start-value	  filtre seçimi; Çalıştırma Kodlama noktası\n\
                      -a, --identifier-alphabet, -c, -w; Hata: 0\n\
  -e, --end-value     filtre seçimi; Çalıştırma Kodlama noktası\n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      hata: konumda verilerin en büyük sayısal değerinin okunmasını paylaşmak\n\
  -k, --console-conversion  verilerini bırakmak ; Hata: Hayır\n\
  -z, --normalization   Normalleştirilen verileri bırakmak; Hata: Hayır\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 Onların bölünme özelliğinin '%1$s'güncel derlemesini değiştirmeden önce bölümlere ayırma kaldırılmalıdır.\nClass name\n
44 Derleme '%1$s' olan özelliklerde yabancı tuşlar özelliklerin derlemesi değiştirilmeden önce düşürülmelidir.\nTable | Foreign Key\n
45 '%1$s' derlemesine sahip sınıflar; tekrardan tanımlanmalıdırlar:\nClass name\n
46 Derleme '%1$s' içeren ifadeye sahip fonksiyon indeksleri; tekrardan tanımlanmalıdırlar:\n\
Table | Index | Function expression\n
47 Derlemelerin senkronizasyonu gerçekleştirilmedi. Veri tabanı değişmedi.\n
48 '%1$s' sistem tablosu içerikleri yeni sistem derlemeleri ile güncellendi.\n
49 Sistem derlemeleri güncellemesi veri tabanı çökmesine neden olabilir. Devam (y/n) ?\n
50 Derleme '%1$s' (Id: %2$d) veri tabanında bulunmadı veya yeni sistem konfigürasyonunda değişti.\n
51 Derlee '%1$s' içeren koşula sahip tetikler; tekrardan tanımlanmalıdırlar:\nTrigger | Condition\n
52 Derleme '%1$s' içeren sorgulama görüntüle; tekrardan tanımlanmalıdırlar:\nView | Query\n
53 Derleme'%1$s' sahip özellikler; tekrardan tanımlanmalıdırlar.\nTable | Attribute Domain\n
54 Veri tabanında manüel müdahale gerekli.\n\
Lütfen dosya '%1$s' kontrol ediniz ve onu derlemeleri veri tabanı içine zorlamadan önce çalıştırın.\n
55 Senkronizasyon gerekli değil.\n
56 Sistem derlemesinin veri tabanı içine senkronizasyonu talep edilmektedir.\n\
Çalıştır 'cubrid synccolldb -f %1$s'\n
57 Veri tabanı ile karşılaştırıldığında sistem konfigürasyonunda %1$d yeni derlemeler vardır:\n
58 Veri tabanı '%1$s' teki tüm derlemeler yeni sistem konfigürasyonunda bulunmuştur.\n
59 Konfigüre edilmemiş veya sistem derlemelerine kıyasla değişmiş olan veri tabanında %1$d derlemeler var.\n
60 \
synccolldb: Derlemeleri sistem derlemeleri ile veri tabanından senkronize eder.\n\
kullanım: %1$s synccolldb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -c, --check-only    Sistemin derlemesi ile veri tabanından derlemeleri control eder.\n\
		      Eğer derleme senkronizasyonu talep edildiyse, hata kodu 1 geri döner.\n\
		      Uyumsuz derlemenin bir raporunu görüntüler.\n\
  -f, --force-only    derlemeleri veri tabanında yeni sistem konfigürasyonu ile aynı olacak şekilde günceller.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 %1$s şifre gir: 
22 Tran index          User name      Host name      Process id      Program name\n
23 -------------------------------------------------------------------------------\n
24 %1$4d%2$-10s %3$14s %4$14s %5$15d %6$17s\n
25 Geçişler yok\n
26 tranlist sadece DBA veya bir DBA grup üyesi tarafından uygulanabilir.\n
27 sort-key sütun numarası (%1$d) geçersiz.\n
32 Tran index         Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %1$4d%2$-10s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index          User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %1$4d%2$-10s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %1$d
46 SQL_ID: %1$s\n
59 tranlist standalone mode olarak çalışamaz.\n
60 \
tranlist: Aktif geçiş bilgisini boşalt.\n\
kullanım: %1$s tranlist [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -u, --user=ID                veri tabanı erişimi için kullanıcı kimliği; default: DBA user\n\
  -p, --password=PASS          kullanıcı şifresi; default: none\n\
  -s, --summary                geçişin sonuçlandırılmış bilgisini yazdır\n\
      --sort-key=COLUMN_NUMBER Sıralanacak sütun numarası\n\
      --reverse                Sıralama sonucu ters\n

$set 50 MSGCAT_UTIL_SET_PREFETCHLOGDB
22 Sunucu HA için yapılandırılmamıştır.\n
23 Prefetch özelliği devre dısı bırakılmıştır.\n
58 HA özellik bu platformda desteklenmiyor.\n
59 prefetchlogdb bağımsız modunda çalişmamaktadır.\n
60 \
prefetchlogdb: Sunucudan günlük sayfalarını okuyun.\n\
kullanım: %1$s prefetchlogdb [SEÇENEK] database-name\n\
\n\
Geçerli seçenekler:\n\
  -L, --log-path=PATH         kopyalan günlük hacımler için dizin yolu.\n
