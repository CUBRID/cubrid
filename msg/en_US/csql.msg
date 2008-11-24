
$
$ Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
$
$   This program is free software; you can redistribute it and/or modify 
$   it under the terms of the GNU General Public License as published by 
$   the Free Software Foundation; version 2 of the License. 
$
$  This program is distributed in the hope that it will be useful, 
$  but WITHOUT ANY WARRANTY; without even the implied warranty of 
$  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
$  GNU General Public License for more details. 
$
$  You should have received a copy of the GNU General Public License 
$  along with this program; if not, write to the Free Software 
$  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
$

$
$ $Id$
$
$ csql.msg
$

$set 1 MSGCAT_CSQL_SET_CSQL
40 \
interactive SQL utility, version %s\n\
usage: %s [OPTION] database-name\n\
\n\
valid options:\n\
  -S, --SA-mode                standalone mode execution\n\
  -C, --CS-mode                client-server mode execution\n\
  -u, --user=ARG               alternate user name\n\
  -p, --pasword=ARG            password string, give "" for none\n\
  -e, --error-continue         don't exit on statement error\n\
  -i, --input-file=ARG         input-file-name\n\
  -o, --output-file=ARG        output-file-name\n\
  -s, --single-line            single line oriented execution\n\
  -c, --command=ARG            SQLX-commands\n\
  -l, --line-output            display each value in a line\n\
      --no-auto-commit         disable auto commit mode execution\n\
      --no-pager               do not use pager\n\
\n\
For addtional information, see http://www.cubrid.com\n
41 %s: the environment value of %s_MODE is incorrect. \nIt should be either %s or %s.\n
42 %s: too many arguments given. maximum is %d.\n
43 %s: The root directory environment variable ($CUBRID) should be set.\n
44 %s: the attempt to execute "%s" has failed.\n
45 %s: cannot use -sa and -cs options simultaneously.\n
46 %d command(s) successfully processed.
47 In the command from line %d,
48 In line %d, column %d,
49 Type `;help' for help messages.
50 ERROR: 
51 CUBRID SQL Interpreter
52 There have been updates. Do you want to commit ? (`Y' or `N') 
53 Enter `Y' to commit, `N' to abort. 
54 Current transaction has been committed.
55 Current transaction has been rolled back.
56 Command buffer has been saved.
57 The file has been read into the command buffer.
58 Command buffer has been sent to the printer.
59 Current directory changed to 
61 Enter Password : 
63 <Result of %s Command in Line %d>
65 There are no results.
70 %d rows %s.
$ These 3 should be blank
$quote "
71 ""
72 ""
73 ""
$quote
75 Automated API tester
76 Invalid Automated API tester
79 csql>
80 csql

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
192 No such command. Type `;syntax general' to see available commands.
193 More than one command matched. Please specify more.
194 Invalid combination of command line arguments (options).
196 An undefined error has occurred.
197 ';edit' is not allowed when output has been redirected.

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
   All session commands should be prefixed by `;' and only blanks/tabs\n\
   can precede the prefix. Capitalized characters represent the minimum\n\
   abbreviation that you need to enter to execute the specified command.\n\
\n\
   ;REad   [<file-name>]       - read a file into command buffer.\n\
   ;Write  [<file-name>]       - (over)write command buffer into a file.\n\
   ;APpend [<file-name>]       - append command buffer into a file.\n\
   ;PRINT                      - print command buffer.\n\
   ;SHELL                      - invoke shell.\n\
   ;CD                         - change current working directory.\n\
   ;EXit                       - exit program.\n\
\n\
   ;CLear                      - clear command buffer.\n\
   ;EDIT                       - invoke system editor with command buffer.\n\
   ;List                       - display the content of command buffer.\n\
\n\
   ;RUn                        - execute sql in command buffer.\n\
   ;Xrun                       - execute sql in command buffer,\n\
                                 and clear the command buffer.\n\
   ;COmmit                     - commit the current transaction.\n\
   ;ROllback                   - roll back the current transaction.\n\
   ;AUtocommit [ON|OFF]        - enable/disable auto commit mode.\n\
\n\
   ;SHELL_Cmd  [shell-cmd]     - set default shell, editor, print and pager\n\
   ;EDITOR_Cmd [editor-cmd]      command to new one, or display the current\n\
   ;PRINT_Cmd  [print-cmd]       one, respectively.\n\
   ;PAger_cmd  [pager-cmd]\n\
\n\
   ;DATE                       - display the local time, date.\n\
   ;DATAbase                   - display the name of database being accessed.\n\
   ;SChema class-name          - display schema information of a class.\n\
   ;SYntax [sql-cmd-name]      - display syntax of a command.\n\
   ;TRigger [`*'|trigger-name] - display trigger definition.\n\
   ;Get system_parameter       - get the value of a system parameter.\n\
   ;SEt system_parameter=value - set the value of a system parameter.\n\
   ;Info <command>             - display internal information.\n\
   ;TIme [ON/OFF]              - enable/disable to display the query\n\
                                 execution time.\n\
   ;HISTORYList                - display list of the executed queries.\n\
   ;HISTORYRead <history_num>  - read entry on the history number into command buffer.\n\
   ;HElp                       - display this help message.\n\n

232 <Constraints> 
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - schema information\n\
\ttrigger [trigger-name] - trigger information\n\
\tdeferred               - deferred trigger information\n\
\tworkspace              - workspace information\n\
\tlock                   - lock information (client-server mode)\n\
\tstats class-name       - class statistics information\n
235 <Partitions>
236 A class name is required.
