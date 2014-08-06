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
1 レングスが正しくないか、正しくない文字「%1$c」がデータベース名「%2$s」に入っています。\n
2 出力ファイル「%1$s」がロードできません。\n
3 ユーティリティー名が抜いているか、または正しくありません。\n
4 正しくないユーティリティー名「%1$s」\n
5 次のリストはただしいユーティリティー名です。:\n
6 正しくない文字「%1$c」がボリューム名「%2$s」に入っています。\n
9 \
%1$s (CUBRID ユーティリティー)\n\
%2$s\n\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
10 \
cubrid 管理者ユーティリティー、バージョン　%1$s\n\
使い方: %2$s <ユーティリティー名> [オプション]\n\
格ユーティリティーに関するヘルプは「%3$s <ユーティリティー名>」コマンドを実行してください。\n\
\n\
正しいユーティリティー名:\n\
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
%4$sはDBMS管理道具です。\n\
追加情報は次のサイトをご覧になってください。http://www.cubrid.org\n
12 '%1$s'は正しくないユーティリティー名です。\n'cubrid --help'コマンドで使い方を確認してください。\n
13 '%1$s'は正しくないコマンドです。\n'cubrid --help'コマンドで使い方を確認してください。\n
14 設定ファイルから必要な一つ以上のアトリビュートを読み込むことができません。\n
15 @ %1$s %2$s: %3$s\n
16 @ %1$s %2$s\n
17 ++ %1$s '%2$s'が実行中ではありません。\n
18 ++ %1$s が実行中ではありません。\n
19 ++ %1$s '%2$s'が実行中です。\n
20 ++ %1$s が実行中です。\n
21 ++ %1$s %2$s: %3$s\n
22 パラメーターが足りません。\n'cubrid --help'コマンドで使い方を確認してください。\n
23 cubridユーティリティー、バージョン %1$s\n\
使い方: %2$s <ユーティリティー名> [パラメーター]\n\
'%3$s <utility-name>'コマンドで各ユーティリティーのヘルプをご覧になってください。\n\
\n\
正しいサービスユーティリティー名:\n\
    service\n\
    server\n\
    broker\n\
    manager\n\
    heartbeat\n\
\n\
正しい管理者ユーティリティー名:\n\
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
%4$sはDBMS道具です。\n\
追加情報は次のサイトをご覧になってください。http://www.cubrid.org\n
24 cubridユーティリティー、バージョン %1$s\n\
使い方: %2$s service <コマンド>\n\
\n\
正しいコマンド:\n\
    start\n\
    stop\n\
    restart\n\
    status\n
25 cubridユーティリティーバージョン %1$s\n\
使い方: %2$s server <コマンド> [パラメーター]\n\
\n\
正しいコマンド:\n\
    start       [データベース名]\n\
    stop        [データベース名]\n\
    restart     [データベース名]\n\
    status\n\
    acl         <status|reload> データベース名\n
37 サーバーはHA環境で設定されています。
26 cubridユーティリティー、バージョン %1$s\n\
使い方: %2$s broker <コマンド> [パラメーター]\n\
\n\
正しいコマンド:\n\
    start\n\
    stop\n\
    restart\n\
    info\n\
    status      [-b] [-f] [-q] [-t] [-s <sec>] [-S] [-P] [-m] [-c] [<expr>]\n\
    on          <ブローカー名>\n\
    off         <ブローカー名>\n\
    reset       <ブローカー名>\n\
    acl         <status|reload> [ブローカー名]\n\
    getid       -b <ブローカー名> [-f] シャードキー\n\
    test        <ブローカー名> [-D <database_name>] [-u <user_name>] [-p <user_password>] [-c <SQL_command>] [-i <input_file>] [-o <output_file>] [-v] [-s]\n
27 cubridユーティリティー、バージョン %1$s\n\
使い方: %2$s manager <コマンド>\n\
\n\
正しいコマンド:\n\
    start\n\
    stop\n\
    status\n
28 cubridユーティリティー、バージョン %1$s\n\
使い方: %2$s heartbeat <コマンド> [パラメーター]\n\
\n\
正しいコマンド:\n\
    start          [データベース名]\n\
    stop           [-h <hostname>] [-i] [データベース名]\n\
    copylogdb      <start|stop> [データベース名] [ノード名]\n\
    applylogdb     <start|stop> [データベース名] [ノード名]\n\
    prefetchlogdb  <start|stop> [データベース名] [ノード名]\n\
    replication    <start|stop> node-name\n\
    status         [-v]\n\
    reload\n
31 '%1$s'パラメーターが必要です。\n
32 データベース名が抜いています。\n
33 %1$s: このオプションは近いうち使えなくなります。\n
34 設定に正しくない内容があります。　%1$s %2$s\n
35 メモリーアロケーションが失敗しました。\n
36 サーバーがHAモードに設定されませんでした。\n
38 HA環境で設定されているサーバーがha_db_listのリストにありません。
39 HA環境で設定されているサーバーがha_node_listのリストにありません。
40 正しくないコマンドです。\n
41 CUBRID Managerサーバがインストールされていません。\n
42 無効なパラメーターです。\n
43 ファイル「%1$s」のオープンに失敗しました。\n

$set 2 MSGCAT_UTIL_SET_BACKUPDB
30 警告: -tオプションは独立実行モードでは無視されます。\n
31 エラー：ターゲットパスが存在しないか、ディレクトリではありません。\n
60 \
backupdb: データーベースをバックアップします。\n\
使い方: %1$s backupdb [オプション] <データーベース名>\n\
\n\
オプション:\n\
  -D, --destination-path=<バックアップ> ボリュームが位置するディレクトリーパス。指定されていない場合はログディレクトリー。\n\
  -r, --remove-archive               必要ないログファイルを消す。気をつけて使ってください。管理者マニュアルを確認してください。\n\
  -l, --level=level　                バックアップレベル。デフォルトは全体です。\n\
                                    次のようなバックアップレベルが使えます。\n\
                                    0　ー　全体\n\
                                    1　ー　インクリメント１\n\
                                    2　ー　インクリメント２\n\
  -o, --output-file=<파일>      詳しいバックアップメッセージをファイルに出力します。\n\
  -S, --SA-mode                独立実行モード\n\
  -C, --CS-mode                クライアント−サーバーモード\n\
      --no-check               データーベースの整合性を確認する作業を実行しません。;　デフォルト:実行\n\
  -t, --thread-count=COUNT     バックアップをするスレッド数。;　デフォルト:自動\n\
  -z, --compress               バックアップボリュームを圧縮します。;　デフォルト:圧縮しない\n\
  -e, --except-active-log      バックアップボリュームにアクティブログを含めません。;　デフォルト:含める\n\
      --sleep-msecs            １メガバイトを読むたびにNミリセコンドを休みます。;　デフォルト: 0 \n
71 「%1$s」と「%2$s」は共に使えません。\n


