
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
$ cubrid.msg
$

$set 1 MSGCAT_SET_GENERAL
1 \n%1$s\n\n
2 Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
3 Información de Copyright.
5 Argumento de valor no entero `%1$s'
6 Argumento de valor no flotante `%1$s'
7 Argumento faltante `%1$s'
8 Argumento duplicado `%1$s'
9 Faltando valor para el argumento `%1$s'
10 Argumento desconocido `%1$s'
11 Argumento posicional inesperado `%1$s'
12 Definiciones del argumento no dadas
13 No se pudo asignar capacidad de almacenamiento para el análisis de argumento
14 El string de argumentos excede la longitud máxima: %1$s
15 Número %1$d de mensaje no válido dentro del catálogo para análisis de argumento
16 [opciones]
17 Opciones:

$set 4 MSGCAT_SET_PARAMETER
2 %1$s no es un directorio. Creando %2$s en ".".
3 No se puede abrir %1$s. Archivo de configuración no creado.
4 No se puede escribir línea %1$d al archivo de configuración %2$s.
6 No se puede acceder al archivo de configuración del sistema "%1$s": %2$s
7 HOME no ajustado
8 El parámetro requierido "%1$s" no tiene ningún valor.
9 No se puede abrir el archivo de configuración "%1$s": %2$s
10 El parametro '%1$s' en la línea %2$d del archivo '%3$s'
11 Variable de entorno %1$s:
12 Palabra clave no reconocida.
13 El tipo de valor no coincide con el tipo de parámetro.
14 No se puede asignar capacidad de memoria para almacenar el valor.
15 Valor de string sin terminación.
16 Valor es fuera de rango.
17 error de UNIX: %1$s
18 No hay mensajes disponibles.
19 Valor es fuera de rango. Restableciendo al valor predeterminado.
20 Keyword = '%1$s'    Value = %2$d    Default Value = %3$d\n
21 Keyword = '%1$s'    Value = %2$f    Default Value = %3$f\n
22 Parámetro obsoleto
23 Los parámetros '%1$s' e '%2$s' no pueden utilizarse juntos.\n
31 El parametro '%1$s':

$set 5 MSGCAT_SET_ERROR
1 Faltando mensaje para el codigo de error %1$d.
2 Fallo del sistema interno: no hay más información específica disponible.
3 Sin memoria virtual: no se pueden asignar %1$lld bytes de memoria.
4 Ha sido interrumpido.
5 Clave = "%1$s" no ha sido encontrada en la tabla hash "%2$s".
6 Una tabla hash de memoria NULL fue suministrada a una función de tabla hash.
7 Tratando de formatear el volumen de disco "%1$s" con un valor incorrecto %2$d para el numero de paginas.
8 No se puede formatear el volumen de disco "%1$s" con %2$d paginas (%3$lld bytes).
9 No se puede formatear el volumen de disco "%1$s" con %2$d paginas (%3$lld Kbytes) debido a la falta de espacio. Espacio actual disponible es %4$d pages (%5$lld Kbytes).
10 No se puede montar el volumen de disco "%1$s".
11 No se puede montar el volumen de disco "%1$s". La base de datos "%2$s", a la que pertenece el volumen de disco, está en uso por el usuario %3$s en el proceso %4$d del host %5$s desde %6$s.
12 Problemas al desmontar el volumen "%1$s".
13 Un error de E/S ha ocurrido al leer la pagina %1$d del volumen "%2$s".
14 Un error de E/S ha ocurrido al escribir la pagina %1$d del volumen "%2$s".
15 No se puede escribir la página %1$d del volumen "%2$s" debido a la falta de espacio en el dispositivo de sistema operativo.
16 Incapaz de renombrar el volumen "%1$s" en "%2$s".
17 Error interno: obteniendo pageid %1$d desasignado del volumen "%2$s".
18 Todos buferes de página estan arreglados (LRU: %1$d).
19 Error interno: pageptr = %1$p de la pagina %2$d del volumen "%3$s" no esta arreglado.
20 Error interno: pageptr = %1$p desconocido.
21 Error interno: sector %1$d desconocido del volumen "%2$s".
22 Error interno: pagina %1$d desconocida de volumen "%2$s".
23 Error interno: tratando de cancelar la asignación de la pagina %1$d del disco del sistema de volumen "%2$s".
24 Casi sin espacio disponible en el volumen "%1$s". Numero total de paginas = %2$d, numero total de paginas libres = %3$d.
25 Casi sin espacio disponible en volúmenes con datos como el propósito principal de almacenamiento. Numero total de paginas = %1$d, numero total de paginas libres = %2$d.
26 Casi sin espacio disponible en los volúmenes con índice como el propósito principal de almacenamiento. Numero total de paginas = %1$d, numero total de paginas libres = %2$d.
27 Casi sin espacio disponible en los volúmenes con fines de almacenamiento genérico. Numero total de paginas = %1$d, numero total de paginas libres = %2$d.
28 Casi sin espacio disponible en los volúmenes con datos temporales como el propósito principal de almacenamiento. Numero total de paginas = %1$d, numero total de paginas libres = %2$d.
29 La ultima notificación "Sin espacio" para el volumen "%1$s". Numero total de paginas = %2$d, numero total de paginas libres = %3$d.
30 La  ultima notificación "Sin espacio" para los volúmenes con datos como el propósito principal de almacenamiento. Numero total de paginas = %1$d, numero total de paginas libres = %2$d.
31 La ultima notificación "Sin espacio" para los volúmenes con índice como el propósito principal de almacenamiento. Numero total de paginas = %1$d, numero total de paginas libres = %2$d.
32 La ultima notificación "Sin espacio" para los volúmenes con fines de almacenamiento genérico. Numero total de paginas = %1$d, numero total de paginas libres = %2$d.
33 La ultima notificación "Sin espacio" para los volúmenes con datos temporales como el propósito principal de almacenamiento. Numero total de paginas = %1$d, numero total de paginas libres = %2$d.
34 Error interno: %1$d nthpage es fuera de rango para el archivo %2$d en volumen "%3$s". Número actual de páginas es %4$d.
35 Error interno: Identificador %1$d de volumen desconocido.
36 Sin espacio de disco en la base de datos. Número de página(s) solicitada(s) = %1$d.
37 Sin espacio de disco en la base de datos volumen "%1$s". Número de página(s) solicitada(s) = %2$d.
38 Error interno: Archivo VFID %1$d|%2$d desconocido.
39 Error interno: La pagina %1$d|%2$d(volumen "%3$s") del archivo VFID %4$d|%5$d(volume "%6$s") no es válida según el mapa de reparto.
40 Error interno: %1$d páginas esperadas, %2$d páginas encontradas para el archivo VFID %3$d|%4$d(volumen "%5$s").
41 Error interno: La página %1$d|%2$d(volumen "%3$s") no es parte del archivo VFID %4$d|%5$d(volumen "%6$s").
42 No se pueden reordenar los registros anclados en la pagina %1$d del volumen "%2$s".
43 Error interno: La página %1$d del volumen "%2$s" puede ser dañada. %3$d registros encontrados mientras %4$d eran esperados.
44 Sin espacio disponible en la página ranurada %1$d del volumen "%2$s".
45 El slot %1$d en la página %2$d del volumen "%3$s" es asignado a un registro anclado. Un nuevo registro no se puede insertar aquí.
46 Error interno: el slot %1$d en la pagina %2$d del volumen "%3$s" no es asignado.
47 No se puede crear un archivo de memoria dinámica en el volumen "%1$s".
48 Accesando el objecto eliminado %1$d|%2$d|%3$d.
49 Error interno: clase del objecto %1$d|%2$d|%3$d es desconocida.
50 Error interno: el registro de reubicación del objecto %1$d|%2$d|%3$d puede ser dañado.
51 Error interno: el objeto %1$d|%2$d|%3$d puede ser dañado.
52 Error interno: sobreflujo de objeto, dirección %1$d|%2$d|%3$d puede ser dañada.
53 Extraendo objeto %1$d|%2$d|%3$d cuando sólo su OID ha sido asignado.
54 Error interno: tratando de manipular un objeto de %1$d páginas. El tamaño máximo permitido para un objeto es %2$d páginas.
55 Error interno: Una referencia de ciclo de página ha sido detectada en la página %1$d|%2$d del archivo de memoria dinámica %3$d|%4$d|%5$d.
56 Error interno: archivo de hash extensible desconocido (Volid: %1$d Fileid: %2$d) página (Volid: %3$d Pageid: %4$d) ha sido especificado.
57 La clave no existe en la estructura hash extensible.
58 String "%1$s" es demasiado largo para que quepa en una página ranurada.
59 Error interno: el tipo %1$d de clave especificado no es valido para la estructura hash extensible.
60 Error interno: la estructura hash extensible ha sido dañada.
61 Error Interno: la pagina de origen del directorio de la estructura de hashing extensible (Volid:%1$d Fileid: %2$d Pageid: %3$d)ha sido danada.
62 Error interno: el registro de clasificación %1$d (tamaño %2$d) no cabe en una página; el tamaño máximo permitido es %3$d.
63 Error interno: una página temporal ha sido dañada durante la clasificación.
64 Clase "%1$s" desconocida.
65 Clase "%1$s" ya existe.
66 Error interno: operacion de fuerza desconocida %1$d para el objeto %2$d|%3$d|%4$d.
67 Error interno: un archivo de memoria dinámica no ha sido asignado para almacenar el objeto %1$d|%2$d|%3$d.
68 Error interno: nombres de clase distintos se han encontrado para la clase de oid = %1$d|%2$d|%3$d. Los nombres de clase encontrados son "%4$s", "%5$s" utilizando la tabla hash classname y la memoria dinamica, respectivamente.
69 Error interno: identificadores distintos de objeto de clase se han encontrado para la clase con el nombre "%1$s". Los OIDS encontrados son %2$d|%3$d|%4$d, %5$d|%6$d|%7$d utilizando la tabla hash y la memoria dinamica, respectivamente.
70 Error interno: La clase con el nobre "%1$s" y oid = %2$d|%3$d|%4$d no existe en la tabla hash classname.
71 Error interno: La clase con el nombre "%1$s" y oid = %2$d|%3$d|%4$d no existe en su memoria dinamica.
72 Su transacción (index %1$d, %2$s@%3$s|%4$d) ha sido abortada unilateralmente por el sistema.
73 Su transacción (index %1$d, %2$s@%3$s|%4$d) ha expirado esperando al bloqueo %5$s en el objeto %6$d|%7$d|%8$d. Esta Usted esperando que el usuario %9$s acabe.
74 Su transacción (index %1$d, %2$s@%3$s|%4$d) ha expirado esperando al bloqueo %5$s en la clase %6$s. Esta Usted esperando que el usuario %7$s acabe.
75 Su transacción (index %1$d, %2$s@%3$s|%4$d) ha expirado esperando al bloqueo %5$s en la instancia %6$d|%7$d|%8$d de la clase %9$s. Esta Usted esperando que el usuario %10$s acabe.
76 Su transacción (index %1$d, %2$s@%3$s|%4$d) ha expirado esperando el %5$s en la pagina %6$d|%7$d. Esta Usted esperando que el usuario %8$s libere el bloqueo de la pagina.
77 Error interno: todos los buferes son arreglados.
78 Error interno: un error de E/S ha ocurrido al leer la página de registro lógico %1$lld (pagina física %2$lld) de "%3$s".
79 Error interno: un error de E/S ha ocurrido al escribir la página de registro lógico %1$lld (pagina física %2$lld) de "%3$s".
80 Espacio insuficiente en el dispositivo del sistema operativo al escribir la página de registro lógico %1$lld (pagina fisica  %2$lld) del "%3$s". No se puede escribir mas de %4$d bytes.
81 Error interno: página de registro lógico %1$lld puede ser dañada.
82 No se puede montar registro de volumen de disco / archivo "%1$s".
83 La longitud de la ruta "%1$s" mas la longitud del prefijo logname "%2$s" es demasiado larga; la longitud combinada debe ser inferior a %3$d.
84 La longitud del prefijo logname "%1$s" es demasiado larga; la longitud debe ser inferior a %2$d.
85 El nombre del prefijo "%1$s" no es el mismo que "%2$s" en el disco de registro. El registro puede haber sido renombrado fuera del dominio de la base de datos.
86 La base de datos es incompatible con el actual "%1$s" release "%2$s".
87 Hay acciones de recuperación que deben ser recuperadas utilizando "%1$s" release "%2$s" en vez del release "%3$s". Despues de la recuperación, la base de datos se puede ejecutar utilizando el release "%4$s".
88 Error interno: release string "%1$s" no puede exceder %2$d. Debe cambiar el encabezamiento del registro.
89 El registro "%1$s" no pertenece a la base de datos dada.
90 Redo logging es siempre una operacion de registro a nivel de pagina. Un puntero de página de datos debe ser suministrado como parte de la dirección.
91 Postpone logging es siempre una operacion de registro a nivel de pagina. Un puntero de página de datos debe ser suministrado como parte de la dirección.
92 Compensate logging es siempre una operacion de registro a nivel de pagina. Un puntero de página de datos debe ser suministrado como parte de la dirección.
93 Advertencia: redo logging es ignorada durante recuperaciones tipo redo y rollback normales.
94 Advertencia: postpone logging es ignorada durante recuperaciones tipo redo y rollback normales.
95 Advertencia: undo logging es ignorada mientras recuperaciones y rollback normal.
96 Recuperacion de media puede ser necesaria en el volumen "%1$s".
97 Error interno: incapaz de localizar pagina de registro %1$lld en las carpetas de registro.
98 No se puede generar carpeta de registro "%1$s" para archivar paginas de %2$lld a %3$lld.
99 Incapaz de generar un respaldo de archivo de informacion de directorio "%1$s".
100 No se puede respaldar la base de datos "%1$s".
101 Archivo "%1$s" de usuario desconocido.
102 Archivo "%1$s" no tiene suficientes entradas: %2$d entradas esperadas.
103 Archivo "%1$s" parece tener entradas deordenadas. Entrada %2$d con valores: %3$d %4$s %5$s, ha sido localizada cuando se esperaba entrada con valores %6$d %7$s.
104 Archivo "%1$s" parece tener una entrada incorrecta para el nombre de base de datos (volumen principal). Entrada %2$d con valores: %3$d %4$s %5$s, ha sido encontrada cuando se esperaba entrada con valores %6$d %7$s %8$s.
105 No se puede accesar el respaldo de archivo "%1$s". Restore o Backup estan cancelados.
106 Incapaz de preparar para cometer la transacción actual con el identificador de transacción global %1$d porque ese mismo identificador de transacción está en uso por otra transacción.
107 No hay ninguna transacción global distribuida, asociada con el identificador de transacción %1$d.
108 La transacción actual %1$d debe ser cometida o abortada antes de intentar unirse a la transacción distribuida con el identificador global %2$d.
109 Su transacción %1$d ha sido abortada unilateralmente por el sistema porque un sitio participante (a distancia) se cayo o ha unilateralmente abortado la transaccion.
110 El valor del nivel de aislamiento tiene que estar entre %1$d y %2$d.
111 Su transacción ha sido abortada unilateralmente por el sistema debido a un fallo del servidor o a un cambio de modo.
112 Host "%1$s" no es autorizado ejecutar la base de datos.
113 Incapaz de reiniciar/inicializar el servidor de la base de datos. %1$s
114 Incapaz de interpretar "%1$s" como una base de datos. Los volumenes/archivos de la base de datos pueden haber sido renombrados/copiados fuera del dominio de base de datos.
115 Base de datos "%1$s" ya existe.
116 Base de datos "%1$s" es desconocida, o el archivo "databases.txt" no se puede accesar.
117 El nombre de ruta absoluto para la base de datos es demasiado largo. La longitud combinada de la ruta "%1$s" y el nombre "%2$s" es %3$d; la longitud combinada debe ser inferior a %4$d.
118 No se ha podido localizar el directorio de trabajo actual.
119 Incapaz de encontrar el hostname local.
120 El numero maximo de volumenes (%1$d) ha sido excedido.
121 Tratando de eliminar "%1$s", un volumen permanente de la base de datos.
122 No se puede acceder al catálogo del sistema de mensajes.
123 No se puede crear %1$s para base de datos "%2$s". Consulte "%3$s" para informacion adicional.
124 Volumen "%1$s" ya existe.
125 Una extension automatica de volumen "%1$s" de %2$d paginas y propósito de almacenamiento genérico ha sido creada.
126 Propósito desconocido "%1$s" dado en la linea %2$d.
127 Valor incorrecto %1$d para numero de paginas dado en la linea %2$d.
128 Numero de paginas no fue dado en la linea %1$d.
129 Token "%1$s" desconocido has sido encontrado en la linea %2$d.
130 Sin memoria virtual.
131 Error del sistema de base de datos.
132 Característica no implementada en el release actual: %1$s.
133 Constante de conjunto anidado no es permitida.
134 No es permitido utilizar atributos de clase y atributos de non-clase en un sola actualización.
135 Declaracion invalida para columnas oid: %1$s.
136 Función "%1$s" no implementada.
137 Objetos de autorizacion danados.
138 Clase de autorizacion  "%1$s" no encontrada.
139 Error de acceso al autorizar el atributo "%2$s" de la clase "%1$s".
140 Operación "%1$s" sólo puede ser realizada por el DBA (administrador de base de datos) o un miembro del grupo DBA.
141 No se puede agregar "%1$s" como miembro de "%2$s".
142 Adición de miembro causa que la jerarquía de usuario se convierta en un gráfico cíclico.
143 Encontrado una clase sin dueno.
144 Incapaz de accesar el objeto de usuario.
145 No se pueden distribuir comandos GRANT/REVOKE a si mismo.
146 No se pueden distribuir comandos GRANT/REVOKE al dueno de una clase.
147 Sin opcion GRANT.
148 No se puede obtener bloqueo de escritura en objeto de autorización.
149 No se puede crear instancia de objeto de autorizacion.
150 No se pueden revocar privilegios de si mismo.
151 No se pueden revocar privilegios del patrocinador de una clase.
152 GRANT no encontrado.
153 Sin privilegios de autorización en vigor para la base de datos.
154 Instalacion de autorizacion incompleta - sin privilegios de autorización en vigor para la base de datos.
155 Origenes de autorizacion multiples.
156 Fallo de autorizacion.
157 error de autorización de SELECT.
158 error de autorización de ALTER.
159 error de autorización de UPDATE.
160 error de autorización de INSERT.
161 error de autorización de DELETE.
162 error de autorización de INDEX.
163 error de autorización de EXECUTE.
164 Usuario "%1$s" ya existe.
165 Usuario "%1$s" es invalido.
166 Usuario no valido especificado.
167 DBA, miembros del grupo DBA y el patrocinador de clase pueden ejecutar la operacion.
168 Miembro no encontrado.
169 No se puede eliminar usuario %1$s de la base de datos.
170 Ningun usuario conectado al sistema.
171 Contrasena incorrecta o faltante.
172 El string de contrasena no puede tener mas de 31 bytes.
173 No se puede localizar el archivo "%1$s" de la base de datos.
174 No se puede obtener acceso de escribir al archivo "%1$s" de la base de datos.
175 Archivo "%1$s" de la base de datos mal formateado.
176 Error de conversion en el formato fecha.
177 No se puede crear objeto grande para almacenamiento interno.
178 OID temporario encontrado sin MOP.
179 Dominio "%1$s" no es compatible con el dominio "%2$s".
180 Dominio "%1$s" del valor suministrado no es compatible con el dominio "%2$s" del destino.
181 No se puede coercer valor del dominio "%1$s" al dominio "%2$s".
182 No se puede coercer valor del dominio "%1$s" al dominio "%2$s" sin sobreflujo.
183 Cantidad inesperada de datos recibidos; %1$d esperado, %2$d recibido.
184 No se puede asignar bufer de comunicacion.
185 Error al recibir datos del cliente.
186 Error al recibir datos del servidor.
187 Bufer de comunicacion no utilizado.
188 Base de datos "%1$s" desconocida.
189 Nombre de host "%1$s" no valido.
190 Host del servidor no identificado.
191 No se puede conectar al servidor "%1$s" en "%2$s".
192 No se puede cargar el archivo de configuracion de red.
193 Servidor ha recibido orden de cierre del cliente.
194 Id %1$d de solicitud del servidor desconocida.
195 Error de comunicacion del servidor: %1$s.
196 Nombre del servidor no identificado.
197 No se puede establecer contacto con el servidor maestro.
198 Búfer de recepción demasiado pequeño, datos truncados.
199 Servidor no responde.
200 Conjunto no fue removido del objeto de manera correcta despues de su liberacion.
201 Entrada de tabla UNIQUE incorrecta eliminada.
202 Atributo "%1$s" no fue encontrado.
203 El valor para el atributo "%1$s" tiene que ser de tipo "%2$s", y no de tipo "%3$s".
204 Función llamada con argumentos que faltan o no válidos.
205 El atributo "%1$s" no puede ser hecho NULL.
206 Atributo "%1$s" no tiene la restricción UNIQUE.
207 La clase "%1$s" tiene un objeto de tamano cero.
208 Método "%1$s" no encontrado.
209 No fueron encontrados ningun atributo o método con el nombre "%1$s".
210 Error interno: procesando plantilla de objeto.
211 Atributo fue eliminado mientras constryendo plantilla de objeto.
212 Atributo "%1$s" viola la restricción UNIQUE.
213 Encontrado entrada de tabla UNIQUE no valida.
214 Un conflicto de dominio existe para el atributo "%1$s".
215 Atributo con id %1$d no encontrado.
216 Etiqueta de posesion de objeto invalido para el valor.
217 Operacion solo puede ser ejecutada para objetos de clase.
218 Objeto no valido en la expresion de ruta db_get.
219 Expresion de ruta db_get no valida.
220 Conjunto no valido en la expresión de ruta db_get.
221 Indice de conjunto no valido en la expresión de ruta db_get.
222 Intentando asignar un valor de string superior a %2$d caracteres al atributo "%1$s".
223 Valor %2$d demasiado grande para atributo "%1$s" de tipo smallint.
224 Una base de datos no ha sido reinicializada.
225 Faltando valor para el atributo "%1$s" con restriccion de tipo NOT NULL.
226 Lista vacia de componentes en la clase.
227 Intente de asignar valor al atributo "%1$s" mas de una vez.
228 Metodo "%1$s" llamado con %2$d argumentos. El sistema permite un maximo de %3$d argumentos.
229 Metodo "%1$s" requiere que el argumento %2$d sea dentro del dominio "%3$s". Un valor invalido del dominio "%4$s" fue dado.
230 Atributo "%1$s" no puede ser asignado un string de longitud %2$d. La maxima longitud de string permitida es %3$d.
231 Operacion API no valida intentada en un objeto temporal.
232 No es permitido utilizar plantillas de objeto sobre transacciones multiples.
233 No se puede crear clase con nombre de tipo basico "%1$s".
234 Archivo "%1$s" de metodo no encontrado.
235 %1$d funciones de metodo no resueltas después de la vinculación.
236 Metodo "%1$s" no resuelto.
237 Problemas graves fueron encontrados durante la vinculación dinámica.
238 No se puede accesar el archivo "%1$s" de metodo.
239 Atributo "%1$s" no encontrado.
240 Metodo "%1$s" no encontrado.
241 No existen ningun atributo o metodo con nombre "%1$s".
242 Firma para %2$s no encontrada para el metodo "%1$s".
243 Argumento %2$d del metodo "%1$s" no es definido.
244 Nombre de dominio "%1$s" no es el nombre de una clase o tipo basico.
245 El nombre "%1$s" ha sido ya utilizado como un nombre de atributo.
246 El nombre "%1$s" ha sido ya utilizado como un nombre de metodo.
247 Argumentos no validos a la funcion interna del manager de esquema.
248 El dominio "%1$s" de atributo no es adecuado para utilizar con la restriccion %2$s.
249 La operacion solicitada en la clase "%1$s" solo puede ser ejecutada antes de que la instancias sean creadas.
250 No se puede crear indice en el atributo "%1$s", definido con un valor compartido.
251 Firma %2$s ya existe para el metodo "%1$s".
252 Atributo "%1$s" debe tener un dominio de conjunto para esa operacion.
253 Atributo "%1$s" no puede ser definido con un dominio de conjunto anidado.
254 Incapaz de eliminar el dominio "%2$s" del atributo "%1$s".
255 Atributo "%1$s" no es de un dominio de tamano variable.
256 Superclase ya existe.
257 Adicion de superclase causaria ciclos en la jerarquía de clase.
258 Superclase no encontrada.
259 Hay firmas multiples para metodo "%1$s".
260 Argumento %2$d de metodo "%1$s" no es de un dominio de conjunto.
261 Resolucion para "%1$s" no encontrada.
262 Dominio de valor "%2$s" no es adecuado para el atributo "%1$s".
263 Corrupción interna detectada en el manager de esquema.
264 Alias "%2$s" para atributo/metodo "%1$s" no es unico.
265 Atributo de shadowing "%1$s" con dominio conflictivo "%2$s".
266 Conflicto de nombre en atributo "%1$s" con clases "%2$s" y "%3$s".
267 Metodo de shadowing "%1$s" con firma conflictiva.
268 Conflicto de nombre en metodo "%1$s" con clases "%2$s" and "%3$s".
269 No se puede definir indice en dominio "%1$s".
270 No se puede definir restriccion UNIQUE en el dominio "%1$s".
271 Ciclo detectado en la jerarquía de clase; "%1$s" es una super clase de "%2$s".
272 Indice "%2$s" ya definido para clase "%1$s".
273 Indice "%1$s" no existe.
274 Caracteres ilegales en nombre: "%1$s".
275 Operacion ilegal en atributo "%1$s", heredada de la clase "%2$s".
276 Operacion ilegal en metodo "%1$s", heredada de la clase "%2$s".
277 Operacion ilegal en "%1$s", heredada de la clase "%2$s".
278 Clase "%4$s" tiene dominios incompatibles para "%1$s", heredados de las clases "%2$s" y "%3$s".
279 En la clase "%4$s", "%1$s" heredado de la clase "%2$s" no puede ser utilizado como solicitado. La definicion de la clase "%3$s" tiene un dominio mas especifico.
280 En la clase "%3$s", "%1$s" no puede ser heredado de la clase "%2$s" porque hay una definicion local con un dominio incompatible.
281 En la clase "%3$s", "%1$s" heredado de la clase "%2$s" es utilizado como alias, pero ningun sustituto de otra fuente se ha encontrado.
282 En la clase "%4$s", "%1$s" heredado de la clase "%3$s" no puede servir como sustituto de alias para la clase "%2$s". Los dominios son incompatibles.
283 En la clase "%4$s", "%1$s" heredado de la clase "%3$s" no puede servir como sustituto de alias para la clase "%2$s". Tiene un dominio menos especifico.
284 No se puede solicitar herencia de "%1$s" porque es definida localmente en la clase.
285 No se puede utilizar "%1$s" como alias para la componenta heredada "%2$s" porque ya hay una componenta definida localmente con ese nombre.
286 No se puede utilizar "%1$s" como alias para "%2$s" de "%3$s". Una componenta con ese nombre ya es heredada de "%4$s".
287 En la clase "%1$s", no se puede definir atributo "%2$s" porque oscureceria un metodo heredado de "%3$s".
288 En la clase "%1$s", no se puede definir metodo "%2$s" porque oscureceria un atributo heredado de "%3$s".
289 Subclase "%1$s" no puede heredar metodo "%2$s" de la clase "%3$s" porque ya hay un atributo con ese nombre.
290 Subclase "%1$s" no puede heredar atributo "%2$s" de la clase "%3$s" porque ya hay un metodo con ese nombre.
291 "%1$s" es definido en la clase "%2$s" como atributo y en clase "%3$s" como metodo.
292 Ninguna especificación poblar con el indice "%1$s".
293 Operacion no permitida para este tipo de clase.
294 Variable de entorno "%1$s" indefinida, referenciada en un archivo de metodo.
295 Incapaz de alterar clase "%1$s". Sin espacio en el catalogo de sistema. Para reclamar espacio, compactar la base de datos con la utilidad compactdb.
296 Encontrado lista de propiedad no valida.
297 Nombre de alias "%1$s" es utilizado mas de una vez.
298 En clase "%1$s", una resolucion para "%2$s" de "%3$s" no es permitida. Esa clase no es una super clase.
299 "%1$s" es una palabra reservada y no puede ser utilizada como atributo, metodo o nombre de clase.
300 Atributo "%1$s" no puede ser definido junto con las restricciones DEFAULT y UNIQUE.
301 Atributo "%1$s" no puede ser definido con una longitud de restriccion de %2$d. La longitud de restriccion maxima es %3$d.
302 No se puede agregar elemento a un conjunto.
303 Valor ya existe en el conjunto.
304 Indice del conjunto %1$d es fuera de limites.
305 Indice de secuencia %1$d es fuera de limites.
306 Operacion solicitada solo puede ser ejecutada en secuencias.
307 Operacion solicitada solo puede ser ejecutada en conjuntos.
308 Valor de elemento no esta dentro del dominio del conjunto.
309 Indice ilegal de elemento de conjunto dado: %1$d.
310 Elemento no encontrado en conjunto.
311 Elemento no encontrado en secuencia.
312 "%1$s" no es un dominio de conjunto valido.
313 Bajoflujo del bufer de objeto al leer.
314 Bajoflujo del bufer de objeto al escribir.
315 Definicion de metaclase ilegal encontrada.
316 Cálculo de tamano del transformador desajustado, esperado %1$d calculado %2$d.
317 Representacion de disco no valida encontrada para la clase "%1$s".
318 Sin sincronización durante la carga de objeto. La base de datos está probablemente dañada o anticuada.
319 Ignorando extension de atributo desconocida.
320 Encontrado respresentacion de disco corrupta del objeto.
321 Detectado lo que puede ser una corrupción en el área de trabajo.
322 MOP no encontrado en el área de trabajo.
323 Intente actualizar MOP non-temporal en el área de trabajo.
324 Objeto encontrado en el área de trabajo sin clase.
325 Intento ejecutar el Garbage Collector para un MOP de la lista de objetos "sucios".
326 Cambiando puntero de clase para objeto en el área de trabajo.
327 No se puede crear MOP con NULL OID.
328 Instancia encontrada en el área de trabajo sin clase.
329 Enlace de la lista de objeto no asignada en el área de trabajo.
330 Violación de pin del área de trabajo.
331 No se puede asignar bloque de área de trabajo del sistema operativo, sin memoria virtual. Abortando transaccion.
332 El estimado original del tamano del área de trabajo ha sido excedido. Una extensión automática se ha hecho.
333 Sin memoria durante la asignación de almacenamiento del área de trabajo. Abortando transaccion.
334 Asignacion intentada de %1$d bytes es superior al tamano maximo del bloque del área de trabajo. Intentar ajustar max_block_size. Transaccion abortada.
335 Ignorando intente de liberar objeto no asignado dentro del área de trabajo.
336 Ignorando intente de liberar un objeto mas de una vez.
337 Ignorando intente de asignar objeto de tamano negativo.
338 Identificador de entrada de mapa sobreejecutado.
339 No se puede salir de la sección crítica, como no es patrocinada por transaccion %1$d.
340 La funcion fork no puede ser utilizada sobre proceso child.
341 La funcion exec no puede ser utilizada sobre proceso child.
342 No se puede cambiar el grupo de proceso.
343 Request_id ya es utilizado.
344 %1$s.
345 Error ocurrido al leer un mensaje de cierre del maestro.
346 Error ocurrido al leer un mensaje de detener el cierre del master.
347 No se puede establecer un tunel hacia el servidor maestro.
348 Debe especificar un identificador sea de servicio o de puerto.
349 Servicio desconocido para TCP: %1$s.
350 Error al obtener el nombre del host local para el host "%1$s". Host no encontrado.
351 No se puede crear enchufe TCP.
352 tcp_open: no se puede obtener un puerto TCP reservado.
353 No se puede establecer conexion hacia el servidor maestro en host "%1$s".
354 No se puede establecer patrocinador de proceso nuevo para enchufe.
355 No se puede crear toma de corriente.
356 '%1$s' archivo para enchufe de dominio UNIX existe.
357 No se puede unir dirección local...abortando.
358 Fallo de Accept.
359 Error de vinculacion del datagrama.
360 Error de aceptar del datagrama.
361 Error de conectar del datagrama.
362 Error de enchufe del datagrama.
363 Recvmsg del enchufe maestro ha fallado.
364 Enviando una nueva solicitud al servidor.
365 Enviando un mensaje de difusión al cliente.
366 Error del servidor: %1$s
367 Servidor %1$s ya existe.
368 Error de comunicación durante la conexión para %1$s.
369 Codigo de error no utilizado.
370 Codigo de error no utilizado.
371 Codigo de error no utilizado.
372 Codigo de error no utilizado.
373 EOF prematuro al leer: %1$d.
374 EOF prematuro al leer el %1$d o %2$d.
375 Conversión de error en (0x%1$c%2$c) %3$d y %4$d convertido en %5$d.
376 Ninguna clase publica encontrada.
377 Error %1$d: fallo al añadir atributo "%2$s" a la jerarquía multimedia.
378 Error %1$d: fallo al añadir metodo "%2$s" implementado como "%3$s" a la jerarquía multimedia.
379 Error %1$d: fallo al añadir una superclase a la jerarquía multimedia.
380 Cargador dinámico ya inicializado.
381 Cargador dinámico no inicializado.
382 Cargador dinámico inicializado impropiamente.
383 "%1$s" no es ni un archivo de objeto ni una carpeta.
384 No se puede determinar ruta absoluta para "%1$s".
385 Proceso ld salio con el codigo %1$d.
386 Proceso ld salio debido a la senal %1$d.
387 Proceso ld salio con waitval 0x%1$x desconocida.
388 No se puede accesar la imagen inicial para "%1$s".
389 Error de sistema: %1$s.
390 Problema con "%1$s": %2$s.
391 Conflicto con otro handler SIGPIPE.
392 Incapaz de desovar el cargador dinamico daemon.
393 Cargador dinamico daemon ha desaparecido.
394 Rango endpoint no entre 0 y 256.
395 Subexpresion ilegal especificada.
396 Dígito octal fuera del rango existe en escape.
397 Expresion regular no delimitada correctamente.
398 Busqueda de texto llamada antes de que la expresion compile.
399 Una paréntesis sin par es encontrada en la expresion regular.
400 Paréntesis anidados demasiado profundo en la expresión regular.
401 Mas de dos numeros especificados en un rango.
402 Una llave sin par es encontrada en la expresion regular.
403 Primer numero en la expresion de rango es mas grande que el segundo.
404 Un corchete sin par es encontrado en la expresion regular.
405 La expresion regular ha sido demasiado larga para compilar.
406 Identificador de indice B+tree no valido: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
407 Clave %1$s desconocida referenciada en el indice de B+tree {vfid: (%2$d, %3$d), rt_pgid: %4$d, key_type: %5$s}.
408 Valor (oid) %1$d|%2$d|%3$d referenciado desconocido.
409 Valor duplicado (oid) %1$d|%2$d|%3$d referenciado.
410 Clave NULL referenciada.
411 Tipo de clave "%1$s" de idice B+tree no valido.
412 Especificacion de busqueda de rango no valida.
413 Indentificador de atributo desconocido: %1$d.
414 Identificador de clase deconocido: %1$d|%2$d|%3$d.
415 Identificador de clase no valido: %1$d|%2$d|%3$d.
416 Identificador de representacion desconocido: %1$d.
417 Identificador de representacion no valido: %1$d.
418 Descriptor de registro no tiene suficiente espacio: %1$d.
419 Clase ha alcanzado la cuenta maxima de representaciones: %1$d.
420 Clase %1$d|%2$d|%3$d ha tenido ya representaciones.
421 Representaciones Falta Directorio en Catálogo para Clase: : %1$d|%2$d|%3$d.
422 Representation Falta Informacion de Registro en Catalogo para Clase - Repr_Id: %1$d|%2$d|%3$d - %4$d.
423 Sesion no valida.
424 Ninguna declaración a ejecutar.
425 Tipo de datos incompatible para atributo "%1$s" de clase "%2$s".
426 Tipo de datos incompatible en atributo "%1$s".
427 Sobreflujo de datos en tipo de datos "%1$s".
428 Declaracion no es actualizable.
429 Comando desconocido.
430 Variable desconocida "%1$s".
431 Error en el intérprete o analizador SQL/X.
432 Clase desconocida "%1$s".
433 Atributo desconocido "%1$s" en clase "%2$s".
434 No se pueden encontrar nombres de columna correctos de una consulta.
435 Declaraciones multiples no permitidas.
436 Declaracion no es una consulta.
437 Objeto largo no valido ID (%1$s,%2$d, %3$s, %4$s).
438 Conflicto existe en el descriptor de objeto largo.
439 Offset %1$d es mayor que la longitud de objeto largo %2$d.
440 Posicion de cursor no valida.
441 Operacion de cursor no valida.
442 Posicion de cursor desconocida.
443 Indice de valor de tupla %1$d es fuera del rango.
444 Nombre de atributo no valido: "%1$s".
445 Indice de lista de valor %1$d es fuera de rango.
446 Hay estructuras de resultados de consulta cerradas por el manager de transaccion pero no cerradas por la aplicacion.
447 Intentado operar una estructura de resultados de consulta ya cerrada.
448 Tamanos de tupla estan actualmente restringidos no ser mas grande de una pagina.
449 Identificador de consulta desconocido: %1$d.
450 Operador de conjunto no valido: %1$d.
451 Archivo de memoria dinamica sin instancias.
452 Contenido XASL del nodo de arbol no valido.
453 No hay mas nodos de especificacion de acceso.
454 Tipo de datos no valido referenciado.
455 No hay mas paginas de archivo de consulta.
456 Referencias de tipo de datos son incompatibles.
457 Tipo de resultado de consulta no valido.
458 Sobreflujo ocurrido en contexto de adicion.
459 Resultado de consulta contiene mas de una sola tupla.
460 Muy pocas variables de host de entrada suministradas.
461 Son dadas mas variables de host que columnas de resultado.
462 Valor de resultado es NULL, pero no hay ninguna variable de indicador.
463 Declaracion de cursor no esta preparada todavia.
464 Cursor tendria que ser una declaracion SELECT.
465 El cursor no esta todavia abierto.
466 El cursor tendria que ser cerrado antes de re-abrirlo.
467 Resultado tendria que regresar un solo objeto.
468 Ningun marcador posicional esta permitido en declaraciones EXECUTE IMMEDIATE.
469 Tipo de datos no valido utilizado.
470 Consulta involucra demasiados elementos para el optimizador.
471 Memoria agotada en linea %1$d del archivo %2$s.
472 Conector %1$s para la base de datos local '%2$s' en host '%3$s' se murio.
473 Reportado de la base de datos local '%1$s' sobre el host '%2$s': %3$s.
474 Error %1$s detectado durante %2$s.
475 Ninguna especificacion de consulta con indice %1$s.
476 LDB %1$s ya existe.
477 No se puede conectar al maestro en host %2$s para accesar LDB %1$s.
478 La base de datos local "%1$s" no es una LDB registrada.
479 La base de datos local "%1$s" esta siendo actualmente accesada por otros clientes, alteración o disminución no está permitido.
480 La base de datos local "%1$s" no es vacia y no puede ser abandonada.
481 "%1$s" no es una clase virtual proxy.
482 "%1$s" no es una clase virtual.
483 "%1$s" no es un atributo de la clase "%2$s".
484 El object_id para la clase virtual proxy "%1$s" es ya ajustado.
485 Uno o mas de un atributo object_id es NULL para instancia de clase proxy "%1$s".
486 Objeto no es una instancia de clase virtual actualizable.
487 Objeto no es una instancia.
488 Un atributo de una clase no puede ser una clase virtual o una clase virtual proxy.
489 Un atributo de una clase virtual proxy no puede ser una clase, una clase virtual o un objeto.
490 La superclase "%1$s" tiene que ser del mismo tipo de clase que "%2$s".
491 No se puede extraer nombre de entidad de la especificacion de consulta proxy "%1$s".
492 %1$s
493 Sintaxis: %1$s %2$s
494 Semántica: %1$s %2$s
495 Ejecutar: %1$s %2$s
496 Clave desconocida referenciada.
497 Valor desconocido (oid) %1$d|%2$d|%3$d referenciado.
498 Valor duplicado (oid) %1$d|%2$d|%3$d referenciado.
499 Clave NULL referenciada.
500 Clave duplicada referenciada.
501 Prioridad no valida del trigger.
502 Clase de destino faltando en el trigger.
503 Trigger "%1$s" no encontrado.
504 Error interno: procesando trigger "%1$s".
505 Un trigger con el nombre "%1$s" ya existe.
506 Un trigger no puede ser definido en "%1$s" porque es una clase virtual.
507 Clase de destino del trigger inapropiada "%1$s".
508 Atributo de destino del trigger inapropiado "%1$s".
509 Condición del trigger no valida "%1$s".
510 Accion del trigger no valida "%1$s".
511 Sin autorizacion de accesar trigger "%1$s".
512 Sin autorizacion para eliminar trigger "%1$s".
513 Sin autorizacion de actualizar trigger "%1$s".
514 Sin autorizacion de alterar trigger "%1$s".
515 Tiempo de ejecución de %2$s de la accion del triger no puede ser antes que la condicion de tiempo de ejecucion de %1$s.
516 Profundidad maxima %1$d del trigger excedida para trigger "%2$s".
517 La operacion ha sido rechazada por el trigger "%1$s".
518 Error interno: procesando trigger "%1$s".
519 Tipo ilegal de condicion del trigger.
520 La accion REJECT no puede ser utilizada con un tiempo de accion de AFTER o DEFERRED.
521 La accion REJECT no puede ser utilizada con los eventos ABORT o TIMEOUT.
522 Expresion de condicion del trigger no fue dada.
523 Expresion de accion del trigger no fue dada.
524 La actividad especificada no puede ser eliminada porque es patrocinada por otro usuario.
525 Error compilando condicion para "%1$s": %2$s
526 Error compilando accion para "%1$s": %2$s
527 Error al evaluar condicion para "%1$s": %2$s
528 Error al evaluar accion para "%1$s": %2$s
529 Transaccion no puede ser cometida, ha sido invalidada por el trigger "%1$s".
530 Faltando expresion regular.
531 Expresion regular es demasiado larga o compleja.
532 Parentesis o corchos desequilibrados en expresion regular.
533 Referencia es fuera de rango en expresion regular.
534 Carácter o sintaxis ilegal en la expresion regular.
535 Ningun texto dado para la busqueda de expresion regular.
536 Opcode ilegal encontrado en la expresion regular.
537 Codigo de comando de busqueda ilegal para el compilador de expresion regular.
538 Bufer de trabajo de expresion regular tiene que ser reinicializado.
539 Intente de dividir por cero.
540 %1$s.
541 Numero de paginas libres para volumen "%1$s" es inconsistente. %2$d y %3$d se han encontrado segun el encabezamiento de volumen y mapa de bits respectivamente.
542 Numero de sectores libres para volumen "%1$s" es inconsistente. %2$d y %3$d se han encontrado segun el encabezamiento de volumen y mapa de bits respectivamente.
543 Encabezamiento de disco para volume "%1$s" es inconsistente.
544 Error interno: Indice %1$s en clase %2$s (CLASS_OID: %3$d|%4$d|%5$d). Clave B+tree %6$s entrada para OID de objeto: %7$d|%8$d|%9$d no se ha encontrado en B+tree: %10$d|%11$d|%12$d.
545 Error interno: Indice %1$s en clase %2$s (CLASS_OID: %3$d|%4$d|%5$d). Clave y OID : %6$d|%7$d|%8$d entrada en B+tree: %9$d|%10$d|%11$d es incorrecta. El objeto no existe.
546 Error interno: Indice %1$s en clase %2$s (CLASS_OID: %3$d|%4$d|%5$d). Esperando %6$d valores de OID de entrada, pero %7$d se han encontrado en B+tree: %8$d|%9$d|%10$d.
547 Release %1$s del servidor es diferente del release %2$s de cliente.
548 No hay ningun object_id definido para la clase proxy "%1$s".
549 La instancia de proxy no ha sido encontrada.
550 Nombre de tipo savepoint desconocido %1$s
551 Incapaz de localizar el archivo de ruta referente a la informacion de volumen "%1$s"... Continuando leer de tablas internas
552 Bajoflujo de aritmetica de fecha.
553 Bajoflujo de aritmetica de tiempo.
$ LOADDB
554 El cargador ha entrado en un estado no valido. No hay mas operaciones posibles.
555 Error de asignacion de memoria durante la carga de la base de datos.
556 Demasiados valores suministrados. Esperando %1$d.
557 Para el atributo %1$s de %2$s, tipo %3$s no esta permitido en el dominio del conjunto.
558 Para el atributo %1$s de %2$s, tipo esperado %3$s ha recibido una constante de conjunto.
559 Para el atributo %1$s de %2$s, tipo esperado %3$s ha recibido %4$s.
560 Para atributo %1$s de %2$s, dominio no es suficientemente especifico para soportar referencias de objeto no calificadas.
561 Conjuntos anidados no son permitidos.
562 Clase %1$s de sistema no puede ser poblada usando el cargador de la base de datos.
563 Referencia a la clase interna %1$s convertida en NULL.
564 Restriccion UNIQUE violada para el atributo %1$s de %2$s.
565 Metodo creador %1$s no es definido en clase %2$s.
566 Ninguna clase especificada o ningun atributo en la clase.
567 Demasiados argumentos de metodo creador suministrados. Esperando %1$d.
568 Faltando parametros de metodo creador. Esperando %1$d, encontrados %2$d.
569 Faltando valores de atributo. Esperando %1$d, encontrados %2$d.
570 No se puede accesar el archivo "%1$s" de datos Glo.
571 Instancia ha sido anteriormente creada a traves de una referencia de tipo "forward". \nEsto no es permitido para instancias creadas usando metodos creador.
572 ERROR INTERNO: No se pueden construir representaciones de disco de instancia.
573 ERROR INTERNO: No se pueden insertar nuevas instancias en la base de datos.
574 ERROR INTERNO: No se puede actualizar nueva instancia en la base de datos.
575 Para argumento %1$d del constructor %2$s, tipo esperado %3$s, ha recibido %4$s.
576 Referencia hacia clase %3$s no es compatible con el dominio de %1$s de %2$s.
577 Para argumento %1$d del constructor %2$s, dominio no es suficientemente especifico para soportar\n referencias de objeto no calificadas.
578 Referencia hacia clase %3$s no es compatible con el dominio del argumento %1$d \n del constructor %2$s.
579 Dominio de destino tiene que incluir tipo "object" para permitir referencia a las clases.
580 Atributo %1$s no es definido para clase %2$s.
581 Intentado actualizar la base de datos cuando las actualizaciones son inhabilitadas.
582 Propósito de almacenamiento desconocido %1$d. Rango valido es de %2$d a %3$d.
583 Tratando de asignar un numero no valido (%1$d) de paginas.
584 Error interno: Una bucle ha sido detectada en la tabla de archivos del archivo %1$d en el volumen "%2$s". La bucle puede ser causada por la pagina %3$d|%4$d.
585 Memoria dinamica desconocida %1$s|%2$d|%3$d.
586 Error interno: Incapaz de actualizar encabezamiento de cadena en VPID %1$d|%2$d para la memoria dinamica %3$d en volumen "%4$s".
587 Entradas de volumenes permanentes no clasificadas en su archivo volinfo "%1$s". Entrada %2$d: %3$d %4$s es fuera de la sequencia.
588 ERROR INTERNO: Aserción '%1$s' fracasada.
589 Nombre de utilizador no valido "%1$s".
590 El dominio "%1$s" de atributo de una clase no puede ser una \nclase virtual o una clase virtual proxy.
591 El dominio "%1$s" de atributo de una clase virtual proxy \ndebe ser una clase virtual proxy.
592 El dominio "%1$s" de atributo de una clase virtual proxy \ny la clase virtual proxy deben estar en la misma base de datos local.
593 Una instancia virtual no actualizable ha sido reclamada y no puede ser reconstruida.
594 Una cláusula object_id debe ser suministrada sin utilizar atributos (%1$s) de tipos de datos aproximados (float o double).
595 El nombre "%1$s" de base de datos es demasiado largo. El nombre de base de datos tiene que ser inferior a %2$d caracteres.
596 Las %1$d paginas del espacio temporal total permitido han sido excedidas.
597 Numero de paginas para el archivo de memoria dinamica %1$d|%2$d|%3$d es inconsistente. \n%4$d y %5$d han sido encontradas segun la cadena de memoria dinamica y tabla de archivos, respectivamente. 
598 Numero de archivos es inconsistente. %1$d esperado, %2$d encontrado
599 Un error de E/S ha ocurrido mientras sincronizando el estado del volumen "%1$s".
600 Funcion "%1$s" no implementada en el PC.
601 "%1$s".
602 Error dl: "%1$s" definido mas de una vez.
603 Error interno: Sector/tabla de paginas del archivo VFID %1$d|%2$d parece ser danado/a.
604 Error interno: Un conjunto de asignacion para el archivo VFID %1$d|%2$d(Volumen "%3$s") parece ser inconsistente en %4$d paginas eliminadas o marcadas como eliminadas y en %5$d agujeros de pagina en tabla
605 Error interno: Numero inconsistente de paginas marcadas como eliminadas para el archivo VFID %1$d|%2$d(Volume "%3$s"). Esperadas %4$d, Encontradas %5$d.
606 No se puede localizar odbc.ini con la ruta "%1$s".
607 Base de datos "%1$s" no encontrada en odbc.ini.
608 Base de datos "%1$s" no tiene una especificacion de Host en el archivo odbc.ini
609 Su transaccion no puede ser repetida ya que el logging ha sido ignorado. Su base de datos puede ser danada.
610 Es probable que su base de datos esté dañada ya que el logging estaba apagado cuando su base de datos se estrello.
611 %1$s no puede ser ejecutado en modo cliente/servidor.
612 Logging no puede ser inhabilitado en este momento ya que hay acciones pendientes para recuperar.
613 En clase "%3$s", componenta local "%1$s" no puede servir como sustituto de alias para clase "%2$s". Los dominios no son compatibles.
614 Numero de carpetas de registros activos ha excedido el numero maximo deseado de %1$d.
615 No se puede establecer contacto con la interfaz Winsock. Error de Winsock: %1$d.
616 No se puede determinar el nombre de maquina utilizando interfaz Winsock. Error de Winsock: %1$d.
617 No se puede determinar el id de maquina utilizando interfaz Winsocket. Debe de haber alguna entrada en la tabla de nombres de host para esta maquina. Error de Winsock: %1$d.
618 Atributo no es actualizable.
619 Conjunto de codigo de origen mala.
620 Conjunto de codigo de destino malo.
621 Tipo de datos no valido. Los argumentos contienen un tipo de datos no valido.
622 Conjuntos de codigo incompatibles. Los conjuntos de codigo de los argumentos son incompatibles.
623 Sequencia de escape no valida. Una sequencia de escape no valida ha causado un error en el compilador de expresion regular.
624 Caracter de escape no valido. El caracter de escape es non-NULL con una longitud superior a 1.
625 Error interno. Tratando de actualizar la plantilla de informacion de atributo del objeto de instancia %1$d|%2$d|%3$d equivocada con el objeto de instancia %4$d|%5$d|%6$d.
626 Error interno. %1$d atributos solicitados no fueron encontrados.
627 Operando de trim no valido. El operando de trim tiene una longitud superior a 1.
628 Tipo de moneda no valido: %1$d.
629 Tamano de pagina %1$d no es una potencia de 2 o pagesize es demasiado pequeno. Pagesize de %2$d es utilizado en cambio.
630 Conversion no soportada por %1$s.
631 Declaracion SQL ha violado la restriccion NOT NULL.
632 %1$s no es un volumen de respaldo.
633 %1$s es un respaldo de la base de datos %2$s creada en %3$s en vez de la base de datos dada %4$s creada en %5$s
634 Un volumen/archivo de la base de datos era esperado en el respaldo.
635 ADVERTENCIA: La fuente de bufer de registro de %1$d buferes parece muy pequena para el entorno actual. \n Es posible que algunos buferes no sean liberados por el sistema debido a un ERROR DESCONOCIDO \n o que haya muchas transaccions concurrentes al mismo tiempo. La fuente de bufer de registro es expandada con otros %2$d buferes.
636 ADVERTENCIA Num_buffers %1$d es demasiado pequeno. Se asumio el valor minimo de %2$d.
637 Error de sistema: liberando un bufer de registro demasiado. Contador de arreglo establecido a cero.
638 ADVERTENCIA: Vaciando una carpeta de registros no actualizable pageid = %1$d
639 Error de sistema: Pagina anterior de sequencia de registros %1$d no es la pagina %2$d de flush retrasado.
640 Error de sistema: un savepoint no puede ser anadido cuando la transaccion no es activa (ejemplo dado: durante commit o rollback)
641 Un nombre tiene que ser dado para un savepoint.
642 Error de sistema: no hay una operacion del sistema superior activa para la transaccion actual.
643 Puede que haya un error de sistema: Cometiendo/Abortando transaccion = %1$d (indice = %2$d) la cual tiene operaciones permanentes \n adjuntas. Se adjuntaran esas operaciones de sistema a la transaccion
644 ERROR FATAL DE REGISTRO: %1$s
645 ERROR DE SISTEMA: Estado = %1$s de la Transaccion = %2$d (indice = %3$d) \n no es el estado correcto para aplicar acciones tipo postpone/undo cliente...ignorado
646 ERROR DE SISTEMA: Parece que un registro de compensacion falta para el revindex = %1$s\n de registro logico. Sin este registro de compensacion, la recuperacion despues del fallo no se hace correctamente
647 Respaldo "%1$s" no pertenece a la base de datos dada.
648 Respaldo es incompatible con "%1$s" actual, release "%2$s".
649 Precision dada de %1$d no valida; deberia ser superior a %2$d y no a %3$d.
650 No se pueden crear suficientes hilos para 'max_clients=%1$d'.
651 No hay memoria virtual para preasignar %1$d buferes de especie para cada uno de los %2$d hilos.
652 Offset equivocado %1$d fue dado para dividir el registro de longitud %2$d almacenado en el slot %3$d
653 Offset %1$d equivocado o longitud %2$d ha sido dada para eliminar una parte de los datos del registro de longitud %3$d almacenado en el slot %4$d
654 Offset equivocado %1$d o longitud %2$d ha sido dada para sobrescribir una parte de los datos del registro de longitud %3$d almacenado en el slot %4$d
655 No se puede abrir el archivo de configuracion del handler de eventos, "%1$s".
656 ADVERTENCIA: Nombre/id null en el archivo de configuracion del handler de eventos , "%1$s", linea %2$d
657 Advertencia: Nombre de evento ilegal, "%1$s", en el archivo de configuracion del handler de evento , "%2$s", linea %3$d
658 Advertencia: id %1$d del evento, no dentro del rango de id de errores %2$d <> %3$d, archivo "%4$s", linea %5$d
659 No se puede accesar el programa del handler de eventos, "%1$s".
660 No se puede abrir/conectar el proceso del handler de eventos, "%1$s".
661 Fracaso al escribir en el proceso del handler de eventos, "%1$s".
662 Fracaso al inicializar el mecanismo de control de eventos.
663 Advertencia: Linea demasiada larga en el archivo de configuracion del handler de eventos, "%1$s", linea %2$d, truncando
664 Mecanismo de control de eventos inicializado.
665 Tunel del handler de eventos roto, "%1$s"
666 Mecanismo de control de eventos detenido.
667 No se puede emparejar clase '%1$s' con la clase C++ correspondiente.
668 Error interno: Tipo de dominio desconocido '%1$d'.
669 Servidor ha rechasado conexion de cliente: numero maximo de clientes (%1$d), excedido.
670 Operacion habria causado una o mas violaciones de restriccion UNIQUE. INDICE %1$s%2$s EN CLASE %3$s%4$s. clave: %5$s%6$s.
671 Un error de la red tipo leer/escribir ha ocurrido para el socket %1$d asociado con su transaccion (indice %2$d, %3$s@%4$s|%5$d)
672 Socket %1$d asociado con su transaccion (indice %2$d, %3$s@%4$s|%5$d) esta cerrado.
673 Su conexion hacia el servidor ha sido cerrada ya que el servidor va a ser apagado inmediatamente.
674 No hay suficientes hilos activos para continuar la ejecucion del servidor actual. Numero actual de hilos activos es %1$d. Podria Ud necesitar un numero hasta %2$d hilos para una ejecucion limpia del servidor en su entorno. Indice de transaccion %3$d ha expirado para poder continuar la ejecucion del servidor.
675 No se puede leer el archivo de ubicacion de la base de datos, '%1$s'.
676 Base de datos '%1$s', no encontrada en el archivo de ubicacion de la base de datos '%2$s'.
677 Fracaso al conectar al servidor de base de datos, '%1$s', en la siguente host(s): %2$s
678 Error interno. Modulo de reinicializacion del cliente ha recibido nombre de servidor de base de datos NULL.
679 Esta transaccion C++ ya empezo.
680 Intentado exceder el nivel maximo de anidamiento de la transaccion C++ (%1$d).
681 %1$s transaccion ha sido intentada en una transaccion C++ que todavia no empieza.
682 No se puede %1$s la transaccion C++ hasta que todas las transacciones subordinadas sean terminadas.
683 Intentado crear string con longitud ilegal - %1$d.
684 Metodo regreso codigo de error definido por usuario %1$d.
685 Clase "%1$s" no es valida.
686 Notificacion: el handler de eventos ha sido reinicializado.
687 No permitido emitir (%1$s *) a (%2$s *).
688 Subindice fuera de rango. Intente accesar elemento %1$lld. Tamaño de la colección = %2$lld.
689 Error interno. Iterator contiene estado corrupto.
690 Intente desreferenciar una instancia no inicializada del odb_Ref<>.
691 Intente utilizar un interator no valido.
692 Intente utilizar un interator con una coleccion no relacionada.
693 Sobreflujo ocurrido sobre tipo de datos Numeric.
694 ERROR DE SISTEMA: Incapaz de cargar B+tree.
695 Interfaz equivocada ha sido utilizada para matar un cliente.
696 Transaccion desconocida (indice %1$d, %2$s@%3$s|%4$d).
697 Indice de transaccion dado (indice %1$d, %2$s@%3$s|%4$d) no empareja con el indice de la transaccion actual (indice %5$d, %6$s@%7$s|%8$d). Probablemente, la transaccion dada ha terminado y su indice de transaccion ha sido reasignado a otra transaccion.
698 Error interno: INDICE %1$s EN CLASE %2$s (CLASS_OID: %3$d|%4$d|%5$d). el numero de OIDs en la jeraquia unica: %6$d no es igual que el numero de OIDs: %7$d encontrado en el unico B+tree mas el numero de NULLs: %8$d encontrado en la jeraquia unica para B+tree: %9$d|%10$d|%11$d.
699 Error interno: INDICE %1$s EN CLASE %2$s (CLASS_OID: %3$d|%4$d|%5$d). el numero de OIDs en la jeraquia unica: %6$d no es igual que el numero de OIDs: %7$d encontrado en las estatisticas de origen unicas B+tree para B+tree: %8$d|%9$d|%10$d.
700 Error interno: INDICE %1$s SOBRE CLASE %2$s (CLASS_OID: %3$d|%4$d|%5$d). el numero de OIDs: %6$d no es igual que el numero de NULLs: %7$d mas el numero de claves: %8$d en el B+tree unico: %9$d|%10$d|%11$d.
701 Ningun proxy definido en "%1$s" en ldb "%2$s".
702 Error interno: INDICE %1$s EN CLASE %2$s (CLASS_OID: %3$d|%4$d|%5$d). el numero de NULLs en la jeraquia unica: %6$d no es igual que el numero de NULLs: %7$d encontrado en las estatisticas de origen unicas B+tree para B+tree: %8$d|%9$d|%10$d.
703 Error interno: INDICE %1$s EN CLASE %2$s (CLASS_OID: %3$d|%4$d|%5$d). OID: %6$d|%7$d|%8$d encontrado en el B+tree unico: %9$d|%10$d|%11$d no pertenece a alguna de las clases para la restriccion unique.
704 Evaluacion de la funcion de servidor generico fracaso.
705 %1$d volumenes permanentes inconsistentes han sido encontrados cuando %2$d se esperaban.
706 No se pueden expandar volumenes permanentes. Volumen permanente "%1$s" con proposito %2$s ha sido dado.
707 Incapaz de expandar volumen temporal "%1$s" con %2$d paginas
708 Incapaz de expandar volumen de disco temporal "%1$s" con %2$d paginas (%3$lld Kbytes) debido a la falta de espacio. Espacio disponible actual es %4$d pages (%5$lld Kbytes).
709 Restriccion "%1$s" no econtrada.
710 Restriccion no valida.
711 Un objeto con el valor de atributo suministrado no existe.
712 Restriccion "%1$s" ya existe.
713 Un indice no existe para este atributo.
714 Fallo de la consulta debido a espacio de archivos temporales insuficiente.
715 Fallo al verificar la esquema: %1$s
716 Restricciones NOT NULL no son permitidas en clases virtuales o proxy.
717 Restricciones UNIQUE no son permitidas en clases virtuales o proxy.
718 Artefacto no valido.
719 Los numeros de atributos (%1$d) y valores (%2$d) no son iguales.
720 No se puede utilizar un artefacto para insertar en un clase virtual o proxy.
721 Consulta Insert rinde mas que una fila.
722 Numero de atributos equivocado para la restriccion NOT NULL.
723 No se puede modificar el dominio de un atributo existente.
724 Parametro(s) no valido(s) para la generacion estandar de nombre de restriccion.
725 Error interno: esperado oid temporal y encontrado oid permanente.
726 Restriccion UNIQUE tiene que ser agregada utilizando una declaracion de tipo ALTER separada\n una vez que las instancias han sido creadas. 
727 Trigger de eventos no valido.
728 Consulta fracaso porque el archivo temporal vfid no es valido para la transaccion %1$d.
729 Sobreflujo ocurrio en contexto de sustracción.
730 Sobreflujo ocurrio en contexto de multiplicación.
731 Sobreflujo ocurrio en contexto de división.
732 Sobreflujo ocurrio en contexto de menos unario.
733 Sobreflujo ocurrio en contexto de coerción.
734 Error interno: Encabezamiento del archivo VFID %1$d|%2$d(Volume "%3$s") es corrupto.
735 Indice de transaccion desconocido %1$d.
736 Tiene que especificar servicio o identificador de puerto. Error de Winsock: %1$d.
737 Servicio desconocido para TCP: %1$s. Error de Winsock: %2$d.
738 No se puede crear corriente de socket. Error de Winsock: %1$d.
739 No se puede unir dirección local...reintentando. Error de Winsock: %1$d.
740 No se puede unir dirección local...abortando. Error de Winsock: %1$d.
741 Fracaso de Accept. Error de Winsock: %1$d.
742 Mandando un mensaje de difusión al cliente. Error de Winsock: %1$d.
743 Fracaso al establecer acuerdo entre cliente y servidor. (peer host %1$s)
744 Error al rehasheando un MOP de proxy/clase virtual.
745 Fase de colección de basura no valida.
746 Callback de colección de basura no valido.
747 Billete de callback de colección de basura no valido.
748 Puntero de objeto no valido encontrado por db_gc_mark_object.
749 Los números de instancias extraidas no emparejan: %1$d esperadas, %2$d recibidas.
750 Los números de instancias extraidas no emparejan: %1$d esperadas, %2$d recibidas.
751 El OID de un objeto nuevo no es disponible en un trigger BEFORE INSERT.
752 Error al restaurar el respaldo unit_num %1$d.
753 Error al restaurar el respaldo unit_num %1$d, pageid %2$d excede el numero total de paginas %3$d para volid %4$d.
754 La instancia proxy OO tiene clave null o tipo non-string.
755 %1$s no puede ser ejecutada en modo independiente.
756 El bufer de string especificado es demasiado pequeno para codificar este objeto.
757 Error interno: no se puede asignar OID permanente durante la codificacion de objeto.
758 String especificado no es valido para objeto de decodificacion.
759 El objeto de destino ha sido eliminado.
760 Error interno: instancia de objeto proxy tiene clave NULL durante la codificacion de objeto.
761 Error interno: instancia de objeto de clase virtual traza un mapa hacia una instancia real no valida.
762 Error interno al resolver instancia de clase virtual en una instancia real.
763 Error interno al codificar instancia de objeto no actualizable.
764 Error interno al codificar objeto en una forma interna vobj.
765 Argumento de objeto de salida tiene direccion NULL.
766 Error interno al decodificar objeto.
767 No se puede crear un nivel de respaldo %1$d sin crear primero un nivel de respaldo %2$d.
768 La base de datos local "%1$s" ha sido accesada en esta sesion, alteración o disminución no son permitidas.
769 Atributo "%1$s", de clase "%2$s" no es una clase valida o un atributo compartido.
770 Sin funciones de tabla.
771 Parametro no valido para funcion.
772 Clase db_serial no encontrada.
773 Serial "%1$s" no encontrado.
774 Serial "%1$s" ya existe.
775 Siguente valor excede el valor MAX(MIN).
776 No se puede extraer objeto serial.
777 No se puede actualizar objeto serial.
778 No se puede crear una vinculacion simbólica "%1$s" al archivo "%2$s".
779 Resultado excede limite: ano de la fecha DEBE estar entre 1 y 9999.
780 Fallo del sistema interno: incapaz de obtener la hora del sistema.
781 El formato del string es demasiado largo. Tiene que ser inferior a 16K.
782 String vacio no permitido aqui.
783 Formato no valido.
784 Dos argumentos son desajustados.
785 El primer string del argumento es demasiado largo. Tiene que ser inferior a 16K.
786 Formato es duplicado.
787 Error de conversion en formato tiempo.
788 Error de conversion en formato fecha y hora.
789 Numero equivocado.
790 Ejecucion de la consulta ha sido interrumpida por el usuario.
791 Valor serial no valido.
792 No se puede asignar memoria para css.
793 fallo de pthread_attr_init().
794 fallo de pthread_attr_destroy().
795 fallo de pthread_attr_setdetachstate().
796 fallo de pthread_attr_setscope().
797 fallo de pthread_attr_setstacksize().
798 fallo de pthread_create().
799 fallo de pthread_join().
800 fallo de pthread_mutex_init().
801 fallo de pthread_mutex_destroy().
802 fallo de pthread_mutex_lock().
803 fallo de pthread_mutex_trylock().
804 fallo de pthread_mutex_unlock().
805 fallo de pthread_mutexattr_init().
806 fallo de pthread_mutexattr_destroy().
807 fallo de pthread_mutexattr_settype().
808 fallo de pthread_mutexattr_gettype().
809 fallo de pthread_cond_init().
810 fallo de pthread_cond_destroy().
811 fallo de pthread_cond_wait().
812 fallo de pthread_cond_timedwait().
813 fallo de pthread_cond_signal().
814 fallo de pthread_cond_broadcast().
815 fallo de pthread_key_create().
816 fallo de pthread_key_delete().
817 fallo de pthread_setspecific().
818 fallo de pthread_getspecific().
819 fallo de pthread_once().
820 No se puede inicializar una conexion.
821 Cerrar una conexion.
822 Operacion mutex fracaso mientras obteniendo el id del siguente cliente.
823 Operacion mutex fracaso mientras inicializando la lista de conexion.
824 No se puede destrozar mutex mientras finalizando la lista de conexion.
825 Llamada pread() fracaso.
826 Llamada pwrite() fracaso.
827 Funcion traverse de la lista de conexion ha regresado valor no valido.
828 Sección crítica no valida del indice ha sido dada.
829 Este bloqueo mutex ha sido ya desbloqueado.
830 Ya no se puede asignar entrada de consulta. Numero maximo de entradas es %1$d.
831 Todos los descriptores (%1$d) de transaccion son bajo uso. Hay que aumentar los parametros max_clients.
832 Respaldo ya se esta ejecutando. Respaldo no puede ser ejecutado por dos procesos.
833 El indice de nombre "%1$s" es ambiguo. Especifique el nombre de clase del indice.
834 Hay algunos desajustes entre el string de origen "%1$s" y el string de formato "%2$s" dados en to_number().
835 Artículo de estadísticas indefinido.
836 Pestillo en la pagina (%1$d|%2$d) ha expirado
837 No se puede disminuir el usuario que patrocina objetos de base de datos (clase/trigger/serial etc)
838 No se pueden abrir simultaneamente mas de %1$d tomografías de indice.
839 "%1$s": parametro de sistema desconocido o valor malo.
840 No se puede cambiar el parametro de sistema "%1$s".
841 Operacion solicitada puede ser ejecutada por solo un cliente (transaccion).
842 No se puede almacenar el bloqueo del objeto %1$d|%2$d|%3$d.
843 Argumentos equivocados en %1$s, una %2$s ha sido adelantada.
844 Aislacion (%1$d) desconocida es encontrada. El indice de la transaccion es %2$d.
845 Tipo de objeto invalido (%1$d) es encontrado. El objeto bloqueable es %2$d|%3$d|%4$d.
846 %1$s de transaccion (%2$d) no es encontrado en la lista de titulares de un objeto (%3$d|%4$d|%5$d).
847 %1$s en un %2$s(%3$d|%4$d|%5$d) no es encontrado en la lista de asimiento (cnt=%6$d) de transaccion (%7$d).
848 %1$s en un objeto(%2$d|%3$d|%4$d) no es encontrado en la lista non2pl de transaccion (%5$d).
849 Intentando abortar una transaccion (%1$d) dos veces.
850 Transaccion (%1$d) no es ni un holder de bloqueo ni un waiter de bloqueo de un objeto (%2$d|%3$d|%4$d).
851 Fracaso al asignar recurso de bloqueo (%1$s).
852 El modo (%1$d) de todos los propietarios de una lista vacia de propietarios de lock no es NULL_LOCK.
853 Para un objeto (%1$d|%2$d|%3$d), transaccion (%4$d) de tipo Uncomitted Read, contenia un error de "lock" (%5$s).
854 ADVERTENCIA: Ningun holder de bloqueo existente, en cambio hay waiters de bloqueo. Referir a %1$s.
855 ADVERTENCIA: Dos o mas hilos estan en la espera de bloqueo para una transaccion (%1$d).
856 ADVERTENCIA: Estado (%1$d|%2$d) extrano de espera de bloqueo, hilo (%3$d|%4$d) y transaccion (%5$d).
857 Un error de E/S ha ocurrido mientras truncando pagina %1$d del volumen "%2$s".
858 Volumen "%1$s" es desconocido al reinicio del servidor.
859 Pestillo en la pagina (%1$d|%2$d) abortado.
860 No se puede hacer la transaccion actual como parte de una transaccion global porque su estado es '%1$s'.
861 No se puede preparar para cometer la transaccion actual sin identificador de transaccion %1$d porque esa transaccion no esta hecha como parte de una transaccion global.
862 No se puede establecer la información de usuario de la transacción global con el identificador %1$d porque su estado es '%2$s'.
863 No se puede obtener la informacion de usuario de la transaccion global con el identificador %1$d porque no esta ajustado.
864 No se puede abrir el archivo verbose '%1$s' de backupdb/restoredb.
865 No se puede ejecutar declaracion debido a marcadores de entrada de tipo desconocido.
866 Codigo de error no utilizado.
867 Codigo de error no utilizado.
868 Codigo de error no utilizado.
869 Codigo de error no utilizado.
870 Codigo de error no utilizado.
871 Codigo de error no utilizado.
872 Codigo de error no utilizado.
873 Error "%1$s" de autentificacion PKI.
874 Argumento de potencia () es fuera de rango.
875 Sobreflujo occurido en contexto de potencia.
876 Codigo de error no utilizado.
877 No hay datos para descargar.
878 Archivo de ubicacion de base de datos desconocido, '%1$s'.
879 No se puede obtener permision: '%1$s'.
880 No se puede cambiar permision '%1$s'.
881 No se puede obtener archivo de bloqueo '%1$s(FILE ID:%2$d)'.
882 No se puede liberar el archivo de bloqueo 'FILE ID:%1$d'.
883 Fracaso al obtener estado de archivo '%1$s'.
884 El nombre de ruta '%1$s' de tipo volext no tiene permiso para escribir.
885 Fracaso al mover de '%1$s' a '%2$s'. Utilizar el mismo dispositivo (disco).
886 "%1$s" ha causado violation unica de restriccion.
887 Procedimiento almacenado '%1$s' ya existe.
888 Contador de parametro no es valido. esperado: %1$d, efectivo: %2$d
889 Error de ejecucion de procedimiento almacenado: %1$s
890 Fallo de particion.
891 Partition adecuada no existe.
892 Clave primaria "%2$s" ya definida para la clase "%1$s".
893 Atributo '%1$s', un miembro de la clave primaria, no pudo ser disminuido.
894 Procedimiento/funcion almacenada '%1$s' no existe.
895 Tipo de procedimiento almacenado no valido: '%1$s' es el '%2$s'.
896 Fracaso al comprimir. Metodo Zip: %1$d (%2$s), Nivel Zip: %3$d (%4$s)
897 Descompresión fallo.
898 Error de replicacion: "%1$s"
899 Peticiones de particion no validas.
900 Biblioteca Java VM no econtrada: %1$s. \n Por consiguiente, llamar procedimiento java almacenado no es permitido.
901 Java VM no puede ser inicializada: %1$s. \n Por consiguiente, llamar procedimiento java almacenado no es permitido.
902 Java VM no se esta ejecutando.
903 No se puede conectar JAVA VM: %1$s
904 Nombre de procedimiento almacenado no valido.
905 Interconexion con JVM fallo: %1$d
906 Firma de metodo java no valido.
907 El patrocinador o un membro de un grupo DBA puede disminuir procedimiento almacenado.
908 Demasiados argumentos del procedimiento almacenado '%1$s'.
909 Catalogo clase/clase virtual faltante o no valido es encontrado.
910 Acceso a la particion no permitido.
911 Llamada no valida: no puede regresar ResultSet.
912 ResultSet no puede ser utilizado como parametro de entrada.
913 Demasiadas llamadas de procedimiento almacenadas anidadas.
914 Un objeto serial ya existe como una restriccion de incremento automatico.
915 Valor de aumento de una restriccion de incremento automatico deberia ser superior a 0.
916 Volumen de registro activo no es incluido en este respaldo de volumen (%1$s).
917 Volumen de registro activo ya existe.
918 La clase '%1$s' referida por la clave externa no existe.
919 La clase '%1$s' referida por la clave externa no tiene la clave primaria.
920 La clave externa '%1$s' no incluye el miembro de clave primaria '%2$s'.
921 El dominio del miembro de clave primaria '%1$s' es otro que aquel de la clave primaria '%2$s'.
922 La restriccion de la clave externa '%1$s' no es valida, debido al valor '%2$s'.
923 Se supone que la clave primaria '%1$s' referida por la clave externa '%2$s' no puede ser disminuida.
924 La operaciones Actualizar/Eliminar son restringidas por la clave externa '%1$s'.
925 Fracaso al adquirir el bloqueo para la clave externa '%1$s'.
926 La instancia teniendo la clave externa '%1$s' no puede ser disminuida.
927 El numero de claves de la clave externa '%1$s' es otro que aquel numero de la clave primaria '%2$s'.
928 No un usuario, pero el sistema puede asignar el atributo '%1$s' creado por 'ON CACHE OBJECT' de la clave externa.
929 Una clave externa no puede ser definida en una clase virtual.
930 Se puede disminuir el atributo '%1$s' creado por 'ON CACHE OBJECT' de una clave externa, solo despues de eliminar la restriccion de la clave externa.
931 Valor de inicio de una restriccion de incremento automatico deberia ser inferior que el valor maximo.
932 No se puede reparar una inconsecuencia del numero de paginas libres para el volumen "%1$s". Porque %2$d paginas obtenidas del encabezamiento del volumen son inconsistentes con %3$d paginas del mapa de bits.
933 No se puede reparar una inconsecuencia del numero de sectores libres para el volumen "%1$s". Porque %2$d sectores obtenidos del encabezamiento del volumen son inconsistentes con %3$d sectores del mapa de bits.
934 Dominio NULL referenciado.
935 Argumento de la funcion %1$s es fuera de rango.
936 Sobreflujo ocurrido en contexto exp.
937 Error de la base de datos.
938 Argumento no valid.
939 Hay demasiadas conexiones.
940 El handle no es valido.
941 La operacion no es soportada.
942 El handler ha expirado esperando a los bloqueos.
943 Error generico.
944 La declaracion no es preparada.
945 La declaracion no tiene ningun conjunto de resultados.
946 La declaracion no es ejecutada.
947 No hay mas conjunto de resultados.
948 El tamano del bufer no es suficiente.
949 No hay ninguna informacion disponible.
950 El nombre no valido.
951 El conjunto de resultados no es actualizable.
952 La fila es eliminada.
953 Parametros de la declaracion preparada no son ajustados.
954 La declaracion no es una declaracion batch.
955 No se puede eliminar esta declaracion batch.
956 No se puede ejecutar en modo batch.
957 La declaracion es declaracion preparada.
958 La declaracion no es declaracion preparada.
959 La declaracion es una declaracion batch.
960 Ya no hay errores.
961 No hay mas resultado.
962 Sin memoria.
963 Error del broker.
964 El handler de conjunto de resultados esta cerrado.
965 No se pueden utilizar nombres de atributo duplicados en la lista de claves de indice. Nombre de atributo '%1$s' listado mas de una vez.
966 Su transaccion (indice %1$d, %2$s@%3$s|%4$d) ha expirado esperando al bloqueo %5$s en objeto %6$d|%7$d|%8$d debido a deadlock. Esta Ud esperando que el usuario %9$s termine.
967 Su transaccion (indice %1$d, %2$s@%3$s|%4$d) ha expirado esperando al bloqueo %5$s en clase %6$s debido a deadlock. Esta Ud esperando que el usuario %7$s termine.
968 Su transaccion (indice %1$d, %2$s@%3$s|%4$d) ha expirado esperando al bloqueo %5$s en instancia %6$d|%7$d|%8$d de clase %9$s debido a deadlock. Esta Ud esperando que el usuario %10$s termine.
969 Plataforma de bits del servidor (%1$d) es diferente de la plataforma de bits del cliente (%2$d).
970 Modo HA del servidor cambiado de '%1$s' en '%2$s'.
971 Programa '%1$s' (pid %2$d) conectado al servidor de base de datos '%3$s' en el host '%4$s' (puerto %5$d).
972 Programa '%1$s' (pid %2$d) fue conectado del host '%3$s'. (indice de transaccion %4$d) 
973 Estado del servidor es %1$s.
974 Registro de carpeta "%1$s" es creado para archivar paginas de %2$lld a %3$lld.
975 Version actual de replicacion no permite cambiar filas multiples con una sola declaracion UPDATE que puede violar la restriccion UNIQUE.
976 Error interno: Sobreflujo del tamano de tabla (tamano asignado: %1$d, tamano accesado: %2$d) a la pagina %3$d|%4$d(volumen "%5$s") de la tabla de archivos. 
977 Punto de control iniciado (id de pagina del punto de control anterior: %1$lld, id de pagina de rehacer anterior : %2$lld).
978 Checkpoint finished (checkpoint page id: %1$lld, checkpoint redo page id: %2$lld, flushed page count: %3$d).
979 Bucle CONNECT BY en los datos de usuario.
980 Un hilo esta esperando en %1$s mas que %2$d msec.
981 Transacción %1$d ha llegado al umbral de estadísticas de supervisión %2$s.
982 Interrupcion de conjunto a la transaccion %1$d.
983 Instancias de una clase OID reutilizable son non-referibles. Esta operacion no esta permitida para instancias non-referibles.
984 No se puede ajustar %3$s en "%6$s" en la seccion "[%5$s]" cuando esta ajustado a "%4$s" en la seccion "[%2$s]" del archivo de configuracion "%1$s".
985 El nombre de host en el string de conexion de la base de datos tendria que ser especificado cuando multihost esta ajustado en "databases.txt".
986 Caracteristica heartbeat de CUBRID inicializada.
987 Caracteristica heartbeat de CUBRID terminada.
988 Evento de nodo: %1$s. 
989 Evento de proceso: %1$s. %2$s
990 Ejecucion de comando: %1$s. %2$s
991 La seleccion de objetos para los cuales ejecutar flushing del bufer de pagina ha sido inicializado.
992 La seleccion de objetos para los cuales ejecutar flushing del bufer de pagina ha sido terminado (contador: %1$d).
993 Sobrefujo ocurrido en contexto de operacion de bit.
994 Esta declaracion no puede ser una declaracion preparada.
995 Una declaracion preparada con el nombre %1$s no existe.
996 Numero de valores dado incorrecto. %1$d valores dados, pero %2$d valores son necesarios.
997 No se puede anadir restriccion de clave externa %1$s a la particionada %2$s. Acciones referenciales que actualizan atributos todavia no son soportados por clases particionadas.
998 No se puede anadir restriccion de clave externa %1$s al atributo %2$s. El atributo no puede tener una restriccion NOT NULL porque tiene que emparejar con la accion referencial SET NULL.
999 No se puede crear tabla '%1$s' como tabla '%2$s' porque la tabla existente tiene caracteristicas que no van a ser duplicadas (%3$s).
1000 '%1$s' es un atributo de clase o compartido pero solo atributos normales son permitidos en este contexto.
1001 Una condicion inesperada o estado ha sido alcanzado. '%1$s'
1002 La transaccion actual no puede adquirir un bloqueo de base de datos. Esta solicitada para continuar el procedimiento.
1003 La restriccion '%1$s' existe pero tiene un otro tipo.
1004 Por favor note que cuando utilizando ALTER TABLE DROP FOREIGN KEY, CUBRID disminua las dos la restriccion y su indice asociado.
1005 Codigo de error no utilizado.
1006 Codigo de error no utilizado.
1007 No se puede crear indice con longitud de prefijo en restricciones unicas, de clave externa o de clase primaria.
1008 No se puede crear indice con longitud de prefijo en clase particionada.
1009 COMPACTDB ya iniciada.
1010 No se puede definir indice con longitud de prefijo en dominio "%1$s".
1011 No se puede anadir restriccion de clave externa %1$s en el atributo compartido %2$s.
1012 No se puede crear tabla con tipo de columna NULL.
1013 No esta permitido ajustar DEFAULT o SHARED en la %1$s columna escrita.
1014 No esta permitido ajustar NOT NULL en la %1$s columna escrita.
1015 Directorio especificado '%1$s' no existe.
1016 %1$s Error de almacenamiento externo: %2$s
1017 Ruta para almacenamiento externo '%1$s' no es valido.
1018 No se puede copiar el archivo externo a un tipo diferente de almacenamiento. (origen %1$s destino %2$s)
1019 Almacenamiento externo no es incializado porque la ruta no es especificada en "databases.txt".
1020 Archivo externo "%1$s" no fue encontrado.
1021 Un ciclo de deadlock es detectado. %1$s.
1022 Valor "%1$d" no valido para la longitud de prefijo del indice.
1023 Error de codigo reservado para HA.
1024 log writer: failed to get log page(s) starting from page id %1$lld.
1025 HA delay info: The replication delay (%1$d secs) has exceeded the limit (%2$d secs).
1026 HA delay info: The replication delay (%1$d secs) is decreased below the acceptable level (%2$d secs).
1027 Aplicador de registro: fracaso al cambiar el estado de aplicar de '%1$s' a '%2$s'.
1028 Aplicador de registro: registro EOF inesperado en la carpeta de registros. LSA: %1$lld|%2$d.
1029 Aplicador de registro: pagina/offset de registro de replicacion no valida. pagina HDR: %1$1lld|%2$d, final: %3$lld|%4$d,, anexar LSA: %5$lld|%6$d, EOF LSA: %7$lld|%8$d, estado de archivo HA: %9$d, es fin-del-registro: %10$d. 
1030 Aplicador de registro: registro de replicacion no valido. LSA: %1$lld|%2$d, forw LSA: %3$lld|%4$d, backw LSA: %5$lld|%6$d, Trid: %7$d, prev tran LSA: %8$lld|%9$d, type: %10$d.
1031 Aplicador de registro: fracaso al aplicar registro de replicacion de esquema. clase: "%1$s", esquema: "%2$s", error del servidor: %3$d, %4$s.  
1032 Aplicador de registro: fracaso al aplicar registro de replicacion insert. clase: "%1$s", clave: "%2$s", error del servidor: %3$d.  
1033 Aplicador de registro: fracaso al aplicar registro de replicacion de actualizacion.clase: "%1$s", clave: "%2$s", error del servidor: %3$d.  
1034 Aplicador de registro: fracaso al aplicar registro de replicacion de eliminar. clase: "%1$s", esquema: "%2$s", error del servidor: %3$d.  
1035 Aplicador de registro: tamano de memoria (%1$d MB) del aplicador de registro es superior al tamano maximo de memoria (%2$d MB) o ha sido aumentado mas de 2 veces (%3$d MB). LSA requerido : %4$lld|%5$d. ultimo LSA cometido: %6$lld|%7$d.
1036 Aplicador de registro: aplicador de registro se ha apagado por senal.
1037 Escritor de registro: escritor de registro se ha apagado por senal.
1038 Aplicador de registro: aplicador de registro inicializado. LSA requerido: %1$lld|%2$d. ultimo LSA cometido: %3$lld|%4$d. ultimo repl LSA cometido: %5$lld|%6$d. 
1039 Escritor de registro: escritor de registro inicializado. modo: %1$d. 
1040 HA generico: %1$s. 
1041 Un string ha sido truncado en contexto de "%1$s".
1042 Tratando de crear un string requierendo %1$d bytes de memoria, mientras el maximo permitido es %2$d bytes.
1043 No se puede cambiar atributo "%1$s". CUBRID no puede cambiar las propiedades solicitadas del atributo si la clase tiene subclases.
1044 No se puede cambiar atributo "%1$s". CUBRID no puede cambiar atributos que son claves de particionamiento.
1045 No se puede cambiar atributo "%1$s". CUBRID no puede cambiar atributos utilizados en claves externas (sean referenciados o referenciando).
1046 No se puede cambiar atributo "%1$s". CUBRID no puede cambiar el dominio del atributo. Este cambio de dominio no es soportado.
1047 No se puede cambiar atributo "%1$s". CUBRID no puede cambiar el dominio del atributo. El nuevo dominio puede que no represente correctamente todos los valores existentes del atributo.
1048 No se puede cambiar atributo "%1$s". CUBRID no puede actualmente cambiar el AUTO_INCREMENT del atributo si el dominio del atributo tambien cambia.
1049 No se puede cambiar atributo "%1$s". CUBRID no puede actualmente cambiar el dominio del atributo. La conversion de dominio es posible, pero esta caracteristica no es permitida por configuracion.
1050 No se puede cambiar atributo "%1$s". CUBRID no puede actualmente cambiar el dominio del atributo. El atributo tiene adjuntado un indice.
1051 No se puede cambiar atributo "%1$s". CUBRID no puede actualmente cambiar el atributo que agregue una restriccion. Valores existentes del atributo pueden no checar la restriccion.
1052 Atributo "%1$s" no ha sido cambiado. CUBRID no ha detectado ningun cambio efectivo en la representacion del atributo.
1053 Las declaraciones REPLACE INTO... e INSERT... ON DUPLICATE KEY UPDATE no son permitidas para clases que son parte de una cadena de herencia.
1054 El nuevo valor de una restriccion de incremento automatico tendria que ser superior o igual al valor actual.
1055 El nuevo valor de una restriccion de incremento automatico tendria que ser inferior al valor maximo.
1056 Para evitar ambigüedad, la opcion de la tabla AUTO_INCREMENT requiere que la tabla tenga exactamente una columna AUTO_INCREMENT y ninguna especificacion seed/increment.
1057 No se puede anadir un atributo NOT NULL '%1$s' sin un valor estandar.
1058 TABLA ALTER..CAMBIA: cambiando a nuevo dominio: fracaso al lanzar, configuracion actual no permite truncamiento o sobreflujo.
1059 TABLA ALTER..CAMBIA: cambiando a nuevo dominio: fracaso al lanzar, valor estandar del nuevo dominio ha sido ajustado.
1060 TABLA ALTER..CAMBIA: cambiando a nuevo dominio: fracaso al lanzar, valor minimo del dominio nuevo ha sido ajustado.
1061 TABLA ALTER..CAMBIA: cambiando a nuevo dominio: fracaso al lanzar, valor maximo del dominio nuevo ha sido ajustado.
1062 No se puede cambiar atributo "%1$s". El cambio resultaria en la definicion de claves primarias multiples.
1063 No se puede anadir restriccion NOT NULL para atributo "%1$s": hay valores NULL existentes para este atributo.
1064 TABLA ALTER..CAMBIA: agregando NOT NULL: todos los valores NULL existentes han sido cambiados al estandar duro del dominio.
1065 No se puede cambiar atributo "%1$s". CUBRID no puede agregar restriccion NOT NULL: un valor estandar duro no existe para el nuevo dominio.
1066 Sesion ha expirado.
1067 Columna '%1$s' no puede tener una restriccion NOT NULL y un valor estandar NULL.
1068 Profundidad maxima del trigger no puede excceder %1$d.
1069 Demasiadas declaraciones preparadas.
1070 Variable de sesion '@%1$s' no definida.
1071 Demasiadas variables de sesion.
1072 Direccion IP (%1$s) no es autorizada.
1073 Formato no valido en el archivo (%1$s).
1074 No se puede leer archivo de lista de acceso (%1$s).
1075 Tomografia de indice descendente abortada debido a prioridad inferior en B+tree con identificador de indice: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
1076 No se puede cargar parametro de sistema.
1077 Parametro '%1$s' en linea %2$d en archivo '%3$s' : Parametro desconocido.
1078 Caracter en offset %1$d no valido con el conjunto de codigo actual.
1079 Conjuntos de codigo de la base de datos (%1$s) y la configuracion del servidor (%2$s) no son compatibles.
1080 Longitud maxima del string de predicado del filtro tiene que ser %1$d.
1081 No se puede alterar un atributo involucrado en un indice de filtro o de funcion.
1082 Todos buferes de paginas son sucios (LRU: %1$d).
1083 Informacion especifica sobre el deadlock.\n%1$s
1084 Saltando pagina no valida en punto de control. (page id: %1$d, "%2$s", oldest_unflush_lsa: %3$lld|%4$d, lsa anterior de rehacer punto de control: %5$lld|%6$d)
1085 No se puede compilar expresion regular: '%1$s'
1086 No se puede ejecutar expresion regular: '%1$s'
1087 Registro activo de respaldo '%1$s' inicializado.
1088 Registro activo de respaldo '%1$s' terminado.
1089 Asignacion de recursos fallada. Aumentar 'max_bestspace_entries=%1$d'.
1090 Inicializacion local: %1$s.
1091 Generacion local: %1$s.
1092 No se puede preparar declaracion de tipo '%1$s' con variables de host.
1093 El argumento '%1$s' para inet_aton no es una direccion ipv4 de formato string valida.
1094 El argumento '%1$lld' para inet_ntoa no es una direccion ipv4 de formato numeral valida.
1095 Nombre de utilizador es demasiado largo.
1096 Alterando esquema de particionamiento cuando la clase particionada es referenciada por una clave externa no es permitido.
1097 Columna '%1$s' no puede tener una restriccion PRIMARY KEY y un valor estandar NULL.
1098 Locales para el idioma '%1$s' no son disponibles con el conjunto de caracteres '%2$s'.
1099 Declaracion DDL no es permitida por configuracion (block_ddl_statement=yes).\n SQL Texto: %1$s
1100 Declaracion sin clausula WHERE no es permitida por configuracion (block_nowhere_statement=yes).
1101 Codigo de error no utilizado.
1102 Parametro de sistema desconocido o valor malo.
1103 No se puede cambiar el parametro de sistema.
1104 Consulta lenta (%1$d mseg)\n%2$s
1105 Error interno: parcialmente no se pudo limpiar objeto sucio.
1106 Varias filas de la tabla fuente coinciden con la misma fila en la tabla de destino.
1107 Numero de bucket no valido para NTILE. Deberia ser mayor que 0 y menor que 2, 147, 483, 647.
1108 Numero de bucket no valido para WIDTH_BUCKET. Deberia ser mayor que 0 y menor que 2, 147, 483, 647.
1109 Valor para definicion de particion no valido.
1110 Espacio insuficiente en pagina btree leaf. (key len: %1$d)\n%2$s
1111 Valor del resultado es truncado, porque la precision (%1$d) es menor que su longitud (%2$d).
1112 Un caracter no pudo ser convertido de '%1$s' en '%2$s'.
1113 Indice de elemento fuera de rango.
1114 Argumento '%1$s' es fuera de rango.
1115 Empezado actualizar estadisticas (clase "%1$s", oid : %2$d|%3$d|%4$d).
1116 Terminado actualizar estadisticas (clase "%1$s", oid : %2$d|%3$d|%4$d, codigo error : %5$d).
1117 No se puede modificar atributo "%1$s". CUBRID no puede modificar un atributo como SHARED y viceversa.
1118 El argumento de "%1$s" no puede ser coercido al dominio deseado "%2$s".
1119 Error interno: Id %1$d de pagina es asignado conforme con el mapa de asignacion del volumen "%2$s", pero no pertenece a ningun archivo.
1120 Error interno: Id %1$d de pagina del volumen "%2$s" esta actualmente utilizado, pero no es asignado conforme con el mapa de asignacion del volumen.
1121 Restricciones NOT NULL no permiten valor NULL.
1122 Tiempo de espera excedido en la recepcion de los datos.
1123 pthread_cond_timedwait() ha excedido el tiempo de espera.
1124 Error de ejecucion de consulta. ERROR_CODE = %1$d, /* SQL_ID: %2$s */ %3$s
1125 Crear archivo clave de sobreflujo.INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1126 Crear una nueva pagina de sobreflujo.INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1127 Borrar una pagina de sobreflujo vacia. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1128 La recuperacion de anotaciones se inicia. El numero de registros de anotacion que se aplicara: %1$lld. Log page: %2$lld ~ %3$lld.
1129 La recuperacion de anotaciones ha terminado.
1130 Transaccion es abortada porque el numero de llaves asociado a la transaccion excede %1$d.
1131 Error interno: La pagina %1$d del volumen "%2$s" puede ser corrota. \n%3$s
1132 Encryption library failure: %1$s
1133 Copylog for %1$s ya existe.
1134 Applylogdb para %1$s ya existe.
1135 Un enlace anterior de B+tree(%1$s) es corrompido: vpid = (%2$d, %3$d)
1136 Un enlace anterior de B+tree(%1$s) es reparado: vpid = (%2$d, %3$d)
1137 La pregunta ha sido rechasada por haber intentado exceder el nivel maximo de anidamiento permitido (%1$d).
1138 Error de apreton de manos (peer host %1$s): interrumpibilidad incompatible. (cliente: %2$s, servidor: %3$s)
1139 Error de apreton de manos (peer host %1$s): modo leer/escribir incompatible. (cliente: %2$s, servidor: %3$s)
1140 Error de apreton de manos (peer host %1$s): replicacion de HA retrasada.
1141 Error de apreton de manos (peer host %1$s): cliente replica-only a servidor non-replica.
1142 Error de apreton de manos (peer host %1$s): version de servidor no identificada.
1143 Error de apreton de manos (peer host %1$s): acceso remoto al servidor no permitido.
1144 Tiempo de expera excedido al intento de conectar a %1$s. (tiempo de espera: %2$d seg(s))
1145 No es permitido cambiar el dueno de una clase de sistema.
1146 Identificador de volumen %1$d no existe.
1147 El tamano de la corriente XASL de cache (%1$d) excede el maximo (%2$d). Aunque no hay victimas, nueva entrada es anadida porque todas las entradas de cache estan utilizadas.
1148 El tamano del predicado de filtro de la corriente de cache (%1$d) excede el maximo (%2$d). Aunque no hay victimas, nueva entrada es anadida porque todas las entradas de cache estan utilizadas.
1149 No se puede encontrar la pagina %1$d de volumen %2$d.
1150 Contexto requiere colaciones compatibles.
1151 The page %1$d of volume %2$d is not a slotted page.
1152 The owner or a member of DBA group is only allowed to kill the transaction (%1$d).
1153 Attempt to use 'zero date'.
1154 MVCC row %1$d|%2$d|%3$d does not satisfies reevaluation.
1155 MVCC invalid row for delete: %1$d|%2$d|%3$d.
1156 MVCC can't get snapshot
1157 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.
1158 Serializable conflict due to concurent updates
1159 Ultimo error

$set 6 MSGCAT_SET_INTERNAL
1 Error en subsistema de error (linea %1$d):
2 Ningun mensaje de error disponible.
3 No se pudo asignar %1$d bytes.
4 No se puede encontrar catalogo de mensaje "%1$s".
5 Faltando mensaje para codigo de error %1$d.
6 ...ABORTAR/SALIR INMEDIATAMENTE ...\n
7 Quiere salir? 1/0
8 Archivo desconocido.
9 Ningun mensaje disponible; este mensaje sustituido por haber formateado erróneamente el original.
10 er_init: *** Valor exit_ask incorrecto = %1$d; en vez, se asumera %2$s. ***\n
11 er_start: *** ADVERTENCIA: Incapaz de abrir registro de mensaje "%1$s"; en vez, se asumera stderr. ***\n
12 er_study_fmt: mensaje sospechoso para codigo de error %1$d.
13 er_estimate_size: codigo de conversion desconocido (%1$d).
14 \n\n*** Registro de mensaje envolvente. Mensajes van a continuar en la parte superior del archivo. ***\n\n
15 \nTime: %1$s - %2$s *** %3$s CODE = %4$d, Tran = %5$d%6$s\n%7$s\n
16 CUBRID (pid: %1$d) *** %2$s *** %3$s CODE = %4$d, Tran = %5$d %6$s
17 \nTime: %1$s - %2$s *** file %3$s, line %4$d %5$s CODE = %6$d Tran = %7$d%8$s\n%9$s\n
18 CUBRID (pid: %1$d) *** %2$s *** file %3$s, line %4$d, %5$s CODE = %6$d, Tran = %7$d. %8$s
19 \n*** El mensaje de error anterior es el ultimo. ***\n\n
20 \nTime: %1$s - DEBUG *** archivo %2$s, linea %3$d\n
21 Correo -s "Servidor ha sido apagado"
22 --->>>\n %1$s ha sido apagado a peticion suya cuando el siguente error ha sido ajustado: \n errid = %2$d, %3$s\nUser: %4$s, pid: %5$d, host: %6$s, time: %7$s<<<---\n
23 %1$s ha sido appagado en errid = %2$d. Utilizador: %3$s, pid: %4$d
24 er_init: no se puede instalar handler de evento "%1$s"\n

$set 7 MSGCAT_SET_PARSER_SYNTAX
1 Por favor verifique sintaxis a '%1$s', esperando %2$s.
2 Por favor verifique sintaxis a '%1$s', esperando %2$s en %3$s.
3 Por favor verifique correctitud del token '%1$s'.
4 'leer esquema no cometida" no es acceptable, utilizando \n'leer esquema cometida' en vez.
5 Longitud maxima del string de caracteres es 16383.
6 %1$s excede limite de %2$lld.
7 Longitud maxima del string de bits es %1$d bits.
8 No se puede utilizar una subconsulta en la clausula condición de unión.
9 No se puede especificar atributo "(%1$s)" en la clausula actualiza privilegio de la declaracion grant/revoke.
10 Uso no valido de la palabra clave '%1$s', esperando %2$s.
11 Longitud del token es demasiado larga.
12 Tipo de datos no valido.
13 Declaracion de adjunto no valida\n attach_statement: ATTACH unsigned_integer
14 Declaracion prepara no valida\n prepare_statement:  PREPARE {to} COMMIT unsigned_integer
15 Declaracion ejecuta no valida\n execute_statement:  EXECUTE DEFERRED TRIGGER [identifier {, identifier}* | ALL TRIGGERS]
16 Declaracion de proposito no valida\n scope_statement: SCOPE trigger_action { FROM table_spec_list }
17 Parametro de ajuste de sistema no valido\n set_system: SET SYSTEM PARAMETERS 'STRING' {, 'STRING_N'}*
18 Ajusta transaccion no valido\n set_transaction: SET TRANSACTION ISOLATION LEVEL expression {, expression}\n                 | SET TRANSACTION LOCK TIMEOUT timeout_spec
19 Ajusta rastro del trigger no valido\n set_trigger: SET TRIGGER TRACE [ON|OFF|unsigned_integer|parameter|host_param]
20 Ajusta profundidad del trigger no valido\n set_trigger: SET TRIGGER {MAXIMUM} DEPTH [unsigned_integer|parameter|host_param]
21 Ajusta nivel de optimizacion no valido\n SET OPTIMIZATION LEVEL {=|to} [ON|OFF|unsigned_integer|parameter|host_param]
22 Ajusta costo de optimizacion no valido\n  SET OPTIMIZATION COST {of} 'STRING' {=|to} literal_
23 Obtiene nivel de optimizacion no valido\n GET OPTIMIZATION LEVEL {[INTO|TO] [host_param|parameter|identifier]}
24 Obtiene costo de optimizacion no valido\n GET OPTIMIZATION COST {of} 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
25 Obtiene aislamiento de transaccion no valido\n GET TRANSACTION ISOL LEVEL {[INTO|TO] [host_param|parameter|identifier]}
26 Obtiene el bloqueo de la transaccion no valido\n GET TRANSACTION LOCK TIMEOUT {[INTO|TO] [host_param|parameter|identifier]}
27 Obtiene rastro del trigger no valido\n GET TRIGGER TRACE {[INTO|TO] [host_param|parameter|identifier]}
28 Obtiene profundidad del trigger no valido\n GET TRIGGER {MAXIMUM} DEPTH {[INTO|TO] [host_param|parameter|identifier]}
29 Obtiene estadisticas no valido \n GET STATISTICS 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
30 Crear indice no valido \n CREATE {REVERSE} {UNIQUE} INDEX identifier ON {only} class_name index_column_name_list {WHERE where_clause}
31 Crear utilizador no valido\n CREATE USER identifier {PASSWORD 'STRING'} {GROUPS identifier {, identifier}* } {MEMBERS identifier {, identifier}* }
32 Crear trigger no valido\n CREATE TRIGGER identifier {STATE [ACTIVE|INACTIVE]} {PRIORITY unsigned_real}\n    [BEFORE|AFTER|DEFERRED] event_spec {IF trigger_condition}\n    EXECUTE {AFTER|DEFERRED} trigger_action
33 Crear serial no valido\n CREATE SERIAL identifier {START WITH integer} {INCREMENT BY integer}\n    serial_min_max {CYCLE|NOCYCLE} {CACHE unsigned_integer|NOCACHE}
34 Crear procedimiento no valido\n CREATE PROCEDURE identifier '(' {sp_param_list} ')' {IS|AS} LANGUAGE JAVA NAME 'STRING'
35 Crear funcion no valido\n CREATE FUNCTION identifier '(' {sp_param_list} ')' RETURN {data_type|CURSOR} {IS|AS} LANGUAGE JAVA NAME 'STRING'
36 Grupos no validos\n groups spec: GROUPS identifier {, identifier}*
37 Miembros no validos\n member spec: MEMBERS identifier {, identifier}*
38 Contraseña no valida\n password spec: PASSWORD 'STRING'
39 Faltando lista de comandos autorizados\n authorized command = SELECT, INSERT, INDEX, DELETE, UPDATE, ALTER, ADD, DROP, EXECUTE, REFERENCES, ALL PRIVILEGES, ALL
40 Faltando lista de especificaciones de clase
41 Faltando lista de identificador
42 Faltando expresion
43 Identificador no valido
44 Uso de substring no valido\n SUBSTRING( expression FROM expression FOR expression )\n  SUBSTRING( expression FROM expression )
45 Uso de recorte no valido\n TRIM ( [LEADING|TRAILING|BOTH] expression FROM expression )\n  TRIM ( [LEADING|TRAILING|BOTH] FROM expression )\n  TRIM ( expression FROM expression )\n  TRIM ( expression )
46 Uso de emitir no valido\n CAST ( expression AS data_type )
47 Funcion add_months necesita dos parametros
48 octet_length necesita un parametro
49 bit_length necesita un parametro
50 lower necesita un parametro
51 upper necesita un parametro
52 translate necesita tres parametros
53 replace necesita dos o tres parametros
54 Uso no valido del to_number\n TO_NUMBER ( expression , char_string )\n  TO_NUMBER ( expression )
55 Operador de union del string es '||' y no '|'
56 Declaracion alterar no valida
57 Declaracion disminuir no valida
58 Declaracion crear no valida
59 Operador relacional no valido
60 Operador igual es '=' y no '=='
61 Operador no igual es '<>' y no '!='
62 Valor negativo no valido (valor negativo en lista)
63 Regla de referencia duplicada.
64 SYS_CONNECT_BY_PATH necesita dos parametros y el segundo necesita ser un literal de string.
65 Entero no asignado no valido %1$s.
66 IF necesita tres parametros: el primero tiene que ser una expresion logica, el segundo y el tercero tienen que ser expresiones evaluando sea a un tipo numerico o a uno de string.
67 IFNULL necesita dos parametros cuales tienen que ser expresiones evaluando a sea un tipo numerico o a uno de string.
68 Uso de isnull no valido\n ISNULL ( expression )
69 Uso de left no valido\n LEFT ( str , len )
70 Uso de right no valido\n RIGHT ( str , len )
71 Uso de mod no valido\n MOD( expression, expression )
72 Uso de adddate no valido\n ADDDATE( expression, expression )
73 Uso de date_add no valido\n DATE_ADD( expression, INTERVAL expression unit)
74 Uso de subdate no valido \n SUBDATE( expression, expression )
75 Uso de date_sub no valido\n DATE_SUB( expression, INTERVAL expression unit)
76 Uso de utilizador no valido / system_user\n USER() / SYSTEM_USER()
77 Uso de timestamp no valido\n TIMESTAMP( expression ), TIMESTAMP( expression, expression )
78 Uso de base de datos no valido\n DATABASE()
79 Uso de current_time no valido\n CURRENT_TIME()
80 Uso de current_date no valido\n CURRENT_DATE()
81 Uso de current_timestamp no valido\n CURRENT_TIMESTAMP()
82 Uso de now invalido\n NOW()
83 Uso de default no valido\n DEFAULT(column_name)
84 Uso de date no valido\n DATE(expression)
85 Uso de localtime local no valido\n LOCALTIME()
86 Uso de localtimestamp local no valido\n LOCALTIMESTAMP()
87 Uso de esquema no valido\n SCHEMA()
88 Uso de truncate no valido\n TRUNCATE( expression, expression )
89 Uso de str_to_date no valido\n STR_TO_DATE( expression, string_literal )
90 Uso de current_date_time no valido\n CURRENT_DATETIME()
91 Uso de insert no valido\n INSERT( expression, expression , expression, expression )
92 Uso de elt no valido\n ELT(expression [, expression]...)
93 Uso de year no valido\n YEAR( expression )
94 Uso de month no valido\n MONTH( expression )
95 Uso de day no valido\n DAY( expression )
96 Uso de hour no valido\n HOUR( expression )
97 Uso de minute no valido\n MINUTE( expression )
98 Uso de second no valido\n SECOND( expression )
99 Uso de quarter no valido\n QUARTER( expression )
100 Uso de week no valido\n WEEK( expression ), WEEK( expression, expression)
101 Uso de time no valido\n TIME(expression)
102 Uso de group_concat no valido\n GROUP_CONCAT (([DISTINCT] {col | expr} \n [ORDER BY {col | unsigned_int} [ASC | DESC] ] \n [SEPARATOR str_val])
103 Uso de indicacion de indice no valido.
104 Longitud maxima del string de bytes es %1$d bytes.
105 Error de sintaxis: unexpected
106 , expecting '{'
107 , expecting SELECT or '('
108 , expecting
109 unexpected
110 END OF STATEMENT
111 In line %1$d, column %2$d before '%3$c%4$s'\n
112 In line %1$d, column %2$d before %3$s\n
113 before ' %1$s'\n
114 before END OF STATEMENT\n
115 Ajusta nombres invalido\n set_names: SET NAMES charset [COLLATE collation]\n
116 Uso de chr no valido\n CHR ( number [using charset] )\n
117 Uso de charset no valido\n CHARSET ( expression )\n
118 Uso de collation no valido\n COLLATION ( expression )\n
119 Uso de clob_to_char no valido\n CLOB_TO_CHAR ( expression [using charset] )\n
120 Uso de index_prefix no valido\n INDEX_PREFIX ( expression, expression, expression )\n
121 Uso incorrecto de UNION y ORDER BY
122 Expecting ON. Outer Joins require join condition.

$set 8 MSGCAT_SET_PARSER_SEMANTIC
1 Sin memoria.
2 Funcion %1$s es indefinida.
3 No se puede encontrar ldb '%1$s' en la base de datos.
4 El numero de columnas, %1$d, en la consulta izquierda no empareja\nel numero de columnas, %2$d, el la consulta derecha.
5 '%1$s' no es compatible de union con '%2$s'.
6 %1$s no es definido.
7 El dominio de %1$s no puede ser una vclass.
8 El dominio de %1$s no puede ser una proxy vclass.
9 El dominio de %1$s no puede ser una clase.
10 %1$s no es un atributo de %2$s.
11 %1$s no puede ser cambiado de esta clase.\nHa sido heredado de %2$s.
12 Clase %1$s no existe.
13 %1$s no es un %2$s.
14 %1$s no es un metodo o atributo de %2$s.
15 %1$s no tiene y no debe tener ninguna especificacion de consulta.
16 %1$s no es definido en la base de datos.
17 %1$s no es una superclase de %2$s.
18 Clase %1$s toma herencia de la non-clase %2$s.
19 VClass %1$s toma herencia de la clase non virtual %2$s.
20 Proxy vclass %1$s toma herencia de la clase virtual non-proxy %2$s.
21 %1$s ya es una superclase de %2$s.
22 %1$s no puede ser una superclase de si misma.
23 %1$s ya es una subclase de %2$s.
24 %1$s tiene mas que un solo valor. Atributos exponidos en\nconsultas de agregacion tienen que aparecer tambien en el grupo por clausula.
25 El numero de columnas (%1$d) y into_names(%2$d) no son iguales.
26 Clase %1$s ya existe.
27 Superclase %1$s en '%2$s' no existe.
28 Esperando una clausula SUBCLASS que haga la clausula '%1$s' tipo INHERIT util.
29 La clausula '%1$s' tipo INHERIT referencia una clase que no esta en la clausula SUBCLASS '%2$s'.
30 Clase no puede tener una especificacion de consulta.
31 %1$s no es una ldb registrada.
32 Un heredero proxy no puede tener su propia clausula object_id. No puede anular su(s) abtributo(s) object_id heredado(s).
33 La vclass proxy tiene mas de una especificacion de consulta.
34 El numero de atributos, %1$d, no empareja con el numero de columnas, \n%2$d, en la especificacion de consulta.
35 Faltando un nombre de atributo esperado para %1$s.
36 Atributo '%1$s' no es compatible con la columna '%2$s' de la especificacion de consulta.
37 %1$s no es una clase.
38 Indices no pueden ser creados en clases virtuales.
39 Indices solamente pueden ser creados en un atributo unico.
40 Uso ilegal de clases multiples en la clausula from.
41 Error de sistema -- Fracaso de resolucion en %1$s.
42 Usted no puede eliminar de una tabla derivada.
43 Utilizador %1$s no esta en la base de datos.
44 Actualizar no puede tener agregacion.
45 Nombres de atributo en cada clase tienen que ser unicos. Nombre '%1$s' de atributo en clase '%2$s' es especificado mas de una vez.
46 Hay asignaciones multiples a %1$s.%2$s.
47 Numero en especificacion de ordenacion no es un entero.
48 %1$d en especificacion de ordenacion es fuera de rango.
49 Ordenar por tema no es un entero.
50 %1$s en especificacion de ordenacion no empareja ninguna columna select.
51 Hay instrucciones contradictorias de clasificación para la columna %1$s.
52 %1$s en la especificacion de ordenacion no es un numero, nombre o expresia de ruta.
53 Expresion Insert pude aparecer solamente en declaracion Insert.
54 El numero de atributos (%1$d) y columnas select (%2$d) no son iguales.
55 El numero de atributos (%1$d) y valores (%2$d) no son iguales.
56 Sobreflujo de datos en tipo de datos %1$s.
57 Tipo de datos incompatible en atributo %1$s.
58 %1$s\n no es un SELECT de una clase individual.
59 `*' no es una lista de seleccion valida en una especificacion de consulta de proxy vclass.
60 %1$s no es ni una vclass ni una proxy vclass.
61 Hay mas de una clausula de nivel de aislacion.
62 Hay mas de una clausula timeout.
63 Ningun nivel de aislacion metido.
64 El operador BETWEEN no es definido en tipos %1$s, %2$s y %3$s.
65 Operador '%1$s' no es definido en tipos %2$s y %3$s.
66 Funcion debe tener argumentos: %1$s.
67 Operandos incompatibles: %1$s(%2$s).
68 Funcion agregada debe tener 1 argumento: %1$s.
69 Intente dividir por cero.
70 Sobreflujo de datos obligando %1$s al tipo %2$s.
71 No se puede obligar %1$s al tipo %2$s.
72 El tipo de '%1$s' tiene que ser SET, MULTISET, o SEQUENCE.
73 '%1$s' deberia de tener exactamente un tipo de elemento de conjunto.
74 %1$s tiene mas que un nombre de columna derivado.
75 %1$s no tiene ningun nombre de columna derivado.
76 %1$s no tiene un numero de columnas (%2$d) y nombres de columnas (%3$d) correspondiente.
77 No hay atributos en %1$s.
78 Esperando tabla independiente en %1$s.
79 Nombre expuesto %1$s es ambiguo.
80 Nombres de columna derivados '%1$s' no son permitidos para especificaciones de clase simple.
81 Tabla derivada '%1$s' debe tener un nombre de correlacion.
82 Autorizacion de Select fallada, clase %1$s.
83 Referencia %1$s de instancia de clase a un nombre de tabla derivado no es permitido.
84 Correlacion (%1$s) de ruta aparece fuera de un plazo de ruta interna.
85 Referencia a %1$s es ambigua.
86 Clase de %1$s no tiene atributo %2$s.
87 No todas clases (%1$s) tienen al atributo %2$s.
88 Clase (%1$s) no tiene atributo %2$s.
89 %1$s es un tipo %2$s, no un tipo de objeto.
90 Selector {%1$s} no resuelve a una especificacion de clase expuesta en\n el contexto requierido (%2$s).
91 Selector {%1$s} refiere a una especificacion de non-clase (%2$s).
92 Selector {%1$s} no puede arrancar de raíz la expresión de ruta de definirse a sí misma.
93 Selector {%1$s} es definido circularmente en terminos de selector {%2$s}.
94 Selector {%1$s} es redefinido a %2$s.
95 %1$s (objeto de dominio) no tiene atributo %2$s.
96 %1$s en la especificacion EXCEPT no es una subclase.
97 Lista ALL... EXCEPT excluye EVERYTHING en clase.
98 '%1$s' es inconsistente con\notro uso del selector de ruta '%2$s' o del origen de ruta '%3$s'.
99 Argumento indefinido.
100 Argumento no es un objeto.
101 %1$s no es un metodo de clase o atributo de %2$s.
102 Especificacion de consulta no debe tener una clausula into.
103 Especificacion de consulta del proxy no debe de tener ninguna expresion agregada,\n ningun group-by-clausula, y no having-clausula.
104 Especificacion de consulta del proxy no debe de tener ni DISTINCT ni UNIQUE\npalabras claves en la lista select.
105 Especificacion de consulta del proxy tiene que ser una declaracion SELECT simple.
106 Especificacion de consulta del proxy debe referenciar solo una tabla o clase\n de la base de datos local.
107 Utilizador '%1$s' no es el patrocinador de %2$s.
108 Proxy '%1$s' es ya definido en clase '%2$s' de ldb '%3$s'.
109 CUBRID no soporta proxy con objetos en conjuntos heterogéneos.
110 Clausula-where tiene que ser una expresion de tipo logico.
111 La clausula like escape tiene que ser un string literal o una variable de host.
112 Combinacion de precision (%1$d) y escala (%2$d) no valida.
113 Precision dada de %1$d no es valida; tendria que ser superior a %2$d y inferior a %3$d.
114 Precision (%1$d) demasiado larga. Precision maxima es %2$d.
115 Metodo %1$s debe de tener un tipo regreso.
116 Numero de argumentos suministrados al metodo no es igual que el\nnumero definido para el metodo.
117 Llamada del metodo debe de tener un destino.
118 Metodo %1$s es indefinido para el destino suministrado.
119 Metodos de instancia no pueden ser llamados en objetos de clase.
120 Metodos de instancia no pueden ser llamados en objetos de instancia.
121 Metodos de clase requieren un objeto de clase como destino.
122 Metodos necesitan un objeto como destino.
123 Metodo %1$s no es completamente escrito, verifique definicion del metodo en la esquema de clase.
124 Bajoflujo de aritmetica de tiempo.
125 Bajoflujo de aritmetica de datos.
126 Lado izquierdo ilegal de una clausula de asignacion.
127 Llamadas de metodo en tablas derivadas ajustadas, correladas con un ambito externo no son\n soportadas a menos que la expresion ajustada sea envuelta en una subconsulta.
128 La columna de especificacion de consulta que corresponde al\n atributo compardito '%1$s' tiene que ser NULL o NA.
129 Una columna NULL no es permitida en la lista select de la especificacion de ver consulta si la definicion del atributo es omitida.
130 Objetos vclass en conjuntos no son soportados.
131 Esperando un valor de string.
132 Esperando una declaracion o un valor de string.
133 Atributo object_id '%1$s' de proxy no debe mapar a\n una expresion de columna de especificacion de consulta.
134 %1$s restriccion ignorada.
135 Declaraciones de restriccion no permitidas en este conexto.
136 Emitir %1$s al tipo %2$s no es soportado.
137 %1$s no es un string de bits valido.
138 Operador '%1$s' no es definido en el tipo %2$s.
139 Operador '%1$s' no es definido en tipos %2$s con %3$s y %4$s.
140 Operandos del operador '%1$s' no pueden ser los dos variables de host dinamicas.
141 Demasiados argumentos a la funcion generica %1$s.\nSolo %2$d argumentos son permitidos para funciones genericas.
142 Selector {%1$s} clase '%2$s' no es una subclase de '%3$s'.
143 No se puede extraer campo '%1$s' del tipo %2$s.
144 Especificacion de consulta no debe tener una clausula ORDER BY.
145 Condicion de caso tiene que ser una expresion de tipo logico.
146 Expresion tendria que ser de tipo %1$s.
147 Serial '%1$s' ya existe.
148 Serial '%1$s' no definido.
149 Valor de incremento no debe ser cero.
150 Valor maximo excede 1.0e37.
151 Valor minimo no es valido.
152 Valor de incremento es invalido.
153 Valor minimo excede -1.0e36.
154 Valor maximo no es valido.
155 Especificar la opcion de alterar serial.
156 El segundo argumento de la funcion serial_next_value deberia ser superior a 0.
157 Valores seriales no pueden exceder su minimo o maximo.
158 Error interno de I/O para serial.
159 No se puede encontrar indice '%1$s'.
160 El nombre '%1$s' de índice calificado refiere a una clase, pero no es especificado en la clausula.
161 Dos o mas clases tienen el mismo indice '%1$s'. Utilizar el nombre de indice calificado.
162 Uso de %1$s no valido.\n%2$s expresion no puede ser utilizada con atributo o subconsulta correlada.
163 Release actual restringe una expresion conteniendo ORDERBY_NUM() en una lista select.
164 Expresion de ruta '%1$s' en condicion JOIN.
165 '%1$s' en condicion de JOIN no es definido.
166 Version actual no permite tablas derivadas en RIGHT OUTER JOIN.
167 No se puede insertar con consulta en otra ldb.
168 Solo una columna puede ser especificada en la lista select cuando la subconsulta no es introducida con EXISTS.
169 Lado derecho ilegal de una clausula de asignamento. El contexto requiere que la consulta sea de columna unica.
170 No se puede utilizar una subconsulta en la clausula de condicion de JOIN.
171 Definicion de particion no valida.
172 Columna de particion debe ser especificada.
173 Solo una columna puede ser especificada como columna de particion.
174 Tipo de columna de particion no soportado.
175 Definicion de particion es duplicada. '%1$s'
176 Desajuste del tipo de constante. '%1$s' 
177 Tamano de particion no valido.
178 '%1$s' no es clase particionada.
179 '%1$s' ya es clase particionada.
180 Solicitudes de particion no validas.
181 %1$s no puede ser cambiado de esta clase (columna de clave de particion).
182 %1$s no puede ser administrada por particion bajo disparadores activos.
183 Particion %1$s no existe.
184 Tipo de particion desajusta. '%1$s'
185 Informacion de particion no valida.
186 Adicion de particion debe ajustar con un rango superior del existente.
187 Reorganizacion de particiones no es permitida porque puede causar perdidas de datos.
188 El rango de particion '%1$s' no es valido.
189 Un atributo de clase no puede tener una restriccion de incremento automatico.
190 Un atributo de clase virtual no puede tener una restriccion de incremento automatico.
191 El dominio del atributo '%1$s' teniendo una restriccion de incremento automatico no es valido.
192 No un utilizador pero el sistema puede asignar el atributo '%1$s' creado por 'ON CACHE OBJECT' de una clave externa.
193 No se pueden utilizar nombres de atributo duplicados en lista de claves de indice. Nombre '%1$s' de atributo listado mas de una vez.
194 Funcion %1$s es indefinida o dada un numero equivocado de parametros.
195 '%1$s' puede solamente ser utilizado en consultas jerárquicas. Specifique una clausula CONNECT BY.
196 '%1$s' no es permitido aqui.
197 Utilizar CONNECT_BY_ISCYCLE es solamente permitido si la consulta tiene una clausula CONNECT BY NOCYCLE.
198 Ninguna tabla utilizada.
199 ORDER BY NULL requiere GROUP BY.
200 El numero para CACHE debe ser inferior a un ciclo (%1$s).
201 '%1$s' no es permitido en una consulta con clausula LIMIT.
202 WITH ROLLUP requiere GROUP BY.
203 Un parametro de nombre de columna deberia ser pasado a la funcion DEFAULT. Expresiones de ruta no son todavia soportadas.
204 Opcion '%1$s' de tabla duplicada.
205 La clase '%1$s' es marcada como REUSE_OID y es non-referible. Clases non-referibles no pueden ser el dominio de un atributo y los OIDs de sus instancias no pueden ser returnados.
206 Opcion 'serial %1$s' duplicada.
207 La columna '%1$s' en la clausula de ordenacion es ambigua.
208 Atributo '%1$s' no tiene un valor estandar.
209 Articulos ORDER BY deben aparecer en la lista select si SELECT DISTINCT es especificada.
210 Variable de host no es permitida en columnas en la especificacion de consulta.
211 %1$s no puede ser una columna tipo ORDER BY
212 %1$s no puede ser una columna tipo GROUP BY
213 No se puede definir '%1$s de %2$s' como tipo de columna.
214 Valor "%1$s" no valido para longitud de prefijo del indice.
215 Funcion '%1$s' no es definida en tipos %2$s, %3$s, %4$s y %5$s.
216 Funcion '%1$s' no es definida en tipos %2$s, %3$s y %4$s.
217 Funcion '%1$s' no es definida en tipos %2$s y %3$s.
218 Funcion '%1$s' no es definida en tipo %2$s.
219 Funcion '%1$s' no es definida para un argumento de indice de tipo %2$s.
220 ALTER VIEW '%1$s' AS SELECT no es permitido para vistas que tienen subclases o superclases. Utilize ALTER VIEW CHANGE QUERY en vez.
221 Ver atributos no puede ser cambiada utilizando esta declaracion. En vez, 'ALTER VIEW name AS query' podria ayudar.
222 CUBRID requiere articulo ORDER BY para ser la misma expresion que el argumento GROUP_CONCAT o un entero (posicion de argumento).
223 El predicado LIKE requiere que un caracter de escapa sea especificado. Ver la documentacion para los parametros de configuracion '%1$s' y '%2$s'.
224 KEYLIMIT no puede ser utilizado en clausula INDEX NONE.
225 Variable de sesion no permitida en este contexto.
226 Conflicto de indicio de indice entre '%1$s' y '%2$s'.
227 Funcion '%1$s' no puede ser utilizada en clausula DEFAULT.
228 Actualizando tablas derivadas no es permitido.
229 Actualizar atributos de clase y atributos de instancia no permitido en la misma declaracion.
230 Actualizando jerquias de clase virtual no es permitido.
231 Variables de host no permitidas en declaracion DDL.
232 Expresion (%1$s) de filtro no valida para indice.
233 Funciones multiples o anidadas no son permitidas en la definicion de indice de funcion.
234 Especificacion de indice no valida.
235 SHARED, DEFAULT y AUTO_INCREMENT no pueden ser definidos uno con el otro.
236 Demasiados valores (%1$d) de enumeracion. Un tipo de enumeracion puede tener un maximo de %2$d valores.
237 Valores duplicados en tipo enum.
238 No se puede MERGE dentro de una tabla derivada.
239 No se puede afectar la tabla de origen en una declaracion MERGE.
240 MERGE con jerquias de clase no es permitido.
241 Nombre de utilizador es demasiado largo.
242 Expresion constante no puede ser utilizada como indice de funcion.
243 Nombre de clase o alias no es unico en lista: '%1$s' 
244 Clausula DEFAULT no valida. '%1$s' no puede ser utilizada en una expresion anidada.
245 '%1$s' requiere argumentos con colaciones compatibles.
246 Conjunto de caracteres no valido.
247 Conjunto de caracteres y colacion requieridas no son compatibles.
248 Colacion '%1$s' desconocida.
249 Contexto requiere coleccion con elementos teniendo conjuntos de codigo y colaciones identicas.
250 Indice de prefijo no es permitido en atributo '%1$s' (tiene colaciones con expansiones).
251 Valor inicial no es valido.
252 Indice de prefijo multi-columna no es permitido.
253 Funcion '%1$s' no puede ser utilizada para indice basado en funcion.
254 Definicion de columna de indice no valida: %1$s
255 '%1$s' no es permitido en una expresion de filtro para indice.
256 No se pueden utilizar GROUPBY_NUM() y clausula WITH ROLLUP en la misma consulta.
257 No se puede MERGE UPDATE columna %1$s referenciada en clausula ON.
258 El numero de columnas, %1$d, en ( %2$s ) no empareja\n el numero de columnas, %3$d, en ( %4$s ).
259 Prefijos de columnas no permitidos en expresion de indice de funcion.
260 %1$s no puede ser una columna PARTITION BY o ORDER BY
261 Consulta debe evaluar hacia un solo valor \n(%1$s).
262 Funciones analiticas anidadas no son permitidas.
263 Un atributo heredado no puede ser utilizado como clave de particion.
264 El tamaño de los agregados de elementos de la enumeración es demasiado grande.
265 No se puede utilizar tabla de destino en especificaciones de origen.
266 COLLATE no es permitido en este contexto.
267 El modificador de COLLATE no puede cambiar conjunto de codigo (de '%1$s' en '%2$s'). En cambio, intentar utilizar CAST.
268 Cojunto de codigo '%1$s' no valido para el valor de particion. Esperando conjunto de codigo '%2$s'.
269 Variable de sesion no puede tener una colacion diferente que la colacion del sistema ('%1$s').
270 Expresion de indice de funcion no valida.
271 La especificacion de consulta de %1$s tiene una referencia ciclica a si misma.
272 %1$s expresion no permitida en la condicion join.
273 ORDER BY no es permitida en la funcion MEDIAN.
274 Los argumentos de la funcion '%1$s' tienen que ser constantes.
275 Funcion '%1$s' deben de tener clausula ORDER BY.
276 Para funcion '%1$s', el numero de argumentos no empareja el numero de columnas ORDER BY.
277 Para funcion '%1$s', la clausula ORDER BY no es valida.
278 Only one table within a view can be updated.
279 Multiple insert targets not allowed.
280 '%1$s' type is vclass, drop vclass with cascade constraints is not allowed.
281 The FOR UPDATE clause cannot be used in subqueries, unions, with GROUP BY clause, DISTINCT operator, aggregate functions.
282 Expects %1$s in argument position %2$d.
283 Too many arguments specified.
284 Too few arguments, need %1$d arguments.
285 Expects %1$s for argument %2$s.
286 Argument %1$s is missing.
287 Unknown argument %1$s.
288 Cannot remove all partitions, use DROP TABLE instead.
289 The expression including %1$s is not allowed in this context.
290 Transaction id is not an integer.
291 Subquery is not allowed in ORDER BY FOR clause.

