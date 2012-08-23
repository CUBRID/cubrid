#!perl -w

use Test::More;
use DBI;
use DBI::Const::GetInfoType;
use strict;
$| = 1;

use vars qw($table $test_dsn $test_user $test_passwd);
use lib 't', '.';
require 'lib.pl';

my $dbh;
eval {
    $dbh = DBI->connect ($test_dsn, $test_user, $test_passwd,
        { RaiseError => 1, PrintError => 1, AutoCommit => 0 });
};

if ($@) {
    plan skip_all => "ERROR: $DBI::errstr Can't continue test";
}

plan tests => 5;

ok(defined $dbh, "Connected to database");

ok($dbh->do("DROP TABLE IF EXISTS $table"), "Making slate clean");

ok($dbh->do("CREATE TABLE $table (id INT(4), name VARCHAR(64))"), "Creating $table");

ok($dbh->do("DROP TABLE $table"), "Dropping created $table");

ok($dbh->disconnect (), "Disconnecting");