$set 3 MSGCAT_UTIL_SET_COPYDB
30 エラー: 対象の名前が原本の名前と同じです。\n
31 エラー: extended-volume-pathが存在しないか、または書く込むことができません。\n
32 エラー: control-fileがないか、読み込むことができません。\n
60 \
copydb: データーベースコピー。\n\
使い方: %1$s copydb [オプション] <原本データーベース名> <対象データベース名>\n\
\n\
オプション:\n\
      --server-name=HOST          サーバーホスト名; デフォルト: 現在ホスト名\n\
  -F, --file-path=PATH            データベースがセーブされるディレクトリーパス; デフォルト: 現在ディレクトリー\n\
  -L, --log-path=PATH             ログボリュームがセーブされるディレクトリーパス; デフォルト: <file-path>\n\
  -B, --lob-base-path=PATH        LOBファイルがセーブされるディレクトリーパス; デフォルト: <file-path>/lob\n\
  -E, --extended-volume-path=PATH 拡張ボリュームがセーブされるディレクトリーパス; デフォルト: データベースがセーブされるパスやcontrol-fileに入ってあるパス\n\
  -i, --control-file=FILE         格ボリュームがセーブされるディレクトリーパスを指定するコントロールするコントロールファイル; デフォルト: なし\n\
  -r, --replace                   同じ名前のデーターベースが存在するとオーバーライトする; デフォルト: オーバーライトしない\n\
  -d, --delete-source             コピーの後、原本データベースを削除。; デフォルト: 削除しない\n\
      --copy-lob-path             原本データベースのLOBファイルディレクトリーパスをコピー。「-B」オプションと共に使えない。; デフォルト: コピーしない\n


$set 4 MSGCAT_UTIL_SET_CREATEDB
40 グループ「%1$s」は存在しません。\n
41 ユーザー「%1$s」は存在しません。\n 
42 正しくないコマンド「%1$s」\n
43 出力ファイル「%1$s」が開けられません。\n
44 既存データベースが削除できません。\n
45 基本ロケール「%2$s」、サイズ「%1$s」のデーターベースを生成します。必要なディスクの容量は「%3$s」です。\n
46 データベースが生成できません。\n
47 ユーザーファイル「%1$s」に問題があります。\n
48 あげられたバリュー：「%1$s」。「%2$s」は範囲を超えてしまいました。「%3$s」と「%4$s」の内のバリューを入力してください。\n
49 %1$sの内容の中で「%2$s」が正しくありません。\n
60 \
createdb: データベース生成。\n\
usage: %1$s createdb [OPTION] database-name database-locale\n\
\n\
データーベースロケールのフォーマットは<language>.<charset>です。例：en_US.iso88591, ko_KR.utf8\n\
使い方:\n\
      --db-volume-size=SIZE           最初に生成されるボリューム(generic)のサイズ\n\
                                      デフォルト: 「cubrid.conf」の「db_volume_size」\n\
      --db-page-size=SIZE             バイト単位のデータベースページサイズ; デフォルト: 16K\n\
      --log-volume-size=SIZE          ログボリュームのサイズ\n\
                                      デフォルト: 「cubrid.conf」の「log_volume_size」\n\
      --log-page-size=SIZE            バイト単位のログページサイズ; デフォルト: データベースページサイズ\n\
      --comment=COMMENT               データベースのコメント; デフォルト: なし\n\
  -F, --file-path=PATH                データベースが生成されるディレクトリーパス; デフォルト: 現在ディレクトリー\n\
  -L, --log-path=PATH                 ログボリュームがセーブされるディレクトリーパス; デフォルト: <file-path>\n\
  -B, --lob-base-path=PATH            LOBファイルがセーブされるディレクトリーパス; デフォルト: <file-path>/lob\n\
      --server-name=HOST              サーバーホスト名; デフォルト: 現在ホスト\n\
  -r, --replace                       同じ名前のデータベースが存在するとオーバーライト; デフォルト: オーバーライトしない\n\
      --more-volume-file=FILE         加えて拡張されるボリュームに関するコントロールファイル\n\
      --user-definition-file=FILE     データベースユーザーを正義するファイル; デフォルト: なし\n\
      --csql-initialization-file=FILE データベース生成の後、CSQLでFILEにセーブされてあるクエリーを実行; デフォルト: なし\n\
  -o, --output-file=FILE              出力メッセージをファイルにセーブ; デフォルト: なし\n\
  -v, --verbose                       詳細なステータス情報を画面に出力; デフォルト: 出力しない\n


$set 6 MSGCAT_UTIL_SET_DELETEDB
60 \
deletedb: データベース削除\n\
使い方: %1$s deletedb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -o, --output-file=FILE       出力メッセージをセーブするファイル; デフォルト: なし\n\
  -d, --delete-backup          バックアップボリュームを消す; デフォルト: 消さない\n

$set 7 MSGCAT_UTIL_SET_RENAMEDB
31 エラー: extended-volume-pathが存在しないか、書き込むことができません。\n
32 エラー: control-fileがないか読み込むことができません。\n
60 \
renamedb: データベース名の変更\n\
使い方: %1$s renamedb [オプション] <原本データベース名> <対象データベース名>\n\
\n\
オプション:\n\
  -E, --extended-volume-path=PATH 拡張ボリュームがセーブされるディレクトリーパス; デフォルト: データベースセーブディレクトリーパス、またはコントロールファイルに入っているパス\n\
  -i, --control-file=FILE         格ボリュウムがセーブされるディレクトリーパスを指定する制御ファイル。; デフォルト: なし\n\
  -d, --delete-backup             バックアップボリュームを消す; デフォルト: 消さない\n

$set 9 MSGCAT_UTIL_SET_MASTER
11 %1$sプロセスがもう実行中です。\n
12 マスターサーバースタート\n
13 マスターサーバー終了\n
14 ローカルホスト名が発見できません。\n
15 パラメーターファイルが発見できません。\n
16 マスタープロセスが初期化できません。\n
17 サーバー%1$sは%2$d分内にシャットダウンします。
18 サーバー%1$sはシャットダウンされます。\nこの作業は何分かかる恐れがあります。しばらくお待ちください。
19 サーバー%1$sが発見できません。
20 マスターサーバーは%1$d分内にシャットダウンされます。\n
21 Heartbeat failover was finished. Please wait until all shutdown processes of cub_servers are completed.\n

$set 10 MSGCAT_UTIL_SET_OPTIMIZEDB
11 出力ファイル「%1$s」が開けられません。\n
60 \
optimizedb: データベースオプティマイズ。\n\
使い方: %1$s optimizedb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -n, --class-name=NAME        対象クラスのスタティクス情報を更新する。; デフォルト: 全体クラス\n