$set 9 MSGCAT_SET_PARSER_RUNTIME
1 Sin memoria virtual: incapaz de asignar %1$d bytes.
2 Nivel de aislacion de transaccion debe ser 1, 2, 3, 4, 5 o 6.
3 Valor del tiempo de expiracion debe ser -1, 0, o >0.
4 Nivel de aislacion ajustado a:\n
5          Unknown isolation.\n
6          Unknown isolation.\n
7          Unknown isolation.\n
8          READ COMMITTED\n
9          REPEATABLE READ\n
10 Una consulta no actualizable no es permitida en una llamada db_query_execute_oid.
11 Transaccion ha expiro esperando bloqueos.
12 Intentado accesar variable de host con indice %1$d cuando solamente %2$d \nvariables de host de entrada fueron dadas.
13 Conversacion no implementada.
14 Error interno, conversion no definida.
15 '%1$s' no produce resultados.
16 '%1$s' produce mas de una fila.
17 Variable %1$s de interpretador no es definida.
18 No se puede evaluar '%1$s'.
19 Variable desconocida %1$s.
20 Atributo compartido desconocido %1$s.
21 '%1$s' produce mas de una columna.
22 Uso no valido o anidado de funcion agregada.
23 Funcion %1$s no es definida.
24 Contexto requiere que la consulta tenga una solo columna\n(%1$s).
25 Demasiados valores han sido especificados.
26 Destino de asignacion virtual (%1$s) no es invertible.
27 Hay mas columnas en la especificacion de consulta que atributos en clase %1$s.
28 Hay mas atributos en clase %1$s que columnas en la especificacion de consulta.
29 Version actual restringe agregacion a las vclasses WITHOUT\n especificaciones de consulta agregadas.
30 Version actual restringe agregacion a vclases WITHOUT\n especificaciones de consulta de union.
31 Uso de vclass (%1$s) en una jeraquia de composicion requiere que sea actualizable. \n No puede ser actualizable con respeto a la clase real %2$s.
32 No utilizado
33 Construct ilegal en expresion de ruta '%1$s'.
34 Expresion de ruta '%1$s' cruza limite de db a %2$s, con vclass %3$s.
35 Expresion de ruta '%1$s' cruza limite db de %2$s, a clase %3$s.
36 %1$s no es autorizado en %2$s.
37 Select no es autorizado en %1$s.
38 Vclass %1$s no es actualizable.
39 %1$s no es una vclass de tipo proxy.
40 Especificacion de consulta de la proxy vclass no debe ser una de tipo join.
41 %1$s no tiene conjunto de identificacion de objeto.
42 Clases (%1$s) con campos object_id multiples en una composicion \n de jerquia no son soportadas.
43 Atributo de objeto %1$s de %2$s no es una vclass en 1db %3$s.
44 Tipo de especificacion de consulta no es compatible con atributo %1$s.
45 La especificacion de consulta de %1$s tiene una referencia ciclica a si misma.
46 Ninguna expresion dada para evaluar.
47 Ninguna clase real encontrada para vclass %1$s.
48 Ejecutando '%1$s' en ldb '%2$s' \nreturnado: %3$s.
49 Poner valor '%1$s' en atributo '%2$s' regreso: %3$s.
50 Ldb debe de no ser null en %1$s.
51 Nombre de Ldb al host debe de no ser null en %1$s.
53 Nombre de host debe de no ser null en %1$s.
54 Nombre de utilizador debe de no ser null en %1$s.
55 Contrasena de utilizador debe de no ser null en %1$s.
56 Insert traduce a declaracion vacia.
57 Update traduce a declaracion vacia.
58 Delete traduce a declaracion vacia.
59 INGRES no soporta fechas (%1$s) antes de Enero 1, 1582.
60 Recursos agotados en generacion de consulta.
61 Consulta no produce resultados, asi que variable '%1$s' no es ajustada.
62 Con opcion de verificar LOCAL no es soportada.
63 Excepcion de opcion de verificar en vista %1$s.
64 Fecha no valida: '%1$s'.
65 Tiempo no valido: '%1$s'.
66 utime  no valido: '%1$s'.
67 Subconsultas en la especificacion de consulta del proxy no son soportadas.
68 Argumento de funcion Ldb no es compatible con ldb en '%1$s'.
69 Numerico: '%1$s' no valido.
70 Fracaso de optimizacion de consulta de union externa.
71         SERIALIZABLE.\n
72 Vclass %1$s no es actualizable. Solo vclasses actualizables son permitidas para selectar VIDs.
73 Particionar ha fracasado.
74 Particion no existe.
75 Acceso a la particion no permitido: '%1$s'.
76 El serial '%1$s' no es todavia definido.
77 El patrocinador o DBA puede solamente alterar la definicion de un serial de destino.
78 Los utilizadores no son permitidos modificar el objeto serial creado por una restriccion de incremento automatico.
79 Nivel de aislamiento no valido: %1$s esquema, %2$s instancias.
80 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.


