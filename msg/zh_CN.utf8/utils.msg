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
$       50      prefetchlogdb

$set 1 MSGCAT_UTIL_SET_GENERIC
1 在数据库名 '%2$s' 中有无效的长度或字符 '%1$c' \n
2 无法打开输出文件 '%1$s'\n
3 应用工具名缺失或无效\n
4 无效的应用工具名 '%1$s'\n
5 下面的列表包含了已识别的工具名:\n
6 在卷名 '%2$s' 中有无效字符 '%1$c' \n
9 \
%1$s (CUBRID 应用工具)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
cubrid 管理工具，版本 %1$s\n\
用法: %2$s <工具名> [参数]\n\
输入 '%3$s <工具名>' 来查看工具的详细帮助.\n\
\n\
可用的管理工具名:\n\
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
%4$s 是一个 DBMS 工具.\n\
要寻找额外的信息, 可浏览 http://www.cubrid.org\n
12 '%1$s' 应用工具名是无效的.\n输入 'cubrid --help' 查询帮助.\n
13 '%1$s' 命令无效.\n输入 'cubrid --help' 查询帮助.\n
14 无法读取从配置文件中读取一个或多个强制属性.\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s' 没有运行.\n
18 ++ %1$s 没有运行.\n
19 ++ %1$s '%2$s' 正在运行.\n
20 ++ %1$s 正在运行.\n
21 ++ %1$s %2$s: %3$s\n
22 缺少一个或多个参数.\n输入 'cubrid --help' 查询帮助.\n
23 cubrid 应用工具, 版本 %1$s\n\
用法: %2$s <工具名> [参数]\n\
输入 '%3$s <工具名>' 来查看工具的详细帮助.\n\
\n\
可用的服务工具:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
\n\
可用的管理员工具:\n\
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
%4$s 是一个 DBMS 工具.\n\
要寻找额外的信息, 可浏览 http://www.cubrid.org\n
24 cubrid 应用工具, 版本 %1$s\n\
用法: %2$s service <命令>\n\
\n\
可用命令:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubrid 应用工具, 版本 %1$s\n\
用法: %2$s server <命令> [参数]\n\
\n\
可用命令:\n\
    start       [数据库名]\n\
    stop        [数据库名]\n\
    restart     [数据库名]\n\
    status\n\
    acl         <status|reload> 数据库名\n
37 服务器没有配置成 HA 模式.\n
26 cubrid 应用工具, 版本 %1$s\n\
用法: %2$s broker <命令> [参数]\n\
\n\
可用命令:\n\
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
    test        broker-name [-D <数据库名>] [-u <用户名>] [-p <密码>] [-c <SQL命令>] [-i <输入文件>] [-o <输出文件>] [-v] [-s]\n
27 cubrid 应用工具, 版本 %1$s\n\
用法: %2$s manager <命令>\n\
\n\
可用命令:\n\
    start\n\
    stop\n\
    status\n
28 cubrid 应用工具, 版本 %1$s\n\
用法: %2$s heartbeat <命令> [参数]\n\
\n\
可用命令:\n\
    start          [数据库名]\n\
    stop           [-h <hostname>] [-i] [数据库名]\n\
    copylogdb      <start|stop> 数据库名 节点名\n\
    applylogdb     <start|stop> 数据库名 节点名\n\
    prefetchlogdb  <start|stop> 数据库名 节点名\n\
    replication    <start|stop> 节点名\n\
    status         [-v]\n\
    reload\n
31 不需要参数 '%1$s' .\n
32 缺少数据库名.\n
33 %1$s: 这个选项已经被废弃.\n
34 非法参数. %1$s %2$s\n
35 不能分配内存.\n
36 服务器没有配置成 HA 模式.\n
38 为HA配置的服务器没有列在 ha_db_list 列表中.\n
39 为HA配置的服务器没有列在 ha_node_list 列表中.\n
40 无效的命令.\n
41 没有安装 cubrid manager 服务.\n
42 无效的参数.\n
43 打开文件 (%1$s) 时出错.\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 警告: 在单机模式下 -t 选项将被忽略.\n
31 错误: 目标路径不存在或不是目录.\n
60 \
backupdb: 备份一个数据库.\n\
用法: %1$s backupdb [选项] 数据库名\n\
\n\
可用选项:\n\
  -D, --destination-path=PATH  将备份卷存到 PATH 目录; 目录的默认值: 数据库的日志目录\n\
  -r, --remove-archive         删除不必要的日志档案; 请参考说明并小心使用\n\
  -l, --level=LEVEL            备份级别; 默认: full level\n\
                               允许的级别:\n\
                                   0 - full\n\
                                   1 - incremental 1\n\
                                   2 - incremental 2\n\
  -o, --output-file=FILE       将备份的细节信息打印到文件 FILE\n\
  -S, --SA-mode                单机模式执行\n\
  -C, --CS-mode                客户端-服务器模式执行\n\
      --no-check               不检测数据库的一致性; 默认: 检测\n\
  -t, --thread-count=COUNT     线程数; 默认: 0(auto)\n\
  -z, --compress               压缩备份卷; 默认: 不压缩\n\
  -e, --except-active-log      不包括活动日志卷; 默认: 包括\n\
      --sleep-msecs=N          每读 1M 休眠 N  毫秒; 默认: 0\n
