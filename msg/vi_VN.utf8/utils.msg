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
$ The following set numbers have been assigned:
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
1 Độ dài hoặc ký tự '%c' không hợp lệ trong tên cơ sở dữ liệu '%s'\n
2 Không thể mở tệp tin đầu ra '%s'\n
3 Tên tiện ích bị thiếu hoặc không hợp lệ.\n
4 Tên tiện ích không hợp lệ '%s'\n
5 Danh sách sau đây chứa các tên tiện ích được nhận biết:\n
6 Ký tự '%c' không hợp lệ trong tên bộ đĩa '%s'\n
9 \
%s (Tiện ích CUBRID)\n\
%s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
tiện ích quản trị viên, phiên bản %s\n\
cách sử dụng: %s <tên-tiện ích> [đối số]\n\
Gõ '%s <tên-tiện ích>' để có trợ giúp trên một tiện ích chỉ định.\n\
\n\
Các tên-tiện ích quản trị có thể dùng:\n\
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
%s là một công cụ cho DBMS.\n\
Để biết thêm thông tin, truy cập http://www.cubrid.com\n
12 tên-tiện ích '%s' không hợp lệ.\nGõ 'cubrid --help' để xem cách sử dụng.\n
13 Lệnh '%s' không hợp lệ.\nGõ 'cubrid --help' để xem cách sử dụng.\n
14 Không thể tải một hoặc nhiều thuộc tính bắt buộc trong tệp tin cấu hình.\n
15 @ %s %s: %s\n
16 @ %s %s\n
17 ++ %s '%s' đang không chạy.\n
18 ++ %s đang không chạy.\n
19 ++ %s '%s' đang chạy.\n
20 ++ %s đang chạy.\n
21 ++ %s %s: %s\n
22 Thiếu một hoặc nhiều đối số.\nGõ 'cubrid --help' để xem cách sử dụng.\n
23 tiện ích cubrid, phiên bản %s\n\
cách sử dụng: %s <tên-tiện ích> [đối số]\n\
Gõ '%s <tên-tiện ích>' để có trợ giúp trên một tiện ích chỉ định.\n\
\n\
Các tiện ích của dịch vụ có thể dùng:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
\n\
Các tiện ích của quản trị viên có thể dùng:\n\
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
%s là một công cụ cho DBMS.\n\
Để biết thêm thông tin, truy cập http://www.cubrid.com\n
24 tiện ích cubrid, phiên bản %s\n\
cách sử dụng: %s service <lệnh>\n\
\n\
Lệnh có thể dùng:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 tiện ích cubrid, phiên bản %s\n\
cách sử dụng: %s server <lệnh> [đối số]\n\
\n\
Lệnh có thể dùng :\n\
    start       [tên-cơ sở dữ liệu]\n\
    stop        [tên-cơ sở dữ liệu]\n\
    restart     [tên-cơ sở dữ liệu]\n\
    status\n\
    acl         <status|reload> tên-cơ sở dữ liệu\n
