#!perl -w
# vim: ft=perl
# Test problem in 3.0002_4 and 3.0005 where if a statement is prepared
# and multiple executes are performed, if any execute fails all subsequent
# executes report an error but may have worked.

use strict;
use DBI;
use Test::More;
use lib '.', 't';
require 'lib.pl';

use vars qw($test_dsn $test_user $test_passwd);

$test_dsn.= ";mysql_server_prepare=1";
my $dbh;
eval {$dbh = DBI->connect($test_dsn, $test_user, $test_passwd,
  { RaiseError => 1, AutoCommit => 1})};

if ($@) {
    plan skip_all => "ERROR: $@. Can't continue test";
}

plan tests => 3;

# execute invalid SQL to make sure we get an error
my $q = "select select select";	# invalid SQL
$dbh->{PrintError} = 0;
$dbh->{PrintWarn} = 0;
my $sth;
eval {$sth = $dbh->prepare($q);};
$dbh->{PrintError} = 1;
$dbh->{PrintWarn} = 1;
ok defined($DBI::errstr);
cmp_ok $DBI::errstr, 'ne', '';

print "errstr $DBI::errstr\n" if $DBI::errstr;
ok $dbh->disconnect();