$set 11 MSGCAT_SET_AUTHORIZATION
1 *** Cache no valida. ***
2 clase %1$s
3 de %1$s :
4 Autorizacion para utilizador %1$s\n
5 Autorizacion para utilizador no definido\n
6 Utilizador : %1$s\n
7            id : %1$d\n
8       miembros : 
9        grupos :
10 Utilizador %1$s\n
11 Utilizador actual es : %1$s\n
12 Objeto de autorizacion de raiz :\n
13   utilizadores :
14 *** No se pudieron emitir todas las subvenciones para el usuario "%1$s"*** \n
15 AUTHORIZATIONS\n
16 direct_groups :

$set 12 MSGCAT_SET_HELP
1 Rootclass
2 %1$s %2$s\n
3 Super clases :\n
4 Sub clases : \n
5 Atributos: \n
6 Metodos: \n
7 Atributos de clase: \n
8 Metodos de clase: \n
9 Resoluciones de conflicto:\n
10 Archivos de metodo:\n
11 Especificacion de consulta:\n
12 OBJECT de %1$s\n
13 \nDescripcion:\n
14 \nEstructura:\n
15 \nEjemplo:\n
16 META CLASS Nombre
17 Clase Nombre
18 VClass Nombre
$  No borre el espacio al final del siguente mensaje.
19 VClass en ldb
20 GENERAL

