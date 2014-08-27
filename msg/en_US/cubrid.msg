
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
3 Copyright Information\n
5 Non integer argument value `%1$s'
6 Non floating argument value `%1$s'
7 Missing argument `%1$s'
8 Duplicate argument `%1$s'
9 Missing value for argument `%1$s'
10 Unknown argument `%1$s'
11 Unexpected positional argument `%1$s'
12 No argument definitions given
13 Couldn't allocate storage for argument parsing
14 Argument string exceeds maximum length: %1$s
15 Invalid message number %1$d within catalog for argument parsing
16 [options]
17 Options:

$set 4 MSGCAT_SET_PARAMETER
2 %1$s is not a directory.  Creating %2$s in ".".
3 Cannot open %1$s.  configuration file not created.
4 Unable to write line %1$d to configuration file %2$s.
6 Can't access system configuration file "%1$s": %2$s
7 HOME not set
8 Required parameter "%1$s" has no value.
9 Cannot open configuration file "%1$s": %2$s
10 The '%1$s' parameter at line %2$d in file '%3$s' : 
11 Environment variable %1$s: 
12 Unrecognized keyword.
13 Value type does not match parameter type.
14 Cannot allocate memory to store value.
15 Unterminated string value.
16 Value is out of range.
17 UNIX error: %1$s
18 No message available.
19 Value is out of range. Resetting to the default value.
20 Keyword = '%1$s'    Value = %2$d    Default Value = %3$d\n
21 Keyword = '%1$s'    Value = %2$f    Default Value = %3$f\n
22 Deprecated parameter
23 The '%1$s' and '%2$s' parameters can not be used together.\n
31 The '%1$s' parameter:

$set 5 MSGCAT_SET_ERROR
1 Missing message for error code %1$d.
2 Internal system failure: no more specific information is available.
3 Out of virtual memory: unable to allocate %1$lld memory bytes.
4 Has been interrupted.
5 Key = "%1$s" was not found in hash table "%2$s".
6 A NULL memory hash table was supplied to a hash table function.
7 Trying to format disk volume "%1$s" with an incorrect value %2$d for number of pages.
8 Unable to format disk volume "%1$s" with %2$d pages (%3$lld bytes).
9 Unable to format disk volume "%1$s" with %2$d pages (%3$lld Kbytes) due to insufficient space. Current space available is %4$d pages (%5$lld Kbytes).
10 Unable to mount disk volume "%1$s".
11 Unable to mount disk volume "%1$s". The database "%2$s", to which the disk volume belongs, is in use by user %3$s on process %4$d of host %5$s since %6$s.
12 Problems dismounting volume "%1$s".
13 An I/O error occurred while reading page %1$d of volume "%2$s".
14 An I/O error occurred while writing page %1$d of volume "%2$s".
15 Unable to write page %1$d of volume "%2$s" due to insufficient space in operating system device.
16 Unable to rename disk volume "%1$s" to "%2$s".
17 Internal error: fetching deallocated pageid %1$d of volume "%2$s".
18 All page buffers are fixed (LRU: %1$d).
19 Internal error: pageptr = %1$p of page %2$d of volume "%3$s" is not fixed.
20 Internal error: unknown pageptr = %1$p.
21 Internal error: unknown sector %1$d of volume "%2$s".
22 Internal error: unknown page %1$d of volume "%2$s".
23 Internal error: trying to deallocate disk system page %1$d of volume "%2$s".
24 Almost out of space in volume "%1$s". Total number of pages = %2$d, total number of free pages = %3$d.
25 Almost out of space in volumes with data as the main storage purpose. Total number of pages = %1$d, total number of free pages = %2$d.
26 Almost out of space in volumes with index as the main storage purpose. Total number of pages = %1$d, total number of free pages = %2$d.
27 Almost out of space in volumes with generic storage purpose. Total number of pages = %1$d, total number of free pages = %2$d.
28 Almost out of space in volumes with temporary data as the main storage purpose. Total number of pages = %1$d, total number of free pages = %2$d.
29 Last out of space notification for volume "%1$s". Total number of pages = %2$d, total number of free pages = %3$d.
30 Last out of space notification for volumes with data as the main storage purpose. Total number of pages = %1$d, total number of free pages = %2$d.
31 Last out of space notification for volumes with index as the main storage purpose. Total number of pages = %1$d, total number of free pages = %2$d.
32 Last out of space notification for volumes with generic storage purpose. Total number of pages = %1$d, total number of free pages = %2$d.
33 Last out of space notification for volumes with temporary data as the main storage purpose. Total number of pages = %1$d, total number of free pages = %2$d.
34 Internal error: %1$d nthpage is out of range for file %2$d in volume "%3$s". Current number of pages are %4$d.
35 Internal error: Unknown volume identifier %1$d.
36 Out of disk space in database. Number of requested page(s) = %1$d.
37 Out of disk space in volume "%1$s". Number of requested page(s) = %2$d.
38 Internal error: Unknown file VFID %1$d|%2$d.
39 Internal error: Page %1$d|%2$d(volume "%3$s") of file VFID %4$d|%5$d(volume "%6$s") is invalid/deallocated according to allocation map.
40 Internal error: %1$d expected pages, %2$d found pages for file VFID %3$d|%4$d(volume "%5$s").
41 Internal error: Page %1$d|%2$d(volume "%3$s") is not part of file VFID %4$d|%5$d(volume "%6$s").
42 Cannot reorder anchored records on page %1$d of volume "%2$s".
43 Internal error: The page %1$d of volume "%2$s" may be corrupted. %3$d records was found when %4$d was expected.
44 No space available in slotted page %1$d of volume "%2$s".
45 Slot %1$d on page %2$d of volume "%3$s" is allocated to an anchored record. A new record cannot be inserted here.
46 Internal error: slot %1$d on page %2$d of volume "%3$s" is not allocated.
47 Unable to create a heap file in volume "%1$s".
48 Accessing deleted object %1$d|%2$d|%3$d.
49 Internal error: class of object %1$d|%2$d|%3$d is unknown.
50 Internal error: relocation record of object %1$d|%2$d|%3$d may be corrupted.
51 Internal error: object %1$d|%2$d|%3$d may be corrupted.
52 Internal error: object overflow address %1$d|%2$d|%3$d may be corrupted.
53 Fetching object %1$d|%2$d|%3$d when only its OID has been assigned.
54 Internal error: trying to manipulate an object of %1$d pages long. The maximum size allowed for an object is %2$d pages.
55 Internal error: A page cycle reference was detected on page %1$d|%2$d of heap file %3$d|%4$d|%5$d.
56 Internal error: unknown extendible hashing file (Volid: %1$d Fileid: %2$d) page (Volid: %3$d Pageid: %4$d) was specified.
57 Key does not exist in the extendible hashing structure.
58 String "%1$s" is too long to fit in a slotted page.
59 Internal error: the specified key type %1$d is not valid for the extendible hashing structure.
60 Internal error: the extendible hashing structure has been corrupted.
61 Internal error: the directory root page of the extendible hashing structure (Volid: %1$d Fileid: %2$d Pageid: %3$d) has been corrupted.
62 Internal error: sorting record %1$d (size %2$d) does not fit in a page; the maximum allowable size is %3$d.
63 Internal error: a temporary page was corrupted during sorting.
64 Unknown class "%1$s".
65 Class "%1$s" already exists.
66 Internal error: unknown force operation %1$d for object %2$d|%3$d|%4$d.
67 Internal error: a heap file has not been allocated to store object %1$d|%2$d|%3$d.
68 Internal error: different classnames for class with oid = %1$d|%2$d|%3$d were found. Found classnames are "%4$s", "%5$s" using classname hash table and heap, respectively.
69 Internal error: different class object identifiers were found for class with name "%1$s". Found OIDS are %2$d|%3$d|%4$d, %5$d|%6$d|%7$d using classname hash table and heap, respectively.
70 Internal error: Class with name "%1$s" and oid = %2$d|%3$d|%4$d does not exist in classname hash table.
71 Internal error: Class with name "%1$s" and oid = %2$d|%3$d|%4$d does not exist in its heap.
72 Your transaction (index %1$d, %2$s@%3$s|%4$d) has been unilaterally aborted by the system.
73 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on object %6$d|%7$d|%8$d. You are waiting for user(s) %9$s to finish.
74 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on class %6$s. You are waiting for user(s) %7$s to finish.
75 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on instance %6$d|%7$d|%8$d of class %9$s. You are waiting for user(s) %10$s to finish.
76 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s on page %6$d|%7$d. You are waiting for user(s) %8$s to release the page lock.
77 Internal error: all buffers are fixed.
78 Internal error: an I/O error occurred while reading logical log page %1$lld (physical page %2$lld) of "%3$s".
79 Internal error: an I/O error occurred while writing logical log page %1$lld (physical page %2$lld) of "%3$s".
80 Insufficient space in operating system device when writing logical log page %1$lld (physical page %2$lld) of "%3$s". Could not write more than %4$d bytes.
81 Internal error: logical log page %1$lld may be corrupted.
82 Unable to mount log disk volume/file "%1$s".
83 Length of path "%1$s" plus length of prefix logname "%2$s" is too long; the combined length must be less than %3$d.
84 Length of prefix logname "%1$s" is too long; the length must be less than %2$d.
85 The prefix name "%1$s" is not the same as "%2$s" on the log disk. The log may have been renamed outside the database domain.
86 Database is incompatible with current "%1$s" release "%2$s".
87 There are recovery actions that must recovered using "%1$s" release "%2$s" instead of release "%3$s". After the recovery, the database can be run on release "%4$s".
88 Internal error: Release string "%1$s" cannot exceed %2$d. Must change header log.
89 Log "%1$s" does not belong to the given database.
90 Redo logging is always a page level logging operation. A data page pointer must be given as part of the address.
91 Postpone logging is always a page level logging operation. A data page pointer must be given as part of the address.
92 Compensate logging is always a page level logging operation. A data page pointer must be given as part of the address.
93 Warning: redo logging is ignored during redo recovery and normal rollbacks.
94 Warning: postpone logging is ignored during redo recovery and normal rollbacks.
95 Warning: undo logging is ignored during recovery and normal rollback.
96 Media recovery may be needed on volume "%1$s".
97 Internal error: unable to find log page %1$lld in log archives.
98 Unable to create archive log "%1$s" to archive pages from %2$lld to %3$lld.
99 Unable to create backup directory information file "%1$s".
100 Unable to backup database "%1$s".
101 Unknown user file "%1$s".
102 File "%1$s" does not have enough entries: %2$d entries expected.
103 File "%1$s" seems to have unordered entries. Entry %2$d with values: %3$d %4$s %5$s, was found when entry with values %6$d %7$s was expected.
104 File "%1$s" seems to have an incorrect entry for database name (main volume). Entry %2$d with values: %3$d %4$s %5$s, was found when entry with values %6$d %7$s %8$s was expected.
105 Cannot access backup file "%1$s". Restore or Backup is cancelled.
106 Cannot prepare to commit the current transaction with global transaction identifier %1$d because that transaction identifier is in use by another transaction.
107 There is no global distributed transaction associated with transaction identifier %1$d.
108 Current transaction %1$d must be committed or aborted before attempting to join to the distributed transaction with global identifier %2$d.
109 Your transaction %1$d has been unilaterally aborted by the system because a participant (remote) site went down or unilaterally aborted the transaction.
110 Isolation level value must be between %1$d and %2$d.
111 Your transaction has been aborted by the system due to server failure or mode change.
112 Host "%1$s" is not authorized to run database.
113 Unable to restart/initialize the database server. %1$s
114 Unable to interpret "%1$s" as a database. The database volumes/files may have been renamed/copied outside the database domain.
115 Database "%1$s" already exists.
116 Database "%1$s" is unknown, or the file "databases.txt" cannot be accessed.
117 Absolute pathname for the database is too long. The combined length of the path "%1$s" plus the name "%2$s" is %3$d; the combined length must be less than %4$d.
118 Failed to locate current working directory.
119 Unable to find local hostname.
120 The maximum number of volumes (%1$d) has been exceeded.
121 Trying to remove "%1$s" a permanent volume from the database.
122 Unable to access system message catalog.
123 Unable to create %1$s for database "%2$s". Please refer "%3$s" for additional information.
124 Volume "%1$s" already exists.
125 An automatic volume extension "%1$s" of %2$d pages and generic storage purpose was created.
126 Unknown purpose "%1$s" given on line %2$d.
127 Incorrect value %1$d for number of pages given on line %2$d.
128 Number of pages was not given on line %1$d.
129 Unknown token "%1$s" was found on line %2$d.
130 Out of virtual memory.
131 Database system error.
132 Feature not implemented in the current release: %1$s.
133 Nested set constant not allowed.
134 Not allowed to use both class attributes and non-class attributes in a single update.
135 Invalid statement for oid columns: %1$s.
136 Unimplemented function "%1$s".
137 Corrupted authorization objects.
138 Authorization class "%1$s" not found.
139 Access error on authorization attribute "%2$s" of class "%1$s".
140 Operation "%1$s" can only be performed by the DBA or a DBA group member.
141 Cannot add "%1$s" as a member of "%2$s".
142 Adding member causes the user hierarchy to become a cyclic graph.
143 Encountered a class with no owner.
144 Unable to access user object.
145 Cannot issue grant/revoke commands to yourself.
146 Cannot issue GRANT/REVOKE to owner of a class.
147 No GRANT option.
148 Cannot obtain write lock on authorization object.
149 Cannot create instance of authorization object.
150 Cannot revoke privileges from self.
151 Cannot revoke privileges from owner of a class.
152 GRANT not found.
153 No authorization privileges in effect for the database.
154 Incomplete authorization installation - no authorization privileges in effect for the database.
155 Multiple authorization roots.
156 Authorization failure.
157 SELECT authorization failure.
158 ALTER authorization failure.
159 UPDATE authorization failure.
160 INSERT authorization failure.
161 DELETE authorization failure.
162 INDEX authorization failure.
163 EXECUTE authorization failure.
164 User "%1$s" already exists.
165 User "%1$s" is invalid.
166 Invalid user specified.
167 DBA, members of DBA group and class owner can perform the operation.
168 Member not found.
169 Cannot remove user %1$s from the database.
170 No user logged in.
171 Incorrect or missing password.
172 Password string cannot have more than 31 bytes.
173 Could not locate database file "%1$s".
174 Could not obtain write access to database file "%1$s".
175 Badly formatted database file "%1$s".
176 Conversion error in date format.
177 Cannot create large object for internal storage.
178 Temporary OID encountered without MOP.
179 Domain "%1$s" is not compatible with domain "%2$s".
180 Domain "%1$s" of supplied value is not compatible with destination domain "%2$s".
181 Cannot coerce value of domain "%1$s" to domain "%2$s".
182 Cannot coerce value of domain "%1$s" to domain "%2$s" without overflow.
183 Unexpected amount of received data; %1$d expected, %2$d received.
184 Cannot allocate communications buffer.
185 Error receiving data from client.
186 Error receiving data from server.
187 Communications buffer not used.
188 Unknown database "%1$s".
189 Invalid host name "%1$s".
190 Server host not identified.
191 Cannot connect to server "%1$s" on "%2$s".
192 Could not load network configuration file.
193 Server received shutdown command from client.
194 Unknown server request id %1$d.
195 Server communications error: %1$s.
196 Server name not identified.
197 Could not make contact with master server.
198 Receive buffer too small, data truncated.
199 Server no longer responding.
200 Not properly removing set from object after free.
201 Removed incorrect UNIQUE table entry.
202 Attribute "%1$s" was not found.
203 Value for attribute "%1$s" must be of type "%2$s", not type "%3$s".
204 Function called with missing or invalid arguments.
205 Attribute "%1$s" cannot be made NULL.
206 Attribute "%1$s" does not have the UNIQUE constraint.
207 Class "%1$s" has a zero-size object.
208 Method "%1$s" not found.
209 No attribute or method with name "%1$s" was found.
210 Internal error: processing object template.
211 Attribute was deleted while building object template.
212 Attribute "%1$s" violates the UNIQUE constraint.
213 Invalid UNIQUE table entry encountered.
214 A domain conflict exists on attribute "%1$s".
215 Attribute with id %1$d not found.
216 Invalid object ownership tag on value.
217 Operation can only be performed on class objects.
218 Invalid object in db_get path expression.
219 Invalid db_get path expression.
220 Invalid set in db_get path expression.
221 Invalid set index in db_get path expression.
222 Attempt to assign string value greater than %2$d characters to attribute "%1$s".
223 Value %2$d is too large for smallint attribute "%1$s".
224 A database has not been restarted.
225 Missing value for attribute "%1$s" with the NOT NULL constraint.
226 Empty component list in class.
227 Attempt to assign value to attribute "%1$s" more than once.
228 Method "%1$s" called with %2$d arguments. The system allows a maximum of %3$d arguments.
229 Method "%1$s" requires argument %2$d to be within the domain "%3$s". An invalid value of domain "%4$s" was given.
230 Attribute "%1$s" cannot be assigned a string of length %2$d. The maximum allowable string length is %3$d.
231 Invalid API operation attempted on a temporary object.
232 Not allowed to use object templates over multiple transactions.
233 Cannot create class with basic type name "%1$s".
234 Method file "%1$s" not found.
235 %1$d unresolved method functions after linking.
236 Unresolved method "%1$s".
237 Severe problems were encountered during dynamic linking.
238 Could not access method file "%1$s".
239 Attribute "%1$s" was not found.
240 Method "%1$s" was not found.
241 There is no attribute or method with name "%1$s".
242 Signature for %2$s not found for method "%1$s".
243 Argument %2$d of method "%1$s" is undefined.
244 Domain name "%1$s" is not the name of a class or basic type.
245 The name "%1$s" has already been used as an attribute name.
246 The name "%1$s" has already been used as a method name.
247 Invalid arguments to schema manager internal function.
248 The attribute domain "%1$s" is not suitable for use with the %2$s constraint.
249 The requested operation on class "%1$s" can only be performed before instances are created.
250 Cannot create index on attribute "%1$s", defined with a shared value.
251 Signature %2$s already exists for method "%1$s".
252 Attribute "%1$s" must have a set domain for that operation.
253 Attribute "%1$s" cannot be defined with a nested set domain.
254 Unable to remove domain "%2$s" from attribute "%1$s".
255 Attribute "%1$s" is not of a variable sized domain.
256 Superclass already exists.
257 Superclass addition would cause cycles in class hierarchy.
258 Superclass not found.
259 Multiple signatures exist for method "%1$s".
260 Argument %2$d of method "%1$s" is not of a set domain.
261 Resolution for "%1$s" not found.
262 Value domain "%2$s" not suitable for attribute "%1$s".
263 Schema manager internal corruption detected.
264 Alias "%2$s" for attribute/method "%1$s" is not unique.
265 Shadowing attribute "%1$s" with conflicting domain "%2$s".
266 Name conflict on attribute "%1$s" with classes "%2$s" and "%3$s".
267 Shadowing method "%1$s" with conflicting signature.
268 Name conflict on method "%1$s" with classes "%2$s" and "%3$s".
269 Cannot define index on domain "%1$s".
270 Cannot define UNIQUE constraint on domain "%1$s".
271 Detected cycle in class hierarchy; "%1$s" is a super class of "%2$s".
272 Index "%2$s" already defined for class "%1$s".
273 Index "%1$s" does not exist.
274 Illegal characters in name: "%1$s".
275 Illegal operation on attribute "%1$s", inherited from class "%2$s".
276 Illegal operation on method "%1$s", inherited from class "%2$s".
277 Illegal operation on "%1$s", inherited from class "%2$s".
278 Class "%4$s" has incompatible domains for "%1$s", inherited from classes "%2$s" and "%3$s".
279 In class "%4$s", "%1$s" inherited from class "%2$s" cannot be used as requested. The definition from class "%3$s" has a more specific domain.
280 In class "%3$s", "%1$s" cannot be inherited from class "%2$s" because there is a local definition with an incompatible domain.
281 In class "%3$s", "%1$s" inherited from class "%2$s" is aliased but no substitute from another source was found.
282 In class "%4$s", "%1$s" inherited from class "%3$s" cannot serve as an alias substitute for class "%2$s". The domains are incompatible.
283 In class "%4$s", "%1$s" inherited from class "%3$s" cannot serve as an alias substitute for class "%2$s". It has a less specific domain.
284 Cannot request inheritance of "%1$s" because it is defined locally in the class.
285 Cannot use "%1$s" as an alias for inherited component "%2$s" because there is already a locally defined component with that name.
286 Cannot use "%1$s" as an alias for "%2$s" of "%3$s". A component with that name is already inherited from "%4$s".
287 In class "%1$s", cannot define attribute "%2$s" because it would shadow a method inherited from "%3$s".
288 In class "%1$s", cannot define method "%2$s" because it would shadow an attribute inherited from "%3$s".
289 Subclass "%1$s" cannot inherit method "%2$s" from class "%3$s" because there is already an attribute with that name.
290 Subclass "%1$s" cannot inherit attribute "%2$s" from class "%3$s" because there is already a method with that name.
291 "%1$s" is defined in class "%2$s" as an attribute and in class "%3$s" as a method.
292 No populate specification with index "%1$s".
293 Operation not allowed for this class type.
294 Undefined environment variable "%1$s" referenced in method file.
295 Unable to alter class "%1$s". The system catalog is out of space. To reclaim space, compact the database with the compactdb utility.
296 Invalid property list encountered.
297 Alias name "%1$s" is used more than once.
298 In class "%1$s", a resolution for "%2$s" of "%3$s"' is not allowed. That class is not a super class.
299 "%1$s" is a reserved word and cannot be used as an attribute, method, or class name.
300 Attribute "%1$s" cannot be defined with DEFAULT and UNIQUE constraints together.
301 Attribute "%1$s" cannot be defined with a length constraint of %2$d. The maximum length constraint is %3$d.
302 Cannot add element to a set.
303 Value already exists in set.
304 Set index %1$d is out of bounds.
305 Sequence index %1$d is out of bounds.
306 Requested operation can only be performed on sequences.
307 Requested operation can only be performed on sets.
308 Element value is not within the domain of the set.
309 Illegal set element index given: %1$d.
310 Element not found in set.
311 Element not found in sequence.
312 "%1$s" is not a valid set domain.
313 Object buffer underflow while reading.
314 Object buffer overflow while writing.
315 Illegal metaclass definition encountered.
316 Transformer size calculation mismatch, expected %1$d calculated %2$d.
317 Invalid disk representation encountered for class "%1$s".
318 Out of sync during object loading. Database is likely corrupted or out of date.
319 Ignoring unknown attribute extension.
320 Encountered corrupted disk representation of object.
321 Possible corruption in the workspace detected.
322 MOP not found in the workspace.
323 Attempt to upgrade non-temporary MOP in workspace.
324 Object found in workspace without class.
325 Attempt to garbage collect a MOP in the dirty list.
326 Changing class pointer for object in workspace.
327 Cannot create MOP with NULL OID.
328 Instance encountered in workspace without class.
329 Object list link not allocated in workspace.
330 Workspace pin violation.
331 Could not allocate workspace block from OS, out of virtual memory. Aborting transaction.
332 Original workspace size estimate exceeded. An automatic extension has been made.
333 Out of memory during workspace storage allocation. Aborting transaction.
334 Attempted allocation of %1$d bytes is larger than maximum workspace block size. Try adjusting max_block_size parameter. Transaction aborted.
335 Ignoring attempt to free object not allocated within the workspace.
336 Ignoring attempt to free an object more than once.
337 Ignoring attempt to allocate object with negative size.
338 Map entry identifier overrun.
339 Cannot exit critical section, since it is not owned by transaction %1$d.
340 Cannot fork child process.
341 Cannot exec child process.
342 Cannot change process group.
343 Request_id is already used.
344 %1$s.
345 Error occurred during reading a shutdown message from master.
346 Error occurred during reading a stop shutdown message from master.
347 Could not establish a pipe to the master server.
348 Must specify either service or port identifier.
349 Unknown service for TCP: %1$s.
350 Error getting local host by name for host "%1$s". Host not found.
351 Cannot create TCP socket.
352 tcp_open: cannot get a reserved TCP port.
353 Cannot make connection to master server on host "%1$s".
354 Cannot set new process owner for socket.
355 Cannot create socket stream.
356 '%1$s' file for UNIX domain socket exist.
357 Cannot bind local address... aborting.
358 Accept failed.
359 Datagram bind error.
360 Datagram accept error.
361 Datagram connect error.
362 Datagram socket error.
363 Recvmsg from master socket failed.
364 Sending a new request to server.
365 Sending a broadcast message to client.
366 Error from server: %1$s
367 Server %1$s already exists.
368 Communication error during connect for %1$s.
369 Unused error code.
370 Unused error code.
371 Unused error code.
372 Unused error code.
373 Premature EOF during read: %1$d.
374 Premature EOF during read of %1$d or %2$d.
375 Conversion error on (0x%1$c%2$c) %3$d and %4$d converted to %5$d.
376 No public class found.
377 Error %1$d: failed to add attribute "%2$s" to multimedia hierarchy.
378 Error %1$d: failed to add method "%2$s" implemented as "%3$s" to multimedia hierarchy.
379 Error %1$d: failed to add a superclass to multimedia hierarchy.
380 Dynamic loader already initialized.
381 Dynamic loader not initialized.
382 Dynamic loader improperly initialized.
383 "%1$s" is not an object file or an archive.
384 Cannot determine absolute path for "%1$s".
385 ld process exited with code %1$d.
386 ld process exited due to signal %1$d.
387 ld process exited with unknown waitval 0x%1$x.
388 Cannot access initial image for "%1$s".
389 System error: %1$s.
390 Problem with "%1$s": %2$s.
391 Conflict with other SIGPIPE handler.
392 Unable to spawn dynamic loader daemon.
393 Dynamic loader daemon has disappeared.
394 Range endpoint not between 0 and 256.
395 Illegal subexpression specified.
396 Octal digit out of range exists in escape.
397 Regular expression not delimited properly.
398 Text search called before expression compiled.
399 An unmatched parenthesis is found in the regular expression.
400 Parentheses too deeply nested in the regular expression.
401 More than two numbers specified in a range.
402 An unmatched curly brace is found in the regular expression.
403 First number in range expression is larger than second.
404 An unmatched bracket is found in the regular expression.
405 The regular expression was too long to compile.
406 Invalid B+tree index identifier: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
407 Unknown key %1$s referenced in B+tree index {vfid: (%2$d, %3$d), rt_pgid: %4$d, key_type: %5$s}.
408 Unknown value (oid) %1$d|%2$d|%3$d referenced.
409 Duplicate value (oid) %1$d|%2$d|%3$d referenced.
410 Null key referenced.
411 Invalid B+tree index key type "%1$s".
412 Invalid range search specification.
413 Unknown attribute identifier: %1$d.
414 Unknown class identifier: %1$d|%2$d|%3$d.
415 Invalid class identifier: %1$d|%2$d|%3$d.
416 Unknown representation identifier: %1$d.
417 Invalid representation identifier: %1$d.
418 Record descriptor does not have enough space: %1$d.
419 Class has reached maximum representations count: %1$d.
420 Class %1$d|%2$d|%3$d already had representations.
421 Representations Directory Missing in Catalog for Class: %1$d|%2$d|%3$d.
422 Representation Information Record Missing in Catalog for Class - Repr_Id: %1$d|%2$d|%3$d - %4$d.
423 Invalid session.
424 No statement to execute.
425 Incompatible data type for attribute "%1$s" of class "%2$s".
426 Incompatible data type on attribute "%1$s".
427 Data overflow on data type "%1$s".
428 Statement is not updatable.
429 Unknown command.
430 Unknown variable "%1$s".
431 Error in SQL/X interpreter or parser.
432 Unknown class "%1$s".
433 Unknown attribute "%1$s" in class "%2$s".
434 Cannot find correct column names from a query.
435 Multiple statements not allowed.
436 Statement is not a query.
437 Invalid long object ID (%1$s,%2$d, %3$s, %4$s).
438 Conflict exists in the long object descriptor.
439 Offset %1$d is larger than length of long object %2$d.
440 Invalid cursor position.
441 Invalid cursor operation.
442 Unknown cursor position.
443 Tuple value index %1$d is out of range.
444 Invalid attribute name: "%1$s".
445 Value list index %1$d is out of range.
446 There are query result structures closed by transaction manager but not closed by the application. 
447 Attempted to operate a query result structure already closed.
448 Tuple sizes are currently restricted to be no larger than a page.
449 Unknown query identifier: %1$d.
450 Invalid set operator: %1$d.
451 Heap file with no instances.
452 Invalid XASL tree node content.
453 No more access specification nodes.
454 Invalid data type referenced.
455 No more query file pages left.
456 Data type references are incompatible.
457 Invalid query result type.
458 Overflow occurred in addition context.
459 Query result contains more than a single tuple. 
460 Too few input host variables provided.
461 More host variables are given than result columns.
462 Result value is NULL, but there is no indicator variable.
463 Cursor statement has not yet been prepared.
464 Cursor should be a SELECT statement.
465 The cursor is not yet opened.
466 The cursor should be closed before re-opening.
467 Result should return a single object.
468 No positional markers are allowed in EXECUTE IMMEDIATE statements.
469 Invalid data type used.
470 Query involves too many items for optimizer.
471 Memory exhausted in line %1$d of file %2$s.
472 %1$s connector for local database '%2$s' on host '%3$s' has died.
473 Reported from local database '%1$s' on host '%2$s': %3$s.
474 Error %1$s detected during %2$s.
475 No query specification with index %1$s.
476 LDB %1$s already exists.
477 Could not connect to master on host %2$s to access LDB %1$s.
478 The local database "%1$s" is not a registered LDB.
479 The local database "%1$s" is currently being accessed by other clients, alteration or drop is not allowed.
480 The local database "%1$s" is not empty and cannot be dropped.
481 "%1$s" is not a proxy virtual class.
482 "%1$s" is not a virtual class.
483 "%1$s" is not an attribute of the class "%2$s".
484 The object_id for proxy virtual class "%1$s" is already set.
485 One or more object_id attributes are NULL for instance of proxy class "%1$s".
486 Object is not an updatable virtual class instance.
487 Object is not an instance.
488 An attribute of a class may not be a virtual class or a proxy virtual class.
489 An attribute of a proxy virtual class may not be a class, virtual class, or object.
490 The superclass "%1$s" must be of the same type of class as "%2$s".
491 Cannot extract entity name from proxy query specification "%1$s".
492 %1$s
493 Syntax: %1$s %2$s
494 Semantic: %1$s %2$s
495 Execute: %1$s %2$s
496 Unknown key referenced.
497 Unknown value (oid) %1$d|%2$d|%3$d referenced.
498 Duplicate value (oid) %1$d|%2$d|%3$d referenced.
499 Null key referenced.
500 Duplicate key referenced.
501 Invalid trigger priority.
502 Missing target class in trigger.
503 Trigger "%1$s" was not found.
504 Internal error: processing trigger "%1$s".
505 A trigger with the name "%1$s" already exists.
506 A trigger cannot be defined on "%1$s" because it is a virtual class.
507 Inappropriate trigger target class "%1$s".
508 Inappropriate trigger target attribute "%1$s".
509 Invalid trigger condition "%1$s".
510 Invalid trigger action "%1$s".
511 Not authorized to access trigger "%1$s".
512 Not authorized to delete trigger "%1$s".
513 Not authorized to update trigger "%1$s".
514 Not authorized to alter trigger "%1$s".
515 Trigger action execution time of %2$s cannot be earlier than condition execution time of %1$s.
516 Maximum trigger depth %1$d exceeded at trigger "%2$s".
517 The operation has been rejected by trigger "%1$s".
518 Internal error: processing trigger "%1$s".
519 Illegal trigger condition type.
520 The REJECT action cannot be used with an action time of AFTER or DEFERRED.
521 The REJECT action cannot be used with the ABORT or TIMEOUT events.
522 Trigger condition expression was not given.
523 Trigger action expression was not given.
524 The specified activity cannot be removed because it is owned by another user.
525 Error compiling condition for "%1$s": %2$s
526 Error compiling action for "%1$s", %2$s
527 Error evaluating condition for "%1$s": %2$s
528 Error evaluating action for "%1$s", %2$s
529 Transaction cannot be committed, it was invalidated by trigger "%1$s".
530 Missing regular expression.
531 Regular expression is too long or complex.
532 Unbalanced parenthesis or brackets in regular expression.
533 Reference is out of range in regular expression.
534 Illegal character or syntax in regular expression.
535 No text given for regular expression search.
536 Illegal opcode found in regular expression.
537 Illegal search command code for regular expression compiler.
538 Regular expression working buffer needs to be initialized.
539 Attempt to divide by zero.
540 %1$s.
541 Number of free pages for volume "%1$s" is inconsistent. %2$d and %3$d were found according to volume header and bitmap, respectively.
542 Number of free sectors for volume "%1$s" is inconsistent. %2$d and %3$d were found according to volume header and bitmap, respectively.
543 Disk header for volume "%1$s" is inconsistent.
544 Internal error: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). B+tree key %6$s entry for object OID: %7$d|%8$d|%9$d was not found on B+tree: %10$d|%11$d|%12$d.
545 Internal error: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Key and OID: %6$d|%7$d|%8$d entry on B+tree: %9$d|%10$d|%11$d is incorrect. The object does not exist.
546 Internal error: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). Expecting %6$d OID entry values, but %7$d were found on B+tree: %8$d|%9$d|%10$d.
547 Server release %1$s is different from client release %2$s.
548 There is no object_id defined for proxy class "%1$s".
549 The proxy instance was not found.
550 Unknown savepoint name %1$s
551 Unable to locate volume information path file "%1$s"... Continue reading from internal tables
552 Date arithmetic underflow.
553 Time arithmetic underflow.
$ LOADDB
554 The loader has entered an invalid state. No further operations are possible.
555 Memory allocation error during database loading.
556 Too many values supplied. Expecting %1$d.
557 For attribute %1$s of %2$s, type %3$s is not allowed in the domain of the set.
558 For attribute %1$s of %2$s, expected type %3$s, got a set constant.
559 For attribute %1$s of %2$s, expected type %3$s, got %4$s.
560 For attribute %1$s of %2$s, domain is not specific enough to support\n unqualified object references.
561 Nested sets are not allowed.
562 System class %1$s cannot be populated using the database loader.
563 Reference to internal class %1$s converted to NULL.
564 Violated the UNIQUE constraint for attribute %1$s of %2$s.
565 Constructor method %1$s is not defined on class %2$s.
566 No class specified or no attribute in class.
567 Too many constructor method arguments supplied. Expected %1$d.
568 Missing constructor method parameters. Expected %1$d, found %2$d.
569 Missing attribute values.  Expected %1$d, found %2$d.
570 Could not access Glo data file "%1$s".
571 Instance was previously created through a forward reference.\nThis is not allowed for instances created using constructor methods.
572 INTERNAL ERROR: Cannot build disk representation of instance.
573 INTERNAL ERROR: Cannot insert new instance in database.
574 INTERNAL ERROR: Cannot update new instance in database.
575 For argument %1$d of constructor %2$s, expected type %3$s, got %4$s.
576 Reference to class %3$s is not compatible with the domain of %1$s of %2$s.
577 For argument %1$d of constructor %2$s, domain is not specific enough to support\n unqualified object references.
578 Reference to class %3$s is not compatible with the domain of argument %1$d \n of constructor %2$s.
579 Target domain must include type "object" to allow references to classes.
580 Attribute %1$s is not defined for class %2$s.
581 Attempted to update the database when updates are disabled. 
582 Unknown storage purpose %1$d. Valid range is from %2$d to %3$d.
583 Trying to allocate an invalid number (%1$d) of pages.
584 Internal Error: A loop was detected in the file table of file %1$d in volume "%2$s". The loop may be caused by page %3$d|%4$d.
585 Unknown heap %1$s|%2$d|%3$d
586 Internal Error: Unable to update chain header in VPID %1$d|%2$d for heap %3$d in volume "%4$s".
587 Entries of permanent volumes are unsorted in your "%1$s" volinfo file. Entry %2$d: %3$d %4$s is out of sequence.
588 INTERNAL ERROR: Assertion '%1$s' failed.
589 Invalid user name "%1$s".
590 The attribute domain "%1$s" of a class may not be a \nvirtual class or a proxy virtual class.
591 The attribute domain "%1$s" of a proxy virtual class \nmust be a proxy virtual class.
592 The attribute domain "%1$s" of a proxy virtual class \nand the proxy virtual class must be on the same local database.
593 A non-updatable virtual instance has been reclaimed and cannot be rebuilt.
594 An object_id clause must be supplied without using attributes (%1$s) of approximate data types (float or double).
595 The database name "%1$s" is too long. Database name should be less than %2$d characters.
596 The %1$d pages of total temporary space allowed have been exceeded.
597 Number of pages for heap file %1$d|%2$d|%3$d is inconsistent. \n%4$d and %5$d were found according to heap chain and file table, respectively.
598 Number of files is inconsistent. %1$d were expected, %2$d were found
599 An I/O error occurred while synchronizing state of volume "%1$s".
600 Function "%1$s" not implemented on the PC.
601 "%1$s".
602 dl error: "%1$s" is defined more than once.
603 Internal Error: Sector/page table of file VFID %1$d|%2$d seems corrupted.
604 Internal Error: An allocation set for file VFID %1$d|%2$d(Volume "%3$s") seems inconsistent in %4$d deleted or marked deleted pages and %5$d page holes in table
605 Internal Error: Inconsistent number of marked deleted pages for file VFID %1$d|%2$d(Volume "%3$s"). Expected %4$d, Found %5$d.
606 Could not locate odbc.ini with path "%1$s".
607 Database "%1$s" was not found in odbc.ini.
608 Database "%1$s" does not have a Host specification in the odbc.ini file.
609 Your transaction cannot be rolled back since logging was ignored. Your database may be corrupted.
610 Your database is likely to be corrupted since logging was turned off when your database crashed.
611 %1$s cannot be executed in client/server mode.
612 Logging cannot be disabled at this moment since there are pending actions to recover.
613 In class "%3$s", local component "%1$s" cannot serve as an alias substitute for class "%2$s". The domains are incompatible.
614 Number of active log archives has been exceeded the max desired number of %1$d.
615 Cannot establish contact with the Winsock interface. Winsock error: %1$d.
616 Cannot determine machine name using Winsock interface. Winsock error: %1$d.
617 Cannot determine machine id using Winsock interface. There must be an entry in the host name table for this machine. Winsock error: %1$d.
618 Attribute is not updatable.
619 Bad source codeset.
620 Bad destination codeset.
621 Invalid data type. The arguments contain an invalid data type.
622 Incompatible code sets. The code sets of the arguments are incompatible.
623 Invalid escape sequence. An invalid escape sequence has caused an error in the regular expression compiler.
624 Invalid escape character. The escape character is non-NULL with a length greater than 1.
625 Internal Error. Trying to update the wrong instance object %1$d|%2$d|%3$d attribute information template with instance object %4$d|%5$d|%6$d.
626 Internal Error. %1$d requested attributes were not found.
627 Invalid trim operand.  The trim operand has a length greater than 1.
628 Invalid currency type: %1$d.
629 Pagesize %1$d is not a power of 2 or pagesize is too small. Pagesize of %2$d is used instead.
630 Conversion not supported by %1$s.
631 SQL statement violated NOT NULL constraint.
632 %1$s is not a backup volume.
633 %1$s is a backup of database %2$s created on %3$s instead of given database %4$s created on %5$s
634 A database volume/file was expected in backup.
635 WARNING: Log Buffer pool of %1$d buffers seems very small for current environment.\n It is possible that some buffers are not freed by the system due to an UNKNOWN ERROR\n or that there are many concurrent transactions being aborted at the same time. The log buffer pool is expanded with %2$d more buffers.
636 WARNING Num_buffers %1$d is too small. The minimum value of %2$d was assumed.
637 System error: freeing a log buffer too much. Fix count is set to zero.
638 Warning: Flushing a non-updatable log archive pageid = %1$d
639 System error: Previous log sequence page %1$d is not the delayed flush page %2$d.
640 System Error: a savepoint cannot be added when the transaction is not active (e.g., during commit or rollback).
641 A name must be given for a savepoint.
642 System Error: there is not an active top system operation for current transaction.
643 May be a system error: Committing/Aborting transaction = %1$d (index = %2$d) which has permanent operations\n attached to it. Will attach those system operations to the transaction
644 LOG FATAL ERROR: %1$s
645 SYSTEM ERROR: State = %1$s of Transaction = %2$d (index = %3$d) \n is not the correct state to apply client postpone/undo actions... ignored
646 SYSTEM ERROR: It seems that a compensating record is missing for logical logging rcvindex = %1$s\n. Without this compensating record crash recovery will not be done correctly
647 Backup "%1$s" does not belong to the given database.
648 Backup is incompatible with current "%1$s" release "%2$s".
649 Given precision of %1$d is invalid; it should be greater than %2$d and no greater than %3$d.
650 Cannot create enough threads for 'max_clients=%1$d'.
651 There is not virtual memory to preallocate %1$d sort buffers for each of %2$d threads.
652 Wrong offset %1$d was given to split the record of length %2$d stored on slot %3$d
653 Wrong offset %1$d or length %2$d was given to remove a portion of the data of the record of length %3$d stored on slot %4$d
654 Wrong offset %1$d or length %2$d was given to overwrite a portion of the data of the record of length %3$d stored on slot %4$d
655 Cannot open the event handler configuration file, "%1$s".
656 Warning: Null id/name in event handler configuration file, "%1$s", line %2$d
657 Warning: Illegal event name, "%1$s", in event handler configuration file, "%2$s", line %3$d
658 Warning: event id %1$d, not in error id range %2$d <> %3$d, file "%4$s", line %5$d
659 Cannot access the event handler program, "%1$s".
660 Cannot open/connect the event handler process, "%1$s".
661 Failed to write to the event handler process, "%1$s".
662 Failed to initialize event handling mechanism.
663 Warning: Line too long in event handler configuration file, "%1$s", line %2$d, truncating
664 Event handling mechanism started.
665 Event handler pipe broken, "%1$s"
666 Event handling mechanism stopped.
667 Cannot match class '%1$s' with corresponding C++ class.
668 Internal error: Unknown domain type '%1$d'.
669 Server refused client connection: max clients, (%1$d), exceeded.
670 Operation would have caused one or more unique constraint violations. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
671 A network read/write error occurred for socket %1$d associated with your transaction (index %2$d, %3$s@%4$s|%5$d)
672 Socket %1$d associated with your transaction (index %2$d, %3$s@%4$s|%5$d) is closed.
673 Your connection to the server has been closed since the server is going down immediately.
674 There is not enough active threads to continue current server execution. Current number of active threads is %1$d. You may need up to %2$d threads for clean server execution in your environment. Transaction index %3$d has been timed out to continue server execution.
675 Cannot read the database location file, '%1$s'.
676 Database '%1$s', cannot be found in the database location file, '%2$s'
677 Failed to connect to database server, '%1$s', on the following host(s): %2$s
678 Internal error. Client restart module received NULL database server name.
679 This C++ transaction has already begun.
680 Attempted to exceed maximum C++ transaction nesting level (%1$d).
681 %1$s transaction was attempted on a C++ transaction that hasn't begun.
682 Cannot %1$s C++ transaction until all subordinate transactions are completed.
683 Attempted to create string with illegal length - %1$d
684 Method returned user-defined error code %1$d.
685 Class "%1$s" is invalid.
686 Notification: the event handler has been restarted.
687 Not allows to cast (%1$s *) to (%2$s *).
688 Subscript out of range. Attempt to access element %1$lld. Collection size = %2$lld.
689 Internal error. Iterator contains corrupt state.
690 Attempt to dereference an uninitialized instance of odb_Ref<>.
691 Attempt to use an invalid iterator.
692 Attempt to use an iterator with an unrelated collection.
693 Overflow occurred on data type Numeric.
694 SYSTEM ERROR: Unable to load B+tree.
695 Wrong interface has been used to kill a client.
696 Unknown transaction (index %1$d, %2$s@%3$s|%4$d).
697 Given transaction index (index %1$d, %2$s@%3$s|%4$d) does not match with current transaction index (index %5$d, %6$s@%7$s|%8$d). Likely, the given transaction has finished and its transaction index was re-assigned to another transaction.
698 Internal error: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). the number of OIDs in the unique hierarchy: %6$d does not equal the number of OIDs: %7$d found in the unique B+tree plus the number of NULLs: %8$d found in the unique hierarchy for B+tree: %9$d|%10$d|%11$d.
699 Internal error: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). the number of OIDs in the unique hierarchy: %6$d does not equal the number of OIDs: %7$d found in the unique B+tree root statistics for B+tree: %8$d|%9$d|%10$d.
700 Internal error: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). the number of OIDs: %6$d does not equal the number of NULLs: %7$d plus the number of keys: %8$d in the unique B+tree: %9$d|%10$d|%11$d.
701 No proxy is defined on "%1$s" at ldb "%2$s".
702 Internal error: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). the number of NULLs in the unique hierarchy: %6$d does not equal the number of NULLs: %7$d found in the unique B+tree root statistics for B+tree: %8$d|%9$d|%10$d.
703 Internal error: INDEX %1$s ON CLASS %2$s (CLASS_OID: %3$d|%4$d|%5$d). OID: %6$d|%7$d|%8$d found in unique B+tree: %9$d|%10$d|%11$d does not belong to one of the classes for the unique constraint.
704 Evaluation of generic server function failed.
705 Inconsistent %1$d permanent volumes were found when %2$d were expected.
706 Cannot expand permanent volumes. Permanent volume "%1$s" with purpose %2$s was given.
707 Unable to expand temporary volume "%1$s" with %2$d pages
708 Unable to expand temporary disk volume "%1$s" with %2$d pages (%3$lld Kbytes) due to insufficient space. Current space available is %4$d pages (%5$lld Kbytes).
709 Constraint "%1$s" not found.
710 Invalid constraint.
711 An object with the supplied attribute value doesn't exist.
712 Constraint" %1$s" already exists.
713 An index does not exist on this attribute.
714 Query failed due to insufficient temporary file space.
715 Failed to verify schema: %1$s
716 NOT NULL constraints are not allowed in virtual or proxy classes.
717 UNIQUE constraints are not allowed in virtual or proxy classes.
718 Invalid gadget.
719 The number of attributes(%1$d) and values(%2$d) are not equal.
720 Cannot use a gadget to insert into a virtual or proxy class.
721 Insert query yields more than one row.
722 Wrong number of attributes for NOT NULL constraint.
723 Cannot modify the domain of an existing attribute.
724 Invalid parameter(s) for default constraint name generation.
725 Internal error: expected temporary oid and encountered permanent oid.
726 UNIQUE constraint must be added using a separate alter statement\nonce instances have been created. For example, instead of:\n\talter table1 add attribute a int unique;\nuse:\n\talter table1 add attribute a int;\n\talter table1 add unique(a);
727 Invalid trigger event.
728 Query failed because temporary file vfid is invalid for transaction %1$d.
729 Overflow occurred in subtraction context.
730 Overflow occurred in multiplication context.
731 Overflow occurred in division context.
732 Overflow occurred in unary minus context.
733 Overflow occurred in coercion context.
734 Internal Error: Header of file VFID %1$d|%2$d(Volume "%3$s") is corrupted.
735 Unknown transaction index %1$d.
736 Must specify either service or port identifier. Winsock error: %1$d.
737 Unknown service for TCP: %1$s. Winsock error: %2$d.
738 Cannot create socket stream. Winsock error: %1$d.
739 Cannot bind local address... retrying. Winsock error: %1$d.
740 Cannot bind local address... aborting. Winsock error: %1$d.
741 Accept failed. Winsock error: %1$d.
742 Sending a broadcast message to client. Winsock error: %1$d.
743 Failed on handshake between client and server. (peer host %1$s)
744 Error in rehashing a proxy/vclass MOP.
745 Invalid garbage collection phase.
746 Invalid garbage collection callback.
747 Invalid garbage collection callback ticket.
748 Invalid object pointer encountered by db_gc_mark_object.
749 The number of fetched instances are not matched: %1$d expected, %2$d received.
750 The number of fetched instances are not matched: %1$d expected, %2$d received.
751 The OID of a new object isn't available in a BEFORE INSERT trigger.
752 Error restoring backup unit_num %1$d.
753 Error restoring backup unit_num %1$d, pageid %2$d exceeds total number of pages %3$d for volid %4$d.
754 OO proxy instance has non-string-type or null key.
755 %1$s cannot be executed in stand-alone mode.
756 Specified string buffer is too small to encode this object.
757 Internal Error: cannot assign permanent OID during object encode.
758 Specified string is not valid for decoding object.
759 The target object has been deleted.
760 Internal error: proxy object instance has NULL key during object encode.
761 Internal error: vclass object instance maps to invalid real instance.
762 Internal error in resolving vclass instance to a real instance.
763 Internal error in encoding non-updatable object instance.
764 Internal error in encoding object into internal vobj form.
765 Output object argument has a NULL address.
766 Internal error in decoding object.
767 Cannot create a level %1$d backup without first creating a level %2$d backup.
768 The local database "%1$s" has been accessed in this session, alteration or drop is not allowed.
769 Attribute "%1$s", of class "%2$s" is not a valid class or shared attribute.
770 No table functions.
771 Invalid parameter for function.
772 db_serial class not found.
773 Serial "%1$s" not found.
774 Serial "%1$s" already exist.
775 Next value exceeds MAX(MIN) value.
776 Cannot fetch serial object.
777 Cannot update serial object.
778 Cannot create a symbolic link "%1$s" to the file "%2$s".
779 Result exceeds limit: year of Date MUST be in 1 and 9999.
780 Internal system failure: unable to get system time.
781 Format string is too long. It must be less than 16K.
782 Empty string not allowed here.
783 Invalid format.
784 Two arguments are mismatched.
785 First argument string is too long. It must be less than 16K.
786 Format is duplicated.
787 Conversion error in time format.
788 Conversion error in timestamp format.
789 Wrong number.
790 Query execution has been interrupted by user.
791 Invalid serial value.
792 Cannot allocate memory for css.
793 pthread_attr_init() failed.
794 pthread_attr_destroy() failed.
795 pthread_attr_setdetachstate() failed.
796 pthread_attr_setscope() failed.
797 pthread_attr_setstacksize() failed.
798 pthread_create() failed.
799 pthread_join() failed.
800 pthread_mutex_init() failed.
801 pthread_mutex_destroy() failed.
802 pthread_mutex_lock() failed.
803 pthread_mutex_trylock() failed.
804 pthread_mutex_unlock() failed.
805 pthread_mutexattr_init() failed.
806 pthread_mutexattr_destroy() failed.
807 pthread_mutexattr_settype() failed.
808 pthread_mutexattr_gettype() failed.
809 pthread_cond_init() failed.
810 pthread_cond_destroy() failed.
811 pthread_cond_wait() failed.
812 pthread_cond_timedwait() failed.
813 pthread_cond_signal() failed.
814 pthread_cond_broadcast() failed.
815 pthread_key_create() failed.
816 pthread_key_delete() failed.
817 pthread_setspecific() failed.
818 pthread_getspecific() failed.
819 pthread_once() failed.
820 Cannot initialize a connection.
821 Shutdown a connection.
822 Mutex operation failed while getting next client id.
823 Mutex operation failed while initializing connection list.
824 Cannot destroy mutex while finalizing connection list.
825 pread() call failed.
826 pwrite() call failed.
827 Connection list traverse function returned invalid value.
828 Invalid critical section of the index was given.
829 This mutex lock has been unlocked already.
830 Cannot allocate query entry any more. Maximum allocatable entries are %1$d.
831 All transaction descriptors(%1$d) are in use. Raise up max_clients parameter.
832 Backup is already running. Backup cannot be run by two processes.
833 The index name "%1$s" is ambiguous. Specify the class name of the index.
834 There are some mismatches between source string "%1$s" and format string "%2$s" given in to_number().
835 Undefined statistics item.
836 LATCH ON PAGE(%1$d|%2$d) TIMEDOUT
837 Cannot drop the user who owns database objects(class/trigger/serial etc). 
838 Cannot open more than %1$d index scans simultaneously.
839 "%1$s": Unknown system parameter or bad value.
840 Cannot change system parameter "%1$s".
841 Requested operation can be executed by only sole client(transaction).
842 Cannot cache lock of object %1$d|%2$d|%3$d.
843 Wrong arguments in %1$s, a %2$s was passed.
844 Unknown isolation(%1$d) is found. The transaction index is %2$d.
845 Invalid object type(%1$d) is found. The lockable object is %2$d|%3$d|%4$d.
846 %1$s of tran(%2$d) is not found in the holder list of an object(%3$d|%4$d|%5$d).
847 %1$s on a %2$s(%3$d|%4$d|%5$d) is not found in the transaction(%6$d) hold list(cnt=%7$d).
848 %1$s on an object(%2$d|%3$d|%4$d) is not found in the transaction(%5$d) non2pl list.
849 Trying to abort a transaction(%1$d) twice.
850 Tran(%1$d) is neither lock holder nor lock waiter of an object(%2$d|%3$d|%4$d).
851 Failed to allocate lock resource (%1$s). 
852 Total holders mode(%1$d) of an empty lock holder list is not NULL_LOCK.
853 For an object(%1$d|%2$d|%3$d), tran(%4$d) of Uncommitted Read held fault lock(%5$s).
854 WARNING: No lock holder, but lock waiters exist. refer to %1$s.
855 WARNING: Two or more threads are lock-waiting for one transaction(%1$d).
856 WARNING: Strange lockwait state(%1$d|%2$d), thread(%3$d|%4$d) and transaction(%5$d).
857 An I/O error occurred while truncating page %1$d of volume "%2$s".
858 Volume "%1$s" is unknown at server restart.
859 LATCH ON PAGE(%1$d|%2$d) ABORTED
860 Cannot make current transaction as a part of a global transaction because its state is '%1$s'.
861 Cannot prepare to commit the current transaction with transaction identifier %1$d because that transaction is not made as a part of a global transaction.
862 Cannot set the user information of the global transaction with the identifier %1$d because its state is '%2$s'.
863 Cannot get the user information of the global transaction with the identifier %1$d because it is not set.
864 Cannot open backupdb/restoredb verbose file '%1$s'.
865 Cannot execute statement due to unknown type input markers.
866 Unused error code.
867 Unused error code.
868 Unused error code.
869 Unused error code.
870 Unused error code.
871 Unused error code.
872 Unused error code.
873 PKI authentication error "%1$s".
874 Argument of power() is out of range.
875 Overflow happened in power context.
876 Unused error code.
877 No data to be unloaded.
878 Invalid database location file, '%1$s'.
879 Cannot get permission: '%1$s'.
880 Cannot change permission '%1$s'.
881 Cannot get lock file '%1$s(FILE ID:%2$d)'.
882 Cannot release lock file 'FILE ID:%1$d'.
883 Failed to get '%1$s' file status.
884 Volext-pathname '%1$s' is not permitted to write.
885 Failure to move from '%1$s' to '%2$s'. Use the same device(disk).
886 "%1$s" caused unique constraint violation.
887 Stored procedure '%1$s' is already exist.
888 Parameter count is invalid. expected: %1$d, actual: %2$d
889 Stored procedure execute error: %1$s
890 Partition failed.
891 Appropriate partition does not exist.
892 Primary key "%2$s" already defined for class "%1$s".
893 Attribute '%1$s', a member of the primary key, could not be dropped.
894 Stored procedure/function '%1$s' does not exist.
895 Invalid stored procedure type: '%1$s' is the '%2$s'.
896 Compression failed. Zip Method: %1$d (%2$s), Zip Level: %3$d (%4$s)
897 Decompression failed.
898 Replication error: "%1$s"
899 Invalid partition requests.
900 Java VM library is not found: %1$s.
901 Java VM can not be started: %1$s.
902 Java VM is not running.
903 Can't connect Java VM: %1$s
904 Invalid stored procedure name.
905 Networking with JVM failed: %1$d
906 Invalid java method signature.
907 The owner or a member of DBA group can drop stored procedure.
908 Too many arguments of stored procedure '%1$s'.
909 Missing or invalid catalog class/vclass is found.
910 Not allowed access to partition.
911 Invalid call: it can not return ResultSet.
912 ResultSet can not be used input parameter.
913 Too many nested stored procedure call.
914 A serial object already exists as an auto increment constraint.
915 Increase value of an auto increment constraint should be greater than 0.
916 Active log volume is not included in this backup volume(%1$s).
917 Active log volume already exists.
918 The class '%1$s' referred by the foreign key does not exist.
919 The class '%1$s' referred by the foreign key does not have the primary key.
920 The foreign key '%1$s' does not include the primary key member '%2$s'.
921 The domain of the foreign key member '%1$s' is different from that of the primary key member '%2$s'.
922 The constraint of the foreign key '%1$s' is invalid, due to value '%2$s'.
923 The primary key '%1$s' referred by a foreign key '%2$s' is not supposed to be dropped.
924 Update/Delete operations are restricted by the foreign key '%1$s'.
925 Acquiring lock for the foreign key '%1$s' has failed.
926 The instance having the foreign key '%1$s' cannot be dropped.
927 The number of keys of the foreign key '%1$s' is different from that of the primary key '%2$s'.
928 Not a user but the system can assign the attribute '%1$s' created by 'ON CACHE OBJECT' of foreign key.
929 A foreign key cannot be defined in virtual class.
930 It is able to drop the attribute '%1$s' created by 'ON CACHE OBJECT' of foreign key only after removing the constraint of the foreign key.
931 Start value of an auto increment constraint should be less then max value.
932 It is not able to repair an inconsistency of the number of free pages for the volume "%1$s". Because %2$d pages obtained from the volume header is inconsistent with %3$d pages from the bitmap.
933 It is not able to repair an inconsistency of the number of free sectors for the volume "%1$s". Because %2$d sectors obtained from the volume header is inconsistent with %3$d sectors from the bitmap.
934 Null domain referenced.
935 Argument of %1$s function is out of range.
936 Overflow occurred in exp context.
937 Database error.
938 Invalid argument.
939 There are too many connection.
940 The handle is invalid.
941 The operation is not supported.
942 Handle timed out waiting for locks.
943 Generic error.
944 The statement is not prepared.
945 The statement has no result set.
946 The statement is not executed.
947 There is no more result set.
948 The size of buffer is not enough.
949 There is no available information.
950 The name not valid.
951 The resultset is not updatable.
952 The row is deleted.
953 Parameters of the prepared statement is not set.
954 The statement is not batch statement.
955 Can not clear this batch statement.
956 Can not batch execute.
957 The statement is prepared statement.
958 The statement is not prepared statement.
959 The statement is batch statement.
960 There is no more error.
961 There is no more result.
962 Out of memory
963 Broker error.
964 The result set handle is closed.
965 Cannot use duplicate attribute names in index key list. Attribute name '%1$s' listed more than once.
966 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on object %6$d|%7$d|%8$d because of deadlock. You are waiting for user(s) %9$s to finish.
967 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on class %6$s because of deadlock. You are waiting for user(s) %7$s to finish.
968 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on instance %6$d|%7$d|%8$d of class %9$s because of deadlock. You are waiting for user(s) %10$s to finish.
969 Server bit platform (%1$d) is different from client bit platform (%2$d).
970 Server HA mode is changed from '%1$s' to '%2$s'.
971 Program '%1$s' (pid %2$d) connected to database server '%3$s' on the host '%4$s' (port %5$d).
972 Program '%1$s' (pid %2$d) was connected from the host '%3$s'. (transaction index %4$d)
973 Server status is %1$s.
974 Archive log "%1$s" is created to archive pages from %2$lld to %3$lld.
975 Current version of replication does not allow changing multiple rows with a single UPDATE statement which can violate the UNIQUE constraint.
976 Internal error: Table size overflow (allocated size: %1$d, accessed size: %2$d) at file table page %3$d|%4$d(volume "%5$s")
977 Checkpoint started (previous checkpoint page id: %1$lld, previous redo page id: %2$lld).
978 Checkpoint finished (checkpoint page id: %1$lld, checkpoint redo page id: %2$lld, flushed page count: %3$d).
979 CONNECT BY loop in user data.
980 A thread is waiting in %1$s more than %2$d msec.
981 Transaction %1$d has reached to the threshold of monitoring statistics %2$s.
982 Set interrupt to the transaction %1$d.
983 Instances of a reusable OID class are non-referable. This operation is not permitted on non-referable instances.
984 Cannot set %3$s to "%6$s" in the "[%5$s]" section when it is set to "%4$s" in the "[%2$s]" section of the configuration file "%1$s".
985 The hostname on the database connection string should be specified when multihost is set in "databases.txt".
986 CUBRID heartbeat feature started. 
987 CUBRID heartbeat feature stopped.
988 Node event: %1$s.
989 Process event: %1$s. %2$s
990 Command execution: %1$s. %2$s
991 Flush victim candidates of page buffer started.
992 Flush victim candidates of page buffer finished (count: %1$d).
993 Overflow happened in bit operation context.
994 This statement cannot be a prepared statement.
995 A prepared statement with the name %1$s does not exist.
996 Incorrect number of values given. %1$d values were given but %2$d values are required.
997 Cannot add the foreign key constraint %1$s to the partitioned %2$s. Referential actions that update attributes are not yet supported on partitioned classes.
998 Cannot add the foreign key constraint %1$s on the attribute %2$s. The attribute cannot have a NOT NULL constraint because it has to match the SET NULL referential action.
999 Cannot create table '%1$s' like table '%2$s' because the existing table has features that will not be duplicated (%3$s).
1000 '%1$s' is a CLASS or SHARED attribute but only normal attributes are allowed in this context.
1001 An unexpected condition or state has been reached. '%1$s'
1002 The current transaction could not acquire a database lock it requires to continue processing.
1003 The constraint '%1$s' exists but has a different type.
1004 Please note that when using ALTER TABLE DROP FOREIGN KEY, CUBRID drops both the constraint and its associated index.
1005 Unused error code.
1006 Unused error code.
1007 Cannot create index with prefix length on unique, foreign key or primary key constraints.
1008 Cannot create index with prefix length on partitioned class.
1009 COMPACTDB already started.
1010 Cannot define index with prefix length on domain "%1$s".
1011 Cannot add the foreign key constraint %1$s on the shared attribute %2$s.
1012 Cannot create table with NULL column type.
1013 Set DEFAULT or SHARED on %1$s typed column is not allowed.
1014 Set NOT NULL on %1$s typed column is not allowed.
1015 Specified directory '%1$s' does not exist.
1016 %1$s external storage error: %2$s
1017 Path for external storage '%1$s' is invalid.
1018 Cannot copy the external file to the different type of storage. (source %1$s dest %2$s)
1019 External storage is not initialized because the path is not specified in "databases.txt".
1020 External file "%1$s" was not found.
1021 A deadlock cycle is detected. %1$s.
1022 Invalid value "%1$d" for prefix length of the index.
1023 Reserved error code for HA.
1024 log writer: failed to get log page(s) starting from page id %1$lld.
1025 HA delay info: The replication delay (%1$d secs) has exceeded the limit (%2$d secs).
1026 HA delay info: The replication delay (%1$d secs) is decreased below the acceptable level (%2$d secs).
1027 log applier: failed to change apply state from '%1$s' to '%2$s'.
1028 log applier: unexpected EOF record in archive log. LSA: %1$lld|%2$d.
1029 log applier: invalid replication log page/offset. page HDR: %1$1lld|%2$d, final: %3$lld|%4$d, append LSA: %5$lld|%6$d, EOF LSA: %7$lld|%8$d, ha file status: %9$d, is end-of-log: %10$d.
1030 log applier: invalid replication record. LSA: %1$lld|%2$d, forw LSA: %3$lld|%4$d, backw LSA: %5$lld|%6$d, Trid: %7$d, prev tran LSA: %8$lld|%9$d, type: %10$d.
1031 log applier: failed to apply schema replication log. class: "%1$s", schema: "%2$s", server error: %3$d, %4$s. 
1032 log applier: failed to apply insert replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1033 log applier: failed to apply update replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1034 log applier: failed to apply delete replication log. class: "%1$s", key: "%2$s", server error: %3$d.  
1035 log applier: mem size(%1$d MB) of log applier is greater than max mem size (%2$d MB) or has been grow more than 2 times (%3$d MB). required LSA: %4$lld|%5$d. last committed LSA: %6$lld|%7$d.
1036 log applier: log applier shut itself down by signal.
1037 log writer: log writer shut itself down by signal.
1038 log applier: log applier started. required LSA: %1$lld|%2$d. last committed LSA: %3$lld|%4$d. last committed repl LSA: %5$lld|%6$d.
1039 log writer: log writer started. mode: %1$d.
1040 HA generic: %1$s. 
1041 A string was truncated in context of "%1$s".
1042 Trying to create a string requiring %1$d bytes of memory, while the maximum allowed is %2$d bytes.
1043 Cannot change attribute "%1$s". CUBRID cannot change the requested attribute's properties if the class has subclasses.
1044 Cannot change attribute "%1$s". CUBRID cannot change attributes that are partitioning keys.
1045 Cannot change attribute "%1$s". CUBRID cannot change attributes used in foreign keys (either referenced or referencing).
1046 Cannot change attribute "%1$s". CUBRID cannot change the attribute's domain. This change of domain is not supported.
1047 Cannot change attribute "%1$s". CUBRID cannot change the attribute's domain. The new domain may not correctly represent all the existing attribute values.
1048 Cannot change attribute "%1$s". CUBRID cannot currently change an attribute's AUTO_INCREMENT if the attribute's domain also changes.
1049 Cannot change attribute "%1$s". CUBRID cannot currently change the attribute's domain. The domain conversion is possible but this feature is not allowed by configuration.
1050 Cannot change attribute "%1$s". CUBRID cannot currently change the attribute's domain. The attribute has an index on it.
1051 Cannot change attribute "%1$s". CUBRID cannot currently change the attribute to add a constraint. Existing attribute values may not check the constraint.
1052 Attribute "%1$s" was not changed. CUBRID did not detect any effective changes in the attribute representation.
1053 The statements REPLACE INTO... and INSERT ... ON DUPLICATE KEY UPDATE are not allowed for classes that are part of an inheritance chain.
1054 The new value of an auto increment constraint should be greater than or equal to the current value.
1055 The new value of an auto increment constraint should be lower than the maximum value.
1056 To avoid ambiguity, the AUTO_INCREMENT table option requires the table to have exactly one AUTO_INCREMENT column and no seed/increment specification.
1057 Cannot add a NOT NULL '%1$s' typed attribute without a DEFAULT value.
1058 ALTER TABLE .. CHANGE : changing to new domain : cast failed, current configuration doesn't allow truncation or overflow.
1059 ALTER TABLE .. CHANGE : changing to new domain : cast failed, default value of new domain was set.
1060 ALTER TABLE .. CHANGE : changing to new domain : cast failed, minimum value of new domain was set.
1061 ALTER TABLE .. CHANGE : changing to new domain : cast failed, maximum value of new domain was set.
1062 Cannot change attribute "%1$s". The change would result in multiple primary keys being defined.
1063 Cannot add NOT NULL constraint for attribute "%1$s": there are existing NULL values for this attribute.
1064 ALTER TABLE .. CHANGE : adding NOT NULL : all existing NULL values were changed to domain's hard default.
1065 Cannot change attribute "%1$s". CUBRID cannot add NOT NULL constraint : a hard default value doesn't exist for the new domain.
1066 Session expired.
1067 Column '%1$s' cannot have a NOT NULL constraint and a NULL default value.
1068 Maximum trigger depth cannot exceed %1$d.
1069 Too many prepared statements.
1070 Session variable '@%1$s' not defined.
1071 Too many session variables.
1072 IP address(%1$s) is not authorized.
1073 Invalid format in file(%1$s).
1074 Cannot read access list file(%1$s).
1075 Descending index scan aborted because of lower priority on B+tree with index identifier: (vfid = (%1$d, %2$d), rt_pgid: %3$d).
1076 Could not load system parameter.
1077 The '%1$s' parameter at line %2$d in file '%3$s' : Unknown parameter
1078 Character at offset %1$d is invalid with current codeset.
1079 Codesets of database(%1$s) and server configuration(%2$s) are incompatible.
1080 The maximum length of filter predicate string must be %1$d.
1081 Cannot alter an attribute involved in a filter or function index.
1082 All page buffers are dirty (LRU: %1$d).
1083 Specific information about deadlock.\n%1$s
1084 Skip invalid page in checkpoint. (page id: %1$d, "%2$s", oldest_unflush_lsa: %3$lld|%4$d, previous checkpoint redo lsa: %5$lld|%6$d)
1085 Cannot compile regular expression: '%1$s'
1086 Cannot execute regular expression: '%1$s'
1087 Backup active log '%1$s' started.
1088 Backup active log '%1$s' finished.
1089 Resources allocation failed. Raise up 'max_bestspace_entries=%1$d'.
1090 Locale initialization: %1$s.
1091 Locale generation: %1$s.
1092 Cannot prepare statement of type '%1$s' with host variables.
1093 The arg '%1$s' for inet_aton is not a valid string formatted ipv4 address.
1094 The arg '%1$lld' for inet_ntoa is not a valid number formatted ipv4 address.
1095 User name is too long.
1096 Altering partitioning schema is not allowed when the partitioned class is referenced by a foreign key.
1097 Column '%1$s' cannot have a PRIMARY KEY constraint and a NULL default value.
1098 Locales for language '%1$s' are not available with charset '%2$s'.
1099 DDL statement is not allowed by configuration (block_ddl_statement=yes).\n SQL Text: %1$s
1100 Statement without WHERE clause is not allowed by configuration (block_nowhere_statement=yes).
1101 Unused error code.
1102 Unknown system parameter or bad value.
1103 Cannot change system parameter.
1104 Slow query (%1$d msec)\n%2$s
1105 Internal error: partially failed to flush dirty object. 
1106 Multiple rows in source table match the same row in destination table.
1107 Invalid bucket number for NTILE. Should be greater than zero and no more than 2,147,483,647.
1108 Invalid bucket number for WIDTH_BUCKET. Should be greater than zero and less than 2,147,483,647.
1109 Invalid value for partition definition.
1110 Insufficient space in btree leaf page. (key len: %1$d)\n%2$s
1111 Result value is truncated, because the precision(%1$d) is less than it's length(%2$d).
1112 A character could not be converted from '%1$s' to '%2$s'.
1113 Element index out of range.
1114 Argument '%1$s' is out of range.
1115 Started to update statistics (class "%1$s", oid : %2$d|%3$d|%4$d).
1116 Finished to update statistics (class "%1$s", oid : %2$d|%3$d|%4$d, error code : %5$d).
1117 Cannot change attribute "%1$s". CUBRID cannot change an attribute as a SHARED and vice versa.
1118 The argument of "%1$s" can not be coerced to desired domain "%2$s".
1119 Internal error: Page id %1$d is allocated according to the allocation map of volume "%2$s", but it does not belong to any file. 
1120 Internal error: Page id %1$d of volume "%2$s" is currently being used, but it is not allocated according to the allocation map of volume.
1121 NOT NULL constraints do not allow NULL value.
1122 Time out in receiving data.
1123 pthread_cond_timedwait() timed out. 
1124 Query execution error. ERROR_CODE = %1$d, /* SQL_ID: %2$s */ %3$s
1125 Create the overflow key file. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1126 Create a new overflow page. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1127 Delete an empty overflow page. INDEX %1$s%2$s ON CLASS %3$s%4$s. key: %5$s%6$s.
1128 Log recovery is started. The number of log records to be applied: %1$lld. Log page: %2$lld ~ %3$lld.
1129 Log recovery is finished.
1130 Transaction is aborted because transaction lock counts exceeds %1$d.
1131 Internal error: The page %1$d of volume "%2$s" may be corrupted.\n%3$s
1132 Encryption library failure: %1$s
1133 Copylogdb for %1$s already exists.
1134 Applylogdb for %1$s already exists.
1135 A previous link of B+tree(%1$s) is corrupted.: vpid = (%2$d, %3$d)
1136 A previous link of B+tree(%1$s) is repaired.: vpid = (%2$d, %3$d)
1137 The query has been rejected due to attempt to exceed the maximum allowed nesting level(%1$d).
1138 Handshake error (peer host %1$s): incompatible interruptibility. (client: %2$s, server: %3$s)
1139 Handshake error (peer host %1$s): incompatible read/write mode. (client: %2$s, server: %3$s)
1140 Handshake error (peer host %1$s): HA replication delayed.
1141 Handshake error (peer host %1$s): replica-only client to non-replica server.
1142 Handshake error (peer host %1$s): unidentified server version.
1143 Handshake error (peer host %1$s): remote access to server not allowed.
1144 Timed out attempting to connect to %1$s. (timeout: %2$d sec(s))
1145 To change the owner of a system class is not allowed.
1146 volume identifier %1$d does not exist.
1147 The size of XASL stream cache(%1$d) exceeds the max(%2$d). New entry is added although nothing is victimized because all cache entries are used.
1148 The size of filter predicate stream cache(%1$d) exceeds the max(%2$d). New entry is added although nothing is victimized because all cache entries are used.
1149 Cannot find the page %1$d of volume %2$d.
1150 Context requires compatible collations.
1151 The page %1$d of volume %2$d is not a slotted page.
1152 The owner or a member of DBA group is only allowed to kill the transaction (%1$d).
1153 Attempt to use 'zero date'.
1154 MVCC row %1$d|%2$d|%3$d does not satisfies reevaluation.
1155 MVCC invalid row for delete: %1$d|%2$d|%3$d.
1156 MVCC can't get snapshot
1157 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.
1158 Serializable conflict due to concurent updates
1159 Last Error

$set 6 MSGCAT_SET_INTERNAL
1 Error in error subsystem (line %1$d):
2 No error message available.
3 Can't allocate %1$d bytes.
4 Can't find message catalog "%1$s".
5 Missing message for error code %1$d.
6 ... ABORT/EXIT IMMEDIATELY ...\n
7 Do you want to exit? 1/0
8 Unknown file.
9 No message available; this message substituted for erroneously formatted original.
10 er_init: *** Incorrect exit_ask value = %1$d; will assume %2$s instead. ***\n
11 er_start: *** WARNING: Unable to open message log "%1$s"; will assume stderr instead. ***\n
12 er_study_fmt: suspect message for error code %1$d.
13 er_estimate_size: unknown conversion code (%1$d).
14 \n\n*** Message log wraparound. Messages will continue at top of the file. ***\n\n
15 \nTime: %1$s - %2$s *** %3$s CODE = %4$d, Tran = %5$d%6$s\n%7$s\n
16 CUBRID (pid: %1$d) *** %2$s *** %3$s CODE = %4$d, Tran = %5$d %6$s
17 \nTime: %1$s - %2$s *** file %3$s, line %4$d %5$s CODE = %6$d Tran = %7$d%8$s\n%9$s\n
18 CUBRID (pid: %1$d) *** %2$s *** file %3$s, line %4$d, %5$s CODE = %6$d, Tran = %7$d. %8$s
19 \n*** The previous error message is the last one. ***\n\n
20 \nTime: %1$s - DEBUG *** file %2$s, line %3$d\n
21 Mail -s "Server has been stopped"
22 --->>>\n %1$s has been stopped at your request when the following error was set:\n errid = %2$d, %3$s\nUser: %4$s, pid: %5$d, host: %6$s, time: %7$s<<<---\n
23 %1$s has been stopped on errid = %2$d. User: %3$s, pid: %4$d
24 er_init: cannot install event handler "%1$s"\n

$set 7 MSGCAT_SET_PARSER_SYNTAX
1 please check syntax at '%1$s', expecting %2$s.
2 please check syntax at '%1$s', expecting %2$s in %3$s.
3 please check correctness of token '%1$s'.
4 'read uncommitted schema' is not acceptable, using \n'read committed schema' instead.
5 Maximum char string length is 16383.
6 %1$s exceeds limit of %2$s.
7 Maximum bit string length is %1$d bits.
8 Cannot use a subquery in join condition clause.
9 Cannot specify attribute "(%1$s)" in update privilege clause of grant/revoke statement.
10 invalid use of keyword '%1$s', expecting %2$s.
11 Token length is too long.
12 invalid data type
13 invalid attach statement\n  attach_statement: ATTACH unsigned_integer
14 invalid prepare statement\n  prepare_statement:  PREPARE {to} COMMIT unsigned_integer
15 invalid execute statement\n  execute_statement:  EXECUTE DEFERRED TRIGGER [identifier {, identifier}* | ALL TRIGGERS]
16 invalid scope statement\n  scope_statement: SCOPE trigger_action { FROM table_spec_list }
17 invalid set system parameter\n  set_system: SET SYSTEM PARAMETERS 'STRING' {, 'STRING_N'}*
18 invalid set transaction\n  set_transaction: SET TRANSACTION ISOLATION LEVEL expression {, expression}\n                 | SET TRANSACTION LOCK TIMEOUT timeout_spec
19 invalid set trigger trace\n  set_trigger: SET TRIGGER TRACE [ON|OFF|unsigned_integer|parameter|host_param]
20 invalid set trigger depth\n  set_trigger: SET TRIGGER {MAXIMUM} DEPTH [unsigned_integer|parameter|host_param]
21 invalid set optimization level\n  SET OPTIMIZATION LEVEL {=|to} [ON|OFF|unsigned_integer|parameter|host_param]
22 invalid set optimization cost\n  SET OPTIMIZATION COST {of} 'STRING' {=|to} literal_
23 invalid get optimization level\n  GET OPTIMIZATION LEVEL {[INTO|TO] [host_param|parameter|identifier]}
24 invalid get optimization cost\n  GET OPTIMIZATION COST {of} 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
25 invalid get transaction isol\n  GET TRANSACTION ISOL LEVEL {[INTO|TO] [host_param|parameter|identifier]}
26 invalid get transaction lock\n  GET TRANSACTION LOCK TIMEOUT {[INTO|TO] [host_param|parameter|identifier]}
27 invalid get trigger trace\n  GET TRIGGER TRACE {[INTO|TO] [host_param|parameter|identifier]}
28 invalid get trigger depth\n  GET TRIGGER {MAXIMUM} DEPTH {[INTO|TO] [host_param|parameter|identifier]}
29 invalid get stat\n  GET STATISTICS 'STRING' {[INTO|TO] [host_param|parameter|identifier]}
30 invalid create index\n  CREATE {REVERSE} {UNIQUE} INDEX identifier ON {only} class_name index_column_name_list {WHERE where_clause}
31 invalid create user\n  CREATE USER identifier {PASSWORD 'STRING'} {GROUPS identifier {, identifier}* } {MEMBERS identifier {, identifier}* }
32 invalid create trigger\n  CREATE TRIGGER identifier {STATE [ACTIVE|INACTIVE]} {PRIORITY unsigned_real}\n    [BEFORE|AFTER|DEFERRED] event_spec {IF trigger_condition}\n    EXECUTE {AFTER|DEFERRED} trigger_action
33 invalid create serial\n  CREATE SERIAL identifier {START WITH integer} {INCREMENT BY integer}\n    serial_min_max {CYCLE|NOCYCLE} {CACHE unsigned_integer|NOCACHE}
34 invalid create procedure\n  CREATE PROCEDURE identifier '(' {sp_param_list} ')' {IS|AS} LANGUAGE JAVA NAME 'STRING'
35 invalid create function\n  CREATE FUNCTION identifier '(' {sp_param_list} ')' RETURN {data_type|CURSOR} {IS|AS} LANGUAGE JAVA NAME 'STRING'
36 invalid groups\n  groups spec: GROUPS identifier {, identifier}*
37 invalid members\n  member spec: MEMBERS identifier {, identifier}*
38 invalid password\n  password spec: PASSWORD 'STRING'
39 missing authorized command list\n  authorized command = SELECT, INSERT, INDEX, DELETE, UPDATE, ALTER, ADD, DROP, EXECUTE, REFERENCES, ALL PRIVILEGES, ALL 
40 missing class spec list
41 missing identifier list
42 missing expression
43 invalid identifier
44 invalid use of substring\n  SUBSTRING( expression FROM expression FOR expression )\n  SUBSTRING( expression FROM expression )
45 invalid use of trim\n  TRIM ( [LEADING|TRAILING|BOTH] expression FROM expression )\n  TRIM ( [LEADING|TRAILING|BOTH] FROM expression )\n  TRIM ( expression FROM expression )\n  TRIM ( expression )
46 invalid use of cast\n  CAST ( expression AS data_type )
47 function add_months needs two parameters
48 octet_length needs one parameter
49 bit_length needs one parameter
50 lower needs one parameter
51 upper needs one parameter
52 translate needs three parameters
53 replace needs two or three parameters
54 invalid use of to_number\n  TO_NUMBER ( expression , char_string )\n  TO_NUMBER ( expression )
55 string bind operator is not '|' but '||'
56 invalid alter statement
57 invalid drop statement
58 invalid create statement
59 invalid relational operator
60 equal operator is not '==' but '='
61 not equal operator is not '!=' but '<>'
62 invalid minus value (or minus value in list)
63 duplicated reference rule
64 SYS_CONNECT_BY_PATH needs two parameters and the second must be a string literal.
65 Invalid unsigned integer %1$s.
66 IF needs three parameters: the first must be a logical expression, the second and third must be expressions evaluating to either a numeric or a string type.
67 IFNULL needs two parameters which must be expressions evaluating to either a numeric or a string type.
68 invalid use of isnull\n  ISNULL ( expression )
69 invalid use of left\n  LEFT ( str , len )
70 invalid use of right\n  RIGHT ( str , len )
71 invalid use of mod\n MOD( expression, expression )
72 invalid use of adddate\n ADDDATE( expression, expression )
73 invalid use of date_add\n DATE_ADD( expression, INTERVAL expression unit)
74 invalid use of subdate\n SUBDATE( expression, expression )
75 invalid use of date_sub\n DATE_SUB( expression, INTERVAL expression unit)
76 invalid use of user / system_user\n USER() / SYSTEM_USER()
77 invalid use of timestamp\n TIMESTAMP( expression ), TIMESTAMP( expression, expression )
78 invalid use of database\n DATABASE()
79 invalid use of current_time\n CURRENT_TIME()
80 invalid use of current_date\n CURRENT_DATE()
81 invalid use of current_timestamp\n CURRENT_TIMESTAMP()
82 invalid use of now\n NOW()
83 invalid use of default\n DEFAULT(column_name)
84 invalid use of date\n DATE(expression)
85 invalid use of localtime\n LOCALTIME()
86 invalid use of localtimestamp\n LOCALTIMESTAMP()
87 invalid use of schema\n SCHEMA()
88 invalid use of truncate\n TRUNCATE( expression, expression )
89 invalid use of str_to_date\n STR_TO_DATE( expression, string_literal )
90 invalid use of current_datetime\n CURRENT_DATETIME()
91 invalid use of insert\n INSERT( expression, expression , expression, expression )
92 invalid use of elt\n ELT(expression [, expression]...)
93 invalid use of year\n YEAR( expression )
94 invalid use of month\n MONTH( expression )
95 invalid use of day\n DAY( expression )
96 invalid use of hour\n HOUR( expression )
97 invalid use of minute\n MINUTE( expression )
98 invalid use of second\n SECOND( expression )
99 invalid use of quarter\n QUARTER( expression )
100 invalid use of week\n WEEK( expression ), WEEK( expression, expression)
101 invalid use of time\n TIME(expression)
102 invalid use of group_concat\n GROUP_CONCAT (([DISTINCT] {col | expr} \n [ORDER BY {col | unsigned_int} [ASC | DESC] ] \n [SEPARATOR str_val])
103 invalid use of index hint
104 Maximum byte string length is %1$d bytes.
105 Syntax error: unexpected
106 , expecting '{'
107 , expecting SELECT or '('
108 , expecting
109 unexpected
110 END OF STATEMENT
111 In line %1$d, column %2$d before '%3$c%4$s'\n
112 In line %1$d, column %2$d before %3$s\n
113 before ' %1$s'\n
114 before END OF STATEMENT\n
115 invalid set names\n  set_names: SET NAMES charset [COLLATE collation]\n
116 invalid use of chr\n  CHR ( number [using charset] )\n
117 invalid use of charset\n  CHARSET ( expression )\n
118 invalid use of collation\n  COLLATION ( expression )\n
119 invalid use of clob_to_char\n  CLOB_TO_CHAR ( expression [using charset] )\n
120 invalid use of index_prefix\n  INDEX_PREFIX ( expression, expression, expression )\n
121 Incorrect usage of UNION and ORDER BY
122 Expecting ON. Outer Joins require join condition.

$set 8 MSGCAT_SET_PARSER_SEMANTIC
1 Out of memory.
2 Function %1$s is undefined.
3 Cannot find ldb '%1$s' in the database.
4 The number of columns, %1$d, in the left query do not match\nthe number of columns, %2$d, in the right query.
5 '%1$s' is not union compatible with '%2$s'.
6 %1$s is not defined.
7 The domain of %1$s cannot be a vclass.
8 The domain of %1$s cannot be a proxy vclass.
9 The domain of %1$s cannot be a class.
10 %1$s is not an attribute of %2$s.
11 %1$s cannot be changed from this class.\nIt was inherited from %2$s.
12 Class %1$s does not exist.
13 %1$s is not a %2$s.
14 %1$s is not a method or attribute of %2$s.
15 %1$s does not have and must not have any query specifications.
16 %1$s is not defined in the database.
17 %1$s is not a superclass of %2$s.
18 Class %1$s inherits from non-class %2$s.
19 VClass %1$s inherits from non-vclass %2$s.
20 Proxy vclass %1$s inherits from non-proxy vclass %2$s.
21 %1$s is already a superclass of %2$s.
22 %1$s may not be a superclass of itself.
23 %1$s is already a subclass of %2$s.
24 %1$s is not single valued. Attributes exposed in\naggregate queries must also appear in the group by clause.
25 The number of columns(%1$d) and into_names(%2$d) are not equal.
26 Class %1$s already exists.
27 Super class %1$s in '%2$s' does not exist.
28 Expecting a SUBCLASS clause to make INHERIT clause '%1$s' meaningful.
29 INHERIT clause '%1$s' references a class that is not in the SUBCLASS clause '%2$s'.
30 Class CANNOT have a query specification.
31 %1$s is not a registered ldb.
32 A proxy heir may not have its own object_id clause.  It cannot override its inherited object_id attribute(s).
33 Proxy vclass has more than one query specification.
34 The number of attributes, %1$d, does not match the number of columns, \n%2$d, in the query specification.
35 Missing an expected attribute name for %1$s.
36 Attribute '%1$s' is not compatible with column '%2$s' of the query specification.
37 %1$s is not a class.
38 Indices may not be created on virtual classes.
39 Indices can only be created on a single attribute.
40 Illegal use of multiple classes in the from clause.
41 System error -- Resolution failed on %1$s.
42 You may not delete from a derived table.
43 User %1$s is not in the database.
44 Update cannot have aggregation.
45 Attribute names in each class must be unique. Attribute name '%1$s' in class '%2$s' is specified more than once.
46 There are multiple assignments to %1$s.%2$s.
47 Number in sort spec is not an integer.
48 %1$d in sort spec is out of range.
49 Order by item is not an integer.
50 %1$s in sort spec doesn't match any select column.
51 There are conflicting sort directions for column %1$s.
52 %1$s in sort spec is not a number, name or path expression.
53 Insert expression may only appear in insert statement.
54 The number of attributes(%1$d) and select columns(%2$d) are not equal.
55 The number of attributes(%1$d) and values(%2$d) are not equal.
56 Data overflow on data type %1$s.
57 Incompatible data type on attribute %1$s.
58 %1$s\n is not a SELECT from a single class.
59 `*' is not a valid select list in a proxy vclass query specification.
60 %1$s is neither a vclass nor a proxy vclass.
61 There are more than one isolation level clauses.
62 There are more than one timeout clauses.
63 No isolation level was entered.
64 BETWEEN operator is not defined on types %1$s, %2$s and %3$s.
65 '%1$s' operator is not defined on types %2$s and %3$s.
66 Function must have arguments: %1$s.
67 Incompatible operands: %1$s(%2$s).
68 Aggregate function must have 1 argument: %1$s.
69 Attempt to divide by zero.
70 Data overflow coercing %1$s to type %2$s.
71 Cannot coerce %1$s to type %2$s.
72 The type of '%1$s' must be SET, MULTISET, or SEQUENCE.
73 '%1$s' should have exactly one set element type.
74 %1$s has more than one derived column name.
75 %1$s has no derived column name.
76 %1$s does not have a corresponding number of columns (%2$d) and column names (%3$d).
77 There are no attributes in %1$s.
78 Expecting single table in %1$s.
79 Exposed name %1$s is ambiguous.
80 Derived column names '%1$s' are not allowed for simple class specifications.
81 Derived table '%1$s' must have a correlation name.
82 Select authorization failure, class %1$s.
83 Class instance reference %1$s to a derived table name is not allowed.
84 Path correlation (%1$s) appears outside an inner path term.
85 Reference to %1$s is ambiguous.
86 Class of %1$s does not have attribute %2$s.
87 Classes (%1$s) do not all have attribute %2$s.
88 Class (%1$s) does not have attribute %2$s.
89 %1$s is a %2$s type, not an object type.
90 Selector {%1$s} does not resolve to an exposed class specification in\n the required context (%2$s).
91 Selector {%1$s} refers to non-class specification (%2$s).
92 Selector {%1$s} cannot root the path expression defining itself.
93 Selector {%1$s} is circularly defined in terms of selector {%2$s}.
94 Selector {%1$s} is redefined at %2$s.
95 %1$s (domain object) does not have attribute %2$s.
96 %1$s in EXCEPT spec is not a subclass.
97 ALL ... EXCEPT list excludes EVERYTHING in class.
98 '%1$s' is inconsistent with\nother use of path selector '%2$s' or of path root '%3$s'.
99 Undefined argument.
100 Argument is not an object.
101 %1$s is not a class method or attribute of %2$s.
102 Query specification must not have an into-clause.
103 Proxy query specification must have no aggregate expression,\nno group-by-clause, and no having-clause.
104 Proxy query specification must not have the DISTINCT nor UNIQUE \nkeyword in the select list.
105 Proxy query specification must be a simple SELECT statement.
106 Proxy query specification must reference only one table or class \nfrom the local database.
107 User '%1$s' is not the owner of %2$s.
108 Proxy '%1$s' is already defined on class '%2$s' of ldb '%3$s'.
109 CUBRID does not support proxy's with objects in heterogeneous sets.
110 Where-clause must be a logical type expression.
111 Like escape clause must be a literal string or a host variable.
112 Invalid combination of precision (%1$d) and scale (%2$d).
113 Given precision of %1$d is invalid; it should be greater than %2$d and no greater than %3$d.
114 Precision (%1$d) too large. Maximum precision is %2$d.
115 Method %1$s must have a return type.
116 Number of arguments supplied to the method does not equal the \nnumber defined for the method.
117 Method call must have a target.
118 Method %1$s is undefined for the supplied target.
119 Instance methods cannot be called on class objects.
120 Class methods cannot be called on instance objects.
121 Class methods require a class object as their target.
122 Methods require an object as their target.
123 Method %1$s is not fully typed, check method definition in class schema.
124 Time arithmetic underflow.
125 Date arithmetic underflow.
126 Illegal left hand side of an assignment clause.
127 Method calls on set derived tables correlated to an outer scope are \nnot supported unless the set expression is wrapped in a subquery.
128 The query specification column that corresponds to the \nshared attribute '%1$s' must be NULL or NA.
129 A NULL column is not allowed on the select list of the view query specification if the attribute definition is omitted.
130 Vclass objects in sets are not supported.
131 Expecting a string value.
132 Expecting a statement or a string value.
133 Proxy object_id attribute '%1$s' must not map to \na query specification column expression.
134 %1$s constraint ignored.
135 Constraint declarations not allowed in this context.
136 Casting %1$s to type %2$s is not supported.
137 %1$s is not a valid bit string.
138 '%1$s' operator is not defined on type %2$s.
139 '%1$s' operator is not defined on types %2$s with %3$s and %4$s.
140 Operands of operator '%1$s' cannot both be dynamic host variables.
141 Too many arguments to generic function %1$s.\nOnly %2$d arguments are allowed for generic functions.
142 Selector {%1$s} class '%2$s' is not a subclass of '%3$s'.
143 Cannot extract '%1$s' field from type %2$s.
144 Query specification must not have an ORDER BY clause.
145 Case condition must be a logical type expression.
146 Expression should be of type %1$s.
147 Serial '%1$s' already exist.
148 Serial '%1$s' not defined.
149 Increment value must not be zero.
150 Maximum value exceeds 1.0e37.
151 Minimum value is invalid.
152 Increment value is invalid.
153 Minimum value exceeds -1.0e36.
154 Maximum value is invalid.
155 Specify alter serial option.
156 The second argument of serial_next_value function should be greater than 0.
157 Serial values cannot exceeds its minimum or maximum
158 Internal I/O error for serial
159 Cannot find the index '%1$s'.
160 The qualified index name '%1$s' refers to a class, but it isn't specified in from clause.
161 Two or more classes have the same index '%1$s'. Use the qualified index names.
162 Invalid use of %1$s.\n%2$s expression cannot be used with attribute or correlated subquery.
163 Current release restricts a expression containing ORDERBY_NUM() in select list.
164 Path expression '%1$s' in join condition.
165 '%1$s' in join condition is not defined.
166 Current release restricts set derived table for right outer join.
167 Cannot insert with query on other ldb.
168 Only one column can be specified in the select list when the subquery is not introduced with EXISTS.
169 Illegal right hand side of an assignment clause. Context requires query to be single column.
170 Cannot use a subquery in join condition clause.
171 Invalid partition definition.
172 Partition column must be specified.
173 Only one column can be specified as partition column.
174 Unsupported partition column type.
175 Partition definition is duplicated. '%1$s'
176 Constant type mismatch. '%1$s'
177 Invalid partition size.
178 '%1$s' is not partitioned class.
179 '%1$s' is already partitioned class.
180 Invalid partition requests.
181 %1$s cannot be changed from this class(partition key column).
182 %1$s cannot be partition-managed under active triggers.
183 Partition %1$s does not exist.
184 Partition type mismatches. '%1$s'
185 Partition information is invalid.
186 Partition addition is to set with a bigger range than the existing.
187 Reorganization of partitions is not permitted because it may cause data loss.
188 The range of the partition '%1$s' is invalid.
189 A class attribute cannot have an auto increment constraint.
190 An attribute of virtual class cannot have an auto increment constraint.
191 The domain of the attribute '%1$s' having an auto increment constraint is invalid.
192 Not a user but the system can assign the attribute '%1$s' created by 'ON CACHE OBJECT' of foreign key.
193 Cannot use duplicate attribute names in index key list. Attribute name '%1$s' listed more than once.
194 Function %1$s is undefined or given wrong number of parameter.
195 '%1$s' can only be used in hierarchical queries. Specify a CONNECT BY clause.
196 '%1$s' is not allowed here.
197 Using CONNECT_BY_ISCYCLE is only allowed if the query has a CONNECT BY NOCYCLE clause.
198 No tables used.
199 ORDER BY NULL requires GROUP BY.
200 The number to CACHE must be less than one cycle (%1$s).
201 '%1$s' is not allowed in a query with LIMIT clause.
202 WITH ROLLUP requires GROUP BY.
203 A column name parameter should be passed to the DEFAULT function. Path expressions are not yet supported.
204 Duplicate table option '%1$s'
205 The class '%1$s' is marked as REUSE_OID and is non-referable. Non-referable classes can't be the domain of an attribute and their instances' OIDs cannot be returned.
206 Duplicate 'serial %1$s' option.
207 The column '%1$s' in ordering clause is ambiguous.
208 Attribute '%1$s' doesn't have a default value.
209 ORDER BY items must appear in the select list if SELECT DISTINCT is specified.
210 Host variable is not allowed on columns in the query specification.
211 %1$s can not be an ORDER BY column
212 %1$s can not be a GROUP BY column
213 Can not define '%1$s of %2$s' as a column type.
214 Invalid value "%1$s" for prefix length of the index.
215 '%1$s' function is not defined on types %2$s, %3$s, %4$s and %5$s.
216 '%1$s' function is not defined on types %2$s, %3$s and %4$s.
217 '%1$s' function is not defined on types %2$s and %3$s.
218 '%1$s' function is not defined on type %2$s.
219 '%1$s' function is not defined for an index argument of type %2$s.
220 ALTER VIEW '%1$s' AS SELECT is not allowed for views that have subclasses or superclasses. Use ALTER VIEW CHANGE QUERY instead.
221 View attributes cannot be changed using this statement. 'ALTER VIEW name AS query' might help instead.
222 CUBRID requires ORDER BY item to be the same expression as GROUP_CONCAT argument or an integer (argument position).
223 The LIKE predicate requires an escape character to be specified. See the documentation for the '%1$s' and '%2$s' configuration parameters.
224 KEYLIMIT can not be used in INDEX NONE clause.
225 Session variable not allowed in this context.
226 Index hint conflict between '%1$s' and '%2$s'.
227 '%1$s' function can not be used in DEFAULT clause.
228 Updating derived tables is not allowed.
229 Updating class attributes and instance attributes not allowed in the same statement.
230 Updating virtual class hierarchies is not allowed.
231 Host variables not allowed in DDL statement.
232 Invalid filter expression (%1$s) for index.
233 Multiple or nested functions are not allowed in a function index definition.
234 Invalid index specification.
235 SHARED, DEFAULT and AUTO_INCREMENT cannot be defined with each other.
236 Too many enumeration values (%1$d). An enumeration type can have a maximum of %2$d values.
237 Duplicate values in enum type.
238 Cannot MERGE into a derived table.
239 Cannot affect the source table in a MERGE statement.
240 MERGE with class hierarchies is not allowed.
241 User name is too long.
242 Constant expression cannot be used as function index.
243 Class name or alias is not unique in list: '%1$s'
244 Invalid DEFAULT clause. '%1$s' cannot be used in a nested expression.
245 '%1$s' requires arguments with compatible collations.
246 Invalid character set.
247 Required character set and collation are not compatible.
248 Unknown collation '%1$s'.
249 Context requires collection with elements having identical codesets and collations.
250 Prefix index is not allowed on attribute '%1$s' (has collation with expansions).
251 Initial value is invalid.
252 Multi-column prefix index is not allowed.
253 '%1$s' function cannot be used for function based index.
254 Invalid index column definition: %1$s
255 '%1$s' is not allowed in a filter expression for index.
256 Cannot use GROUPBY_NUM() and WITH ROLLUP clause in same query.
257 Cannot MERGE UPDATE column %1$s referenced in ON clause.
258 The number of columns, %1$d, in ( %2$s ) do not match\nthe number of columns, %3$d, in ( %4$s ).
259 Prefixes of columns not allowed in function index expression.
260 %1$s cannot be a PARTITION BY or ORDER BY column
261 Query must evaluate to a single value\n(%1$s).
262 Nested analytic functions are not allowed.
263 An inherited attribute cannot be used as the partitioning key.
264 The aggregate size of enumeration elements is too large.
265 Cannot use target table in source spec.
266 COLLATE is not allowed in this context.
267 COLLATE modifier cannot change codeset (from '%1$s' to '%2$s'). Try using CAST instead.
268 Invalid codeset '%1$s' for partition value. Expecting '%2$s' codeset.
269 Session variable cannot have a different collation than system collation ('%1$s').
270 Invalid function index expression.
271 The query specification of %1$s has a cyclic reference to itself.
272 %1$s expression not allowed in join condition.
273 ORDER BY is not allowed in function MEDIAN.
274 The arguments of the function '%1$s' must be constant.
275 Function '%1$s' must have ORDER BY clause.
276 For function '%1$s', the number of arguments does not match the number of ORDER BY columns.
277 For function '%1$s', the ORDER BY clause is invalid.
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
1 Out of virtual memory: unable to allocate %1$d bytes.
2 Transaction isolation level must be 4, 5 or 6.
3 Timeout value must be -1, 0, or >0.
4 Isolation level set to:\n
5          Unknown isolation.\n
6          Unknown isolation.\n
7          Unknown isolation.\n
8          READ COMMITTED\n
9          REPEATABLE READ\n
10 A non-updatable query is not allowed in a db_query_execute_oid call.
11 Transaction timed out waiting for locks.
12 Attempted to access host variable with index %1$d when only %2$d \ninput host variables were given.
13 Unimplemented conversion.
14 Internal error, undefined conversion.
15 '%1$s' yields no results.
16 '%1$s' yields more than one row.
17 Interpreter variable %1$s is not defined.
18 Cannot evaluate '%1$s'.
19 Unknown variable %1$s.
20 Unknown shared attribute %1$s.
21 '%1$s' yields more than one column.
22 Nested or invalid use of aggregate function.
23 Function %1$s is not defined.
24 Context requires query to be single column\n(%1$s).
25 Too many values have been specified.
26 Virtual assignment target (%1$s) is not invertible.
27 There are more columns in the query specification than attributes in class %1$s.
28 There are more attributes in class %1$s than columns in the query specification.
29 Current release restricts aggregation to vclasses WITHOUT \naggregate query specifications.
30 Current release restricts aggregation to vclasses WITHOUT \nunion query specifications.
31 Use of vclass (%1$s) in a composition hierarchy requires it to be updatable.\nIt cannot be updated with respect to real class %2$s.
32 not used
33 Illegal construct in path expression '%1$s'.
34 path expression '%1$s' crosses db boundary to %2$s, with vclass %3$s.
35 path expression '%1$s' crosses db boundary from %2$s, to class %3$s.
36 %1$s is not authorized on %2$s.
37 Select is not authorized on %1$s.
38 Vclass %1$s is not updatable.
39 %1$s is not a proxy vclass.
40 query spec of proxy vclass must not be a join.
41 %1$s has no object id set.
42 Classes (%1$s) with multiple object_id fields in a composition \nhierarchy are not supported.
43 Object attribute %1$s of %2$s is not a vclass on ldb %3$s.
44 Query spec type incompatible with attribute %1$s.
45 The query specification of %1$s has a cyclic reference to itself.
46 No expression given to evaluate.
47 No real class found for vclass %1$s.
48 Executing '%1$s' on ldb '%2$s' \nreturned: %3$s.
49 Putting value '%1$s' into attribute '%2$s' returned: %3$s.
50 Ldb name must not be null in %1$s.
51 Ldb name at host must not be null in %1$s.
53 Host name must not be null in %1$s.
54 User name must not be null in %1$s.
55 User password must not be null in %1$s.
56 Insert translates to empty statement.
57 Update translates to empty statement.
58 Delete translates to empty statement.
59 INGRES does not support dates (%1$s) before January 1, 1582.
60 Resources exhausted in query generation.
61 Query yields no result, so variable '%1$s' is not set.
62 With LOCAL check option is not supported.
63 Check option exception on view %1$s.
64 Invalid date: '%1$s'.
65 Invalid time: '%1$s'.
66 Invalid utime: '%1$s'.
67 Subqueries in a proxy's query specification are not supported.
68 Ldb function argument is not compatible with ldb in '%1$s'.
69 Invalid numeric: '%1$s'.
70 Outer join query optimization failed.
71         SERIALIZABLE.\n
72 Vclass %1$s is not updatable. Only updatable vclasses are allowed to select VIDs.
73 Partitioning failed.
74 Partition does not exist.
75 Not allowed access to partition: '%1$s'.
76 The serial '%1$s' is not defined yet.
77 The owner or DBA can only alter the definition of the target serial.
78 It is not allowed for users to modify the serial object created by an auto increment constraint.
79 Invalid isolation level: %1$s schema, %2$s instances.
80 Isolation level value in MVCC must be 'read committed', 'repeatable read' or 'serializable'.