$set 11 MSGCAT_UTIL_SET_RESTOREDB
19 日付形式エラー、日付形式: 日-月-年:時:分:秒(dd-mm-yyyy:hh:mm:ss)、または「backuptime」\n
20 復旧がキャンセルされたか、またはエラー発生。\n
60 \
restoredb: データベース復旧\n\
使い方: %1$s restoredb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -d, --up-to-date=DATE             パラメーターの時間にデータベースを復旧。日付形式: dd-mm-yyyy:hh:mm:ssや 「backuptime」\n\
      --list                        復旧なしで、すべてのバックアップボリュームを画面に出力\n\
  -B, --backup-file-path=PATH       バックアップされたボリュームが存在するディレクトリーパス\n\
  -l, --level=LEVEL                 復旧に使うバックアップのレベル; デフォルト: 0(Full backup)\n\
  -p, --partial-recovery            アーカイブログがなければ強制にパーシャルリカバリーを実行する。\n\
  -o, --output-file=FILE            出力メッセージがセーブされるファイル; デフォルト: なし\n\
  -u, --use-database-location-path  データベース位置ファイルに設定されたパスにデータベースボリュームとログボリュームを復旧\n


$set 12 MSGCAT_UTIL_SET_LOADDB
19 データベース名が抜いています。\n
20 ファイル「%1$s」に%2$dのラインがありません。\n
21 インターラプト... \n
22 ロードがインターラプトされる - 現在トランザクションはコミットします。\n
23 ロードがインターラプトされる - 現在トランザクションはロールバックします。\n
24 ファイル「%1$s」が開けられません。\n
25 \n*** 文法チェック ***\n\n
26 %1$d個のエラーが検出されました。\n
27 \n*** オブジェクト挿入中 ***\n\n
28 総%1$8d個のオブジェクトが挿入されました。\n
29 総%1$8d個のれレファレンスされたオブジェクトがデフォルトバリューで入力されました。\n
30 \n*** トランザクションコミット中 ***\n
31 \n*** データベースを閉まっています ***\n
32 ライン %1$d:
33 「%4$s」のアトリビュート「%3$s」に対する「%1$s」を「%2$s」にするパーシングエラー。\n
34 「%2$s」のアトリビュート「%1$s」に対するドメインエラー\n
35 「%2$s」のアトリビュート「%1$s」に対してタイプ「%3$s」はsetのドメインに許可されていません。\n
36 「%2$s」のアトリビュート「%1$s」に対してタイプ「%3$s」を予測しましたが、setが入力されました。\n
37 「%2$s」のアトリビュート「%1$s」に対してタイプ「%3$s」を予測しましたが、「%4$s」が入力されました。
38 存在しないクラス「%1$s」が「%3$s」のアトリビュート「%2$s」にれレファレンスされました。
39 「%1$s」クラスは存在しません。タプルは無視されました。\n
40 識別者「%1$d」である存在しないクラスは「%3$s」のアトリビュート「%2$s」にレファレンスされました。\n
41 クラス「%1$s」はローダー入力ファイルに許可されません。\n
42 エラーが原因となる中断。\n
43 警告: 既存インスタンスを更新。\n
44 警告: クラス「%2$s」のインスタンス「%1$d」を再正義。\n
45 クラス「%2$s」のインスタンス「%1$d」はすでに正義されました。\n
46 クラス「%2$s」のインスタンス「%1$d」はもう使用されました。\n
47 アトリビュート「%1$s」のUNIQUE違反。NULLに変換しました。\n
48    %1$d インスタンス\n
49 クラス %1$s\n
50 パスワード: 
51 \n*** クラススタティスティックス情報を更新中 ***\n
52 %1$sエラーがライン%2$dで発生「%3$s」\n
53 Lexicalエラー
54 ライン %1$d: 「%2$s」で文法エラー
55 missing
56 in %1$s
57 一緒に使えないパラメーター「%1$s」と「%2$s」\n
58 %1$dインスタンスをコミットしました。\n
59 \n以前バージョンのloaddbが実行できません。\n 
60 「%1$s」を「%2$s」タイプにパーシングエラー \n
112   %1$d個のインスタンス\r
113 最後にコミットされたライン : %1$d
114 エラーコントロールファイル
115 ロードする時にエラー制御のための入力ファイル
116 Total %1$8d object(s) inserted, %2$d object(s) failed.\n
117 Load failed.\n
118 Maximum length of class name is %1$d bytes.\n
120 \
loaddb: データベースにオブジェクトやスキーマロード\n\
使い方: %1$s loaddb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -u, --user=ID                 データベースアクセスに関するユーザー名; デフォルト: 「public」\n\
  -p, --password=PASS           ユーザーパスワード; デフォルト: なし\n\
      --data-file-check-only    文法チェックだけを実行; デフォルト: 文法チェックの後、ロード\n\
  -l, --load-only               文法チェックなしでロード; デフォルト: 文法チェックの後、ロード\n\
      --estimated-size=NUMBER   インスタンスの予測数; デフォルト: 5000\n\
  -v, --verbose                 多様な進行情報を出力; デフォルト: 出力しない\n\
  -c, --periodic-commit=COUNT   周期的なコミットに関するカウント; デフォルト: ロード中にコミットしない\n\
      --no-oid                  OIDを未使用\n\
      --no-statistics           ロードの後、スタティスティックス情報を更新しない。\n\
  -s, --schema-file=FILE[:LINE] ファイル[:ライン]情報でスキーマ生成\n\
  -i, --index-file=FILE[:LINE]  ファイル[:ライン]情報でインデックス生成\n\
  -d, --data-file=FILE          ロードするデータファイル\n\
  -t, --table=TABLE             データをロードするテーブル名; データファイルにテーブル情報がない場合使う\n\
      --error-control-file=FILE ロード中に発生するエラーに関するコントロールファイル\n\
      --ignore-class-file=FILE  ロードしないクラス名が入っているファイル\n
121 \
load_noopt: このユーティリティーはもう追加支援しません。気をつけて使ってください。\n\
使い方: load_noopt [オプション] <データベース名>\n\
\n\
オプション:\n\
  -u, --user=ID                 データベースアクセスに関するユーザー名; デフォルト: 「public」\n\
  -p, --password=PASS           ユーザーパスワード; デフォルト: なし\n\
      --data-file-check-only    文法チェックだけを実行; デフォルト: 文法チェックの後、ロード\n\
  -l, --load-only               文法チェックなしでロード; デフォルト: 文法チェックの後、ロード\n\
      --estimated-size=NUMBER   インスタンスの予測数; デフォルト: 5000\n\
  -v, --verbose                 多様な進行情報を出力; デフォルト: 出力しない\n\
  -c, --periodic-commit=COUNT   周期的なコミットに関するカウント; デフォルト: ロード中にコミットしない\n\
      --no-oid                  OIDを未使用\n\
  -s, --schema-file=FILE[:LINE] ファイル[:ライン]情報でスキーマ生成\n\
  -i, --index-file=FILE[:LINE]  ファイル[:ライン]情報でインデックス生成\n\
  -d, --data-file=FILE          ロードするデータファイル\n\
      --error-control-file=FILE ロード中に発生するエラーに関するコントロールファイル\n