$set 13 MSGCAT_SET_TRIGGER
1 TRACE: Evaluando condicion para trigger '%1$s'.
2 TRACE: Ejecutando accion para trigger '%1$s'.

$set 14 MSGCAT_SET_LOCK
1 \n
2 Hilo (indice %1$3d) de transaccion (indice %2$3d, %3$s, %4$s@%5$s|%6$d) ha sido suspendido.\n
3 Hilo (indice %1$3d) de transaccion (indice %2$3d, %3$s, %4$s@%5$s|%6$d) ha sido reanudado.\n
4 Indice %1$3d de transaccion ha adquirrido bloqueo %2$s en objeto %3$d|%4$d|%5$d.\n
5 Indice %1$3d de transaccion ha adquirrido bloqueo %2$s en pagina %3$d|%4$d.\n
6 Indice %1$3d de transaccion ha convertido su bloqueo en bloqueo %2$s en objeto %3$d|%4$d|%5$d.\n
7 Indice %1$3d de transaccion ha convertido su bloqueo en bloqueo %2$s en pagina %3$d|%4$d.\n
8 Indice de transaccion %1$3d espera bloqueo %2$s en objeto %3$d|%4$d|%5$d.\n
9 Indice de transaccion %1$3d espera bloqueo %2$s en pagina %3$d|%4$d.\n
10 Indice de transaccion %1$3d libera bloqueo %2$s en objeto %3$d|%4$d|%5$d.\n
11 Indice de transaccion %1$3d libera bloqueo %2$s en pagina %3$d|%4$d.\n
12 Indice de transaccion %1$3d degrada bloqueo a %2$s en objeto %3$d|%4$d|%5$d.\n
13 Indice de transaccion %1$3d degrada bloqueo a %2$s en pagina %3$d|%4$d.\n
14 OID = %1$2d|%2$6d|%3$4d\n
15 Tipo de objeto: Clase de origen. \n
16 Tipo de objeto: Clase = %1$s.\n
17 Tipo de objeto: Instancia de clase (%1$2d|%2$6d|%3$4d) = %4$s.\n
18 Tipo de objeto: Desconocido. \n
19 Modo total de tenedores = %1$8s, modo total de los que esperan = %2$8s.\n
20 Numero de tenedores=%1$3d, Numero de tenedores bloqueados=%2$3d, Numero de los que esperan=%3$3d\n
21 Tenedores de bloqueo:\n
22 Tenedores de bloqueo bloqueados: \n
23 Esperantes de bloqueo:\n
24 NON_2PL_RELEASED:\n
25 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d\n
26 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d, Nsubgranules = %5$2d\n
27 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d\n\t%5$13s Blocked_mode = %6$8s\n\t%7$17s Start_waiting_at = %8$-30s\n\t%9$17s Wait_for_secs = %10$.2f\n
28 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d, Nsubgranules = %5$2d\n\t%6$13s Blocked_mode =%7$8s\n\t%8$13s Start_waiting_at = %9$-30s\n\t%10$13s Wait_for_secs = %11$.2f\n
29 %1$4sTran_index = %2$3d, Blocked_mode =%3$8s \n\t%4$13s Start_waiting_at = %5$-30s \n\t%6$13s Wait_for_secs = %7$.2f\n
30 %1$4sTran_index = %2$3d, Non_2_phase_lock =%3$8s\n
31 VPID (volid|pageid) = %1$3d|%2$6d.\n
32 *** vertedero de tabla de bloqueo ***\n Escalacion de bloqueo a = %1$d, intervalo de ejecutar deadlock = %2$5.2f\n
33 Transaccion (index %1$2d, %2$s, %3$s@%4$s|%5$d)\n
34 Aislamiento %1$s\n
35 Estado %1$s\n
36 Periodo de expiracion %1$s\n
37 Transacciones abortadas unilateralmente con indices:\n
38 %1$d ,
39 Transacciones expiradas con idices:\n
40 %1$d ,
41 Resolviendo el deadlock selectando una transaccion y una funcion \n para romper el deadlock: %1$d %2$s 
42 , %1$d %2$s
43 Tipo de objeto: Clave de indice de clase (%1$2d|%2$6d|%3$4d) = %4$s.\n
44 Nombre de indice: %1$s\n

