
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
Utilidad SQL interactiva, version %1$s\n\
uso: %2$s [OPCION] nombre-de-base de datos [@host] \n\
\n\
opciones validas:\n\
  -S, --SA-mode                modo de ejecucion independiente\n\
  -C, --CS-mode                modo de ejecucion cliente-servidor\n\
  -u, --user=ARG               nombre de utilizador suplente\n\
  -p, --password=ARG           string de contrasena, de "" para ninguna\n\
  -e, --error-continue         no salga a error de declaracion\n\
  -i, --input-file=ARG         nombre-de-archivo-de-entrada\n\
  -o, --output-file=ARG        nombre-de-archivo-de-salida\n\
  -s, --single-line            ejecucion linea suelta\n\
  -c, --command=ARG            CSQL-comandos\n\
  -l, --line-output            muestra cada valor en una linea\n\
  -r, --read-only              modo read-only\n\
  -t, --plain-output           mostrar resultados en un formato script-amigable (solo funciona con -c y -i)\n\
  -q, --query-output           mostrar resultados en un formato query-amigable (solo funciona con -c y -i)\n\
  -d, --loaddb-output          mostrar resultados en un formato loaddb-amigable (solo funciona con -c y -i)\n\
  -N, --skip-column-names      no mostrar nombres de columnas en resultados (solo funciona con -c y -i)\n\
      --string-width           muestra cada columna de tipo string en esta anchura\n\
      --no-auto-commit         inhabilitar modo de ejecucion cometer automaticamente\n\
      --no-pager               no utilize pager\n\
      --no-single-line         desactivar modo de ejecucion linea suelta\n\
      --no-trigger-action      inhabilitar accion del trigger\n\
      --delimiter=ARG          delimitador entre columna (solo funciona con -q)\n\
      --enclosure=ARG          recinto para una cadena de resultado (solo funciona con -q)\n\
\n\
Para mas informacion, vea http://www.cubrid.org\n
41 %1$s: el valor de entorno de %2$s_MODE es incorrecto. \nDeberia ser sea %3$s o %4$s.\n
42 %1$s: demasiados argumentos dados. maximo es %2$d.\n
43 %1$s: La variable ($CUBRID) de entorno del directorio de raiz deberia ser ajustada.\n
44 %1$s: el intente de ejecutar "%2$s" ha fracasado.\n
45 %1$s: no se puede utilizar las opciones -sa y -cs simultaneamente.\n
46 %1$d comando (s) procesados con éxito.
47 En el comando de la linea %1$d,
48 En linea %1$d, columna %2$d,
49 Escriba `;help' para mensajes de ayuda.
50 ERROR: 
51 Interpretador CUBRID SQL
52 Hay actualizaciones. Quiere cometer? (`Y' or `N') 
53 Introduzca 'Y' para cometer, 'N' para abortar.
54 Cometido.
55 Retrotraido.
56 Bufer de comando ha sido guardado.
57 El archivo ha sido leido en el bufer de comando.
58 Bufer de comando ha sido mandado a la impresora.
59 Directorio actual cambiado a
61 Introduzca contrasena:
62 <Resultado de %1$s Comanda en Linea %2$d>
63 No hay resultados.
64 Punto de control se ha emitido.
65 La base de datos ha sido reinicializada.
66 Indice de Tran        Nombre de utilizador   Nombre de host   Identificador de Proceso   Nombre de programa\n
67 %1$4d%2$-12s %3$14s %4$14s %5$15d %6$17s\n
68 La transaccion especificada ha sido matada.
69 Fracaso al matar la transaccion especificada.
70 %1$d filas %2$s.
71 %1$d fila %2$s.
$ Estos 3 deberian ser vacias.
75 Tester API automatizado.
76 Tester API automatizado no valido.
79 csql>
80 csql
81 sysadm>
82 %1$d transactions %2$s.
83 %1$d transaction %2$s.
84 Se han dado muy pocos argumentos.