$set 13 MSGCAT_UTIL_SET_UNLOADDB
41 cached-pagesが正しくありません。\n
42 cached-page-sizeが正しくありません。\n
43 %1$dこのオブジェクトがダンプされました。\n
44 クラス %1$s
45 , %1$dこのインスタンス。\n
46 %1$dこのオブジェクトが(全体%2$d個)問題なくダンプされました。\n
47 出力ディレクトリー名が長過ぎます。\n
48 LSA : %1$d %2$d \n
49 パスワード: 
60 \
unloaddb: データベースからスキーマとオブジェクトをアンロード\n\
使い方: %1$s unloaddb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -u, --user=ID               データベースアクセスに関するユーザー名; デフォルト: 「dba」\n\
  -p, --password=PASS         ユーザーパスワード; デフォルト: なし\n\
  -i, --input-class-file=FILE テーブル名が入っているファイル; デフォルト: すべてのクラス\n\
      --include-reference     レファレンスされるテーブルも含める; デフォルト: 許可しない(「-i」が指定される必要がある)\n\
      --input-class-only      特定クラスだけを処理; デフォルト: 許可しない(「-i」が指定される必要がある)\n\
      --lo-count=COUNT        ひとつのディレクトリーにセーブするloファイルの数; デフォルト: 0\n\
      --estimated-size=NUMBER インスタンスの予測数; デフォルト: 自動で計算される\n\
      --cached-pages=NUMBER   キャッシュされたページの数; デフォルト: 自動で計算される\n\
  -O, --output-path=PATH      出力ディレクトリーパス; デフォルト: 現在作業ディレクトリー\n\
  -s, --schema-only           スキーマだけを処理; デフォルト: スキーマとオブジェクトを処理\n\
  -d, --data-only             オブジェクトだけを処理; デフォルト: スキーマとオブジェクト処理\n\
      --output-prefix=PREFIEX 出力ファイルのprefix; デフォルト: データベース名\n\
      --hash-file=FILE        ハッシュファイル名; デフォルト: ファイルを作るために「tmpnam()」使う。\n\
  -v, --verbose               多様な進行情報を出力; デフォルト: 出力しない\n\
      --use-delimiter         識別者の最初と最後に「"」をつける; デフォルト: つけない\n\
  -S, --SA-mode               独立モードで実行\n\
  -C, --CS-mode               クライアントーサーバモードで実行\n\
      --datafile-per-class    格クラス別にオブジェクトファイル生成; デフォルト:　ひとつのオブジェクトファイルを生成\n



$set 14 MSGCAT_UTIL_SET_COMPACTDB
11 \nパス 1\n\n
12 %1$dオブジェクトが処理されました。\n
13 \nパス 2\n\n
14 クラス　%1$s\n
15 oid:  %1$d|%2$d|%3$d,\n
16 %1$d インスタンス\n
17 オブジェクト更新中\n
18 ref_oid:  %1$d.%2$d.%3$d, ref_class_oid:  %4$d.%5$d.%6$d\n
19 オブジェクトを変換することができません。\n
20 クラスのヒープファイルがありません。\n
21 オブジェクトが更新できませんでした。\n
22 %1$d個のオブジェクト(全体%2$dオブジェクト)が問題なく処理されました。\n
23 COMPACTDBはすでにスタートしました。\n
24 ページの数は%1$dと%2$dの間しかできません。\n
25 インスタンスロックのタイムアウトは%1$dと%2$dしかできません。\n
26 全体オブジェクト: %1$d\n
27 失敗したオブジェクト: %1$d\n
28 修正されたオブジェクト: %1$d\n
29 大きいオブジェクト: %1$d\n
30 Old class representations deleted from catalog.\n
31 Old class representations preserved in catalog.\n
32 孤立レベルが設定できません。\n
33 データベースを圧縮できませんでした。\n
34 クラスラックのタイムアウトは%1$dと%2$dしかできません。\n
35 ロックされたクラス。作業が完全に終了できませんでした...\n
36 処理されなかったクラス...\n
37 プロセスエラー... 作業が完全に終了できませんでした...\n
38 処理するものがありません...\n
39 正しくないパラメーター...\n
40 正しくないクラス名...\n
41 使わないクラスのOIDとページはりクレームされました。\n
42 OIDとページのりクレームをスキップしました。\n
43 クラスを処理中にエラー発生、クラスをスキップしました。\n
44 パス 3\n\n
45 ヒープコンパクト失敗...\n
46 ヒープコンパクト成功...\n
60 \
compactdb: データベースで使用しない領域を解除。\n\
使い方: %1$s compactdb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -v, --verbose                     多様な進行情報を出力; デフォルト: 出力しない\n\
  -i, --input-class-file=FILE       テーブル名が入っているファイルを入力\n\
                                    class_nameリストは必ず空いている必要があります。\n\
                                    デフォルト: すべてのクラスをコンパクト\n\
                                    クライアントーサーバモードでしか作動しない。\n\
  -p, --pages-commited-once=NUMBER  一回にコミットする最大ページ数\n\
                                    デフォルト: 10\n\
                                    クライアントーサーバモードでしか作動しない。\n\
  -S, --SA-mode                     スタンドアロンモードで実行\n\
  -C, --CS-mode                     クライアントーサーバモードで実行\n\
  -d, --delete-old-repr             カタログから古いクラス情報を削除; デフォルト: 削除しない\n\
  class_name_list                   クライアントーサーバモードでしか作動しない。\n\
  -I, --Instance-lock-timeout       インスタンスロックのタイムアウト\n\
  -c, --class-lock-timeout          クラスロックのタイムアウト\n

$set 15 MSGCAT_UTIL_SET_COMMDB
21 CUBRIDマスターがホスト%1$sで実行中です。%2$sに始まって
22 %1$d個のリクエストを行いました。\n現在%2$d個のサーバーが作動中です。\n
23 %1$s分以内にシャットダウンします!!!\n
24 %1$s
25 シャットダウンするサーバー名 :
26 シャットダウンするまでの時間(分)? :
27 \
cub_commdb: このユーティリティーは使わないでください。その代わり「cubrid」コマンドを使ってください。\n\
使い方: cub_commdb [オプション]\n\
\n\
オプション:\n\
  -P, --server-list                サーバープロストを出力\n\
  -O, --all-list                   すべてのプロセスを出力\n\
  -S, --shutdown-server=NAME       NAMEデータベースをシャットダウン\n\
  -A, --shutdown-all               すべてのプロセスをシャットダウン\n\
  -c, --server-mode=NAME           NAMEデータベースのHAモードを出力\n\
  -N, --node-list                  CUBRID heartbeatに管理されるノードリストを出力\n\
  -L, --process-list               CUBRID heartbeatに管理されるローカルプロセス情報を出力\n\
  -p, --ping-host                  list ping hosts used for CUBRID heartbeat\n\
  -D, --dereg-process=PID          対象プロセスを終了して管理リストから削除\n\
  -F, --reconfig-node-list         CUBRID heartbeatノードの設定を再適用\n\
  -V, --verbose                    多様な進行情報を出力; デフォルト: 出力しない\n