$set 15 MSGCAT_SET_IO
1 \n*************************************************************************\n
2 Respaldo = %1$s es necesario. \n Tiene que ser montado/cargado para continuar.\n Escriba 1 para continuar despues de que el respaldo sea montado/cargado. \n Escriba 0 para abandonar. \n
3 \n\n*** Informacion de encabezamiento de respaldo ***\n\n
4 Identificacion de archivo magico de respaldo %1$s, \n
5          Release: %1$s\n\
     Disk Version: %2$g\n
6 Nombre de base de datos: %1$s\n\
		 Tiempo de creacion de base de datos: %2$s\n\
		Pagesize: %3$d\n	 
7     Backup Level: %1$d (%2$s)\n\
        Start_lsa: %3$lld|%4$d\n\
         Last_lsa: %5$lld|%6$d\n
8     Tiempo de respaldo: %1$s\
	Numero de unidad de respaldo: %2$d\n
9  Nombre de volumen de base de datos: %1$s\n\
   Identificador de volumen: %2$d, Tamano: %3$lld bytes (%4$d paginas)\n
10 Nombre de base de datos: %1$s\n\
		Nombre de volumen: %2$s\n\
		Numero de unidad: %3$d\n\
		Nivel de respaldo: %4$d (%5$s)\n\
		Introducir una de las siguentes opciones:\n
