#!perl -w
# vim: ft=perl

use Data::Dumper;
use Test::More;
use DBI;
use DBI::Const::GetInfoType;
use lib '.', 't';
require 'lib.pl';
use strict;
$|= 1;

use vars qw($table $test_dsn $test_user $test_passwd);

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_passwd,
                      { RaiseError            => 1,
                        PrintError            => 1,
                        AutoCommit            => 1 });};

if ($@) {
    plan skip_all => "ERROR: $DBI::errstr. Can't continue test";
}
plan tests => 6;

ok(defined $dbh, "connecting");

ok($dbh->do(qq{DROP TABLE IF EXISTS t1}), "cleaning up");

ok($dbh->do(qq{CREATE TABLE t1 (a INT PRIMARY KEY AUTO_INCREMENT,
                                b INT,
                                `a_` INT,
                                `a'b` INT,
                                bar INT
                                )}), "creating table");

my $sth= $dbh->column_info(undef, undef, "t1", "a%");
my ($info)= $sth->fetchall_arrayref({});
is(scalar @$info, 3);
$sth= $dbh->column_info(undef, undef, "t1", "a'b");
($info)= $sth->fetchall_arrayref({});
is(scalar @$info, 1);

ok($dbh->do(qq{DROP TABLE IF EXISTS t1}), "cleaning up");

$dbh->disconnect();
