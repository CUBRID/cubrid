
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
$ esql.msg
$

$set 1
1 tamsayı argüman değeri bulunmamaktadır "%1$s"\n
2 floating argüman değeri bulunmamaktadır "%1$s"\n
3 kayıp argüman %1$s\n
4 [ayarlar] 
5 Ayarlar: \n
6 Anahtar için kayıp argüman değeri %1$s\n
7 Bilinmeyen ayarlar "%1$s"\n
8 Beklenmeyen konumsal argüman "%1$s"\n

$set 2
1 İmlecin yeniden tanımlanması "%1$s"
2  *\tfor ifade %1$s\n
3  *\n * imleç tablosu:\n

$set 3
1 (set_class_bit içinde DAHİLİ HATA): kötü saklama sınıfı(%1$d)
2 (add_spec_to_decl() içinde DAHİLİ HATA): bozuk zinciri (belirtici bulunmamaktadır)
3 saklama sınıfı belirteci bu bağlamda izin verilmez
4 yasadışı depolama sınıfı kombinasyonu
5 yasadışı tür kombinasyonu
6 yasadışı değiştirici kombinasyonu
7 (pp_add_type_spec içinde DAHİLİ HATA): Beklenmedik durum (%1$d)
8 (pp_add_type_adj içinde DAHİLİ HATA): Beklenmedik durum (%1$d)
9 bozuk biçim %1$s beyanı

$set 4
1 %1$s
2 %1$s: Bir dizin değil\n
3 %1$s: Kotü yüklenmiş kök?\n

$set 5
1 ?
2 simge doğruluğunu kontrol ediniz
3 Parçası USING statik imleç için izin "%1$s"
4 iöleç "%1$s" tanımsız
5 Bir tanımlayıcı göstericisi
6 Bir DB_OBJECT göstericisi
7 Karakterseti dizini
8 "%1$s" için gösterge değişkeni bu bağlamda izin verilmez
9 "%1$s" bölümünde BEGIN DECLARE içinde bildirilmemiş
52 %1$s ( metin "%2$s")
53 Sözcüksel Hata
54 sözdizimi hatası "%1$s": %3$s içinde %2$s kayıp
55 sözdizimi hatası "%1$s": %2$s kayıp

$set 6
1 suppress-line-directive
2 output-file
3 include-file
4 dump-scope-info
5 dump-malloc-info
6 announce-version
7 enable-uci-trace
8 disable-varchar-length
9 varchar2-style
10 unsafe-null
11 internal-indicator
12 input-file

$set 7
1 çıktı dosyasında #satır yönergeleri çıktı dosyası, varsayılan kapalı
2 çıktı dosyası adı, varsayılan <input-file>.c veya stdout
3 dahili dosya iöermekte uci_ prototipler, varsayılan "esqlx.h"
4 dump kapsamı hata ayıklama bilgisi, varsayılan kapalı
5 dump final malloc hata ayıklama bilgileri, varsayılan kapalı
6 sürüm numarası bilgilerini yazdırabilirsiniz, varsayılan kapalı
7 UCI fonksiyonu iz sağlar, varsayılan kapalı
8 VARCHAR ana değişken uzunluğu başlatma devre dışı bırakın, varsayılan kapalı
9 Farklı VARCHAR stili kullanabilirsiniz, varsayılan kapalı
10 -462 (null göstergesi gerekli) hatası görmezden geliyor., varsayılan kapalı
11 -462 (null göstergesi gerekli) hatayı önlemek için iç NULL gösterge kullanabilirsiniz, varsayılan kapalı
12 Girdi dosyası adı, default stdin

$set 8
1 REPEATED seçeneği göz ardı edildi.
2 %1$s sürüm %2$s\n

$set 9
1 (yy_pop_mode) iç hata: boş mod yığını
2 "%1$s" bu bağlamda izin verilmez

$set 10
1 iç hata %1$s: %2$s\n
2 tablo taşma

$set 11
1 gösterge değişkeni "%1$s" kısa bir int olmalı
2 bir yapıya sahip bir gösterge değişken kullanamaz
3 "%1$s" eksik yapı tanımı vardır
4 "%1$s" mutlaka geçerli olamalı %2$s
5 %1$s için kabul edilebilir tür değil "%2$s"
6 beklenmeyen ana değişken tipi: %1$s
7 (pp_addr_expr): NULL değişkenin adresini bulma girişimi
8 "%1$s" bir işaretçi değildir.
9 "%1$s" bir yapı işaret değildir.
10 "%1$s" bir yapı değildir.
11 %1$s herhangi bir alan yoktur "%2$s"
12 "%2$s" tarafından başvurusu çözülemiyor "%1$s"

$set 12
1 bellek yetersiz

$set 13
1 fonksiyonların dizi yasaktır, işlev işaretçileri varsayarak.
2 (pp_the_same_type) bilinmeyen bağlantı sınıfı
3 (pp_get_sizeof): kötü isim (%1$d)

$set 14
1 (%1$s DAHİLİ HATA): imkansız durumda