11 Escribir\n\
  - %1$d para abandonar.\n\
  - %2$d para continuar despues de que el volumen sea montado/cargado. (reintentar) \n \
  - %3$d para continuar despues de cambiar el directorio del volumen o el dispositivo.\n
12 Introducir nuevo directorio o dispositivo para el volumen de respaldo.\n
13 Error en entrada, escoger un valor entre %1$d t %2$d.\n
14 ** ADVERTENCIA ** Volumen de respaldo incorrecto montado.\n
15 Volumen de respaldo %1$s es de un otro nivel de respaldo\n (Nivel: %2$d, esperando %3$d) . \n\n
16 Volumen de respaldo %1$s tiene pagina incorrecta o identificador magico.\n\n
17 Volumen de respaldo %1$s es de otra base de datos\n (Nombre: %2$s, esperando %3$s). \n\n
18 Volumen de respaldo %1$s fuera de sequencia\n (Numero de unidad: %2$d, esperando %3$d). \n\n
19 Desajuste de sello de tiempo del volumen %1$s de respaldo\n (Sello de tiempo: %2$s, esperando %3$s). \n\n
20 Error de archivo de informacion del volumen de respaldo, linea %1$d.\n
21 Etiqueta de volumen de respaldo: Nivel: %1$d, Unidad: %2$d, Base de datos %3$s, Tiempo de respaldo: %4$s\n
22     Nivel de respaldo anterior: %1$d tiempo: %2$s (start_lsa was %3$lld|%4$d)\n
23 El siguente volumen de respaldo de la base de datos es necesario para continuar restablecer:\n\n
24 Respaldo de destino esta lleno, un nuevo destino es requierido para continuar:\n\n
25 Nombre de volumen de respaldo precedente: %1$s\n
26     Siguente nombre de volumen de respaldo: %1$s\n
27  Tamano de pagina de respaldo: %1$d\n
28       Zip Method: %1$d (%2$s)\n\
        Zip Level: %3$d (%4$s)\n