71 不兼容的参数, \"%1$s\", 和 \"%2$s\"\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 错误: 目的名与源名相同.\n
31 错误: volext-pathname不存在或不可写.\n
32 错误: vols-tofrom-paths_file不存在或不可读.\n
60 \
copydb: 复制一个数据库.\n\
用法: %1$s copydb [选项] 源数据库名 目的数据库名\n\
\n\
可用选项:\n\
      --server-name=HOST          指定服务器的主机名为HOST; 默认: 当前主机名\n\
  -F, --file-path=PATH            指定数据库的卷路径为 PATH; 默认: 当前目录\n\
  -L, --log-path=PATH             指定日志卷的路径为 PATH; 默认: 等于file-path\n\
  -B, --lob-base-path=PATH        指定外部LOB数据基准路径为 PATH; 默认: <file-path>/lob\n\
  -E, --extended-volume-path=PATH 指定 PATH 作为复制/重命名 扩展卷的目录; 默认: 等于原始卷的路径或给定的control-file路径\n\
  -i, --control-file=FILE         FILE 用来控制 复制/重命名 在多个位置上的卷\n\
  -r, --replace                   替换一个存在的数据库; 默认: 不替换\n\
  -d, --delete-source             删除一个源数据库; 默认: 不删除\n\
      --copy-lob-path             从源数据库复制LOB数据到外部存储路径. 不能与-B 参数一起使用; 默认: 不拷贝\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 组 '%1$s' 不存在\n
41 用户 '%1$s' 不存在\n
42 未知的命令 '%1$s'\n
43 无法打开输出文件 '%1$s'\n
44 无法移除已存在的数据库.\n
45 创建数据库大小为 %1$s，使用的缺省语言区域是 %2$s。总共需要的磁盘空间大小为 %3$s.\n
46 无法创建数据库.\n
47 用户文件 '%1$s' 存在问题\n
48 给定值 '%1$s', %2$s 超过了范围. 选择大小在 %3$s 和 %4$s之间的数.\n
49 %1$s '%2$s'的值是非法的.\n
60 \
createdb: 创建一个数据库.\n\
用法: %1$s createdb [选项] 数据库名 语言字符集\n\
\n\
语言字符集格式为： <语言>.<字符集>; 例如 : en_US.iso88591, ko_KR.utf8\n\
可用选项:\n\
      --db-volume-size=SIZE           设置数据卷大小\n\
                                      默认: db_volume_size 在 cubrid.conf 文件中\n\
      --db-page-size=SIZE             设设置数据库的页大小为SIZE字节; 默认: 16K\n\
      --log-volume-size=SIZE          设置日志卷大小\n\
                                      默认: log_volume_size 在 cubrid.conf 文件中\n\
      --log-page-size=SIZE            设置日志页的大小为SIZE字节; 默认: 数据库的页大小\n\
      --comment=COMMENT               用COMMENT来注释一个数据库; 默认: 空\n\
  -F, --file-path=PATH                用PATH作为数据库的卷目录; 默认: 当前目录\n\
  -L, --log-path=PATH                 用PATH作为日志卷的目录; 默认: 与file-path相同\n\
  -B, --lob-base-path=PATH            用PATH作为外部LOB数据基准目录; 默认: <file-path>/lob\n\
      --server-name=HOST              用HOST作为服务器的主机名; 默认: 当前主机名\n\
  -r, --replace                       替换一个已存在的数据库; 默认: 不替换\n\
      --more-volume-file=FILE         使用 FILE 中用于创建附加卷的详述; 默认: 无\n\
      --user-definition-file=FILE     使用 FILE 中包含的关于用户的定义; 默认: 无\n\
      --csql-initialization-file=FILE 使用 FILE 中包含的用于初始化数据库的SQls; 默认: 无\n\
  -o, --output-file=FILE              重定向输出信息到FILE; 默认: 无\n\
  -v, --verbose                       使用详细状态信息; 默认: 不可用\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: 删除一个数据库.\n\
用法: %1$s deletedb [选项] 数据库名\n\
\n\
可用选项:\n\
  -o, --output-file=FILE       重定向输出信息到 FILE; 默认: 无\n\
  -d, --delete-backup          删除备份卷; 默认: 不删除\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 错误: volext-pathname 不存在或不可写.\n
32 ERROR: vols-tofrom-paths_file 不存在或不可写.\n
60 \
renamedb: 重命名数据库.\n\
用法: %1$s renamedb [选项] 源数据库名 目的数据库名\n\
\n\
有效的选项:\n\
  -E, --extended-volume-path=PATH 指定 PATH 作为扩展卷的目录; 默认: 等于数据卷的路径或给定的控制文件路径\n\
  -i, --control-file=FILE         重命名并写入控制文件的多个位置; 默认: 无\n\
  -d, --delete-backup             删除备份卷; 默认: 不删除\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 一个 %1$s 进程 已经在运行.\n
12 MASTER服务运行. \n
13 MASTER服务退出.\n
14 查找本地主机名错误\n
15 无法找到参数文件\n
16 无法初始化主进程\n
17 服务 %1$s 在 %2$d 分钟后停止
18 服务 %1$s 被通知要关闭.\n这可能会占用几分钟. 请等待.
19 服务 %1$s 没有找到.
20 Master将在 %1$d 分钟内停止\n
21 完成心跳故障转移. 请稍候, 直到所有 cub_servers 的进程完全关闭后再进行操作.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 无法打开输出文件 '%1$s'\n
60 \
optimizedb: 优化一个数据库.\n\
用法: %1$s optimizedb [选项] 数据库名\n\
\n\
可用选项:\n\
  -n, --class-name=NAME        更新名为NAME的类的统计信息; 默认: 所有类\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 无效的日期格式, 有效地日期格式是 dd-mm-yyyy:hh:mm:ss or 'backuptime'\n