$set 11 MSGCAT_SET_AUTHORIZATION
1 *** invalid cache ***
2    class %1$s
3 from %1$s :
4 Authorization for user %1$s\n
5 Authorization for undefined user\n
6 User : %1$s\n
7            id : %1$d\n
8       members :
9        groups :
10 User %1$s\n
11 Current user is : %1$s\n
12 Root authorization object :\n
13   users :
14 *** Couldn't issue all grants for user "%1$s"*** \n
15 AUTHORIZATIONS\n
16 direct_groups :

$set 12 MSGCAT_SET_HELP
1 Rootclass
2 %1$s %2$s\n
3 Super classes :\n
4 Sub classes :\n
5 Attributes: \n
6 Methods: \n
7 Class attributes: \n
8 Class methods: \n
9 Conflict resolutions:\n
10 Method files:\n
11 Query specification:\n
12 OBJECT of %1$s\n
13 \nDescription:\n
14 \nStructure:\n
15 \nExample:\n
16 META CLASS Name
17 Class Name
18 VClass Name
$  Don't delete the space at the end of the next message
19 VClass on ldb
20 GENERAL

$set 13 MSGCAT_SET_TRIGGER
1 TRACE: Evaluating condition for trigger '%1$s'.
2 TRACE: Executing action for trigger '%1$s'.

