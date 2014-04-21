#!perl -w
# vim: ft=perl

use strict;
use Test::More;
use DBI;
use lib 't', '.';
require 'lib.pl';
use vars qw($table $test_dsn $test_user $test_passwd);

$|= 1;

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_passwd,
                      { RaiseError => 1, PrintError => 1, AutoCommit => 0 });};

if ($@) {
    plan skip_all => "ERROR: $@. Can't continue test";
}
plan tests => 21; 

ok(defined $dbh, "connecting");

ok($dbh->do(qq{DROP TABLE IF EXISTS t1}), "making slate clean");

#
# Bug #20559: Program crashes when using server-side prepare
#
ok($dbh->do(qq{CREATE TABLE t1 (id INT, num DOUBLE)}), "creating table");

my $sth;
ok($sth= $dbh->prepare(qq{INSERT INTO t1 VALUES (?,?),(?,?)}), "loading data");
ok($sth->execute(1, 3.0, 2, -4.5));

#ok ($sth= $dbh->prepare("SELECT num FROM t1 WHERE id = ? FOR UPDATE"));
ok ($sth= $dbh->prepare("SELECT num FROM t1 WHERE id = ?"));

ok ($sth->bind_param(1, 1), "binding parameter");

ok ($sth->execute(), "fetching data");

is_deeply($sth->fetchall_arrayref({}), [ { 'num' => '3' } ]);

ok ($sth->finish);

ok ($dbh->do(qq{DROP TABLE t1}), "cleaning up");

#
# Bug #42723: Binding server side integer parameters results in corrupt data
#
ok($dbh->do(qq{DROP TABLE IF EXISTS t2}), "making slate clean");

ok($dbh->do(q{CREATE TABLE `t2` (`i` int,`si` smallint,`ti` tinyint,`bi` bigint)}), "creating test table");

my $sth2;
ok($sth2 = $dbh->prepare('INSERT INTO t2 VALUES (?,?,?,?)'));

#bind test values
ok($sth2->bind_param(1, 101), "binding the first param");
ok($sth2->bind_param(2, 102), "binding the second param");
ok($sth2->bind_param(3, 103), "binding the third param");
ok($sth2->bind_param(4, 104), "binding the fourth param");

ok($sth2->execute(), "inserting data");

is_deeply($dbh->selectall_arrayref('SELECT * FROM t2'), [[101, 102, 103, 104]]);

ok ($dbh->do(qq{DROP TABLE t2}), "cleaning up");

$dbh->disconnect();
