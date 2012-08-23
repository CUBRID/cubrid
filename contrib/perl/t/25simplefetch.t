#!perl -w
#
#   $Id: 30insertfetch.t 1228 2004-09-04 01:23:38Z capttofu $
#
#   This is a simple insert/fetch test.
#
use Test::More;
use DBI ();
use strict;
use lib 't', '.';
require 'lib.pl';

#
#   Make -w happy
#
use vars qw($table $test_dsn $test_user $test_passwd);

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_passwd,
                      { RaiseError => 1, PrintError => 1, AutoCommit => 0 });};

if ($@) {
    plan skip_all => "Can't connect to database ERROR: $DBI::errstr. Can't continue test";
}

plan tests => 11;

my $create= <<EOT; 
CREATE TABLE $table ( 
    id int(4) NOT NULL default 0,
    name varchar(64) NOT NULL default ''
    )
EOT

ok $dbh->do("DROP TABLE IF EXISTS $table"), "drop table if exists $table";

ok $dbh->do($create), "create table $table";

ok $dbh->do("INSERT INTO $table VALUES(1, 'Alligator Descartes')"), "Insert ";

ok $dbh->do("DELETE FROM $table WHERE id = 1"), "Delete"; 

my $sth;
eval {$sth= $dbh->prepare("SELECT * FROM $table WHERE id = 1")};

ok !$@, "Prepare of select";

ok defined($sth), "Prepare of select";

ok  $sth->execute , "Execute";

my ($row, $errstr);
$errstr= '';
$row = $sth->fetchrow_arrayref;
$errstr= $sth->errstr;
ok !defined($row), "Fetch should have failed";
ok !defined($errstr), "Fetch should have failed";

ok $dbh->do("DROP TABLE $table"), "Drop table $table";
ok $dbh->disconnect, "Disconnecting";
