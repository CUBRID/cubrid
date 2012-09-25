
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
1 非整形的参数值 "%s"\n
2 非浮点型参数值 "%s"\n
3 丢失参数 %s\n
4 [选项] 
5 选项: \n
6 对于键 %s 丢失参数数值\n
7 未知的选项 "%s"\n
8 意外的位置参数 "%s"\n

$set 2
1 游标的重定义 "%s"
2  *\t对于语句 %s\n
3  *\n * 游标表:\n

$set 3
1 (内部错误 在 set_class_bit): 错误的存储类 (%d)
2 (内部错误 在 add_spec_to_decl()): 链格式不正确 (no specifier)
3 在这个场景中存储类描述符是不允许的
4 非法的存储类组合
5 非法的类型组合
6 非法的修改者组合
7 (内部错误 在 pp_add_type_spec): 意外的情况 (%d)
8 (内部错误 在 pp_add_type_adj): 意外的情况 (%d)
9 %s : 声明无效

$set 4
1 %s
2 %s: 不是一个目录\n
3 %s: 错误的安装渠道?\n

$set 5
1 ?
2 请检测符号的正确性
3 对于静态游标 "%s", USING 部分不被允许
4 游标 "%s" 未定义
5 指向一个描述符的指针
6 指向一个DB_OBJECT的指针
7 字符的字符串
8 为了 "%s" 的指示变量在这个场景中不被允许
9 "%s" 在 BEGIN DECLARE 段中没有被声明
52 %s (文本是 "%s")
53 词法错误
54 语法错误 在 "%s": 丢失 %s 在 %s 内
55 语法错误 在 "%s": 丢失 %s

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
1 在输出文件中废止 #line 指令, 默认 关闭
2 输出文件名, 默认 <input-file>.c 或者 stdout
3 include 文件包含uci_ 原型, 默认 "esqlx.h"
4 转储 scope调试信息, 默认 关闭
5 转储最终malloc调试信息, 默认 关闭
6 打印出版本号信息, 默认 关闭
7 启动 UCI function trace, 默认 关闭
8 禁止初始化VARCHAR host variable的长度, 默认 关闭
9 使用不同的 VARCHAR 风格, 默认 关闭
10 忽略 -462(null indicator needed)错误, 默认 关闭
11 使用内部 NULL 指示器来阻止 -462(null indicator needed)错误, 默认 关闭
12 输入文件名, 默认 stdin

$set 8
1 REPEATED 选项被忽略
2 %s 版本 %s\n

$set 9
1 (yy_pop_mode) 内部错误: mode堆栈为空
2 "%s" 在这个场景中不被允许

$set 10
1 内部错误 在 %s: %s\n
2 表溢出

$set 11
1 指示器变量 "%s" 必须是一个短整型
2 无法对结构使用指示器变量
3 "%s" 有不完整的结构定义
4 "%s" 必须是一个有效的 %s
5 %s 是一个不可接受的类型对于 "%s"
6 意料之外的host variable类型: %s
7 (pp_addr_expr): 尝试找到NULL变量的地址
8 "%s" 不是一个指针
9 "%s" 没有指向一个结构
10 "%s" 不是个结构
11 %s 没有域 "%s"
12 "%s" 无法被 "%s" 解引用

$set 12
1 内存用尽

$set 13
1 函数数组是非法的, 假定为函数指针
2 (pp_the_same_type) 未知的链接类
3 (pp_get_sizeof): 错误的名词 (%d)

$set 14
1 (内部错误 在 %s): 不可能的情况
