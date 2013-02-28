"""
This module implements connections for CUBRIDdb. Presently there is
only one class: Connection. Others are unlikely. However, you might
want to make your own subclasses. In most cases, you will probably
override Connection.default_cursor with a non-standard Cursor class.

"""
from CUBRIDdb.cursors import *
import types, _cubrid


class Connection(object):
    """CUBRID Database Connection Object"""

    def __init__(self, *args, **kwargs):

        'Create a connecton to the database.'
        self.charset = ''
        kwargs2 = kwargs.copy()
        self.charset = kwargs2.pop('charset', 'utf8')

        self.connection = _cubrid.connect(*args, **kwargs2)

    def __del__(self):
        pass

    def cursor(self, dictCursor = None):
        if dictCursor:
            cursorClass = DictCursor
        else:
            cursorClass = Cursor
        return cursorClass(self)
        
    def set_autocommit(self, value):
        if not isinstance(value, bool):
            raise ValueError("Parameter should be a boolean value")
        if value:
            switch = 'TRUE'
        else:
            switch = 'FALSE'
        self.connection.set_autocommit(switch)

    def get_autocommit(self):
        if self.connection.autocommit == 'TRUE':
            return True
        else:
            return False
        
    autocommit = property(get_autocommit, set_autocommit, doc = "autocommit value for current Cubrid session")

    def commit(self):
        self.connection.commit()

    def rollback(self):
        self.connection.rollback()

    def close(self):
        self.connection.close()

    def escape_string(self, buf):
        return self.connection.escape_string(buf)
