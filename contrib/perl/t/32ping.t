#!perl -w

use DBI;
use Test::More;
use lib 't', '.';
require 'lib.pl';

use vars qw($test_dsn $test_user $test_passwd $table);

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_passwd,
                      { RaiseError => 1, PrintError => 1, AutoCommit => 0 });};

if ($@) {
    plan skip_all => "ERROR: $DBI::errstr. Can't continue test";
}

$dbh->do("SELECT * FROM code WHERE s_name = ?", undef, 'X');

plan tests => 2;

ok $dbh->ping;

$dbh->do("SELECT * FROM unknown_table");

ok $dbh->disconnect;



