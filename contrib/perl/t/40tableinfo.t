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
plan tests => 26;

ok(defined $dbh, "connecting");

my $sth = $dbh->table_info("%", undef, undef, undef);
is(scalar @{$sth->fetchall_arrayref()}, 0, "No catalogs expected");

$sth = $dbh->table_info(undef, "%", undef, undef);
is(scalar @{$sth->fetchall_arrayref()}, 0, "Some schemas expected");

$sth = $dbh->table_info(undef, undef, undef, "%");
ok(scalar @{$sth->fetchall_arrayref()} > 0, "Some table types expected");

ok($dbh->do(qq{DROP TABLE IF EXISTS t_dbd_cubrid_t1, t_dbd_cubrid_t11,
                                    t_dbd_cubrid_t2, t_dbd_cubridhh2,
                                    `t_dbd_cubrid_a'b`,
                                    `t_dbd_cubrid_a``b`}),
            "cleaning up");
ok($dbh->do(qq{CREATE TABLE t_dbd_cubrid_t1 (a INT)}) and
   $dbh->do(qq{CREATE TABLE t_dbd_cubrid_t11 (a INT)}) and
   $dbh->do(qq{CREATE TABLE t_dbd_cubrid_t2 (a INT)}) and
   $dbh->do(qq{CREATE TABLE t_dbd_cubridhh2 (a INT)}) and
   $dbh->do(qq{CREATE TABLE `t_dbd_cubrid_a'b` (a INT)}) and
   $dbh->do(qq{CREATE TABLE `t_dbd_cubrid_a``b` (a INT)}),
   "creating test tables");

# $base is our base table name, with the _ escaped to avoid extra matches
my $base = "t_dbd_cubrid_";

# Test fetching info on a single table
$sth = $dbh->table_info(undef, undef, $base . "t1", undef);
my $info = $sth->fetchall_arrayref({});

is($info->[0]->{TABLE_CAT}, undef);
is($info->[0]->{TABLE_NAME}, "t_dbd_cubrid_t1");
is($info->[0]->{TABLE_TYPE}, "TABLE");
is(scalar @$info, 1, "one row expected");

# Test fetching info on a wildcard
$sth = $dbh->table_info(undef, undef, $base . "t1%", undef);
$info = $sth->fetchall_arrayref({});

is($info->[0]->{TABLE_CAT}, undef);
is($info->[0]->{TABLE_NAME}, "t_dbd_cubrid_t1");
is($info->[0]->{TABLE_TYPE}, "TABLE");
is($info->[1]->{TABLE_CAT}, undef);
is($info->[1]->{TABLE_NAME}, "t_dbd_cubrid_t11");
is($info->[1]->{TABLE_TYPE}, "TABLE");
is(scalar @$info, 2, "two rows expected");

# Test fetching info on a single table with escaped wildcards
$sth = $dbh->table_info(undef, undef, $base . "t2", undef);
$info = $sth->fetchall_arrayref({});

is($info->[0]->{TABLE_CAT}, undef);
is($info->[0]->{TABLE_NAME}, "t_dbd_cubrid_t2");
is($info->[0]->{TABLE_TYPE}, "TABLE");
is(scalar @$info, 1, "only one table expected");

# Test fetching info on a single table with ` in name
$sth = $dbh->table_info(undef, undef, $base . "a`b", undef);
$info = $sth->fetchall_arrayref({});

is($info->[0]->{TABLE_CAT}, undef);
is($info->[0]->{TABLE_NAME}, "t_dbd_cubrid_a`b");
is($info->[0]->{TABLE_TYPE}, "TABLE");
is(scalar @$info, 1, "only one table expected");

# Clean up
ok($dbh->do(qq{DROP TABLE IF EXISTS t_dbd_cubrid_t1, t_dbd_cubrid_t11,
                                    t_dbd_cubrid_t2, t_dbd_cubridhh2,
                                    `t_dbd_cubrid_a'b`,
                                    `t_dbd_cubrid_a``b`}),
            "cleaning up");

$dbh->disconnect();
