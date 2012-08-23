#!perl -w
# vim: ft=perl

# this test is for bug 49719
# https://rt.cpan.org/Ticket/Display.html?id=49719

use strict;
use Test::More;
use DBI;
use DBI::Const::GetInfoType;
use Carp qw(croak);
use Data::Dumper;
use lib 't', '.';
require 'lib.pl';

my ($row, $sth, $dbh);
my ($table, $def, $rows, $errstr, $ret_ref);
use vars qw($table $test_dsn $test_user $test_passwd);

eval {$dbh = DBI->connect($test_dsn, $test_user, $test_passwd,
    { RaiseError => 1, AutoCommit => 1});};

if ($@) {
    plan skip_all => 
        "Can't connect to database ERROR: $DBI::errstr. Can't continue test";
}
plan tests => 22; 

ok(defined $dbh, "Connected to database");

ok($dbh->do("DROP TABLE IF EXISTS t1"), "Making slate clean");

my $create= <<EOSQL;
CREATE TABLE t1 (
    id int,
    value0 varchar(10),
    value1 varchar(10),
    value2 varchar(10))
EOSQL

ok($dbh->do($create), "creating test table for bug 49719");

my ($sth_insert, $sth_lookup);

my $insert= 'INSERT INTO t1 (id, value0, value1, value2) VALUES (?, ?, ?, ?)';

ok($sth_insert= $dbh->prepare($insert), "Prepare of insert");

my $select= "SELECT * FROM t1 WHERE id = ?";

ok($sth_lookup= $dbh->prepare($select), "Prepare of query");

# Insert null value
ok($sth_insert->bind_param(1, 42), "bind_param(1,42)");
ok($sth_insert->bind_param(2, 102), "bind_param(2,102");
ok($sth_insert->bind_param(3, ''), "bind_param(3, '')");
ok($sth_insert->bind_param(4, 10004), "bind_param(4, 10004)");
ok($sth_insert->execute(), "Executing the first insert");

# Insert afterwards none null value
# The bug would insert (DBD::MySQL-4.012) corrupted data....
# incorrect use of MYSQL_TYPE_NULL in prepared statement in dbdimp.c
ok($sth_insert->bind_param(1, 43),"bind_param(1,43)");
ok($sth_insert->bind_param(2, 2002),"bind_param(2,2002)");
ok($sth_insert->bind_param(3, 20003),"bind_param(3,20003)");
ok($sth_insert->bind_param(4, 200004),"bind_param(4,200004)");
ok($sth_insert->execute(), "Executing the 2nd insert");

# verify
ok($sth_lookup->execute(42), "Query for record of id = 42");
is_deeply($sth_lookup->fetchrow_arrayref(), [42, 102, '', 10004]);

ok($sth_lookup->execute(43), "Query for record of id = 43");
is_deeply($sth_lookup->fetchrow_arrayref(), [43, 2002, 20003, 200004]);

ok($sth_insert->finish());
ok($sth_lookup->finish());

ok($dbh->disconnect(), "Testing disconnect");
