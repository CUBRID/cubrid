
$
$  Copyright 2008 Search Solution Corporation
$  Copyright 2016 CUBRID Corporation
$ 
$   Licensed under the Apache License, Version 2.0 (the "License");
$   you may not use this file except in compliance with the License.
$   You may obtain a copy of the License at
$ 
$       http://www.apache.org/licenses/LICENSE-2.0
$ 
$   Unless required by applicable law or agreed to in writing, software
$   distributed under the License is distributed on an "AS IS" BASIS,
$   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
$   See the License for the specific language governing permissions and
$   limitations under the License.
$ 
$ 
$

$
$ $Id$
$
$ csql.msg
$

$set 1 MSGCAT_CSQL_SET_CSQL
40 \
��ȭ�� SQL ����, ���� %1$s\n\
����: %2$s [�ɼ�] �����ͺ��̽�-�̸�[@ȣ��Ʈ]\n\
\n\
��ȿ�� �ɼǵ�:\n\
  -S, --SA-mode                ���� ��� ����\n\
  -C, --CS-mode                Ŭ���̾�Ʈ ���� ��� ����\n\
  -u, --user=ARG               ����� �̸�\n\
  -p, --password=ARG            ��й�ȣ ���ڿ�, ���� ��� ""\n\
  -e, --error-continue         ���� �߻� �� ��� ����\n\
  -i, --input-file=ARG         �Է� ���� �̸�\n\
  -o, --output-file=ARG        ��� ���� �̸�\n\
  -s, --single-line            ���� ���� ����\n\
  -c, --command=ARG            CSQL ���ɾ��\n\
  -l, --line-output            �� ���� ���� �� ǥ��\n\
  -r, --read-only              �б� ���� ���\n\
  -t, --plain-output           ��ũ��Ʈ ��뿡 ������ �������� ��� �� ��� (-c�� -i �ɼ� ��� �� ����)\n\
  -q, --query-output           ���ǹ� ���ѱ⿡ ������ �������� ��� �� ��� (-c�� -i �ɼ� ��� �� ����)\n\
  -d, --loaddb-output          loaddb�� ����⿡ ������ �������� ��� �� ��� (-c�� -i �ɼ� ��� �� ����)\n\
  -N, --skip-column-names      ��� �� �÷� �̸� ���� (-c�� -i �ɼ� ��� �� ����)\n\
      --string-width           �� ���ڿ� �÷� ���� ����� ����\n\
      --no-auto-commit         �ڵ� Ŀ�� �� ��\n\
      --no-pager               ������ ������ ǥ�� �� ��\n\
      --no-single-line         ���� ���� ���� �� ��\n\
      --no-trigger-action      Ʈ���� ���� ����\n\
      --delimiter=ARG          �÷��� ������ (-q������ ��� ����)\n\
      --enclosure=ARG          ��� ���ڿ� ���۷��� (-q������ ��� ����)\n\
