
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
$ cubrid.msg
$

$set 1 MSGCAT_SET_GENERAL
1 \n%s\n\n
2 Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n
3 Copyright Information\n
5 Non integer argument value `%s'
6 Non floating argument value `%s'
7 Missing argument `%s'
8 Duplicate argument `%s'
9 Missing value for argument `%s'
10 Unknown argument `%s'
11 Unexpected positional argument `%s'
12 No argument definitions given
13 Couldn't allocate storage for argument parsing
14 Argument string exceeds maximum length: %s
15 Invalid message number %d within catalog for argument parsing
16 [options]
17 Options:

$set 4 MSGCAT_SET_PARAMETER
2 %s is not a directory.  Creating %s in ".".
3 Cannot open %s.  configuration file not created.
4 Unable to write line %d to configuraiton file %s.
6 Can't access system configuration file "%s": %s
7 HOME not set???
8 Required parameter "%s" has no value.
9 Cannot open configuration file "%s": %s
10 Line %d in startup file %s:
11 Environment variable %s:
12 Unrecognized keyword.
13 Value type does not match parameter type.
14 Cannot allocate memory to store value.
15 Unterminated string value.
16 Value is out of range.
17 UNIX error: %s
18 No message available.
19 Value is out of range. Resetting to the default value.
20 Keyword = '%s'    Value = %d    Default Value = %d\n
21 Keyword = '%s'    Value = %f    Default Value = %f\n

$set 5 MSGCAT_SET_ERROR
2 Internal system failure:  no more specific information is available.
3 Out of virtual memory:  unable to allocate %1$d memory bytes.
4 Interrupt.
5 Key = "%1$s" was not found in hash table "%2$s".
6 A NULL memory hash table was supplied to a hash table function.
7 Trying to format disk volume "%1$s" with an incorrect value %2$d for number of pages.
8 Unable to format disk volume "%1$s" with %2$d pages (%3$d bytes).
9 Unable to format disk volume "%1$s" with %2$d pages (%3$d Kbytes) due to insufficient space. Current space available is %4$d pages (%5$d Kbytes).
10 Unable to mount disk volume "%1$s".
11 Unable to mount disk volume "%1$s". The database "%2$s", to which the disk volume belongs, is in use by user %3$s on process %4$d of host %5$s since %6$s.
12 Problems dismounting volume "%1$s".
13 An I/O error occurred while reading page %1$d of volume "%2$s".
14 An I/O error occurred while writing page %1$d of volume "%2$s".
15 Unable to write page %1$d of volume "%2$s" due to insufficient space in operating system device.
16 Unable to rename disk volume "%1$s" to "%2$s".
17 Internal error: fetching deallocated pageid %1$d of volume "%2$s".
18 All page buffers are fixed.
19 Internal error: pageptr = %1$p of page %2$d of volume "%3$s" is not fixed.
20 Internal error: unknown pageptr = %1$p.
21 Internal error: unknown sector %1$d of volume "%2$s".
22 Internal error: unknown page %1$d of volume "%2$s".
23 Internal error: trying to deallocate disk system page %1$d of volume "%2$s".
24 Almost out of space in volume "%1$s".  Total number of pages = %2$d, total number of free pages = %3$d.
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
43 Internal error: incorrect number (%1$d) of slots passed for reordering records on page %2$d of volume "%3$s".  There are actually %4$d slots.
44 No space available in slotted page %1$d of volume "%2$s".
45 Slot %1$d on page %2$d of volume "%3$s" is allocated to an anchored record.  A new record cannot be inserted here.
46 Internal error: slot %1$d on page %2$d of volume "%3$s" is not allocated.
47 Unable to create a heap file in volume "%1$s".
48 Accessing deleted object %1$d|%2$d|%3$d.
49 Internal error: class of object %1$d|%2$d|%3$d is unknown.
50 Internal error: relocation record of object %1$d|%2$d|%3$d may be corrupted.
51 Internal error: object %1$d|%2$d|%3$d may be corrupted.
52 Internal error: object overflow address %1$d|%2$d|%3$d may be corrupted.
53 Fetching object %1$d|%2$d|%3$d when only its OID has been assigned.
54 Internal error: trying to manipulate an object of %1$d pages long.  The maximum size allowed for an object is %2$d pages.
55 Internal error: A page cycle reference was detected on page %1$d|%2$d of heap file %3$d|%4$d|%5$d.
56 Internal error: unknown extendible hashing file (Volid: %1$d Fileid: $2$d) page (Volid: %3$d Pageid: %4$d) was specified.
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
69 Internal error: different class object identifers were found for class with name "%1$s". Found OIDS are %2$d|%3$d|%4$d, %5$d|%6$d|%7$d using classname hash table and heap, respectively.
70 Internal error: Class with name "%1$s" and oid = %2$d|%3$d|%4$d does not exist in classname hash table.
71 Internal error: Class with name "%1$s" and oid = %2$d|%3$d|%4$d does not exist in its heap.
72 Your transaction (index %1$d, %2$s@%3$s|%4$d) has been unilaterally aborted by the system.
73 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on object %6$d|%7$d|%8$d. You are waiting for user(s) %9$s to finish.
74 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on class %6$s. You are waiting for user(s) %7$s to finish.
75 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on instance %6$d|%7$d|%8$d of class %9$s. You are waiting for user(s) %10$s to finish.
76 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s on page %6$d|%7$d. You are waiting for user(s) %8$s to release the page lock.
77 Internal error: all buffers are fixed.
78 Internal error: an I/O error occurred while reading logical log page %1$d (physical page %2$d) of "%3$s".
79 Internal error: an I/O error occurred while writing logical log page %1$d (physical page %2$d) of "%3$s".
80 Insufficient space in operating system device when writing logical log page %1$d (physical page %2$d) of "%3$s". Could not write more than %4$d bytes.
81 Internal error: logical log page %1$d may be corrupted.
82 Unable to mount log disk volume/file "%1$s".
83 Length of path "%1$s" plus length of prefix logname "%2$s" is too long; the combined length must be less than %3$d.
84 Length of prefix logname "%1$s" is too long; the length must be less than %2$d.
85 The prefix name "%1$s" is not the same as "%2$s" on the log disk.  The log may have been renamed outside the database domain.
86 Database is incompatible with current "%1$s" release "%2$s".
87 There are recovery actions that must recovered using "%1$s" release "%2$s" instead of release "%3$s". After the recovery, the database can be run on release "%4$s".
88 Release string "%1$s" is larger  than %2$d. Internal error .. must change header log.
89 Log "%1$s" does not belong to the given database.
90 Redo logging is always a page level logging operation.  A data page pointer must be given as part of the address.
91 Postpone logging is always a page level logging operation.  A data page pointer must be given as part of the address.
92 Compensate logging is always a page level logging operation.  A data page pointer must be given as part of the address.
93 Warning: redo logging is ignored during redo recovery and normal rollbacks.
94 Warning: postpone logging is ignored during redo recovery and normal rollbacks.
95 Warning: undo logging is ignored during recovery and normal rollback.
96 Media recovery may be needed on volume "%1$s".
97 Internal error: unable to find log page %1$d in log archives.
98 Unable to create archive log "%1$s" to archive pages from %2$d to %3$d.
99 Unable to create backup directory information file "%1$s".
100 Unable to backup database "%1$s".
101 Unknown user file "%1$s".
102 File "%1$s" does not have enough entries:  %2$d entries expected.
103 File "%1$s" seems to have unordered entries. Entry %2$d with values: %3$d %4$s %5$s, was found when entry with values %6$d %7$s ??? was expected.
104 File "%1$s" seems to have an incorrect entry for database name (main volume). Entry %2$d with values: %3$d %4$s %5$s, was found when entry with values %6$d %7$s %8$s was expected.
105 Cannot access backup file "%1$s". Restore or Backup is cancelled.
106 Cannot prepare to commit the current transaction with global transaction identifier %1$d because that transaction identifier is in use by another transaction.
107 There is no global distributed transaction associated with transaction identifier %1$d.
108 Current transaction %1$d must be committed or aborted before attempting to join to the distributed transaction with global identifier %2$d.
109 Your transaction %1$d has been unilaterally aborted by the system because a participant (remote) site went down or unilaterally aborted the transaction.
110 Isolation level value must be between %1$d and %2$d.
111 Your transaction has been aborted by the system due to server failure.
112 Host "%1$s" is not authorized to run database.
113 Unable to restart/initialize the database server.
114 Unable to interpret "%1$s" as a database.  The database volumes/files may have been renamed/copied outside the database domain.
115 Database "%1$s" already exists.
116 Database "%1$s" is unknown, or the file "databases.txt" cannot be accessed.
117 Absolute pathname for the database is too long.  The combined length of the path "%1$s" plus the name "%2$s" is %3$d; the combined length must be less than %4$d.
118 Failed to locate current working directory.
119 Unable to find local hostname.
120 The maximum number of volumes (%1$d) has been exceeded.
121 Trying to remove "%1$s" a permanent volume from the database.
122 Unable to access system message catalog.
123 Unable to create "%1$s" for database "%2$s".
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
134 It is illegal to mix class attributes and non-class attributes in the same update.
135 Invalid statement for oid columns: %1$s.
136 Unimplemented function "%1$s".
137 Corrupted authorization objects.
138 Authorization class "%1$s" not found.
139 Access error on authorization attribute "%2$s" of class "%1$s".
140 Operation "%1$s" can only be performed by the DBA or a DBA group member.
141 Cannot add "%1$s" as a member of "%2$s".
142 Adding member causes the user hierarchy to become a cyclic graph.
143 Encountered class with no owner.
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
167 Must be class owner to perform operation.
168 Member not found.
169 Cannot remove user %s from the database.
170 No user logged in.
171 Incorrect or missing password.
172 Password string cannot have more than 31 characters.
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
195 Server communications error : %1$s.
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
207 Class "%1$s" has object size of zero.
208 Method "%1$s" not found.
209 No attribute or method with name "%1$s" was found.
210 Internal error: processing object template.
211 Attribute was deleted while building object template.
212 Attribute "%1$s" could not be set without violating the UNIQUE contraint.
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
225 Missing value for attribute "%1$s" with the NON NULL constraint.
226 Empty component list in class.
227 Attempt to assign value to attribute "%1$s" more than once.
228 Method "%1$s" called with %2$d arguments.  The system allows a maximum of %3$d arguments.
229 Method "%1$s" requires argument %2$d to be within the domain "%3$s".  An invalid value of domain "%4$s" was given.
230 Attribute "%1$s" cannot be assigned a string of length %2$d.  The maximum allowable string length is %3$d.
231 Invalid API operation attempted on a temporary object.
232 Attempt to use object template over a transaction
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
245 The name "%1$s" has already been used as the name of an attribute.
246 The name "%1$s" has already been used as the name of a method.
247 Invalid arguments to schema manager internal function.
248 The attribute domain "%1$s" is not suitable for use with the UNIQUE integrity constraint.
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
279 In class "%4$s", "%1$s" inherited from class "%2$s" cannot be used as requested.  The definition from class "%3$s" has a more specific domain.
280 In class "%3$s", "%1$s" cannot be inherited from class "%2$s" because there is a local definition with an incompatible domain.
281 In class "%3$s", "%1$s" inherited from class "%2$s" is aliased but no substitute from another source was found.
282 In class "%4$s", "%1$s" inherited from class "%3$s" cannot serve as an alias substitute for class "%2$s". The domains are incompatible.
283 In class "%4$s", "%1$s" inherited from class "%3$s" cannot serve as an alias substitute for class "%2$s". It has a less specific domain.
284 Cannot request inheritance of "%1$s" because it is defined locally in the class.
285 Cannot use "%1$s" as an alias for inherited component "%2$s" because there is already a locally defined component with that name.
286 Cannot use "%1$s" as an alias for "%2$s" of "%3$s".  A component with that name is already inherited from "%4$s".
287 In class "%1$s", cannot define attribute "%2$s" because it would shadow a method inherited from "%3$s".
288 In class "%1$s", cannot define method "%2$s" because it would shadow an attribute inherited from "%3$s".
289 Subclass "%1$s" cannot inherit method "%2$s" from class "%3$s" because there is already an attribute with that name.
290 Subclass "%1$s" cannot inherit attribute "%2$s" from class "%3$s" because there is already a method with that name.
291 "%1$s" is defined in class "%2$s" as an attribute and in class "%3$s" as a method.
292 No populate specification with index "%1$s".
293 Operation not allowed for this class type.
294 Undefined environment variable "%1$s" referenced in method file.
295 Unable to alter class "%1$s".  The system catalog is out of space.  To reclaim space, compact the database with the compactdb utility.
296 Invalid property list encountered.
297 Alias name "%1$s" is used more than once.
298 In class "%1$s", a resolution for "%2$s" of "%3$s"' is not allowed.  That class is not a super class.
299 "%1$s" is a reserved word and cannot be used as an attribute, method, or class name.
300 Attribute "%1$s" cannot be defined with both a DEFAULT value and the UNIQUE integrity constraint.
301 Attribute "%1$s" cannot be defined with a length constraint of %2$d.  The maximum length constraint is %3$d.
302 Could not add element to a set.
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
331 Could not allocate workspace block from OS, out of virtual memory.  Aborting transaction.
332 Original workspace size estimate exceeded.  An automatic extension has been made.
333 Out of memory during workspace storage allocation.  Aborting transaction.
334 Attempted allocation of %1$d bytes is larger than maximum workspace block size.  Try adjusting max_block_size parameter.  Transaction aborted.
335 Ignoring attempt to free object not allocated within the workspace.
336 Ignoring attempt to free an object more than once.
337 Ignoring attempt to allocate object with negative size.
338 Map entry identifier overrun.
339 Cannot exit critical section, since it is not owned by transaction %1$d.
340 Cannot fork child process.
341 Cannot exec child process.
342 Cannot change process group.
343 Request_id's used up.
344 %1$s.
345 Error reading shutdown message from master.
346 Error reading stop shutdown message from master.
347 Could not establish a pipe to the master server.
348 Must specify either service or port identifier.
349 Unknown service for TCP: %1$s.
350 Error getting local host by name for host "%1$s". Host not found.
351 Cannot create TCP socket.
352 tcp_open: cannot get a reserved TCP port.
353 Cannot make connection to master server.
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
366 %1$s
367 Server %1$s already exists.
368 Communication error during connect for %1$s.
373 Premature EOF during read: %1$d.
374 Premature EOF during read of %1$d or %2$d.
375 Conversion error on (0x%1$c%2$c) %3$d and %4$d converted to %5$d.
376 Error finding class public.
377 Error %1$d adding attribute "%2$s" to multimedia hierarchy.
378 Error %1$d adding method "%2$s" implemented as "%3$s" to multimedia hierarchy.
379 Error %1$d adding superclass to multimedia hierarchy.
380 Dynamic loader already initialized.
381 Dynamic loader not initialized.
382 Dynamic loader improperly initialized.
383 "%1$s" is not an object file or an archive.
384 Cannot determine absolute path for "%1$s".
385 ld process exited with code %1$d.
386 ld process exited due to signal %1$d.
387 ld process exited with unknown waitval 0x%1$lx.
388 Cannot access initial image for "%1$s".
389 System error: %1$s.
390 Problem with "%1$s": %2$s.
391 Conflict with other SIGPIPE handler.
392 Unable to spawn dynamic loader daemon.
393 Dynamic loader daemon has disappeared.
394 Range endpoint not between 0 and 256.
395 Illegal subexpression specified.
396 Octal digit in escape out of range.
397 Regular expression not delimited properly.
398 Text search called before expression compiled.
399 Unbalanced parentheses in regular expression.
400 Parentheses too deeply nested in regular expression.
401 More than two numbers specified in a range.
402 Unmatched {.
403 First number in range expression is larger than second.
404 Unbalanced [ in regular expression.
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
422 Representation Information Record  Missing in Catalog for Class - Repr_Id: %1$d|%2$d|%3$d - %4$d.
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
447 An operation has been attempted on a closed query result structure.
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
458 Overflow happened in addition context.
459 Query result does not contain a single tuple.
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
525 Error compiling condition for "%1$s" : %2$s
526 Error compiling action for "%1$s", %2$s
527 Error evaluating condition for "%1$s" : %2$s
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
544 Internal error: Btree key %1$s entry for object OID: %2$d|%3$d|%4$d was not found on B+tree: %5$d|%6$d|%7$d.
545 Internal error: Key and OID: %1$d|%2$d|%3$d entry on B+tree: %4$d|%5$d|%6$d is incorrect.The object does not exist.
546 Internal error: expecting %1$d OID entry values, but %2$d were found on B+tree: %3$d|%4$d|%5$d.
547 Server release %1$s is different from client release %2$s.
548 There is no object_id defined for proxy class "%1$s".
549 The proxy instance was not found.
550 Unknown savpoint name %1$s
551 Unable to locate volume information path file "%1$s"... Continue reading from internal tables
552 Date arithmetic underflow.
553 Time arithmetic underflow.
$ LOADDB
554 The loader has entered an invalid state.  No further operations are possible.
555 Memory allocation error during database loading.
556 Too many values supplied.  Expecting %d.
557 For attribute %1$s of %2$s, type %3$s is not allowed in the domain of the set.
558 For attribute %1$s of %2$s, expected type %3$s, got a set constant.
559 For attribute %1$s of %2$s, expected type %3$s, got %4$s.
560 For attribute %1$s of %2$s, domain is not specific enough to support\n unqualified object references.
561 Nested sets are not allowed.
562 System class %1$s cannot be populated using the database loader.
563 Reference to internal class %1$s converted to NULL.
564 Violation of the UNIQUE constraint for attribute %1$s of %2$s.
565 Constructor method %1$s is not defined on class %2$s.
566 Unused error id.
567 Too many constructor method arguments supplied.  Expected %1$d.
568 Missing constructor method parameters.  Expected %1$d, found %2$d.
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
581 Attempt was made to update the database when updates are disabled.
582 Unknown storage purpose %1$d. Valid range is from %2$d to %3$d.
583 Trying to allocate an invalid number (%1$d) of pages.
584 Internal Error: A loop was detected in the file table of file %1$d in volume "%2$s". The loop may be caused by page %3$d|%4$d.
585 Unknown heap %1$s|%2$d|%3$d
586 Internal Error: Unable to update chain header in VPID %1$d|%2$d for heap %3$d in volume "%4$s".
587 Entries of permanent volumes are unsorted in your "%1$s" volinfo file. Entry %2$d: %3$d %4$s is out of sequence.
588 INTERNAL ERROR: Failed optimizer assertion. Optimization aborted.
589 Invalid user name "%1$s".
590 The attribute domain "%1$s" of a class may not be a \nvirtual class or a proxy virtual class.
591 The attribute domain "%1$s" of a proxy virtual class \nmust be a proxy virtual class.
592 The attribute domain "%1$s" of a proxy virtual class \nand the proxy virtual class must be on the same local database.
593 A non-updatable virtual instance has been reclaimed and cannot be rebuilt.
594 An object_id clause must be supplied without using attributes (%1$s) of approximate data types (float or double).
595 The database name "%1$s" is too long. Name of database should be less than  %2$d characters
596 The %1$d pages of total temporary space allowed have been exceeded.
597 Number of pages for heap file %1$d|%2$d|%3$d is inconsistent. \n%4$d and %5$d were found according to heap chain and file table, respectively.
598 Number of files is inconsistent. %1$d were expected, %2$d were found
599 An I/O error occurred while synchronizing state of volume "%1$s".
600 Function "%1$s" not implemented on the PC.
601 "%1$s".
602 dl error: "%1$s" is multiply defined.
603 Internal Error: Sector/page table of file VFID %1$d|%2$d seems corrupted.
604 Internal Error: An allocation set for file VFID %1$d|%2$d(Volume "%3$s") seems inconsistent in %4$d deleted or marked deleted pages and %5$d page holes in table
605 Internal Error: Inconsistent number of marked deleted pages for file VFID %1$d|%2$d(Volume "%3$s"). Expected %4$d, Found %5$d.
606 Could not locate odbc.ini with path "%1$s".
607 Database "%1$s" was not found in odbc.ini.
608 Database "%1$s" does not have a Host specification in the odbc.ini file.
609 Your transaction cannot be rolled back since logging was ignored. Your database may be corrupted.
610 Your database is likely to be corrupted since logging was turned off when your database crashed.
611 %s cannot be disabled in client/server mode.
612 Logging cannot be disabled at this moment since there are pending actions to recover.
613 In class "%3$s", local component "%1$s" cannot serve as an alias substitute for class "%2$s". The domains are incompatible.
614 Number of active log archives has been exceeded the max desired number of %1$d.
615 Cannot establish contact with the Winsock interface. Winsock error: %1$d.
616 Cannot determine machine name using Winsock interface. Winsock error: %1$d.
617 Cannot determine machine id using Winsock interface.  There must be an entry in the host name table for this machine.  Winsock error: %1$d.
618 Attribute is not updatable.
619 Bad source codeset.
620 Bad destination codeset.
621 Invalid data type. The arguments contain an invalid data type.
622 Incompatible code sets. The code sets of the arguments are incompatible.
623 Invalid escape sequence. An invalid escape sequence has caused an error in the regular expression compiler.
624 Invalid escape character.  The escape character is non-NULL with a length greater than 1.
625 Internal Error. Trying to update the wrong instance object %1$d|%2$d|%3$d attribute information template with instance object %4$d|%5$d|%6$d.
626 Internal Error. %1$d requested attributes were not found.
627 Invalid trim operand.  The trim operand has a length greater than 1.
628 Invalid currency type: %1$d.
629 Pagesize %1$d is not a power of 2 or pagesize is too small. Pagesize of %2$d is used instead.
630 Conversion not supported by %1$s.
631 Statement would violate one or more attribute NOT NULL constraints.
632 %1$s does not seem to be a backup volume.
633 %1$s is a backup of database %2$s created on %3$s instead of given database %4$s created on %5$s
634 A database volume/file was expected in backup.
635 WARNING: Log Buffer pool of %1$d buffers seems very small for current environment.\n It is possible that some buffers are not freed by the system due to an UNKNOWN ERROR\n or that there are many concurrent transactions being aborted at the same time. The log buffer pool is expanded with %2$d more buffers.
636 WARNING Num_buffers %1$d is too small. The minumum value of %2$d was assumed.
637 System error: freeing a log buffer too much. Fix count is set to zero.
638 Warning: Flushing an unupdatable log archive pageid = %1$d
639 System error: Previous log sequence page %1$d is not the delayed flush page %2$d.
640 System Error: a savepoint cannot be added when the transaction is not active (e.g., during commit or rollback).
641 A name must be given for a savepoint.
642 System Error: there is not an active top system operation for current transaction.
643 May be a system error: Commiting/Aborting transaction = %1$d (index = %2$d) which has permanent operations\n attached to it. Will attach those system operations to the transaction
644 SYSTEM ERROR: Log manager does not known either undo/redo function for rcvindex = %1$d
645 SYSTEM ERROR: State = %1$s of Transaction = %2$d (index = %3$d) \n is not the correct state to apply client postpone/undo actions... ignored
646 SYSTEM ERROR: It seems that a compensating record is missing for logical logging rcvindex = %s\n. Without this compensating record crash recovery will not be done correctly
647 Backup "%1$s" does not belong to the given database.
648 Backup is incompatible with current "%1$s" release "%2$s".
649 Given precision of %1$d is invalid; it should be greater than %2$d and no greater than %3$d.
650 There is not enough stack space to run %1$d threads.
651 There is not virtual memory to preallocate %1$d sort buffers for each of %2$d threads.
652 Wrong offset %1$d was given to split the record of length %2$d stored on slot %3$d
653 Wrong offset %1$d or length %2$d was given to remove a portion of the data of the record of length %3$d stored on slot %4$d
654 Wrong offset %1$d or length %2$d was given to overwrite a portion of the data of the record of length %3$d stored on slot %4$d
655 \nCannot open the event handler configuration file, "%1$s".\n
656 \nWarning : Null id/name in event handler configuration file, "%1$s", line %2$d\n
657 \nWarning : Illegal event name, "%1$s",  in event handler configuration file, "%2$s", line %3$d\n
658 \nWarning : event id %1$d, not in error id range %2$d <> %3$d, file "%4$s", line %5$d\n
659 \nCannot access the event handler program, "%1$s"\n
660 \nCannot open/connect the event handler process, "%1$s"\n
661 \nFailed to write to the event handler process, "%1$s"\n
662 \nFailed to initialize event handling mechanism.\n
663 \nWarning : Line too long in event handler configuration file, "%1$s", line %2$d, truncating.\n
664 Notification : Database Server up.
665 \nEvent handler pipe broken, "%1$s"\n
666 \nWarning : Event Notification Mechanism disabled"\n
667 Cannot match class '%1$s' with corresponding C++ class.\n
668 Internal error: Unknown domain type '%1$d'.\n
669 Server refused client connection : max clients, (%1$d), exceeded\n
670 Operation would have caused one or more unique constraint violations.
671 A network read/write error ocurred for socket %1$d associated with your transaction (index %2$d, %3$s@%4$s|%5$d)
672 Socket %1$d associated with your transaction (index %2$d, %3$s@%4$s|%5$d) is closed.
673 Your connection to the server has been closed since the server is going down immediately.
674 There is not enough active threads to continue current server execution. Current number of active threads is %1$d. You may need up to %2$d threads for aclean server execution in your environment. Transaction index %3$d has been timed out to continue server execution.
675 Cannot read the database location file, '%1$s'.
676 Database '%1$s', cannot be found in the database location file, '%2$s'
677 Failed to connect to database server, '%1$s', on the following host(s): %2$s
678 Internal error. Client restart module received NULL database server name.
679 This C++ transaction has already been 'begun'.
680 Attempted to exceed maximum C++ transaction nesting level (%d).
681 %s transaction' was attempted on a C++ transaction that hasn't been 'begun'.
682 Cannot %s C++ transaction until all subordinate transactions are completed.
683 Attempt to create string with illegal length - %d
684 Method returned user-defined error code %d.
685 Class "%1$s" is invalid.
686 Notification : the event handler has been restarted.
687 No cast available for (%s *) to (%s *).
688 Subscript out of range.  Attempt to access element %ld. Collection size = %ld.
689 Internal error. Iterator contains corrupt state.
690 Attempt to dereference an uninitialized instance of odb_Ref<>.
691 Attempt to use an invalid iterator.
692 Attempt to use an iterator with an unrelated collection.
693 Overflow occurred on data type Numeric.
694 SYSTEM ERROR: Unable to load Btree.
695 Bad interface to kill client has been used.
696 Unknown transaction (index %1$d, %2$s@%3$s|%4$d).
697 Given transaction index (index %1$d, %2$s@%3$s|%4$d) does not match with current transaction index (index %5$d, %6$s@%7$s|%8$d). Likely, the given transaction has finished and its transaction index was re-assigned to another transaction.
698 Internal error: the number of OIDs in the unique hierarchy: %1$d does not equal the number of OIDs: %2$d found in the unique B+tree plus the number of NULLs: %3$d found in the unique hierarchy for B+tree: %4$d|%5$d|%6$d.
699 Internal error: the number of OIDs in the unique hierarchy: %1$d does not equal the number of OIDs: %2$d found in the unique B+tree root statistics for B+tree: %3$d|%4$d|%5$d.
700 Internal error: the number of OIDs: %1$d does not equal the number of NULLs: %2$d plus the number of keys: %3$d in the unique B+tree: %4$d|%5$d|%6$d.
701 No proxy is defined on "%1$s" at ldb "%2$s".
702 Internal error: the number of NULLs in the unique hierarchy: %1$d does not equal the number of NULLs: %2$d found in the unique B+tree root statistics for B+tree: %3$d|%4$d|%5$d.
703 Internal error: OID: %1$d|%2$d|%3$d found in unique B+tree: %4$d|%5$d|%6$d does not belong to one of the classes for the unique constraint.
704 Evaluation of generic server function failed.
705 Inconsistent %1$d permanent volumes were found when %2$d were expected.
706 Cannot expand permanent volumes. Permanent volume "%1$s" with purpose %2$s was given.
707 Unable to expand temporary volume "%1$s" with %2$d pages
708 Unable to expand temporary disk volume "%1$s" with %2$d pages (%3$d Kbytes) due to insufficient space. Current space available is %4$d pages (%5$d Kbytes).
709 Constraint "%1$s" not found.
710 Invalid constraint.
711 An object with the supplied attribute value could not be located.
712 Constraint" %1$s" already exists.
713 An index does not exist on this attribute.
714 Query failed due to insufficient temporary file space.
715 Schema failed to verify: %1$s
716 NOT NULL constraints are not allowed in virtual or proxy classes.
717 UNIQUE constraints are not allowed in virtual or proxy classes.
718 Invalid gadget.
719 The number of attributes(%d) and values(%d) are not equal.
720 Cannot use a gadget to insert into a virtual or proxy class.
721 Insert results in more than one row.
722 Wrong number of attributes for NOT NULL constraint.
723 Cannot modify the domain of an existing attribute.
724 Invalid parameter(s) for default constraint name generation.
725 Internal error: expected temporary oid and encountered permanent oid.
726 Unique constraint must be added using a separate alter statement\nonce instances have been created.  For example, instead of:\n\talter table1 add attribute a int unique;\nuse:\n\talter table1 add attribute a int;\n\talter table1 add unique(a);
727 Invalid trigger event.
728 Query failed because temporary file vfid is invalid for transaction %1$d.
729 Overflow happened in subtraction context.
730 Overflow happened in multiplication context.
731 Overflow happened in division context.
732 Overflow happened in unary minus context.
733 Overflow happened in coercion context.
734 Internal Error: Header of file VFID %1$d|%2$d(Volume "%3$s") is corrupted.
735 Unknown transaction index %1$d.
736 Must specify either service or port identifier. Winsock error: %1$d.
737 Unknown service for TCP: %1$s. Winsock error: %1$d.
738 Cannot create socket stream. Winsock error: %1$d.
739 Cannot bind local address... retrying. Winsock error: %1$d.
740 Cannot bind local address... aborting. Winsock error: %1$d.
741 Accept failed. Winsock error: %1$d.
742 Sending a broadcast message to client. Winsock error: %1$d.
743 Internal error registering client handshake.
744 Error in rehashing a proxy/vclass MOP.
745 Invalid garbage collection phase.
746 Invalid garbage collection callback.
747 Invalid garbage collection callback ticket.
748 Invalid object pointer encountered by db_gc_mark_object.
749 Number of fetched instances do not match: %1$d expected, %2$d received.
750 Number of fetched attributes do not match: %1$d expected, %2$d received.
751 The OID of a new object isn't available in a BEFORE INSERT trigger.
752 Error restoring backup unit_num %d.
753 Error restoring backup unit_num %d, pageid %d exceeds total number of pages %d for volid %d.
754 OO proxy instance has non-string-type or null key.
755 This operation can not be executed in stand-alone mode.
756 Specified string buffer is too small to encode this object.
757 Internal Error: cannot assign permanent OID during object encode.
758 Specified string is not valid for decoding object.
759 The target object has been deleted.
760 Internal error: proxy object instance has NULL key during object encode.
761 Internal error: vclass object instance maps to invalid real instance.
762 Internal error in resolving vclass instance to a real instance.
763 Internal error in encoding nonupdatable object instance.
764 Internal error in encoding object into internal vobj form.
765 Output object argument has a NULL address.
766 Internal error in decoding object.
767 Cannot create a level %d backup without first creating a level %d backup.
768 The local database "%1$s" has been accessed in this session, alteration or drop is not allowed.
769 Attribute "%s", of class "%s" is not a valid class or shared attribute.
770 No table functions.
771 Invalid parameter for function.
772 db_serial class not found.
773 Serial "%s" not found.
774 Serial "%s" already exist.
775 Next value exceeds MAX(MIN) value.
776 Cannot fetch serial object.
777 Cannot update serial object.
778 Cannot create a symbolic link "%s" to the file "%s".
779 Date exceeds limit. Date MUST be in 1 and 9999 .
780 Internal system failure: unable to get system time.
781 Format string is too long. It must be less than 16K.
782 Empty string not allowed here.
783 Invalid format.
784 Two arguments are mismatched.
785 First argument string is too long. It must be less than 16K.
786 Format is duplicated.
787 ERROR: Conversion error in time format.
788 ERROR: Conversion error in timestamp format.
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
817 pthread_set_specific() failed.
818 pthread_getspecific() failed.
819 pthread_once() failed.
820 Cannot initialize mutex for connection.
821 Cannot destroy mutex for connection.
822 Mutex operation failed while getting next client id.
823 Mutex operation failed while initializing connection list.
824 Cannot destroy mutex while finalizing connection list.
825 pread() call failed.
826 pwrite() call failed.
827 Connection list traverse function returned invalid value.
828 Invalid critical section index is used.
829 This mutex lock has been unlocked already.
830 Cannot allocate query entry any more. Maximum allocatable entries are %d.
831 All transaction descriptors(%d) are in use. Raise up max_clients parameter.
832 Backup is already running. Backup cannot be run by two processes.
833 The index name "%1$s" is ambiguous. Specify the class name of the index.
834 There are some mismatches between source string "%1$s" and format string "%2$s" given in to_number().
835 Undefined statistics item.
836 LATCH ON PAGE(%d|%d) TIMEDOUT
837 Cannot drop user, who owns database object(class/trigger/serial etc)s.
838 Cannot open more than %d index scans simultaneously.
839 "%s": Unknown system parameter or bad value.
840 Cannot change system parameter "%s".
841 Server system parameter can be set by only sole client(transaction).
842 Cannot cache lock of object %1$d|%2$d|%3$d.
843 Bad arguments in %s, a %s was passed.
844 Unknown isolation(%d) is found. The transaction index is %d.
845 Invalid object type(%d) is found. The lockable object is %d|%d|%d.
846 %s of tran(%d) is not found in the holder list of an object(%d|%d|%d).
847 %s on a %s(%d|%d|%d) is not found in the transaction(%d) hold list(cnt=%d).
848 %s on an object(%d|%d|%d) is not found in the transaction(%d) non2pl list.
849 Tring to abort a transaction(%d) twice.
850 Tran(%d) is neither lock holder nor lock waiter of an object(%d|%d|%d).
851 Lock resource (%s) allocation fail..
852 Total holders mode(%d) of an empty lock holder list is not NULL_LOCK.
853 For an object(%d|%d|%d), tran(%d) of Uncommitted Read held fault lock(%s).
854 WARNING: No lock holder, but lock waiters exist. refer to %s.
855 WARNING: Two or more threads are lock-waiting for one transaction(%d).
856 WARNING: Strange lockwait state(%d|%d), thread(%d|%d) and transaction(%d).
857 An I/O error occurred while truncating page %1$d of volume "%2$s".
858 Volume "%2$s" is unknown at server restart".
859 LATCH ON PAGE(%d|%d) ABORTED
860 Cannot make current transaction as a part of a global transaction because its state is '%1$s'.
861 Cannot prepare to commit the current transaction with transaction identifier %1$d because that transaction is not made as a part of a global transaction.
862 Cannot set the user information of the global transaction with the identifier %1$d because its state is '%2$s'.
863 Cannot get the user information of the global transaction with the identifier %1$d because it is not set.
864 Cannot open backupdb/restoredb verbose file '%s'.
865 Cannot execute statement due to unknown type input markers.
873 PKI authentication error "%1$s".
874 Argument of power() is out of range.
875 Overflow happened in power context.
877 No data to be unloaded.
878 Invalid database location file, '%1$s'.
879 Cannot get permission: '%s'.
880 Cannot change permission '%s'.
881 Cannot get lock file '%s(FILE ID:%d)'.
882 Cannot release lock file 'FILE ID:%d'.
883 Failure to get '%s' file status.
884 Volext-pathname '%s' is write permission denied.
885 Failure to move from '%s' to '%s'.  Use the same device(disk).
886 "%1$s" caused unique constraint violation.
887 Stored procedure '%s' is already exist.
888 Parameter count is invalid. expected: %d, actual: %d
889 Stored procedure execute error: %s
890 Partition work failed.
891 Appropriate partition does not exist.
892 Primary key "%2$s" already defined for class "%1$s".
893 Attribute '%1$s', a member of the primary key, could not be dropped.
894 Stored procedure/function '%s' is not exist.
895 Invalid stored procedure type: '%s' is the '%s'.
896 Compression failed. Zip Method: %d (%s), Zip Level: %d (%s)
897 Decompression failed.
898 Replication Error: "%1$s"
899 Invalid partition requests.
900 Java VM library is not found : %s. \nConsequently, calling java stored procedure is not allowed.
901 Java VM can not be started: %s. \nConsequently, calling java stored procedure is not allowed.
902 Java VM is not running.
903 Can't connect Java VM: %s
904 Invalid stored procedure name.
905 Networking with JVM failed: %d
906 Invalid java method signature.
907 The owner or a member of DBA group can drop stored procedure.
908 Too many arguments of stored procedure '%s'.
909 Missing or invalid catalog class/vclass is found.
910 Not allowed access to partition.
911 Invalid call: it can not return ResultSet.
912 ResultSet can not be used input parameter.
913 Too many nested stored procedure call.
914 A serial object already exists as an auto increment constraint.
915 Increase value of an auto increment constraint should be greater than 0.
916 Active log volume is not included in this backup volume(%s).
917 Active log volume already exists.
918 The class '%s' referred by the foreign key does not exist.
919 The class '%s' referred by the foreign key does not have the primary key.
920 The foreign key '%s' does not include the primary key member '%s'.
921 The domain of the foreign key member '%s' is different from that of the primary key member '%s'.
922 The constraint of the foreign key '%s' is invalid.
923 The primary key referred by a foreign key is not supposed to be dropped.
924 Update/Delete operations are restricted by the foreign key '%s'.
925 Acquiring lock for the foreign key '%s' has failed.
926 The instance having the foreign key '%s' cannot be dropped.
927 The number of keys of the foreign key '%s' is different from that of the primary key '%s'.
928 Not a user but the system can assign the attribute '%s' created by 'ON CACHE OBJECT' of foreign key.
929 A foreign key cannot be defined in virtual class.
930 It is able to drop the attribute '%s' created by 'ON CACHE OBJECT' of foreign key only after removing the constraint of the foreign key.
931 Start value of an auto increment constraint should be less then max value.
932 It is not able to repair an inconsistency of the number of free pages for the volume "%1$s".  Because %2$d pages obtained from the volume header is inconsistent with %3$d pages from the bitmap.
933 It is not able to repair an inconsistency of the number of free sectors for the volume "%1$s".  Because %2$d sectors obtained from the volume header is inconsistent with %3$d sectors from the bitmap.
934 Null domain referenced.
935 Argument of %s function is out of range.
936 Overflow happened in exp context.
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
965 Cannot use duplicate attribute names in index key list. Attribute name '%s' listed more than once.
966 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on object %6$d|%7$d|%8$d because of deadlock. You are waiting for user(s) %9$s to finish.
967 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on class %6$s because of deadlock. You are waiting for user(s) %7$s to finish.
968 Your transaction (index %1$d, %2$s@%3$s|%4$d) timed out waiting on %5$s lock on instance %6$d|%7$d|%8$d of class %9$s because of deadlock. You are waiting for user(s) %10$s to finish.

969 Last Error

$set 6 MSGCAT_SET_INTERNAL
1 Error in error subsystem (line %d):
2 No error message available.
3 Can't allocate %d bytes.
4 Can't find message catalog "%s".
5 Missing message for error code %d.
6 ... ABORT/EXIT IMMEDIATELY ...\n
7 Do you want to exit? 1/0
8 Unknown file.
9 No message available; this message substituted for erroneously formatted original.
10 er_init: *** Incorrect exit_ask value = %d; will assume %s instead. ***\n
11 er_start: *** WARNING: Unable to open message log "%s"; will assume stderr instead. ***\n
12 er_study_fmt: suspect message for error code %d.
13 er_estimate_size: unknown conversion code (%d).
14 \n\n*** Message log wraparound. Messages will continue at top of the file. ***\n\n
15 \nTime: %s - %s *** %s CODE = %d, Tran = %d\n%s\n
16 System (pid: %d) *** %s *** %s CODE = %d, Tran = %d %s
17 \nTime: %s - %s *** file %s, line %d %s CODE = %d Tran = %d\n%s\n
18 System (pid: %d) *** %s *** file %s, line %d, %s CODE = %d, Tran = %d. %s
19 \n*** The previous error message is the last one. ***\n\n
20 \nDEBUG/NOTIFICATION *** file %s, line %d\n
21 Mail -s "Server has been stopped"
22 --->>>\n %s has been stopped at your request when the following error was set:\n errid = %d, %s\nUser: %s, pid: %d, host: %s, time: %s<<<---\n
23 %s has been stopped on errid = %d. User: %s, pid: %d

$set 7 MSGCAT_SET_PARSER_SYNTAX
1 please check syntax at '%s', expecting %s.
2 please check syntax at '%s', expecting %s in %s.
3 please check correctness of token '%s'.
4 'read uncommitted schema' is not acceptable, using \n'read committed schema' instead.
5 Maximum char string length is 16383.
6 %s exceeds limit of %ld.
7 Maximum bit string length is %d bits.
8 Cannot use a subquery in join condition clause.
9 Cannot specify attribute "(%s)" in update privilege clause of grant/revoke statement.
10 invalid use of keyword '%s', expecting %s.
11 Token length is too long.

$set 8 MSGCAT_SET_PARSER_SEMANTIC
1 Out of memory.
2 Function %s is undefined.
3 Cannot find ldb '%s' in the database.
4 The number of columns, %d, in the left query do not match\nthe number of columns, %d, in the right query.
5 '%s' is not union compatible with '%s'.
6 %s is not defined.
7 The domain of %s cannot be a vclass.
8 The domain of %s cannot be a proxy vclass.
9 The domain of %s cannot be a class.
10 %s is not an attribute of %s.
11 %s cannot be changed from this class.\nIt was inherited from %s.
12 Class %s does not exist.
13 %s is not a %s.
14 %s is not a method or attribute of %s.
15 %s does not have and must not have any query specifications.
16 %s is not defined in the database.
17 %s is not a superclass of %s.
18 Class %s inherits from non-class %s.
19 VClass %s inherits from non-vclass %s.
20 Proxy vclass %s inherits from non-proxy vclass %s.
21 %s is already a superclass of %s.
22 %s may not be a superclass of itself.
23 %s is already a subclass of %s.
24 %s is not single valued. Attributes exposed in\naggregate queries must also appear in the group by clause.
25 The number of columns(%d) and into_names(%d) are not equal.
26 Class %s already exists.
27 Super class %s in '%s' does not exist.
28 Expecting a SUBCLASS clause to make INHERIT clause '%s' meaningful.
29 INHERIT clause '%s' references a class that is not in the SUBCLASS clause '%s'.
30 Class CANNOT have a query specification.
31 %s is not a registered ldb.
32 A proxy heir may not have its own object_id clause.  It cannot override its inherited object_id attribute(s).
33 Proxy vclass has more than one query specification.
34 The number of attributes, %d, does not match the number of columns, \n%d, in the query specification.
35 Missing an expected attribute name for %s.
36 Attribute '%s' is not compatible with column '%s' of the query specification.
37 %s is not a class.
38 Indices may not be created on virtual classes.
39 Indices can only be created on a single attribute.
40 Illegal use of multiple classes in the from clause.
41 System error -- Resolution failed on %s.
42 You may not delete from a derived table.
43 User %s is not in the database.
44 Update cannot have aggregation.
45 Attribute names in each class must be unique. Attribute name '%s' in class '%s' is specified more than once.
46 There are multiple assignments to %s.
47 Number in sort spec is not an integer.
48 %d in sort spec is out of range.
49 Order by item is not an integer.
50 %s in sort spec doesn't match any select column.
51 There are conflicting sort directions for column %s.
52 %s in sort spec is not a number, name or path expression.
53 Insert expression may only appear in insert statement.
54 The number of attributes(%d) and select columns(%d) are not equal.
55 The number of attributes(%d) and values(%d) are not equal.
56 Data overflow on data type %s.
57 Incompatible data type on attribute %s.
58 %s\n is not a SELECT from a single class.
59 `*' is not a valid select list in a proxy vclass query specification.
60 %s is neither a vclass nor a proxy vclass.
61 There are more than one isolation level clauses.
62 There are more than one timeout clauses.
63 No isolation level was entered.
64 BETWEEN operator is not defined on types %s, %s and %s.
65 '%s' operator is not defined on types %s and %s.
66 Function must have arguments: %s.
67 Incompatible operands: %s(%s).
68 Aggregate function must have 1 argument: %s.
69 Attempt to divide by zero.
70 Data overflow coercing %s to type %s.
71 Cannot coerce %s to type %s.
72 The type of '%s' must be SET, MULTISET, or SEQUENCE.
73 '%s' should have exactly one set element type.
74 %s has more than one derived column name.
75 %s has no derived column name.
76 %s does not have a corresponding number of columns (%d) and column names (%d).
77 There are no attributes in %s.
78 Expecting single table in %s.
79 Exposed name %s is ambiguous.
80 Derived column names '%s' are not allowed for simple class specifications.
81 Derived table '%s' must have a correlation name.
82 Select authorization failure, class %s.
83 Class instance reference %s to a derived table name is not allowed.
84 Path correlation (%s) appears outside an inner path term.
85 Reference to %s is ambiguous.
86 Class of %s does not have attribute %s.
87 Classes (%s) do not all have attribute %s.
88 Class (%s) does not have attribute %s.
89 %s is a %s type, not an object type.
90 Selector {%s} does not resolve to an exposed class specification in\n the required context (%s).
91 Selector {%s} refers to non-class specification (%s).
92 Selector {%s} cannot root the path expression defining itself.
93 Selector {%s} is circularly defined in terms of selector {%s}.
94 Selector {%s} is redefined at %s.
95 %s (domain object) does not have attribute %s.
96 %s in EXCEPT spec is not a subclass.
97 ALL ... EXCEPT list excludes EVERYTHING in class.
98 '%s' is inconsistent with\nother use of path selector '%s' or of path root '%s'.
99 Undefined argument.
100 Argument is not an object.
101 %s is not a class method or attribute of %s.
102 Query specification must not have an into-clause.
103 Proxy query specification must have no aggregate expression,\nno group-by-clause, and no having-clause.
104 Proxy query specification must not have the DISTINCT nor UNIQUE \nkeyword in the select list.
105 Proxy query specification must be a simple SELECT statement.
106 Proxy query specification must reference only one table or class \nfrom the local database.
107 User '%s' is not the owner of %s.
108 Proxy '%s' is already defined on class '%s' of ldb '%s'.
109 CUBRID does not support proxy's with objects in heterogeneous sets.
110 Where-clause must be a logical type expression.
111 Like escape clause must be a literal string.
112 Invalid combination of precision (%d) and scale (%d).
113 Given precision of %1$d is invalid; it should be greater than %2$d and no greater than %3$d.
114 Precision (%d) too large. Maximum precision is %d.
115 Method %s must have a return type.
116 Number of arguments supplied to the method does not equal the \nnumber defined for the method.
117 Method call must have a target.
118 Method %s is undefined for the supplied target.
119 Instance methods cannot be called on class objects.
120 Class methods cannot be called on instance objects.
121 Class methods require a class object as their target.
122 Methods require an object as their target.
123 Method %s is not fully typed, check method definition in class schema.
124 Time arithmetic underflow.
125 Date arithmetic underflow.
126 Illegal left hand side of an assignment clause.
127 Method calls on set derived tables correlated to an outer scope are \nnot supported unless the set expression is wrapped in a subquery.
128 The query specification column that corresponds to the \nshared attribute '%s' must be NULL or NA.
129 The query specification must have some non-NULL columns in the select list.
130 Vclass objects in sets are not supported.
131 Expecting a string value.
132 Expecting a statement or a string value.
133 Proxy object_id attribute '%s' must not map to \na query specification column expression.
134 %s constraint ignored.
135 Constraint declarations not allowed in this context.
136 Casting %s to type %s is not supported.
137 %s is not a valid bit string.
138 '%s' operator is not defined on type %s.
139 '%s' operator is not defined on types %s, %s, and %s.
140 Operands of operator '%s' cannot both be dynamic host variables.
141 Too many aguments to generic function %s.\nOnly %d arguments are allowed for generic functions.
142 Selector {%s} class '%s' is not a subclass of '%s'.
143 Cannot extract '%s' field from type %s.
144 Query specification must not have an ORDER BY clause.
145 Case condition must be a logical type expression.
146 Expression should be of type %s.
147 Serial '%s' already exist.
148 Serial '%s' not defined.
149 Increment value must not be zero.
150 Maximum value exceeds 1.0e37.
151 Minimum value is invalid.
152 Increment value is invalid.
153 Minimum value exceeds -1.0e36.
154 Maximum value is invalid.
155 Specify alter serial option.
156 Creator or DBA can alter target serial.
157 Serial values cannot exceeds its minimum or maximum
158 Internal I/O error for serial
159 Cannot find the index '%s'.
160 The qualified index name '%s' refers to a class, but it isn't specified in from clause.
161 Two or more classes have the same index '%s'. Use the qualified index names.
162 Invalid use of %s.\n%s expression cannot be used with attribute or correlated subquery.
163 Current release restricts a expression containing ORDERBY_NUM() in select list.
164 Path expression '%s' in join condition.
165 '%s' in join condition is not defined.
166 Current release restricts set derived table for right outer join.
167 Cannot insert with query on other ldb.
168 Only one column can be specified in the select list when the subquery is not introduced with EXISTS.
169 Illegal right hand side of an assignment clause. Context requires query to be single column.
170 Cannot use a subquery in join condition clause.
171 Invalid partition definition.
172 Partition column must be specified.
173 Only one column can be specified as partition column.
174 Unsupported partition column type.
175 Partition definition is duplicated. '%s'
176 Constant type mismatch. '%s'
177 Invalid partition size.
178 '%s' is not partitioned class.
179 '%s' is already partitioned class.
180 Invalid partition requests.
181 %s cannot be changed from this class(partition key column).
182 %s cannot be partition-managed under active triggers.
183 Partition %s does not exist.
184 Partition type mismatches. '%s'
185 Partition information is invalid.
186 Partition addition is to set with a bigger range than the existing.
187 Reorganization of partitions is not permitted because it may cause data loss.
188 The range of the partition '%s' is invalid.
189 A class attribute cannot have an auto increment constraint.
190 An attribute of virtual class cannot have an auto increment constraint.
191 The domain of the attribute '%s' having an auto increment constraint is invalid.
192 Not a user but the system can assign the attribute '%s' created by 'ON CACHE OBJECT' of foreign key.
193 Cannot use duplicate attribute names in index key list. Attribute name '%s' listed more than once.