$set 14 MSGCAT_SET_LOCK
1 \n
2 Thread (index %1$3d) of transaction (index %2$3d, %3$s, %4$s@%5$s|%6$d) has been suspended.\n
3 Thread (index %1$3d) of transaction (index %2$3d, %3$s, %4$s@%5$s|%6$d) has been resumed.\n
4 Transaction index %1$3d ACQUIRED %2$s lock on object %3$d|%4$d|%5$d.\n
5 Transaction index %1$3d ACQUIRED %2$s lock on page %3$d|%4$d.\n
6 Transaction index %1$3d CONVERTED its lock to %2$s lock on object %3$d|%4$d|%5$d.\n
7 Transaction index %1$3d CONVERTED its lock to %2$s lock on page %3$d|%4$d.\n
8 Transaction index %1$3d WAIT FOR %2$s lock on object %3$d|%4$d|%5$d.\n
9 Transaction index %1$3d WAIT FOR %2$s lock on page %3$d|%4$d.\n
10 Transaction index %1$3d RELEASE %2$s lock on object %3$d|%4$d|%5$d.\n
11 Transaction index %1$3d RELEASE %2$s lock on page %3$d|%4$d.\n
12 Transaction index %1$3d DEMOTE lock to %2$s on object %3$d|%4$d|%5$d.\n
13 Transaction index %1$3d DEMOTE lock to %2$s on page %3$d|%4$d.\n
14 OID = %1$2d|%2$6d|%3$4d\n
15 Object type: Root class.\n
16 Object type: Class = %1$s.\n
17 Object type: Instance of class (%1$2d|%2$6d|%3$4d) = %4$s.\n
18 Object type: Unknown.\n
19 Total mode of holders = %1$8s, Total mode of waiters = %2$8s.\n
20 Num holders=%1$3d, Num blocked-holders=%2$3d, Num waiters=%3$3d\n
21 LOCK HOLDERS:\n
22 BLOCKED LOCK HOLDERS:\n
23 LOCK WAITERS:\n
24 NON_2PL_RELEASED:\n
25 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d\n
26 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d, Nsubgranules = %5$2d\n
27 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d\n\t%5$13s Blocked_mode = %6$8s\n\t%7$17s Start_waiting_at = %8$-30s\n\t%9$17s Wait_for_secs = %10$.2f\n
28 %1$4sTran_index = %2$3d, Granted_mode =%3$8s, Count = %4$3d, Nsubgranules = %5$2d\n\t%6$13s Blocked_mode =%7$8s\n\t%8$13s Start_waiting_at = %9$-30s\n\t%10$13s Wait_for_secs = %11$.2f\n
29 %1$4sTran_index = %2$3d, Blocked_mode =%3$8s \n\t%4$13s Start_waiting_at = %5$-30s \n\t%6$13s Wait_for_secs = %7$.2f\n
30 %1$4sTran_index = %2$3d, Non_2_phase_lock =%3$8s\n
31 VPID (volid|pageid) = %1$3d|%2$6d.\n
32 *** Lock Table Dump ***\n Lock Escalation at = %1$d, Run Deadlock interval = %2$5.2f\n
33 Transaction (index %1$2d, %2$s, %3$s@%4$s|%5$d)\n
34 Isolation %1$s\n
35 State %1$s\n
36 Timeout_period %1$s\n
37 Unilaterally aborted transactions with indices:\n
38 %1$d ,
39 Timed_out transactions with indices:\n
40 %1$d ,
41 Solving the deadlock by selecting a transaction and a function \n to break the deadlock: %1$d %2$s
42 , %1$d %2$s
43 Object type: Index key of class (%1$2d|%2$6d|%3$4d) = %4$s.\n
44 Index name: %1$s\n

