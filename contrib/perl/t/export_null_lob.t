#!/usr/bin/perl

#using NULL value
use DBI;
use Test::More;

require 'lib.pl';
use vars qw($table $test_dsn $test_user $test_passwd);

$|= 1;

my $dbh;
eval {$dbh = DBI->connect($test_dsn, $test_user, $test_passwd, { RaiseError => 1});};

$dbh -> do("drop table if EXISTS tbl;") or die "drop error: " . $dbh->errstr;
$dbh -> do("create table tbl(id int, picture BLOB)") or die "create error: " . $dbh->errstr;
$dbh -> do("insert into tbl values(1, NULL);") or die "insert error: " . $dbh->errstr;

plan tests => 1;

$sth = $dbh->prepare("SELECT * FROM tbl where id = 1");
$sth->execute();
$sth->cubrid_lob_get(2);

$ret = $sth->cubrid_lob_export(1, "out.pic");
is($ret, 0, "Expected msg: [" . $dbh->errstr . "]");
$sth->cubrid_lob_close;

$sth->finish();
$dbh -> disconnect();