$set 9 MSGCAT_SET_PARSER_RUNTIME
1 Out of virtual memory: unable to allocate %d bytes.
2 Transaction isolation level must be 1, 2, 3, 4, 5 or 6.
3 Timeout value must be -1, 0, or >0.
4 Isolation level set to:\n
5          READ COMMITTED SCHEMA, READ UNCOMMITTED INSTANCES.\n
6          READ COMMITTED SCHEMA, READ COMMITTED INSTANCES.\n
7          REPEATABLE READ SCHEMA, READ UNCOMMITTED INSTANCES.\n
8          REPEATABLE READ SCHEMA, READ COMMITTED INSTANCES.\n
9          REPEATABLE READ SCHEMA, REPEATABLE READ INSTANCES.\n
10 A non-updatable query is not allowed in a db_query_execute_oid call.
11 Transaction timed out waiting for locks.
12 Attempted to access host variable with index %ld when only %ld \ninput host variables were given.
13 Unimplemented conversion.
14 Internal error, undefined conversion.
15 '%s' yields no results.
16 '%s' yields more than one row.
17 Interpreter variable %s is not defined.
18 Cannot evaluate '%s'.
19 Unknown variable %s.
20 Unknown shared attribute %s.
21 '%s' yields more than one column.
22 Nested or invalid use of aggregate function.
23 Function %s is not defined.
24 Context requires query to be single column\n(%s).
25 Too many values have been specified.
26 Virtual assignment target (%s) is not invertible.
27 There are more columns in the query specification than attributes in class %s.
28 There are more attributes in class %s than columns in the query specification.
29 Current release restricts aggregation to vclasses WITHOUT \naggregate query specifications.
30 Current release restricts aggregation to vclasses WITHOUT \nunion query specifications.
31 Use of vclass (%s) in a composition hierarchy requires it to be updatable.\nIt cannot be updated with respect to real class %s.
32 not used
33 Illegal construct in path expression '%s'.
34 path expression '%s' crosses db boundary to %s, with vclass %s.
35 path expression '%s' crosses db boundary from %s, to class %s.
36 %s is not authorized on %s.
37 Select is not authorized on %s.
38 Vclass %s is not updatable.
39 %s is not a proxy vclass.
40 query spec of proxy vclass must not be a join.
41 %s has no object id set.
42 Classes (%s) with multiple object_id fields in a composition \nhierarchy are not supported.
43 Object attribute %s of %s is not a vclass on ldb %s.
44 Query spec type incompatible with attribute %s.
45 The query specification of %s has a cyclic reference to itself.
46 No expression given to evaluate.
47 No real class found for vclass %s.
48 Executing '%s' on ldb '%s' \nreturned: %s.
49 Putting value '%s' into attribute '%s' returned: %s.
50 Ldb name must not be null in %s.
51 Ldb name at host must not be null in %s.
53 Host name must not be null in %s.
54 User name must not be null in %s.
55 User password must not be null in %s.
56 Insert translates to empty statement.
57 Update translates to empty statement.
58 Delete translates to empty statement.
59 INGRES does not support dates (%s) before January 1, 1582.
60 Resources exhausted in query generation.
61 Query yields no result, so variable '%s' is not set.
62 With LOCAL check option is not supported.
63 Check option exception on view %s.
64 Invalid date: '%s'.
65 Invalid time: '%s'.
66 Invalid utime: '%s'.
67 Subqueries in a proxy's query specification are not supported.
68 Ldb function argument is not compatible with ldb in '%s'.
69 Invalid numeric: '%s'.
70 Outer join query optimization failed.
71         SERIALIZABLE.\n
72 Vclass %s is not updatable. Only updatable vclasses are allowed to select VIDs.
73 Partitioning failed.
74 Partition does not exist.
75 Not allowed access to partition: '%s'.
76 The serial '%s' is not defined yet.
77 The owner or DBA can only alter the definition of the target serial.
78 It is not allowed for users to modify the serial object created by an auto increment constraint.
79 Invalid isolation level: %s schema, %s instances.

