
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
interactive SQL utility, version %s\n\
usage: %s [OPTION] database-name[@host]\n\
\n\
valid options:\n\
  -S, --SA-mode                ការប្រតិបត្តិតាមទម្រង់ឥតជំនួយ(ផ្ទាល់ខ្លួន)\n\
  -C, --CS-mode                ការប្រតិបត្តិតាមទម្រង់client-server\n\
  -u, --user=ARG               ឈ្មោះuserដែលឆ្លាស់គ្នា\n\
  -p, --password=ARG            កំណត់សម្ងាត់string, ផ្តល់""សម្រាប់ភាពទទេ(none)\n\
  -e, --error-continue         កុំបិទនៅត្រង់statement error\n\
  -i, --input-file=ARG         ឈ្មោះinput-file\n\
  -o, --output-file=ARG        ឈ្មោះoutput-file\n\
  -s, --single-line            បន្ទាត់ឯក(single​ line)ដែលតម្រង់រកការប្រតិបត្តិ\n\
  -c, --command=ARG            CSQL-commands\n\
  -l, --line-output            បង្ហាញតម្លៃនីមួយៗក្នុងបន្ទាត់មួយ\n\
  -r, --read-only              ទម្រង់read-only\n\
      --string-width           បង្ហាញនូវជួរនិមួយៗដែលមានប្រវែងទទឹងស្មើនឹងប្រភេទខ្សែអក្សរ\n\
      --no-auto-commit         មិនអាចស្វ័យធ្វើប្រតិបត្តិការទម្រង់(mode execution)\n\
      --no-pager               កុំប្រើទំព័រ\n\
      --no-single-line         បិទបន្ទាត់ឯក(single line)ដែលតម្រង់រកការប្រតិបត្តិ\n\
      --no-trigger-action      disable trigger action\n\
\n\
សម្រាប់ពត៌មានបន្ថែម សូមមើលhttp://www.cubrid.org\n
41 %s: តម្លៃពាក់ព័ន្ធនានានៃ %s_MODE មិនត្រឹមត្រូវទេ។ \nវាគួរតែទាំងជា %s ឬ %s។\n
42 %s: សេចក្តីប្រទាំងប្រទើសដ៏ច្រើនលើសលប់បានកើតមាន។ អតិបរិមាគឺ %d។\n
43 %1$s: តម្លៃអថេរពាក់ព័ន្ធនានានៃបញ្ជីរាយប្លង់សំខាន់ៗ ($CUBRID)គួរតែត្រូវបានកំណត់។\n
44 %s: ការព្យាយាមដើម្បីប្រតិបត្តិ"%s"បានជួបបរាជ័យ។\n
45 %1$s: មិនអាចប្រើជម្រើស -sa និង -cs ដំណាលគ្នាបានទេ។\n
46 %1$d command(s)បានដំណើរការដោយជោគជ័យ។
47 ក្នុងការបញ្ជាពីបន្ទាត់ %1$d,
48 ក្នុងបន្ទាត់ %d, ជួរឈរ %d,
49 ប្រភេទ `;help' សម្រាប់សារជំនួយ (help messages)។
50 ERROR: 
51 អ្នកបកស្រាយCUBRID SQL
52 មានទំនើបកម្មជាច្រើន។ តើអ្នកចង់ប្រតិបត្តិទេ? (`Y' or `N') 
53 វាយបញ្ជូល `Y' ដើម្បីប្រតិបត្តិ, `N' ដើម្បីមិនសម្រចប្រតិបត្តិការ។ 
54 ការប្តេជ្ញាចិត្ត.
55 រមៀលត្រលប់មកវិញ.
56 Command buffer ត្រូវបានរក្សាទុក។
57 fileនេះ បានធ្វើដំណើរការទៅក្នុង command buffer។
58 Command buffer បានបញ្ជូនទៅកាន់ម៉ាស៊ីនព្រីន។
59 បញ្ជីរាយប្លង់ចរន្តត្រូវប្តូរ
61 វាយបញ្ជូលកំណត់សម្គាល់សម្ងាត់: 
62 <Result of %s Command in Line %d>
63 ពុំមានលទ្ធផលសោះ។
64 កន្លែងត្រួតពិនិត្យត្រូវបានបញ្ជេញមក។
65 ដែលតាបេសបានដំណើរការឡើងវិញ។
66 លិបិក្រមTran          ឈ្មោះUser      ឈ្មោះHost      ដំណើរការ id      ឈ្មោះកម្មវិធី\n
67 %4d%-12s %14s %14s %15d %17s\n
68 កិច្ចសម្រួលដំណើរការពិសេសត្រូវបានបំផ្លាញ។
69 ការបំផ្លាញនៃកិច្ចសម្រួលដំណើរការពិសេសបានជួបបរាជ័យ។
70 %d rows %s.
71 %d row %s.
$ ទាំង3នេះ គួរតែមិនទំនេរ
75 Automated API tester
76 Invalid Automated API tester
79 csql>
80 csql
81 sysadm>