28 正しくないコマンドです。: %1$s\n
29 ローカルホスト名が検索できません。　-　中止\n
30 パラメーターファイルが検索できません。\n
31 %1$sからマスターサーバーにコネクトできません。\n
32 何分あとにシャットダウンしますか？\n
33 マスター%1$d分あとにシャットダウンします。\n
34 クライアントーからでーたが受け取れない。
35 データベースサーバー%1$sは現在作動中ではありません。
36 シェル「%1$s」が始まりません。%2$s, %3$s 
37 プロセスfork %1$s, 返還 %2$d, %3$s
38 %1$sが実行せきません。
39 -i(--immediately) オプションは、--deact-stop-all オプションと一緒に使用する必要があります。\n

$set 16 MSGCAT_UTIL_SET_PATCHDB
60 \
emergency_patchlog: 緊急状況でデータベースログのパッチ\n\
使い方: %1$s emergency_patchlog [オプション] <データベース名>\n\
\n\
オプション:\n\
  -r, --recreate-log           ログファイルをまた生成する。\n


$set 17 MSGCAT_UTIL_SET_ADDVOLDB
20 ページ数%1$dが正しくありません。\n
21 ボリュームの使い道'%1$s'が正しくありません。\n
22 WARNING：--max-writesize-in-secオプションはスタンドアローンモードでは無視されます。\n
60 \
addvoldb: データベースボリュームファイルを追加。\n\
使い方: %1$s addvoldb [オプション] <データベース名>\n\
\n\
オプション:\n\
      --db-volume-size=SIZE        追加するボリュームのサイズ; デフォルト: 設定から読み込む。\n\
      --max-writesize-in-sec=SIZE  一秒に書かれるボリューム量; (ex. 512K, 1M, 10M); デフォルト: 使用しない; 最小: 160K\n\
  -n, --volume-name=NAME           ボリューム名; デフォルト: "db"_ext1のような名前で生成。\n\
  -F, --file-path=PATH             データベースファイルのパス; デフォルト: 現在ディレクトリー\n\
      --comment=COMMENT            ボリュームコメント; デフォルト: なし\n\
  -p, --purpose=PURPOSE            ボリュームの使い道; 次のような使い道ができます。\n\
                                           DATA - データヴォリューム\n\
                                           INDEX - インデックスボリューム\n\
                                           TEMP - 臨時データボリューム\n\
                                           GENERIC - 一般データボリューム\n\
  -S, --SA-mode                    スタンドアロンモードで実行\n\
  -C, --CS-mode                    クライアントーサーバモードで実行\n

$set 18 MSGCAT_UTIL_SET_CHECKDB
20 データベースに非一貫性が発見されました。\n追加情報は「error_log_file %1$s」をご覧になってください。\n
21 %1$s: クラスが見つからないため、スキップします。\n
22 %1$s: Cannot find the index on class %2$s.\n
60 \
checkdb: データベース一貫性をチェック\n\
使い方: %1$s checkdb [オプション] <データベース名> [class_name1 class_name2 ...]\n\
\n\
オプション:\n\
  -S, --SA-mode                スタンドアロンモードで実行\n\
  -C, --CS-mode                クライアントーサーバモードで実行\n\
  -r, --repair                 非一貫性を発見すると復旧する\n\
  -i, --input-class-file=FILE  クラスリストが入ってあるファイル名\n\
                               デフォルト: すべてのクラスをチェック\n\
  -I, --index-name             インデックス名（チェックのみです）\n\
                               デフォルト：すべてのインデックスをチェック\n\
      --check-prev-link        インデックスの前のリンクをチェック\n\
      --repair-prev-link       インデックスの前のリンクを修復します。\n\
      --check-file-tracker     ファイルのすべての割り当てられたページをチェックします。\n\
      --check-heap             ヒープのファイルのすべてのページをチェックします。\n\
      --check-catalog          カタログの整合性をチェックします。\n\
      --check-btree            すべてのBツリーインデックスをチェックします。\n\
      --check-class-name       クラス名の整合性をチェックします。\n\
      --check-btree-entries    BTREEエントリの整合性をチェックします。\n


$set 19 MSGCAT_UTIL_SET_SPACEDB
15 データベース「%1$s」(ページサイズ%2$s)のボリューム情報(ルグページサイズ%3$s)\n\n
16 %1$5d %2$9s  %3$s %4$s  %5$s\n
17 出力ファイル「%1$s」が見つかりません。\n
18 \nデータベース「%1$s」(ページサイズ%2$s)の臨時ボリューム。\n\n
19 番号  使い道       ページ数  フリーページ  ボリューム名\n\n
20 番号  使い道       トータルサイズ    フリーサイズ  ボリューム名\n\n
21 LOBスペース %1$s\n\n
22 -------------------------------------------------------------------------------\n
30 データベース「%1$s」(ページサイズ%2$s)ボリュームの使い道別の合算(ログページサイズ%3$s)\n\n
31 使い道         ページ数  ユーズドページ  フリーページ  ボリューム数\n
32 使い道         トータルサイズ    ユーズドスペース    フリースペース  ボリューム数\n
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
spacedb: データベースのディスクスペース情報を出力\n\
使い方: %1$s spacedb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -o, --output-file=FILE       ファイルに出力する; デフォルト: 画面に出力\n\
  -S, --SA-mode                スタンドアロンモードで実行\n\
  -C, --CS-mode                クライアントーサーバモードで実行\n\
      --size-unit={PAGE|M|G|T|H}\n\
                               出力の単位(page, mega, giga, tera, human readable)設定; デフォルト: H\n\
  -s, --summarize              ボリュームの使い道別に合算を出力\n\
  -p, --purpose                各自の目的で使われたボリュームのサイズを表示します(data/index/temp)\n


$set 20 MSGCAT_UTIL_SET_ESTIMATEDB_DATA
15 ページの推定数 = %1$d\n
60 \
estimate_data: データサイズの推定\n\
使い方: %1$s estimate_data インスタンス数 平均インスタンスサイズ アトリビュート数 アトリビュートバリアブル数\n


$set 21 MSGCAT_UTIL_SET_ESTIMATEDB_INDEX
15 正しくないインデックスキータイプです。 = %1$s\n
16 平均キーが正しくありません。 レングス = %1$d, プレシジョン = %2$d\n
17 正しくないパラメーターがあります。\n
20 予想トータルインデックスページ数 = %1$d\n(インスタンスが生成されてインデックスが生成される場合)\n
21 予想トータルインデックスページ数 = %1$d\n(インスタンスが生成される前にインデックスが生成される場合(average case))\n
22 予想トータルインデックスページ数 = %1$d\n(インスタンスが生成される前にインデックスが生成される場合(worst case))\n
23 入力されたバリュー...\n
24 インスタンスのトータル数   : %1$d\n
25 お互いに違うキーの数      : %1$d\n
26 平均キーサイズ           : %1$d\n
27 キータイプ              : %1$s\n\n
60 \
estimate_index: インデックスサイズを推定\n\
使い方: %1$s estimate_index インスタンス数 お互いに違うキー 平均キーサイズ キータイプ\n

