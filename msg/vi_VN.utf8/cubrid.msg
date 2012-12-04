
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
1 \n%s\n\n
2 Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
3 Thông tin bản quyền\n
5 Giá trị đối số `%s' không phải số nguyên
6 Giá trị đối số `%s' không phải floating
7 Thiếu đối số `%s'
8 Đối số `%s' trùng lặp
9 Thiếu giá trị cho đối số `%s'
10 Đối số `%s' không biết
11 Đối số vị trí `%s' không mong đợi
12 Không có định nghĩa đối số nào được đưa ra
13 Không thể cấp phát kho lưu trữ cho phân tích cú pháp đối số
14 Chuỗi đối số vượt quá độ dài tối đa: %s
15 Số thông báo không hợp lệ %d trong catalog đối với phân tích cú pháp đối số
16 [tùy chọn]
17 Tùy chọn:

$set 4 MSGCAT_SET_PARAMETER
2 %s không phải là một thư mục.  Đang tạo %s trong ".".
3 Không thể mở %s.  tệp tin cấu hình chưa được tạo.
4 Không thể ghi dòng %d đến tệp tin cấu hình %s.
6 Không thể truy cập tệp tin cấu hình hệ thống "%s": %s
7 HOME chưa được thiết lập
8 Tham số được yêu cầu "%s" không có giá trị.
9 Không thể mở tệp tin cấu hình "%s": %s
10 Tham số '%s' tại dòng %d trong tệp tin '%s' : 
11 Biến số môi trường %s: 
12 Từ khóa không nhận dạng được.
13 Kiểu giá trị không khớp với kiểu tham số.
14 Không thể cấp phát bộ nhớ cho giá trị kho lưu trữ.
15 Giá trị chuỗi chưa kết thúc.
16 Giá trị nằm ngoài phạm vi.
17 Lỗi UNIX : %s
18 Không có sẵn thông báo nào.
19 Giá trị nằm ngoài phạm vi. Thiết lập lại về giá trị mặc định.
20 Từ khóa = '%s'    Giá trị = %d    Giá trị mặc định = %d\n
21 Từ khóa = '%s'    Giá trị = %f    Giá trị mặc định = %f\n
22 Tham số không được chấp thuận
23 Tham số '%s' và '%s' không thể được dùng cùng nhau.\n
31 The '%s' parameter:

$set 5 MSGCAT_SET_ERROR
1 Thiếu thông báo cho mã lỗi %1$d.
2 Lỗi hệ thống nội tại: không có thêm thông tin cụ thể nào.
3 Không đủ bộ nhớ ảo: không thể cấp phát %1$lld byte của bộ nhớ.
4 Đã bị ngắt.
5 Key = "%1$s" không được tìm thấy trong bảng hash "%2$s".
6 Một bảng hash bộ nhớ NULL được cung cấp đến hàm bảng hash.
7 Cố gắng để định dạng dung lượng đĩa "%1$s" với một giá trị %2$d không chính xác cho số trang.
8 Không thể định dạng dung lượng đĩa "%1$s" với %2$d trang (%3$lld bytes).
9 Không thể định dạng dung lượng đĩa "%1$s" với %2$d trang (%3$lld Kbytes) bởi vì không đủ không gian. Không gian có thể dùng hiện tại là %4$d trang (%5$lld Kbytes).
10 Không thể gắn kết dung lượng đĩa "%1$s".
11 Không thể gắn kết dung lượng đĩa "%1$s". Cơ sở dữ liệu "%2$s", mà dung lượng đĩa thuộc về, đang được sử dụng bởi người dùng %3$s trong tiến trình %4$d của máy chủ %5$s từ %6$s.
12 Vấn đề khi gỡ gắn kết dung lượng "%1$s".
13 Một lỗi I/O phát sinh khi đọc trang %1$d của bộ đĩa "%2$s".
14 Một lỗi I/O phát sinh khi ghi trang %1$d của bộ đĩa "%2$s".
15 Không thể ghi trang %1$d của bộ đĩa "%2$s" bởi vì không đủ không gian trong device hệ thống thao tác.
16 Không thể đổi tên ổ đĩa "%1$s" thành "%2$s".
17 Lỗi nội bộ: lấy pageid %1$d đã thôi cấp phát của ổ "%2$s".
18 Tất cả các bộ đệm trang đã được cố định (LRU: %1$d).
19 Lỗi nội bộ: pageptr = %1$p của trang %2$d của bộ đĩa "%3$s" chưa được cố định.
20 Lỗi nội bộ: pageptr = %1$p không biết.
21 Lỗi nội bộ: sector %1$d không biết của bộ đĩa "%2$s" không biết.
22 Lỗi nội bộ: trang %1$d không biết của bộ đĩa "%2$s".
23 Lỗi nội bộ: cố gắng thôi cấp phát trang hệ thống đĩa %1$d của bộ đĩa "%2$s".
24 Gần như thiếu không gian trong bộ đĩa "%1$s". Tổng số trang = %2$d, tổng số trang còn trống = %3$d.
25 Gần như thiếu không gian trong các bộ đĩa với dữ liệu là mục đích lưu trữ chính. Tổng số trang = %1$d, tổng số trang còn trống = %2$d.
26 Gần như thiếu không gian trong các bộ đĩa với chỉ mục là mục đích lưu trữ chính. Tổng số trang = %1$d, tổng số trang trống = %2$d.
27 Gần như thiếu không gian trong các bộ đĩa với mục đích lưu trữ generic.Tổng số trang = %1$d, tổng số trang trống = %2$d.
28 Gần như thiếu không gian trong các bộ đĩa với dữ liệu tạm thời là mục đích lưu trữ chính. Tổng số trang = %1$d, tổng số trang trống = %2$d.
29 Thông báo thiếu không gian cuối cùng cho bộ đĩa "%1$s". Tổng số trang = %2$d, tổng số trang trống = %3$d.
30 Thông báo thiếu không gian cuối cùng cho bộ đĩa với dữ liệu là mục đích lưu trữ chính. Tổng số trang = %1$d, tổng số trang trống = %2$d.
31 Thông báo thiếu không gian cuối cùng cho bộ đĩa với chỉ mục là mục đích lưu trữ chính. Tổng số trang = %1$d, tổng số trang trống = %2$d.
32 Thông báo thiếu không gian cuối cùng cho bộ đĩa với mục đích lưu trữ generic.Tổng số trang = %1$d, tổng số trang trống = %2$d.
33 Thông báo thiếu không gian cuối cùng cho bộ đĩa với dữ liệu tạm thời là mục đích lưu trữ chính. Tổng số trang = %1$d, tổng số trang trống = %2$d.
34 Lỗi nội bộ: %1$d nthpage nằm ngoài phạm vi của tệp tin %2$d trong bộ đĩa "%3$s". Số trang hiện tại là %4$d.
35 Lỗi nội bộ: mã định danh bộ đĩa %1$d không biết.
36 Thiếu không gian đĩa trong cơ sở dữ liệu. Số trang đòi hỏi = %1$d.
37 Thiếu không gian đĩa trong bộ đĩa "%1$s". Số trang đòi hỏi = %2$d.
38 Lỗi nội bộ: Tệp tin VFID %1$d|%2$d không biết.
39 Lỗi nội bộ: Trang %1$d|%2$d(bộ đĩa "%3$s") của tệp tin VFID %4$d|%5$d(bộ đĩa "%6$s") là không hợp lệ/ đã thôi cấp phát dựa theo bản đồ phân phát.
40 Lỗi nội bộ: %1$d trang được đòi hỏi, %2$d trang được tìm thấy cho tệp tin VFID %3$d|%4$d(bộ đĩa "%5$s").
41 Lỗi nội bộ: Trang %1$d|%2$d(bộ đĩa "%3$s") không phải một phần của tệp tin VFID %4$d|%5$d(bộ đĩa "%6$s").
42 Không thể sắp xếp lại bản ghi chép đã neo trên trang %1$d của bộ đĩa "%2$s".
43 Lỗi nội bộ: Trang %1$d của bộ đĩa "%2$s" có thể bị hỏng. %3$d bản ghi chép đã được tìm thấy khi %4$d được đòi hỏi.
44 Không có không gian nào có sẵn trong trang đã được cắt xẻ %1$d của bộ đĩa "%2$s".
45 Khe %1$d trên trang %2$d của bộ đĩa "%3$s" được cấp phát đến một bản ghi chép đã được neo. Một bản ghi chép mới không thể được chèn tại đây.
46 Lỗi nội bộ: khe %1$d trên trang %2$d của bộ đĩa "%3$s" chưa được cấp phát.
47 Không thể tạo một tệp tin heap trong bộ đĩa "%1$s".
48 Truy cập đối tượng đã bị xóa %1$d|%2$d|%3$d.
49 Lỗi nội bộ: lớp của đối tượng %1$d|%2$d|%3$d là không biết.
50 Lỗi nội bộ: bản ghi chép tái định vị của đối tượng %1$d|%2$d|%3$d có thể bị hỏng.
51 Lỗi nội bộ: đối tượng %1$d|%2$d|%3$d có thể bị hỏng.
52 Lỗi nội bộ: địa chỉ đối tượng tràn %1$d|%2$d|%3$d có thể bị hỏng.
53 Lấy đối tượng %1$d|%2$d|%3$d khi chỉ có OID của nó được gán.
54 Lỗi nội bộ: cố gắng thao tác một đối tượng của trang %1$d dài. Kích cỡ tối đa cho phép cho một đối tượng là %2$d trang.
55 Lỗi nội bộ: Một tham chiếu vòng tròn trang nhớ được phát hiện trên trang %1$d|%2$d của tệp tin heap %3$d|%4$d|%5$d.
56 Lỗi nội bộ: extendible hashing file (Volid: %1$d Fileid: %2$d) page (Volid: %3$d Pageid: %4$d) không rõ đã được chỉ định.
57 Key không tồn tại trong extendible hashing structure.
58 Chuỗi "%1$s" quá dài để vừa với một trang đã cắt xẻ.
59 Lỗi nội bộ: kiểu key được chỉ định %1$d không hợp lệ cho extendible hashing structure.
60 Lỗi nội bộ: extendible hashing structure đã bị hỏng.
61 Lỗi nội bộ: trang gốc thư mục của extendible hashing structure (Volid: %1$d Fileid: %2$d Pageid: %3$d) đã bị hỏng.
62 Lỗi nội bộ: sắp xếp bản ghi chép %1$d (kích cỡ %2$d) không vừa trong một trang; kích cỡ tối đa cho phép là %3$d.
63 Lỗi nội bộ: một trang tạm thời đã bị hỏng trong quá trình sắp xếp.
64 Lớp "%1$s" không biết .
65 Lớp "%1$s" đã tồn tại.
66 Lỗi nội bộ: thao tác bắt buộc %1$d không biết cho đối tượng %2$d|%3$d|%4$d.
67 Lỗi nội bộ: một tệp tin heap chưa được cấp phát đến đối tượng kho lưu trữ %1$d|%2$d|%3$d.
68 Lỗi nội bộ: tên lớp khác cho lớp với oid = %1$d|%2$d|%3$d đã được tìm thấy. Các tên lớp được tìm thấy là "%4$s", "%5$s" đang sử dụng classname hash table và heap, theo thứ tự.
69 Lỗi nội bộ: Các mã định danh đối tượng lớp khác được tìm thấy cho lớp với tên "%1$s". Các OID được tìm thấy là %2$d|%3$d|%4$d, %5$d|%6$d|%7$d đang sử dụng classname hash table và heap, respectively.
70 Lỗi nội bộ: Lớp với tên "%1$s" và oid = %2$d|%3$d|%4$d không tồn tại trong classname hash table.
71 Lỗi nội bộ: Lớp với tên "%1$s" và oid = %2$d|%3$d|%4$d không tồn tại trong heap của nó.
72 Giao dịch của bạn  (chỉ mục %1$d, %2$s@%3$s|%4$d) đã bị đơn phương bỏ dở bởi hệ thống.
73 Giao dịch của bạn  (chỉ mục %1$d, %2$s@%3$s|%4$d) tạm ngưng chờ trên khóa %5$s trên đối tượng %6$d|%7$d|%8$d. Bạn đang chờ người dùng %9$s để kết thúc.
74 Giao dịch của bạn  (chỉ mục %1$d, %2$s@%3$s|%4$d) tạm ngưng chờ trên khóa %5$s trên lớp %6$s. Bạn đang chờ người dùng %7$s để kết thúc.
75 Giao dịch của bạn  (chỉ mục %1$d, %2$s@%3$s|%4$d) tạm ngưng chờ trên khóa %5$s trên thực thể %6$d|%7$d|%8$d of class %9$s. Bạn đang chờ người dùng %10$s để kết thúc.
76 Giao dịch của bạn  (chỉ mục %1$d, %2$s@%3$s|%4$d) tạm ngưng chờ trên khóa %5$s trên trang %6$d|%7$d. Bạn đang chờ người dùng %8$s để giải phóng khóa trang.
77 Lỗi nội bộ: tất cả bộ đệm bị cố định.
78 Lỗi nội bộ: một lỗi I/O phát sinh khi đọc trang bản ghi logic %1$lld (trang vật lý %2$lld) của "%3$s".
79 Lỗi nội bộ: một lỗi I/O phát sinh khi ghi trang bản ghi logic %1$lld (trang vật lý %2$lld) của "%3$s".
80 Không đủ không gian trong thiết bị hệ điều hành khi ghi trang bản ghi logic %1$lld (trang vật lý %2$lld) của "%3$s". Không thể ghi nhiều hơn %4$d bytes.
81 Lỗi nội bộ: trang bản ghi logic %1$lld có thể bị hỏng.
82 Không thể lắp đặt dung lượng đĩa bản ghi/tệp tin "%1$s".
83 Độ dài của đường dẫn "%1$s" cộng với độ dài tên bản ghi prefix "%2$s" là quá dài; chiều dài kết hợp phải nhỏ hơn %3$d.
84 Độ dài của tên bản ghi prefix "%1$s" là quá dài; chiều dài phải nhỏ hơn %2$d.
85 Tên prefix "%1$s" không giống với "%2$s" trong đĩa bản ghi. Bản ghi này có thể đã bị đổi tên ở bên ngoài miền cơ sở dữ liệu.
86 Cơ sở dữ liệu không tương thích với phát hành "%1$s" hiện tại "%2$s".
87 Có các hành động khôi phục cần phải phục hồi bằng cách sử dụng "%1$s" phát hành "%2$s" thay vì phát hành "%3$s". Sau khi khôi phục, cơ sở dữ liệu có thể được chạy trên phát hành "%4$s".
88 Lỗi nội bộ: Chuỗi phát hành "%1$s" không thể vượt quá %2$d. Phải thay đổi bản ghi đề mục.
89 Bản chi "%1$s" không thuộc về cơ sở dữ liệu đã cho.
90 Redo logging luôn luôn là một thao tác logging cấp độ trang. Một con trỏ trang dữ liệu phải được đưa ra như một phần của địa chỉ.
91 Postpone logging luôn luôn là một thao tác logging cấp độ trang. Một con trỏ trang dữ liệu phải được đưa ra như một phần của địa chỉ.
92 Compensate logging luôn luôn là một thao tác logging cấp độ trang.  Một con trỏ trang dữ liệu phải được đưa ra như một phần của địa chỉ.
93 Cảnh báo: Redo logging bị bỏ qua trong quá trình khôi phục làm lại và quay lui bình thường.
94 Cảnh báo: Postpone logging bị bỏ qua trong quá trình khôi phục làm lại và quay lui bình thường.
95 Cảnh báo: undo logging bị bỏ qua trong quá trình khôi phục và quay lui bình thường.
96 Khôi phục phương tiện có thể cần thiết trong bộ đĩa "%1$s".
97 Lỗi nội bộ: không thể tìm thấy trang bản ghi %1$lld trong lưu trữ bản ghi.
98 Không thể tạo bản ghi lưu trữ "%1$s" để lưu trữ các trang từ %2$lld đến  %3$lld.
99 Không thể tạo tệp tin thông tin thư mục sao lưu "%1$s".
100 Không thể sao lưu cơ sở dữ liệu "%1$s".
101 Tệp tin người dùng "%1$s" không biết.
102 Tệp tin "%1$s" không có đủ các mục:đòi hỏi %2$d mục.
103 Tệp tin "%1$s" dường như có các mục chưa theo thứ tự. Mục %2$d với giá trị: %3$d %4$s %5$s, được tìm thấy khi mục với giá trị %6$d %7$s được mong đợi.
104 Tệp tin "%1$s" dường như có một mục không chính xác cho tên cơ sở dữ liệu (bộ đĩa chính). Mục %2$d với giá trị: %3$d %4$s %5$s, được tìm thấy khi mục với giá trị %6$d %7$s %8$s được mong đợi.
105 Không thể truy cập tệp tin sao lưu "%1$s". Khôi phục hoặc Sao lưu bị hủy.
106 Không thể chuẩn bị để cam kết giao dịch hiện tại với mã định danh giao dịch toàn cục %1$d bởi vì mã định danh giao dịch đó đang được dùng bởi giao dịch.
107 Không có giao dịch phân phối toàn cục nào liên kết với mã định danh giao dịch %1$d.
108 Giao dịch hiện tại %1$d phải được cam kết hoặc bỏ dở trước khi tìm cách tham gia vào giao dịch phân phối có mã định danh toàn cục %2$d.
109 Giao dịch của bạn  %1$d đã bị đơn phương bỏ dở bởi hệ thống bởi vì một site tham gia (từ xa) đi xuống hoặc đơn phương bỏ dở giao dịch.
110 Giá trị cấp độ cô lập phải nằm giữa %1$d và %2$d.
111 Giao dịch của bạn đã bị bỏ dở bởi hệ thống bởi vì server lỗi hoặc chế độ thay đổi.
112 Máy chủ "%1$s" chưa được cho phép để chạy cơ sở dữ liệu.
113 Không thể khởi động lại/khởi tạo server cơ sở dữ liệu. %1$s
114 Không thể diễn giải "%1$s" như một cơ sở dữ liệu. Bộ đĩa/tệp tin cơ sở dữ liệu có thể đã bị đổi tên/sao chép ở bên ngoài miền cơ sở dữ liệu.
115 Cơ sở dữ liệu "%1$s" đã tồn tại.
116 Cơ sở dữ liệu "%1$s" là không biết, hoặc tệp tin "databases.txt" không thể được truy cập.
117 Tên đường dẫn tuyệt đối cho cơ sở dữ liệu là quá dài. Chiều dài kết hợp của đường dẫn "%1$s" cộng với tên "%2$s" là %3$d; chiều dài kết hợp phải nhỏ hơn %4$d.
118 Thất bại khi xác định vị trí thư mục hiện tại đang làm việc.
119 Không thể tìm thấy tên máy chủ cục bộ.
120 Số bộ đĩa đĩa tối đa (%1$d) đã bị vượt quá.
121 Cố gắng gỡ bỏ "%1$s" một bộ đĩa thường trực từ cơ sở dữ liệu.
122 Không thể truy cập catalog thông báo hệ thống.
123 Không thể tạo "%1$s" cho cơ sở dữ liệu "%2$s".
124 Bộ đĩa "%1$s" đã tồn tại.
125 Phần mở rộng bộ đĩa tự động "%1$s" của trang %2$d và mục đích lưu trữ generic đã được tạo.
126 Mục đích "%1$s" không biết được đưa ra trên dòng %2$d.
127 Giá trị %1$d không chính xác cho số trang được đưa ra trên dòng %2$d.
128 Số trang chưa được đưa ra trên dòng %1$d.
129 Mã thông báo "%1$s" không biết được tìm thấy trên dòng %2$d.
130 Không đủ bộ nhớ ảo.
131 Lỗi hệ thống cơ sở dữ liệu.
132 Tính năng không được thực hiện trong phát hành hiện tại: %1$s.
133 Tập hợp không đổi lồng nhau không được phép.
134 Không được phép dùng cả thuộc tính lớp và thuộc tính không-lớp trong một UPDATE đơn.
135 Câu lệnh không hợp lệ cho cột oid: %1$s.
136 Hàm số chưa thực hiện "%1$s".
137 Đối tượng ủy quyền bị hỏng.
138 Không tìm thấy lớp cấp phép "%1$s".
139 Lỗi truy cập trong thuộc tính cấp phép "%2$s" của lớp "%1$s".
140 Thao tác "%1$s" chỉ có thể thực thi bởi DBA hoặc một thành viên nhóm DBA.
141 Không thể thêm "%1$s" như một thành viên của "%2$s".
142 Thêm thành viên dẫn đến hệ thống phân cấp người dùng trở thành một đồ thị vòng tròn.
143 Bắt gặp một lớp không có chủ sở hữu.
144 Không thể truy cập đối tượng người dùng.
145 Không thể đưa ra lệnh GRANT/REVOKE cho chính bạn.
146 Không thể đưa ra GRANT/REVOKE cho chủ sở hữu của một lớp.
147 Không có tùy chọn GRANT.
148 Không thể có được khóa ghi trên đối tượng cấp phép.
149 Không thể tạo thực thể của đối tượng cấp phép.
150 Không thể hủy đặc quyền từ chính nó.
151 Không thể hủy đặc quyền từ chủ sở hữu của một lớp.
152 Không tìm thấy GRANT.
153 Không có đặc quyền cấp phép  nào có hiệu lực cho cơ sở dữ liệu.
154 Cài đặt cấp phép chưa hoàn thành - không có đặc quyền cấp phép nào có hiệu lực cho cơ sở dữ liệu.
155 Gốc đã cấp phép.
156 Cấp phép thất bại.
157 Cấp phép SELECT thất bại.
158 Cấp phép ALTER thất bại.
159 Cấp phép UPDATE thất bại.
160 Cấp phép INSERT  thất bại.
161 Cấp phép DELETE thất bại.
162 Cấp phép INDEX thất bại.
163 Cấp phép EXECUTE thất bại.
164 Người dùng  "%1$s" đã tồn tại.
165 Người dùng User "%1$s" không hợp lệ.
166 Người dùng không hợp lệ được chỉ định.
167 DBA, các thành viên của nhóm DBA và chủ sở hữu lớp có thể thực thi thao tác.
168 Không tìm thấy thành viên.
169 Không thể gỡ bỏ người dùng %s từ cơ sở dữ liệu.
170 Không có người dùng nào đăng nhập.
171 Mật mã không chính xác hoặc thiếu.
172 Chuỗi mật khẩu không được có nhiều hơn 31 byte.
173 Không thể xác định vị trí tệp tin cơ sở dữ liệu "%1$s".
174 Không thể có được truy cập ghi đến tệp tin cơ sở dữ liệu "%1$s".
175 Tệp tin cơ sở dữ liệu bị định dạng tồi "%1$s".
176 Lỗi chuyển đổi trong định dạng ngày tháng.
177 Không thể tạo large object cho kho lưu trữ nội tại.
178 OID tạm thời bị bắt gặp không có MOP.
179 Miền "%1$s" không tương thích với miền "%2$s".
180 Miền "%1$s" của giá trị đã cung cấp không tương thích với miền đích đến "%2$s".
181 Không thể ép buộc giá trị của miền "%1$s" đến miền "%2$s".
182 Không thể ép buộc giá trị của miền "%1$s" đến miền "%2$s" mà không tràn.
183 Số lượng dữ liệu nhận được không mong đợi; đòi hỏi %1$d, nhận được  %2$d.
184 Không thể cấp phát bộ đệm liên lạc.
185 Lỗi khi nhận dữ liệu từ máy khách.
186 Lỗi khi nhận dữ liệu từ server.
187 Bộ đệm liên lạc không được dùng.
188 Cơ sở dữ liệu "%1$s" không biết.
189 Tên máy chủ "%1$s" không hợp lệ.
190 Máy chủ Server chưa được xác định.
191 Không thể kết nối đến server "%1$s" trên "%2$s".
192 Không thể tải tệp tin cấu hình mạng.
193 Server nhận được lệnh tắt máy từ máy khách.
194 Server không rõ đòi hỏi id %1$d.
195 Lỗi liên lạc server: %1$s.
196 Tên Server chưa được xác định.
197 Không thể liên lạc với server chủ.
198 Nhận được bộ đệm quá nhỏ, dữ liệu đã bị cắt bỏ.
199 Server không còn đáp ứng.
200 Không đúng cách loại bỏ các thiết lập từ đối tượng sau khi miễn phí.
201 Đã gỡ bỏ mục bảng UNIQUE không chính xác.
202 Thuộc tính "%1$s" không được tìm thấy.
203 Giá trị cho thuộc tính "%1$s" phải là kiểu "%2$s", không phải kiểu "%3$s".
204 Hàm số được gọi có đối số bị thiếu hoặc không hợp lệ.
205 Thuộc tính "%1$s" không thể là NULL.
206 Thuộc tính "%1$s" không có ràng buộc UNIQUE.
207 Lớp "%1$s" có một đối tượng kích cỡ bằng không.
208 Không tìm thấy phương pháp "%1$s".
209 Không có thuộc tính hoặc phương pháp nào với tên "%1$s" được tìm thấy.
210 Lỗi nội bộ: xử lý bản mẫu đối tượng.
211 Thuộc tính đã bị xóa trong khi xây dựng bản mẫu đối tượng.
212 Thuộc tính "%1$s" vi phạm ràng buộc UNIQUE.
213 Bắt gặp mục bảng UNIQUE không hợp lệ.
214 Một xung đột miền tồn tại trong thuộc tính "%1$s".
215 Không tìm thấy thuộc tính với id %1$d .
216 Thẻ quyền sở hữu đối tượng trên giá trị không hợp lệ.
217 Thao tác chỉ có thể được thực thi trên các đối tượng lớp.
218 Đối tượng không hợp lệ trong db_get path expression.
219 db_get path expression không hợp lệ.
220 Tập hợp không hợp lệ trong db_get path expression.
221 Chỉ mục tập hợp không hợp lệ trong db_get path expression.
222 Tìm cách gán giá trị chuỗi lớn hơn %2$d ký tự đến thuộc tính "%1$s".
223 Giá trị %2$d quá lớn đối với thuộc tính smallint "%1$s".
224 Một cơ sở dữ liệu chưa được khởi động lại.
225 Thiếu giá trị cho thuộc tính "%1$s" với ràng buộc NOT NULL.
226 Danh sách cấu phần rỗng trong lớp.
227 Tìm cách gán giá trị đến thuộc tính "%1$s" nhiều hơn một lần.
228 Phương pháp "%1$s" được gọi với đối số %2$d. Hệ thống cho phép tối đa %3$d đối số.
229 Phương pháp "%1$s" đòi hỏi đối số %2$d nằm trong miền "%3$s". Một giá trị không hợp lệ của miền "%4$s" đã được đưa ra.
230 Thuộc tính "%1$s" không thể được gán một chuỗi với độ dài %2$d. Độ dài của chuỗi tối đa cho phép %3$d.
231 Thao tác API không hợp lệ đã cố gắng trên một đối tượng tạm thời.
232 Không được phép dùng các bản mẫu đối tượng qua nhiều giao dịch.
233 Không thể tạo lớp với tên kiểu cơ bản "%1$s".
234 Không tìm thấy tệp tin phương pháp "%1$s".
235 %1$d hàm số phương pháp chưa được giải quyết sau khi kết nối.
236 Phương pháp chưa được giải quyết "%1$s".
237 Một vài vấn đề đã bị bắt gặp trong quá trình kết nối động.
238 Không thể truy cập tệp tin phương pháp "%1$s".
239 Không tìm thấy thuộc tính "%1$s" .
240 Không tìm thấy phương pháp "%1$s" .
241 Không có thuộc tính hoặc phương pháp nào có tên "%1$s".
242 Không tìm thấy signature cho %2$s cho phương pháp "%1$s".
243 Đối số %2$d của phương pháp "%1$s" chưa được định nghĩa.
244 Tên miền "%1$s" không phải là tên của một lớp hoặc kiểu cơ bản.
245 Tên "%1$s" đã được sử dụng như một tên thuộc tính.
246 Tên "%1$s" đã được sử dụng như một tên phương pháp.
247 Đối số không hợp lệ đến hàm số nội tại bộ quản lý lược đồ.
248 Miền thuộc tính "%1$s" không phù hợp để sử dụng với ràng buộc %2$s.
249 Thao tác được đòi hỏi trên lớp "%1$s" chỉ có thể thực thi trước khi các thực thể được tạo.
250 Không thể tạo chỉ mục trên thuộc tính "%1$s", được định nghĩa với một giá trị dùng chung.
251 signature %2$s đã tồn tại cho phương pháp "%1$s".
252 Thuộc tính "%1$s" phải có một tập hợp miền cho thao tác đó.
253 Thuộc tính "%1$s" không thể được định nghĩa với tập hợp miền lồng nhau.
254 Không thể gỡ bỏ miền "%2$s" từ thuộc tính "%1$s".
255 Thuộc tính "%1$s" không phải là một miền có kích thước có thể biến đổi.
256 Lớp cao cấp đã tồn tại.
257 Thêm lớp cao cấp sẽ dẫn đến các vòng tròn trong hệ thống phân cấp lớp.
258 Không tìm thấy lớp cao cấp.
259 Nhiều chữ ký đã tồn tại cho phương pháp "%1$s".
260 Đối số %2$d của phương pháp "%1$s" không phải một tập hợp miền.
261 Không tìm thấy nghị quyết cho "%1$s".
262 Miền giá trị "%2$s" không phù hợp cho thuộc tính "%1$s".
263 Hư hỏng của Bộ quản lý lược đồ nội tại đã được phát hiện.
264 Bí danh "%2$s" cho thuộc tính/phương pháp "%1$s" không phải duy nhất.
265 Thuộc tính shadowing "%1$s" có miền xung đột "%2$s".
266 Xung đột tên trên thuộc tính "%1$s" với lớp "%2$s" và "%3$s".
267 Phương pháp shadowing "%1$s" có chữ ký xung đột.
268 Xung đột tên trên phương pháp "%1$s" với lớp "%2$s" và "%3$s".
269 Không thể định nghĩa chỉ mục trên miền "%1$s".
270 Không thể định nghĩa ràng buộc UNIQUE trên miền "%1$s".
271 Phát hiện vòng tròn trong hệ thống phân cấp lớp; "%1$s" là một lớp cao cấp của "%2$s".
272 Chỉ mục "%2$s" đã được định nghĩa cho lớp "%1$s".
273 Chỉ mục "%1$s" không tồn tại.
274 Ký tự không hợp lệ trong tên: "%1$s".
275 Thao tác không hợp lệ trên thuộc tính "%1$s", thừa kế từ lớp "%2$s".
276 Thao tác không hợp lệ trên phương pháp "%1$s", thừa kế từ lớp "%2$s".
277 Thao tác không hợp lệ trên "%1$s", thừa kế từ lớp "%2$s".
278 Lớp "%4$s" có miền không tương thích cho "%1$s", thừa kế từ các lớp "%2$s" và "%3$s".
279 Trong lớp "%4$s", "%1$s" thừa kế từ lớp "%2$s" không thể được sử dụng như yêu cầu. Định nghĩa từ lớp "%3$s" có thêm miền cụ thể.
280 Trong lớp "%3$s", "%1$s" không thể được thừa kế từ lớp "%2$s" bởi vì có một định nghĩa cục bộ với miền không tương thích.
281 Trong lớp"%3$s", "%1$s" thừa kế từ lớp "%2$s" được lập bí danh nhưng không thể thay thế từ một nguồn khác đã được tìm thấy.
282 Trong lớp "%4$s", "%1$s" thừa kế từ lớp "%3$s" không thể phục vụ như một bí danh thay thế cho lớp "%2$s". Các miền không tương thích.
283 Trong lớp "%4$s", "%1$s" thừa kế từ lớp "%3$s" không thể phục vụ như một bí danh thay thế cho lớp "%2$s". Nó có ít nhất một miền được chỉ định.
284 Không thể yêu cầu thừa kế của "%1$s" bởi vì nó được định nghĩa một cách cục bộ trong lớp.
285 Không thể sử dụng "%1$s" như một bí danh cho cấu phần được thừa kế "%2$s" bởi vì đã có một cấu phần được định nghĩa một cách cục bộ với tên đó.
286 Không thể sử dụng  "%1$s" như một bí danh cho "%2$s" of "%3$s". Một cấu phần với tên đó đã được thừa kế từ "%4$s".
287 Trong lớp "%1$s", không thể định nghĩa thuộc tính "%2$s" bởi vì nó sẽ đổ bóng một phương pháp được thừa kế từ "%3$s".
288 Trong lớp "%1$s", không thể định nghĩa phương pháp "%2$s" bởi vì nó sẽ đổ bóng một thuộc tính được thừa kế từ "%3$s".
289 Lớp con "%1$s" không thể thừa kế phương pháp "%2$s" từ lớp "%3$s" bởi vì đã có một thuộc tính với tên đó.
290 Lớp con "%1$s" không thể thừa kế thuộc tính "%2$s" từ lớp "%3$s" bởi vì đã có một phương pháp với tên đó.
291 "%1$s" được định nghĩa trong lớp "%2$s" như một thuộc tính và trong lớp "%3$s" như một phương pháp.
292 Không cư trú đặc tả với chỉ mục "%1$s".
293 Thao tác không được cho phép cho kiểu lớp này.
294 Biến số môi trường chưa định nghĩa "%1$s" được tham chiếu trong tệp tin phương pháp.
295 Không thể sửa đổi lớp "%1$s". Catalog hệ thống không đủ không gian. Để lấy lại không gian, nén cơ sở dữ liệu với tiện ích compactdb.
296 Danh sách thuộc tính không hợp lệ đã bị bắt gặp.
297 Tên bí danh "%1$s" được sử dụng nhiều hơn một lần.
298 Trong lớp  "%1$s", một nghị quyết cho "%2$s" of "%3$s"' không được cho phép. Lớp đó không phải là một lớp cao cấp.
299 "%1$s" là một từ dành riêng và không thể được sử dụng như một thuộc tính, phương pháp, hoặc tên lớp.
300 Thuộc tính "%1$s" không thể được định nghĩa với ràng buộc DEFAULT và UNIQUE cùng nhau.
301 Thuộc tính "%1$s" không thể được định nghĩa với ràng buộc độ dài của %2$d. Ràng buộc độ dài tối đa là %3$d.
302 Không thể thêm phần tử cho một tập hợp.
303 Giá trị đã tồn tại trong tập hợp.
304 Chỉ mục set %1$d nằm ngoài giới hạn.
305 Chỉ mục sequence %1$d nằm ngoài giới hạn.
306 Thao tác được yêu cầu chỉ có thể được thực thi theo sequence.
307 Thao tác được yêu cầu chỉ có thể được thực thi theo set.
308 Giá trị phần tử không nằm trong miền của tập hợp.
309 Chỉ mục phần tử tập hợp không hợp lệ được đưa ra: %1$d.
310 Không tìm thấy phần tử trong set.
311 Không tìm thấy phần tử trong sequence.
312 "%1$s" không phải là một tập hợp miền.
313 Bộ đệm đối tượng hụt trong khi đang đọc.
314 Bộ đệm đối tượng tràn trong khi đang ghi.
315 Bắt gặp định nghĩa metaclass không hợp lệ.
316 Kích cỡ bộ chuyển đổi tính toán không khớp, đòi hỏi %1$d tính toán %2$d.
317 Bắt gặp đại diện đĩa không hợp lệ cho lớp  "%1$s".
318 Thiếu đồng bộ trong quá trình tải đối tượng. Cơ sở dữ liệu có thể bị hỏng hoặc hết hạn.
319 Bỏ qua phần mở rộng thuộc tính không biết.
320 Bắt gặp đại diện đĩa hỏng của đối tượng.
321 Phát hiện khả năng hỏng hóc trong vùng làm việc.
322 Không tìm thấy MOP trong vùng làm việc.
323 Cố gắng nâng cấp MOP không-tạm thời trong vùng làm việc.
324 Đối tượng được tìm thấy trong vùng làm việc mà không có lớp.
325 Cố gắng thu thập rác một MOP trong danh sách bẩn.
326 Thay đổi con trỏ lớp cho đối tượng trong vùng làm việc.
327 Không thể tạo MOP với NULL OID.
328 Thực thể bị bắt gặp trong vùng làm việc mà không có lớp.
329 Kết nối danh sách đối tượng không được cấp phát trong vùng làm việc.
330 Vi phạm pin vùng làm việc.
331 Không thể cấp phát khối vùng làm việc từ OS, không đủ bộ nhớ ảo. Đang bỏ dở giao dịch.
332 Ước tính kích cỡ vùng làm việc ban đầu đã bị vượt quá. Một phần mở rộng tự động đã được tạo ra.
333 Không đủ bộ nhớ trong quá trình cấp phát lưu trữ vùng làm việc. Bỏ dở giao dịch.
334 Cố gắng cấp phát của %1$d bytes lớn hơn kích cỡ khối làm việc tối đa. Thử điều chỉnh tham số max_block_size. Giao dịch đã bị bỏ dở.
335 Bỏ qua cố gắng thả tự do đối tượng chưa được cấp phát trong vùng làm việc.
336 Bỏ qua cố gắng thả tự do một đối tượng nhiều hơn một lần.
337 Bỏ qua cố gắng để cấp phát đối tượng với kích cỡ âm.
338 Mã định danh mục nhập bản đồ vượt quá.
339 Không thể thoát đoạn mã tới hạn, bởi vì nó không được sở hữu bởi giao dịch %1$d.
340 Không thể phân nhánh tiến trình con.
341 Không thể thực thi tiến trình con.
342 Không thể đổi nhóm tiến trình.
343 Request_id đã được dùng.
344 %1$s.
345 Lỗi đã phát sinh trong quá trình đọc một thông báo shutdown từ master.
346 Lỗi đã phát sinh trong quá trình đọc một thông báo stop shutdown từ master.
347 Không thể thiết lập một pipe đến master server.
348 Phải chỉ định hoặc dịch vụ hoặc mã định danh cổng.
349 Dịch vụ không rõ cho TCP: %1$s.
350 Lỗi khi lấy máy chủ cục bộ theo tên cho máy chủ "%1$s". Không tìm thấy máy chủ.
351 Không thể tạo TCP socket.
352 tcp_open: không thể lấy một cổng TCP riêng.
353 Không thể kết nối tới server chính trên máy chủ "%1$s".
354 Không thể thết lập chủ sở hữu tiến trình mới cho socket.
355 Không thể tạo luồng socket.
356 Tệp tin '%1$s' cho UNIX domain socket tồn tại.
357 Không thể gắn kết địa chỉ cục bộ... đang bỏ dở.
358 Accept đã bị thất bại.
359 Lỗi gắn kết datagram.
360 Lỗi chấp nhận datagram.
361 Lỗi kết nối Datagram.
362 Lỗi socket Datagram.
363 Thất bại khi Recvmsg từ socket master.
364 Đang gửi một yêu cầu mới đến server.
365 Đang gửi một thông báo broadcast đến máy khách.
366 Lỗi từ server: %1$s
367 Server %1$s đã tồn tại.
368 Lỗi liên lạc trong quá trình kết nối cho %1$s.
369 Mã lỗi chưa được dùng.
370 Mã lỗi chưa được dùng.
371 Mã lỗi chưa được dùng.
372 Mã lỗi chưa được dùng.
373 EOF sớm trong quá trình đọc: %1$d.
374 EOF sớm trong quá trình đọc của %1$d hoặc %2$d.
375 Lỗi chuyển đổi trên (0x%1$c%2$c) %3$d và %4$d được chuyển đổi thành %5$d.
376 Không tìm thấy lớp public nào.
377 Lỗi %1$d: thất bại khi thêm thuộc tính "%2$s" cho hệ thống phân cấp đa phương tiện.
378 Lỗi %1$d: thất bại khi thêm phương pháp "%2$s" được thực hiện như "%3$s" cho hệ thống phân cấp đa phương tiện.
379 Lỗi %1$d: thất bại khi thêm lớp cao cấp vào hệ thống phân cấp đa phương tiện.
380 Bộ tải động đã được khởi tạo.
381 Bộ tải động chưa được khởi tạo.
382 Bộ tải động được khởi tạo không đúng cách.
383 "%1$s" không phải là một tệp tin đối tượng hoặc một lưu trữ.
384 Không thể xác định đường dẫn tuyệt đối cho "%1$s".
385 Quá trình ld đã thoát với mã %1$d.
386 Quá trình ld đã thoát vì tín hiệu %1$d.
387 Quá trình ld đã thoát với waitval 0x%1$x không rõ.
388 Không thể truy cập hình ảnh ban đầu cho "%1$s".
389 Lỗi hệ thống: %1$s.
390 Vấn đề với "%1$s": %2$s.
391 Xung đột với bộ xử lý SIGPIPE khác.
392 Không thể sinh ra bộ tải động daemon.
393 Bộ tải động daemon đã biến mất.
394 Điểm kết thúc phạm vi không nằm giữa 0 và 256.
395 Biểu thức con được chỉ định không hợp pháp.
396 Chữ số hệ cơ số 8 nằm ngoài phạm vi tồn tại trong escape.
397 Biểu thức chính quy chưa được định ranh chính xác.
398 Tìm kiếm văn bản được gọi trước khi biểu thức được biên dịch.
399 Tìm thấy một dấu ngoặc đơn chưa được khớp trong biểu thức chính quy.
400 Dấu ngoặc đơn được lồng nhau quá sâu trong biểu thức chính quy.
401 Nhiều hơn hai số được chỉ định trong một phạm vi.
402 Tìm thấy một dấu ngoặc móc xoăn chưa được khớp trong biểu thức chính quy.
403 Số đầu tiên trong biểu thức phạm vi lớn hơn số thứ hai.
404 Tìm thấy một dấu ngoặc vuông chưa được khớp trong biểu thức chính quy.
405 Biểu thức chính quy quá dài để biên dịch.
406 Mã định danh chỉ mục B+tree không hợp lệ: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
407 Key %1$s không rõ được tham chiếu trong chỉ mục B+tree {vfid: (%2$d, %3$d), rt_pgid: %4$d, key_type: %5$s}.
408 Giá trị không biết (oid) %1$d|%2$d|%3$d được tham chiếu.
409 Giá trị trùng lặp (oid) %1$d|%2$d|%3$d được tham chiếu.
410 Null key được tham chiếu.
411 Kiểu key chỉ mục B+tree không hợp lệ "%1$s".
412 Đặc tả tìm kiếm phạm vi không hợp lệ.
413 Mã định danh thuộc tính không biết: %1$d.
414 Mã định danh lớp không biết: %1$d|%2$d|%3$d.
415 Mã định danh lớp không hợp lệ: %1$d|%2$d|%3$d.
416 Mã định danh đại diện không biết: %1$d.
417 Mã định danh đại diện không hợp lệ: %1$d.
418 Bộ mô tả bản ghi chép không có đủ không gian: %1$d.
419 Lớp đã đạt số lượng đại diện tối đa: %1$d.
420 Lớp %1$d|%2$d|%3$d đã có đại diện.
421 Thiếu Thư mục Đại diện trong Catalog cho Lớp: %1$d|%2$d|%3$d.
422 Thiếu Bản ghi chép Thông tin Đại diện trong Catalog cho Lớp - Repr_Id: %1$d|%2$d|%3$d - %4$d.
423 Phiên không hợp lệ.
424 Không có câu lệnh nào để thực thi.
425 Kiểu dữ liệu không tương thích cho thuộc tính "%1$s" của lớp "%2$s".
426 Kiểu dữ liệu không tương thích trên thuộc tính "%1$s".
427 Tràn dữ liệu trên kiểu dữ liệu "%1$s".
428 Câu lệnh không thể cập nhật.
429 Lệnh không biết.
430 Biến số không biết "%1$s".
431 Lỗi trong bộ diễn dịch SQL/X hoặc bộ phân tích cú pháp.
432 Lớp không biết "%1$s".
433 Thuộc tính không biết "%1$s" trong lớp "%2$s".
434 Không thể tìm thấy tên cột chính xác từ một truy vấn.
435 Nhiều câu lệnh chưa được cho phép.
436 Câu lệnh không phải là một truy vấn.
437 Long object ID không hợp lệ (%1$s,%2$d, %3$s, %4$s).
438 Xung đột tồn tại trong bộ mô tả Long Object.
439 Độ lệch %1$d lớn hơn độ dài của Long Object %2$d.
440 Vị trí con chạy không hợp lệ.
441 Thao tác con chạy không hợp lệ.
442 Vị trí con chạy không biết.
443 Chỉ mục giá trị Tuple %1$d nằm ngoài phạm vi.
444 Tên thuộc tính không hợp lệ: "%1$s".
445 Chỉ mục danh sách giá trị %1$d nằm ngoài phạm vi.
446 Có một cấu trúc kết quả truy vấn được đóng lại bởi bộ quản lý giao dịch nhưng chưa được đóng lại bởi ứng dụng.
447 Tìm cách để thao tác một cấu trúc kết quả truy vấn đã được đóng lại.
448 Kích cỡ Tuple hiện tại bị giới hạn để không lớn hơn một trang.
449 Mã định danh truy vấn không rõ: %1$d.
450 Bộ thao tác tập hợp không hợp lệ: %1$d.
451 Tệp tin heap không có thực thể nào.
452 Nội dung nút cây XASL không hợp lệ.
453 Không có thêm nút đặc tả truy cập.
454 Kiểu dữ liệu được tham chiếu không hợp lệ.
455 Không có thêm trang tệp tin truy vấn nào rời đi.
456 Tham chiếu kiểu dữ liệu không tương thích.
457 Kiểu kết quả truy vấn không hợp lệ.
458 Tràn đã xảy ra trong ngữ cảnh thêm vào.
459 Kết quả truy vấn chứa nhiều hơn một tuple đơn.
460 Một vài biến máy chủ đầu vào được cung cấp.
461 Nhiều biến máy chủ được đưa ra hơn các cột kết quả.
462 Giá trị kết quả là NULL, nhưng không có biến chỉ báo nào.
463 Câu lệnh Cursor vẫn chưa được chuẩn bị.
464 Con chạy phải là một câu lệnh SELECT.
465 Con chạy vẫn chưa được mở.
466 Con chạy phải được đóng trước khi mở lại.
467 Kết quả phải trả về một đối tượng đơn.
468 Không có positional marker nào được cho phép trong câu lệnh EXECUTE IMMEDIATE.
469 Kiểu dữ liệu không hợp lệ được sử dụng.
470 Truy vấn chứa quá nhiều mục để tối ưu hóa.
471 Cạn kiệt bộ nhớ trong dòng %1$d của tệp tin %2$s.
472 %1$s bộ kết nối cho cơ sở dữ liệu cụ bộ '%2$s' trong máy chủ '%3$s' đã chết.
473 Báo cáo từ cơ sở dữ liệu cục bộ '%1$s' trong máy chủ '%2$s': %3$s.
474 Lỗi %1$s được phát hiện trong quá trình %2$s.
475 Không có đặc tả truy vấn nào có chỉ mục %1$s.
476 LDB %1$s đã tồn tại.
477 Không thể kết nối đến master trong máy chủ %2$s để truy cập LDB %1$s.
478 Cơ sở dữ liệu cục bộ "%1$s" không phải là một LDB đã đăng ký.
479 Cơ sở dữ liệu cục bộ "%1$s" hiện tại đang được truy cập bởi máy khách khác, sửa đổi hoặc xóa đều không được phép.
480 Cơ sở dữ liệu cục bộ "%1$s" không rỗng và không thể bị xóa.
481 "%1$s" không phải một lớp ảo proxy.
482 "%1$s" không phải là một lớp ảo.
483 "%1$s" không phải một thuộc tính của lớp "%2$s".
484 object_id cho lớp ảo proxy "%1$s" đã được thiết lập.
485 Một hoặc nhiều thuộc tính object_id là NULL cho thực thể của lớp proxy "%1$s".
486 Đối tượng không phải là một thực thể lớp ảo có thể cập nhật.
487 Đối tượng không phải là một thực thể.
488 Một thuộc tính của lớp có thể không phải là một lớp ảo hoặc một lớp ảo proxy.
489 Một thuộc tính của lớp ảo proxy có thể không phải là một lớp, lớp ảo, hoặc đối tượng.
490 Lớp cao cấp "%1$s" phải cùng một kiểu của lớp như "%2$s".
491 Không thể trích xuất tên đặc tính từ đặc tả truy vấn proxy "%1$s".
492 %1$s
493 Cú pháp: %1$s %2$s
494 Ngữ nghĩa: %1$s %2$s
495 Thực thi: %1$s %2$s
496 Key không rõ được tham chiếu.
497 Giá trị không biết (oid) %1$d|%2$d|%3$d được tham chiếu.
498 Giá trị trùng lặp Duplicate value (oid) %1$d|%2$d|%3$d được tham chiếu.
499 Null key được tham chiếu.
500 Key trùng lặp được tham chiếu.
501 Ưu tiên trigger không hợp lệ.
502 Thiếu lớp mục tiêu trong trigger.
503 Trigger "%1$s" không được tìm thấy.
504 Lỗi nội bộ: xử lý trigger "%1$s".
505 Một trigger có tên "%1$s" đã tồn tại.
506 Một trigger không thể được định nghĩa trên "%1$s" bởi vì nó là một lớp ảo.
507 Lớp mục tiêu trigger "%1$s" không thích hợp.
508 Thuộc tính mục tiêu trigger "%1$s" không thích hợp.
509 Điều kiện trigger "%1$s" không hợp lệ.
510 Hành động trigger "%1$s" không hợp lệ.
511 Không được quyền truy cập trigger "%1$s".
512 Không được quyền xóa trigger "%1$s".
513 Không được quyền cập nhật trigger "%1$s".
514 Không được quyền sửa đổi trigger "%1$s".
515 Thời gian thực thi hành động của %2$s không thể sớm hơn thời gian thực thi điều kiện của %1$s.
516 Độ dài tối đa của trigger %1$d đã bị vượt quá tại "%2$s".
517 Thao tác đã bị từ chối bởi trigger "%1$s".
518 Lỗi nội bộ: xử lý trigger "%1$s".
519 Kiểu điều kiện trigger không hợp lệ.
520 Hành động REJECT không thể được dùng với một thời gian hành động của AFTER hoặc DEFERRED.
521 Hành động REJECT không thể được dùng với sự kiện ABORT hoặc TIMEOUT.
522 Biểu thức điều kiện Trigger chưa được đưa ra.
523 Biểu thức hành động Trigger chưa được đưa ra.
524 Hoạt động được chỉ định không thể bị gỡ bỏ bởi vì nó được sở hữu bởi một người dùng khác.
525 Lỗi biên dịch điều kiện cho "%1$s": %2$s
526 Lỗi biên dịch hành động cho "%1$s", %2$s
527 Lỗi ước tính điều kiện cho "%1$s": %2$s
528 Lỗi ước tính hành động cho  "%1$s", %2$s
529 Giao dịch không thể được cam kết, nó đã bị vô hiệu hóa bởi trigger "%1$s".
530 Thiếu biểu thức chính quy.
531 Biểu thức chính quy quá dài hoặc phức tạp.
532 Dấu ngoặc đơn hoặc dấu ngoặc vuông không cân bằng trong biểu thức chính quy.
533 Tham chiếu nằm ngoài phạm vi trong biểu thức chính quy.
534 Ký tự hoặc cú pháp không hợp pháp trong biểu thức chính quy.
535 Không có văn bản nào được đưa ra cho tìm kiếm biểu thức chính quy.
536 Tìm thấy opcode (mã thao tác) không hợp pháp trong biểu thức chính quy.
537 Mã lệnh tìm kiếm không hợp lệ cho bộ biên dịch biểu thức chính quy.
538 Bộ đệm làm việc biểu thức chính quy cần được khởi tạo.
539 Tìm cách để chia cho Không.
540 %1$s.
541 Số trang trống cho bộ đĩa "%1$s" là không nhất quán. %2$d và %3$d đã được tìm thấy dựa theo đề mục bộ đĩa và bitmap, theo thứ tự.
542 Số sectors trống cho bộ đĩa "%1$s" là không nhất quán. %2$d và %3$d đã được tìm thấy dựa theo đề mục bộ đĩa và bitmap, theo thứ tự.
543 Đề mục đĩa cho bộ đĩa "%1$s" không nhất quán.
544 Lỗi nội bộ: CHỈ MỤC %1$s TRÊN LỚP %2$s (CLASS_OID: %3$d|%4$d|%5$d). Khóa B+tree %6$s mục nhập cho OID đối tượng: %7$d|%8$d|%9$d không được tìm thấy trên B+tree: %10$d|%11$d|%12$d.
545 Lỗi nội bộ: CHỈ MỤC %1$s TRÊN LỚP %2$s (CLASS_OID: %3$d|%4$d|%5$d). Khóa và OID Key and OID: %6$d|%7$d|%8$d mục nhập trên B+tree: %9$d|%10$d|%11$d không chính xác. Đối tượng không tồn tại.
546 Lỗi nội bộ: CHỈ MỤC %1$s TRÊN LỚP %2$s (CLASS_OID: %3$d|%4$d|%5$d). Đòi hỏi giá trị mục nhập OID %6$d, nhưng %7$d được tìm thấy trên B+tree: %8$d|%9$d|%10$d.
547 Server phát hành %1$s khác với máy khách phát hành %2$s.
548 Không có object_id nào được định nghĩa cho lớp proxy "%1$s".
549 Thực thể proxy không được tìm thấy.
550 Tên savepoint %1$s không rõ
551 Không thể xác định vị trí tệp tin đường dẫn thông tin bộ đĩa "%1$s"... Tiếp tục đọc từ các bảng nội bộ
552 Số học date hụt.
553 Số học time hụt.
$ LOADDB
554 Bộ tải đã nhập vào một trạng thái không hợp lệ. Không có thao tác hơn nữa nào là có thể.
555 Lỗi cấp phát bộ nhớ trong quá trình tải cơ sở dữ liệu.
556 Giá trị quá nhiều cung cấp. Mong đợi %d.
557 Với thuộc tính %1$s of %2$s, kiểu %3$s không được cho phép trong miền của tập hợp.
558 Với thuộc tính %1$s of %2$s, kiểu mong đợi %3$s, có một hằng số tập hợp.
559 Với thuộc tính %1$s of %2$s, kiểu mong đợi %3$s, có  %4$s.
560 Với thuộc tính %1$s of %2$s, miền không đủ cụ thể để hỗ trợ\n các tham chiếu đối tượng chưa đủ tiêu chuẩn.
561 Các tập hợp lồng nhau là không được phép.
562 Lớp hệ thống %1$s không thể được cư trú bằng cách dùng bộ tải cơ sở dữ liệu.
563 Tham chiếu đến lớp nội bộ %1$s được chuyển đổi thành NULL.
564 Vi phạm ràng buộc UNIQUE cho thuộc tính %1$s của %2$s.
565 Phương pháp constructor %1$s chưa được định nghĩa trên lớp %2$s.
566 Không có lớp quy định hoặc không có thuộc tính trong lớp học.
567 Có quá nhiều đối số phương pháp constructor được cung cấp. Đã yêu cầu %1$d.
568 Thiếu tham số phương pháp constructor. Đã yêu cầu %1$d, đã tìm thấy %2$d.
569 Thiếu giá trị thuộc tính.  Đã yêu cầu %1$d, đã tìm thấy %2$d.
570 Không thể truy cập tệp tin dữ liệu Glo "%1$s".
571 Thực thể đã được tạo trước đó qua một tham chiếu phía trước.\nĐiều này là không được phép cho các thực thể được tạo bằng cách sử dụng phương pháp constructor.
572 Lỗi nội bộ: Không thể xây dựng đại diện ổ đĩa của thực thể.
573 Lỗi nội bộ: Không thể chèn một thực thể mới trong cơ sở dữ liệu.
574 Lỗi nội bộ: Không thể cập nhật một thực thể mới trong cơ sở dữ liệu.
575 Với đối số %1$d của constructor %2$s, kiểu %3$s được mong đợi, có được %4$s.
576 Tham chiếu đến lớp %3$s không tương thích với miền của %1$s của %2$s.
577 Với đối số %1$d của bộ xây dựng %2$s, miền không đủ cụ thể để hỗ trợ\n các tham chiếu đối tượng chưa đủ tiêu chuẩn.
578 Tham chiếu đến lớp %3$s không tương thích với miền của đối số %1$d \n của constructor %2$s.
579 Miền mục tiêu phải bao gồm kiểu "object" để cho phép các tham chiếu đến các lớp.
580 Thuộc tính %1$s chưa được định nghĩa cho lớp %2$s.
581 Cố gắng để cập nhật cơ sở dữ liệu khi các cập nhật bị vô hiệu hóa.
582 Mục đích lưu trữ %1$d không rõ. Vùng hiệu lực là từ %2$d đến %3$d.
583 Cố gắng để cấp phát một số trang̣ (%1$d) không hợp lệ.
584 Lỗi nội bộ: Một vòng lặp được phát hiện trong bảng tệp tin của tệp tin %1$d trong bộ đĩa "%2$s". Vòng lặp có thể bị gây ra bởi trang %3$d|%4$d.
585 Heap %1$s|%2$d|%3$d không biết
586 Lỗi nội bộ: Không thể cập nhật tiêu đề chuỗi trong VPID %1$d|%2$d cho heap %3$d trong bộ đĩa "%4$s".
587 Các mục nhập của các bộ đĩa thường trực chưa được phân loại trong tệp tin volinfo "%1$s" của bạn  volinfo file. Mục nhập %2$d: %3$d %4$s nằm ngoài trình tự.
588 Lỗi nội bộ: Xác nhận '%1$s' thất bại.
589 Tên người dùng "%1$s" không hợp lệ.
590 Miền thuộc tính "%1$s" của một lớp có thể không phải là một \nlớp ảo hoặc một lớp ảo proxy.
591 Miền thuộc tính "%1$s" của một lớp ảo proxy \nphải là một lớp ảo proxy.
592 Miền thuộc tính "%1$s" của một lớp ảo proxy \nvà lớp ảo proxy phải trong một cơ sở dữ liệu cục bộ giống nhau.
593 Một thực thể ảo không thể cập nhật đã được cải tạo và không thể được xây dựng lại.
594 Một mệnh đề object_id phải được cung cấp mà không sử dụng thuộc tính (%1$s) của các kiểu dữ liệu approximate (float hoặc double).
595 Tên cơ sở dữ liệu "%1$s" quá dài. Tên cơ sở dữ liệu phải ít hơn %2$d ký tự.
596 %1$d trang của tổng không gian tạm thời cho phép đã bị vượt quá.
597 Số trang cho tệp tin heap %1$d|%2$d|%3$d không nhất quán. \n%4$d và %5$d được tìm thấy dựa theo heap chain và bảng tệp tin, theo thứ tự.
598 Số tệp tin không đồng nhất. %1$d được mong đợi, %2$d được tìm thấy
599 Một lỗi I/O phát sinh khi đồng bộ hóa trạng thái của bộ đĩa "%1$s".
600 Hàm số "%1$s" không được thực thi trên máy PC.
601 "%1$s".
602 Lỗi dl: "%1$s" được định nghĩa nhiều hơn một lần.
603 Lỗi nội bộ: Sector/bảng trang của tệp tin VFID %1$d|%2$d dường như bị hỏng.
604 Lỗi nội bộ: Một thiết lập cấp phát cho tệp tin VFID %1$d|%2$d(Bộ đĩa"%3$s") dường như không đồng nhất trong %4$d bị xóa hoặc được đánh dấu các trang bị xóa và lỗ thủng trang %5$d trong bảng
605 Lỗi nội bộ: Số trang được đánh dấu bị xóa không đồng nhất cho tệp tin VFID %1$d|%2$d(Bộ đĩa "%3$s"). %4$d được đòi hỏi, %5$d được tìm thấy.
606 Không thể xác định vị trí odbc.ini với đường dẫn "%1$s".
607 Cơ sở dữ liệu "%1$s" không được tìm thấy trong odbc.ini.
608 Cơ sở dữ liệu "%1$s" không có một đặc tả Máy chủ trong tệp tin odbc.ini.
609 Giao dịch của bạn không thể được quay lui lại vì bản ghi đã bị bỏ qua. cơ sở dữ liệu của bạn có thể bị hỏng.
610 Cơ sở dữ liệu của bạn có thể bị hỏng vì bản ghi đã bị tắt khi cơ sở dữ liệu của bạn gặp sự cố.
611 %s không thể được thực thi trong chế độ client/server.
612 Bản ghi không thể bị tắt tại thời điểm này vì có các hành động đang chờ để khôi phục.
613 Trong lớp "%3$s", cấu phần cục bộ "%1$s" không thể phục vụ như một thay thế bí danh cho lớp "%2$s". Các miền không tương thích.
614 Số lưu trữ bản ghi đang hoạt động đã vượt qua số %1$d tối da cần thiết.
615 Không thể thiết lập liên lạc với giao diện Winsock. Lỗi Winsock: %1$d.
616 Không thể xác định tên máy sử dụng giao diện Winsock. Lỗi Winsock: %1$d.
617 Không thể xác định id máy sử dụng giao diện Winsock. Phải có một mục nhập trong bảng tên máy chủ cho máy này. Lỗi Winsock: %1$d.
618 Thuộc tính không thể cập nhật.
619 Tập hợp mã nguồn tồi.
620 Tập hợp mã đích nhận sai.
621 Kiểu dữ liệu không hợp lệ. Đối số chứa một kiểu dữ liệu không hợp lệ.
622 Tập hợp mã không tương thích. Các tập hợp mã của các đối số là không tương thích.
623 Escape sequence không hợp lệ. Escape sequence không hợp lệ bị gây ra bởi một lỗi trong bộ biên dịch biểu thức chính quy.
624 Ký tự escape không hợp lệ. Một ký tự escape là non-NULL với độ dài lớn hơn 1.
625 Lỗi nội bộ. Cố gắng để cập nhật đối tượng thực thể sai %1$d|%2$d|%3$d bản mẫu thông tin thuộc tính với đối tượng thực thể %4$d|%5$d|%6$d.
626 Lỗi nội bộ. %1$d thuộc tính được yêu cầu không tìm thấy.
627 Toán hạng trim không hợp lệ.  Toán hạng trim có độ dài lớn hơn 1.
628 Kiểu tiền tệ(currency) không hợp lệ: %1$d.
629 Kích cỡ trang %1$d không phải là một lũy thừa của 2 hoặc kích cỡ trang quá nhỏ. Kích cỡ trang của %2$d được dùng để thay thế.
630 Chuyển đổi không được hỗ trợ bởi %1$s.
631 Câu lệnh SQL vi phạm ràng buộc NOT NULL.
632 %1$s không phải là một bộ đĩa sao lưu.
633 %1$s là một sao lưu của cơ sở dữ liệu %2$s được tạo trên %3$s thay vì cơ sở dữ liệu đã cho %4$s được tạo trên %5$s
634 Một tệp tin/bộ đĩa cơ sở dữ liệu được mong đợi trong bản sao lưu.
635 CẢNH BÁO: Bể chứa Bộ đệm Bản ghi của bộ đệm %1$d dường như quá nhỏ đối với môi trường hiện tại.\n Có thể một số bộ đệm không được giải phóng bởi hệ thống do một lỗi không rõ \n hoặc có nhiều giao dịch đồng thời bị bỏ dở cùng một lúc. Bể chứa bộ đệm bản ghi được mở rộng với thêm %2$d bộ đệm.
636 CẢNH BÁO: Num_buffers %1$d quá nhỏ. Giá trị nhỏ nhất của %2$d đã được giả định.
637 Lỗi hệ thống: giải phóng một bộ đệm bản ghi quá nhiều. Fix Count được thiết lập về Không.
638 Cảnh báo: Làm sạch một lưu trữ bản ghi không thể cập nhật pageid = %1$d
639 Lỗi hệ thống: Trang trình tự bản ghi trước đó %1$d không phải trang làm sạch bị trì hoãn %2$d.
640 Lỗi hệ thống: một savepoint không thể được thêm khi giao dịch không hoạt động (e.g., trong quá trình cam kết hoặc quay lui).
641 Một tên phải được đưa ra cho một savepoint.
642 Lỗi hệ thống: không có một thao tác hệ thống hàng đầu đang hoạt động cho giao dịch hiện tại.
643 Có thể một lỗi hệ thống: Cam kết/bỏ dở giao dịch = %1$d (chỉ mục= %2$d) mà có thao tác thường trực\n đính kèm với nó. Sẽ đính kèm các thao tác hệ thống đó đến giao dịch
644 LỖI NHẬT KÝ NGHIÊM TRỌNG: %1$s
645 LỖI HỆ THỐNG: Trạng thái = %1$s của giao dịch = %2$d (chỉ mục = %3$d) \n không phải là một trạng thái đúng để áp dụng các hành động trì hoãn/hoàn tác máy khách... bị bỏ qua
646 LỖI HỆ THỐNG: It seems that a compensating record is missing for logical logging rcvindex = %s\n. Without this compensating record crash recovery will not be done correctly
647 Sao lưu "%1$s" không thuộc về cơ sở dữ liệu đã cho.
648 Sao lưu không tương thích với phát hành "%1$s" hiện tại phát hành "%2$s".
649 precision đã cho của %1$d không hợp lệ; nó phải lớn hơn %2$d và không lớn hơn %3$d.
650 Không thể tạo đủ tuyến đoạn cho 'max_clients=%1$d'.
651 Không có bộ nhớ ảo để cấp phát trước bộ đệm phân loại %1$d cho mỗi tuyến đoạn %2$d.
652 Độ lệch sai %1$d được đưa ra để phân chia bản ghi chép của độ dài %2$d được lưu trữ trên khe %3$d
653 Độ lệch sai %1$d hoặc độ dài %2$d được đưa ra để gỡ bỏ một phần của dữ liệu của bản ghi chép của độ dài %3$d được lưu trữ trên khe %4$d
654 Độ lệch sai %1$d hoặc độ dài %2$d được đưa ra để ghi đè lên một phần của dữ liệu của bản ghi chép của độ dài %3$d được lưu trữ trên khe %4$d
655 Không thể mở tệp tin cấu hình bộ xử lý sự kiện, "%1$s".
656 Cảnh báo: Id/name null trong tệp tin cấu hình bộ xử lý sự kiện, "%1$s", dòng %2$d
657 Cảnh báo: Tên sự kiện không hợp lệ, "%1$s", trong tệp tin cấu hình bộ xử lý sự kiện, "%2$s", dòng %3$d
658 Cảnh báo: id sự kiện %1$d, không trong phạm vi id lỗi %2$d <> %3$d, tệp tin "%4$s", dòng %5$d
659 Không thể truy cập chương trình bộ xử lý sự kiện, "%1$s".
660 Không thể mở/kết nối tiến trình bộ xử lý sự kiện, "%1$s".
661 Thất bại ghi vào tiến trình bộ xử lý sự kiện, "%1$s".
662 Thất bại khởi tạo cơ chế xử lý sự kiện.
663 Cảnh báo: Dòng quá dài trong tệp tin cấu hình bộ xử lý sự kiện, "%1$s", dòng %2$d, đang cắt bỏ
664 Cơ chế xử lý sự kiện đã bắt đầu.
665 Pipe bộ xử lý sự kiện đã vỡ, "%1$s"
666 Cơ chế xử lý sự kiện đã dừng.
667 Không thể khớp lớp '%1$s' với lớp C++ tương ứng.
668 Lỗi nội bộ: Kiểu miền '%1$d' không rõ.
669 Server từ chối kết nối máy khách: máy khách tối đa, (%1$d), đã bị vượt quá.
670 Thao tác có thể đã gây ra một hoặc nhiều vi phạm ràng buộc. INDEX %1$s%2$s Ở CLASS %3$s%4$s. phím: %5$s%6$s.
671 Một lỗi ghi/đọc mạng đã xảy ra với socket %1$d gắn kết với giao dịch của bạn (chỉ mục %2$d, %3$s@%4$s|%5$d)
672 Socket %1$d gắn kết với giao dịch của bạn (chỉ mục %2$d, %3$s@%4$s|%5$d) bị đóng.
673 Kết nối của bạn đến server đã bị đóng từ khi máy chủ lập tức đi xuống.
674 Không có đủ tuyến trình đang hoạt động để kết nối với thực thi server hiện tại. Số tuyến đoạn đang hoạt động hiện tại là %1$d. Bạn có thể cần đến khoảng %2$d tuyến đoạn để làm sạch thực thi server trong môi trường của bạn. Chỉ mục giao dịch %3$d tạm ngừng để tiếp tục thực thi server.
675 Không thể đọc tệp tin vị trí cơ sở dữ liệu, '%1$s'.
676 Cơ sở dữ liệu '%1$s', không thể được tìm thấy trong tệp tin vị trí cơ sở dữ liệu, '%2$s'
677 Thất bại kết nối đến server cơ sở dữ liệu, '%1$s', trong (các) máy chủ sau đây: %2$s
678 Lỗi nội bộ: Client restart module received NULL database server name.
679 Giao dịch C++ này đã được bắt đầu.
680 Tìm cách vượt quá cấp độ lồng nhau giao dịch C++ tối đa (%d).
681 Giao dịch %s đã tìm cách để đạt tới một giao dịch C++ mà vẫn chưa được bắt đầu.
682 Không thể %s giao dịch C++ cho đến khi tất cả các giao dịch cấp dưới được hoàn thành.
683 Tìm cách để tạo chuỗi ký tự với độ dài không hợp lệ - %d
684 Phương pháp trả về mã lỗi người dùng tự định nghĩa %d.
685 Lớp "%1$s" không hợp lệ.
686 Thông báo: bộ xử lý sự kiện đã được khởi động lại.
687 Không cho phép ném (%s *) đến to (%s *).
688 Chỉ số dưới nằm ngoài phạm vi. Cố gắng truy cập phần tử %lld. Kích cỡ bộ sưu tập = %lld.
689 Lỗi nội bộ. Iterator chứa trạng thái hỏng.
690 Cố gắng để bỏ tham chiếu một thực thể chưa được khởi tạo của odb_Ref<>.
691 Cố gắng để dùng một iterator không hợp lệ.
692 Cố gắng sử dụng một iterator với một collection không liên quan.
693 Tràn đã phát sinh trên kiểu dữ liệu Numeric.
694 LỖI HỆ THỐNG: Không thể tải B+tree.
695 Giao diện sai đã được dùng để xóa một máy khách.
696 Giao dịch không biết (chỉ mục %1$d, %2$s@%3$s|%4$d).
697 Chỉ mục giao dịch đã cho (index %1$d, %2$s@%3$s|%4$d) không khớp với chỉ mục giao dịch hiện tại (chỉ mục %5$d, %6$s@%7$s|%8$d). Có khả năng, giao dịch đã cho đã kết thúc và chỉ mục giao dịch của nó đã được gắn lại đến giao dịch khác.
698 Lỗi nội bộ: CHỈ MỤC %1$s TRÊN LỚP %2$s (CLASS_OID: %3$d|%4$d|%5$d). số OID trong hệ thống phân cấp duy nhất: %6$d không bằng với số OID: %7$d được tìm thấy trong B+tree duy nhất cộng với số NULL: %8$d được tìm thấy trong hệ thống phân cấp duy nhất cho B+tree: %9$d|%10$d|%11$d.
699 Lỗi nội bộ: CHỈ MỤC %1$s TRÊN LỚP %2$s (CLASS_OID: %3$d|%4$d|%5$d). số OID trong hệ thống phân cấp duy nhất: %6$d không bằng với số OID: %7$d được tìm thấy trong thống kê gốc B+tree duy nhất cho B+tree: %8$d|%9$d|%10$d.
700 Lỗi nội bộ: CHỈ MỤC %1$s TRÊN LỚP %2$s (CLASS_OID: %3$d|%4$d|%5$d). số OID  the number of OIDs: %6$d không bằng với số NULL: %7$d cộng với số khóa: %8$d trong B+tree duy nhất: %9$d|%10$d|%11$d.
701 Không có proxy nào được định nghĩa trên "%1$s" tại ldb "%2$s".
702 Lỗi nội bộ: CHỈ MỤC %1$s TRÊN LỚP %2$s (CLASS_OID: %3$d|%4$d|%5$d). số NULL trong hệ thống phân cấp duy nhất: %6$d không bằng với số NULL: %7$d được tìm thấy trong thống kê gốc B+tree duy nhất cho B+tree: %8$d|%9$d|%10$d.
703 Lỗi nội bộ: CHỈ MỤC %1$s TRÊN LỚP %2$s (CLASS_OID: %3$d|%4$d|%5$d). OID: %6$d|%7$d|%8$d được tìm thấy trong B+tree duy nhất: %9$d|%10$d|%11$d không thuộc về một trong các lớp cho ràng buộc duy nhất.
704 Ước tính hàm số server chung bị thất bại.
705 Bộ đĩa thường trực %1$d không đồng nhất được tìm thấy khi %2$d được mong đợi.
706 Không thể mở rộng bộ đĩa thường trực. Bộ đĩa thường trực "%1$s" với mục đích %2$s đã được đưa ra.
707 Không thể mở rộng bộ đĩa tạm thời "%1$s" với %2$d trang
708 Không thể mở rộng bộ đĩa đĩa tạm thời "%1$s" với %2$d trang (%3$lld Kbytes) vì không đủ không gian. Số trang hiện tại có thể dùng là %4$d trang (%5$lld Kbytes).
709 Không tìm thấy ràng buộc "%1$s".
710 Ràng buộc không hợp lệ.
711 Một đối tượng với giá trị thuộc tính đã cung cấp không tồn tại.
712 Ràng buộc " %1$s" đã tồn tại.
713 Một chỉ mục không tồn tại trong thuộc tính này.
714 Truy vấn thất bại vì không đủ không gian tệp tin tạm thời.
715 Thất bại kiểm nghiệm lược đồ: %1$s
716 Ràng buộc NOT NULL không được cho phép trong các lớp ảo hoặc proxy.
717 Ràng buộc UNIQUE không được cho phép trong các lớp ảo hoặc proxy.
718 Gadget không hợp lệ.
719 Số thuộc tính(%d) và giá trị (%d) không bằng nhau.
720 Không thể dùng một gadget để chèn vào một lớp ảo hay proxy.
721 Chèn kết quả truy vấn nhiều hơn một hàng.
722 Số thuộc tính sai cho ràng buộc NOT NULL.
723 Không thể sửa đổi miền của một thuộc tính đang tồn tại.
724 Tham số không hợp lệ cho sự tạo tên ràng buộc mặc định.
725 Lỗi nội bộ: mong đợi oid tạm thời và gặp phải oid thường trực.
726 Ràng buộc UNIQUE phải được thêm bằng cách sử dụng một câu lệnh Alter tách biệt\nmột khi các thực thể đã được tạo. Ví dụ, thay vì:\n\talter table1 add attribute a int unique;\nuse:\n\talter table1 add attribute a int;\n\talter table1 add unique(a);
727 Sự kiện trigger không hợp lệ.
728 Truy vấn thất bại bởi vì tệp tin tạm thời vfid không hợp lệ cho giao dịch %1$d.
729 Tràn xảy ra trong ngữ cảnh phép trừ.
730 Tràn xảy ra trong ngữ cảnh dấu nhân.
731 Tràn xảy ra trong ngữ cảnh dấu chia.
732 Tràn xảy ra trong ngữ cảnh dầu trừ.
733 Tràn xảy ra trong ngữ cảnh ép buộc.
734 Lỗi nội bộ: Đề mục của tệp tin VFID VFID %1$d|%2$d(Bộ đĩa "%3$s") bị hỏng.
735 Chỉ mục giao dịch %1$d không biết.
736 Phải chỉ định hoặc dịch vụ hoặc mã định danh cổng. Lỗi Winsock: %1$d.
737 Dịch vụ không biết cho TCP: %1$s. Lỗi Winsock: %2$d.
738 Không thể tạo luồng socket. Lỗi Winsock: %1$d.
739 Không thể gắn kết địa chỉ cục bộ... đang thử lại. Lỗi Winsock: %1$d.
740 Không thể gắn kết địa chỉ cục bộ... đang bỏ dở. Lỗi Winsock: %1$d.
741 Chấp nhận thất bại. Lỗi Winsock: %1$d.
742 Đang gửi một thông báo broadcast đến máy khách. Lỗi Winsock: %1$d.
743 Thất bại trên handshake giữa máy khách và server. (peer host %s)
744 Lỗi trong rehashing một proxy/vclass MOP.
745 Giai đoạn thu gom rác thải không hợp lệ.
746 Thu gom rác thải không hợp lệ gọi lại.
747 Gọi lại vé không hợp lệ thu gom rác thải.
748 Con trỏ đối tượng không hợp lệ bị bắt gặp bởi db_gc_mark_object.
749 Số các thực thể lấy về không được khớp: %1$d được mong đợi, %2$d được nhận về.
750 Số các thực thể lấy về không được khớp: %1$d  được mong đợi, %2$d được nhận về.
751 OID của đối tượng mới không có sẵn trong một trigger BEFORE INSERT.
752 Lỗi khôi phục lại sao lưu unit_num %d.
753 Lỗi khôi phục lại sao lưu unit_num %d, pageid %d vượt quá tổng số trang %d cho volid %d.
754 Thực thể proxy OO có non-string-type hoặc null key.
755 %s không được thực thi trong chế độ độc lập.
756 Bộ đệm chuỗi ký tự được chỉ định quá nhỏ để mã hóa đối tượng này.
757 Lỗi nội bộ: không thể gán OID thường trực trong quá trình mã hóa đối tượng.
758 Chuỗi ký tự đã chỉ định không hợp lệ cho giải mã đối tượng.
759 Đối tượng mục tiêu đã bị xóa.
760 Lỗi nội bộ: thực thể đối tượng proxy có khóa NULL trong quá trình giải mã đối tượng.
761 Lỗi nội bộ: thực thể đối tượng ánh xạ đến thực thể thật không hợp lệ.
762 Lỗi nội bộ trong việc giải quyết thực thể lớp ảo đến một thực thể thật.
763 Lỗi nội bộ trong việc mã hóa thực thể đối tượng không thể cập nhật.
764 Lỗi nội bộ trong việc mã hóa đối tượng thánh dạng thức nội tại vobj.
765 Đối số đối tượng đầu ra có một địa chỉ NULL.
766 Lỗi nội bộ trong việc giải mã đối tượng.
767 Không thể tạo một cấp độ sao lưu %d mà không tạo một cấp độ sao lưu %d trước tiên.
768 Cơ sở dữ liệu cục bộ "%1$s" đã được truy cập trong phiên này, sự thay đổi hoặc xóa đều không được phép.
769 Thuộc tính "%s", của lớp "%s" không phải một lớp hoặc thuộc tính dùng chung hợp lệ.
770 Không có hàm số bảng.
771 Tham số không hợp lệ cho hàm số.
772 Không tìm thấy lớp db_serial.
773 Không tìm thấy số sê-ri Serial "%s".
774 Serial "%s" đã tồn tại.
775 next value vượt quá giá trị LỚN NHẤT (NHỎ NHẤT).
776 Không thể lấy về đối tượng serial.
777 Không thể cập nhật đối tượng serial.
778 Không thể tạo một liên kết biểu tượng "%s" đến tệp tin "%s".
779 Kết quả vượt quá giới hạn. Năm trong trường Ngày tháng năm PHẢI trong khoảng từ 1 tới 9999.
780 Hệ thống nội bộ thất bại: không thể lấy được thời gian hệ thống.
781 Chuỗi định dạng quá dài. Nó phải nhỏ hơn 16K.
782 Chuỗi rỗng(empty string) không được phép tại đây.
783 Định dạng không hợp lệ.
784 Hai đối số không khớp.
785 Chuỗi đối số đầu tiên quá dài. Nó phải nhỏ hơn 16K.
786 Định dạng bị trùng lặp.
787 Lỗi chuyển đổi trong định dạng time.
788 Lỗi chuyển đổi trong định dạng timestamp.
789 Số sai.
790 Sự thực thi truy vấn đã bị ngắt bởi người dùng.
791 Giá trị serial không hợp lệ.
792 Không thể cấp phát bộ nhớ cho css.
793 pthread_attr_init() thất bại.
794 pthread_attr_destroy() thất bại.
795 pthread_attr_setdetachstate() thất bại.
796 pthread_attr_setscope() thất bại.
797 pthread_attr_setstacksize() thất bại.
798 pthread_create() thất bại.
799 pthread_join() thất bại.
800 pthread_mutex_init() thất bại.
801 pthread_mutex_destroy() thất bại.
802 pthread_mutex_lock() thất bại.
803 pthread_mutex_trylock() thất bại.
804 pthread_mutex_unlock() thất bại.
805 pthread_mutexattr_init() thất bại.
806 pthread_mutexattr_destroy() thất bại.
807 pthread_mutexattr_settype() thất bại.
808 pthread_mutexattr_gettype() thất bại.
809 pthread_cond_init() thất bại.
810 pthread_cond_destroy() thất bại.
811 pthread_cond_wait() thất bại.
812 pthread_cond_timedwait() thất bại.
813 pthread_cond_signal() thất bại.
814 pthread_cond_broadcast() thất bại.
815 pthread_key_create() thất bại.
816 pthread_key_delete() thất bại.
817 pthread_setspecific() thất bại.
818 pthread_getspecific() thất bại.
819 pthread_once() thất bại.
820 Không thể khởi tạo một kết nối.
821 Tắt một kết nối.
822 Thao tác Mutex thất bại trong khi lấy id máy khách tiếp theo.
823 Thao tác Mutex thất bại trong khi khởi tạo danh sách kết nối.
824 Không thể hủy mutex trong khi hoàn thành danh sách kết nối.
825 Gọi pread() thất bại.
826 Gọi pwrite() thất bại.
827 Chức năng xem xét danh sách kết nối trả về giá trị không hợp lệ.
828 Đoạn mã tới hạn của chỉ mục đã được đưa ra không hợp lệ.
829 Khóa mutex này đã được mở khóa.
830 Không thể cấp phát thêm mục nhập truy vấn nữa. Mục nhập tối đa có thể cấp phát là %d.
831 Tất cả bộ mô tả giao dịch(%d) đang được dùng. Nâng cao tham số max_clients.
832 Sao lưu đang được chạy. Sao lưu không thể được chạy bởi hai tiến trình.
833 Tên chỉ mục "%1$s" mơ hồ. Chỉ định tên lớp của chỉ mục.
834 Có một vài chưa khớp giữa chuỗi source "%1$s" và chuỗi format "%2$s" được đưa ra trong to_number().
835 Mục thống kê chưa định nghĩa.
836 CHỐT TRÊN TRANG(%d|%d) TẠM NGƯNG
837 Không thể xóa người dùng sở hữu các đối tượng cơ sở dữ liệu (lớp/trigger/serial vân vân).
838 Không thể đồng thời mở nhiều hơn %d quét chỉ mục.
839 "%s": Tham số hệ thống chưa biết hoặc giá trị sai.
840 Không thể thay đổi tham số hệ thống "%s".
841 Thao tác được yêu cầu có thể được thực hiện bởi chỉ một khách hàng (giao dịch) duy nhất.
842 Không thể lưu ẩn khóa của đối tượng %1$d|%2$d|%3$d.
843 Đối số sai trong %s, một %s đã được chuyển.
844 Sự cô lập không rõ (%d) được tìm thấy. Chỉ mục giao dịch là %d.
845 Kiểu đối tượng không hợp lệ (%d) được tìm thấy. Đối tượng có thể khóa là %d|%d|%d.
846 %s của giao dịch (%d) không được tìm thấy trong danh sách bộ nắm giữ của một đối tượng(%d|%d|%d).
847 %s trên một %s(%d|%d|%d) không được tìm thấy trong giao dịch(%d) danh sách nắm giữ(cnt=%d).
848 %s trên một đối tượng(%d|%d|%d) không được tìm thấy trong giao dịch(%d) danh sách non2pl.
849 Cố gắng bỏ dở một giao dịch(%d) hai lần.
850 Giao dịch (%d) không phải lock holder hoặc lock waiter của một đối tượng(%d|%d|%d).
851 Thất bại cấp phát tài nguyên khóa (%s).
852 Tổng chế độ holder (%d) của một danh sách lock holder rỗng là không NULL_LOCK.
853 Đối với một đối tượng(%d|%d|%d), giao dịch(%d) của of Uncommitted Read nắm giữ khóa lỗi(%s).
854 CẢNH BÁO: Không có lock holder, nhưng tồn tại lock waiter. tham chiếu đến %s.
855 CẢNH BÁO: Một hoặc nhiều tuyến đoạn là lock-waiting cho một giao dịch (%d).
856 CẢNH BÁO: Trạng thái chờ khóa lạ(%d|%d), tuyến đoạn (%d|%d) và giao dịch (%d).
857 Một lỗi I/O phát sinh khi cắt bỏ trang %1$d của bộ đĩa "%2$s".
858 Bộ đĩa "%1$s" là không biết tại khởi động lại server.
859 CHỐT TRÊN TRANG (%d|%d) ĐÃ BỎ DỞ
860 Không thể thực hiện giao dịch hiện tại như một phần của giao dịch toàn cục bởi vì trạng thái của nó là '%1$s'.
861 Không thể chuẩn bị cam kết giao dịch hiện tại có mã định danh giao dịch %1$d bởi vì giao dịch đó không được thực hiện như một phần của giao dịch toàn cục.
862 Không thể thiết lập thông tin người dùng của giao dịch toàn cục với mã định danh %1$d bởi vì trạng thái của nó là '%2$s'.
863 Không thể có được thông tin người dùng của giao dịch toàn cục với mã định danh %1$d bởi vì nó chưa được thiết lập.
864 Không thể mở tệp tin chi tiết backupdb/restoredb '%s'.
865 Không thể thực thi câu lệnh vì kiểu input marker không biết.
866 Mã lỗi chưa được sử dụng.
867 Mã lỗi chưa được sử dụng.
868 Mã lỗi chưa được sử dụng.
869 Mã lỗi chưa được sử dụng.
870 Mã lỗi chưa được sử dụng.
871 Mã lỗi chưa được sử dụng.
872 Mã lỗi chưa được sử dụng.
873 Lỗi xác thực PKI "%1$s".
874 Đối số của power () nằm ngoài phạm vi.
875 Tràn xảy ra trong ngữ cảnh power.
876 Mã lỗi chưa được sử dụng.
877 Không có dữ liệu nào để được dỡ tải.
878 Tệp tin vị trí cơ cở dữ liệu không hợp lệ, '%1$s'.
879 Không thể có được cho phép: '%s'.
880 Không thể thay đổi cho phép '%s'.
881 Không thể có được tệp tin khóa '%s(ID TỆP TIN:%d)'.
882 Không thể phát hành tệp tin khóa 'ID TỆP TIN:%d'.
883 Thất bại khi lấy trạng thái tệp tin '%s'.
884 Volext-pathname '%s' không được cho phép ghi.
885 Thất bại để di chuyển từ '%s' đến '%s'. Dùng thiết bị(đĩa) giống nhau.
886 "%1$s" gây ra vi phạm ràng buộc duy nhất.
887 Thủ tục lưu trữ '%s' đã tồn tại.
888 Đếm tham số không hợp lệ. mong đợi: %d, thực tế: %d
889 Lỗi thực thi thủ tục lưu trữ: %s
890 Phân vùng thất bại.
891 Phân vùng thích hợp không tồn tại.
892 Khóa chính "%2$s" đã được định nghĩa cho lớp "%1$s".
893 Thuộc tính '%1$s', một thành viên của khóa chính, không thể bị xóa.
894 Hàm số/Thủ tục lưu trữ '%s' không tồn tại.
895 Kiểu thủ tục lưu trữ không hợp lệ : '%s' là '%s'.
896 Nén thất bại. Phương pháp Zip: %d (%s), Cấp độ Zip: %d (%s)
897 Giải nén thất bại.
898 Lỗi nhân bản: "%1$s"
899 Yêu cầu phân vùng không hợp lệ.
900 Không tìm thấy thư viện Java VM: %s. \nVì vậy, không được phép gọi thủ tục lưu trữ java.
901 Java VM không thể được khởi động: %s. \nVì vậy, không được phép gọi thủ tục lưu trữ java.
902 Java VM đang không chạy is not running.
903 Không thể kết nối Can't connect Java VM: %s
904 Tên thủ tục lưu trữ không hợp lệ Invalid stored procedure name.
905 Nối mạng với JVM thất bại: %d
906 Chữ ký phương pháp java không hợp lệ.
907 Chủ sở hữu hoặc một thành viện của nhóm DBA có thể xóa thủ tục lưu trữ.
908 Quá nhiều đối số của thủ tục lưu trữ '%s'.
909 Tìm thấy lớp catalog/lớp ảo thiếu hoặc không hợp lệ.
910 Không được phép truy cập đến phân vùng.
911 Gọi không hợp lệ: nó không thể trả về ResultSet.
912 ResultSet không thể được sử dụng tham số đầu vào.
913 Quá nhiều thủ tục lưu trữ lồng nhau gọi.
914 Một đối tượng serial đã tồn tại như một ràng buộc AUTO INCREMENT.
915 Giá trị tăng của một ràng buộc gia tăng phải lớn hơn 0.
916 Bộ đĩa bản ghi đang hoạt động không được bao gồm trong bộ đĩa sao lưu này (%s).
917 Bộ đĩa bản ghi đang hoạt động đã tồn tại.
918 Lớp '%s' được tham chiếu bởi khóa ngoại không tồn tại.
919 Lớp '%s' được tham chiếu bởi khóa ngoại không có khóa chính.
920 Khóa ngoại '%s' không bao gồm thành viên khóa chính '%s'.
921 Miền của thành viên khóa ngoại '%s' khác với miền của thành viên khóa chính '%s'.
922 Ràng buộc phím bên ngoài '%s' vô hiệu, do giá trị '%s'.
923 Khóa chính '%s' tham chiếu bởi một khóa ngoại '%s' không được bị xóa.
924 Thao tác Update/Delete bị hạn chế bởi khóa ngoại '%s'.
925 Thu khóa cho khóa ngoại '%s' đã thất bại.
926 Thực thể có khóa ngoại '%s' không thể bị xóa.
927 Số khóa của khóa ngoại '%s' khác với số khóa của khóa chính '%s'.
928 Không phải người sử dụng nhưng hệ thống có thể gán thuộc tính '%s' được tạo bởi 'ON CACHE OBJECT' của khóa ngoại.
929 Một khóa ngoại không thể được định nghĩa trong lớp ảo.
930 Có thể xóa thuộc tính '%s' được tạo bởi 'ON CACHE OBJECT' của khóa ngoại chỉ sau khi gỡ bỏ ràng buộc của khóa ngoại.
931 Giá trị bắt đầu của một ràng buộc AUTO INCREMENT phải nhỏ hơn giá trị lớn nhất.
932 Không thể sửa chữa sự không đồng nhất của số trang trống cho bộ đĩa "%1$s". Bởi vì %2$d trang thu được từ đề mục bộ đĩa là không đồng nhất với %3$d trang từ bitmap.
933 Không thể sửa chữa sự không đồng nhất của số khu vực trống cho bộ đĩa "%1$s". Bởi vì %2$d khu vực thu được từ đề mục bộ đĩa là không đồng nhất với %3$d khu vực từ bitmap.
934 Miền NUll được tham chiếu.
935 Đối số của hàm số %s nằm ngoài phạm vi.
936 Tràn đã xảy ra trong ngữ cảnh exp.
937 Lỗi cơ sở dữ liệu.
938 Đối số không hợp lệ.
939 Có quá nhiều kết nối.
940 Xử lý không hợp lệ.
941 Thao tác không được hỗ trợ.
942 Xử lý tạm ngưng chờ cho các khóa.
943 Lỗi Generic.
944 Câu lệnh không được chuẩn bị.
945 Câu lệnh không có tập kết quả.
946 Câu lệnh không được thực thi.
947 Không có thêm tập kết quả nào.
948 Kích cỡ của bộ đệm không đủ.
949 Không có thông tin có sẵn nào.
950 Tên không hợp lệ.
951 Tập kết quả không thể cập nhật.
952 Hàng bị xóa.
953 Tham số của câu lệnh đã chuẩn bị chưa được thiết lập.
954 Câu lệnh không phải là một câu lệnh hàng loạt.
955 Không thể xóa câu lệnh hàng loạt này.
956 Không thể thực thi hàng loạt.
957 Câu lệnh là câu lệnh được chuẩn bị.
958 Câu lệnh không phải câu lệnh được chuẩn bị.
959 Câu lệnh là câu lệnh hàng loạt.
960 Không có thêm lỗi nào.
961 Không có thêm kết quả nào.
962 Không đủ bộ nhớ
963 Lỗi broker.
964 Xử lý tập kết quả bị đóng.
965 Không thể sử dụng tên thuộc tính trùng lặp trong danh sách khóa chỉ mục. Tên thuộc tính '%s' được liệt kê nhiều hơn một lần.
966 Giao dịch của bạn  (chỉ mục %1$d, %2$s@%3$s|%4$d) đã tạm ngưng chờ trên %5$s khóa trên đối tượng %6$d|%7$d|%8$d bởi vì khóa chết. Bạn đang chờ người dùng %9$s hoàn thành.
967 Giao dịch của bạn  (chỉ mục %1$d, %2$s@%3$s|%4$d) đã tạm ngưng chờ trên %5$s khóa trên lớp %6$s bởi vì khóa chết. Bạn đang chờ người dùng %7$s hoàn thành.
968 Giao dịch của bạn  (chỉ mục %1$d, %2$s@%3$s|%4$d) đã tạm ngưng chờ trên %5$s khóa trên thực thể %6$d|%7$d|%8$d của lớp %9$s bởi vì khóa chết. Bạn đang chờ người dùng %10$s hoàn thành.
969 Nền tảng bit server (%1$d) khác với nền tảng bit máy khách (%2$d).
970 Chế độ Server HA được thay đổi từ '%1$s' thành '%2$s'.
971 Chương trình '%1$s' (pid %2$d) được kết nối đến server cơ sở dữ liệu '%3$s' trên máy chủ '%4$s' (cổng %5$d).
972 Chương trình '%1$s' (pid %2$d) đã được kết nối từ máy chủ '%3$s'. (chỉ mục giao dịch %4$d)
973 Trạng thái Server là %1$s.
974 Bản ghi lưu trữ "%1$s" được tạo đến trang lưu trữ từ %2$lld đến %3$lld.
975 Phiên bản hiện tại của nhân bản không cho phép thay đổi nhiều hàng với một câu lệnh UPDATE đơn có thể vi phạm ràng buộc UNIQUE.
976 Lỗi nội bộ: Kích cỡ bảng tràn (kích cỡ được cấp phát: %1$d, kích cỡ được truy cập: %2$d) tại trang bảng tệp tin %3$d|%4$d(bộ đĩa "%5$s")
977 Điểm mốc bắt đầu (mã trang mốc trước: %lld, mã trang làm lại trước: %lld).
978 Điểm mốc kết thúc (mã trang điểm mốc: %lld, mã trang làm lại điểm mốc: %lld).
979 Vòng lặp CONNECT BY trong dữ liệu người dùng.
980 Tuyến đoạn đang chờ trong %1$s nhiều hơn %2$d msec.
981 Giao dịch %1$d đã đạt đến ngưỡng của thống kê giám sát %2$s.
982 Thiết lập ngắt giao dịch %1$d.
983 Thực thể của một lớp OID có thể dùng lại là không thể tham chiếu. Thao tác này không được cho phép trên thực thể không thể tham chiếu.
984 Không thể thiết lập %3$s thành "%6$s" trong khu vực "[%5$s]" khi nó được thiết lập thành "%4$s" trong khu vực "[%2$s]" của tệp tin cấu hình "%1$s".
985 Tên máy chủ trên chuỗi kết nối cơ sở dữ liệu phải được chỉ định khi nhiều máy chủ được thiết lập trong "databases.txt".
986 Tính năng CUBRID heartbeat đã bắt đầu.
987 Tính năng CUBRID heartbeat đã dừng.
988 Sự kiện Node: %1$s.
989 Sự kiện tiến trình: %1$s. %2$s
990 Thực thi lệnh: %1$s. %2$s
991 Flush victim candidates của bộ đệm trang đã bắt đầu.
992 Flush victim candidates của bộ đệm trang đã hoàn thành (đếm: %d).
993 Tràn đã xảy ra trong ngữ cảnh thao tác bit.
994 Câu lệnh không thể là một câu lệnh được chuẩn bị.
995 Câu lệnh được chuẩn bị có tên %1$s không tồn tại.
996 Số lượng giá trị đã được đưa ra không đúng. %1$d giá trị được đưa ra nhưng %2$d giá trị được yêu cầu.
997 Không thể thêm ràng buộc khóa ngoại %1$s đến %2$s đã phân vùng. Hành động tham chiếu mà cập nhật các thuộc tính chưa được hỗ trợ trên các lớp đã phân vùng.
998 Không thể thêm ràng buộc khóa ngoại %1$s trên thuộc tính %2$s. Thuộc tính không thể có một ràng buộc NOT NULL bởi vì nó phải khớp với hành động tham chiếu SET NULL.
999 Không thể tạo bảng '%1$s' giống bảng '%2$s' bởi vì bảng hiện có có các tính năng mà không được trùng lặp (%3$s).
1000 '%1$s' là một LỚP hoặc thuộc tính DÙNG CHUNG nhưng chỉ các thuộc tính bình thường được cho phép trong ngữ cảnh này.
1001 Một tình huống hoặc trạng thái không mong đợi đã được đạt tới. '%1$s'
1002 Giao dịch hiện tại không thể thu được một khóa cơ sở dữ liệu mà nó yêu cầu để tiếp tục xử lý.
1003 Ràng buộc '%1$s' tồn tại nhưng có một kiểu khác.
1004 Xin vui lòng chú ý rằng khi sử dụng ALTER TABLE DROP FOREIGN KEY, CUBRID xóa cả ràng buộc và các chỉ mục liên kết với nó.
1005 Mã lỗi chưa được sử dụng.
1006 Mã lỗi chưa được sử dụng.
1007 Không thể tạo chỉ mục với prefix length trên ràng buộc UNIQUE, FOREIGN KEY, PRIMARY KEY.
1008 Không thể tạo prefix với prefix length trên lớp đã phân vùng.
1009 COMPACTDB đã được bắt đầu.
1010 Không thể định nghĩa chỉ mục với prefix length trên miền "%1$s".
1011 Không thể thêm ràng buộc khóa ngoại %1$s trên thuộc tính SHARED %2$s.
1012 Không thể tạo bảng với kiểu cột NULL.
1013 Thiết lập DEFAULT hoặc SHARED trên %s cột đã gõ vào là không được phép.
1014 Thiết lập NOT NULL trên %s cột đã gõ vào là không được phép.
1015 Thư mục đã chỉ định '%s' không tồn tại.
1016 Lỗi lưu trữ bên ngoài %1$s: %2$s
1017 Đường dẫn cho lưu trữ bên ngoài '%s' là không hợp lệ.
1018 Không thể sao chép tệp tin bên ngoài đến kiểu khác của kho lưu trữ. (nguồn %1$s đích nhận %2$s)
1019 Kho lưu trữ bên ngoài không được khởi tạo bởi vì đường dẫn chưa được chỉ định trong "databases.txt".
1020 Không tìm thấy tệp tin bên ngoài "%s".
1021 Vòng khóa chết được phát hiện. %1$s.
1022 Giá trị "%d" không hợp lệ cho prefix length của chỉ mục.
1023 Mã lỗi bảo lưu cho HA.
1024 Mã lỗi bảo lưu cho HA.
1025 Mã lỗi bảo lưu cho HA.
1026 Mã lỗi bảo lưu cho HA.
1027 log applier: thất bại khi thay đổi trạng thái áp dụng từ '%1$s' đến '%2$s'.
1028 log applier: bản ghi EOF không mong đợi từ bản ghi lưu trữ. LSA: %1$lld|%2$d.
1029 log applier: độ lệch/trang bản ghi nhân bản không hợp lệ. page HDR: %1$1lld|%2$d, final: %3$lld|%4$d, append LSA: %5$lld|%6$d, EOF LSA: %7$lld|%8$d, ha file status: %9$d, is end-of-log: %10$d.
1030 log applier: bản ghi chép nhân bản không hợp lệ. LSA: %1$lld|%2$d, forw LSA: %3$lld|%4$d, backw LSA: %5$lld|%6$d, Trid: %7$d, prev tran LSA: %8$lld|%9$d, type: %10$d.
1031 log applier: thất bại áp dụng bản ghi nhân bản Schema. lớp: "%1$s", lược đồ: "%2$s", Lỗi nội bộ: %3$d.
1032 log applier: thất bại khi áp dụng bản ghi nhân bản Insert. lớp: "%1$s", khóa: "%2$s", Lỗi nội bộ: %3$d.
1033 log applier: thất bại khi áp dụng bản ghi nhân bản Update. class: "%1$s", key: "%2$s", Lỗi nội bộ: %3$d.
1034 log applier: thất bại khi áp dụng bản ghi nhân bản Delete. class: "%1$s", key: "%2$s", Lỗi nội bộ: %3$d.
1035 log applier: kích cỡ bộ nhớ(%1$d MB) của log applier lớn hơn kích cỡ bộ nhớ tối đa (%2$d MB) hoặc đã được tăng lên nhiều hơn 2 lần (%3$d MB). LSA được yêu cầu: %4$lld|%5$d. LSA được cam kết lần cuối: %6$lld|%7$d.
1036 log applier: log applier tự tắt nó bằng tín hiệu.
1037 log writer: log writer tự tắt nó bằng tín hiệu.
1038 log applier: ứng dụng nhật ký bắt đầu. LSA yêu cầu: %1$lld|%2$d. LSA ghi vào lần cuối: %3$lld|%4$d. LSA trả lời ghi vào lần cuối: %5$lld|%6$d.
1039 log writer: log writer đã bắt đầu. chế độ: %1$d.
1040 HA generic: %1$s.
1041 Một chuỗi ký tự đã bị cắt bỏ trong ngữ cảnh của "%1$s".
1042 Cố gắng để tạo một chuỗi ký tự yêu cầu %1$d bytes của bộ nhớ, trong khi tối đa được cho phép là %2$d bytes.
1043 Không thể thay đổi thuộc tính "%1$s". CUBRID không thể thay đổi đặc tính của thuộc tính được yêu cầu nếu lớp có các lớp con.
1044 Không thể thay đổi thuộc tính "%1$s". CUBRID không thể thay đổi các thuộc tính mà là các khóa phân vùng.
1045 Không thể thay đổi thuộc tính "%1$s". CUBRID không thể thay đổi các thuộc tính được sử dụng trong các khóa ngoại (đã được tham chiếu hoặc đang tham chiếu).
1046 Không thể thay đổi thuộc tính "%1$s". CUBRID không thể thay đổi miền của thuộc tính. Thay đổi này của miền không được hỗ trợ.
1047 Không thể thay đổi thuộc tính "%1$s". CUBRID không thể thay đổi miền của thuộc tính. Miền mới có thể không đại diện một cách chính xác tất cả các giá trị hiện có của thuộc tính.
1048 Không thể thay đổi thuộc tính "%1$s". CUBRID hiện tại không thể thay đổi một AUTO_INCREMENT của thuộc tính nếu miền của thuộc tính cũng thay đổi.
1049 Không thể thay đổi thuộc tính "%1$s". CUBRID hiện tại không thể thay đổi miền của thuộc tính. Việc chuyển đổi tên miền là có thể nhưng tính năng này không được cho phép bởi cấu hình.
1050 Không thể thay đổi thuộc tính "%1$s". CUBRID hiện tại không thể thay đổi miền của thuộc tính. Thuộc tính có một chỉ mục trên nó.
1051 Không thể thay đổi thuộc tính "%1$s". CUBRID hiện tại không thể thay đổi thuộc tính để thêm một ràng buộc. Giá trị thuộc tính hiện có có thể không kiểm tra ràng buộc.
1052 Thuộc tính "%1$s" đã không được thay đổi. CUBRID đã không phát hiện bất kỳ thay đổi có hiệu lực nào trong đại diện của thuộc tính.
1053 The statements REPLACE INTO... and INSERT ... ON DUPLICATE KEY UPDATE are not allowed for classes that are part of an inheritance chain.
1054 Giá trị mới của một ràng buộc AUTO INCREMENT phải lớn hơn hoặc bằng với giá trị hiện tại.
1055 Giá trị mới của một ràng buộc AUTO INCREMENT phải nhỏ hơn giá trị tối đa.
1056 Để tránh sự mơ hồ, tùy chọn bảng AUTO_INCREMENT yêu cầu bảng có chính xác một cột AUTO_INCREMENT và không có đặc tả khởi đầu/gia tăng.
1057 Không thể thêm một thuộc tính đã gõ '%s' NOT NULL mà không có một giá trị DEFAULT.
1058 ALTER TABLE .. CHANGE : đang thay đổi thành miền mới : đổi kiểu thất bại, cấu hình hiện tại không cho phép cắt bỏ hoặc tràn.
1059 ALTER TABLE .. CHANGE : đang thay đổi thành miền mới : đổi kiểu thất bại, giá trị mặc định của miền mới đã được thiết lập.
1060 ALTER TABLE .. CHANGE : đang thay đổi thành miền mới : đổi kiểu thất bại, giá trị nhỏ nhất của miền mới đã được thiết lập.
1061 ALTER TABLE .. CHANGE : đang thay đổi thành miền mới : đổi kiểu thất bại, giá trị lớn nhất của miền mới đã được thiết lập.
1062 Không thể thay đổi thuộc tính "%1$s". Thay đổi sẽ dẫn đến nhiều khóa chính được định nghĩa.
1063 Không thể thêm ràng buộc NOT NULL cho thuộc tính "%1$s": có giá trị NULL đang tồn tại cho thuộc tính này.
1064 ALTER TABLE .. CHANGE : thêm NOT NULL : tất cả giá trị NULL đang tồn tại đã được thay đổi hard default của miền.
1065 Không thể thay đổi thuộc tính "%1$s". CUBRID không thể thêm ràng buộc NOT NULL : một giá trị hard default không tồn tại cho miền mới.
1066 Phiên đã quá hạn.
1067 Cột '%s' không thể có một ràng buộc NOT NULL và một giá trị DEFAULT NULL.
1068 Độ dài trigger tối đa không thể vượt quá %d.
1069 Quá nhiều câu lệnh được chuẩn bị.
1070 Biến số phiên '@%s' chưa được định nghĩa.
1071 Quá nhiều biến số phiên.
1072 Địa chỉ IP(%1$s) chưa được cấp phép.
1073 Định đạng không hợp lệ trong tệp tin(%1$s).
1074 Không thể đọc tệp tin danh sách truy cập (%1$s).
1075 Quét chỉ mục giảm dần bị bỏ dở vì ưu tiên thấp hơn trên B+tree với mã định danh chỉ mục: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
1076 Không thể tải tham số hệ thống.
1077 Tham số '%s' tại dòng %d trong tệp tin '%s' : Tham số không biết
1078 Ký tự ở vị trí chuyển vị %d không có giá trị với bộ mã hiện tại.
1079 Các bộ mã của cơ sở dữ liệu và cấu hình server không tương thích.
1080 Chiều dài chuỗi xác nhận lọc tối đa phải bằng %1$d.
1081 Không thể thay đổi thuộc tính của bộ lọc hoặc chỉ mục chức năng.
1082 Tất cả các tấm đệm trang đều bẩn (LRU: %1$d).
1083 Thông tin cụ thể về khóa chết.\n%s
1084 Bỏ qua trang vô hiệu tại điểm mốc. (page id: %1$d, "%2$s", oldest_unflush_lsa: %3$lld|%4$d, LSA làm lại tại điểm redo lsa: %5$lld|%6$d)
1085 Không thể biên dịch cách diễn đạt thông thường: '%s'
1086 Không thể chạy cách diễn đạt thông thường: '%s'
1087 Nhật ký kích hoạt sao lưu '%s' bắt đầu.
1088 Nhật ký kích hoạt sao lưu '%s' kết thúc.
1089 Phân bổ tài nguyên thất bại. Tăng lên 'max_bestspace_entries=%1$d'.
1090 Khởi động tại chỗ: %s.
1091 Khởi tạo tại chỗ: %s.
1092 Không thể soạn thảo báo cáo loại '%s' với các biến chủ.
1093 Phần arg '%s' cho inet_aton không phải là một địa chỉ ipv4 có định dạng chuỗi hợp lệ.
1094 Phần arg '%lld' cho inet_ntoa không phải là một địa chỉ ipv4 có định dạng số hợp lệ.
1095 Tên người dùng quá dài.
1096 Thay đổi kế hoạch phân mảng không được cho phép khi hạng phân mảng được tham chiếu bởi một phím bên ngoài.
1097 Cột '%s' không thể có ràng buộc PRIMARY KEY và giá trị mặc định NULL.
1098 Thiết lập cục bộ cho ngôn ngữ '%s' không khả dụng với bộ ký tự '%s'.
1099 Tuyên bố DDL không được cho phép bởi cấu hình (block_ddl_statement=yes).
1100 Tuyên bố không có mệnh đề Ở WHERE không được cho phép bởi cấu hình (block_nowhere_statementiyes).
1101 Giao dịch hiện tại không thể lấy được khóa phím trên PSEUDO-OID (%1$d|%2$d|%3$d) đính kèm với OID (%4$d|%5$d|%6$d)
1102 Không biết hệ thống tham số hoặc giá trị xấu.
1103 Không thể thay đổi tham số hệ thống.
1104 Slow query (%d msec)\n%s
1105 Internal error: partially failed to flush dirty object. 
1106 Multiple rows in source table match the same row in destination table.
1107 Invalid bucket number for NTILE. Should be greater than zero.
1108 Lỗi sau cùng

$set 6 MSGCAT_SET_INTERNAL
1 Lỗi trong các lỗi hệ thống con (dòng %d):
2 Không có sẵn thông báo lỗi nào.
3 Không thể cấp phát %d bytes.
4 Không thể tìm catalog thông báo "%s".
5 Thiếu thông báo cho mã lỗi %d.
6 ... ABORT/EXIT IMMEDIATELY ...\n
7 Bạn có muốn thoát không? 1/0
8 Tệp tin không biết.
9 Không có sẵn thông báo nào; thông báo này thay thế cho nguyên bản đã bị định dạng nhầm lẫn.
10 er_init: *** Giá trị exit_ask không đúng = %d; sẽ mặc nhận %s thay thế. ***\n
11 er_start: *** CẢNH BÁO: Không thể mở bản ghi thông báo "%s"; sẽ mặc nhận stderr thay thế. ***\n
12 er_study_fmt: suspect message cho mã lỗi %d.
13 er_estimate_size: mã chuyển đổi không rõ  (%d).
14 \n\n*** Tự động xuống dòng bản ghi thông báo. Thông báo sẽ tiếp tục tại đầu của tệp tin. ***\n\n
15 \nTime: %s - %s *** %s MÃ = %d, Tran = %d%s\n%s\n
16 CUBRID (pid: %d) *** %s *** %s MÃ = %d, Tran = %d %s
17 \nTime: %s - %s *** file %s, dòng %d %s MÃ = %d Tran = %d%s\n%s\n
18 CUBRID (pid: %d) *** %s *** tệp tin %s, dòng %d, %s MÃ = %d, Tran = %d. %s
19 \n*** Thông báo lỗi trước đó là cái cuối cùng. ***\n\n
20 \nTime: %s - DEBUG *** tệp tin %s, dòng %d\n
21 Mail -s "Server đã được dừng"
22 --->>>\n %s đã được dừng theo yêu cầu của bạn khi các lỗi sau đây được thiết lập:\n errid = %d, %s\nUser: %s, pid: %d, host: %s, time: %s<<<---\n
23 %s đã được dừng trên errid = %d. User: %s, pid: %d
24 er_init: không thể cài đặt event handler "%s"\n

$set 7 MSGCAT_SET_PARSER_SYNTAX
1 xin vui lòng kiểm tra cú pháp tại '%s', mong đợi %s.
2 xin vui lòng kiểm tra cú pháp tại '%s', mong đợi %s in %s.
3 xin vui lòng kiểm tra tính chính xác của mã thông báo '%s'.
4 'read uncommitted schema' không thể chấp nhận, dùng \n'read committed schema' để thay thế.
5 Độ dài chuỗi ký tự char tối đa là 16383.
6 %s vượt quá giới hạn của %lld.
7 Độ dài chuỗi bit tối đa là %d bits.
8 Không thể dùng truy vấn con trong mệnh đề điều kiện gia nhập.
9 Không thể chỉ định thuộc tính "(%s)" trong mệnh đề đặc quyền update của câu lệnh grant/revoke.
10 sử dụng từ khóạ '%s' không hợp lê, mong đợi %s.
11 Độ dài mã thông báo quá dài.
12 Kiểu dữ liệu không hợp lệ
13 câu lệnh attach không hợp lệ\n  attach_statement: ATTACH unsigned_integer
14 câu lệnh prepare không hợp lệ\n  prepare_statement:  PREPARE {to} COMMIT unsigned_integer
15 câu lệnh execute không hợp lệ\n  execute_statement:  EXECUTE DEFERRED TRIGGER [identifier {, identifier}* | ALL TRIGGERS]
16 câu lệnh scope không hợp lệ\n  scope_statement: SCOPE trigger_action { FROM table_spec_list }
17 set system parameter không hợp lệ\n  set_system: SET SYSTEM PARAMETERS 'STRING' {, 'STRING_N'}*
18 set transaction không hợp lệ\n  set_transaction: SET TRANSACTION ISOLATION LEVEL expression {, expression}\n                 | SET TRANSACTION LOCK TIMEOUT timeout_spec
19 invalid set trigger trace không hợp lệ\n  set_trigger: SET TRIGGER TRACE [ON|OFF|unsigned_integer|parameter|host_param]
20 set trigger depth  không hợp lệ\n  set_trigger: SET TRIGGER {MAXIMUM} DEPTH [unsigned_integer|parameter|host_param]
21 set optimization level không hợp lệ\n  SET OPTIMIZATION LEVEL {=|to} [ON|OFF|unsigned_integer|parameter|host_param]
22 set optimization cost không hợp lệ\n  SET OPTIMIZATION COST {of} 'STRING' {=|to} literal_
23 get optimization level không hợp lệ\n  GET OPTIMIZATION LEVEL {[INTO|TO] [host_param|parameter|identifier]}
24 get optimization cost không hợp lệ\n  GET OPTIMIZATION COST {of} 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
25 get transaction isol không hợp lệ\n  GET TRANSACTION ISOL LEVEL {[INTO|TO] [host_param|parameter|identifier]}
26 get transaction lock không hợp lệ\n  GET TRANSACTION LOCK TIMEOUT {[INTO|TO] [host_param|parameter|identifier]}
27 get trigger trace không hợp lệ\n  GET TRIGGER TRACE {[INTO|TO] [host_param|parameter|identifier]}
28 get trigger depth không hợp lệ\n  GET TRIGGER {MAXIMUM} DEPTH {[INTO|TO] [host_param|parameter|identifier]}
29 get stat không hợp lệ\n  GET STATISTICS 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
30 create index không hợp lệ\n  CREATE {REVERSE} {UNIQUE} INDEX identifier ON {only} class_name index_column_name_list {WHERE where_clause}
31 create user không hợp lệ\n  CREATE USER identifier {PASSWORD 'STRING'} {GROUPS identifier {, identifier}* } {MEMBERS identifier {, identifier}* }
32 create trigger không hợp lệ\n  CREATE TRIGGER identifier {STATE [ACTIVE|INACTIVE]} {PRIORITY unsigned_real}\n    [BEFORE|AFTER|DEFERRED] event_spec {IF trigger_condition}\n    EXECUTE {AFTER|DEFERRED} trigger_action
33 create serial không hợp lệ\n  CREATE SERIAL identifier {START WITH integer} {INCREMENT BY integer}\n    serial_min_max {CYCLE|NOCYCLE} {CACHE unsigned_integer|NOCACHE}
34 create procedure không hợp lệ\n  CREATE PROCEDURE identifier '(' {sp_param_list} ')' {IS|AS} LANGUAGE JAVA NAME 'STRING'
35 create function không hợp lệ\n  CREATE FUNCTION identifier '(' {sp_param_list} ')' RETURN {data_type|CURSOR} {IS|AS} LANGUAGE JAVA NAME 'STRING'
36 groups không hợp lệ\n  groups spec: GROUPS identifier {, identifier}*
37 members không hợp lệ\n  member spec: MEMBERS identifier {, identifier}*
38 password không hợp lệ\n  password spec: PASSWORD 'STRING'
39 thiếu authorized command list\n  authorized command = SELECT, INSERT, INDEX, DELETE, UPDATE, ALTER, ADD, DROP, EXECUTE, REFERENCES, ALL PRIVILEGES, ALL
40 thiếu class spec list
41 thiếu identifier list
42 thiếu expression
43 identifier không hợp lệ
44 sử dụng substring không hợp lệ\n  SUBSTRING( expression FROM expression FOR expression )\n  SUBSTRING( expression FROM expression )
45 sử dụng trim không hợp lệ\n  TRIM ( [LEADING|TRAILING|BOTH] expression FROM expression )\n  TRIM ( [LEADING|TRAILING|BOTH] FROM expression )\n  TRIM ( expression FROM expression )\n  TRIM ( expression )
46 sử dụng cast không hợp lệ\n  CAST ( expression AS data_type )
47 hàm số add_months cần có hai tham số
48 octet_length cần có một tham số
49 bit_length cần có một tham số
50 lower cần có một tham số
51 upper cần có một tham số
52 translate cần có ba tham số
53 replace cần có hai hoặc ba tham số
54 sử dụng to_number không hợp lệ\n  TO_NUMBER ( expression , char_string )\n  TO_NUMBER ( expression )
55 toán tử gắn kết chuỗi không phải là '|' mà là '||'
56 câu lệnh alter không hợp lệ
57 câu lệnh drop không hợp lệ
58 câu lệnh create không hợp lệ
59 toán tử relational không hợp lệ
60 toán tử equal không phải là '==' mà là '='
61 toán tử not equal không phải là '!=' mà là '<>'
62 giá trị âm không hợp lệ (hoặc giá trị âm trong danh sách)
63 reference rule bị trùng lặp
64 SYS_CONNECT_BY_PATH cần có hai tham số và tham số thứ hai phải là một chuỗi ký tự.
65 unsigned integer %s không hợp lệ.
66 IF cần có ba tham số: tham số đầu tiên phải là một biểu thức logic, tham số thứ hai và thứ ba phải là biểu thức ước tính bằng số hoặc bằng kiểu chuỗi ký tự.
67 IFNULL cần có hai tham số mà phải là biểu thức ước tính bằng số hoặc bằng kiểu chuỗi ký tự.
68 sử dụng isnull không hợp lệ\n  ISNULL ( expression )
69 sử dụng left không hợp lệ\n  LEFT ( str , len )
70 sử dụng right không hợp lệ\n  RIGHT ( str , len )
71 sử dụng mod không hợp lệ\n MOD( expression, expression )
72 sử dụng adddate không hợp lệ\n ADDDATE( expression, expression )
73 sử dụng date_add không hợp lệ\n DATE_ADD( expression, INTERVAL expression unit)
74 sử dụng subdate không hợp lệ\n SUBDATE( expression, expression )
75 sử dụng date_sub không hợp lệ\n DATE_SUB( expression, INTERVAL expression unit)
76 sử dụng user không hợp lệ / system_user\n USER() / SYSTEM_USER()
77 sử dụng timestamp không hợp lệ\n TIMESTAMP( expression ), TIMESTAMP( expression, expression )
78 sử dụng database không hợp lệ\n DATABASE()
79 sử dụng current_time không hợp lệ\n CURRENT_TIME()
80 sử dụng current_date không hợp lệ\n CURRENT_DATE()
81 sử dụng current_timestamp không hợp lệ\n CURRENT_TIMESTAMP()
82 sử dụng now không hợp lệ\n NOW()
83 sử dụng default không hợp lệ\n DEFAULT(column_name)
84 sử dụng date không hợp lệ\n DATE(expression)
85 sử dụng localtime không hợp lệ\n LOCALTIME()
86 sử dụng localtimestamp không hợp lệ\n LOCALTIMESTAMP()
87 sử dụng schema không hợp lệ\n SCHEMA()
88 sử dụng truncate không hợp lệ\n TRUNCATE( expression, expression )
89 sử dụng str_to_date không hợp lệ\n STR_TO_DATE( expression, string_literal )
90 sử dụng current_datetime không hợp lệ\n CURRENT_DATETIME()
91 sử dụng insert không hợp lệ\n INSERT( expression, expression , expression, expression )
92 sử dụng elt không hợp lệ\n ELT(expression [, expression]...)
93 sử dụng year không hợp lệ\n YEAR( expression )
94 sử dụng month không hợp lệ\n MONTH( expression )
95 sử dụng day không hợp lệ\n DAY( expression )
96 sử dụng hour không hợp lệ\n HOUR( expression )
97 sử dụng minute không hợp lệ\n MINUTE( expression )
98 sử dụng second không hợp lệ\n SECOND( expression )
99 sử dụng quarter không hợp lệ\n QUARTER( expression )
100 sử dụng week không hợp lệ\n WEEK( expression ), WEEK( expression, expression)
101 sử dụng time không hợp lệ\n TIME(expression)
102 sử dụng group_concat không hợp lệ\n GROUP_CONCAT (([DISTINCT] {col | expr} \n [ORDER BY {col | unsigned_int} [ASC | DESC] ] \n [SEPARATOR str_val])
103 sử dụng gợi ý chỉ mục vô hiệu
104 Chiều dài chuỗi bằng byte tối đa bằng %d byte.
105 Lỗi cú pháp: unexpected
106 , expecting '{'
107 , expecting SELECT or '('
108 , expecting
109 unexpected
110 END OF STATEMENT
111 In line %d, column %d before '%c%s'\n
112 In line %d, column %d before %s\n
113 before ' %s'\n
114 before END OF STATEMENT\n
115 tên đặt vô hiệu\n  set_names: SET NAMES charset [{COLLATE | COLLATION} collation]\n
116 sử dụng chr không hợp lệ\n  CHR ( number [using charset] )\n
117 sử dụng charset không hợp lệ\n  CHARSET ( expression )\n
118 sử dụng collation không hợp lệ\n  COLLATION ( expression )\n

$set 8 MSGCAT_SET_PARSER_SEMANTIC
1 Không đủ bộ nhớ.
2 Hàm số %s chưa được định nghĩa.
3 Không thể tìm thấy ldb '%s' trong cơ sở dữ liệu.
4 Số lượng cột, %d, trong truy vấn bên trái không khớp với\nsố cột, %d, trong truy vấn bên phải.
5 '%s' không phải union tương thích với '%s'.
6 %s chưa được định nghĩa.
7 Miền của %s không thể là một lớp ảo.
8 Miền của %s không thể là một lớp ảo proxy.
9 Miền của %s không thể là một lớp.
10 %s không phải là một thuộc tính của %s.
11 %s không thể được thay đổi từ lớp này.\nNó đã được thừa kế từ %s.
12 Lớp %s không tồn tại.
13 %s không phải là một %s.
14 %s không phải một phương pháp hoặc thuộc tính của %s.
15 %s không có hoặc không được có bất kỳ đặc tả truy vấn nào.
16 %s chưa được định nghĩa trong cơ sở dữ liệu.
17 %s không phải một lớp cao cấp của %s.
18 Class %s thừa kế từ  non-class %s.
19 VClass %s thừa kế từ non-vclass %s.
20 Lớp ảo Proxy %s thừa kế từ lớp ảo non-proxy %s.
21 %s đã là một lớp cao cấp của %s.
22 %s có thể không phải là một lớp cao cấp của chính nó.
23 %s đã là một lớp con của %s.
24 %s không phải giá trị đơn. Các thuộc tính được hiện ra trong\ncác truy vấn Aggregate cũng phải xuất hiện trong mệnh đề Group by.
25 Số cột(%d) và into_names(%d) không bằng nhau.
26 Lớp %s đã tồn tại.
27 Lớp cao cấp %s trong '%s' không tồn tại.
28 Mong đợi một mệnh đề SUBCLASS để khiến cho mệnh đề INHERIT '%s' có nghĩa.
29 Mệnh đề INHERIT '%s' tham chiếu một lớp mà không nằm trong mệnh đề SUBCLASS '%s'.
30 Lớp không thể có một đặc tả truy vấn.
31 %s không phải một ldb đã đăng ký.
32 Một thừa kế proxy có thể không có mệnh đề object_id của chính nó. Nó không thể ghi đè lên thuộc tính object_id được thừa kế của nó.
33 Lớp ảo Proxy có nhiều hơn một đặc tả truy vấn.
34 Số thuộc tính, %d, không khớp với số cột, \n%d, trong đặc tả truy vấn.
35 Thiếu một tên thuộc tính được mong đợi cho %s.
36 Thuộc tính '%s' không tương thích với cột '%s' của đặc tả truy vấn.
37 %s không phải một lớp.
38 Các thư mục có thể không được tạo trên các lớp ảo.
39 Các thư mục chỉ có thể được tạo trên một thuộc tính đơn.
40 Sử dụng của nhiều lớp trong mệnh đề FROM không hợp lệ.
41 Lỗi hệ thống -- Resolution thất bại trên %s.
42 Bạn có thể không xóa từ một derived table.
43 Người dùng %s không ở trong cơ sở dữ liệu.
44 Update không thể có aggregation.
45 Tên thuộc tính trong mỗi lớp phải là duy nhất. Tên thuộc tính '%s' trong lớp '%s' được chỉ định nhiều hơn một lần.
46 Có nhiều phép gán đến %s.
47 Số trong sort spec không phải là một số nguyên.
48 %d trong sort spec nằm ngoài phạm vi.
49 Order by item không phải một số nguyên.
50 %s trong sort spec không khớp với bất kỳ cột truy xuất nào.
51 Có mâu thuẫn hướng sắp xếp cho cột %s.
52 %s trong sort spec không phải là một số, tên hoặc path expression.
53 Insert expression có thể chỉ xuất hiện trong câu lệnh Insert.
54 Số thuộc tính(%d) và cột truy xuất(%d) không bằng nhau.
55 Số thuộc tính(%d) và giá trị(%d) không bằng nhau.
56 Dữ liệu tràn trong kiểu dữ liệu %s.
57 Kiểu dữ liệu không tương thích trên thuộc tính %s.
58 %s\n không phải một SELECT từ lớp đơn.
59 `*' không phải một danh sách truy xuất hợp lệ trong một đặc tả truy vấn lớp ảo proxy.
60 %s không phải là Lớp ảo hoặc lớp ảo proxy.
61 Có nhiều hơn một mệnh đề isolation level.
62 Có nhiều hơn một mệnh đề timeout.
63 Không có isolation level nào được nhập.
64 Toán tử BETWEEN không được định nghĩa trên kiểu %s, %s và %s.
65 Toán tử '%s' không được định nghĩa trên kiểu %s và %s.
66 Function phải có đối số: %s.
67 Toán hạng không tương thích: %s(%s).
68 Aggregate function phải có 1 đối số: %s.
69 Tìm cách chia cho Không.
70 Dữ liệu tràn ép buộc %s đến kiểu %s.
71 Không thể ép buộc %s đến kiểu %s.
72 Kiểu của '%s' phải là SET, MULTISET, hoặc SEQUENCE.
73 '%s' phải có chính xác một kiểu tập hợp phần tử.
74 %s có nhiều hơn một tên derived column.
75 %s không có tên derived column.
76 %s không có một số cột (%d) và tên cột (%d) tương ứng.
77 Không có thuộc tính trong %s.
78 Mong đợi bảng đơn trong %s.
79 Tên hiện ra %s là mơ hồ is ambiguous.
80 Tên Derived column '%s' không được phép cho đặc tả lớp đơn giản.
81 Derived table '%s' phải có một tên tương quan.
82 Cấp phép Select thất bại, lớp %s.
83 Tham chiếu thực thể lớp %s đến một tên derived table là không được cho phép.
84 Path correlation (%s) xuất hiện bên ngoài một path term bên trong.
85 Tham chiếu đến %s không rõ ràng.
86 Lớp của %s không có thuộc tính %s.
87 Các lớp (%s) không phải tất cả đều có thuộc tính %s.
88 Lớp (%s) không có thuộc tính %s.
89 %s là một kiểu %s, không phải một kiểu đối tượng.
90 Selector {%s} không giải quyết đến một đặc tả lớp đã hiện ra trong\n ngữ cảnh được yêu cầu (%s).
91 Selector {%s} tham chiếu đến đặc tả non-class (%s).
92 Selector {%s} không thể làm nguồn path expression tự định nghĩa nó.
93 Selector {%s} được định nghĩa vòng tròn trong các mục của of selector {%s}.
94 Selector {%s} được định nghĩa lại tại %s.
95 %s (đối tượng miền) không có thuộc tính %s.
96 %s trong EXCEPT spec không phải một lớp con.
97 ALL ... EXCEPT danh sách ngoại trừ EVERYTHING trong lớp.
98 '%s' là không đồng nhất với\nngười dùng khác của path selector '%s' hoặc của path root '%s'.
99 Đối số chưa định nghĩa.
100 Đối số không phải là một đối tượng.
101 %s không phải một phương pháp lớp hay thuộc tính của %s.
102 Đặc tả truy vấn không được có một mệnh đề Into.
103 Đặc tả truy vấn Proxy phải không có biểu thức Aggregate,\n mệnh đề group by, và không có mệnh đề having.
104 Đặc tả truy vấn Proxy phải không có từ khóa DISTINCT hoặc UNIQUE \ntrong danh sách truy xuất.
105 Đặc tả truy vấn Proxy phải là một câu lệnh SELECT đơn giản.
106 Đặc tả truy vấn Proxy phải tham chiếu chỉ một bảng hoặc lớp \ntừ cơ sở dữ liệu cục bộ.
107 Người dùng '%s' không phải là chủ sở hữu của %s.
108 Proxy '%s' đã được định nghĩa trên lớp '%s' của ldb '%s'.
109 CUBRID không hỗ trợ proxy's với đối tượng trong các heterogeneous set.
110 Mệnh đề Where phải là một biểu thức kiểu logic.
111 Mệnh đề Like Escape phải là một chuỗi ký tự hoặc một biến số máy chủ.
112 Kết hợp của precision (%d) và scale (%d) không hợp lệ.
113 Precision đã cho của %1$d không hợp lệ; nó phải lớn hơn %2$d và không lớn hơn %3$d.
114 Precision (%d) quá lớn. Precision tối đa là  is %d.
115 Phương pháp %s phải có một kiểu trả về.
116 Số đối số được cung cấp đến phương pháp không bằng với \nsố được định nghĩa cho phương pháp.
117 Gọi phương pháp phải có một mục tiêu.
118 Phương pháp %s chưa được định nghĩa cho mục tiêu đã cung cấp.
119 Phương pháp thực thể không thể được gọi trên các đối tượng lớp.
120 Phương pháp lớp không thể được gọi trên đối tượng thực thể.
121 Các phương pháp lớp đòi hỏi một đối tượng lớp như mục tiêu của chúng.
122 Các phương pháp đòi hỏi một đối tượng như mục tiêu của chúng.
123 Phương pháp %s chưa được gõ đầy đủ, kiểm tra định nghĩa phương pháp trong lược đồ lớp.
124 Số học thời gian hụt.
125 Số học ngày tháng hụt.
126 Phía bên tay trái của mệnh đề Assignment không hợp lệ.
127 Phương pháp gọi trên các set derived table tương quan với một phạm vi bên ngoài \nchưa được hỗ trợ trừ khi set expression bị ngắt trong một truy vấn con.
128 Cột đặt tả truy vấn tương ứng với \nthuộc tính dùng chung '%s' phải là NULL hoặc NA.
129 Một cột NULL chưa được cho phép trên danh sách truy xuất của khung nhìn đặc tả truy vấn nếu định nghĩa thuộc tính bị bỏ sót.
130 Đối tượng Vclass trong tập hợp không được hỗ trợ.
131 Mong đợi một giá trị chuỗi.
132 Mong đợi một câu lệnh hoặc một giá trị chuỗi.
133 Thuộc tính Proxy object_id '%s' không được ánh xạ đến \n một biểu thức cột đặc tả truy vấn.
134 Ràng buộc %s bị bỏ qua.
135 Khai báo ràng buộc không được phép trong ngữ cảnh này.
136 Đổi kiểu %s đến kiểu %s không được hỗ trợ.
137 %s không phải là một chuỗi bit hợp lệ.
138 Toán tử '%s' được định nghĩa trên kiểu %s.
139 Toán tử '%s' chưa được định nghĩa trên kiểu %s với %s và %s.
140 Các toán hạng của toán tử '%s' không thể cùng là các biến máy chủ động.
141 Quá nhiều đối số đến hàm số generic %s.\nChỉ đối số %d được cho phép cho hàm số generic.
142 Selector lớp {%s} '%s' không phải một lớp con của '%s'.
143 Không thể giải nén trường '%s' từ kiểu %s.
144 Đặc tả truy vấn không được có một mệnh đề ORDER BY.
145 Điều kiện Case phải là một biểu thức kiểu logic.
146 Biểu thức phải là của kiểu %s.
147 Serial '%s' đã tồn tại.
148 Serial '%s' chưa được định nghĩa.
149 Giá trị tăng dần không được là Không.
150 Giá trị lớn nhất vượt quá 1.0e37.
151 Giá trị nhỏ nhất không hợp lệ.
152 Giá trị tăng dần không hợp lệ.
153 Giá trị nhỏ nhất vượt quá -1.0e36.
154 Giá trị lớn nhất không hợp lệ.
155 Chỉ định tùy chọn sửa đổi serial.
156 Tham số thứ hai của chức năng serial_next_value nên lớn hơn 0.
157 Giá trị Serial không thể vượt quá giá trị lớn nhất hoặc nhỏ nhất của nó
158 Lỗi I/O nội tại cho serial
159 Không thể tìm thấy chỉ mục '%s'.
160 Tên chỉ mục đủ điều kiện '%s' tham chiếu đến một lớp, nhưng nó không được chỉ định trong mệnh đề From.
161 Một hoặc nhiều lớp có chỉ mục giống nhau '%s'. Dùng các tên chỉ mục đủ điều kiện.
162 Sử dụng %s không hợp lệ.Biểu thức \n%s không thể được dùng với thuộc tính hoặc truy vấn con liên quan.
163 Phát hành hiện tại hạn chế một biểu thức chứa ORDERBY_NUM() trong danh sách truy xuất.
164 Path expression '%s' trong điều kiện gia nhập.
165 '%s' trong điều kiện gia nhập chưa được định nghĩa.
166 Phát hành hiện tại hạn chế set derived table cho quyền gia nhập bên ngoài.
167 Không thể chèn truy vấn trên ldb khác.
168 Chỉ một cột có thể được chỉ định trong danh sách truy vấn khi truy vấn con không được giới thiệu với EXISTS.
169 Phía bên tay phải của một mệnh đề Assignment không hợp lệ. Ngữ cảnh đòi hỏi truy vấn là cột đơn.
170 Không thể dùng một truy vấn con trong mệnh đề điều kiện gia nhập.
171 Định nghĩa phân vùng không hợp lệ.
172 Cột phân vùng phải được chỉ định.
173 Chỉ một cột có thể được chỉ định như cột phân vùng.
174 Kiểu cột phân vùng không được hỗ trợ.
175 Định nghĩa phân vùng bị trùng lặp. '%s'
176 Không khớp kiểu hằng số. '%s'
177 Kích cỡ phân vùng không hợp lệ.
178 '%s' không phải là lớp được phân vùng.
179 '%s' đã là lớp được phân vùng.
180 Yêu cầu phân vùng không hợp lệ.
181 %s không thể được thay đổi từ lớp này (cột khóa phân vùng).
182 %s không thể được quản lý phân vùng dưới trigger đang hoạt động.
183 Phân vùng Partition %s không tồn tại.
184 Kiểu phân vùng không khớp. '%s'
185 Thông tin phân vùng không hợp lệ.
186 Thêm phân vùng để được thiết lập với một phạm vi lớn hơn hiện có.
187 Việc sắp xếp lại của phân vùng không được cho phép bởi vì nó có thể dẫn đến mất dữ liệu.
188 Phạm vi của phân vùng '%s' không hợp lệ.
189 Một thuộc tính lớp không thể có một ràng buộc AUTO INCREMENT.
190 Một thuộc tính của lớp ảo không thể có một ràng buộc AUTO INCREMENT.
191 Miền của thuộc tính '%s' có một ràng buộc AUTO INCREMENT không hợp lệ.
192 Không phải người dùng nhưng hệ thống có thể gán thuộc tính '%s' tạo bởi 'ON CACHE OBJECT' của khóa ngoại.
193 Không thể dùng tên thuộc tính trùng lặp trong danh sách khóa chỉ mục. Tên thuộc tính '%s' được liệt kê nhiều hơn một lần.
194 Hàm số %s chưa được định nghĩa hoặc được đưa ra số tham số sai.
195 '%s' chỉ có thể được sử dụng trong các truy vấn phân cấp. Chỉ định một mệnh đề CONNECT BY.
196 '%s' không được phép tại đây.
197 Dùng CONNECT_BY_ISCYCLE chỉ được cho phép nếu truy vấn có một mệnh đề CONNECT BY NOCYCLE.
198 Không có bảng nào được dùng.
199 ORDER BY NULL đòi hỏi GROUP BY.
200 Số đến CACHE phải nhỏ hơn một vòng (%s).
201 '%s' không được cho phép trong một truy vấn có mệnh đề LIMIT.
202 WITH ROLLUP đòi hỏi GROUP BY.
203 Một tham số tên cột phải được chuyển đến hàm số DEFAULT. Path expressions chưa được hỗ trợ.
204 Tùy chọn bảng '%s' trùng lặp
205 Lớp '%s' được đánh dấu như REUSE_OID và không thể tham chiếu. Các lớp không thể tham chiếu không thể là miền của một thuộc tính và OID các thực thể của chúng không thể được trả về.
206 Trùng lặp tùy chọn 'serial %s'.
207 Cột '%s' trong mệnh đề sắp xếp thứ tự không rõ ràng.
208 Thuộc tính '%s' không có một giá trị mặc định.
209 Mục ORDER BY phải xuất hiện trong danh sách truy xuất nếu SELECT DISTINCT được chỉ định.
210 Biến số máy chủ không được cho phép trên cột trong đặc tả truy vấn.
211 %s không thể là một cột ORDER BY
212 %s không thể là một cột GROUP BY
213 Không thể định nghĩa '%1$s của %2$s' như một kiểu cột.
214 Giá trị "%s" không hợp lệ cho prefix length của chỉ mục.
215 Chức năng '%s' chưa được định nghĩa trên kiểu %s, %s, %s và %s.
216 Chức năng '%s' chưa được định nghĩa trên kiểu %s, %s và %s.
217 Chức năng '%s' chưa được định nghĩa trên kiểu %s và %s.
218 Chức năng '%s' chưa được định nghĩa trên kiểu %s.
219 Chức năng '%s' chưa được định nghĩa cho một đối số chỉ mục của kiểu %s.
220 ALTER VIEW '%s' AS SELECT không được cho phép cho các khung nhìn có các lớp con hoặc lớp cao cấp. Dùng ALTER VIEW CHANGE QUERY để thay thế.
221 Thuộc tính khung nhìn không thể được thay đổi bằng cách dùng câu lệnh này. 'ALTER VIEW name AS query' có thể có ích để thay thế.
222 CUBRID yêu cầu mục ORDER BY có biểu thức giống với đối số GROUP_CONCAT hoặc một số nguyên (vị trí đối số).
223 Vị từ LIKE đòi hỏi một ký tự thoát được chỉ định. Xem tài liệu cho '%1$s' và '%2$s' tham số hệ thống.
224 KEYLIMIT không thể được dùng trong mệnh đề INDEX NONE.
225 Biến số phiên không được cho phép trong ngữ cảnh này.
226 Xung đột gợi ý chỉ mục giữa '%s' và '%s'.
227 Chức năng '%s' không thể được sử dụng trong mệnh đề DEFAULT 
228 Cập nhật bảng kết quả không được cho phép.
229 Cập nhật thuộc tính hạng và thuộc tính đối tượng không được cho phép trong cùng tuyên bố.
230 Cập nhật thứ bậc hạng ảo không được cho phép.
231 Biến chủ không được cho phép trong tuyên bố DDL.
232 Biểu diễn bộ lọc vô hiệu (%s) cho chỉ mục.
233 Đa chức năng hoặc chức năng lồng ghép không được cho phép trong định nghĩa chỉ mục chức năng.
234 Đặc điểm chỉ mục vô hiệu.
235 SHARED, DEFAULT and AUTO_INCREMENT không được xác định riêng rẽ.
236 Quá nhiều giá trị liệt kê (%d). Loại liệt kê có thể có tối đa %d giá trị.
237 Giá trị lặp lại trong loại liệt kê.
238 Không được MERGE vào một bảng kết quả.
239 Không thể ảnh hưởng tới bảng nguồn trong tuyên bố MERGE.
240 MERGE với các bậc hạng khác không được cho phép.
241 Tên người dùng quá dài.
242 Biểu diễn hằng số không được dùng làm chỉ mục chức năng.
243 Tên hoặc bí danh hạng không được duy nhất trong danh sách: '%s'
244 Mệnh đề DEFAULT vô hiệu. '%s' không thể được sử dụng trong biểu diễm lồng nhau.
245 '%s' yêu cầu các lý lẽ với đối chiếu tương thích.
246 Bộ ký tự vô hiệu.
247 Bộ ký tự yêu cầu và đối chiếu không tương thích.
248 Đối chiếu chưa biết '%s'.
249 Nội dung yêu cầu đối chiếu với các yếu tố có bộ mã và đối chiếu tương tự.
250 Chỉ mục tiền tố không được cho phép trên thuộc tính '%s' (có đối chiếu với phần mở rộng).
251 Giá trị ban đầu vô hiệu.
252 Chỉ mục tiền tố nhiều cột không được cho phép.
253 Chức năng '%s' không được dùng cho chỉ mục dựa trên chức năng.
254 Định nghĩa cột chỉ mục vô hiệu: %s
255 '%s' không được cho phép trong một biểu diễn lọc cho chỉ mục.
256 Không được sử dụng mệnh đề GROUPBY_NUM() và WITH ROLLUP trong cùng trường.
257 Không được MERGE UPDATE cột CẬP NHẬT %s được tham chiếu ở mệnh đề MỞ.
258 Số cột, %d, trong ( %s ) không cân đối với số cột, %d, trong ( %s ).
259 Tiền tố cột không được cho phép trong biểu diễn chỉ mục chức năng.
260 %s không được là cột PARTITION BY hoặc ORDER BY
261 Trường phải đánh giá so với một giá trị đơn\n(%s).
262 Chức năng phân tích lồng nhau không được cho phép.
263 Thuộc tính kế thừa không được sử dụng như phím phân mảng.
264 Tổng kích thước các yếu tố liệt kê quá lớn.
265 Không thể sử dụng bảng đích trong đặc điểm nguồn.

