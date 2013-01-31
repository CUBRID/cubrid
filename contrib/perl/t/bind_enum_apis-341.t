#!perl -w

use DBI ();
use Test::More;
use lib 't', '.';
require 'lib.pl';
use vars qw($table $test_dsn $test_user $test_passwd);

my $dbh;
eval {$dbh= DBI->connect($test_dsn, $test_user, $test_passwd,
                      { RaiseError => 1, PrintError => 1, AutoCommit => 1 });};
if ($@) {
    plan skip_all => "ERROR: $DBI::errstr. Can't continue test";
}

plan tests => 18;

ok ($dbh->do("DROP TABLE IF EXISTS $table"));

my $create = <<EOT;
CREATE TABLE $table (
        id int(4) NOT NULL default 0,
        name varchar(64) default '',
        answers enum('yes', 'no', 'cancel')
        )
EOT

ok ($dbh->do($create));

ok ($sth = $dbh->prepare("INSERT INTO $table VALUES (?, ?, ?)"));

ok ($sth->bind_param(1, "1", DBI::SQL_INTEGER));
ok ($sth->bind_param(2, 'Andreas Koenig', {TYPE => DBI::SQL_VARCHAR}));
ok ($sth->bind_param(3, 'no', DBI::SQL_VARCHAR));
ok ($sth->execute);

ok ($sth->bind_param(1, 2, DBI::SQL_INTEGER));
ok ($sth->bind_param(2, 'Jack'));
ok ($sth->bind_param(3, 1, DBI::SQL_INTEGER));
ok ($sth->execute);

ok ($sth->bind_param(1, 3));
ok ($sth->bind_param(2, 'Jerry'));
ok ($sth->bind_param(3, 'cancel'));
ok ($sth->execute);

ok !($sth->execute(4, "Joe", 1));

#ok ($dbh->do("DROP TABLE $table"));

ok $sth->finish;
ok $dbh->disconnect;