$set 11 MSGCAT_SET_AUTHORIZATION
1 *** invalid cache ***
2    class %s
3 from %s :
4 Authorization for user %s\n
5 Authorization for undefined user\n
6 User : %s\n
7            id : %d\n
8       members :
9        groups :
10 User %s\n
11 Current user is : %s\n
12 Root authorization object :\n
13   users :
14 *** Couldn't issue all grants for user "%s"*** \n
15 AUTHORIZATIONS\n
16 direct_groups :

$set 12 MSGCAT_SET_HELP
1 Rootclass
2 %s %s\n
3 Super classes :\n
4 Sub classes :\n
5 Attributes: \n
6 Methods: \n
7 Class attributes: \n
8 Class methods: \n
9 Conflict resolutions:\n
10 Method files:\n
11 Query specification:\n
12 OBJECT of %s\n
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
1 TRACE: Evaluating condition for trigger '%s'.
2 TRACE: Executing action for trigger '%s'.

$set 14 MSGCAT_SET_LOCK
1  \n
2  Thread (index %3d) of transaction (index %3d, %s, %s@%s|%d) has been suspended.\n
3  Thread (index %3d) of transaction (index %3d, %s, %s@%s|%d) has been resumed.\n
4  Transaction index %3d ACQUIRED %s lock on object %d|%d|%d.\n
5  Transaction index %3d ACQUIRED %s lock on page %d|%d.\n
6  Transaction index %3d CONVERTED its lock to %s lock on object %d|%d|%d.\n
7  Transaction index %3d CONVERTED its lock to %s lock on page %d|%d.\n
8  Transaction index %3d WAIT FOR %s lock on object %d|%d|%d.\n
9  Transaction index %3d WAIT FOR %s lock on page %d|%d.\n
10 Transaction index %3d RELEASE %s lock on object %d|%d|%d.\n
11 Transaction index %3d RELEASE %s lock on page %d|%d.\n
12 Transaction index %3d DEMOTE lock to %s on object %d|%d|%d.\n
13 Transaction index %3d DEMOTE lock to %s on page %d|%d.\n
14 OID = %2d|%6d|%4d\n
15 Object type: Root class.\n
16 Object type: Class = %s.\n
17 Object type: Instance of class (%2d|%6d|%4d) = %s.\n
18 Object type: Unknown.\n
19 Total mode of holders = %8s, Total mode of waiters = %8s.\n
20 Num holders=%3d, Num blocked-holders=%3d, Num waiters=%3d\n
21 LOCK HOLDERS:\n
22 BLOCKED LOCK HOLDERS:\n
23 LOCK WAITERS:\n
24 NON_2PL_RELEASED:\n
25 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d\n
26 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d, Nsubgranules = %2d\n
27 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d\n\t%13s Blocked_mode = %8s\n\t%17s Start_waiting_at = %-30s\n\t%17s Wait_for_nsecs = %d\n
28 %4sTran_index = %3d, Granted_mode =%8s, Count = %3d, Nsubgranules = %2d\n\t%13s Blocked_mode =%8s\n\t%13s Start_waiting_at = %-30s\n\t%13s Wait_for_nsecs = %d\n
29 %4sTran_index = %3d, Blocked_mode =%8s \n\t%13s Start_waiting_at = %-30s \n\t%13s Wait_for_nsecs = %d\n
30 %4sTran_index = %3d, Non_2_phase_lock =%8s\n
31 VPID (volid|pageid) = %3d|%6d.\n
32 *** Lock Table Dump ***\n Lock Escalation at = %d, Run Deadlock interval = %d\n
33 Transaction (index %2d, %s, %s@%s|%d)\n
34 Isolation %s\n
35 State %s\n
36 Timeout_period %d\n
37 Unilaterally aborted transactions with indices:\n
38 %d ,
39 Timed_out transactions with indices:\n
40 %d ,
41 Solving the deadlock by selecting a transaction and a function \n to break the deadlock: %d %s
42 , %d %s

