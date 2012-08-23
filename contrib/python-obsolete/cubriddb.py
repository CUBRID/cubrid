#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met: 
#
# - Redistributions of source code must retain the above copyright notice, 
#   this list of conditions and the following disclaimer. 
#
# - Redistributions in binary form must reproduce the above copyright notice, 
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution. 
#
# - Neither the name of the <ORGANIZATION> nor the names of its contributors 
#   may be used to endorse or promote products derived from this software without 
#   specific prior written permission. 
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
# IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
# OF SUCH DAMAGE. 
#
#

"""
  cubriddb - Python DB API v2.0 Module for CUBRID
  v0.5
  author : Kang, Dong-Wan <cubrid_python@nhncorp.com>
  homepage  : http://dev.naver.com/projects/cubrid-python
"""

import cubrid
import time
import types

# /* Exceptions */ #
Warning             = cubrid.Warning
Error               = cubrid.Error
InterfaceError      = cubrid.InterfaceError
DatabaseError       = cubrid.DatabaseError
DataError           = cubrid.DataError
OperationalError    = cubrid.OperationalError
IntegrityError      = cubrid.IntegrityError
InternalError       = cubrid.InternalError
ProgrammingError    = cubrid.ProgrammingError
NotSupportedError   = cubrid.NotSupportedError

class Date:
    def __init__(self, year, month, day):
        self.value = (year, month, day)

    def __str__(self):
        return '%04d-%02d-%02d' % self.value

class Time:
    def __init__(self, hour, min, sec):
        self.value = (hour, min, sec)

    def __str__(self):
        return '%02d:%02d:%02d' % self.value

class Timestamp:
    def __init__(self, year, month, day, hour, min, sec):
        self.value = (year, month, day, hour, min, sec)

    def __str__(self):
        return '%04d-%02d-%02d %02d:%02d:%02d' % self.value

def DateFromTicks(ticks=None):
    if ticks == None: ticks = time.time()
    return Date(*time.localtime(ticks)[:3])

def TimeFromTicks(ticks=None):
    if ticks == None: ticks = time.time()
    return Time(*time.localtime(ticks)[3:6])

def TimestampFromTicks(ticks=None):
    if ticks == None: ticks = time.time()
    return Timestamp(*time.localtime(ticks)[:6])

class Cursor:
    def __init__(self, _cs):
        self._cs = _cs
        self.arraysize = 10

    def __del__(self):
        pass

    def __getattr__(self, name):
        return getattr(self._cs, name)

    def _convert_params(self, args):
        args = list(args)
        for i in range(len(args)):
            args[i] = str(args[i])

        return tuple(args)

    def execute(self, stmt, *args):
        if len(args) == 1 and type(args[0]) in (tuple, list):
            args = args[0]

        args = self._convert_params(args)
        return self._cs.execute(stmt, *(args, ))

    def executemany(self, stmt, seq_params):
        for p in seq_params:
            self.execute(stmt, *(p, ))

    def fetchone(self, **kwargs):
        return self._cs.fetch()

    def fetchmany(self, size=None):
        if size == None: size = self.arraysize
        if size <= 0:
            return []

        rlist = []
        for i in range(size):
            r = self.fetchone()
            if not r:
                break
            rlist.append(r)
        return rlist
                        
    def fetchall(self):
        rlist = []
        while 1:
            r = self.fetchone()
            if not r:
                break
            rlist.append(r)
        return rlist

    def nextset(self):
        raise NotSupportedError

    def setinputsizes(self, sizes):
        raise NotSupportedError

    def setoutputsize(self, size, column):
        raise NotSupportedError

    def callproc(self, procname, *args):
        pass

class DictCursor(Cursor):
    def fetchone(self):
        r = {}
        data = Cursor.fetchone(self)
        if not data:
            return r;

        desc = self._cs.description

        for i in range(len(desc)):
            name = desc[i][0]
            r[name] = data[i]

        return r

class Connection:
    def __init__(self, *args, **kwargs):
        self._db = cubrid.connect(*args, **kwargs)

    def __del__(self):
        pass

    def __str__(self):
        return str(self._db)

    def close(self):
        self._db.close()

    def cursor(self, dictCursor=0):
        if dictCursor:
            cursorClass = DictCursor
        else:
            cursorClass = Cursor
        return cursorClass(self._db.cursor())

    def commit(self):
        self._db.commit()

    def rollback(self):
        self._db.rollback()

connect         = Connection
apilevel        = '2.0'
threadsafety    = 0
paramstyle      = 'qmark'