$set 9 MSGCAT_SET_PARSER_RUNTIME
1 Không đủ bộ nhớ ảo: không thể cấp phát %d bytes.
2 Cấp độ cô lập giao dịch phải là 1, 2, 3, 4, 5 or 6.
3 Giá trị Timeout phải là -1, 0, or >0.
4 Mức độ cô lập thiết lập đến:\n
5          READ COMMITTED SCHEMA, READ UNCOMMITTED INSTANCES.\n
6          READ COMMITTED SCHEMA, READ COMMITTED INSTANCES.\n
7          REPEATABLE READ SCHEMA, READ UNCOMMITTED INSTANCES.\n
8          REPEATABLE READ SCHEMA, READ COMMITTED INSTANCES.\n
9          REPEATABLE READ SCHEMA, REPEATABLE READ INSTANCES.\n
10 Một truy vấn không thể cập nhật không được cho phép trong một gọi về a db_query_execute_oid.
11 Giao dịch tạm ngưng chờ cho khóa.
12 Tìm cách truy cập biến số máy chủ có chỉ mục %d trong khi chỉ %d \ncác biến số máy chủ đầu vào được đưa ra.
13 Chuyển đổi chưa được thực hiện.
14 Lỗi nội bộ, chuyển đổi chưa được định nghĩa.
15 '%s' không sinh ra kết quả nào.
16 '%s' sinh ra kết quả nhiều hơn một hàng.
17 Biến số bộ diễn dịch %s chưa được định nghĩa.
18 Không thể ước tính '%s'.
19 Biến số không biết %s.
20 Biến số dùng chung không biết %s.
21 '%s' sinh ra kết quả nhiều hơn một cột.
22 Sử dụng hàm Aggregate bị lồng nhau hoặc không hợp lệ.
23 Functions %s chưa được định nghĩa.
24 Ngữ cảnh yêu cầu truy vấn phải là cột đơn\n(%s).
25 Quá nhiều giá trị đã được chỉ định.
26 Virtual assignment target (%s) không thể đảo nghịch.
27 Có nhiều cột trong đặc tả truy vấn hơn số thuộc tính trong lớp %s.
28 Có nhiều thuộc tính trong lớp %s hơn số cột trong đặc tả truy vấn.
29 Phát hành hiện tại hạn chế Aggregation đến các lớp ảo mà không có \n đặc tả truy vấn aggregation.
30 Phát hành hiện tại hạn chế Aggregation đến các lớp ảo mà không có \n đặc tả truy vấn union.
31 Sử dụng của lớp ảo (%s) trong một composition hierarchy yêu cầu nó phải là có thể cập nhật.\nNó không thể được cập nhất đối với lớp thực %s.
32 không được dùng
33 Xây dựng không hợp lệ trong path expression '%s'.
34 biểu thức đường dẫn '%s' vượt qua ranh giới db%s, với vclass%s.
35 biểu thức đường dẫn '%s' vượt qua ranh giới db từ%s, lớp%s.
36 %s chưa được cấp phép trên %s.
37 Select chưa được cấp phép trên %s.
38 Lớp ảo %s không thể cập nhật.
39 %s không phải một lớp ảo proxy.
40 đặc tả truy vấn của lớp ảo proxy không được là một join.
41 %s không có tập hợp id đối tượng.
42 Các lớp (%s) với nhiều trường object_id trong một composition \nhierarchy không được hỗ trợ.
43 Thuộc tính đối tượng %s của %s không phải là một lớp ảo trên ldb %s.
44 Kiểu đặc tả truy vấn không tương thích với thuộc tính %s.
45 Đặc tả truy vấn của %s có một tham chiếu vòng tròn đến chính nó.
46 Không có biểu thức nào được đưa ra để ước tính.
47 Không tìm thấy lớp thực nào cho lớp ảo %s.
48 Đang thực thi '%s' trên ldb '%s' \ntrả về: %s.
49 Đặt giá trị '%s' vào thuộc tính '%s' trả về: %s.
50 Tên Ldb không được là null trong %s.
51 Tên Ldb tại máy chủ không được là null trong %s.
53 Tên máy chủ không được là null trong %s.
54 Tên người dùng không được là null trong %s.
55 Mật mã người dùng không được là null trong %s.
56 Insert chuyển thành câu lệnh rỗng.
57 Update chuyển thành câu lệnh rỗng.
58 Delete chuyển thành câu lệnh rỗng.
59 INGRES không hỗ trợ những ngày tháng (%s) trước 1 tháng một,1582.
60 Tài nguyên đã hết trong việc tạo truy vấn.
61 Truy vấn không sinh ra kết quả nào, vì vậy biến số '%s' không được thiết lập.
62 With LOCAL check option không được hỗ trợ.
63 Check option ngoại lệ trên khung nhìn %s.
64 Ngày tháng không hợp lệ: '%s'.
65 Thời gian không hợp lệ: '%s'.
66 Utime không hợp lệ: '%s'.
67 Các truy vấn con trong đặc tả truy vấn của proxy không được hỗ trợ.
68 Đối số hàm Ldb không tương thích với ldb trong '%s'.
69 Numeric không hợp lệ: '%s'.
70 Tối ưu hóa truy vấn tham gia bên ngoài thất bại.
71         SERIALIZABLE.\n
72 Lớp ảo %s không thể cập nhật. Chỉ những lớp ảo có thể cập nhật được phép truy xuất VIDs.
73 Phân vùng thất bại.
74 Phân vùng không tồn tại.
75 Không được phép truy cập đến phân vùng: '%s'.
76 Serial '%s' chưa được định nghĩa.
77 Chủ sở hữu hoặc DBA chỉ có thể sửa đổi định nghĩa của serial mục tiêu.
78 Không cho phép người dùng sửa đổi đối tượng serial được tạo bởi một ràng buộc AUTO INCREMENT.
79 Cấp độ cô lập không hợp lệ: %s lược đồ, %s thực thể.

