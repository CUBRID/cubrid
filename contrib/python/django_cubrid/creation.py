import sys
import os
import time
import subprocess

from django.db.backends.creation import BaseDatabaseCreation
from django.core.management import call_command
from django.conf import settings

# The prefix to put on the default database name when creating
# the test database.
TEST_DATABASE_PREFIX = 'test_'

class DatabaseCreation(BaseDatabaseCreation):
    # This dictionary maps Field objects to their associated CUBRID column
    # types, as strings. Column-type strings can contain format strings; they'll
    # be interpolated against the values of Field.__dict__ before being output.
    # If a column type is set to None, it won't be included in the output.
    data_types = {
        'AutoField':         'integer AUTO_INCREMENT',
        'BooleanField':      'smallint',
        'CharField':         'varchar(%(max_length)s)',
        'CommaSeparatedIntegerField': 'varchar(%(max_length)s)',
        'DateField':         'date',
        'DateTimeField':     'datetime',
        'DecimalField':      'numeric(%(max_digits)s, %(decimal_places)s)',
        'FileField':         'varchar(%(max_length)s)',
        'FilePathField':     'varchar(%(max_length)s)',
        'FloatField':        'double precision',
        'IntegerField':      'integer',
        'BigIntegerField':   'bigint',
        'IPAddressField':    'char(15)',
        'NullBooleanField':  'smallint',
        'OneToOneField':     'integer',
        'PositiveIntegerField': 'integer',
        'PositiveSmallIntegerField': 'smallint',
        'SlugField':         'varchar(%(max_length)s)',
        'SmallIntegerField': 'smallint',
        'TextField':         'string',
        'TimeField':         'time',
    }

    def sql_for_inline_foreign_key_references(self, field, known_models, style):
            "Return the SQL snippet defining the foreign key reference for a field"
            qn = self.connection.ops.quote_name
            if field.rel.to in known_models:
                output = [style.SQL_KEYWORD('FOREIGN KEY') + ' ' + \
                    style.SQL_KEYWORD('REFERENCES') + ' ' + \
                    style.SQL_TABLE(qn(field.rel.to._meta.db_table)) + ' (' + \
                    style.SQL_FIELD(qn(field.rel.to._meta.get_field(field.rel.field_name).column)) + ')' +
                    self.connection.ops.deferrable_sql()
                ]
                pending = False
            else:
                # We haven't yet created the table to which this field
                # is related, so save it for later.
                output = []
                pending = True

            return output, pending

    def sql_indexes_for_model(self, model, style):
        """ 
        Returns the CREATE INDEX SQL statements for a single model.
        The reference coloum can't be indexed in Cubrid.
        """
        if not model._meta.managed or model._meta.proxy:
            return []
        output = []
        for f in model._meta.local_fields:
            if not f.rel:
                output.extend(self.sql_indexes_for_field(model, f, style))
        return output

    def _create_test_db(self, verbosity, autoclobber):
        "Internal implementation - creates the test db tables."
        suffix = self.sql_table_creation_suffix()

        if self.connection.settings_dict['TEST_NAME']:
            test_database_name = self.connection.settings_dict['TEST_NAME']
        else:
            test_database_name = TEST_DATABASE_PREFIX + self.connection.settings_dict['NAME']

        qn = self.connection.ops.quote_name

        # Create the test database and start the cubrid server.
        try:
            subprocess.call(["cubrid", 'createdb' , '--db-volume-size=20M', '--log-volume-size=20M', "%s" % test_database_name])
            print 'Created'
            subprocess.call(["cubrid", "server", "start", "%s" % test_database_name])
            print 'Started'

        except Exception, e:
            sys.stderr.write("Got an error creating the test database: %s\n" % e)
            if not autoclobber:
                confirm = raw_input("Type 'yes' if you would like to try deleting the test database '%s', or 'no' to cancel: " % test_database_name)
            if autoclobber or confirm == 'yes':
                try:
                    if verbosity >= 1:
                        print "Destroying old test database..."
                        subprocess.call(["cubrid", "server", "stop", "%s" % test_database_name])
                        subprocess.call(["cubrid", "deletedb", "%s" % test_database_name])

                        print "Creating test database..."
                        subprocess.call(["cubrid", 'createdb' , '--db-volume-size=20M', '--log-volume-size=20M', "%s" % test_database_name])
                        subprocess.call(["cubrid", "server", "start", "%s" % test_database_name])
                except Exception, e:
                    sys.stderr.write("Got an error recreating the test database: %s\n" % e)
                    sys.exit(2)
            else:
                print "Tests cancelled."
                sys.exit(1)

        return test_database_name

    def _rollback_works(self):
        cursor = self.connection.cursor()
        cursor.execute('CREATE TABLE ROLLBACK_TEST (X INT)')
        self.connection._commit()
        cursor.execute('INSERT INTO ROLLBACK_TEST (X) VALUES (8)')
        self.connection._rollback()
        cursor.execute('SELECT COUNT(X) FROM ROLLBACK_TEST')
        count, = cursor.fetchone()
        cursor.execute('DROP TABLE ROLLBACK_TEST')
        self.connection._commit()
        return count == 0

    def destroy_test_db(self, old_database_name, verbosity=1):
        """
        Destroy a test database, prompting the user for confirmation if the
        database already exists. Returns the name of the test database created.
        """
        if verbosity >= 1:
            print "Destroying test database '%s'..." % self.connection.alias
        self.connection.close()
        test_database_name = self.connection.settings_dict['NAME']
        self.connection.settings_dict['NAME'] = old_database_name

        self._destroy_test_db(test_database_name, verbosity)

    def _destroy_test_db(self, test_database_name, verbosity):
        "Internal implementation - remove the test db tables."
        # Remove the test database to clean up after
        # ourselves. Connect to the previous database (not the test database)
        # to do so, because it's not allowed to delete a database while being
        # connected to it.
        cursor = self.connection.cursor()
        self.set_autocommit()
        time.sleep(1) # To avoid "database is being accessed by other users" errors.
        p = subprocess.Popen(["cubrid", "server", "stop", "%s" % test_database_name])
        ret = os.waitpid(p.pid, 0)[1]
        p = subprocess.Popen(["cubrid", "deletedb", "%s" % test_database_name])
        ret = os.waitpid(p.pid, 0)[1]

        self.connection.close()
