#
#   lib.pl is the file where database specific things should live,
#   whereever possible. For example, you define certain constants
#   here and the like.
#
# All this code is subject to being GUTTED soon
#
use strict;
use vars qw($table $mdriver $dbdriver $test_dsn $test_user $test_passwd);
$table= 'test_cubrid';

$| = 1; # flush stdout asap to keep in sync with stderr

#
#   Driver names; EDIT THIS!
#

$mdriver = 'cubrid';
$dbdriver = $mdriver; # $dbdriver is usually just the same as $mdriver.
                      # The exception is DBD::pNET where we have to
                      # to separate between local driver (pNET) and
                      # the remote driver ($dbdriver)


#
#   DSN being used; do not edit this, edit "$dbdriver.dbtest" instead
#

my ($database, $hostname, $port, $autocommit);
$database = 'demodb';
$hostname = 'localhost';
$port = 33000;
$test_dsn = "DBI:cubrid:database=$database;host=$hostname;port=$port";
$test_user = 'public';
$test_passwd = '';

$::COL_NULLABLE = 1;
$::COL_KEY = 2;

sub byte_string {
    my $ret = join ("|", unpack ("C*", $_[0]));
    return $ret;
}

1;