20 Restoredb 被取消或发生了一个错误.\n
60 \
restoredb: 恢复一个数据库.\n\
用法: %1$s restoredb [选项] 数据库名\n\
\n\
可用选项:\n\
  -d, --up-to-date=DATE             将数据库恢复到给定日期DATE时的情况. DATE 为如下形式: dd-mm-yyyy:hh:mm:ss 或 'backuptime' 关键字\n\
      --list                        列出不恢复的卷n\
  -B, --backup-file-path=PATH       PATH 备份卷被保存的目录\n\
  -l, --level=LEVEL                 指定将要被恢复的备份的等级为 LEVEL ; 查看 backup 的使用说明; 默认: 0(full backup)\n\
  -p, --partial-recovery            执行部分恢复，如果有任何日志档案存在\n\
  -o, --output-file=FILE            将输出信息重定向到FILE; 默认: 空\n\
  -u, --use-database-location-path  将数据库和日志文件恢复到数据库位置文件中指定的路径中去\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 数据库名缺失.\n
20 '%1$s' 文件没有 %2$d 行.\n
21 中断 ... \n
22 中断读取 - 当前事务被提交\n
23 中断读取 - 当前事务被终止\n
24 无法打开文件 '%1$s'\n
25 \n*** 语法检测 ***\n\n
26 %1$d 个错误被检测到\n
27 \n*** 插入对象 ***\n\n
28 总共 %1$8d 个对象被插入.\n
29 总共 %1$8d 个被涉及的对象以默认内容被插入.\n
30 \n*** 提交事务中 ***\n
31 \n*** 关闭数据库 ***\n
32 行 %1$d:
33 对于%4$s 的属性 '%3$s', 将 '%1$s' 错误解析为一个 '%2$s' 值 \n
34 %2$s 的属性 %1$s 缺少域\n
35 对于%2$s 的属性 %1$s, 类型 %3$s 在域的集合中是不被允许的\n
36 对于%2$s 的属性 %1$s, 期望类型 %3$s, 得到一个set常量\n
37 对于%2$s 的属性 %1$s, 期望类型 %3$s, 得到 %4$s\n
38 没有名为 %1$s 的类被 %3$s 的属性 %2$s 所引用\n
39 没有名为 %1$s 的类, 忽略元组\n
40 没有以 %1$d 为标识符的类被 %3$s 的属性 %2$s 所引用\n
41 在加载器输入文件中类 %1$s 不被允许\n
42 以错误而终止.\n
43 警告: 更新已存在的实例 !\n
44 警告: 重定义类 %2$s 的实例 %1$d \n
45 类 %2$s 的实例 %1$d 已经被定义\n
46 类 %2$s 的实例 %1$d 已经被保留\n
47 在属性 '%1$s' 上违反了唯一性, 转化为 NULL\n
48   %1$d 个实例\n
49 类 %1$s\n
50 密码:
51 \n*** 更新类统计信息 ***\n
52 %1$s 在行 %2$d 附近(文本是 '%3$s')\n
53 词法错误
54 行 %1$d: 语法错误， 在 \"%2$s\"
55 丢失
56 在 %1$s
57 不兼容的参数, \"%1$s\", 和 \"%2$s\"\n
58 %1$d 个实例被提交\n
59 \n无法调用非优化版本的loaddb.\n
60 将 \"%1$s\" 解析为 %2$s 类型时出错.\n
112   %1$d 个实例\r
113 最后提交的行 : %1$d
114 错误控制文件
115 在加载过程中输入文件来控制错误
116 共插入了 %1$8d 个对象, 其中 %2$d 个对象插入失败.\n
117 装载失败.\n
118 类名的最大长度是 %1$d 字节.\n
120 \
loaddb: 将对象和结构导入到数据库.\n\
用法: %1$s loaddb [选项] 数据库名\n\
\n\
可用选项:\n\
  -u, --user=ID                 用于访问数据库的用户ID; 默认: "public"\n\
  -p, --password=PASS           用户密码; 默认: 空\n\
      --data-file-check-only    对数据文件进行语法检测而不加载; 默认: 检测之后加载\n\
  -l, --load-only               加载数据文件而不检测语法; 默认: 检测之后加载\n\
      --estimated-size=NUMBER   用NUMBER作为估计的实例数目; 默认: 5000\n\
  -v, --verbose                 开启详细状态信息; 默认: 关闭\n\
  -c, --periodic-commit=COUNT   每插入COUNT个实例进行周期性的提交; 默认: 在读取时不提交\n\
      --no-oid                  不使用 OID\n\
      --no-statistics           在读取时不更新统计信息\n\
  -s, --schema-file=FILE[:LINE] 从文件 FILE 的第 LINE 行创建结构(schema)\n\
  -i, --index-file=FILE[:LINE]  从文件 FILE 的第 LINE 行创建索引\n\
  -d, --data-file=FILE          从文件 FILE 读取数据\n\
  -t, --table=TABLE             用TABLE名替代数据文件中找不到表头的表\n\
      --error-control-file=FILE 指定文件 FILE 用来描述在读取数据过程中如何处理特定的错误\n\
      --ignore-class-file=FILE  指定文件 FILE 用来描述要忽略掉的类\n
121 \
load_noopt: 不赞成使用这个工具. 小心使用.\n\
用法: load_noopt [选项] 数据库名\n\
\n\
可用选项:\n\
  -u, --user=ID                 访问数据库的用户ID; 默认: "public"\n\
  -p, --password=PASS           用户密码; 默认: 空\n\
      --data-file-check-only    对数据文件进行语法检测而不加载; 默认: 检测后读取\n\
  -l, --load-only               读取数据而不进行语法检测; 默认: 检测后读取\n\
      --estimated-size=NUMBER   用NUMBER作为估计的实例数目; 默认: 5000\n\
  -v, --verbose                 启用详细状态信息; 默认: 不可用\n\
  -c, --periodic-commit=COUNT   当插入COUNT个实例进行周期性的提交; 默认: 读取是不提交\n\
      --no-oid                  不使用OID\n\
  -s, --schema-file=FILE[:LINE] 从文件 FILE 的第 LINE 行创建结构(schema)\n\
  -i, --index-file=FILE[:LINE]  从文件 FILE 的第 LINE 行创建索引\n\
  -d, --data-file=FILE          从文件 FILE 读取数据\n\
      --error-control-file=FILE 指定文件 FILE 用来描述在读取数据过程中如何处理特定的错误\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 缓存页数值无效.\n