$set 11 MSGCAT_SET_AUTHORIZATION
1 *** lưu ẩn không hợp lệ ***
2    lớp %s
3 từ %s :
4 Cấp phép cho người dùng %s\n
5 Cấp phép cho người dùng chưa định nghĩa\n
6 Người dùng : %s\n
7            id : %d\n
8       thành viên :
9        nhóm :
10 Người dùng %s\n
11 Người dùng hiện tại là : %s\n
12 Đối tượng cấp phép gốc :\n
13   người dùng :
14 *** Không thể phát hành tất cả ủy quyền cho người dùng "%s"*** \n
15 CẤP PHÉP\n
16 direct_groups :

$set 12 MSGCAT_SET_HELP
1 Lớp gốc
2 %s %s\n
3 Lớp cao cấp :\n
4 Lớp con :\n
5 Thuộc tính: \n
6 Phương pháp: \n
7 Thuộc tính lớp: \n
8 Phương pháp lớp: \n
9 Cách giải quyết mâu thuẫn:\n
10 Tệp tin phương pháp:\n
11 Đặc tả truy vấn:\n
12 ĐỐI TƯỢNG của %s\n
13 \nMô tả:\n
14 \nCấu trúc:\n
15 \nVí dụ:\n
16 Tên META CLASS
17 Tên lớp
18 Tên lớp ảo
$  Don't delete the space at the end of the next message
19 Lớp ảo trên ldb
20 GENERAL