$set 22 MSGCAT_UTIL_SET_INSTALLDB
60 \
installdb: データベースをインストールする\n\
使い方: %1$s installdb [オプション] <データベース名>\n\
\n\
オプション:\n\
      --server-name=HOST       サーアーホスト名; デフォルト: 現在ホスト名\n\
  -F, --file-path=PATH         データベースボリュームのパス; デフォルト: 現在ディレクトリー\n\
  -L, --log-path=PATH          ログボリュームのディレクトリーパス; デフォルト: file-pathと同じ\n

$set 23 MSGCAT_UTIL_SET_MIGDB
1 臨時クラスOID\n
2 ELOバリューが出力できません。\n
3 long objectアクセスエラー\n
4 long objectマイグレーションのための出力ファイルエラー\n
5 読み込みエラー\n
6 書き込みエラー\n
7 ELO入力ファイル「%1$s」の読み込みエラー\n
8 クラス:
9 ハッシュファイル名
10 名前:　　　　　　　　　　　　　　　　　　　%1$s\n
11 サイズ:                   　   　%1$d\n
12 ページサイズ:             　      %1$d\n
13 データサイズ:            　       %1$d\n
14 エントリーサイズ:        　        %1$d\n
15 ページ別エントリー: 　   　         %1$d\n
16 キャッシュされたページ:          　 %1$d\n
17 エントリー数:      　　           %1$d\n
18 衝突数:     　　　　　　           %1$d\n
19 ハッシュファイル名:                %1$s\n
20 次のオーバーフローエントリー:　        %1$d\n
21 キータイプ:                      %1$s\n
22 ページヘッダーアドレス:    　        %1$x\n
23 ラストベージヘッダーアドレス: 　　　    %1$x\n
24 フリーページヘッダーアドレス:         %1$x\n
25 ページビッドマップアドレス:          %1$x\n
26 ページビッドマップサイズ:            %1$d\n


$set 24 MSGCAT_UTIL_SET_DIAGDB
60 \
diagdb: データベース情報をダンプ\n\
使い方: %1$s diagdb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -d, --dump-type=TYPE         ダンプタイプ\n\
                               -1 全部ダンプ\n\
                                1 ファイルテーブルをダンプ\n\
                                2 ファイルサイズをダンプ\n\
                                3 ヒープサイズをダンプ\n\
                                4 インデックスサイズをダンプ\n\
                                5 クラス名をダンプ\n\
                                6 ディスクビットマップをダンプ\n\
                                7 カタログをダンプ\n\
                                8 ログをダンプ\n\
                                9 ヒープをダンプ\n


$set 25 MSGCAT_UTIL_SET_LOCKDB
15 出力ファイル「%1$s」が開けません。\n
59 lockdbはスタンドアロンモードで実行できません。\n
60 \
lockdb: データベースのロック情報を出力\n\
使い方: %1$s lockdb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -o, --output-file=FILE       出力メッセージをセーブするファイル; デフォルト: なし\n


$set 26 MSGCAT_UTIL_SET_KILLTRAN
20 次のパラメータの中でひとつしか許可しない: トランザクションーインデックス、ユーザー、ホスト、プログラム\n
21 DBAパスワード:
26 パラメーターに対するトランザクションがありません。\n
27 次のトランザクションを終了させる支度が終わりました:\n\n
28 続きますか？ (Y/N)
29 トランザクションーインデックス「%1$d」で指定されたトランザクションを終了させます。\n
30 次のトランザクションを終了が失敗しました。\n\n
31 トランザクションが終わりませんでした。ロールバックかも知れません。\nしばらく待ってからまたやり直してください。\n
32 正しくないトランザクションインデックス「%1$s」。\n
59 killtranはスタンドアロンモードで実行できません。\n
60 \
killtran: データベーストランザクションを管理\n\
使い方: %1$s killtran [オプション] <データベース名>\n\
\n\
オプション:\n\
  -i, --kill-transaction-index=INDEX INDEXに対するトランザクションを終了\n\
      --kill-user-name=ID            IDに対するユーザーのトランザクションを全部終了\n\
      --kill-host-name=HOST          HOSTに対するクライアントのトランザクションを全部終了\n\
      --kill-program-name=NAME       NAMEに対するトランザクションを全部終了\n\
      --kill-sql-id=SQL_ID           SQL_IDに対するすべてのトランザクションを中止する。\n\
  -p, --dba-password=PASS            DBAパスワード; 指定されてない場合は入力プロンプトが表示される。\n\
  -d, --display-information          現在アクティブトランザクションの情報を表示\n\
  -q, --query-exec-info              アクティブトランザクションのクエリ実行情報を表示する。\n\
  -f, --force                        強制に確認なしでトランザクションを終了\n


$set 31 MSGCAT_UTIL_SET_MIGRATE
1  データベース名
2  必須パラメーター; デフォルト: なし
3  互換性チェック
4  互換性チェックのみ実行
5  バックアップパス
6  バックアップボリュームをセーブするディレクトリーパスやディバイス名
20 データベース「%1$s」が存在しません。
21 「%1$s」はバージョンが低すぎるので「%2$s」にマイグレーションできません。\n
22 「%1$s」はマイグレーションが要りません。\n
23 「%1$s」はマイグレーションが必要です。\n
24 マイグレーションの後、バックアップすることをおすすめします。今バックアップしますか？ (y/n): 
25 マイグレーションの後、バックアップすることをおすすめします。
26 \n*** 警告: %1$sにマイグレーションする前にデータベースをバックアップしてください ***\n\nマイグレーションの途中に問題が発生する恐れがあるので、障害を復旧するために必ずバックアップが必要になります。\nバックアップしましたか？ (y/n): 
27 \n1. バックアップ実行 \n2. 新しいデータベースのバックアップパスを入力。(現在バックアップパス: %1$s) \n3. 終了 \n
28 \n新しいデータベースのバックアップパス: 
29 \nマイグレーションとバックアップを問題なく完了しました。
30 バックアップに失敗しました。また遣り直してください。
31 \n\n*** 警告: マイグレーションが完了すると、以前バージョンのバックアップファイルを使って復旧できません。\n以前バージョンのバックアップファイルは、当バージョンでしか復旧できません。\n以前バージョンのバックアップファイルを保存してください。 ***\n

$set 33 MSGCAT_UTIL_SET_ALTERDBHOST
60 \
alterdbhost: データベースホストを変更\n\
使い方: %1$s alterdbhost [オプション] <データベース名>\n\
\n\
オプション:\n\
  -h, --host=HOST              ホスト名; デフォルト: 現在ホスト名\n


$set 34 MSGCAT_UTIL_SET_LOADJAVA
1 データベース名
2 必須パラメーター; デフォルト: なし
3 ジャバークラスーファイル
4 必須パラメーター; デフォルト: なし
5 強制ーオーバーライト
6 クラスファイルがもう存在したらオーバーライトする。; デフォルト: オーバーライトしない