42 缓存页大小无效.\n
43 %1$d 个对象被转储.\n
44 类 %1$s
45 , %1$d 个实例.\n
46 %2$d 个对象中只有 %1$d 个对象被成功转储.\n
47 输出目录过长.\n
48 LSA : %1$d %2$d \n
49 密码:
60 \
unloaddb: 从数据库导出对象和结构.\n\
用法: %1$s unloaddb [选项] 数据库名\n\
\n\
可用选项:\n\
  -u, --user=ID               访问数据库的用户ID; 默认: "dba"\n\
  -p, --password=PASS         用户密码; 默认: 空\n\
  -i, --input-class-file=FILE 指定包含表名的输入文件; 默认: 转储所有类\n\
      --include-reference     和-i选项一起使用.当卸载了指定数据库类，卸载对象的引用; 默认: disabled\n\
      --input-class-only      和-i选项一起使用. 只创建与输入文件中的表相关的方案(schema)文件; 默认: 未启用\n\
      --lo-count=COUNT        指明一个目录中要被创建的大对象(large object)文件的数目; 默认: 0\n\
      --estimated-size=NUMBER 指定预期的实例数目; 默认: 自动计算\n\
      --cached-pages=NUMBER   配置要被缓存进内存的对象数目; 默认: 100\n\
  -O, --output-path=PATH      用 PATH 作为输出目录; 默认: 工作目录\n\
  -s, --schema-only           只转储结构(schema); 默认: 结构和对象\n\
  -d, --data-only             只转储对象; 默认: 结构和对象\n\
      --output-prefix=PREFIEX 指定输出文件的前缀; 默认: 数据库名\n\
      --hash-file=FILE        指定hash文件名称; 默认: 由 tmpnam() 生成\n\
  -v, --verbose               开启详细状态信息; 默认: 不可用\n\
      --use-delimiter         用 '"' 作为一个标示符的开始和结束; 默认: 不使用\n\
  -S, --SA-mode               单机模式执行\n\
  -C, --CS-mode               客户端-服务器模式执行\n\
      --datafile-per-class    为每个表创建一个对象文件; 默认: 禁止\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \n传递 1\n\n
12 %1$d 个对象被处理.\n
13 \n传递 2\n\n
14 类 %1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d 个对象.\n
17 更新对象.\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 无法转换对象\n
20 类的heap文件缺失\n
21 无法更新对象\n
22 %2$d 个对象之中只有 %1$d 个对象被成功处理.\n
23 COMPACTDB 已经启动.\n
24 页的数目必须在 %1$d 和 %2$d 之间.\n
25 实例锁的超时时间必须在 %1$d 和 %2$d 之间.\n
26 对象总数: %1$d\n
27 失败的对象: %1$d\n
28 修改的对象: %1$d\n
29 大对象: %1$d\n
30 旧的类表述从目录中删除.\n
31 旧的类表述在目录中保留.\n
32 无法设置隔离级别.\n
33 无法压缩数据库.\n
34 类锁的超时必须在 %1$d 和 %2$d 之间.\n
35 类被锁住... 不完全的处理...\n
36 未处理的类...\n
37 处理错误... 不完全的处理...\n
38 没有什么可被处理...\n
39 无效的参数...\n
40 未知的类名...\n
41 未使用的类OID和页已被回收.\n
42 回收OID和页已经被跳过.\n
43 在处理类时出错, 类被跳过.\n
44 \n传递 3\n\n
45 heap压缩失败...\n
46 heap压缩成功...\n
60 \
compactdb: 释放数据库中未使用的空间.\n\
用法: %1$s compactdb [选项] 数据库名 [类名1 类名2 ...]\n\
\n\
可用选项:\n\
  -v, --verbose                     开启详细状态信息; 默认: 不可用\n\
  -i, --input-class-file=FILE       类名的输入文件\n\
                                    class_name 列表必须为空\n\
                                    默认: 压缩所有的类\n\
                                    只在CS模式中有效\n\
  -p, --pages-commited-once=NUMBER  一次提交的最大页数目\n\
                                    默认: 10\n\
                                    只在CS模式中可用\n\
  -S, --SA-mode                     单机模式执行\n\
  -C, --CS-mode                     客户端-服务端模式执行\n\
  -d, --delete-old-repr             从目录中删除旧的类表述; 默认: 不使用\n\
  class_name_list                   只在CS模式中有效\n\
  -I, --Instance-lock-timeout       实例锁的超时\n\
  -c, --class-lock-timeout          类锁的超时\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 运行在主机 %1$s 上的 CUBRID master进程\n在 %2$s 启动.
22 收到了 %1$d 请求. %2$d 个服务在运行.\n
23 在 %1$s 分钟内关闭\n
24 %1$s
25 要关闭的服务名 :
26 多少分钟后关闭? :
27 \
cub_commdb: 这个工具不赞成使用. 用 'cubrid' 命令来代替.\n\
用法: cub_commdb [选项]\n\
\n\
可用选项:\n\
  -P, --server-list                打印服务进程\n\
  -O, --all-list                   打印所有进程\n\
  -S, --shutdown-server=NAME       关闭名为 NAME 的数据库服务\n\
  -A, --shutdown-all               关闭所有进程\n\
  -c, --server-mode=NAME           打印名为 NAME 的数据库的HA 模式\n\
  -N, --node-list                  列出由 CUBRID heartbeat 所管理的结点\n\
  -L, --process-list               列出由 CUBRID heartbeat 所管理的本地进程\n\
  -p, --ping-host                  列出所有用于 ping CUBRID 心跳的主机\n\
  -D, --dereg-process=PID          退出目标进程并从被管理的进程条目中将其移除\n\
  -F, --reconfig-node-list         应用最近改变的 CUBRID heartbeat 配置\n\
  -V, --verbose                    开启详细状态信息; 默认: 不可用\n
