
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
tiện ích SQL tương tác, phiên bản %s\n\
cách sử dụng: %s [TÙY CHỌN] tên cơ sở dữ liệu[@host]\n\
\n\
tùy chọn có thể dùng:\n\
  -S, --SA-mode                thực thi chế độ độc lập\n\
  -C, --CS-mode                thực thi chế độ client-server\n\
  -u, --user=ARG               tên người dùng thay thế\n\
  -p, --password=ARG            chuỗi mật mã, "" cho không có\n\
  -e, --error-continue         không thoát trên câu lệnh lỗi\n\
  -i, --input-file=ARG         tên-tệp tin-đầu vào\n\
  -o, --output-file=ARG        tên-tệp tin-đầu ra\n\
  -s, --single-line            thực thi theo định hướng dòng đơn\n\
  -c, --command=ARG            CSQL-các lệnh\n\
  -l, --line-output            hiển thị mỗi giá trị trên một dòng\n\
  -r, --read-only              chế độ chỉ-đọc\n\
      --no-auto-commit         tắt thực thi chế độ tự động cam kết\n\
      --no-pager               không dùng bộ phân trang\n\
      --no-single-line         tắt thực thi theo định hướng dòng đơn\n\
\n\
Để biết thêm thông tin, truy cập http://www.cubrid.com\n
41 %s: giá trị môi trường của %s_MODE không chính xác. \nNó phải là %s hoặc %s.\n
42 %s: quá nhiều đối số được đưa ra. Tối đa là %d.\n
43 %s: Biến môi trường thư mục gốc ($CUBRID) phải được thiết lập.\n
44 %s: thất bại khi cố gắng thực thi "%s" .\n
45 %s: không thể dùng tùy chọn -sa và -cs đồng thời.\n
46 %d lệnh đã xử lý thành công.
47 Trong lệnh từ dòng %d,
48 Trong dòng %d, cột %d,
49 Gõ ';help' để xem các tin nhắn trợ giúp.
50 LỖI:
51 CUBRID SQL Bộ diễn dịch
52 Đã có những cập nhật. Bạn có muốn cam kết không? (`Y' hoặc `N')
53 Nhập `Y' để cam kết, `N' để bỏ dở.
54 Giao dịch hiện tại đã được cam kết.
55 Giao dịch hiện tại đã được quay lui lại.
56 Bộ đệm lệnh đã được lưu lại.
57 Tệp tin đã được đọc vào bộ đệm lệnh.
58 Bộ đệm lệnh đã được gửi đến máy in.
59 Thư mục hiện tại được thay đổi thành:
61 Nhập mật mã:
62 <Kết quả của Lệnh %s trong Dòng %d>
63 Không có kết quả nào.
64 Điểm kiểm tra đã được đưa ra.
65 Cơ sở dữ liệu đã được khởi động lại.
66 Chỉ mục giao dịch       Tên người dùng      Tên máy chủ      Tiến trình id      Tên chương trình\n
67 %7d(%1c) %14s %14s %15d %17s\n
68 Giao dịch được chỉ định đã bị bỏ.
69 Thất bại khi bỏ giao dịch đã chỉ định.
70 %d hàng %s.
71 %d hàng %s.
$ These 3 should be blank
75 Bộ kiểm tra API tự động
76 Bộ kiểm tra API tự động không hợp lệ
79 csql>
80 csql
81 sysadm>

145 <Trợ giúp: Lược đồ của một Lớp>
146 none
147 <Trợ giúp: Tất cả Trigger>
148 <Trợ giúp: Không có Trigger được định nghĩa>
150 <Trợ giúp: Trigger Định nghĩa>
151 <Trợ giúp: Cú pháp Lệnh>
152 <Trợ giúp: Tóm tắt Lệnh Phiên>
178 Đòi hỏi một tên tệp.
179 Không thể thực thi bộ phân trang.
180 Không có đủ bộ nhớ để cấp phát.
184 Dòng đang được nhập quá dài.
185 Quá nhiều dòng đã được nhập.
188 Quá nhiều tên tệp tin đã được nhập.
190 Không có lệnh phiên nào như thế. Gõ `;help' cho các tin nhắn trợ giúp.
191 Có nhiều hơn một phiên phù hợp. Xin vui lòng chỉ định thêm.
193 Có nhiều hơn một lệnh phù hợp. Xin vui lòng chỉ định thêm.
194 Sự kết hợp các đối số dòng lệnh không hợp lệ (tùy chọn).
196 Một lỗi chưa xác định đã phát sinh.
197 ';edit' không được phép khi đầu ra đã được chuyển hướng.

203 <%s> 
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
   Tất cả các lệnh phiên phải được bắt đầu bởi `;' và chỉ có khoảng trống/tab\n\
   mới có thể nằm trước các tiền tố đó. Ký tự viết hoa đại diện cho viết tắt\n\
   tối thiểu mà bạn cần nhập vào để thực thi lệnh được chỉ định.\n\
\n\
   ;REAd   [<file-name>]       - đọc một tệp tin vào bộ đệm lệnh.\n\
   ;Write  [<file-name>]       - ghi (ghi đè) bộ đệm lệnh vào một tệp tin.\n\
   ;APpend [<file-name>]       - nối bộ đệm lệnh vào một tệp tin.\n\
   ;PRINT                      - in bộ đệm lệnh.\n\
   ;SHELL                      - gọi ra shell.\n\
   ;CD                         - thay đổi thư mục đang làm việc hiện tại.\n\
   ;EXit                       - thoát chương trình.\n\
\n\
   ;CLear                      - xóa sạch bộ đệm lệnh.\n\
   ;EDIT                       - gọi ra trình soạn thảo hệ thống có bộ đệm lệnh.\n\
   ;List                       - hiển thị nội dung của bộ đệm lệnh.\n\
\n\
   ;RUn                        - thực thi sql trong bộ đệm lệnh.\n\
   ;Xrun                       - thực thi sql trong bộ đệm lệnh,\n\
                                 và xóa sạch bộ đệm lệnh.\n\
   ;COmmit                     - cam kết giao dịch hiện tại.\n\
   ;ROllback                   - quay lui lại giao dịch hiện tại.\n\
   ;AUtocommit [ON|OFF]        - bật/tắt chế độ tự động cam kết.\n\
   ;REStart                    - khởi động lại cơ sở dữ liệu.\n\
\n\
   ;SHELL_Cmd  [shell-cmd]     - thiết lập shell mặc định, trình soạn thảo, in và bộ phân trang\n\
   ;EDITOR_Cmd [editor-cmd]      lệnh đến một cái mới, hoặc hiện thị cái hiện tại\n\
   ;PRINT_Cmd  [print-cmd]       ,theo thứ tự.\n\
   ;PAger_cmd  [pager-cmd]\n\
\n\
   ;DATE                       - hiển thị ngày, thời gian cục bộ.\n\
   ;DATAbase                   - hiển thị tên của cơ sở dữ liệu đang được truy cập.\n\
   ;SChema class-name          - hiển thị thông tin lược đồ của một lớp.\n\
   ;TRigger [`*'|trigger-name] - hiển thị định nghĩa trigger.\n\
   ;Get system_parameter       - lấy giá trị của một tham số hệ thống.\n\
   ;SEt system_parameter=value - thiết lập giá trị của một tham số hệ thống.\n\
   ;PLan [simple/detail/off]   - hiện kế hoạch thực thi truy vấn.\n\
   ;Info <command>             - hiển thị thông tin nội tại.\n\
   ;TIme [ON/OFF]              - bật/tắt để hiển thị thời gian\n\
                                 thực thi truy vấn.\n\
   ;HISTORYList                - hiển thị danh sách của các truy vấn được thực thi.\n\
   ;HISTORYRead <history_num>  - đọc mục nhập trên số lịch sử vào bộ đệm lệnh.\n\
   ;HElp                       - hiển thị tin nhắn trợ giúp.\n\n

232 <Constraints>
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - thông tin lược đồ\n\
\ttrigger [trigger-name] - thông tin trigger\n\
\tdeferred               - thông tin trigger bị hoãn lại\n\
\tworkspace              - thông tin vùng làm việc\n\
\tlock                   - thông tin khóa (chế độ client-server)\n\
\tstats class-name       - thông tin thống kê lớp\n\
\ttrantable              - thông tin giao dịch\n\
\tlogstat                - thông tin bản ghi\n\
\tcsstat                 - thông tin đoạn mã tới hạn\n\
\tplan                   - thông tin bộ lưu ẩn kế hoạch\n\
\tqcache                 - thông tin bộ lưu ẩn truy vấn\n
235 <Partitions>
236 Đòi hỏi một tên lớp.