$set 13 MSGCAT_SET_TRIGGER
1 TRACE: Kiểm tra điều kiện cho trigger '%s'.
2 TRACE: Kiểm tra hành động cho trigger '%s'.

$set 14 MSGCAT_SET_LOCK
1 \n
2 Tuyến đoạn (chỉ mục %3d) của giao dịch (chỉ mục %3d, %s, %s@%s|%d) đã bị tạm ngưng.\n
3 Tuyến đoạn (chỉ mục %3d) của giao dịch (chỉ mục %3d, %s, %s@%s|%d) đã được tiếp tục lại.\n
4 Chỉ mục giao dịch  %3d THU ĐƯỢC %s khóa trên đối tượng %d|%d|%d.\n
5 Chỉ mục giao dịch  %3d THU ĐƯỢC %s khóa trên trang %d|%d.\n
6 Chỉ mục giao dịch  %3d CHUYỂN ĐỔI khóa của nó đến %s khóa trên đối tượng %d|%d|%d.\n
7 Chỉ mục giao dịch  %3d CHUYỂN ĐỔI khóa của nó đến %s khóa trên trang %d|%d.\n
8 Chỉ mục giao dịch  %3d CHỜ ĐỢI %s khóa trên đối tượng %d|%d|%d.\n
9 Chỉ mục giao dịch  %3d CHỜ ĐỢI %s khóa trên trang %d|%d.\n
10 Chỉ mục giao dịch  %3d PHÁT HÀNH %s khóa trên đối tượng %d|%d|%d.\n
11 Chỉ mục giao dịch  %3d PHÁT HÀNH %s khóa trên trang %d|%d.\n
12 Chỉ mục giao dịch  %3d GIÁNG CẤP khóa đến %s trên đối tượng %d|%d|%d.\n
13 Chỉ mục giao dịch  %3d GIÁNG CẤP khóa đến %s trên trang %d|%d.\n
14 OID = %2d|%6d|%4d\n
15 Kiểu đổi tượng: Lớp gốc.\n
16 Kiểu đổi tượng: Lớp = %s.\n
17 Kiểu đổi tượng: Thực thể của lớp (%2d|%6d|%4d) = %s.\n
18 Kiểu đổi tượng: Không biết.\n
19 Total mode of holders = %8s, Total mode of waiters = %8s.\n
20 Num holders=%3d, Num blocked-holders=%3d, Num waiters=%3d\n
21 LOCK HOLDERS:\n
22 BLOCKED LOCK HOLDERS:\n
23 LOCK WAITERS:\n
24 NON_2PL_RELEASED:\n
25 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d\n
26 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d, Nsubgranules = %2d\n
27 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d\n\t%13s Blocked_mode = %8s\n\t%17s Start_waiting_at = %-30s\n\t%17s Wait_for_secs = %.2f\n
28 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d, Nsubgranules = %2d\n\t%13s Blocked_mode =%8s\n\t%13s Start_waiting_at = %-30s\n\t%13s Wait_for_secs = %.2f\n
29 %4sTran_index = %3d, Blocked_mode =%8s \n\t%13s Start_waiting_at = %-30s \n\t%13s Wait_for_secs = %.2f\n
30 %4sTran_index = %3d, Non_2_phase_lock =%8s\n
31 VPID (volid|pageid) = %3d|%6d.\n
32 *** Lock Table Dump ***\n Lock Escalation at = %d, Run Deadlock interval = %d\n
33 Giao dịch (chỉ mục %2d, %s, %s@%s|%d)\n
34 Cô lập %s\n
35 Trạng thái %s\n
36 Timeout_period %s\n
37 Đơn phương bỏ dở giao dịch với chỉ mục:\n
38 %d ,
39 Giao dịch Timed_out với chỉ mục:\n
40 %d ,
41 Giải quyết khóa chết bằng cách chọn một giao dịch và một hàm số \n để phá vỡ khóa chết: %d %s
42 , %d %s
43 Loại đối tượng: Phím chỉ mục thuộc hạng (%2d|%6d|%4d) = %s.\n
44 Tên chỉ mục: %s\n

