"""
Cubrid database backend for Django.

Requires CUBRIDdb: http://www.cubrid.org/wiki_apis
"""

import sys

try:
    import CUBRIDdb as Database
except ImportError, e:
    from django.core.exceptions import ImproperlyConfigured
    raise ImproperlyConfigured("Error loading CUBRIDdb module: %s" % e)

from django.db import utils
from django.db.backends import *
from django.db.backends.signals import connection_created
from django_cubrid.client import DatabaseClient
from django_cubrid.creation import DatabaseCreation
from django_cubrid.introspection import DatabaseIntrospection
from django_cubrid.validation import DatabaseValidation
from django.utils import timezone

DatabaseError = Database.DatabaseError
#IntegrityError = Database.IntegrityError

class CursorWrapper(object):
    """
    A thin wrapper around CUBRID's normal curosr class.

    """

    def __init__(self, cursor):
        self.cursor = cursor

    def execute(self, query, args=None):
        try:
            query = query.replace("%s", "?")
            
            return self.cursor.execute(query, args)

        except Database.DatabaseError, e:
            raise utils.DatabaseError, utils.DatabaseError(*tuple(e)), sys.exc_info()[2]

    def executemany(self, query, args):
        try:
            query = query.replace("%s", "?")
            return self.cursor.executemany(query, args)
        except Database.DatabaseError, e:
            raise utils.DatabaseError, utils.DatabaseError(*tuple(e)), sys.exc_info()[2]

    def __getattr__(self, attr):
        if attr in self.__dict__:
            return self.__dict__[attr]
        else:
            return getattr(self.cursor, attr)

    def __iter__(self):
        return iter(self.cursor)

class DatabaseFeatures(BaseDatabaseFeatures):
    interprets_empty_strings_as_nulls = True
    allows_group_by_pk = True 
    allows_primary_key_0 = False
    can_defer_constraint_checks = False
    supports_microsecond_precision = False
    update_can_self_select = False
    related_fields_match_type = True
    allow_sliced_subqueries = False
    supports_forward_references = False
    #can_return_id_from_insert = True
    has_select_for_update = True
    has_select_for_update_nowait = False
    supports_regex_backreferencing = False
    uses_autocommit = True
    can_distinct_on_fields = True
    requires_explicit_null_ordering_when_grouping = True
    supports_timezones = False

class DatabaseOperations(BaseDatabaseOperations):
    compiler_module = "django_cubrid.compiler"

    def date_extract_sql(self, lookup_type, field_name):
        if lookup_type == 'week_day':
            # DAYOFWEEK() returns an integer, 1-7, Sunday=1.
            # Note: WEEKDAY() returns 0-6, Monday=0.
            return "DAYOFWEEK(%s)" % field_name
        else:
            return "EXTRACT(%s FROM %s)" % (lookup_type.upper(), field_name)

    def date_trunc_sql(self, lookup_type, field_name):
        fields = ['year', 'month', 'day', 'hour', 'minute', 'second', 'milisecond']
        format = ('%%Y-', '%%m', '-%%d', ' %%H:', '%%i', ':%%s', '.%%ms') # Use double percents to escape.
        format_def = ('0000-', '01', '-01', ' 00:', '00', ':00', '.00')
        try:
            i = fields.index(lookup_type) + 1
        except ValueError:
            sql = field_name
        else:
            format_str = ''.join([f for f in format[:i]] + [f for f in format_def[i:]])
            sql = "CAST(DATE_FORMAT(%s, '%s') AS DATETIME)" % (field_name, format_str)
        return sql

    def drop_foreignkey_sql(self):
        return "DROP FOREIGN KEY"

    def force_no_ordering(self):
        return ["NULL"]

    def fulltext_search_sql(self, field_name):
        return 'MATCH (%s) AGAINST (%%s IN BOOLEAN MODE)' % field_name

    def quote_name(self, name):
        if name.startswith("`") and name.endswith("`"):
            return name # Quoting once is enough.
        return "`%s`" % name

    def no_limit_value(self):
        # 2**63 - 1
        return 9223372036854775807

    def last_insert_id(self, cursor, table_name, pk_name):
        cursor.execute("SELECT LAST_INSERT_ID()")
        result = cursor.fetchone()
        return result[0]

    def random_function_sql(self):
        return 'RAND()'

    def sql_flush(self, style, tables, sequences):
        # 'TRUNCATE x;', 'TRUNCATE y;', 'TRUNCATE z;'... style SQL statements
        # to clear all tables of all data
        # TODO: when there are FK constraints, the sqlflush command in django may be failed.
        if tables:
            sql = []
            for table in tables:
                sql.append('%s %s;' % (style.SQL_KEYWORD('TRUNCATE'), style.SQL_FIELD(self.quote_name(table))))

            # 'ALTER TABLE table AUTO_INCREMENT = 1;'... style SQL statements
            # to reset sequence indices
            sql.extend(["%s %s %s %s %s;" % \
                (style.SQL_KEYWORD('ALTER'),
                 style.SQL_KEYWORD('TABLE'),
                 style.SQL_TABLE(self.quote_name(sequence['table'])),
                 style.SQL_KEYWORD('AUTO_INCREMENT'),
                 style.SQL_FIELD('= 1'),
                ) for sequence in sequences])
            return sql
        else:
            return []

    def value_to_db_datetime(self, value):
        if value is None:
            return None

        # Check if CUBRID supports timezones
        if timezone.is_aware(value):
            if settings.USE_TZ:
                value = value.astimezone(timezone.utc).replace(tzinfo=None)
            else:
                raise ValueError("CUBRID does not support timezone-aware datetime when USE_TZ is False.")

        return unicode(value)

    def value_to_db_time(self, value):
        if value is None:
            return None

        # Check if CUBRID supports timezones
        if value.tzinfo is not None:
            raise ValueError("CUBRID does not support timezone-aware times.")

        return unicode(value)

    def year_lookup_bounds(self, value):
        # Again, no microseconds
        first = '%s-01-01 00:00:00.00'
        second = '%s-12-31 23:59:59.99'
        return [first % value, second % value]

    def max_name_length(self):
        return 64

