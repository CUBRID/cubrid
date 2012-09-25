
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
1 ពុំមានតម្លៃមិនចុះសម្រុងគ្នានៃលេខគត់ទេ "%s"\n
2 ពុំមានតម្លៃមិនចុះសម្រុងគ្នានៃលេខទស្សភាគទេ "%s"\n
3 អស់ភាពមិនចុះសម្រុង %s\n
4 [options] 
5 ជម្រើស: \n
6 អស់តម្លៃមិនចុះសម្រុងសម្រាប់ក្តាចុច(key) %s\n
7 ជម្រើសដែលមិនស្គាល់ "%s"\n
8 ភាពមិនចុះសម្រុងដែលមិនរំពឹងទុកនៃទីតាំង"%s"\n

$set 2
1 កំណត់ឡើងវិញនៃ cursor "%s"
2  *\tfor statement %s\n
3  *\n * Cursor table:\n

$set 3
1 (INTERNAL ERROR ក្នុង set_class_bit): ថ្នាក់(class)នៃការរក្សាទុកដ៏អន់ (%d)
2 (INTERNAL ERROR ក្នុង add_spec_to_decl()): ចង្វាក់ខុសទម្រង់(no specifier)
3 អ្នកបញ្ជាក់(specifier)នៃថ្នាក់រក្សាទុក មិនត្រូវបានអនុញ្ញាតនៅក្នុងមាតិការនេះទេ។
4 បន្សំថ្នាក់រក្សាទុកខុស
5 បន្សំប្រភេទខុស
6 បន្សំអ្នកកំណត់បញ្ជាក់(modifier)ខុស 
7 (INTERNAL ERROR ក្នុង pp_add_type_spec): ករណីមិនរំពឹងទុក (%d)
8 (INTERNAL ERROR ក្នុង pp_add_type_adj): ករណីមិនរំពឹងទុក (%d)
9 សេចក្តីថ្លែង %s ត្រូវបានបង្កើតឡើងមិនត្រឹមត្រូវ

$set 4
1 %s
2 %s: មិនមែនបញ្ជីរាយឈ្មោះមួយ\n
3 %s: តើrootនៃការតម្លើងអន់ឬ?\n

$set 5
1 ?
2 សូមពិនិត្យមើលកំណែត្រូវនៃtoken
3 ផ្នែកUSINGមិនត្រូវបានអនុញ្ញាតសម្រាប់cursorមិនផ្លាស់ទី "%s"
4 cursor "%s" មិនត្រូវបានកំណត់
5 ទ្រនិច(pointer) តម្រង់ descriptorមួយ
6 ទ្រនិច(pointer) តម្រង់ DB_OBJECTមួយ
7 អក្សរstring
8 ចំនួនអថេរindicatorសម្រាប់ "%s" មិនត្រូវបានអនុញ្ញាតនៅក្នុងមាតិការនេះទេ។
9 "%s" មិនត្រូវបានបង្ហាញនៅក្នុងផ្នែក BEGIN DECLARE។
52 %s (text was "%s")
53 errorនៃសទ្ធានុក្រម
54 errorវាក្សសម្ព័ន្ធ ត្រង់ "%s": បាត់ %s ក្នុង %s
55 errorវាក្សសម្ព័ន្ធ ត្រង់ "%s": បាត់ %s

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
1 សង្កត់លើសេចក្តីបង្គាប់ #line ក្នុង output file, អស់ស្វ័យប្រតិបត្តិការ
2 ឈ្មោះoutput file, ស្វ័យប្រតិបត្តិការ <input-file>.c ឬ stdout
3 រួមមានfile ដែលមាន uci_ prototypes, ស្វ័យប្រតិបត្តិការ "esqlx.h"
4 ពត៌មាននៃការដកកំហុសចេញពីdump scope, អស់ស្វ័យប្រតិបត្តិការ
5 ពត៌មាននៃការដកកំហុសចេញពីmallocចុងក្រោយនៃdump, អស់ស្វ័យប្រតិបត្តិការ
6 ព្រីន/ផ្តិតចេញនូវពត៌មាននៃចំនួនកំណែថ្មី, អស់ស្វ័យប្រតិបត្តិការ
7 បង្កើនលទ្ធភាពនៃគំនូសផ្លូវ UCI, អស់ស្វ័យប្រតិបត្តិការ
8 ការកំណត់ចំនួនdisable lengthនៃអថេរ VARCHAR host, អស់ស្វ័យប្រតិបត្តិការ
9 ប្រើរបៀបVARCHARផ្សេង, អស់ស្វ័យប្រតិបត្តិការ
10 មិនស្គាល់ -462(null indicator needed) error, អស់ស្វ័យប្រតិបត្តិការ
11 ប្រើNULL indicatorខាងក្នុង ដើម្បីទប់ស្កាត់ -462(null indicator needed) error, អស់ស្វ័យប្រតិបត្តិការ
12 ឈ្មោះinput file, ស្វ័យប្រតិបត្តិការ stdin

$set 8
1 ជម្រើសREPEATEDដែលមិនស្គាល់
2 %s version %s\n

$set 9
1 (yy_pop_mode) internal error: empty mode stack
2 "%s" មិនត្រូវបានអនុញ្ញាតនៅក្នុងមាតិការនេះទេ។

$set 10
1 errorខាងក្នុង ក្នុង %s: %s\n
2 តារាង overflow

$set 11
1 អថេរindicator "%s" ត្រូវតែជាintខ្លីមួយ
2 មិនអាចប្រើអថេរindicatorមួយដែលមានរចនាសម្ព័ន្ធ
3 "%s" មិនបានបំពេញតាមកំណត់ struct
4 "%s"ត្រូវតែជា%sសុពល
5 %s មិនមែនជាប្រភេទដែលអាចទទួលយកបានសម្រាប់ "%s"
6 ប្រភេទអថេរhostដែលមិនព្រៀងទុក: %s
7 (pp_addr_expr): បំណងដើម្បីស្វែងរកអាសយដ្ឋាននៃអថេរ NULL
8 "%s" មិនមែនជាទ្រនិច
9 "%s" មិនចង្អុលតម្រង់ struct
10 "%s" មិនមែនជា struct
11 %s គ្មានfield "%s"
12 "%s" មិនអាចត្រូវប្រតិបត្តិមិនផ្ទាល់ដោយ"%s"

$set 12
1 អស់memory

$set 13
1 លំដាប់ជួរនែតួនាទីមិនត្រឹមត្រូវ ដោយសន្មត់ជាមុនទ្រនិចតួនាទី
2 (pp_the_same_type) ថ្នាក់ភ្ជាប់ដែលមិនស្គាល់
3 (pp_get_sizeof): នាមអន់(bad noun) (%d)

$set 14
1 (INTERNAL ERROR in %s): ករណីមិនអាច