145 <Ayuda: Esquema de una Clase>
146 ninguna
147 <Ayuda: Todos Disparadores>
148 <Ayuda: Sin Disparadores Definidos>
150 <Ayuda: Definicion de Disparador>
151 <Ayuda: Sintaxis de Comando>
152 <Ayuda: Resumen de Comando de Sesion>
178 Un nombre de archivo es requierido.
179 No se puede ejecutar el pager.
180 No hay suficiente memoria asignada.
184 La linea que se esta introduciendo es demasiado larga.
185 Demasiadas lineas han sido introducidas.
188 Demasiados nombres de archivo han sido introducidos.
190 No hay tal comandos de sesion. Escriba `;help' para mensajes de ayuda.
191 Mas de un comando de session ha emparejado. Por favor especifique mas.
193 Mas de un comando ha emparejado. Por favor especifique mas.
194 Combinacion no valida de argumentos de linea de comando (opciones).
196 Un error no definido ha ocurrido.
197 ';edit' no es permitido cuando salida ha sido redireccionada.
198 Error en el formato.

203 <%1$s> 
204 <Super Clases> 
205 <Sub Clases> 
206 <Atributos> 
207 <Atributos de Clase> 
208 <Metodos> 
209 <Metodos de Clase> 
210 <Resoluciones> 
211 <Archivos de Metodo> 
212 <Especificaciones de Consulta> 
213 <Disparadores> 
214 <Nombre> 
215 <Evento> 
216 <Tiempo de Condicion> 
217 <Condicion> 
218 <Tiempo de Accion> 
219 <Accion> 
220 <Estado> 
221 <Prioridad> 
222 <Nombre> 
223 <Descripcion> 
224 <Sintaxis> 
225 <Ejemplo> 

$ csql help messages
231 \
\n\
   Todos los comandos de sesion tendrian que utilizar `;' como prefijo y solo blanks/tabs\n\
   pueden preceder el prefijo. Caracteres mayúsculas representan la abreviacion minima\n\
   que necesita introducir para ejecutar el comando especificado.\n\
\n\
   ;REAd   [<file-name>]        - leer un archivo en el bufer de comando.\n\
   ;Write  [<file-name>]        - (sobre)escribe bufer de comando en un archivo.\n\
   ;APpend [<file-name>]        - añadir bufer de comando en un archivo.\n\
   ;PRINT                       - imprimir bufer de comando.\n\
   ;SHELL                       - invocar shell.\n\
   ;CD                          - cambiar directorio de trabajo actual.\n\
   ;EXit (or Ctrl+d)            - salir del programa.\n\
\n\
   ;CLear                       - limpia bufer de comando.\n\
   ;EDIT   [format/fmt]         - invoque el editor del sistema [después del formateador] con el búfer de comandos.\n\
   ;LISt                        - mostrar el contenido del bufer de comando.\n\
\n\
   ;RUn                         - ejecutar sql en bufer de comando.\n\
   ;Xrun                        - ejecutar sql en bufer de comando,\n\
                                  y limpia el bufer de comando.\n\
   ;COMmit                      - cometer transaccion actual.\n\
   ;ROllback                    - hacer retroceder la transaccion actual.\n\
   ;AUtocommit [ON|OFF]         - habilitar / deshabilitar modo cometer auto.\n\
   ;REStart                     - volver a conectarse a la base de datos actual en una sesión de CSQL.\n\
   ;CHeckpoint                  - ejecutar el punto de control(CSQL solo con --sysadm).\n\
   ;Killtran                    - verifique la información del estado de la transacción o finalice una transacción específica(CSQL solo con --sysadm).\n\
\n\
   ;SHELL_Cmd     [shell-cmd]   - ajusta estandar shell, editor, impresora, pager y formateador\n\
   ;EDITOR_Cmd    [editor-cmd]    comandar a una nueva, o mostrar la actual\n\
   ;PRINT_Cmd     [print-cmd]     respectivamente.\n\
   ;PAger_cmd     [pager-cmd]\n\
   ;FOrmatter_cmd [formatter-cmd]\n\
\n\
   ;DATE                        - mostrar el tiempo local, fecha.\n\
   ;DATAbase                    - mostrar el nombre de la base de datos que esta accesada.\n\
   ;SChema class-name           - mostrar informacion de esquema de una clase.\n\
   ;TRIgger [`*'|trigger-name]  - mostrar informacion de disparador.\n\
   ;Get system_parameter        - obtener el valor de un parametro de sistema.\n\
   ;SEt system_parameter=value  - ajustar el valor de un parametro de sistema.\n\
   ;STring-width [width]        - ajusta la anchura en que cada columna de tipo string sera mostrada.\n\
   ;COLumn-width [name]=[width] - establecer amplitud para que una columna especifica sea mostrada.\n\
   ;PLan [simple/detail/off]    - mostrar el plan de ejecucion de la consulta.\n\
   ;Info <command>              - mostrar informacion interna.\n\
   ;TIme [ON/OFF]               - habilitar/inhabilitar para mostrar la consulta \n\
                                  tiempo de ejecucion.\n\
   ;LINe-output [ON/OFF]        - habilitar/inhabilitar muestra cada valor en una linea\n\
   ;HISTORYList                 - mostrar lista de las consultas ejecutadas.\n\
   ;HISTORYRead <history_num>   - leer entrada sobre el numero de historia en bufer de comando.\n\
   ;TRAce [ON/OFF] [text/json]  - enable/disable sql auto trace.\n\
   ;SIngleline [ON|OFF]         - habilitar/deshabilitar el modo de una sola línea.\n\
   ;CONnect username [dbname | dbname@hostname]\n\
                                - conectarse a la base de datos actual o de otro tipo como nombre de usuario.\n\
   ;.Hist [ON/OFF]              - iniciar/detener la recopilación de información estadística en CSQL (solo DBA disponible).\n\
   ;.Clear_hist                 - borre la información de estadísticas de CSQL en el búfer.\n\
   ;.Dump_hist                  - muestra la información de estadísticas de CSQL en CSQL.\n\
   ;.X_hist                     - muestra la información de estadísticas de CSQL en CSQL con datos de estadísticas inicializados.\n\
   ;HElp                        - mostrar este mensaje de ayuda.\n\n

232 <Restricciones>
233 Info {schema|trigger|deferred|workspace|lock|stats}\n\
\tschema [class-name]    - informacion de esquema\n\
\ttrigger [trigger-name] - informacion de disparador\n\
\tdeferred               - information de disparador diferido\n\
\tworkspace              - informacion de espacio de trabajo\n\
\tlock                   - informacion de bloqueo(modo cliente-servidor)\n\
\tstats class-name       - informacion de estadisticas de clase\n\
\ttrantable              - informacion de transaccion\n\
\tlogstat                - information de registro\n\
\tcsstat                 - information de seccion critica\n\
\tplan                   - informacion de cache de plan\n\
\tqcache                 - informacion de cache de consulta\n
235 <Particiones>
236 Un nombre de clase es requierido.
237 <Comment>
