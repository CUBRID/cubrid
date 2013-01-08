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

$set 1 MSGCAT_UTIL_SET_GENERIC
1 Geçersiz uzunluk veya karakter '%c' veritabanı adı '%s' \n
2 Çıktı dosyası '%s' açılamadı \n
3 Eksik veya geçersiz yardımcı programı adı.\n
4 Geçersiz yardımcı programı adı  '%s'\n
5 Aşağıdaki liste tanınan yardımcı programı adlarını içerir:\n
6 Geçersiz karakter '%c'ses adına'%s'\n
9 \
%s (CUBRID yardımcı programları)\n\
%s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
cubrid yönetici yardımcı programı, sürüm %s\n\
kullanım: %s <yardımcı programı-adı> [args]\n\
Tür '%s <yardımcı programı-adı>' belirli bir yardımcı programı hakkında yardım alın.\n\
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
\n\
%s DBMS için bir araçtır.\n\
Daha fazla bilgi için bkz:http://www.cubrid.com\n
12 '%s' yardımcı programı-adı geçersiz.\nTür 'cubrid --help' kullanıma bak.\n
13 '%s' komutu geçersiz.\nTür 'cubrid --help' kullanıma bak.\n
14 Yapılandırma dosyaları bir veya daha fazla zorunlu özellikleri yüklenemedi.\n
15 @ %s %s: %s\n
16 @ %s %s\n
17 ++ %s '%s'çalışmıyor.\n
18 ++ %s çalışmıyor.\n
19 ++ %s '%s' çalışmıyor.\n
20 ++ %s is running.\n
21 ++ %s %s: %s\n
22 Bir veya daha fazla bağımsız değişkeni eksik.\nTür 'cubrid --help' kullanıma bak.\n
23 cubrid yardımcı programı, sürüm %s\n\
kullanım: %s <yardımcı programı-adı> [args]\n\
Tür '%s <yardımcı programı-adı>' belirli bir yardımcı programı hakkında yardım alın.\n\
\n\
Mevcut hizmetin programları:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
    shard\n\
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
    copylogdb\n\
    applylogdb\n\
    applyinfo\n\
    genlocale\n\
    dumplocale\n\
    synccolldb\n\
\n\
%s DBMS için bir araçtır.\n\
Daha fazla bilgi için bkz: http://www.cubrid.com\n
24 cubrid yardımcı programı, sürüm %s\n\
kullanım: %s service <komut>\n\
\n\
Mevcut komut:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubrid yardımcı programı, sürüm %s\n\
kullanım: %s server <komut> [args]\n\
\n\
Mevcut komut:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    restart     [database-name]\n\
    status\n\
    acl         <status|reload> database-name\n
37 Sunucu HA için yapılandırıldı.\n
26 cubrid yardımcı programı, sürüm %s\n\
kullanım: %s broker <komut> [args]\n\
\n\
Mevcut komut:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [<expr>]\n\
    on          broker-name\n\
    off         broker-name\n\
    reset       broker-name\n\
    acl         <status|reload> [broker-name]\n
27 cubrid yardımcı programı, sürüm %s\n\
kullanım: %s manager <komut>\n\
\n\
Mevcut komut:\n\
    start\n\
    stop\n\
    status\n
28 cubrid yardımcı programı, sürüm %s\n\
kullanım: %s heartbeat <komut> [args]\n\
\n\
Mevcut komut:\n\
    start       [database-name]\n\
    stop        [database-name]\n\
    copylogdb   <start|stop> database-name node-name\n\
    applylogdb  <start|stop> database-name node-name\n\
    status\n\
    reload\n