$set 15 MSGCAT_SET_IO
1 \n*************************************************************************\n
2 Sao lưu = %s là cần có. \n Nó phải được gắn kết/tải để tiếp tục.\n Gõ 1 để tiếp tục sau khi sao lưu được gắn kết/tải.\n Gõ 0 để thoát khỏi.\n
3 \n\n*** BACKUP HEADER INFORMATION ***\n\n
4 Backup magic file identification %s,\n
5          Release: %s\n\
     Disk Version: %g\n
6    Database Name: %s\n\
 DB Creation Time: %s\
         Pagesize: %d\n
7     Backup Level: %d (%s)\n\
        Start_lsa: %lld|%d\n\
         Last_lsa: %lld|%d\n
8      Backup Time: %s\
  Backup Unit Num: %d\n
9  Database Volume name: %s\n\
   Volume Identifier: %d, Size: %lld bytes (%d pages)\n
10   Database Name: %s\n\
     Volume Name: %s\n\
        Unit Num: %d\n\
    Backup Level: %d (%s)\n\
Nhập một trong các tùy chọn sau đây:\n
11 Gõ\n \
  -  %d để thoát khỏi.\n \
  -  %d để tiếp tục sau khi bộ đĩa đã được gắn kết/tải. (thử lại) \n \
  -  %d để tiếp tục sau khi thay đổi thư mục hoặc device của bộ đĩa.\n