$set 15 MSGCAT_SET_IO
1 \n*************************************************************************\n
2 Backup = %s is needed. \n It must be mounted/loaded to continue.\n Type 1 to continue after the backup is mounted/loaded.\n Type 0 to quit.\n
3 \n\n*** BACKUP HEADER INFORMATION ***\n\n
4 Backup magic file identification %s,\n
5          Release: %s\n\
     Disk Version: %g\n
6    Database Name: %s\n\
 DB Creation Time: %s\
         Pagesize: %d\n
7     Backup Level: %d (%s)\n\
        Start_lsa: %d|%d\n\
         Last_lsa: %d|%d\n
8      Backup Time: %s\
  Backup Unit Num: %d\n
9  Database Volume name: %s\n\
   Volume Identifier: %d, Size: %d bytes (%d pages)\n
10   Database Name: %s\n\
     Volume Name: %s\n\
        Unit Num: %d\n\
    Backup Level: %d (%s)\n\
Enter one of the following options:\n
11 Type\n \
  -  %d to quit.\n \
  -  %d to continue after the volume is mounted/loaded. (retry) \n \
  -  %d to continue after changing the volume's directory or device.\n
12 Enter new directory or device for backup volume.\n
13 Error in input, choose a value between %d and %d.\n
14 ** WARNING ** Incorrect backup volume mounted.\n
15 Backup volume %s is from another backup level\n   (Level: %d, expecting %d).\n\n
16 Backup volume %s has incorrect page or magic identifier.\n\n
17 Backup volume %s is from another database\n  (Name: %s, expecting %s).\n\n
18 Backup volume %s out of sequence\n  (Unit Num: %d, expecting %d).\n\n
19 Backup volume %s timestamp mismatch\n  (Timestamp: %s, expecting %s).\n\n
20 Backup volume information file error, line %d.\n
21 Backup Volume Label: Level: %d, Unit: %d, Database %s, Backup Time: %s\n
22     Previous Backup level: %d Time: %s (start_lsa was %d|%d)\n
23 The following database backup volume is needed to continue restoring:\n\n
24 Backup destination is full, a new destination is required to continue:\n\n
25 Preceding Backup Volume Name: %s\n
26     Next Backup Volume Name: %s\n
27  Backup Pagesize: %d\n
28       Zip Method: %d (%s)\n\
        Zip Level: %d (%s)\n
