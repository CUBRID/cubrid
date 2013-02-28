from django.db.backends import BaseDatabaseIntrospection
from CUBRIDdb import FIELD_TYPE

class DatabaseIntrospection(BaseDatabaseIntrospection):
    data_types_reverse = {
        FIELD_TYPE.CHAR : 'CharField',
        FIELD_TYPE.VARCHAR : 'CharField',
        FIELD_TYPE.NCHAR : 'CharField',
        FIELD_TYPE.VARNCHAR : 'CharField',
        FIELD_TYPE.NUMERIC : 'DecimalField',
        FIELD_TYPE.INT : 'IntegerField',
        FIELD_TYPE.SMALLINT : 'SmallIntegerField',
        FIELD_TYPE.BIGINT : 'BigIntegerField',
        FIELD_TYPE.FLOAT : 'FloatField',
        FIELD_TYPE.DOUBLE : 'FloatField',
        FIELD_TYPE.DATE : 'DateField',
        FIELD_TYPE.TIME : 'TimeField',
        FIELD_TYPE.TIMESTAMP : 'DateTimeField',
        FIELD_TYPE.STRING : 'CharField',
        FIELD_TYPE.SET : 'TextField',
        FIELD_TYPE.MULTISET : 'TextField',
        FIELD_TYPE.SEQUENCE : 'TextField',
        FIELD_TYPE.BLOB : 'TextField',
        FIELD_TYPE.CLOB : 'TextField',
    }

    def get_table_list(self, cursor):
        """Returns a list of table names in the current database."""
        cursor.execute("SHOW TABLES")
        return [row[0] for row in cursor.fetchall()]

    def table_name_converter(self, name):
        """Table name comparison is case insensitive under CUBRID"""
        return name.lower()

    def get_table_description(self, cursor, table_name):
        """Returns a description of the table, with the DB-API cursor.description interface."""
        cursor.execute("SELECT * FROM %s LIMIT 1" % self.connection.ops.quote_name(table_name))
        return cursor.description

