#!perl -w
#
# The base driver test of DBD::cubrid
#

use Test::More tests => 6;

use vars qw($mdriver);
use lib 't', '.';
require 'lib.pl';

BEGIN {
    use_ok('DBI') or BAIL_OUT "Unable to load DBI";
    use_ok('DBD::cubrid') or BAIL_OUT "Unable to load DBD::cubrid";
}

$switch = DBI->internal;
cmp_ok ref $switch, 'eq', 'DBI::dr', 'Internal set';

# This is a special case. install_driver should not normally be used.
$drh= DBI->install_driver($mdriver);

ok $drh, 'Install driver';

cmp_ok ref $drh, 'eq', 'DBI::dr', 'DBI::dr set';

ok $drh->{Version}, "Version $drh->{Version}"; 