28 对不起，未知的命令: %1$s\n
29 无法得到主机名或本地主机 - 终止\n
30 找不到参数文件\n
31 无法在 %1$s 连接主服务\n
32 多少分钟后关闭? :
33 主进程(Master) 将在 %1$d 分钟内关闭\n
34 从客户端那里收到了错误的数据
35 数据库服务 %1$s 当前没有运行
36 对 %2$s, %3$s, 无法执行 shell (%1$s) 
37 进程 fork %1$s, 返回 %2$d, %3$s
38 无法执行 %1$s
39 选项 -i(--immediately) 必须与选项 --deact-stop-all 一起使用.\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: 紧急情况下修补一个数据库.\n\
用法: %1$s emergency_patchlog [选项] 数据库名\n\
\n\
可用选项:\n\
  -r, --recreate-log           重建日志文件; 小心使用\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 错误的值 %1$d 对于 页的数目\n
21 错误的值 '%1$s' 对于卷存储目的.\n
22 警告: --max-writesize-in-sec 选项在单机模式下是忽略的.\n
60 \
addvoldb: 为数据库添加一个卷文件.\n\
用法: %1$s addvoldb [选项] 数据库名\n\
\n\
可用选项:\n\
      --db-volume-size=SIZE        指定附加卷大小; 默认: db_volume_size 在 cubrid.conf 文件中\n\
      --max-writesize-in-sec=SIZE  指定每秒写卷文件的上限值; (例如. 512K, 1M, 10M); 默认: 不指定; 最小值: 160K\n\
  -n, --volume-name=NAME           信息卷的名称; 默认: 像这样生成 "db"_ext1)\n\
  -F, --file-path=PATH             为将被添加的卷文件指定一个创建目录; 默认: 工作目录\n\
      --comment=COMMENT            给添加的卷文件加注释; 默认: 无\n\
  -p, --purpose=PURPOSE            指明添加卷文件的目的; 允许的值:\n\
                                           DATA - 为了数据\n\
                                           INDEX - 为了索引\n\
                                           TEMP - 临时的\n\
                                           GENERIC - 通用的\n\
  -S, --SA-mode                    单机模式执行\n\
  -C, --CS-mode                    客户端-服务器模式执行\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 一些不一致性在你的数据库中被检测出来.\n 请在错误日志文件 %1$s 中查阅额外的信息.\n
21 %1$s: 无法找到该表. 它将被忽略.\n
22 %1$s: 无法在表 %2$s 找到索引.\n
60 \
checkdb: 监测数据库一致性.\n\
用法: %1$s checkdb [选项] 数据库名 [class_name1 class_name2 ...]\n\
\n\
可用选项:\n\
  -S, --SA-mode                单机模式执行\n\
  -C, --CS-mode                客户端-服务器模式执行\n\
  -r, --repair                 修复找到的不一致性\n\
  -i, --input-class-file=FILE  输入包含表名的FILE\n\
                               默认: 检查所有表\n\
  -I, --index-name             指定要检查的索引名\n\
                               默认: 检查所有的索引\n\
      --check-prev-link        检查索引前面的链接\n\
      --repair-prev-link       检查并修复索引前面的链接\n\
      --check-file-tracker     检查文件中所有分配的页\n\
      --check-heap             检查堆文件的所有页\n\
      --check-catalog          检查目录的一致性\n\
      --check-btree            检查所有的 btree 索引\n\
      --check-class-name       检查类名的一致性\n\
      --check-btree-entries    检查 btree 条目的一致性\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 数据库 '%1$s' 的空间描述 页大小 %2$s. (日志页大小: %3$s)\n\n
16 %1$5d %2$9s  %3$s %4$s  %5$s\n
17 无法打开输出文件 '%1$s'\n
18 数据库 '%1$s' 临时卷的空间描述 页大小 %2$s.\n\n
19 卷标识  用途  页总数  空闲页  卷名\n\n
20 卷标识  用途  总大小  空闲空间  卷名\n\n
21 LOB 空间描述 %1$s \n\n
22 -------------------------------------------------------------------------------\n
30 数据库 '%1$s' 的 汇总空间描述 页大小 %2$s. (日志页大小: %3$s)\n\n
31 设定的    总页数  已用页数  空闲页数  卷数\n
32 设定的     总大小   已用大小   空闲大小  卷数\n
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
spacedb: 显示数据库的使用/空闲空间.\n\
用法: %1$s spacedb [选项] 数据库名\n\
\n\
可用选项:\n\
  -o, --output-file=FILE       重定向输出信息到文件 FILE; 默认: 无\n\
  -S, --SA-mode                单机模式执行\n\
  -C, --CS-mode                客户端服务端模式执行\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               设置显示单元 (页, M, G, T, 人类可读);默认 : 人类可读\n\
  -s, --summarize              为每个设定的卷只显示一个总结\n\
  -p, --purpose                显示每个卷上的每个 purpose(data/index/temp) 已使用的大小\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 预计页的数目 = %1$d\n
