#!perl -w
# vim: ft=perl

use Test::More;
use DBI;
use DBI::Const::GetInfoType;
use lib 't', '.';
require 'lib.pl';
use strict;
$|= 1;

use vars qw($table $test_dsn $test_user $test_passwd);

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_passwd,
                      { RaiseError => 1, PrintError => 1, AutoCommit => 0 });};
if ($@) {
    plan skip_all => 
        "ERROR: $DBI::errstr. Can't continue test";
}
plan tests => 10;

ok(defined $dbh, "Connected to database");

ok($dbh->do("DROP TABLE IF EXISTS $table"), "making slate clean");

ok($dbh->do("CREATE TABLE $table (id INT(4), name VARCHAR(64))"), "creating table");

ok($dbh->do("INSERT INTO $table VALUES(1, 'Alligator Descartes')"), "loading data");

ok($dbh->do("DELETE FROM $table WHERE id = 1"), "deleting from table $table");

ok (my $sth= $dbh->prepare("SELECT * FROM $table WHERE id = 1")); 

ok($sth->execute());

ok(not $sth->fetchrow_arrayref());

ok($sth->finish());

ok($dbh->do("DROP TABLE $table"),"Dropping table");

$dbh->disconnect();