145 <Help: Schema of a Class>
146 none
147 <Help: All Triggers>
148 <Help: No Triggers Defined>
150 <Help: Trigger Definition>
151 <Help: Command Syntax>
152 <Help: Session Command Summary>
178 តម្រូវឲ្យមានឈ្មោះfileមួយ។
179 មិនអាចប្រតិបត្តិអ្នកកំណត់ទំព័រ(pager)។
180 ពុំមានmemoryដែលត្រូវចែកចាយគ្រប់គ្រាន់ទេ។
184 បន្ទាត់ដែលត្រូវបញ្ជូលវែងពេក។
185 បញ្ជូលបន្ទាត់ច្រើនពេក។
188 បញ្ជូលឈ្មោះfileច្រើនពេក។
190 ពុំមានsession commandបែបនេះ។ ប្រភេទ `;help' សម្រាប់សារជំនួយ (help messages)។
191 មានsession commandយ៉ាងតិចមួយ បានដំណើរការ។ សូមកំណត់ឲ្យកាន់តែច្បាស់។
193 មានcommandយ៉ាងតិចមួយបានដំណើរការ។ សូមកំណត់ឲ្យកាន់តែច្បាស់។
194 បន្សំពុំមានសុពលភាពនៃ command line arguments (options).
196 errorមិនបំណត់មួយបានកើតឡើង។ 
197 ';edit'មិនត្រូវបានអនុញ្ញាតទេ នៅពេលoutputបានប្តូរទិស។

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
   session commandsទាំងអស់គួរត្រូវកំណត់ខាងមុខដោយ`;' និង blanks/tabsប៉ុណ្ណោះ។\n\
         អាចដាក់ខាងមុខបុព្វបទ។ អក្សធំៗជាតំណាងនៃចំនួនអប្បបរមា\n\
        ពាក្យសរសេរកាត់ដែលអ្នកត្រូវតែបញ្ជូលដើម្បីប្រតិបត្តិcommandពិសេស។\n\
\n\
   ;REAd   [<file-name>]        - ដំណើរការfileមួយទៅក្នុងcommand buffer។\n\
   ;Write  [<file-name>]        - ដាក់ជំនួសcommand bufferក្នុងfileមួយ។\n\
   ;APpend [<file-name>]        - បន្ថែមcommand bufferទៅក្នុង fileមួយ។\n\
   ;PRINT                       - ផ្តិត(print) command buffer។\n\
   ;SHELL                       - ហៅ shell.\n\
   ;CD                          - ប្តូរបញ្ជីការងារបច្ចុប្បន្ន\n\
   ;EXit                        - បិទកម្មវិធី។\n\
\n\
   ;CLear                       - ជម្រះ command buffer។\n\
   ;EDIT                        - ហៅអ្នកកែតម្រូវប្រព័ន្ធដែលមានcommand buffer។\n\
   ;LISt                        - បង្ហាញមាតិការនៃcommand buffer។\n\
\n\
   ;RUn                         - ប្រតិបត្តិ sql ក្នុង command buffer។\n\
   ;Xrun                        - ប្រតិបត្តិ sql ក្នុង command buffer,\n\
                                  និងជម្រះ command buffer។\n\
   ;COMmit                      - ប្រតិបត្តិកិច្ចសម្រួលដំណើរការបច្ចុប្បន្ន។\n\
   ;ROllback                    - បន្ថយកិច្ចសម្រួលដំណើរការបច្ចុប្បន្ន។\n\
   ;AUtocommit [ON|OFF]         - មិនអាចស្វ័យប្រតិបត្តិទម្រង់(mode)។\n\
   ;REStart                     - ចាប់ផ្តើមដែលតាបេសឡើងវិញ។\n\
\n\
   ;SHELL_Cmd    [shell-cmd]    - កំណត់ default shell, អ្នកកែត្រូវ(editor), ម៉ាស៊ីនព្រីន(print) និងអ្នកកំណត់ទំព័រ(pager)\n\
   ;EDITOR_Cmd   [editor-cmd]     បញ្ជាទៅកាន់ឧបករណ៍ថ្មីមួយ(new one), ឬបង្ហាញបច្ចុប្បន្នភាព(the current)\n\
   ;PRINT_Cmd    [print-cmd]      ឧបករណ៍មួយ, រៀងៗគ្នា។\n\
   ;PAger_cmd    [pager-cmd]\n\
\n\
   ;DATE                        - បង្ហាញម៉ោងក្នុងស្រុក, កាលបរិច្ឆេទ។\n\
   ;DATAbase                    - បង្ហាញឈ្មោះដែលតាបេសដែលពេញសិទិ្ធ។\n\
   ;SChema class-name           - បង្ហាញពត៌មានschemaនៃថ្នាក់មួយ(class)។\n\
   ;TRIgger [`*'|trigger-name]  - បង្ហាញនិយមន័យtrigger។\n\
   ;Get system_parameter        - ទទួលយកតម្លៃនៃប៉ារ៉ាម៉ែត្រប្រព័ន្ធ។\n\
   ;SEt system_parameter=value  - កំណត់តម្លៃនៃប៉ារ៉ាម៉ែត្រប្រព័ន្ធ។\n\
   ;STring-width [width]        - ស៊េរីប្រវែងទទឹងនៃជួរនិមួយៗដែលបានបង្ហាញនូវប្រភេទខ្សែអក្សរ.\n\
   ;COLumn-width [name]=[width] - ស៊េរីប្រវែងទទឹងនៃជួរពិសេសដែលបានបង្ហាញហើយ.\n\
   ;PLan [simple/detail/off]    - បង្ហាញផែនការណ៍ប្រតិបត្តិquery។\n\
   ;Info <command>              - បង្ហាញពត៌មានខាងក្នុង។\n\
   ;TIme [ON/OFF]               - មិនអាចបង្ហាញ query\n\
                                  ម៉ោងប្រតិបត្តិ\n\
   ;LINe-output [ON/OFF]        - អាច/មិនអាចបង្ហាញនូវតម្លៃនិមួយៗក្នុងមួយជួរ\n\
   ;HISTORYList                 - បង្ហាញតារាងឈ្មោះqueriesដែលបានប្រតិបត្តិ។\n\
   ;HISTORYRead <history_num>   - ដំណើរការentryនៅលើចំនួន historyទៅក្នុងcommand buffer។\n\
   ;TRAce [ON/OFF] [text/json]  - enable/disable sql auto trace.\n\
   ;HElp                        - បង្ហាញសារជំនួយនេះ (help message)។\n\n

232 <Constraints> 
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - ពត៌មានschema\n\
\ttrigger [trigger-name] - ពត៌មានtrigger\n\
\tdeferred               - បានពន្យាពត៌មានtrigger\n\
\tworkspace              - ពត៌មានworkspace\n\
\tlock                   - lockពត៌មាន(client-server mode)\n\
\tstats class-name       - ចាត់ថ្នាក់ពត៌មានស្ថិតិ(statistics)\n\
\ttrantable              - ពត៌មានសម្រួលដំណើរការ\n\
\tlogstat                - logពត៌មាន\n\
\tcsstat                 - ពត៌មានតាមផ្នែកសំខាន់ៗ(critical section)\n\
\tplan                   - ពត៌មានplan cache\n\
\tqcache                 - ពត៌មានquery cache\n
235 <Partitions>
236 តម្រូវឲ្យមានឈ្មោះថ្នា់ក់មួយ(A class name)។
