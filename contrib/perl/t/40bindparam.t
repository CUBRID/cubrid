#!perl -w

use DBI ();
use Test::More;
use lib 't', '.';
require 'lib.pl';
use vars qw($table $test_dsn $test_user $test_passwd);

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_passwd,
                      { RaiseError => 1, PrintError => 1, AutoCommit => 0 });};
if ($@) {
    plan skip_all => "ERROR: $DBI::errstr. Can't continue test";
}

plan tests => 39;

ok ($dbh->do("DROP TABLE IF EXISTS $table"));

my $create = <<EOT;
CREATE TABLE $table (
        id int(4) NOT NULL default 0,
        name varchar(64) default ''
        )
EOT

ok ($dbh->do($create));

ok ($sth = $dbh->prepare("INSERT INTO $table VALUES (?, ?)"));

ok ($sth->execute(1, "Alligator Descartes"));
ok ($sth->execute("3", "Jochen Wiedmann"));

ok ($sth->execute(2, "Tim Bunce"));

ok ($sth->bind_param(1, " 4"));
ok ($sth->bind_param(2, 'Andreas Koenig'));
ok ($sth->execute);

ok ($sth->bind_param(1, 5));
ok ($sth->bind_param(2, ''));
ok ($sth->execute);

ok ($sth->bind_param(1, ''));
ok ($sth->bind_param(2, ''));

ok ($sth->execute(-1, "abc"));

ok ($dbh->do("INSERT INTO $table VALUES (6, '?')"));
ok ($dbh->do("INSERT INTO $table VALUES (7, '?')"));

ok ($sth = $dbh->prepare("SELECT * FROM $table ORDER BY id"));
ok($sth->execute);

ok ($sth->bind_columns(undef, \$id, \$name));

$ref = $sth->fetch; 

is $id, -1, 'id set to -1'; 

cmp_ok $name, 'eq', 'abc', 'name eq abc'; 

$ref = $sth->fetch;
is $id, 1, 'id set to 1';
cmp_ok $name, 'eq', 'Alligator Descartes', '$name set to Alligator Descartes';

$ref = $sth->fetch;
is $id, 2, 'id set to 2';
cmp_ok $name, 'eq', 'Tim Bunce', '$name set to Tim Bunce';

$ref = $sth->fetch;
is $id, 3, 'id set to 3';
cmp_ok $name, 'eq', 'Jochen Wiedmann', '$name set to Jochen Wiedmann';

$ref = $sth->fetch;
is $id, 4, 'id set to 4';
cmp_ok $name, 'eq', 'Andreas Koenig', '$name set to Andreas Koenig';

$ref = $sth->fetch;
is $id, 5, 'id set to 5';
cmp_ok $name, 'eq', '', 'name not defined';

$ref = $sth->fetch;
is $id, 6, 'id set to 6';
cmp_ok $name, 'eq', '?', "\$name set to '?'";

$ref = $sth->fetch;
is $id, 7, '$id set to 7';
cmp_ok $name, 'eq', '?', "\$name set to '?'";

ok ($dbh->do("DROP TABLE $table"));

ok $sth->finish;
ok $dbh->disconnect;
