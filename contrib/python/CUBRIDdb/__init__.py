"""
CUBRIDdb - A DB API v2.0 compatible interface to CUBRID.

This package is a wrapper around _cubrid.

connect() -- connects to server

"""
import _cubrid

threadsafety = 2
apilevel = "2.0"
paramstyle = 'qmark'

from _cubrid import *
from CUBRIDdb import FIELD_TYPE

from _cubrid_exceptions import Warning, Error, InterfaceError, DataError, \
        DatabaseError, OperationalError, IntegrityError, InternalError, \
        NotSupportedError, ProgrammingError

from time import localtime
from datetime import date, datetime, time

Date = date
Time = time
Timestamp = datetime

def DateFromTicks(ticks):
    return date(*localtime(ticks)[:3])

def TimeFromTicks(ticks):
    return time(*localtime(ticks)[3:6])

def TimestampFromTicks(ticks):
    return datetime(*localtime(ticks)[:6])

try:
    frozenset
except NameError:
    from sets import ImmutableSet as frozenset

class DBAPISet(frozenset):
    """A special type of set for which A == x is true if A is a
    DBAPISet and x is a member of that set."""

    def __eq__(self, other):
        if isinstance(other, DBAPISet):
            return not self.difference(other)
        return other in self

STRING = DBAPISet([FIELD_TYPE.CHAR, FIELD_TYPE.STRING, FIELD_TYPE.NCHAR, FIELD_TYPE.VARCHAR])
BINARY = DBAPISet([FIELD_TYPE.BIT, FIELD_TYPE.VARBIT])
NUMBER = DBAPISet([FIELD_TYPE.NUMERIC, FIELD_TYPE.INT, FIELD_TYPE.SMALLINT, FIELD_TYPE.BIGINT])
DATETIME = DBAPISet([FIELD_TYPE.DATE, FIELD_TYPE.TIME, FIELD_TYPE.TIMESTAMP])
FLOAT = DBAPISet([FIELD_TYPE.FLOAT, FIELD_TYPE.DOUBLE])
SET = DBAPISet([FIELD_TYPE.SET, FIELD_TYPE.MULTISET, FIELD_TYPE.SEQUENCE])
BLOB = DBAPISet([FIELD_TYPE.BLOB])
CLOB = DBAPISet([FIELD_TYPE.CLOB])
ROWID = DBAPISet()

def Connect(*args, **kwargs):
    """Factory function for connections.Connection."""
    from CUBRIDdb.connections import Connection
    return Connection(*args, **kwargs)

connect = connection = Connect

Warning = Warning
Error = Error
InterfaceError = InterfaceError
DatabaseError = DatabaseError
DataError = DataError
OperationalError = OperationalError
IntegrityError = IntegrityError
InternalError = InternalError
ProgrammingError = ProgrammingError
NotSupportedError = NotSupportedError

__all__ = [ 'Connect', 'connection', 'connect', 'connections','DataError', 
    'DatabaseError', 'Error', 'IntegrityError', 'InterfaceError', 
    'InternalError', 'CUBRIDError', 'Warning', 'NotSupportedError', 
    'OperationalError', 'ProgrammingError', 'apilevel', 'Cursor', 
    'DictCursor', 'paramstyle', 'threadsafety', 'STRING', 'BINARY', 'NUMBER',
    'DATE', 'TIME', 'TIMESTAMP', 'DATETIME', 'ROWID', 'SET', 'BLOB', 'CLOB'] 
    
