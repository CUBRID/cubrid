
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
interactive SQL utility, version %1$s\n\
usage: %2$s [OPTION] database-name[@host]\n\
\n\
valid options:\n\
  -S, --SA-mode                standalone mode execution\n\
  -C, --CS-mode                client-server mode execution\n\
  -u, --user=ARG               alternate user name\n\
  -p, --password=ARG           password string, give "" for none\n\
  -e, --error-continue         don't exit on statement error\n\
  -i, --input-file=ARG         input-file-name\n\
  -o, --output-file=ARG        output-file-name\n\
  -s, --single-line            single line oriented execution\n\
  -c, --command=ARG            CSQL-commands\n\
  -l, --line-output            display each value in a line\n\
  -r, --read-only              read-only mode\n\
      --string-width           display each column which is a string type in this width\n\
      --no-auto-commit         disable auto commit mode execution\n\
      --no-pager               do not use pager\n\
      --no-single-line         turn off single line oriented execution\n\
      --no-trigger-action      disable trigger action\n\
\n\
For additional information, see http://www.cubrid.org\n
41 %1$s: the environment value of %2$s_MODE is incorrect. \nIt should be either %3$s or %4$s.\n
42 %1$s: too many arguments given. maximum is %2$d.\n
43 %1$s: The root directory environment variable ($CUBRID) should be set.\n
44 %1$s: the attempt to execute "%2$s" has failed.\n
45 %1$s: cannot use -sa and -cs options simultaneously.\n
46 %1$d command(s) successfully processed.
47 In the command from line %1$d,
48 In line %1$d, column %2$d,
49 Type `;help' for help messages.
50 ERROR: 
51 CUBRID SQL Interpreter
52 There have been updates. Do you want to commit ? (`Y' or `N') 
53 Enter `Y' to commit, `N' to abort. 
54 Committed.
55 Rolled back.
56 Command buffer has been saved.
57 The file has been read into the command buffer.
58 Command buffer has been sent to the printer.
59 Current directory changed to 
61 Enter Password : 
62 <Result of %1$s Command in Line %2$d>
63 There are no results.
64 Checkpoint has been issued.
65 The database has been restarted.
66 Tran index            User name      Host name      Process id      Program name\n
67 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
68 The specified transaction has been killed.
69 Killing of the specified transaction has failed.
70 %1$d rows %2$s.
71 %1$d row %2$s.
$ These 3 should be blank
75 Automated API tester
76 Invalid Automated API tester
79 csql>
80 csql
81 sysadm>
82 %1$d transactions %2$s.
83 %1$d transaction %2$s.

145 <Help: Schema of a Class>
146 none
147 <Help: All Triggers>
148 <Help: No Triggers Defined>
150 <Help: Trigger Definition>
151 <Help: Command Syntax>
152 <Help: Session Command Summary>
178 A file name is required.
179 Cannot execute the pager.
180 There is not enough memory allocated.
184 The line being entered is too long.
185 Too many lines have been entered.
188 Too many file names have been entered.
190 No such session command. Type `;help' for help messages.
191 More than one session command matched. Please specify more.
193 More than one command matched. Please specify more.
194 Invalid combination of command line arguments (options).
196 An undefined error has occurred.
197 ';edit' is not allowed when output has been redirected.

203 <%1$s> 
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
   All session commands should be prefixed by `;' and only blanks/tabs\n\
   can precede the prefix. Capitalized characters represent the minimum\n\
   abbreviation that you need to enter to execute the specified command.\n\
\n\
   ;REAd   [<file-name>]        - read a file into command buffer.\n\
   ;Write  [<file-name>]        - (over)write command buffer into a file.\n\
   ;APpend [<file-name>]        - append command buffer into a file.\n\
   ;PRINT                       - print command buffer.\n\
   ;SHELL                       - invoke shell.\n\
   ;CD                          - change current working directory.\n\
   ;EXit                        - exit program.\n\
\n\
   ;CLear                       - clear command buffer.\n\
   ;EDIT                        - invoke system editor with command buffer.\n\
   ;LISt                        - display the content of command buffer.\n\
\n\
   ;RUn                         - execute sql in command buffer.\n\
   ;Xrun                        - execute sql in command buffer,\n\
                                  and clear the command buffer.\n\
   ;COMmit                      - commit the current transaction.\n\
   ;ROllback                    - roll back the current transaction.\n\
   ;AUtocommit [ON|OFF]         - enable/disable auto commit mode.\n\
   ;REStart                     - restart database.\n\
\n\
   ;SHELL_Cmd    [shell-cmd]    - set default shell, editor, print and pager\n\
   ;EDITOR_Cmd   [editor-cmd]     command to new one, or display the current\n\
   ;PRINT_Cmd    [print-cmd]      one, respectively.\n\
   ;PAger_cmd    [pager-cmd]\n\
\n\
   ;DATE                        - display the local time, date.\n\
   ;DATAbase                    - display the name of database being accessed.\n\
   ;SChema class-name           - display schema information of a class.\n\
   ;TRIgger [`*'|trigger-name]  - display trigger definition.\n\
   ;Get system_parameter        - get the value of a system parameter.\n\
   ;SEt system_parameter=value  - set the value of a system parameter.\n\
   ;STring-width [width]        - set width that each column which is a string type is displayed.\n\
   ;COLumn-width [name]=[width] - set width that a specific column is displayed.\n\
   ;PLan [simple/detail/off]    - show query execution plan.\n\
   ;Info <command>              - display internal information.\n\
   ;TIme [ON/OFF]               - enable/disable to display the query\n\
                                  execution time.\n\
   ;LINe-output [ON/OFF]        - enable/disable to display each value in a line\n\
   ;HISTORYList                 - display list of the executed queries.\n\
   ;HISTORYRead <history_num>   - read entry on the history number into command buffer.\n\
   ;TRAce [ON/OFF] [text/json]  - enable/disable sql auto trace.\n\
   ;HElp                        - display this help message.\n\n

232 <Constraints> 
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - schema information\n\
\ttrigger [trigger-name] - trigger information\n\
\tdeferred               - deferred trigger information\n\
\tworkspace              - workspace information\n\
\tlock                   - lock information (client-server mode)\n\
\tstats class-name       - class statistics information\n\
\ttrantable              - transaction information\n\
\tlogstat                - log information\n\
\tcsstat                 - critical section information\n\
\tplan                   - plan cache information\n\
\tqcache                 - query cache information\n
235 <Partitions>
236 A class name is required.