29 Incluir Registro Activo: %1$s\n

$set 16 MSGCAT_SET_LOG
1 \n*************************************************************************\n
2 Carpeta de registro %1$s \n es necesaria para continuar la ejecucion normal. \n\
  Escriba \n \
  - 0 para abandonar.\n\
  - 1 para cotinuar sin la carpeta presente. (Recuperacion parcial) \n\
  - 2 para continuar despues de que la carpeta sea montada/cargada. \n\
  - 3 para continuar despues de cambiar la ubicacion/nombre del archivo. \n
3 Archivo de informacion de respaldo %1$s \n es necesario para continuar el proceso de recuperacion. \n\
  Escriba\n \
  - 0 para abandonar.\n \
  - 1 para continuar despues de que el archivo sea montado/cargado. \n\
  - 2 para continuar despues de cambiar ubicacion/nombre del archivo \n
4 Introducir nueva ubicacion/nombre.\n
5 COMENTARIO: %1$s para base de datos %2$s\n
6 COMENTARIO: Carpeta de registro %1$s no es mas necesario a menos que ocurre una caida de la media de la base de datos.\n
7 COMENTARIO: Carpetas de registro de %1$s a %2$s no son necesarias mas, a menos que ocurre una caida de la media de la base de datos.
8 COMENTARIO: de la base de datos renombrada = %1$s\n
9 CARPETA: %1$d %2$s %3$lld %4$lld\n
10 CARPETA:
11  ELIMINA: %1$d %2$s to \n \
	   %3$d %4$s.\n \
	   RAZON: %5$s\n   