12 Nhập một thư mục hoặc device mới cho bộ đĩa sao lưu.\n
13 Lỗi trong đầu vào, chọn một giá trị giữa %d và %d.\n
14 ** CẢNH BÁO ** Bộ đĩa sao lưu được gắn kết không đúng.\n
15 Bộ đĩa sao lưu %s từ một cấp độ sao lưu khác\n   (Cấp độ: %d, mong đợi %d).\n\n
16 Bộ đĩa sao lưu %s có identifier magic hoặc page không đúng.\n\n
17 Bộ đĩa sao lưu %s từ cơ sở dữ liệu khác\n  (Tên: %s, mong đợi %s).\n\n
18 Bộ đĩa sao lưu %s ngoài trình tự\n  (Unit Num: %d, mong đợi %d).\n\n
19 Bộ đĩa sao lưu %s timestamp không khớp\n  (Timestamp: %s, mong đợi %s).\n\n
20 Lỗi tệp tin thông tin bộ đĩa sao lưu, dòng %d.\n
21 Backup Volume Label: Level: %d, Unit: %d, Database %s, Backup Time: %s\n
22     Previous Backup level: %d Time: %s (start_lsa was %lld|%d)\n
23 Bộ đĩa sao lưu cơ sở dữ liệu sau đây cần được tiếp tục khôi phục:\n\n
24 Đích nhận sao lưu đã đầy, một đích nhận mới được đòi hỏi để tiếp tục:\n\n
25 Tên bộ đĩa sao lưu trước: %s\n
26     Tên bộ đĩa sao lưu tiếp theo: %s\n
27  Backup Pagesize: %d\n
28       Zip Method: %d (%s)\n\
        Zip Level: %d (%s)\n