$set 15 MSGCAT_SET_IO
1 \n*************************************************************************\n
2 Backup = %1$s is needed. \n It must be mounted/loaded to continue.\n Type 1 to continue after the backup is mounted/loaded.\n Type 0 to quit.\n
3 \n\n*** BACKUP HEADER INFORMATION ***\n\n
4 Backup magic file identification %1$s,\n
5          Release: %1$s\n\
     Disk Version: %2$g\n
6    Database Name: %1$s\n\
 DB Creation Time: %2$s\
         Pagesize: %3$d\n
7     Backup Level: %1$d (%2$s)\n\
        Start_lsa: %3$lld|%4$d\n\
         Last_lsa: %5$lld|%6$d\n
8      Backup Time: %1$s\
  Backup Unit Num: %2$d\n
9  Database Volume name: %1$s\n\
   Volume Identifier: %2$d, Size: %3$lld bytes (%4$d pages)\n
10   Database Name: %1$s\n\
     Volume Name: %2$s\n\
        Unit Num: %3$d\n\
    Backup Level: %4$d (%5$s)\n\
Enter one of the following options:\n
11 Type\n \
  -  %1$d to quit.\n \
  -  %2$d to continue after the volume is mounted/loaded. (retry) \n \
  -  %3$d to continue after changing the volume's directory or device.\n