29 Include Active Log: %s\n

$set 16 MSGCAT_SET_LOG
1 \n*************************************************************************\n
2 Log Archive %s \n is needed to continue normal execution.   \n \
  Type\n \
  -  0 to quit.\n \
  -  1 to continue without present archive. (Partial recovery)\n \
  -  2 to continue after the archive is mounted/loaded.       \n \
  -  3 to continue after changing location/name of archive.   \n
3 Backup information file %s \n is needed to continue the restore process. \n \
  Type\n \
  -  0 to quit.\n \
  -  1 to continue after the file is mounted/loaded.       \n \
  -  2 to continue after changing location/name of file.   \n
4 Enter new location/name.\n
5 COMMENT: %s for database %s\n
6 COMMENT: Log archive %s is not needed any longer unless a database media crash occurs.\n
7 COMMENT: Log archives from %s to %s are not needed any longer unless a database media crash occurs.\n
8 COMMENT: from renamed database = %s\n
9 ARCHIVE: %d %s %d %d\n
10 ARCHIVE:
11 REMOVE: %d %s to \n \
       %d %s.\n \
       REASON: %s\n
12 ACTIVE: %s %d pages\n
13  Finish commit of tran_index %d with tranid = %d,\n just before LSA= %d|%d and at %s
14  Finish abort of tran_index %d with tranid = %d,\n just before LSA= %d|%d and at %s
15  Incomplete Media Recovery is in effect. The database will be \n \
 recovered up to LSA point address of %d|%d. Last commit/abort \n \
 point time is approximately at %s \n \
 The database has lost all information of transactions committed \n \
 after this time.\n