class DatabaseWrapper(BaseDatabaseWrapper):
    vendor = 'cubrid'
    # Operators taken from MySQL implementation.
    # Check for differences between this syntax and CUBRID's.
    operators = {
        'exact': '= %s',
        'iexact': 'LIKE %s',
        'contains': 'IN %s',
        'icontains': 'LIKE %s',
        'regex': 'LIKE %s',
        'iregex': 'LIKE %s',
        'gt': '> %s',
        'gte': '>= %s',
        'lt': '< %s',
        'lte': '<= %s',
        'startswith': 'LIKE %s',
        'endswith': 'LIKE %s',
        'istartswith': 'LIKE %s',
        'iendswith': 'LIKE %s',
        }

    def __init__(self, *args, **kwargs):
        super(DatabaseWrapper, self).__init__(*args, **kwargs)

        self.server_version = None
        self.features = DatabaseFeatures(self)
        self.ops = DatabaseOperations(self)
        self.client = DatabaseClient(self)
        self.creation = DatabaseCreation(self)
        self.introspection = DatabaseIntrospection(self)
        self.validation = DatabaseValidation(self)

    def _valid_connection(self):
        if self.connection is not None:
            try:
                self.connection.commit()
                return True
            except DatabaseError:
                self.connection.close()
                self.connection = None
        return False

    def _cursor(self):
        if not self._valid_connection():

            settings_dict = self.settings_dict

            # Connection to CUBRID database is made through connect() method.
            # Syntax:
            # connect (url[, user[password]])
            #    url - CUBRID:host:port:db_name:db_user:db_password:::
            #    user - Authorized username.
            #    password - Password associated with the username.
            url = "CUBRID"
            user = "public"
            passwd = ""

            if settings_dict['HOST'].startswith('/'):
                url += ':' + settings_dict['HOST']
            elif settings_dict['HOST']:
                url += ':' + settings_dict['HOST']
            else:
                url += ':localhost'
            if settings_dict['PORT']:
                url += ':' + settings_dict['PORT']
            if settings_dict['NAME']:
                url += ':' + settings_dict['NAME']
            if settings_dict['USER']:
                user = settings_dict['USER']
            if settings_dict['PASSWORD']:
                passwd = settings_dict['PASSWORD']

            url += ':::'

            self.connection = Database.connect(url, user, passwd, charset='utf8')
            connection_created.send(sender=self.__class__, connection=self)
        cursor = CursorWrapper(self.connection.cursor())
        return cursor

    def get_server_version(self):
        if not self.server_version:
            if not self._valid_connection():
                self.cursor()
            m = self.connection.server_version()
            if not m:
                raise Exception('Unable to determine CUBRID version')
            self.server_version = m
        return self.server_version