29 Include Active Log: %s\n

$set 16 MSGCAT_SET_LOG
1 \n*************************************************************************\n
2 Lưu trữ bản ghi %s \n cần có để tiếp tục sự thực thi bình thường.   \n \
  Gõ\n \
  -  0 để thoát khỏi.\n \
  -  1 để tiếp tục mà không có lưu trữ hiện tại. (Khôi phục từng phần)\n \
  -  2 để tiếp tục sau khi lưu trữ được gắn kết/tải.       \n \
  -  3 để tiếp tục sau khi thay đổi vị trí/tên của lưu trữ.   \n
3 Tệp tin thông tin sao lưu %s \n là cần có để tiếp tục tiến trình khôi phục. \n \
  Gõ\n \
  -  0 để thoát khỏi.\n \
  -  1 để tiếp tục sau khi tệp tin được gắn kết/tải.       \n \
  -  2 để tiếp tục sau khi thay đổi vị trí/tên của tệp tin.   \n
4 Nhập vị trí/tên mới.\n
5 CHÚ THÍCH: %s cho cơ sở dữ liệu for database %s\n
6 CHÚ THÍCH: Lưu trữ bản ghi %s là không cần thiết nữa trừ khi xảy ra một database media crash.\n
7 CHÚ THÍCH: Lưu trữ bản ghi từ %s đến to %s là không cần thiết nữa trừ khi xảy ra một database media crash.\n
8 CHÚ THÍCH: từ cơ sở dữ liệu đã được đổi tên = %s\n
9 ARCHIVE: %d %s %lld %lld\n
10 ARCHIVE:
11 REMOVE: %d %s to \n \
       %d %s.\n \
       REASON: %s\n
