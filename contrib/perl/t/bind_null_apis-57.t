#!/usr/bin/perl

#using NULL value
use DBI;
use Test::More;

use vars qw($db $port $hostname); 

$db="demodb";
$port=33000;
$hostname="localhost";
my $user="public";
my $pass="";

my $dsn="dbi:cubrid:database=$db;host=$hostname;port=$port";
my $dbh;
$dbh=DBI->connect($dsn, $user, $pass,{RaiseError => 1}) or die "connect err: $dbh->errstr";

$dbh -> do("drop table if EXISTS tbl;") or die "drop error: $dbh->errstr";
$dbh -> do("create table tbl(id int, name char(20),age int);") or die "create error: $dbh->errstr";
$dbh -> do("insert into tbl values(1,'zhangsan',30);") or die "insert error:$dbh->errstr";

plan tests => 1;

my $sth=$dbh->prepare("insert into tbl (id,name,age) values (?,?,?)") or die "select error: $dbh->errstr";
$sth->execute(1,'Joe', undef) or die "insert error: $dbh->errstr";

$sth1= $dbh->prepare("SELECT * FROM tbl where name = 'Joe'");
$sth1->execute();
while ($row_ref = $sth1->fetchrow_arrayref) {
    #print "$row_ref->[0] $row_ref->[1] $row_ref->[2]\n";
    is($row_ref->[2], undef);
}

$sth->execute(1,'Tom', '') or die "insert error: $dbh->errstr";

$sth->finish();
$dbh -> disconnect();