\n\
�߰����� ������ http://www.cubrid.org ���� ���� �� �ֽ��ϴ�.\n
41 %1$s: ȯ�� ���� %2$s_MODE �� ���� Ʋ��. \n%3$s �Ǵ� %4$s �̾�� �մϴ�.\n
42 %1$s: �ʹ� ���� ���ڰ� �־���. �ִ� ������ %2$d �Դϴ�.\n
43 %1$s: ȯ�� ���� ($CUBRID) �� �����Ǿ�� �մϴ�.\n
44 %1$s: "%2$s"  ���� ����.\n
45 %1$s: -S�� -C �ɼ��� ���ÿ� ������ �� �����ϴ�.\n
46 %1$d ���� ���ɾ ����Ǿ����ϴ�.
47 ���� %1$d,
48 ���� %1$d, �� %2$d,
49 ���� �޽����� ������ ';help' ���ɾ �Է��ϼ���.
50 ����: 
51 CUBRID SQL �����
52 �����ͺ��̽��� ���� �Ǿ����ϴ�. Ŀ�� �ϰڽ��ϱ� ? (`Y' or `N') 
53 Ŀ���� 'Y', öȸ�� 'N' �Է�. 
54 Ŀ�� �Ǿ����ϴ�.
55 öȸ �Ǿ����ϴ�.
56 ���ɾ� ���۰� ���� �Ǿ����ϴ�.
57 ���ɾ� ���۷� ������ �о� �鿴���ϴ�.
58 ���ɾ� ������ ������ �����ͷ� ����߽��ϴ�.
59 ���� ���͸� ���� : 
61 ��й�ȣ �Է� : 
62 <%1$s�� ���, ���ɾ� ���� %2$d>
63 ��� ����.
64 üũ����Ʈ�� ����Ǿ����ϴ�.
65 �����ͺ��̽��� �ٽ� ���۵Ǿ����ϴ�.
66 Ʈ������ε���        ������̸�     ȣ��Ʈ�̸�     ���μ��� ID     ���α׷��̸�\n
67 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
68 ������ Ʈ������� ����Ǿ����ϴ�.
69 ������ Ʈ����� ���ᰡ �����߽��ϴ�.
70 %1$d ���� ���� %2$s.
71 %1$d ���� ���� %2$s.
$ These 3 should be blank
75 �ڵ�ȭ�� API �����
76 �������� ���� �ڵ�ȭ API �����
79 csql>
80 csql
81 sysadm>
82 %1$d ���� Ʈ������� %2$s.
83 %1$d ���� Ʈ������� %2$s.

145 <����: Ŭ���� ��Ű��>
146 ����
147 <����: ��� Ʈ����>
148 <����: ���ǵ� Ʈ���� ����>
150 <����: Ʈ���� ����>
151 <����: ���ɾ� ����>
152 <����: ���� ���ɾ� ���>
178 ���� �̸��� �ʿ��մϴ�.
179 ������ ���б⸦ ������ �� �����ϴ�.
180 �޸� ����.
184 �Էµ� ������ �ʹ� ��ϴ�.
185 �ʹ� ���� ������ �ԷµǾ����ϴ�.
188 �ʹ� ���� ���ϸ��� �ԷµǾ����ϴ�.
190 ���� ���� ���ɾ��Դϴ�. ���� �޽����� ������ ';help'�� �Է��ϼ���.
191 ���� ���ɾ ��ȣ�մϴ�. �� �ڼ��� �Է��ϼ���.
193 ���ɾ ��ȣ�մϴ�. �� �ڼ��� �Է��ϼ���.
194 ���ɾ� �ɼ� ������ �߸��Ǿ����ϴ�.
196 ������ ���� �߻�.
197 ����� �������� ��쿡�� ';edit' ���ɾ ����� �� �����ϴ�.
198 ���� ����.

203 <%1$s> 
204 <���� Ŭ����> 
205 <���� Ŭ����> 
206 <�Ӽ�> 
207 <Ŭ���� �Ӽ�> 
208 <�޼ҵ�> 
209 <Ŭ���� �޼ҵ�> 
210 <�ذ�>
211 <�޼ҵ� ����> 
212 <���� ����> 
213 <Ʈ����> 
214 <�̸�> 
215 <�̺�Ʈ> 
216 <���� �ð�> 
217 <����> 
218 <�׼� �ð�> 
219 <�׼�> 
220 <����> 
221 <�켱����> 
222 <�̸�> 
223 <����> 
224 <����> 
225 <����> 

$ csql help messages
231 \
\n\
   ��� ���� ���ɾ�� ';'�� �����ϸ� �տ��� ����/�� ���� �̿ܿ��� �� �� �����ϴ�.\n\
   �빮�ڷ� ǥ�õ� �κ��� �ش� ���ɾ �����ϱ� ���� �����Ǿ� �� �ּ����� ����Դϴ�.\n\
\n\
   ;REAd   [<file-name>]        - ���ɾ� ���۷� ���� ������ �о� ����.\n\
   ;Write  [<file-name>]        - ���ɾ� ���� ������ ���Ͽ� ����.\n\
   ;APpend [<file-name>]        - ���ɾ� ���� ������ ���Ͽ� ������.\n\
   ;PRINT                       - ���ɾ� ���� ������ ����Ʈ.\n\
   ;SHELL                       - �� ����.\n\
   ;CD                          - ���� �۾� ���͸� ����.\n\
   ;EXit                        - ����.\n\
\n\
   ;CLear                       - ���ɾ� ���� ������ ����.\n\
   ;EDIT   [format/fmt]         - ���ɾ� ���� [���� ��] ����.\n\
   ;LISt                        - ���ɾ� ���� ���� ���.\n\
\n\
   ;RUn                         - ���ɾ� ���� ����.\n\
   ;Xrun                        - ���ɾ� ���� ���� �� ���� ������ ����.\n\
   ;COMmit                      - ���� ���� Ʈ����� Ŀ��.\n\
   ;ROllback                    - ���� ���� Ʈ����� öȸ.\n\
   ;AUtocommit [ON|OFF]         - �ڵ� Ŀ�� ��� ����|����.\n\
   ;REStart                     - �����ͺ��̽� �����.\n\
\n\
   ;SHELL_Cmd     [shell-cmd]   - ������ ���� �����ϰų� ������.\n\
   ;EDITOR_Cmd    [editor-cmd]  - ������ �����⸦ �����ϰų� ������.\n\
   ;PRINT_Cmd     [print-cmd]   - ������ ����Ʈ ���ɾ �����ϰų� ������.\n\
   ;PAger_cmd     [pager-cmd]   - ������ ������ ���б⸦ �����ϰų� ������.\n\
   ;FOrmatter_cmd [formatter-cmd] - ������ �����͸� �����ϰų� ������.\n\
\n\
   ;DATE                        - ���� ��¥, �ð� ���.\n\
   ;DATAbase                    - ���� ���� �����ͺ��̽� �̸� ���.\n\
   ;SChema class-name           - Ŭ������ ��Ű�� ���� ���.\n\
   ;TRIgger [`*'|trigger-name]  - Ʈ���� ���� ���.\n\
   ;Get system_parameter        - �ý��� �Ķ���� ����.\n\
   ;SEt system_parameter=value  - �ý��� �Ķ���� ����.\n\
   ;STring-width [width]        - �� ���ڿ� �÷� ���� ����� ���̸� ����.\n\
   ;COLumn-width [name]=[width] - ������ �÷� ���� ����� ���̸� ����.\n\
   ;PLan [simple|detail|off]    - ���� �÷� ���� ����.\n\
   ;Info <command>              - ���� ���� ���.\n\
   ;TIme [ON/OFF]               - ���� ���� �ð� ���� ����|����\n\
   ;LINe-output [ON/OFF]        - �� ���� ���� �� ǥ�� ����|����\n\
   ;HISTORYList                 - ����� ���� ����Ʈ ����.\n\
   ;HISTORYRead <history_num>   - �����丮 ��ȣ�� �ش�Ǵ� ������ ���ɾ� ���ۿ� �ø�.\n\
   ;TRAce [ON|OFF] [text/json]  - ���� �ڵ� Ʈ���̽� ����|����.\n\
   ;SIngleline [ON|OFF]         - �̱� ���� ��� Ȱ��ȭ|��Ȱ��ȭ.\n\
   ;HElp                        - ���� �޽��� ���.\n\n

232 <�������> 
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - ��Ű�� ����\n\
\ttrigger [trigger-name] - Ʈ���� ����\n\
\tdeferred               - ���� Ʈ���� ����\n\
\tworkspace              - workspace ����\n\
\tlock                   - lock ���� (client-server mode)\n\
\tstats class-name       - Ŭ���� ��� ����\n\
\ttrantable              - Ʈ����� ����\n\
\tlogstat                - �α� ����\n\
\tcsstat                 - critical section ����\n\
\tplan                   - �÷� ĳ�� ����\n\
\tqcache                 - ���� ĳ�� ����\n
235 <����>
236 Ŭ���� �̸��� �ʿ��մϴ�.
237 <Ŀ��Ʈ>
