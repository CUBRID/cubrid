
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
1 giá trị của đối số "%1$s" không phải kiểu số nguyên \n
2 giá trị của đối số "%1$s" không phải kiểu float\n
3 Thiếu đối số %1$s\n
4 [tùy chọn]
5 Tùy chọn: \n
6 Thiếu giá trị đối số cho khóa %1$s\n
7 Tùy chọn "%1$s" không biết\n
8 Ðối số vị trí "%1$s" không mong đợi\n

$set 2
1 Ðịnh nghĩa lại của con chạy "%1$s"
2  *\tcho câu lệnh %1$s\n
3  *\n * Bảng con chạy:\n

$set 3
1 (LỖI NỘI TẠI trong set_class_bit): lớp lưu trữ (%1$d) tồi
2 (LỖI NỘI TẠI trong add_spec_to_decl()): chuỗi không đúng dạng (no specifier)
3 bộ chỉ định lớp lưu trữ không được phép trong ngữ cảnh này
4 kết hợp lớp lưu trữ không hợp lệ
5 kết hợp kiểu không hợp lệ
6 kết hợp bộ sửa đổi không hợp lệ
7 (LỖI NỘI TẠI trong pp_add_type_spec): trường hợp không mong đợi (%1$d)
8 (LỖI NỘI TẠI trong pp_add_type_adj): trường hợp không mong đợi (%1$d)
9 khai báo %1$s được định dạng không đúng cách

$set 4
1 %1$s
2 %1$s: Không phải một thư mục\n
3 %1$s: Gốc cài đặt tồi?\n

$set 5
1 ?
2 xin vui lòng kiểm tra sự chính xác của mã thông báo
3 Phần USING không được phép cho con chạy static "%1$s"
4 con chạy "%1$s" chưa được định nghĩa
5 con trỏ đến một bộ mô tả
6 con trỏ đến một DB_OBJECT
7 chuỗi ký tự
8 biến số chỉ báo cho "%1$s" không được phép trong ngữ cảnh này
9 "%1$s" chưa được khai báo trong BEGIN DECLARE section
52 %s (văn bản là "%s")
53 Lỗi Lexical
54 lỗi cú pháp tại "%s": thiếu %s trong %s
55 lỗi cú pháp tại "%s": thiếu %s

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
1 không hiện chỉ thị #line trong tệp tin đầu ra, mặc định off
2 tên tệp tin đầu ra, mặc định <input-file>.c hoặc stdout
3 tệp tin include chứa uci_ prototypes, mặc định "esqlx.h"
4 kết xuất thông tin gỡ lỗi scope, mặc định off
5 kết xuất thông tin gỡ lỗi malloc cuối, mặc định off
6 in ra thông tin số phiên bản, mặc định off
7 bật theo dõi chức năng UCI, mặc định off
8 tắt khởi tạo length của biến máy chủ VARCHAR, mặc định off
9 dùng kiểu VARCHAR khác, mặc định off
10 bỏ qua lỗi -462(null indicator needed), mặc định off
11 dùng chỉ báo NULL nội tại để tránh lỗi -462(null indicator needed), mặc định off
12 tên tệp tin đầu vào, mặc định stdin

$set 8
1 tùy chọn REPEATED bị bỏ qua
2 %s phiên bản %s\n

$set 9
1 (yy_pop_mode) lỗi nội tại: chế độ xếp chồng rỗng
2 "%1$s" không được cho phép trong ngữ cảnh này

$set 10
1 lỗi nội tại trong %s: %s\n
2 bảng tràn

$set 11
1 biến số chỉ báo "%1$s" phải là một short int
2 không thể dùng biến số chỉ báo có một cấu trúc
3 "%1$s" có một định nghĩa cấu trúc không đầy đủ
4 "%s" phải là một %s hợp lệ
5 %s không phải một kiểu có thể chấp nhận cho "%s"
6 kiểu biến số máy chủ không mong đợi: %1$s
7 (pp_addr_expr): cố gắng để tìm địa chỉ của biến số NULL
8 "%1$s" không phải một con trỏ
9 "%1$s" không chỉ đến một cấu trúc
10 "%1$s" không phải một cấu trúc
11 %s không có trường "%s"
12 "%s" không thể được bỏ tham chiếu bởi "%s"

$set 12
1 hết bộ nhớ

$set 13
1 mảng của các chức năng không hợp lệ, giả định các con trỏ chức năng
2 (pp_the_same_type) lớp liên kết không rõ
3 (pp_get_sizeof): danh từ sai (%1$d)

$set 14
1 (LỖI NỘI TẠI trong %1$s): trường hợp không thể