31 '%s' argümanı gerekli değildir.\n
32 Bir veritabanı-adı eksik.\n
33 %s: Böyle seçeneği önerilmiyor.\n
34 Geçersiz Parametre. %s %s\n
35 Bellek ayrılamadı.\n
36 Sunucu HA için yapılandırıldı.\n
38 HA için ayarlanan sunucu ha_db_list.\n görülmedi. \n Listesinde
39 HA için ayarlanan sunucu in_ ha_node_list.\n görülmedi. \n Listesinde
62 Cubrid kullanım amacı, Versiyon %s\n\
usage: %s shard <komut> [args]\n\
\n\
Mevcut komut:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-t] [-s <sec>] [<expr>]\n\
    on          shard-adı\n\
    off         shard-adı\n\
    reset       shard-adı\n\
    acl         <status|reload> [shard-adı]\n\
    getid       -b <broker-name> [-f] shard-key\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 UYARI: -t seçeneği stand-alone modu yürütülmesine göz ardı edilir.\n
60 \
backupdb: Bir veritabanı yedeklemesi.\n\
kullanım: %s backupdb [SEÇENEK] veritabanı-adı\n\
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
71 Uyumsuz bağımsız değişkenleri, \"%s\", and \"%s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 HATA: Hedef adı kaynak adı ile aynıdır.\n
31 HATA: volext-pathadı mevcut değil veya yazılabilir değildir.\n
32 HATA: vols-tofrom-paths_file yok veya okunabilir değil değil.\n
60 \
copydb: Bir veritabanı kpyala.\n\
kullanım: %s copydb [SEÇENEK] src-database-name dest-database-name\n\
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
40 Group '%s' bulunmamaktadır\n
41 User '%s' bulunmamaktadır\n
42 Bilinmeyen komut '%s'\n
43 Çıktı dosyası '%s' açılamadı\n
44 Varolan veritabanı kaldırılamadı\n
45 Creating database with %s size. The total amount of disk space needed is %s.\n
46 Veritabanı oluşturulamadı.\n
47 '%s' Kullanıcı dosya ile ilgili sorun\n
48 The given '%s', %s is out of range. Choose a size between %s and %s.\n
49 %s Sayısal değeri '%s' geçersiz. \n
60 \
createdb: Bir veritabanı oluştur.\n\
kullanım: %s createdb [SEÇENEK] database-name\n\
\n\
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
kullanım: %s deletedb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE       DOSYA çıktı mesajları yönlendirme; varsayılan: hiçbir\n\
  -d, --delete-backup          yedekleme birimleri silin; varsayılan: silmeyin\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 HATA: volext-pathadı mevcut değil veya yazılabilir değildir.\n
32 HATA: vols-tofrom-paths_file yok veya okunabilir değil değil.\n
60 \
renamedb: Bir veritabanı yeniden adlandırın.\n\
kullanım: %s renamedb [SEÇENEK] src-database-name dest-database-name\n\
\n\
geçerli seçenekler:\n\
  -E, --extended-volume-path=PATH genişletilmiş bilgi yoğunlukları için dizin PATH; varsayılan: veritabanı yoğunlukları aynı path veya verilen path kontrol- dosyasında\n\
  -i, --control-file=FILE         kontrol dosyası yazılmış birkaç yerle adlandırma yoğunlukları; varsayılan: hiçbir\n\
  -d, --delete-backup             yedekleme birimleri silin; varsayılan: silmeyin\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 A %s süreç zaten çalışıyor.\n
12 MASTER SERVER başlangıç​​. \n
13 MASTER SERVER çıkarken.\n
14 Hata yerel ana bilgisayar adı bulmakta\n
15 Parametre dosyası bulunamadı\n
16 Ana işlemi başlatılamadı\n
17 Sunucu %s %d dakika içinde aşağı gidiyor
18 Sunucu %s kapatma bildirilir. \n Bu işlem birkaç dakika sürebilir. Lütfen bekleyiniz.
19 Sunucu %s bulunamadı.
20 Master %d dakika aşağı gidiyor\n