12 Enter new directory or device for backup volume.\n
13 Error in input, choose a value between %1$d and %2$d.\n
14 ** WARNING ** Incorrect backup volume mounted.\n
15 Backup volume %1$s is from another backup level\n   (Level: %2$d, expecting %3$d).\n\n
16 Backup volume %1$s has incorrect page or magic identifier.\n\n
17 Backup volume %1$s is from another database\n  (Name: %2$s, expecting %3$s).\n\n
18 Backup volume %1$s out of sequence\n  (Unit Num: %2$d, expecting %3$d).\n\n
19 Backup volume %1$s timestamp mismatch\n  (Timestamp: %2$s, expecting %3$s).\n\n
20 Backup volume information file error, line %1$d.\n
21 Backup Volume Label: Level: %1$d, Unit: %2$d, Database %3$s, Backup Time: %4$s\n
22     Previous Backup level: %1$d Time: %2$s (start_lsa was %3$lld|%4$d)\n
23 The following database backup volume is needed to continue restoring:\n\n
24 Backup destination is full, a new destination is required to continue:\n\n
25 Preceding Backup Volume Name: %1$s\n
26     Next Backup Volume Name: %1$s\n
27  Backup Pagesize: %1$d\n
28       Zip Method: %1$d (%2$s)\n\
        Zip Level: %3$d (%4$s)\n