16 Reset of log due to incomplete recovery.
17 Database backup was taken.
18 Media crash is not important for the application.
19 Backing up to directory %s which seems to have an earlier backup in it.\n\
Obsolete backup volumes of this database in this location will be removed,\n\
including those from the current backup level and higher.\n
20 Enter 'y' to continue or any other key to cancel.\n
21 Database backup was aborted by the user.\n
22 DELETE POSTPONED: Archives %d %s to \n \
        %d %s \n \
        are no longer needed unless a restore from current backup occurs.\n
23 A newer backup has released archives previously marked DELETE POSTPONED.\n
24 COMMENT: Log archive %s is not DELETE POSTPONED any longer due to more recent\n \
   backup and therefore is not needed unless a database media crash occurs.\n
25 COMMENT: Log archives from %s to %s are not DELETE POSTPONED any longer due to\n \
   more recent backup and therefore are not needed unless a database media\n \
   crash occurs.\n
26 An error occurred during a database restore, while processing\n\
     Backup Volume: %s\n\
          Unit Num: %d\n\
   Database Volume: %s\n\
    Database Volid: %d\n\
** WARNING: Do not use the database until an error-free restore is achieved.\n
27 Error in input, choose a value between %d and %d.\n
28 The time(%s) specified must be after the time(%s) of the specified backup.\n