$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Çıktı dosyası '%s' açılamadı\n
60 \
optimizedb: Bir veritabanı optimize edin.\n\
kullanım: %s optimizedb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -n, --class-name=NAME        NAME sınıf için güncelleştirme istatistikleri; varsayılan: tüm sınıflar\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Geçersiz tarih biçimi, geçerli biçimi dd-mm-yyyy:hh:mm:ss veya 'backuptime'\n
20 Restoredb iptal edildi veya bir hata oluştu.\n
60 \
restoredb: Bir veritabanı geri yükleyin.\n\
kullanım: %s restoredb [SEÇENEK] database-name\n\
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
20 '%s' dosyası %d satır yoktur.\n
21 ara verme ... \n
22 ara verilen yük - mevcut işlem tamamlanmış\n
23 ara verilen yük - durduruldu mevcut işlem\n
24 '%s' dosyası açılamıyor\n
25 \n*** syntax kontrolü ***\n\n
26 %d hataları algılandı.\n
27 \n*** Ekleme nesneleri ***\n\n
28 Toplam %8d nesneleri eklenir.\n
29 Toplam %8d varsayılan içeriği ile eklenen nesneleri başvurulan.\n
30 \n*** İşlem İşlemek ***\n
31 \n*** Veritabanı Kapama ***\n
32 Satır %d:
33 Hata '%s' özniteliği '%s' için '%s' değer olarak '%s' ayrıştırma\n
34 %s özniteliği %s için eksik alanı\n
35 %s özniteliği %s için, tip %s set etki alanında izin verilmez\n
36 %s özniteliği %s için, tip %s beklenen, bir dizi sabiti var\n
37 %s özniteliği %s için, tip %s beklenen, %s var\n
38 %s özniteliği %s %s başvurulan adında bir sınıf\n
39 Hiçbir sınıf dizilerini görmezden gelerek, %s adlı\n
40 Tanımlayıcısı %d ile hiçbir sınıf %s özniteliği %s tarafından başvurulan\n
41 Class %s yükleyici girdi dosyasında izin verilmiyor\n
42 Hata ile Durduruldu.\n
43 UYARI: varolan örneği güncellendi!\n
44 UYARI: Sınıf %s Derece %d yeniden tanımlıyor\n
45 Sınıf %s Derece %d zaten tanımlanmış\n
46 Sınıf %s Derece %d zaten ayrılmıştır\n
47 Özniteliği '%s 'EŞSİZ aykırı, NULL'a dönüştürülür\n
48   %d örnekleri\n
49 Sınıf %s\n
50 Şifre:
51 \n*** Sınıf istatistikleri güncelleniyor ***\n
52 %d hat yakınlarındaki %s (metin '%s' oldu)\n
53 Sözcüksel hata
54 satır %d: \"%s\" içinde syntax hata
55 eksik
56 %s de
57 Uyumsuz bağımsız değişkenleri, \"%s\", ve \"%s\"\n
58 %d taahhüt örnekleri\n
59 \nLoaddb olmayan-optimize edilmiş sürümü çağırılamaz.\n
60 Hata ayrıştırma \"%s\", gibi %s tür.\n
112   %d örnekleri\r
113 Son kararlı hattı: %d
114 hata-kontrol-dosyası
115 yükleme sırasında bir hata(lar) kontrol etmek için giriş dosyası
120 \
loaddb: Veritabanına veri alma nesneler ve şemalar.\n\
kullanım: %s loaddb [SEÇENEK] database-name\n\
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
43 %d nesneler atılıyor.\n
44 Class %s
45 , %d örnekleri.\n
46 %d nesneleri yalnızca %d tanesinde başarılı atılıyor.\n
47 çıkış dizin çok uzun.\n
48 LSA : %d %d \n
60 \
unloaddb: Veritabanındaki nesneleri ve şemalar verin.\n\
kullanım: %s unloaddb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
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
12 %d işlenmiş nesneler.\n
13 \nPass 2\n\n
14 Class %s\n
15 oid:  %d|%d|%d,\n
16 %d örnekleri.\n
17 nesne güncelleniyor.\n
18 ref_oid:  %d.%d.%d, ref_class_oid:  %d.%d.%d\n
19 Nesne dönüşümü bulunamadı\n
20 Eksik sınıf yığın dosyası\n
21 Nesneyi güncelleştirme bulunamadı\n
22 Başarıyla işlenmiş %d nesnelerin sadece %d tanesi.\n
23 COMPACTDB Zaten başladı.\n
24 Sayfa sayısı %d ve %d arasında olmalıdır.\n
25 Örneğin kilit zaman aşımı %d ile %d arasında olmalıdır.
26 Toplam nesneler: %d\n
27 Başarısız nesneler: %d\n
28 Değiştirilmiş nesneler: %d\n
29 Büyük nesneler: %d\n
30 Alt sınıf temsilleri katalog silinir.\n
31 Alt sınıf temsilleri katalogda korunmuş.\n
32 Yalıtım düzeyi ayarlanamıyor.\n
33 Veritabanını düzenleme bulunamadı.\n
34 Sınıf kilit zaman aşımı %d ile %d arasında olmalıdır.\n
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
kullanım: %s compactdb [SEÇENEK] database-name [class_name1 class_name2 ...]\n\
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
21 Host üzerinde çalışan CUBRID Master %s \n başlandı %s
22 servisli %d  isteklerde bulunur. %d  sunucu (lar) çalışıyor.\n
23 %s dakika AŞAĞI KAPATIYOR\n
24 %s
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
  -D, --dereg-process=PID          hedef işlemi çıkmak ve yönetilen süreç girdileri kaldırmak\n\
  -F, --reconfig-node-list         yakın zamanda değişenleri CUBRID sinyal yapılandırmayı uygulamak\n\
  -U, --deactivate-heartbeat       CUBRID sinyal özelliği devre dışı bırakmak ve düğüm grubundan geçerli düğüm kaldırmak\n\
  -T, --activate-heartbeat         etkinleştirme CUBRID sinyal özelliği ve geçerli düğüm düğümün grubunun bir üyesi olur\n\
  -V, --verbose                    Ayrıntılı durum Mesajını sağlamak; varsayılan: Devre Dışı\n
