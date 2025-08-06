#!perl -w

use File::Spec;

use DBI ();
use DBI::Const::GetInfoType;
use Test::More;
use vars qw($table $test_dsn $test_user $test_passwd);
use lib '.', 't';
require 'lib.pl';

my $volume;
my $script_directory;
# get the full path of the hotcopy_script.
my $abs_path = File::Spec->rel2abs($0);
($volume, $script_directory, undef) = File::Spec->splitpath($abs_path);

my $dbh;

eval {$dbh = DBI->connect($test_dsn, $test_user, $test_passwd,
        { RaiseError => 1, AutoCommit => 1})};

if ($@) {
    plan skip_all => "ERROR: $DBI::errstr. Can't continue test";
}
else {
    plan tests => 20;
}

ok $dbh->do("DROP TABLE IF EXISTS $table"), "Drop table if exists $table";

my $create = <<EOT;
CREATE TABLE $table (
    id INT(3) NOT NULL DEFAULT 0,
    picture BLOB )
EOT

ok ($dbh->do($create));

my ($sth, $query);

# Insert a row into the test table .......
$query = "INSERT INTO $table VALUES(1, ?)";
ok ($sth = $dbh->prepare($query));
my $test_png_file = File::Spec->catfile($volume, $script_directory, "cubrid_logo.png");
ok ($sth->cubrid_lob_import(1, $test_png_file, DBI::SQL_BLOB), "import cubrid_logo.png");
ok ($sth->execute);

# Insert a NULL row into the test table ......
$query = "INSERT INTO $table VALUES(2, ?)";
ok ($sth = $dbh->prepare($query));
$sth->cubrid_lob_import(1, NULL, DBI::SQL_BLOB);
ok ($sth->execute);

ok ($sth->finish);

# Now, try SELECT'ing the first row out.
ok ($sth = $dbh->prepare("SELECT * FROM $table WHERE id = 1"), "prepare to select picture");
ok ($sth->execute, "executing...");

ok ($sth->cubrid_lob_get(2), 'get lob object');
ok ($sth->cubrid_lob_export(1, "out"), 'export lob object');

# Now try SELECT'ing the second row out: NULL
ok ($sth = $dbh->prepare("SELECT * FROM $table WHERE id = 2"), "prepare to select picture");
ok ($sth->execute, "executing 2...");

ok ($sth->cubrid_lob_get(2), 'get lob object 2');
ok !($sth->cubrid_lob_export(1, "out2"));


ok ($sth->cubrid_lob_close, 'close lob object');

ok ($sth->finish);

ok $dbh->do("DROP TABLE $table"), "Drop table $table";

ok $dbh->disconnect;