60 \
估算数据: 估算数据的大小.\n\
用法: %1$s 估算数据 实例的数目 实例的平均大小 属性的数目 可变属性数目\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 指定了不正确的索引键类型 = %1$s\n
16 当precision = %2$d, 指定了不正确的平均键长度 = %1$d\n
17 指定了错误的参数\n
20 估算的全部索引页数目 = %1$d\n (在实例被写入后，索引被创建.)\n
21 估算的全部索引页数目 = %1$d\n (在实例被写入前(平均情况)，索引被创建)\n
22 估算的全部索引页数目 = %1$d\n (在实例被写入前(最坏情况)，索引被创建)\n
23 用于给定的输入...\n
24 实例的总数               : %1$d\n
25 不同键的数目             : %1$d\n
26 键的平均大小             : %1$d\n
27 键的类型                 : %1$s\n\n
60 \
估算索引: 估算一个索引的大小.\n\
用法: %1$s 估算索引 实例的数目 不同键的数目 平均键长 键类型\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: 安装一个数据库.\n\
用法: %1$s installdb [选项] 数据库名\n\
\n\
可用选项:\n\
      --server-name=HOST       指定服务端的主机名; 默认: 当前主机名e\n\
  -F, --file-path=PATH         注册数据库的卷目录 PATH 到databases.txt; 默认: 当前目录\n\
  -L, --log-path=PATH          注册数据库的日志卷目录 PATH 到databases.txt; 默认: 和 file-path 相同\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 临时类OID\n
2 无法打印 elo 数值\n
3 访问用于迁移的长对象错误\n
4 打开用于迁移长对象的输出文件错误\n
5 读错误\n
6 写错误\n
7 打开ELO输入文件错误 "%1$s"\n
8 类:
9 哈希文件
10 名称:                         %1$s\n
11 大小:                         %1$d\n
12 页大小:                       %1$d\n
13 数据大小:                     %1$d\n
14 条目大小:                     %1$d\n
15 每一页的条目:                 %1$d\n
16 缓存的页:                     %1$d\n
17 条目的数目:                   %1$d\n
18 冲突的数目:                   %1$d\n
19 哈希文件名:                   %1$s\n
20 下一个溢出条目:               %1$d\n
21 键的类型:                     %1$s\n
22 页头的地址:                   %1$x\n
23 最后的页头地址:               %1$x\n
24 空闲页的头地址:               %1$x\n
25 页位图的地址:                 %1$x\n
26 页位图的大小:                 %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: 转储一个数据库.\n\
用法: %1$s diagdb [选项] 数据库名\n\
\n\
可用选项:\n\
  -d, --dump-type=TYPE         转储类型\n\
                               -1 转储所有\n\
                                1 转储文件表\n\
                                2 转储文件容量\n\
                                3 转储堆容量\n\
                                4 转储索引容量\n\
                                5 转储类名\n\
                                6 转储磁盘位图\n\
                                7 转储目录\n\
                                8 转储日志\n\
                                9 转储堆\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 无法打开输出文件 '%1$s'\n
59 lockdb 无法在单机模式下运行.\n
60 \
lockdb: 显示数据库的锁状态.\n\
用法: %1$s lockdb [选项] 数据库名\n\
\n\
可用选项:\n\
  -o, --output-file=FILE       重定向输出信息到文件 FILE ; 默认: 空\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 在事务索引，用户名，主机，程序名之间，只能指定一个参数\n
21 输入 DBA 密码:
26 没有一个事务带有指定的参数值.\n
27 准备取消下面的事务:\n\n
28 你希望进行吗 ? (Y/N)
29 取消与事务索引 %1$d相关联的事务\n
30 取消如下事务失败\n\n
31 事务目前还没有终止，但是可能在做一些回滚工作.\n 请稍等片刻，如果有必要，可以稍后再做尝试.\n
32 非法的tranindex(%1$s).\n
59 无法在单机模式下取消事务.\n
60 \
killtran: 管理数据库的事务.\n\
用法: %1$s killtran [选项] 数据库名\n\
\n\
可用选项:\n\
  -i, --kill-transaction-index=INDEX 通过事务索引取消事务\n\
      --kill-user-name=ID            通过用户ID取消所有事务\n\
      --kill-host-name=HOST          通过客户端主机名取消所有事务\n\
      --kill-program-name=NAME       通过客户端程序名取消所有事务\n\
      --kill-sql-id=SQL_ID           取消所有sql-id为SQL_ID的事务\n\
  -p, --dba-password=PASS            DBA用户的密码; 如果没有指定将提示输入\n\
  -d, --display-information          显示关于活动事务的信息\n\
  -q, --query-exec-info              显示活动事务查询执行的信息\n\
  -f, --force                        取消事务而不需要核对提示\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1 数据库名
2 需要参数, 没有默认值
3 兼容性检测
4 只进行兼容性检测
5 备份目的地
6 一个用于备份卷目文件的目录或设备
20 数据库 '%1$s' 不存在.
21 '%1$s' 无法被迁移到 %2$s, 因为数据库的版本太低.\n
22 '%1$s' 不需要被迁移.\n
23 '%1$s' 需要被迁移.\n
24 推荐在迁移后进行备份. 你希望现在就备份数据库吗? (y/n):
25 我们推荐在迁移后用backupdb命令备份你的新数据库.
26 \n*** 警告: 在迁移到 %1$s 前备份你的数据库***\n\n为了恢复可能在迁移中产生的错误，我们强烈推荐备份你的数据库.\n你已经备份了你的数据库吗? (y/n):
27 \n1. 运行 backupdb \n2. 为新的数据库输入新的备份路径.(当前路径是: %1$s)\n3. 退出 \n
28 \n数据库的新备份路径:
29 \n迁移和备份成功的完成.
30 备份失败. 请再次尝试备份数据库.
31 \n\n*** 警告 : 一旦迁移完成, 您不能用以前的备份卷来恢复数据.\n注意，只有在restoredb版本正确的情况下，之前版本的备份文件才可以被恢复.\n请确保之前的备份卷被安全的保存.\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: 改变数据库的主机.\n\
用法: %1$s alterdbhost [选项] 数据库名\n\
\n\
可用选项:\n\
  -h, --host=HOST              指定HOST为新的主机; 默认: 当前主机名\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 数据库名
