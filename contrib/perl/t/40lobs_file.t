#!perl -w

use DBI ();
use DBI::Const::GetInfoType;
use Test::More;
use vars qw($table $test_dsn $test_user $test_passwd);
use lib '.', 't';
require 'lib.pl';

my $dbh;

eval {$dbh = DBI->connect($test_dsn, $test_user, $test_passwd,
        { RaiseError => 1, AutoCommit => 1})};

if ($@) {
    plan skip_all => "ERROR: $DBI::errstr. Can't continue test";
}
else {
    plan tests => 14;
}

ok $dbh->do("DROP TABLE IF EXISTS $table"), "Drop table if exists $table";

my $create = <<EOT;
CREATE TABLE $table (
    id INT(3) NOT NULL DEFAULT 0,
    picture BLOB )
EOT

ok ($dbh->do($create));

#   Insert a row into the test table.......
my ($sth, $query);
$query = "INSERT INTO $table VALUES(1, ?)";
ok ($sth = $dbh->prepare($query));
ok ($sth->cubrid_lob_import(1, "cubrid_logo.png", DBI::SQL_BLOB));
ok ($sth->execute);
ok ($sth->finish);

#   Now, try SELECT'ing the row out.
ok ($sth = $dbh->prepare("SELECT * FROM $table WHERE id = 1"), "prepare to select picture");
ok ($sth->execute, "executing...");

ok ($sth->cubrid_lob_get(2), 'get lob object');

ok ($sth->cubrid_lob_export(1, "out"), 'export lob object');
ok ($sth->cubrid_lob_close, 'close lob object');

ok ($sth->finish);

ok $dbh->do("DROP TABLE $table"), "Drop table $table";

ok $dbh->disconnect;