37 máy chủ được cấu hình cho HA.\n
26 tiện ích cubrid, phiên bản %s\n\
cách sử dụng: %s broker <lệnh> [đối số]\n\
\n\
Lệnh có thể dùng :\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-m] [-c] [<expr>]\n\
    on          tên-trung gian\n\
    off         tên-trung gian\n\
    reset       tên-trung gian\n\
    acl         <status|reload> [tên-trung gian]\n\
    getid       -b <tên-trung gian> [-f] shard-key\n\
    test        tên-trung gian [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 tiện ích cubrid, phiên bản %s\n\
cách sử dụng: %s manager <lệnh>\n\
\n\
Lệnh có thể dùng :\n\
    start\n\
    stop\n\
    status\n
28 tiện ích cubrid, phiên bản %s\n\
cách sử dụng: %s heartbeat <lệnh> [đối số]\n\
\n\
Lệnh có thể dùng :\n\
    start       [tên cơ sở dữ liệu]\n\
    stop        [-i|tên cơ sở dữ liệu]\n\
    copylogdb   <start|stop> <tên cơ sở dữ> <liệu tên> node\n\
    applylogdb  <start|stop> <tên cơ sở dữ> <liệu tên> node\n\
    status\n\
    reload\n
31 Đối số'%s' không cần thiết.\n
32 Thiếu một tên-cơ sở dữ liệu.\n
33 %s: tùy chọn này không được chấp nhận.\n
34 Tham số không hợp lệ. %s %s\n
35 Không thể cấp phát bộ nhớ.\n
36 The server was not configured for HA.\n
38 Máy chủ được cấu hình cho HA không được liệt kê trong ha_db_list.\n
39 Máy chủ được cấu hình cho HA không được liệt kê trong ha_node_list.\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 CẢNH BÁO: tùy chọn -t bị bỏ qua trong thực thi chế độ độc lập.\n
31 Lỗi: Đường dẫn không tồn tại hoặc không phải một thư mục.\n
60 \
backupdb: Sao lưu một cơ sở dữ liệu.\n\
cách sử dụng: %s backupdb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -D, --destination-path=PATH  lưu trữ bộ đĩa sao lưu đến đường dẫn thư mục; mặc định: thư mục bản ghi của cơ sở dữ liệu\n\
  -r, --remove-archive         xóa các lưu trữ-bản ghi không cần thiết; sử dụng cẩn thận và tham khảo hướng dẫn\n\
  -l, --level=LEVEL            cấp độ sao lưu; mặc định: cấp độ đầy\n\
                               LEVEL được cho phép:\n\
                                   0 - đầy\n\
                                   1 - tăng dần 1\n\
                                   2 - tăng dần 2\n\
  -o, --output-file=FILE       in các thông báo sao lưu chi tiết vào FILE\n\
  -S, --SA-mode                thực thi chế độ độc lập\n\
  -C, --CS-mode                thực thi chế độ client-server\n\
      --no-check               không kiểm tra tính đồng nhất của cơ sở dữ liệu; mặc định: kiểm tra\n\
  -t, --thread-count=COUNT     số tuyến đoạn; mặc định: 0(tự động)\n\
  -z, --compress               nén các bộ đĩa sao lưu; mặc định: không nén\n\
  -e, --except-active-log      không bao gồm một bộ đĩa bản ghi đang hoạt động; mặc định: bao gồm\n\
     --sleep-msecs=N           ngủ N mili giây trên tốc độ đọc 1M; mặc định: 0\n
71 Các đối số không tương thích, \"%s\", và \"%s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 LỖI: Tên đích nhận trùng với tên nguồn.\n
31 LỖI: volext-pathname không tồn tại hoặc không thể được ghi.\n
32 LỖI: vols-tofrom-paths_file không tồn tại hoặc không thể được đọc.\n
60 \
copydb: Sao chép một cơ sở dữ liệu.\n\
cách sử dụng: %s copydb [TÙY CHỌN] tên-cơ sở dữ liệu-nguồn  tên-cơ sở dữ liệu-đích nhận\n\
\n\
các tùy chọn hợp lệ:\n\
      --server-name=HOST          tên HOST của server; mặc định: tên máy chủ hiện tại\n\
  -F, --file-path=PATH            đường dẫn thư mục cho bộ đĩa cơ sở dữ liệu; mặc định: thư mục hiện tại\n\
  -L, --log-path=PATH             đường dẫn thư mục cho bộ đĩa bản ghi; mặc định: giống với file-path\n\
  -B, --lob-base-path=PATH        đường dẫn thư mục cơ sở cho dữ liệu LOB bên ngoài; mặc định: <file-path>/lob\n\
  -E, --extended-volume-path=PATH đường dẫn thư mục cho bộ đĩa thông tin mở rộng; mặc định: đường dẫn giống với bộ đĩa cơ sở dữ liệu hoặc đường dẫn đã cho trong control-file\n\
  -i, --control-file=FILE         sao chép bộ đĩa trên một số vị trí được ghi trong tệp tin kiểm soát; mặc định: không có\n\
  -r, --replace                   thay thế một cơ sở dữ liệu hiện có; mặc định: không thay thế\n\
  -d, --delete-source             xóa một cơ sở dữ liệu nguồn; mặc định: không xóa\n\
      --copy-lob-path             sao chép đường dẫn kho lưu trữ bên ngoài cho dữ liệu LOB từ cơ sở dữ liệu nguồn. không thể được sử dụng với tùy chọn -B; mặc định: không sao chép\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 Nhóm '%s' không tồn tại\n
41 Người dùng '%s' không tồn tại\n
42 Lệnh '%s' không rõ\n
43 Không thể mở tệp tin đầu ra '%s'\n
44 Không thể gỡ bỏ cơ sở dữ liệu hiện có.\n
45 Tạo cơ sở dữ liệu sử dụng ngôn ngữ mặc định %1$s với %2$s kích thước. Tổng số không gian đĩa trống cần thiết là %3$s.\n
46 Không thể tạo cơ sở dữ liệu.\n
47 Vấn đề với tệp tin người dùng '%s'\n
48 Với '%s' đã cho, %s nằm ngoài khoảng. Lựa chọn kích thước giữa %s và %s.\n
49 Giá trị %s '%s' vô hiệu.\n
60 \
createdb: Tạo một cơ sở dữ liệu.\n\
usage: %s createdb [OPTION] database-name database-locale\n\
\n\
định dạng cơ sở dữ liệu là <language>.<charset>; ví dụ : en_US.iso88591, ko_KR.utf8\n\
các tùy chọn hợp lệ:\n\
      --db-volume-size=SIZE           kích cỡ của bộ đĩa thông tin đầu tiên\n\
                                      mặc định: db_volume_size trong cubrid.conf\n\
      --db-page-size=SIZE             kích cỡ trang cơ sở dữ liệu theo byte; mặc định: 16K\n\
      --log-volume-size=SIZE          kích cỡ của lưu trữ bản ghi\n\
                                      mặc định: log_volume_size trong cubrid.conf\n\
      --log-page-size=SIZE            kích cỡ trang bản ghi theo byte; mặc định: kích cỡ trang cơ sở dữ liệu\n\
      --comment=COMMENT               Chú thích cho cơ sở dữ liệu; mặc định: không có\n\
  -F, --file-path=PATH                đường dẫn thư mục cho bộ đĩa cơ sở dữ liệu; mặc định: thư mục hiện tại\n\
  -L, --log-path=PATH                 đường dẫn thư mục cho bộ đĩa bản ghi; mặc định: giống với file-path\n\
  -B, --lob-base-path=PATH            đường dẫn thư mục cơ sở cho dữ liệu LOB bên ngoài; mặc định: <file-path>/lob\n\
      --server-name=HOST              tên HOST của server; mặc định: tên máy chủ hiện tại\n\
  -r, --replace                       thay thế một cơ sở dữ liệu hiện có; mặc định: không thay thế\n\
      --more-volume-file=FILE         Tệp tin chứa các thông tin kỹ thuật để tạo ra bộ đĩa bổ sung; mặc định: không có\n\
      --user-definition-file=FILE     Tệp tin chứa các định nghĩa người dùng; mặc định: không có\n\
      --csql-initialization-file=FILE Tệp tin chứa SQLs cho một khởi tạo của cơ sở dữ liệu; mặc định: không có\n\
  -o, --output-file=FILE              chuyển hướng thông báo đầu ra đến tệp tin; mặc định: không có\n\
  -v, --verbose                       bật các thông báo trạng thái chi tiết; mặc định: tắt\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: Xóa một cơ sở dữ liệu.\n\
cách sử dụng: %s deletedb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -o, --output-file=FILE       chuyển hướng thông báo đầu ra đến FILE; mặc định: không có\n\
  -d, --delete-backup          xóa bộ đĩa sao lưu; mặc định: không xóa\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 LỖI: volext-pathname không tồn tại hoặc không thể ghi.\n
32 LỖI: vols-tofrom-paths_file không tồn tại hoặc không thể đọc.\n
60 \
renamedb: Đổi tên một cơ sở dữ liệu.\n\
cách sử dụng: %s renamedb [TÙY CHỌN] tên-cơ sở dữ liệu-nguồn tên-cơ sở dữ liệu-đích nhận\n\
\n\
các tùy chọn hợp lệ:\n\
  -E, --extended-volume-path=PATH đường dẫn thư mục cho bộ đĩa thông tin mở rộng; mặc định: đường dẫn giống với bộ đĩa cơ sở dữ liệu hoặc đường dẫn đã cho trong control-file\n\
  -i, --control-file=FILE         đổi tên bộ đĩa trên một số vị trí được ghi trong tệp tin kiểm soát; mặc định: không có \n\
  -d, --delete-backup             xóa bộ đĩa sao lưu; mặc định: không xóa\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 Một tiến trình  %s đã chạy.\n
12 MASTER SERVER đang bắt đầu. \n
13 MASTER SERVER đang tồn tại.\n
14 Lỗi tìm tên máy chủ cục bộ\n
15 Không thể tìm tệp tin tham số\n
16 Không thể khởi tạo tiến trình master\n
17 Server %s đi xuống trong %d phút
18 Server %s được thông báo tắt.\nĐiều này có thể mất vài phút. Xin vui lòng chờ.
19 Server %s không được tìm thấy.
20 Master đi xuống trong %d phút\n
21 Heartbeat failover was finished. Please wait until all shutdown processes of cub_servers are completed.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 Không thể mở tệp tin đầu ra '%s'\n
60 \
optimizedb: Tối ưu hóa một cơ sở dữ liệu.\n\
cách sử dụng: %s optimizedb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -n, --class-name=NAME        cập nhật thống kê cho lớp NAME; mặc định: tất cả các lớp\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 Dạng thức ngày tháng không hợp lệ, dạng thức hợp lệ là dd-mm-yyyy:hh:mm:ss hoặc 'backuptime'\n
20 Restoredb bị hủy hoặc một lỗi đã phát sinh.\n
60 \
restoredb: Khôi phục một cơ sở dữ liệu.\n\
cách sử dụng: %s restoredb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -d, --up-to-date=DATE             khôi phục cơ sở dữ liệu về điều kiện của nó tại DATE được đưa ra. DATE được đưa ra như sau dây: dd-mm-yyyy:hh:mm:ss hoặc từ khóa 'backuptime'\n\
      --list                        hiện một danh sách của bộ đĩa sao lưu mà không khôi phục\n\
  -B, --backup-file-path=PATH       PATH là một thư mục của bộ đĩa sao lưu cần được khôi phục\n\
  -l, --level=LEVEL                 cấp độ của sao lưu cần được khôi phục; xem cách sử dụng sao lưu; mặc định: 0(sao lưu đầy)\n\
  -p, --partial-recovery            thực hiện khôi phục từng phần nếu bất kỳ lưu trữ bản ghi nào bị thiếu\n\
  -o, --output-file=FILE            chuyển hướng thông báo đầu ra đến FILE; mặc định: không có\n\
  -u, --use-database-location-path  khôi phục cơ sở dữ liệu và bộ đĩa bản ghi đến đường dẫn được chỉ định trong tệp tin vị trí cơ sở dữ liệu\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 Thiếu tên cơ sở dữ liệu.\n
20 Tệp tin '%s' không có %d dòng.\n
21 ngắt ... \n
22 Tải bị ngắt - giao dịch hiện tại đã được cam kết\n
23 Tải bị ngắt - giao dịch hiện tại đã bị bỏ dở\n
24 Không thể mở tệp tin '%s'\n
25 \n*** Kiểm tra cú pháp ***\n\n
26 %d lỗi được phát hiện.\n
27 \n*** Chèn đối tượng ***\n\n
28 Tổng cộng %8d đối tượng được chèn.\n
29 Tổng cộng %8d đối tượng tham chiếu được chèn với nội dung mặc định.\n
30 \n*** Cam kết giao dịch ***\n
31 \n*** Đóng cơ sở dữ liệu ***\n
32 Dòng %d:
33 Lỗi phân tích '%s' như một giá trị '%s' cho thuộc tính '%s' của '%s'\n
34 Thiếu miền cho thuộc tính %s của %s\n
35 Đối với thuộc tính %s của %s, kiểu %s không được cho phép trong miền của tập hợp\n
36 Đối với thuộc tính %s của %s, kiểu mong đợi %s, lấy được một tập hợp không đổi\n
37  Đối với thuộc tính %s của %s, kiểu mong đợi %s, lấy được %s\n
38 Không có lớp có tên %s được tham chiếu bởi thuộc tính %s của %s\n
39 Không có lớp có tên %s, đang bỏ qua các tuple\n
40 Không có lớp có mã định danh %d tham chiếu bởi thuộc tính %s của %s\n
41 Lớp %s không được cho phép trong tệp tin đầu vào bộ tải\n
42 Bị dừng với lỗi.\n
43 CẢNH BÁO: thực thể hiện có đã được cập nhật !\n
44 CẢNH BÁO: định nghĩa lại thực thể %d của lớp %s\n
45 Thực thể %d của lớp %s đã được định nghĩa\n
46 Thực thể %d của lớp %s đã được bảo lưu\n
47 Vi phạm UNIQUE trên thuộc tính '%s', chuyển đổi đến NULL\n
48   %d thực thể\n
49 Lớp %s\n
50 Mật mã:
51 \n*** Cập nhật thống kê lớp ***\n
52 %s gần dòng %d (văn bản là '%s')\n
53 Lỗi Lexical
54 dòng %d: lỗi cú pháp tại \"%s\"
55 thiếu
56 trong in %s
57 Các đối số không tương thích, \"%s\", và \"%s\"\n
58 %d thực thể được cam kết\n
59 \nKhông thể hủy quyền phiên bản chưa tối ưu hóa của loaddb.\n
60 Lỗi phân tích \"%s\", như kiểu %s.\n
112   %d thực thể\r
113 Dòng cam kết cuối : %d
114 lỗi-kiểm soát-tệp tin
115 tệp tin đầu vào đến lỗi kiểm soát trong quá trình tải
120 \
loaddb: Nhập đối tượng và lược đồ đến cơ sở dữ liệu.\n\
cách sử dụng: %s loaddb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -u, --user=ID                 ID người dùng để truy cập cơ sở dữ liệu; mặc định: "public"\n\
  -p, --password=PASS           mật mã người dùng; mặc định: không có \n\
      --data-file-check-only    kiểm tra cú pháp cho tệp tin dữ liệu mà không tải; mặc định: tải sau khi kiểm tra\n\
  -l, --load-only               tải tệp tin dữ liệu mà không kiểm tra cú pháp; mặc định: tải sau khi kiểm tra\n\
      --estimated-size=NUMBER   số thực thể được ước tính; mặc định: 5000\n\
  -v, --verbose                 bật thông báo trạng thái chi tiết; mặc định: tắt\n\
  -c, --periodic-commit=COUNT   đếm chèn cho cam kết định kỳ; mặc định: không cam kết trong quá trình tải\n\
      --no-oid                  không dùng OID\n\
      --no-statistics           không cập nhật thống kê trong khi đang tải\n\
  -s, --schema-file=FILE[:LINE] tạo lược đồ từ dòng LINE với tệp tin FILE\n\
  -i, --index-file=FILE[:LINE]  tạo chỉ mục từ dòng LINE với tệp tin FILE\n\
  -d, --data-file=FILE          tải dữ liệu với tệp tin FILE\n\
  -t, --table=TABLE             tên bảng được thay thế cho tiêu đề đầu hạng bị thiếu trong file dữ liệu\n\
      --error-control-file=FILE tệp tin đến (các)lỗi kiểm soát trong quá trình tải\n\
      --ignore-class-file=FILE  tệp tin đầu vào của tên lớp mà bỏ qua tải\n
121 \
load_noopt: Tiện ích này không được chấp nhận. Cẩn thận khi sử dụng.\n\
cách sử dụng: load_noopt [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -u, --user=ID                 ID người dùng cho truy cập cơ sở dữ liệu; mặc định: "public"\n\
  -p, --password=PASS           mật mã người dùng; mặc định: không có \n\
      --data-file-check-only    kiểm tra cú pháp cho tệp tin dữ liệu mà không tải; mặc định: tải sau khi kiểm tra\n\
  -l, --load-only               tải tệp tin dữ liệu mà không kiểm tra cú pháp; mặc định: tải sau khi kiểm tra\n\
      --estimated-size=NUMBER   số thực thể được ước tính; mặc định: 5000\n\
  -v, --verbose                 bật thông báo trạng thái chi tiết; mặc định: tắt\n\
  -c, --periodic-commit=COUNT   đếm chèn cho cam kết định kỳ; mặc định: không cam kết trong quá trình tải\n\
      --no-oid                  không dùng OID\n\
  -s, --schema-file=FILE[:LINE] tạo lược đồ từ dòng LINE với tệp tin\n\
  -i, --index-file=FILE[:LINE]  tạo chỉ mục từ dòng LINE với tệp tin FILE\n\
  -d, --data-file=FILE          tải dữ liệu với tệp tin FILE\n\
      --error-control-file=FILE tệp tin đến (các) lỗi kiểm soát trong quá trình tải\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 giá trị cached-pages không hợp lệ.\n
42 giá trị cached-page-size không hợp lệ.\n
43 %d đối tượng được kết xuất.\n
44 Lớp %s
45 , %d thực thể.\n
46 Chỉ %d trong số %d đối tượng được kết xuất thành công.\n
47 thư mục đầu ra quá dài.\n
48 LSA : %d %d \n
49 Mật mã:
60 \
unloaddb: Xuất đối tượng và lược đồ từ cơ sở dữ liệu.\n\
cách sử dụng: %s unloaddb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -u, --user=ID               ID người dùng cho truy cập cơ sở dữ liệu; mặc định: "dba"\n\
  -p, --password=PASS         mật mã người dùng; mặc định: không có \n\
  -i, --input-class-file=FILE tệp tin đầu vào của tên bảng; mặc định: kết xuất tất cả các lớp\n\
      --include-reference     bao gồm các bảng đã tham chiếu; với tùy chọn -i; mặc định: tắt\n\
      --input-class-only      chỉ bao gồm lớp được chỉ định; với tùy chọn -i; mặc định: tắt\n\
      --lo-count=COUNT        tệp tin lo đếm trên một thư mục; mặc định: 0\n\
      --estimated-size=NUMBER số thực thể được ước tính; mặc định: tự động tính toán\n\
      --cached-pages=NUMBER   số trang lưu ẩn; mặc định: 100\n\
  -O, --output-path=PATH      đường dẫn của thư mục đầu ra; mặc định: thư mục đang làm việc\n\
  -s, --schema-only           chỉ kết xuất lược đồ; mặc định: lược đồ và đối tượng\n\
  -d, --data-only             chỉ kết xuất đối tượng; mặc định: lược đồ và đối tượng\n\
      --output-prefix=PREFIEX prefix cho tệp tin đầu ra; mặc định: tên cơ sở dữ liệu\n\
      --hash-file=FILE        tệp tin cho hash; mặc định: tạo bởi tmpnam()\n\
  -v, --verbose               bật thông báo trạng thái chi tiết; mặc định: tắt\n\
      --use-delimiter         dùng '"' tại nơi mã định danh bắt đầu và kết thúc; mặc định: không dùng\n\
  -S, --SA-mode               thực thi chế độ độc lập\n\
  -C, --CS-mode               thực thi chế độ client-server\n\
      --datafile-per-class    tạo một tệp tin đối tượng cho mỗi lớp; mặc định: tắt\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nQua 1\n\n
12 %d đối tượng được xử lý.\n
13 \nQua 2\n\n
14 Lớp %s\n
15 oid:  %d|%d|%d,\n
16 %d thực thể.\n
17 Đang cập nhật đối tượng.\n
18 ref_oid:  %d.%d.%d, ref_class_oid:  %d.%d.%d\n
19 Không thể chuyển đổi đối tượng\n
20 Thiếu tệp tin class heap\n
21 Không thể cập nhật đối tượng\n
22 Chỉ %d trong số %d đối tượng được xử lý thành công.\n
23 COMPACTDB đã được bắt đầu.\n
24 Số trang phải ở giữa %d và %d.\n
25 The instance lock timeout must be between %d and %d.\n
26 Tổng đối tượng: %d\n
27 Đối tượng thất bại: %d\n
28 Đối tượng sửa đổi: %d\n
29 Đối tượng lớn: %d\n
30 Đại diện lớp cũ bị xóa khỏi catalog.\n
31 Đại diện lớp cũ được bảo lưu trong catalog.\n
32 Không thể thiết lập cấp độ cô lập.\n
33 Không thể nén cơ sở dữ liệu.\n
34 The class lock timeout must be between %d and %d.\n
35 Lớp bị khóa ... Xử lý chưa hoàn thành ...\n
36 Lớp chưa được xử lý ...\n
37 Lỗi xử lý ... Xử lý chưa hoàn thành ...\n
38 Không có gì để xử lý ...\n
39 Tham số không hợp lệ ...\n
40 Tên lớp không biết ...\n
41 The unused class OIDs and pages have been reclaimed.\n
42 Reclaiming of OIDs and pages skipped.\n
43 Lỗi khi xử lý lớp, lớp đã bị bỏ qua.\n
44 \nQua 3\n\n
45 Nén heap thất bại ...\n
46 Nén heap thành công ...\n
60 \
compactdb: Không gian chưa dùng còn trống trong một cơ sở dữ liệu.\n\
cách sử dụng: %s compactdb [TÙY CHỌN] tên-cơ sở dữ liệu [class_name1 class_name2 ...]\n\
\n\
các tùy chọn hợp lệ:\n\
  -v, --verbose                     bật thông báo trạng thái chi tiết; mặc định: tắt\n\
  -i, --input-class-file=FILE       tệp tin đầu vào của tên bảng\n\
                                    danh sách class_name phải là rỗng\n\
                                    mặc định: nén tất cả các lớp\n\
                                    chỉ bật trong CS-mode\n\
  -p, --pages-commited-once=NUMBER  số trang tối đa để cam kết một lần\n\
                                    mặc định: 10\n\
                                    chỉ bật trong CS-mode\n\
  -S, --SA-mode                     thực thi chế độ độc lập\n\
  -C, --CS-mode                     thực thi chế độ client-server\n\
  -d, --delete-old-repr             xóa đại diện lớp cũ khỏi catalog; mặc định: tắt\n\
  class_name_list                   chỉ bật trong CS-mode\n\
  -I, --Instance-lock-timeout       thời gian chờ cho khóa thực thể\n\
  -c, --class-lock-timeout          thời gian chờ cho khóa lớp\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 CUBRID master đang chạy trên máy chủ %s \nđã được bắt đầu tại %s
22 đã phục vụ %d yêu cầu. %d server(s) đang chạy.\n
23 SHUTTING DOWN trong %s phút\n
24 %s
25 Tên của server để shutdown :
26 Còn bao nhiêu phút cho đến khi shutdown? :
27 \
cub_commdb: Tính năng này không được chấp nhận. Dùng lệnh 'cubrid' để thay thế.\n\
cách sử dụng: cub_commdb [TÙY CHỌN]\n\
\n\
các tùy chọn hợp lệ:\n\
  -P, --server-list                in các tiến trình server\n\
  -O, --all-list                   in tất cả các tiến trình\n\
  -S, --shutdown-server=NAME       tắt server cơ sở dữ liệu NAME\n\
  -A, --shutdown-all               tắt tất cả tiến trình\n\
  -c, --server-mode=NAME           in chế độ HA của server cơ sở dữ liệu NAME\n\
  -N, --node-list                  liệt kê các nút quản lý bởi CUBRID heartbeat\n\
  -L, --process-list               liệt kê tiến trình cục bộ quản lý bởi CUBRID heartbeat\n\
  -D, --dereg-process=PID          thoát tiến trình đích và gỡ bỏ nó từ các mục tiến trình được quản lý\n\
  -F, --reconfig-node-list         áp dụng cấu hình CUBRID heartbeat vừa được thay đổi\n\
  -U, --deactivate-heartbeat       bỏ kích hoạt tính năng CUBRID heartbeat và gỡ bỏ nút hiện tại từ nhóm nút\n\
  -i, --immediately                Immediately kill active ones, if one of HA processes are not terminated while CUBRID heartbeat is shutting down; with -U option; default: disabled\n\
  -T, --activate-heartbeat         kích hoạt tính năng CUBRID heartbeat và nút hiện tại trở thành một thành viên của nhóm nút\n\
  -V, --verbose                    bật thông báo trạng thái chi tiết; mặc định: tắt\n
28 Xin lỗi, câu lệnh không rõ: %s\n
29 Không thể lấy tên máy chủ cho máy chủ cục bộ - bỏ dở\n
30 Không thể tìm thấy tệp tin tham số\n
31 Không thể kết nối đến server chủ trên %s\n
32 Còn bao nhiêu phút cho đến khi tắt? :
33 Master sẽ tắt trong %d phút\n
34 Lỗi nhận dữ liệu từ máy khách
35 server cơ sở dữ liệu %s hiện tại đang không chạy
36 không thể thực thi shell (%s) cho %s, %s
37 xử lý fork %s, trả về %d, %s
38 không thể thực thi %s
39 -i(--immediately) option must be used with -U(--deactivate-heartbeat) option.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: Vá lỗi một cơ sở dữ liệu trong trường hợp khẩn cấp.\n\
cách sử dụng: %s emergency_patchlog [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -r, --recreate-log           tạo lại tệp tin bản ghi; sử dụng cẩn thận\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 Giá trị sai %d cho npages\n
21 Giá trị sai '%s' cho mục đích lưu trữ bộ đĩa.\n
22 CẢNH BÁO: --max-writesize-in-sec lựa chọn bị bỏ qua khi thực hiện ở chế độ độc lập.\n
60 \
addvoldb: Thêm một tệp tin bộ đĩa cho cơ sở dữ liệu.\n\
cách sử dụng: %s addvoldb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
      --db-volume-size=SIZE        kích cỡ của bộ đĩa bổ sung; mặc định: db_volume_size trong cubrid.conf\n\
      --max-writesize-in-sec=SIZE  the amount of volume written per second; (ex. 512K, 1M, 10M); default: not used; minimum: 160K\n\
  -n, --volume-name=NAME           tên của bộ đĩa thông tin; mặc định: tạo như "db"_ext1)\n\
  -F, --file-path=PATH             đường dẫn cho thêm tệp tin bộ đĩa; mặc định: thư mục đang làm việc\n\
      --comment=COMMENT            Chú thích cho thêm tệp tin bộ đĩa; mặc định: không có \n\
  -p, --purpose=PURPOSE            Mục đích cho thêm tệp tin bộ đĩa; được phép:\n\
                                           DATA - chỉ cho dữ liệu\n\
                                           INDEX - chỉ cho thư mục\n\
                                           TEMP - chỉ cho tạm thời\n\
                                           GENERIC - cho tất cả mục đích\n\
  -S, --SA-mode                    thực thi chế độ độc lập\n\
  -C, --CS-mode                    thực thi chế độ client-server\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 Một vài sự không đồng nhất được phát hiện trong cơ sở dữ liệu của bạn.\n Xin vui lòng tham khảo error_log_file %s để biết thêm thông tin.\n
21 %s: Không thể tìm thấy lớp. Nó sẽ bị bỏ qua.\n
60 \
checkdb: Kiểm tra tính đồng nhất của cơ sở dữ liệu.\n\
cách sử dụng: %s checkdb [TÙY CHỌN] tên-cơ sở dữ liệu [class_name1 class_name2 ...]\n\
\n\
các tùy chọn hợp lệ:\n\
  -S, --SA-mode                thực thi chế độ độc lập\n\
  -C, --CS-mode                thực thi chế độ client-server\n\
  -r, --repair                 sửa chữa khi tìm thấy sự không đồng nhất\n\
  -i, --input-class-file=FILE  tệp tin đầu vào của tên lớp\n\
                               mặc định: kiểm tra tất cả các lớp\n\
      --check-prev-link        check only previous links of indices\n\
      --repair-prev-link       repair previous links of indices\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 Mô tả không gian cho cơ sở dữ liệu '%s' với kích cỡ trang %s. (kích cỡ trang bản ghi: %s)\n\n
16 %5d %9s  %s %s  %s\n
17 Không thể mở tệp tin đầu vào '%s'\n
18 Mô tả không gian cho bộ đĩa tạm thời cho cơ sở dữ liệu '%s' với kích cỡ trang %s.\n\n
19 id bộ đĩa  Mục đích  tổng số trang  trang còn trống  Tên bộ đĩa\n\n
20 id bộ đĩa  Mục đích  tổng dung lượng  dung lượng còn trống  Tên bộ đĩa\n\n
21 Mô tả không gian LOB %s \n\n
22 -------------------------------------------------------------------------------\n
30 Mô tả tóm tắt không gian cho cơ sở dữ liệu '%s' với kích cỡ trang %s. (kích cỡ bản ghi: %s)\n\n
31 Mục đích    tổng số trangt  số trang đã dùng   số trang còn trống  đếm bộ đĩa\n
32 Mục đích     tổng dung lượng   dung lượng đã dùng   dung lượng còn trống  đếm bộ đĩa\n
33 %10s %s %s %s %10d\n
34 -------------------------------------------------------------\n
40 Volid  Purpose   total_pages  free_pages  data_pages index_pages  temp_pages  Vol Name\n\n
41 Volid  Purpose    total_size   free_size   data_size  index_size   temp_size  Vol Name\n\n
42 %5d %9s  %s %s %s %s %s  %s\n
43 ----------------------------------------------------------------------------------------------------\n
50 Purpose    total_pages  used_pages  free_pages  data_pages index_pages  temp_pages  volume_count\n
51 Purpose     total_size   used_size   free_size   data_size  index_size   temp_size  volume_count\n
52 %10s %s %s %s %s %s %s %10d\n
53 -------------------------------------------------------------------------------------------------\n
60 \
spacedb: Hiển thị cách sử dụng/không gian trống của cơ sở dữ liệu.\n\
cách sử dụng: %s spacedb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -o, --output-file=FILE       chuyển hướng thông báo đầu ra đến tệp tin FILE; mặc định: không có\n\
  -S, --SA-mode                thực thi chế độ độc lập\n\
  -C, --CS-mode                thực thi chế độ client-server\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               thiết lập đơn vị hiển thị (page, mega, giga, tera, human readable); mặc định: H\n\
  -s, --summarize              chỉ hiển thị một tóm tắt cho mỗi mục đích ổ\n\
  -p, --purpose                hiển thị kích thước sử dụng của từng mục(data/index/temp) của từng bộ đĩa\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 Số trang được ước tính = %d\n
60 \
estimate_data: Ước tính kích cỡ của dữ liệu.\n\
cách sử dụng: %s estimate_data number-of-instances average-instance-size number-of-attributes variable-number-of-attributes\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 Kiểu khóa chỉ mục sai được chỉ định = %s\n
16 Độ dài khóa trung bình sai được chỉ định = %d when precision = %d\n
17 Đối số sai được chỉ định\n
20 Tổng số trang chỉ mục được ước tính = %d\n (Khi chỉ mục được tạo, sai khi trường hợp đã được cư trú.)\n
21 Tổng số trang chỉ mục được ước tính = %d\n (Khi chỉ mục được tạo, trước khi trường hợp đã được cư trú (trường hợp chung))\n
22 Tổng số trang chỉ mục được ước tính = %d\n (Khi chỉ mục được tạo, trước khi trường hợp đã được cư trú (trường hợp xấu nhất))\n
23 Đối với đầu vào đã cho ...\n
24 Tổng số thực thể: %d\n
25 Số các khóa khác nhau : %d\n
26 Kích cỡ khóa trung bình         : %d\n
27 Kiểu khóa                : %s\n\n
60 \
estimate_index: Ước tính kích cỡ của một chỉ mục.\n\
cách sử dụng: %s estimate_index number-of-instances number-of-different-keys average-key-size key-type\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: Cài đặt một cơ sở dữ liệu.\n\
cách sử dụng: %s installdb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
      --server-name=HOST       tên máy chủ server; mặc định: tên máy chủ hiện tại\n\
  -F, --file-path=PATH         đường dẫn thư mục cho bộ đĩa cơ sở dữ liệu; mặc định: thư mục hiện tại\n\
  -L, --log-path=PATH          đường dẫn thư mục cho bộ đĩa bản ghi; mặc định: giống với đường dẫn tệp tin\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 OID lớp tạm thời OID\n
2 Không thể in giá trị elo\n
3 Lỗi truy cập long object cho di cư\n
4 Lỗi mở tệp tin đầu ra cho di cư long object \n
5 Lỗi đọc\n
6 Lỗi ghi\n
7 Lỗi mở tệp tin đầu vào ELO "%s"\n
8 LỚP:
9 băm tên tập tin
10 Tên:                                %s\n
11 Kích cỡ:                            %d\n
12 Kích cỡ trang:                      %d\n
13 Kích cỡ dữ liệu:                    %d\n
14 Kích cỡ mục nhập:                   %d\n
15 Mục nhập trên trang:                %d\n
16 Trang lưu ẩn:                       %d\n
17 Số mục nhập:                        %d\n
18 Số xung đột:                        %d\n
19 Hash filename:                      %s\n
20 Mục nhập tràn tiếp theo:            %d\n
21 Kiểu khóa:                          %s\n
22 Địa chỉ của đề mục trang:           %x\n
23 Địa chỉ của đề mục trang cuối cùng: %x\n
24 Địa chỉ của đề mục trang trống:     %x\n
25 Địa chỉ của page bitmap:            %x\n
26 Kích cỡ Page bitmap:                %d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: Kết xuất một cơ sở dữ liệu.\n\
cách sử dụng: %s diagdb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -d, --dump-type=TYPE         Kiểu của kết xuất\n\
                               -1 kết xuất tất cả\n\
                                1 kết xuất bảng tệp tin\n\
                                2 kết xuất dung lượng tệp tin\n\
                                3 kết xuất dung lượng heap\n\
                                4 kết xuất dung lượng chỉ mục\n\
                                5 kết xuất classnames\n\
                                6 kết xuất disk bitmaps\n\
                                7 kết xuất catalog\n\
                                8 kết xuất bản ghi\n\
                                9 kết xuất heap\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 Không thể mở tệp tin đầu ra '%s'\n
59 lockdb không thể chạy như chế độ độc lập.\n
60 \
lockdb: Hiển thị trạng thái khóa của cơ sở dữ liệu.\n\
cách sử dụng: %s lockdb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -o, --output-file=FILE       chuyển hướng tệp tin đầu ra đến tệp tin FILE; mặc định: không có\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 Chỉ một đối số phải được đưa ra trong các: tran-index, user, host, progname\n
21 Nhập mật mã DBA:
26 Không có giao dịch nào với giá trị đối số đã chỉ định.\n
27 Sẵn sàng xóa bỏ các giao dịch sau đây:\n\n
28 Bạn có muốn tiếp tuc không ? (Y/N)
29 Xóa bỏ giao dịch kết nối với chỉ mục giao dịch %d\n
30 Thất bại xóa bỏ các giao dịch sau đây\n\n
31 Giao dịch chưa được hoàn thành, nhưng nó có thể thực hiện việc quay lui lại.\n Xin vui lòng chờ một lát và thử lại sau đó nếu cần.\n
32 Vô hiệu tranindex(%s).\n
59 killtran không thể chạy như chế độ độc lập.\n
60 \
killtran: Quản lý giao dịch của cơ sở dữ liệu.\n\
cách sử dụng: %s killtran [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -i, --kill-transaction-index=INDEX xóa bỏ giao dịch với chỉ mục INDEX giao dịch\n\
      --kill-user-name=ID            xóa bỏ tất cả giao dịch với ID người dùng\n\
      --kill-host-name=HOST          xóa bỏ tất cả giao dịch với client HOST\n\
      --kill-program-name=NAME       xóa bỏ tất cả giao dịch với tên chương trình máy khách NAME\n\
      --kill-sql-id=SQL_ID           hủy bỏ tất cả giao dịch với SQL_ID\n\
  -p, --dba-password=PASS            mật mã cho người dùng DBA; sẽ nhắc nếu không chỉ định\n\
  -d, --display-information          hiển thị thông tin về các giao dịch đang hoạt động\n\
  -q, --query-exec-info              hiển thị thông tin về thông tin thực hiện câu hỏi của các giao dịch hiện hoạt\n\
  -f, --force                        xóa bỏ giao dịch mà không có nhắc nhở kiểm chứng\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 tên-cơ sở dữ liệu
2 đối số được đòi hỏi, mặc định: không có
3 kiểm tra khả năng tương thích
4 chỉ kiểm tra khả năng tương thích
5 đích nhận-sao lưu
6 một đường dẫn thư mục hoặc device cho đích nhận của bộ đĩa sao lưu
20 cơ sở dữ liệu '%s' không tồn tại.
21 '%s' không thể được di cư đến %s, bởi vì phiên bản DB của nó quá thấp.\n
22 '%s' không cần thiết phải di cư.\n
23 '%s' cần được di cư.\n
24 Sao lưu được được khuyến nghị sau khi di cư. Bạn có muốn sao lưu cơ sở dữ liệu của bạn bây giờ không? (y/n):
25 Chúng tôi khuyến nghị sao lưu cơ sở dữ liệu mới của bạn với lệnh backupdb sau khi di cư.
26 \n*** CẢNH BÁO: Sao lưu cơ sở dữ liệu của bạn trước khi bạn bắt đầu di cư đến %s ***\n\nĐể khôi phục các lỗi có thể xảy ra trong quá trình di cư, chúng tôi khuyến nghị sao lưu cơ sở dữ liệu của bạn.\nBạn đã sao lưu cơ sở dữ liệu của bạn chưa? (y/n):
27 \n1. Chạy backupdb \n2. Nhập đường dẫn sao lưu mới cho cơ sở dữ liệu mới.(đường dẫn hiện tại: %s)\n3. Thoát \n
28 \nĐường dẫn sao lưu mới cho cơ sở dữ liệu:
29 \nDi cư và sao lưu đã được hoàn tất thành công.
30 Sao lưu thất bại. Xin vui lòng thử lại sao lưu cơ sở dữ liệu của bạn.
31 \n\n*** CẢNH BÁO : Một khi di cư hoàn tất, bạn không thể khôi phục lại cơ sở dữ liệu với bộ đĩa sao lưu trước đây.\nLưu ý rằng phiên bản trước đó của tệp tin sao lưu có thể được khôi phục lại chỉ trên phiên bản tương ứng của restotedb.\nXin vui lòng kiểm tra bộ đĩa sao lưu trước đây được giữ an toàn.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: Sửa đổi một máy chủ của cơ sở dữ liệu.\n\
cách sử dụng: %s alterdbhost [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -h, --host=HOST              tên máy chủ mới; mặc định: tên máy chủ hiện tại\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 tên-cơ sở dữ liệu
2 đối số được đòi hỏi, mặc định: không có
3 java-class-file
4 đối số được đòi hỏi, mặc định: không có
5 ép ghi đè lên
6 ghi đè tệp tin lớp nếu tồn tại, mặc định: không

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 Không thể mở tệp tin đầu ra '%s'\n
59 plandump không thể chạy như chế độ độc lập.\n
60 \
plandump: Hiển thị hoặc kiểm soát kế hoạch truy vấn được lưu ẩn.\n\
cách sử dụng: %s plandump [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -d, --drop                   xóa tất cả kế hoạch trong bộ lưu ẩn của server\n\
  -o, --output-file=FILE       chuyển hướng thông báo đầu ra đến tệp tin FILE; mặc định: không có \n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 Không thể mở tệp tin đầu ra '%s'\n
21 Tham số hệ thống cần được sử dụng bởi chương trình máy khách.\n
22 Tham số hệ thống được tải bởi server '%s'\n
23 Tham số hệ thống cần được sử dụng trong chế độ độc lập.\n
60 \
paramdump: Kết xuất cài đặt tham số hệ thống.\n\
cách sử dụng: %s paramdump [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -o, --output-file=FILE       chuyển hướng thông báo đầu ra đến tệp tin FILE; mặc định: không có \n\
  -b, --both                   kết xuất cả tham sô server và máy khách\n\
  -S, --SA-mode                thực thi chế độ độc lập\n\
  -C, --CS-mode                thực thi chế độ client-server\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 Giá trị sai \"%s\" cho chế độ HA đang chạy của server.\n
12 Không thể thay đổi chế đô HA đang chạy của server.\n
21 Nhập mật mã DBA:
22 Chế độ HA hiện tại đang chạy của server '%s'' là %s.\n
23 Chế độ HA đang chạy của '%s'' được thay đổi thành \"%s\".\n
24 Máy chủ này không cấu hình cho HA.\n
58 Tính năng HA không được hỗ trợ trong nền tảng này.\n
59 changemode không thể chạy như chế độ độc lập.\n
60 \
changemode: Hiển thi hoặc thay đổi chế độ HA đang chạy của server.\n\
cách sử dụng: %s changemode [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -m, --mode=MODE             thay đổi chế độ server thành MODE; mặc định: không có \n\
  -f, --force                 bắt buộc thay đổi\n\
  -t, --timeout=S             chờ S giây trong khi giao dịch đang hoạt động được kết thúc bình thường\n\
                              tùy chọn -t chỉ có hiệu lực khi người dùng thay đổi chế độ server thành bảo trì\n\
                              mặc định: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 Giá trị sai \"%s\" cho chế độ đang chạy của copylogdb.\n
21 Nhập mật mã DBA:
22 Máy chủ này không cấu hình cho HA.\n
58 Tính năng HA không được hỗ trợ trong nền tảng này.\n
59 copylogdb không thể chạy như chế độ độc lập.\n
60 \
copylogdb: Sao chép trang bản ghi từ server.\n\
cách sử dụng: %s copylogdb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -L, --log-path=PATH         đường dẫn thư mục cho bộ đĩa bản ghi được sao chép\n\
  -m, --mode=MODE             chế độ thao tác; mặc định: sync\n\
                              MODE: sync hoặc semisync hoặc async\n

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 Nhập mật mã DBA:
22 Máy chủ này không cấu hình cho HA.\n
58 Tính năng HA không được hỗ trợ trong nền tảng này.\n
59 applylogdb không thể chạy như chế độ độc lập.\n
60 \
applylogdb: Áp dụng bản ghi đã sao chép đến server.\n\
cách sử dụng: %s applylogdb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -L, --log-path=PATH         đường dẫn thư mục cho bộ đĩa bản ghi được sao chép\n\
      --max-mem-size=SIZE     kích cỡ tiến trình lớn nhất theo MB; tối đa 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 Không thể mở tệp tin đầu ra '%s'\n
59 statdump không thể chạy như chế độ độc lập.\n
60 \
statdump: Kết xuất thống kê toàn cục.\n\
cách sử dụng: %s statdump [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -o, --output-file=FILE       chuyển hướng thông báo đầu ra đến tệp tin FILE; mặc định: không có \n\
  -c, --cumulative             in thống kê tích lũy\n\
  -i, --interval=S             in các thông tin  S giây một lần\n\
  -s, --substr=STRING          in mục thống kê chỉ khi nó khớp với chuỗi con\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 Nhập mật mã DBA:
22 Máy chủ này không cấu hình cho HA.\n
58 Tính năng HA không được hỗ trợ trong nền tảng này.\n
59 applyinfo không thể chạy như chế độ độc lập.\n
60 \
applyinfo: hiển thị thông tin CUBRID HA Apply.\n\
cách sử dụng: %s applyinfo [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -r, --remote-host-name      tên máy chủ từ xa, thông tin đăng nhập hoạt động hiển thị từ xa của nút \n\
  -a, --applied-info 	      hiển thị thông tin áp dụng \n\
  -L, --copied-log-path=PATH  đường dẫn của ổ ghi sao chép, hiển thị đăng nhập sao chép thông tin \n\
  -p, --pageid=ID             page id; mặc định : 0(active page)\n\
  -v, --verbose               kích hoạt các thông báo trạng thái tiết; mặc định : vô hiệu hóa\n\
  -i, --interval=S            print information every S secs\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb không thể chạy như chế độ độc lập.\n
60 \
acldb: Hiển thị hoặc tải lại cài đặt kiểm soát truy cập của cơ sở dữ liệu.\n\
cách sử dụng: %s acldb [TÙY CHỌN] tên-cơ sở dữ liệu\n\
\n\
các tùy chọn hợp lệ:\n\
  -r, --reload       tải lại tệp tin kiểm soát ip\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: Khởi tạo thư viện chia sẻ cục bộ sử dụng (các) file đầu vào LDML.\n\
sử dụng: %s genlocale [tùy chọn] [ngôn ngư - chuỗi]\n\
\n\
ngôn ngư - chuỗi      chuỗi ngôn ngữ cục bộ dùng để khởi tạo thư viện chia sẻ.\n\
		      Nếu không cho language-sring, tất cả ngôn ngữ trong 'cubrid_locales.txt'\n\
		      sẽ được xử lý.\n\
các tùy chọn khả dụng:\n\
  -v, --verbose	              cho phép các thông điệp trạng thái dài dòng; mặc định : tắt\n\
  -i, --input-ldml-file       đường dẫn cho file đầu vòa LDML. Chỉ có khi cho language-string.\n
61 Mệnh đề tên cục bộ vô hiệu.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 Không thể đồng thời sử dụng -i/--input-file vào và bộ nhận diện cục bộ.\n
58 Khoảng điểm mã vô hiệu (-s -e).\n
59 Mệnh đề tên cục bộ vô hiệu.\n
60 \
dumplocale: Xóa bỏ các nội dung của thư viện chia sẻ cục bộ được khởi tạo trước đó bằng các file đầu vào LDML.\n\
sử dụng: %s dumplocale [tùy chọn] [ngôn ngư - chuỗi]\n\
\n\
ngôn ngư - chuỗi	chuỗi ngôn ngữ cục bộ để xóa bỏ thư viện chia sẻ cục bộ.\n\
		         Nếu không cho language-sring, tất cả ngôn ngữ trong 'cubrid_locales.txt'\n\
		         sẽ được xử lý.\n\
các tùy chọn khả dụng:\n\
  -i, --input-file    đường dẫn cho thư viện chia sẻ cục bộ đầu vào (đã được khởi tạo).\n\
                      Chỉ có thể sử dụng khi không cho chuỗi ký tự.\n\
  -d, --calendar      xóa bỏ thông tin lịch và ngày/thời gian; mặc định : tắt\n\
  -n, --numeric	      xóa bỏ thông tin số; mặc định : tắt \n\
  -a, --alphabet=l|lower|u|upper|both	     xóa bỏ thông tin số chữ cái và in hoa; mặc định : tắt \n\
      --identifier-alphabet=l|lower|u|upper	 xóa bỏ thông tin số chữ cái và in hoa cho bộ nhận diện; mặc định : tắt\n\
  -c, --codepoint-order xóa bỏ thông tin đối chiếu được sắp xếp dựa trên các giá trị điểm mã\n\
                         (cp, char, trọng lượng, tiếp theo -cp, char và trọng lượng);\n\
                         Mặc định : tắt\n\
  -w, --weight-order - để  xóa bỏ thông tin đối chiếu được sắp xếp dựa trên các giá trị khối lượng\n\
                      (trọng lượng, cp, char); Mặc định : tắt\n\
  -s, --start-value	  tùy chọn lọc; bắt đầu điểm mã cho\n\
                      -a, -- định danh - bảng chữ cái, -c, -w; mặc định : 0\n\
  -e, --end-value     tùy chọn lọc; kết thúc điểm mã cho \n\
                      -a, -- định danh - bảng chữ cái, -c, -w; \n\
                      Mặc định : tối đa giá trị đọc từ miền địa phương chia sẻ thư viện \n\
  -k, --console-conversion  xóa bỏ dữ liệu chuyển đổi; mặc định : tắt\n\
  -z, --normalization   xóa bỏ dữ liệu chuẩn tắc; mặc định : tắt\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 Phân vùng phải được loại bỏ trước khi thay đổi đối chiếu '%s' hiện tại của thuộc tính phân vùng của chúng.\nClass name\n
44 Các phím ngoại lai trên thuộc tính có đối chiếu '%s' phải dừng lại trước khi thay đổi đối chiếu thuộc tính.\nTable | Foreign Key\n
45 Các cấp có thuộc tính '%s'; chúng nên được định nghĩa lại :\nClass name\n
46 Các chỉ số chức năng có biểu thức chứa đối chiếu '%s'; chúng nên được định nghĩa lại :\n\
Table | Index | Function expression\n
47 Đồng bộ hóa các đối chiếu chưa được thực hiện. Cơ sở dữ liệu không bị thay đổi.\n
48 Nội dung bảng hệ thống '%s' được cập nhật bằng các đối chiếu hệ thống mới.\n
49 Cập nhật đối chiếu hệ thống có thể gây gián đoạn cơ sở dữ liệu. Tiếp tục (y/n) ?\n
50 Đối chiếu '%s' (Id: %d) không tìm thấy trong cơ sở dữ liệu hoặc được thay đổi trong cài đặt hệ thống mới.\n
51 Các kích hoạt có điều kiện chứa đối chiếu '%s'; chúng nên được định nghĩa lại :\nTrigger | Condition\n
52 Trình xem câu hỏi chứa đối chiếu '%s'; chúng nên được định nghĩa lại :\nView | Query\n
53 Các thuộc tính chứa đối chiếu '%s'; chúng nên được định nghĩa lại.\nTable | Attribute Domain\n
54 Yêu cầu can thiệp thủ công vào cơ sở dữ liệu.\n\
Hãy kiểm tra file '%s' và chạy file trước khi ép đối chiếu vào cơ sở dữ liệu.\n
55 Không cần đồng bộ.\n
56 Cần đồng bộ phân loại hệ thống vào cơ sở dữ liệu.\n\
Chạy 'cubrid synccolldb -f %s'\n
57 Có các đối chiếu mới %d trong cài đặt hệ thống so với cơ sở dữ liệu:\n
58 Tất cả đối chiếu trong cơ sở dữ liệu '%s' được tìm thấy trong cài đặt hệ thống mới.\n
59 Có đối chiếu %d trong cơ sở dữ liệu không được cài đặt hoặc bị thay đổi so với các đối chiếu hệ thống.\n
60 \
synccolldb: Đồng bộ các đối chiếu từ cơ sở dữ liệu với các đối chiếu của hệ thống.\n\
usage: %s synccolldb [OPTION] database-name\n\
\n\
lựa chọn có giá trị:\n\
  -c, --check-only    Kiểm tra các đối chiếu từ cơ sở dữ liệu với các đối chiếu của hệ thống.\n\
		      Nếu cần đồng bộ hóa các đối chiếu, mã lỗi 1 được trả lại.\n\
		      Hiển thị báo cáo các đối chiếu không tương thích.\n\
  -f, --force-only    Cập nhật các đối chiếu trong cơ sở dữ liệu giống như các đối chiếu của cài đặt hệ thông mới.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 Nhập mật khẩu %s:  
22 Chỉ mục giao dịch      Tên người dùng      Tên máy chủ      Id tiến trình      Tên chương trình\n
23 -------------------------------------------------------------------------------\n
24 %4d%-10s %14s %14s %15d %17s\n
25 Không có giao dịch\n
26 tranlist chỉ có thể được thực hiện bởi DBA hoặc một thành viên nhóm DBA.\n
27 sort-key column number(%d) is invalid.\n
32 Tran index         Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %4d%-10s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index          User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %4d%-10s %14s %14s %14d    %-25s %10.2f %12.2f    %28s     %13s   %s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %d
46 SQL_ID: %s\n
59 tranlist không thể chạy như chế độ độc lập.\n
60 \
tranlist: Đổ các thông tin giao dịch hiện hoạt.\n\
usage: %s tranlist [OPTION] database-name\n\
\n\
lựa chọn có giá trị:\n\
  -u, --user=ID                ID người dùng để truy cập cơ sở dữ liệu; default: DBA user\n\
  -p, --password=PASS          mật khẩu người dùng; default: none\n\
  -s, --summary                in các thông tin tổng hợp về giao dịch\n\
      --sort-key=COLUMN_NUMBER số cột được sắp xếp\n\
      --reverse                đảo ngược thứ hạng kết quả\n
