#!perl -w

use DBI;
use Test::More;
use lib '.', 't';
require 'lib.pl';

use vars qw($test_dsn $test_user $test_passwd);

my $create;

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_passwd,
                      { RaiseError => 1, PrintError => 1, AutoCommit => 0 });};

if ($@) {
    plan skip_all => "ERROR: $DBI::errstr. Can't continue test";
}
plan tests => 23;

ok $dbh->do("DROP TABLE IF EXISTS $table"), "drop table if exists $table";

$create = <<EOC;
CREATE TABLE $table (
    id INT(4) NOT NULL,
    name VARCHAR(64),
    key id (id)
    )
EOC

ok $dbh->do($create), "create table $table";

ok $dbh->table_info(undef,undef,$table), "table info for $table";

ok $dbh->column_info(undef,undef,$table,'%'), "column_info for $table";

$sth= $dbh->column_info(undef,undef,"this_does_not_exist",'%');

ok $sth, "\$sth defined";

ok !$sth->err(), "not error";

$sth = $dbh->prepare("SELECT * FROM $table");

ok $sth, "prepare succeeded";

ok $sth->execute, "execute select";

my $res;
$res = $sth->{'NUM_OF_FIELDS'};

ok $res, "$sth->{NUM_OF_FIELDS} defined";

is_deeply $sth->{'SCALE'}, [0, 0], "get scacle";
is_deeply $sth->{'PRECISION'}, [0, 64], "get precision";

is $res, 2, "\$res $res == 2";

$ref = $sth->{'NAME'};

ok $ref, "\$sth->{NAME} defined";

cmp_ok $$ref[0], 'eq', 'id', "$$ref[0] eq 'id'"; 

cmp_ok $$ref[1], 'eq', 'name', "$$ref[1] eq 'name'";

$ref = $sth->{'NULLABLE'};

ok $ref, "nullable";

ok !($$ref[0] xor (0 & $COL_NULLABLE));
ok !($$ref[1] xor (1 & $COL_NULLABLE));

$ref = $sth->{TYPE};

ok $ref->[0], "INTEGER type is $ref->[0]";

ok $ref->[1], "VARCHAR type is $ref->[1]";

ok ($sth= $dbh->prepare("DROP TABLE $table"));

ok($sth->execute);

ok (! defined $sth->{'NUM_OF_FIELDS'});