2 必选参数, no default
3 java类文件
4 必选参数, no default
5 强制重写
6 如果类存在则进行重写, 默认 不

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 无法打开输出文件 '%1$s'\n
59 plandump 无法运行在单机模式.\n
60 \
plandump: 显示或控制缓存的查询计划.\n\
用法: %1$s plandump [选项] 数据库名\n\
\n\
可用选项:\n\
  -d, --drop                   丢弃服务端缓存中的所有计划\n\
  -o, --output-file=FILE       重定向输出信息到FILE; 默认: 空\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 无法打开输出文件 '%1$s'\n
21 系统参数将被客户端程序使用.\n
22 系统参数被服务 '%1$s' 读取\n
23 系统参数将被用于单机模式.\n
60 \
paramdump: 转储系统参数设置.\n\
用法: %1$s paramdump [选项] 数据库名\n\
\n\
可用选项:\n\
  -o, --output-file=FILE       重定向输出信息到FILE; 默认: 无\n\
  -b, --both                   转储客户端和服务端的参数\n\
  -S, --SA-mode                单机模式执行\n\
  -C, --CS-mode                客户端-服务端模式执行\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 错误的值 \"%1$s\" 对于服务端的HA运行模式.\n
12 无法改变服务端的HA运行模式.\n
21 输入DBA密码:
22 服务 '%1$s'的当前HA运行模式是 %2$s.\n
23 服务 '%1$s'的 HA运行模式被改变为\"%2$s\".\n
24 服务没有被设置为HA模式.\n
58 HA 特性在这个平台上不被支持.\n
59 changemode 无法以单机模式运行.\n
60 \
changemode: 显示或改变服务的HA运行模式.\n\
用法: %1$s changemode [选项] 数据库名\n\
\n\
可用选项:\n\
  -m, --mode=MODE             将服务模式改变为MODE; 默认: 空\n\
  -f, --force                 强制改变\n\
  -t, --timeout=S             当活动事务被正常终止时 等待 S 秒\n\
                              -t 选项只在用户改变服务模式为维护时有效\n\
                              默认: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 错误的值 \"%1$s\" 对于copylogdb的运行模式.\n
21 输入DBA的密码:
22 服务被设置为HA模式.\n
58 HA 特性在这个平台上不被支持.\n
59 copylogdb 无法以单机模式运行.\n
60 \
copylogdb: 从服务端拷贝日志页.\n\
用法: %1$s copylogdb [选项] 数据库名\n\
\n\
可用选项:\n\
  -L, --log-path=PATH         指定目录PATH用于拷贝日志卷\n\
  -m, --mode=MODE             操作模式; 默认: 同步\n\
                              模式: 同步 或 半同步 或 异步\n\
  -S, --start-page-id=ID      first log page to copy\n 

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 输入DBA密码:
22 服务没有被设置为HA模式.\n
58 HA 特性在这个平台上不被支持.\n
59 applylogdb 无法以单机模式运行.\n
60 \
applylogdb: 将已拷贝的日志应用到服务端.\n\
用法: %1$s applylogdb [选项] 数据库名\n\
\n\
可用选项:\n\
  -L, --log-path=PATH         将被读取的事务日志的目录\n\
      --max-mem-size=SIZE     进程可用的最大内存数, 以MB为单位; 最大为 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 无法打开输出文件 '%1$s'\n
59 statdump 无法以单机模式运行.\n
60 \
statdump: 转储全局统计信息.\n\
用法: %1$s statdump [选项] 数据库名\n\
\n\
可用选项:\n\
  -o, --output-file=FILE       重定向输出信息到文件FILE; 默认: 无\n\
  -c, --cumulative             打印累计状态\n\
  -i, --interval=S             每 S 秒打印一次状态\n\
  -s, --substr=STRING          只为匹配上得 子字符串打印状态\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 输入 DBA 密码:
22 服务没有被设置为HA模式.\n
58 HA 特性在这个平台上不被支持..\n
59 applylogdb 无法以单机模式运行.\n
60 \
applyinfo: 显示 CUBRID HA 应用信息.\n\
用法: %1$s applyinfo [选项] 数据库名\n\
\n\
可用选项:\n\
  -r, --remote-host-name      远程主机名; 显示远程节点的活动日志信息 \n\
  -a, --applied-info 	      显示运用的信息 \n\
  -L, --copied-log-path=PATH  设置复制的日志卷路径; 显示复制的日志信息 \n\
  -p, --page=ID               页 id; 默认: 0\n\
  -v, --verbose               显示详细状态信息; 默认 : 关闭\n\
  -i, --interval=S            设置每 S 秒打印一次信息\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59 acldb 无法在单机模式下运行.\n
60 \
acldb: 显示和加载数据的访问控制设置.\n\
用法: %1$s acldb [选项] 数据库名\n\
\n\
可用选项:\n\
  -r, --reload       加载IP 控制文件\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: 使用LDML 输入文件 生成区域共享库.\n\
用法: %1$s genlocale [选项] [language-string]\n\
\n\
language-string	      区域语言字符串用于生成指定共享库.\n\
		      如果没有指定语言字符串，所有在 'cubrid_locales.txt'文件中的语言\n\
		      都会被处理.\n\
可用选项:\n\
  -v, --verbose	              显示详细状态信息; 默认 : 关闭\n\
  -i, --input-ldml-file       输入LDML文件路径. 只在language-string被给定的时候使用.\n