28 Üzgünüz, bilinmeyen komut: %s\n
29 Yerel bilgisayar için ana bilgisayar adı alınamadı - durduruluyor\n
30 Parametre dosyası bulunamadı\n
31 %s ana sunucuya bağlantı kurulamadı\n
32 Kaç dakika kapatılmasına dek? :
33 Master %d dakika içinde kapanacaktır\n
34 İstemciden veri alma hatası
35 veritabanı sunucusu %s şu anda çalışmıyor
36 %s kabuk çalıştırma (%s), %s yapamam
37 süreç çatalı %s,%d,%s döndürülen
38 %s yürütemez


$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: Patch acil durumlarda bir veritabanı.\n\
kullanım: %s emergency_patchlog [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -r, --recreate-log           log dosyalarını yeniden; dikkatli bir şekilde kullanın\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Npages için yanlış değer %d\n
21 Yoğunlaştırma depolama amacıyla yanlış bir değeri '%s'.\n
22 WARNING: --max-writesize-in-sec option is ignored in stand-alone mode execution.\n
60 \
addvoldb: Add a volume-file for the database.\n\
kullanım: %s addvoldb [SEÇENEK] database-name\n\
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
20 Bazı tutarsızlıklar veritabanı tespit edildi.\n ek bilgi için error_log_file %s başvurun.\n
21 %s: Sınıf bulamıyor. Bu dahil edilmeyecektir.\n
60 \
checkdb:Veritabanının tutarlılığını denetleyin.\n\
kullanım: %s checkdb [SEÇENEK] database-name [class_name1 class_name2 ...]\n\
\n\
geçerli seçenekler:\n\
  -S, --SA-mode                stand-alone modu yürütme\n\
  -C, --CS-mode                istemci-sunucu modunda yürütme\n\
  -r, --repair                 tutarsızlık bulunduğu zaman onar\n\
  -i, --input-class-file=FILE  sınıf adları girişi FILE\n\
                               varsayılan: tüm sınıfları kontrol edin\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 Sayfa boyutu %s ile veritabanı '%s' için alan açıklaması. (log sayfa boyutu:%s)\n\n
16 %5d %8s  %s %s  %s\n
17 Çıktı dosyası '%s' açılamadı\n
18 Veritabanı '%s' sayfa boyutu %s için geçici yoğunlukları için alan açıklaması.\n\n
19 Volid Amacı toplam_sayfa free_pages Vol Name\n\n
20 Volid Amacı total_size free_size Vol Name\n\n
21 LOB alanı açıklaması %s\n\n
30 sayfa boyutu %s ile veritabanı '%s' için özet alan açıklama. (sayfa boyutu log:%s)\n\n
31 Amaç toplam_sayfa used_pages free_pages volume_count\n
32 Amaç total_size used_size free_size volume_count\n
33 %10s %s %s %s %10d\n
60 \
spacedb: Kullanım / veritabanı boş alanı gösterir.\n\
kullanım: %s spacedb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE       FILE çıktı mesajları yönlendirme; varsayılan: hiçbir\n\
  -S, --SA-mode                stand-alone modu yürütme\n\
  -C, --CS-mode                istemci-sunucu modunda yürütme\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               gösterge ünitesi (sayfa, mega, giga, tera, insanların okuyabileceği); varsayılan: H\n\
  -s, --summarize              her birim bir amaç için sadece özet görüntüler\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Tahmini Sayfa Sayısı = %d\n
60 \
estimate_data: Veri Tahmini boyutu.\n\
kullanım: %s estimate_data sayı-örnekleri ortalama örnekli-boyut numarası-öznitelikleri değişken sayı-öznitelikleri\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Belirtilen Kötü indeks anahtar türü = %s\n
16 Kötü ortalama anahtar uzunluğu belirtilen = %d olduğunda hassasiyet= %d\n
17 Belirtilen Kötü bağımsız değişkenleri\n
20 Toplam dizin sayfaları Tahmini sayısı = %d\n (Endeksi yarattığı zaman, örnekler sonra doldurulacaktır edilmiştir.)\n
21 Toplam dizin sayfaları Tahmini sayısı = %d\n (Endeksi yarattığı zaman, örnekler önce doldurulması edilmiştir (ortalama durumda))\n
22 Toplam dizin sayfaları Tahmini sayısı = %d\n (Örnekleri doldurulan endeksi yarattığı zaman, önce (en kötü durumda))\n
23 Verilen giriş için...\n
24 Örneklerinin sayısı: %d\n
25 Farklı anahtar numarası: %d\n
26 Ortalama anahtar boyutu: %d\n
27 Anahtar Türü			  : %s\n\n
60 \
estimate_index: Bir dizinin boyutu tahmin.\n\
kullanım: %s estimate_index sayı-örnekleri sayısı-farklı-tuşları ortalama-key-size anahtar-tipi\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Bir veritabanı yükleyin.\n\
kullanım: %s installdb [SEÇENEK] database-name\n\
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
7 Açılırken hata ELO girdi dosyası "%s"\n
8 CLASSES:
9 karma Dosya adı
10 Ad:                         		%s\n
11 Boyut:                         	%d\n
12 Sayfa Boyutu:                    %d\n
13 Veri Boyutu:                   	%d\n
14 Giriş Boyutu:                  	%d\n
15 Her Sayfa İçin Giriş:            %d\n
16 Önbelleğe alınan sayfaları:      %d\n
17 Girişlerinin sayısı:             %d\n
18 Çarpışmalar sayısı:              %d\n
19 Karma Dosya:                     %s\n
20 Sonraki taşma girişi:            %d\n
21 Anahtar türü:                    %s\n
22 Sayfa başlıkları Adresi:         %x\n
23 Son sayfa başlığının Adresi:     %x\n
24 Ücretsiz sayfa başlığının Adresi:%x\n
25 Sayfa bitmap Adresi:             %x\n
26 Sayfa bitmap boyutu:             %d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Bir veritabanı dökümü.\n\
kullanım: %s diagdb [SEÇENEK] database-name\n\
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
15 Çıktı dosyası '%s' açılmaadı \n
59 lockdb bağımsız modu olarak çalıştıramazsınız.\n
60 \
lockdb: veritabanının kilit durumunu görüntüler.\n\
kullanım: %s lockdb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE      FILE çıktı mesajları yönlendirme; varsayılan: hiçbir\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Sadece tek bir bağımsız değişkeni arasında verilmelidir: tran-index, user, host, progname\n
21 DBA şifrenizi girin:
26 Belirtilen bağımsız değişken değerleri ile bir işlem yoktur.\n
27 Aşağıdaki işlemleri iptal etmek için hazır:\n\n
28 Devam etmek istiyor musunuz? (Y/N)
29 İptal etme işlemi işlemin endeksi %d ile ilişkili\n
30 Aşağıdaki işlemleri İptal etme başarısız oldu\n\n
31 işlemin henüz bitmiş değil, ama geri alma işi olabilir.\n bir saniye bekleyin ve gerekirse daha sonra deneyin.\n
32 Invalid tranindex(%s).\n
59 killtran bağımsız modu olarak çalıştıramazsınız.\n
60 \
killtran: Veritabanı işlemleri yönetin.\n\
kullanım: %s killtran [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -i, --kill-transaction-index=INDEX işlemi İNDEX ile işlemi iptal etme\n\
      --kill-user-name=ID            kullanıcı ID ile tüm işlemleri iptal etme\n\
      --kill-host-name=HOST          istemci HOST ile tüm işlemleri iptal etme\n\
      --kill-program-name=NAME       istemci programı NAME ile tüm işlemleri iptal etme\n\
      --kill-sql-id=SQL_ID           kill all transactions with SQL_ID\n\
  -p, --dba-password=PASS            DBA kullanıcı şifresi; belirtmezseniz isteyecektir\n\
  -d, --display-information          etkin işlemler hakkında bilgi görüntülemek\n\
  -q, --query-exec-info              display information about query execution information of active transactions\n\
  -f, --force                        doğrulaması için bir istemi olmadan işlemi iptal etme\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 database-name
2 Gerekli bağımsız değişkeni, varsayılan yok
3 uyumluluk - denetimi
4 uyumluluk yalnızca kontrol edin
5 yedekleme-hedefi
6 yedekleme yoğunluk hedef için bir dizin veya aygıt
20 veritabanı '%s' yok.
21 DB sürüm çok düşük olduğundan, '%s',%s geçirilemez.\n
22 '%s' taşınmasına olanak gerekmez.\n
23 '%s' taşıma gerekir.\n
24 Yedekleme geçişten sonra önerilir. Şimdi veritabanınızı yedeklemek ister misiniz? (e/ h):
25 Bu taşınmanın ardından backupdb komutu ile yeni bir veritabanı yedeklemek için önerilir.
26 \n*** UYARI:taşıma başlamadan önce veritabanı yedekleme %s*** \n\nBu taşıması sırasında meydana gelen olabilecek hataları kurtarabilirsiniz, şiddetle veritabanınızı yedeklemenizi önerilir \n zaten veritabanı yedeklendiyse Varmı.? (e/h):
27 \n1. Backupdb\n2 çalıştırın..\n3: yeni veritabanı için yeni bir yedekleme path (%s geçerli path) girin. Çıkış
28 \nVeritabanı için yeni bir yedekleme path:
29 \nTaşınması ve yedekleme başarıyla tamamlandı.
30 Yedekleme başarısız oldu. Lütfen , Veritabanı yedeklemenizi yeniden deneyin.
31 \n\n*** UYARI: Bir kez geçiş tamamlandığında, eski yedekleme yoğunlukları ile veritabanını geri yüklemek yapamazsınız yedekleme dosyalarının önceki sürümü sadece restoredb ilgili sürümü restore edilebilir \n NOT..\n Lütfen, güvenli bir şekilde tutulmasını önceki yedekleme yoğunlukları kontrol edin.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Veritabanının bir ana bilgisayar değiştirebilir.\n\
kullanım: %s alterdbhost [SEÇENEK] database-name\n\
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
15 Çıktı dosyası '%s' açılamıyor \n
59 plandump bağımsız modu olarak çalıştıramazsınız.\n
60 \
plandump: Önbelleğe alınan sorgu planlarını görüntüleme ya da kontrol etmek.\n\
kullanım: %s plandump [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -d, --drop                   sunucunun önbelleğine tüm planları bırakın\n\
  -o, --output-file=FILE       FILE çıktı mesajları yönlendirme; varsayılan: hiçbir\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Çıktı dosyası açılamıyor'%s'\n
21 Sistem parametreleri istemci programı tarafından kullanılacak.\n
22 Sistem parametreleri sunucu '%s' tarafından yüklenen\n
23 Sistem parametrelerinin bağımsız modda kullanılır.\n
60 \
paramdump: Sistem parametre ayarları dökümü.\n\
kullanım: %s paramdump [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE       FILE çıktı iletiyi yönlendirmek; varsayılan: hiçbir\n\
  -b, --both                   istemci ve sunucu parametreleri tüm dökümü\n\
  -S, --SA-mode                stand-alone modu yürütme\n\
  -C, --CS-mode                istemci-sunucu modunda yürütme\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 yanlış bir değeri \"%s\" sunucunun HA çalışan modu için.\n
12 Sunucunun HA modunda çalışması değiştirilemez.\n
21 DBA şifrenizi girin:
22 Modunda çalışırken sunucu '%s'' şu andaki HA %s dir.\n
23 Sunucu '%s'' HA modunda çalışırken \"%s\"olarak değiştirildi.\n
24 Sunucu modunda HA ayarlı değil.\n
58 HA özelliği bu platformda desteklenmiyor.\n
59 changemode bağımsız modu olarak çalıştıramazsınız.\n
60 \
changemode: Sunucunun HA çalışma modunda görüntülemek veya değiştirmek.\n\
kullanım: %s changemode [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -m, --mode=MODE             MODE sunucu modunu değiştirmek; varsayılan: hiçbir\n\
  -f, --force                 değiştirmeye zorlamak\n\
  -t, --timeout=S             etkin işlem normalde sonlandırılmış iken S saniye bekliyor\n\
                              kullanıcı bakımı için sunucu modunda değiştirir sadece -t seçeneği etkilidir\n\
                              varsayılan: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 yanlış bir değeri \"%s\" copylogdb çalışan modu.\n
21 DBA şifrenizi girin:
22 Sunucu modunda HA ayarlı değil.\n
58 HA özelliği bu platformda desteklenmiyor.\n
59 copylogdb bağımsız modu olarak çalıştıramazsınız.\n
60 \
copylogdb: Sunucu günlük sayfaları kopyalayın.\n\
kullanım: %s copylogdb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -L, --log-path=PATH         Kopyalanan log yoğunlukları için dizin PATH\n\
  -m, --mode=MODE             çalışma modu, varsayılan: sync\n\
                              MODE: sync veya semisync veya async\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 DBA şifrenizi girin:
22 Sunucu modunda HA ayarlı değil.\n
58 HA özelliği bu platformda desteklenmiyor.\n
59 applylogdb bağımsız modu olarak çalıştıramazsınız.\n
60 \
applylogdb: sunucuya kopyalanmaz log uygulayın.\n\
kullanım: %s applylogdb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -L, --log-path=PATH         Kopyalanan log yoğunlukların dizininde PATH\n\
      --max-mem-size=SIZE     MB maksimum işlem SIZE; max 1000\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Çıktı dosyası '%s' açılamıyor\n
59 statdump bağımsız modu olarak çalıştıramazsınız.\n
60 \
statdump: Genel istatistiklere dökümü.\n\
kullanım: %s statdump [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -o, --output-file=FILE       FILE çıktı iletiyi yönlendirmek; varsayılan: hiçbir\n\
  -c, --cumulative             kümülatif istatistikler Yazdırma\n\
  -i, --interval=S             istatistikleri her S saniye Yazdırma\n\
  -s, --substr=STRING          substring eşleşen, stat öğeyi Yazdırma\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 DBA şifrenizi girin:
22 Sunucu modunda HA ayarlı değil.\n
58 HA özelliği bu platformda desteklenmiyor.\n
59 applyinfo standlone modu olarak çalıştıramazsınız.\n
60 \
applyinfo: Ekran CUBRID HA bilgileri uygulayın.\n\
kullanım: %s applyinfo [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -r, --remote-host-name      Uzak ana bilgisayar adı; görüntü uzak düğüm aktif log bilgileri\n\
  -a, --applied-info 	      uygulamalı bilgilerde görüntü \n\
  -L, --copied-log-path=PATH  Kopyalanan log yoğunlukların path; görüntü kopyalanan log bilgileri \n\
  -p, --pageid=ID             sayfa id; varsayılan: 0 (etkin sayfa)\n\
  -v, --verbose               Ayrıntılı durum mesajları sağlamak; varsayılan: devre dışı bırakmak\n\
  -i, --interval=S            print information every S secs\n	

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb bağımsız modu olarak çalıştıramazsınız.\n
60 \
acldb: Görüntüleme veya veritabanı erişim kontrol ayarı yeniden yükleyin.\n\
kullanım: %s acldb [SEÇENEK] database-name\n\
\n\
geçerli seçenekler:\n\
  -r, --reload       ip kontrol dosyası yeniden yükleyin\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
Mevcut komutlar:konum oluşturarak paylaşan kütüphane, Bu kütüphane LDML Giriş belgesi(s).\n\
Kullanımı: %s Mevcut komutlar[seçmiş] [Dil-Dizeler]\n\
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
Kullanımı: %s dumplocale [seçmiş] [Dil-Dizeler]\n\
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
44 Foreign keys on attributes having collation '%s' must be dropped before changing collation of attributes.\nTable | Foreign Key\n
45 Classes having collation '%s'; they should be redefined :\nClass name\n
46 Function indexes having expression containing collation '%s'; they should be redefined :\n\
Table | Index | Function expression\n
47 Synchronization of collations was not performed. Database was not changed.\n
48 Contents of '%s' system table was updated with new system collations.\n
49 Updating system collations may cause corruption of database. Continue (y/n) ?\n
50 Collation '%s' (Id: %d) not found in database or changed in new system configuration.\n
51 Triggers having condition containing collation '%s'; they should be redefined :\nTrigger | Condition\n
52 Query views containing collation '%s'; they should be redefined :\nView | Query\n
53 Attributes having collation '%s'; they should be redefined.\nTable | Attribute Domain\n
54 Manual intervention on database is required.\n\
Please check file '%s' and execute it before forcing collations into database.\n
55 Synchronization is not needed.\n
56 Synchronization of system collation into database is required.\n\
Run 'cubrid synccolldb -f %s'\n
57 There are %d new collations in system configuration compared to database:\n
58 All collations in database '%s' are found in the new system configuration.\n
59 There are %d collations in database which are not configured or are changed compared to system collations.\n
60 \
synccolldb: Synchronizes the collations from database with collations of system.\n\
usage: %s synccolldb [OPTION] database-name\n\
\n\
valid options:\n\
  -c, --check-only    Checks the collations from databases with collation of system.\n\
		      If collations synchronization is required, error code 1 is returned.\n\
		      Displays a report of incompatible collations.\n\
  -f, --force-only    Updates the collations in databases to be same as the collations of new sytem configuration.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Enter %s password: 
22 Tran index          User name      Host name      Process id      Program name\n
23 -------------------------------------------------------------------------------\n
24 %4d%-10s %14s %14s %15d %17s\n
25 Hiçbir işlem bulunmamaktadır\n
26 tranlist can only be performed by the DBA or a DBA group member.\n
32 Tran index         Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %4d%-10s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index          User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %4d%-10s %14s %14s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %d
46 SQL_ID: %s\n
59 tranlist bağımsız modu olarak çalıştıramazsınız.\n
60 \
tranlist: Dump active transaction information.\n\
usage: %s tranlist [OPTION] database-name\n\
\n\
valid options:\n\
  -u, --user=ID                user ID for database access; default: DBA user\n\
  -p, --password=PASS          user password; default: none\n\
  -s, --summary                print summarized information of transaction\n