29 Include Active Log: %1$s\n

$set 16 MSGCAT_SET_LOG
1 \n*************************************************************************\n
2 Log Archive %1$s \n is needed to continue normal execution.   \n \
  Type\n \
  -  0 to quit.\n \
  -  1 to continue without present archive. (Partial recovery)\n \
  -  2 to continue after the archive is mounted/loaded.       \n \
  -  3 to continue after changing location/name of archive.   \n
3 Backup information file %1$s \n is needed to continue the restore process. \n \
  Type\n \
  -  0 to quit.\n \
  -  1 to continue after the file is mounted/loaded.       \n \
  -  2 to continue after changing location/name of file.   \n
4 Enter new location/name.\n
5 COMMENT: %1$s for database %2$s\n
6 COMMENT: Log archive %1$s is not needed any longer unless a database media crash occurs.\n
7 COMMENT: Log archives from %1$s to %2$s are not needed any longer unless a database media crash occurs.\n
8 COMMENT: from renamed database = %1$s\n
9 ARCHIVE: %1$d %2$s %3$lld %4$lld\n
10 ARCHIVE:
11 REMOVE: %1$d %2$s to \n \
       %3$d %4$s.\n \
       REASON: %5$s\n
12 ACTIVE: %1$s %2$d pages\n
13  Finish commit of tran_index %1$d with tranid = %2$d,\n just before LSA= %3$lld|%4$d and at %5$s
14  Finish abort of tran_index %1$d with tranid = %2$d,\n just before LSA= %3$lld|%4$d and at %5$s
15  Incomplete Media Recovery is in effect. The database will be \n \
 recovered up to LSA point address of %1$lld|%2$d. Last commit/abort \n \
 point time is approximately at %3$s \n \
 The database has lost all information of transactions committed \n \
 after this time.\n