$set 37 MSGCAT_UTIL_SET_PLANDUMP
15 出力ファイル「%1$s」が開けません。\n
59 plandumpはスタンドアロンモードで実行できません。\n
60 \
plandump: クエリープラン表示と管理\n\
使い方: %1$s plandump [オプション] <データベース名>\n\
\n\
オプション:\n\
  -d, --drop                   サーバーキャッシュに存在するプランを全部削除\n\
  -o, --output-file=FILE       出力メッセージを書き込むファイル; デフォルト: なし\n

$set 38 MSGCAT_UTIL_SET_DUMPPARAM
11 出力ファイル「%1$s」が開けません。\n
21 システムパラメーターはクライアントプログラムに使用されます。\n
22 システムパラメーターはサーバー「%1$s」にロードされます。\n
23 システムパラメーターはスタンドアロンモードでしか使えません。\n
60 \
paramdump: システムパラメーター設定をダンプする。\n\
使い方: %1$s paramdump [オプション] データベース名\n\
\n\
オプション:\n\
  -o, --output-file=FILE       出力メッセージを書き込むファイル; デフォルト: なし\n\
  -b, --both                   クライアントとサーバー両方のパラメーターをダンプ\n\
  -S, --SA-mode                スタンドアロンモードで実行\n\
  -C, --CS-mode                クライアントーサーバモードで実行\n

$set 39 MSGCAT_UTIL_SET_CHANGEMODE
11 「%1$s」はサーバーのHA実行モードに正しくありません。\n
12 サーバーのHA実行モードを変えられません。\n
21 DBAパスワードを入力してください:
22 サーバー「%1$s」の現在HA実行モード： %2$s\n
23 サーバ「%1$s」のHA実行モードは「%2$s」になりました。\n
24 サーバーがHAモードに設定されていません。\n
58 HAを支援しないプラットホームです。\n
59 changemodeはスタンドアロンモードで実行できません。\n
60 \
changemode: サーバーのHA実行モードの表示と変更。\n\
使い方: %1$s changemode [オプション] データベース名\n\
\n\
オプション:\n\
  -m, --mode=MODE             サーバーモードをMODEに変更する; デフォルト: なし\n\
  -f, --force                 強制変更\n\
  -t, --timeout=S             トランザクションが問題なく終了するまでS秒を待つ\n\
                              -tオプションはユーザーがサーバーモードからメインテナンスモードに変更する時効果的です。\n\
			      デフォルト: 5\n

$set 40 MSGCAT_UTIL_SET_COPYLOGDB
11 「%1$s」はcopylogdbの実行モードではありません。\n
21 DBAパスワードを入力してください。:
22 サーバーがHAモードで設定されていません。\n
58 HAを支援しないプラットホームです。\n
59 copylogdbはスタンドアロンモードで実行できません。\n
60 \
copylogdb: サーバーからログページをコピー\n\
使い方: %1$s copylogdb [オプション] データベース名\n\
\n\
オプション:\n\
  -L, --log-path=PATH         コピーしたログボリュームのディレクトリーパス\n\
  -m, --mode=MODE             運営モード; デフォルト: sync\n\
                              MODE: sync or semisync or async\n\
  -S, --start-page-id=ID      first log page to copy\n 

$set 41 MSGCAT_UTIL_SET_APPLYLOGDB
21 DBAパスワードを入力してください。:
22 サーバーがHAモードで設定されていません。\n
58 HAを支援しないプラットホームです。\n
59 applylogdbはスタンドアロンモードで実行できません。\n
60 \
applylogdb: サーバーからコピーしたログを適用する。\n\
usage: %1$s applylogdb [オプション] データベース名\n\
\n\
オプション:\n\
  -L, --log-path=PATH         コピーしたログボリュームのディレクトリーパス\n\
      --max-mem-size=SIZE     最大プロセスサイズ(MB); 最大 500\n

$set 43 MSGCAT_UTIL_SET_STATDUMP
11 出力ファイル「%1$s」が開けません。\n
59 statdumpはスタンドアロンモードで実行できません。\n
60 \
statdump: グローバル総計を出力。\n\
使い方: %1$s statdump [オプション] <データベース名>\n\
\n\
valid options:\n\
  -o, --output-file=FILE       出力メッセージを書き込むファイル; デフォルト: なし\n\
  -c, --cumulative             総計を出力\n\
  -i, --interval=S             S秒の旅に通計を出力\n\
  -s, --substr=STRING          指定したストリングが含まれた項目だけ出力\n

$set 44 MSGCAT_UTIL_SET_APPLYINFO
21 DBAパスワードを入力してください。:
22 サーバーがHAモードで設定されていません。\n
58 HA機能を支援しないプラットホームです。\n
59 applyinfoはスタンドアロンモードで実行できません。\n
60 \
applyinfo: CUBRID HA Apply情報を出力。\n\
使い方: %1$s applyinfo [オプション] データベース名\n\
\n\
オプション:\n\
  -r, --remote-host-name      リモートホスト名; リモートノードのアクティブログ情報を出力\n\
  -a, --applied-info          適用された情報を出力\n\
  -L, --copied-log-path=PATH  コピーしたログブリュームのパス; コピーしたログ情報を出力。\n\
  -p, --page=ID               ページID; デフォルト : 0\n\
  -v, --verbose               多様な進行情報を出力; デフォルト: 出力しない\n\
  -i, --interval=S            S秒のたびに表示される情報を更新する。\n

$set 45 MSGCAT_UTIL_SET_ACLDB
59  acldbはスタンドアロンモードで実行できません。\n
60 \
acldb: サーバーのアクセス権限リストを出力するか、またはもう一度読み込みます。\n\
使い方: %1$s acldb [オプション] <データベース名>\n\
\n\
オプション:\n\
  -r, --reload       アクセス権限ファイルをもう一度読み込む。\n

$set 46 MSGCAT_UTIL_SET_GENLOCALE
60 \
genlocale: LDML入力ファイルを使ってローケルシェアードライブラリーを生成する。\n\
使い方: %1$s genlocale [オプション] [language-string]\n\
\n\
language-string	      生成するシェアードライブラリーのロケール言語。\n\
                      language-stringがなければ「cubrid_locales.txt」にあるすべての\n\
		                  言語に対してライブラリを生成する。\n\
オプション:\n\
  -v, --verbose	              多様な進行情報を出力; デフォルト: 出力しない\n\
  -i, --input-ldml-file       LDMLファイルのパス。language-stringが必要になる。\n
61 正しくないロケール名パラメーター。\n

$set 47 MSGCAT_UTIL_SET_DUMPLOCALE
57 -i/--input-fileオプションとlanguage-stringは一緒に使えません。\n
58 正しくないコードポイント範囲(-s -e).\n
59 正しくないロケール名パラメーター。\n
60 \
dumplocale: LDML入力ファイルを使って生成したロケールシャードライブラリーの内容をダンプする。\n\
使い方: %1$s dumplocale [オプション] [language-string]\n\
\n\
language-string　　　　　生成するシェアードライブラリーのロケール言語。\n\
		                  language-stringがなければ「cubrid_locales.\n\
                      txt」にあるすべての言語に対してライブラリを生成する。\n\