12 ACTIVE: %s %d pages\n
13  Finish commit of tran_index %d with tranid = %d,\n just before LSA= %lld|%d and at %s
14  Finish abort of tran_index %d with tranid = %d,\n just before LSA= %lld|%d and at %s
15  Incomplete Media Recovery is in effect. The database will be \n \
 recovered up to LSA point address of %lld|%d. Last commit/abort \n \
 point time is approximately at %s \n \
 The database has lost all information of transactions committed \n \
 after this time.\n
16 Thiết lập lại của bản ghi vì sự khôi phục không đầy đủ.
17 Cơ sở dữ liệu đã được sao lưu.
18 Media crash không quan trọng đối với ứng dụng.
19 Đang sao lưu đến thư mục %s mà dường như đã có một sao lưu trước đó ở trong đó.\n\
Ổ sao lưu quá cũ của cơ sở dữ liệu trong vị trí này sẽ được gỡ bỏ,\n\
bao gồm cả những từ cấp độ sao lưu hiện hành và cao hơn.\n
20 Nhập 'y' để tiếp tục hoặc bất kỳ phím nào để hủy.\n
21 Sao lưu cơ sở dữ liệu đã bị bỏ dở bởi người dùng.\n
22 DELETE POSTPONED: Archives %d %s to \n \
        %d %s \n \
        are no longer needed unless a restore from current backup occurs.\n
23 Một bản sao lưu mới đã phát hành tài liệu lưu trữ trước đó được đánh dấu DELETE POSTPONED.\n
24 CHÚ THÍCH: Đăng nhập lưu trữ%s không phải là DELETE POSTPONED nữa do để sao lưu\n \
   gần đây và do đó không cần thiết trừ khi xảy ra một vụ tai nạn cơ sở dữ\n liệu phương tiện truyền thông.\n
25 CHÚ THÍCH: Đăng nhập lưu trữ từ%s đến%s không DELETE POSTPONED nữa do để sao lưu\n \
   gần đây và do đó không cần thiết trừ khi xảy ra một vụ tai nạn cơ sở dữ\n \
   liệu phương tiện truyền thông.\n
26 Một lỗi đã xảy ra trong quá trình khôi phục cơ sở dữ liệu, trong khi đang xử lý\n\
     Backup Volume: %s\n\
          Unit Num: %d\n\
   Database Volume: %s\n\
    Database Volid: %d\n\
** CẢNH BÁO: Không sử dụng cơ sở dữ liệu cho đến khi đạt được một khôi phục không lỗi.\n
27 Lỗi trong đầu vào, chọn một giá trị giữa %d và %d.\n
28 Thời gian(%s) được chỉ định phải sau thời gian (%s) của sao lưu được chỉ định.\n
29 CHÚ THÍCH: Lưu trữ bản ghi %s, chứa các trang bản ghi trước %lld, là không cần thiết nữa với bất kỳ tiện ích HA nào.\n
30 Số lưu trữ bản ghi đang hoạt động đã vượt quá số lượng tối đa mong muốn.