16 Reset of log due to incomplete recovery.
17 Database backup was taken.
18 Media crash is not important for the application.
19 Backing up to directory %1$s which seems to have an earlier backup in it.\n\
Obsolete backup volumes of this database in this location will be removed,\n\
including those from the current backup level and higher.\n
20 Enter 'y' to continue or any other key to cancel.\n
21 Database backup was aborted by the user.\n
22 DELETE POSTPONED: Archives %1$d %2$s to \n \
        %3$d %4$s \n \
        are no longer needed unless a restore from current backup occurs.\n
23 A newer backup has released archives previously marked DELETE POSTPONED.\n
24 COMMENT: Log archive %1$s is not DELETE POSTPONED any longer due to more recent\n \
   backup and therefore is not needed unless a database media crash occurs.\n
25 COMMENT: Log archives from %1$s to %2$s are not DELETE POSTPONED any longer due to\n \
   more recent backup and therefore are not needed unless a database media\n \
   crash occurs.\n
26 An error occurred during a database restore, while processing\n\
     Backup Volume: %1$s\n\
          Unit Num: %2$d\n\
   Database Volume: %3$s\n\
    Database Volid: %4$d\n\
** WARNING: Do not use the database until an error-free restore is achieved.\n
27 Error in input, choose a value between %1$d and %2$d.\n
28 The time(%1$s) specified must be after the time(%2$s) of the specified backup.\n
29 COMMENT: Log archive %1$s, which contains log pages before %2$lld, is not needed any longer by any HA utilities.\n
30 Number of active log archives has been exceeded the max desired number.