オプション:\n\
  -i, --input-file    入力したロケールシェアードライブラリーのパス(もう生成されたあるもの).\n\
                      language-stringパラメーターある時しか使えません。\n\
  -d, --calendar      カレンダーとデート／タイム情報をダンプ; デフォルト: しない\n\
  -n, --numeric　　　　　ニューメリック情報をダンプ; デフォルト: しない\n\
  -a, --alphabet=l|lower|u|upper|both　      アルファベットとキャスティング情報をダンプ; デフォルト: しない\n\
      --identifier-alphabet=l|lower|u|upper　識別者のアルファベットとキャスティング情報をダンプ; デフォルト : しない\n\
  -c, --codepoint-order   コードポイントに整列されていた収集情報をダンプ\n\
                          (cp, char, weight, next-cp, char and weight);\n\
                          デフォルト : しない\n\
  -w, --weight-order  ウエートバリューに整列されていた収集情報をダンプ\n\
                      (weight, cp, char); デフォルト : しない\n\
  -s, --start-value	  フィルタリングオプション; -a, --identifier-alphabet, -c, -wの\n\
                      スターティングコードポイント; デフォルト : 0\n\
  -e, --end-value     フィルタリングオプション; -a, --identifier-alphabet, -c, -w\n\
                      エンドポイント; \n\
                      デフォルト : ロケールシェアードライブラリーから読み込んだ最大バリュー\n\
  -k, --console-conversion　コンソール返還データをダンプ; デフォルト : しない\n\
  -z, --normalization 正規化データをダンプ; デフォルト : しない\n

$set 48 MSGCAT_UTIL_SET_SYNCCOLLDB
43 パーティショニングアトリビュートの現在コーレーション「%1$s」を変える前にパーティショニングを削除しないといけません。\nクラス名\n
44 アトリビュートのコーレーションを変える前に「%1$s」コーレーションを持つ外来キーを削除しないといけません。\nテーブル | 外来キー\n
45 クラスのコーレーション「%1$s」；必ず再正義する必要があります。:\nクラス名\n
46 ファンクションインデックスはコーレーション「%1$s」を含めるエクスプレッションを持ちます。；必ず再正義する必要があります。:\n\
テーブル | インデックス | ファンクションエクスプレッション\n
47 コーレーションの同期が実行されませんでした。データベースは変えられませんでした。\n
48 システムテーブル「%1$s」の内容が新しいシステムコーレーションに更新されました。\n
49 システムコーレーションの更新はデータベースを壊す原因になるかもしれません。続けましか(y/n) ?\n
50 コーレーション「%1$s」(Id: %2$d)はデータベースにないか、またはもう新しいシステム設定に更新されました。\n
51 トリガーはコーレーション「%1$s」を含めるコンディションを持っています。;必ず再正義する必要があります。:\nトリガー | コンディション\n
52 クエリビューはコーレーション「%1$s」を含めています。; 必ず再正義する必要があります。 :\nビュー | クエリ\n
53 アトリビュートはコーレーション「%1$s」を含めています。; 必ず再正義する必要があります。\nテーブル | アトリビュートドメイン\n
54 マニュアルでデータベースをチェックする必要があります。\n\
データベースのコーレーションを強制に適用する前にファイル「%1$s」をチェックしてください。\n
55 同機する必要がありません。\n
56 データベースのシステムコーレーション同機化が必要になります。\n\
「cubrid synccolldb -f %1$s」を実行してください。\n
57 新しいシステム設定の%1$d個のコーレーションとデータベースのコーレーションとの比較をかんりょうしました。:\n
58 データベース「%1$s」のすべてのコーレーションが新しいシステム設定にあることを確認しました。\n
59 データベースに%1$d個のコーレーションが設定されていないか、システムコーレーションに変わりました。\n
60 \
synccolldb: データベースのコーレーションをシステムコーレーションに同期します。\n\
使い方: %1$s synccolldb [オプション] データベース名\n\
\n\
オプション:\n\
  -c, --check-only    データベースのコーレーションとシステムコーレーションの同期作業をチェックします。\n\
		      もし、コーレーションの同機化が必要になると、エラーコード１がリターンされます。\n\
		      また、互換しないコーレーションの結果を表示します。\n\
  -f, --force-only    データベースのコーレーションをシステムの新しいコーレーションに強制更新します。\n\

$set 49 MSGCAT_UTIL_SET_TRANLIST
20 「%1$s」のパスワードを入力してください: 
22 トランザクションーインデックス  ユーザー名     ホスト名     プロセスID     プログラム名\n
23 -------------------------------------------------------------------------------\n
24 %1$4d%2$-10s %3$14s %4$14s %5$15d %6$17s\n
25 トランザクションがありません。\n
26 tranlistはDBAユーザー、またはDBAグループのメンバーが実行できます。\n
27 正しくないソートキーのカラム番号(%1$d)。\n
32 トランインデックス　　　　プロセスid       プログラム名　　　　　　　　　　　　　クエリタイム　　　　トランタイム　　　　　　　　　　　ロックゴールだウェート　　　　     SQL ID        SQLテキスト\n
33 %1$4d%2$-10s %3$14d    %4$-25s %5$10.2f %6$12.2f    %7$28s     %8$13s   %9$s\n
34 --------------------------------------------------------------------------------------------------------------------------------------------------\n
42 トランインデックス　　　　　ユーザー名　　　　　　ホスト名　　　　　　　プロセスid　　　　　プログラム名　　　　　　　　　　　　　クエリタイム　　　　トランタイム　　　　　　　　　　　ロックウェート　　　　　　　　　ホルダー　　　　　SQL_ID       SQLテキスト\n
43 %1$4d%2$-10s %3$14s %4$14s %5$14d    %6$-25s %7$10.2f %8$12.2f    %9$28s     %10$13s   %11$s\n
44 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------\n
45 トランザクションインデックス : %1$d
46 SQL_ID: %1$s\n
59 tranlistはスタンドアロンモードで実行できません。\n
60 \
tranlist: アクティブトランザクション情報をダンプします。\n\
使い方: %1$s tranlist [オプション] データベース名\n\
\n\
オプション:\n\
  -u, --user=ID                データベースに接続するユーザーID; デフォルト: DBAユーザー\n\
  -p, --password=PASS          ユーザーパスワード; デフォルト: なし\n\
  -s, --summary                トランザクション情報を要約して出力する。\n\
      --sort-key=COLUMN_NUMBER ソートされるカラム番号\n\
      --reverse                ソート結果を逆にする。\n

$set 50 MSGCAT_UTIL_SET_PREFETCHLOGDB
22 サーバは、HA用に設定されていませんでした。\n
23 プリフェッチ機能が無効になっています。\n
58 HA機能は、このプラットフォームでサポートされていません。\n
59 prefetchlogdbはスタンドアロンモードで実行できません。\n
60 \
prefetchlogdb: サーバーからのログページをプリフェッチします。\n\
使い方: %1$s prefetchlogdb [オプション] データベース名\n\
\n\
オプション:\n\
  -L, --log-path=PATH         コピーされたログボリュームのディレクトリパス\n
