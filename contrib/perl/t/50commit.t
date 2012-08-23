#!perl -w
#
#   $Id$ 
#
#   This is testing the transaction support.
#


use DBI;
use Test::More; 
use lib 't', '.';
require 'lib.pl';

use vars qw($test_dsn $test_user $test_passwd $table);

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_passwd,
                      { RaiseError => 1, PrintError => 1, AutoCommit => 0 });};
if ($@) {
    plan skip_all => 
        "ERROR: $DBI::errstr. Can't continue test";
}

sub num_rows($$$) {
    my($dbh, $table, $num) = @_;
    my($sth, $got);

    if (!($sth = $dbh->prepare("SELECT * FROM $table"))) {
      return "Failed to prepare: err " . $dbh->err . ", errstr "
        . $dbh->errstr;
    }
    if (!$sth->execute) {
      return "Failed to execute: err " . $dbh->err . ", errstr "
        . $dbh->errstr;
    }
    $got = 0;
    while ($sth->fetchrow_arrayref) {
      ++$got;
   }
    if ($got ne $num) {
      return "Wrong result: Expected $num rows, got $got.\n";
    }
    return '';
}

plan tests => 17; 

ok $dbh->do("DROP TABLE IF EXISTS $table"), "drop table if exists $table";
my $create =<<EOT;
CREATE TABLE $table (
    id INT(4) NOT NULL default 0,
    name VARCHAR(64) NOT NULL default ''
) 
EOT

ok $dbh->do($create), 'create $table';

ok !$dbh->{AutoCommit}, "\$dbh->{AutoCommit} not defined |$dbh->{AutoCommit}|";

$dbh->{AutoCommit} = 0;
ok !$dbh->err;
ok !$dbh->errstr;
ok !$dbh->{AutoCommit};

ok $dbh->commit, 'commit';

ok $dbh->do("INSERT INTO $table VALUES (1, 'Jochen')"), "insert into $table (1, 'Jochen')";

my $msg;
$msg = num_rows($dbh, $table, 1);
ok !$msg;

ok $dbh->rollback, 'rollback';

ok $dbh->disconnect;
ok ($dbh = DBI->connect($test_dsn, $test_user, $test_passwd));

$msg = num_rows($dbh, $table, 0);
ok !$msg;

# Check auto rollback after disconnect
ok $dbh->do("INSERT INTO $table VALUES (1, 'Jochen')");

$msg = num_rows($dbh, $table, 1);
ok !$msg;

ok $dbh->disconnect;

ok $dbh->{AutoCommit}, "\$dbh->{AutoCommit} $dbh->{AutoCommit}";