61 非法的区域设置名称参数.\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 不能同时使用 -i/--input-file 和 本地标示.\n
58 非法 codepoint 范围 (-s -e).\n
59 非法的区域设置名称参数.\n
60 \
dumplocale: 输入LDML 文件输出以前生成的区域共享库的内容.\n\
用法: %1$s dumplocale [选项] [language-string]\n\
\n\
language-string	 区域语言字符串用于输出区域共享库.\n\
		         如果没有指定语言字符串，所有在 'cubrid_locales.txt'文件中的语言\n\
		         都会被处理.\n\
可用选项:\n\
  -i, --input-file    指定(先前生成的)区域共享库的路径.\n\
                      仅在language-string没有指定的时候使用.\n\
  -d, --calendar      输出日历和 日期/时间 信息; 默认 : 关闭\n\
  -n, --numeric		  输出 数字 信息; 默认 : 关闭\n\
  -a, --alphabet=l|lower|u|upper|both	     输出 字母和转换 信息; 默认 : 关闭\n\
      --identifier-alphabet=l|lower|u|upper	 为标识符输出 字母和转换 信息; 默认 : 关闭\n\
  -c, --codepoint-order	 基于codepoint输出排序后的核对信息\n\
                         (cp, char, weight, next-cp, char and weight);\n\
                         默认 : 关闭\n\
  -w, --weight-order  基于权重值输出排序后的核对信息\n\
                      (weight, cp, char); 默认 : 关闭\n\
  -s, --start-value	  过滤选项; 开始的 codepoint\n\
                      -a, --identifier-alphabet, -c, -w; 默认 : 0\n\
  -e, --end-value     过滤选项; 结束的 codepoint\n\
                      -a, --identifier-alphabet, -c, -w; \n\
                      默认 : 从区域共享库中读到的最大值\n\
  -k, --console-conversion  转储控制台转换信息; 默认 : 关闭\n\
  -z, --normalization   转储规范化数据; 默认 : 关闭\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 改变当前排序规则的分区属性之前，必须先移除分区.\nClass name\n
44 外键存在排序规则'%1$s'的，在改变排序规则属性之前必须先删除.\nTable | Foreign Key\n
45 表包含排序规则'%1$s'; 它们需要被重新定义 :\nClass name\n
46 函数索引的表达式中包含排序规则 '%1$s'; 它们需要被重新定义 :\n\
Table | Index | Function expression\n
47 同步排序规则没有执行. 数据库没有被改变.\n
48 系统表'%1$s'的内容被更新成新的系统排序规则.\n
49 更新系统排序规则可能导致系统， 是否继续 (y/n) ?\n
50 在新的系统配置中, 排序规则 '%1$s' (Id: %2$d) 在数据库中不存在或改变.\n
51 触发器的条件中包含排序规则 '%1$s'; 它们需要被重新定义 :\nTrigger | Condition\n
52 查询视图中包含排序规则 '%1$s'; 它们需要被重新定义 :\nView | Query\n
53 属性中包含排序规则 '%1$s'; 它们需要被重新定义.\nTable | Attribute Domain\n
54 需要手动干预数据库.\n\
请检查文件 '%1$s' 并在强制排序规则到数据库中前执行它.\n
55 不需要同步.\n
56 需要同步系统排序规则到数据库中.\n\
运行 'cubrid synccolldb -f %1$s'\n
57 相比数据库，系统配置中有 %1$d 个新的排序规则:\n
58 所有数据库'%1$s'中的排序规则都内在新的系统配置中找到.\n
59 数据库中的这些排序规则 %1$d 没有被配置或 与系统排序规则相比被改变.\n
60 \
synccolldb: 从数据库的排序规则同步到系统的排序规则.\n\
用法: %1$s synccolldb [选项] 数据库名\n\
\n\
可用选项::\n\
  -c, --check-only    检查从数据库的排序规则到系统的排序规则.\n\
		      如果需要同步排序规则， 返回错误代码 1.\n\
		      显示不兼容的排序规则报告.\n\
  -f, --force-only    更新数据库的排序规则与新的系统排序规则一致.\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 输入密码 %1$s: 
22 Tran index          User name      Host name      Process id    Program name   \n
23 -------------------------------------------------------------------------------\n
24 %1$4d%2$-10s %3$14s %4$14s %5$15d %6$17s\n
25 没有事务\n
26 tranlist只能被DBA或DBA组成员来执行.\n
27 sort-key 列序号 (%1$d) 不可用.\n
32 Tran index         Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
33 %1$4d%2$-10s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 Tran index          User name      Host name      Process id    Program name              Query time    Tran time              Wait for lock holder      SQL_ID       SQL Text\n
43 %1$4d%2$-10s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 Tran index : %1$d
46 SQL_ID: %1$s\n
59 tranlist 无法在单机模式下运行.\n
60 \
tranlist: 输出活动事务信息.\n\
用法: %1$s tranlist [选项] 数据库名\n\
\n\
可用选项:\n\
  -u, --user=ID                访问数据库的用户ID; default: DBA user\n\
  -p, --password=PASS          用户密码; default: none\n\
  -s, --summary                打印事务的汇总信息\n\
      --sort-key=COLUMN_NUMBER 列序号将被排序\n\
      --reverse                反转排序结果\n

$set 50 MSGCAT_UTIL_SET_PREFETCHLOGDB
22 服务器没有配置成 HA 模式.\n
23 预取功能已经被禁用.\n
58 HA 功能不支持此平台.\n
59 prefetchlogdb 不能作为 standalone 模式运行.\n
60 \
prefetchlogdb: 从服务器中预取日志页.\n\
用法: %1$s prefetchlogdb [选项] 数据库名\n\
\n\
可用选项:\n\
  -L, --log-path=PATH         指定拷贝日志卷的路径为 PATH\n
