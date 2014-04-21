#!perl -w
# vim: ft=perl
# Test problem in 3.0002_4 and 3.0005 where if a statement is prepared
# and multiple executes are performed, if any execute fails all subsequent
# executes report an error but may have worked.

use strict;
use DBI ();
use DBI::Const::GetInfoType;
use Test::More;
use lib '.', 't';
require 'lib.pl';

use vars qw($table $test_dsn $test_user $test_passwd);

my $dbh;
eval {$dbh = DBI->connect($test_dsn, $test_user, $test_passwd,
  { RaiseError => 1, AutoCommit => 1})};

if ($@) {
    plan skip_all => "ERROR: $@. Can't continue test";
}
plan tests => 9;

ok $dbh->do("DROP TABLE IF EXISTS $table");

my $create = <<EOT;
CREATE TABLE $table (
    id INT(3) PRIMARY KEY NOT NULL,
    name VARCHAR(64))
EOT

ok $dbh->do($create);

my $query = "INSERT INTO $table (id, name) VALUES (?,?)";
ok (my $sth = $dbh->prepare($query));

ok $sth->execute(1, "Jocken");

$sth->{PrintError} = 0;
eval {$sth->execute(1, "Jochen")};
ok defined($@), 'fails with duplicate entry';

$sth->{PrintError} = 1;
ok $sth->execute(2, "Jochen");

ok $sth->finish;

ok $dbh->do("DROP TABLE $table");

ok $dbh->disconnect();