12 ACTIVO: %1$s %2$d paginas\n
13  Terminado cometer de tran_indes %1$d con tranid = %2$d,\n justo antes LSA= %3$lld|%4$d y a %5$s
14  Terminado abortar tran_index %1$d con tranid = %2$d,\n justo antes de LSA= %3$lld|%4$d y a %5$s
15  Recuperacion de media incompleta es en efecto. La base de datos va a ser \n \
 recuperada hasta la direccion de punto LSA de %1$lld|%2$d. Ultimo tiempo de cometer/abortar \n \
 tiempo de punto es aproximadamente a %3$s. \n \
 La base de datos ha perdido toda la informacion de las transacciones cometidas \n\
 despues de este tiempo.\n
16 Reinicializacion del registro debida a recuperacion incompleta.
17 Respaldo de base de datos ha sido tomado.
18 Caida de media no es importante para la aplicacion.
19 Regresando al directorio %1$s cual parece tener un respaldo anterior en el. \n\
 Volumenes de respaldo obsoletos de esta base de datos en esta locacion van a ser eliminados, \n\
 incluyendo los del nivel de respaldo actual y superior.\n
20 Introducir 'y' para continuar o cualquier otra clase para cancelar.\n
21 Respaldo de base de datos ha sido abortado por el utilizador. \n
22 DELETE posponido: Carpetas %1$d %2$s to \n \
	%3$d %4$s \n \
	no son necesarias mas a menos que ocurre una restauracion del respaldo actual.\n	
23 Un respaldo mas nuevo ha liberado carpetas marcadas anteriormente como DELETE POSTPONED.\n
24 COMENTARIO: Carpeta de registro %1$s ya no es DELETE POSTPONED debido a mas recien\n \
   respaldo asi que no es requierida a menos que ocurre una caida de la media de base de datos.\n
25 COMENTARIO: Carpetas de registro de %1$s a %2$s no son mas DELETE POSTPONED debido a \n\
   mas recien respaldo asi que no son requieridas a menos que ocurre una caida de la media\n \
   de base de datos.\n
26 Un error ha ocurrido durante una restauracion de base de datos, mientras procesando \n\
	                  Volumen de respaldo: %1$s\n\
	                     Numero de unidad: %2$d\n\
	             Volumen de base de datos: %3$s\n\
   Identificacion de volumen de base de datos: %4$d\n\
** ADVERTENCIA: No utilize la base de datos hasta una restauracion sin errores es lograda.\n
27 Error en entrada, escoja un valor entre %1$d y %2$d.\n
28 El tiempo (%1$s) especificado tiene que ser despues del tiempo (%2$s) del respaldo especificado.\n
29 COMENTARIO: Carpeta de registro %1$s, que contiene paginas de registro antes de %2$lld, no es necesaria mas para ninguna de las utilidades HA.\n
30 Numero de carpetas de registro activas ha excedido el numero maximo deseado.
