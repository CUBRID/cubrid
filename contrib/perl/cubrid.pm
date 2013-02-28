# cubrid.pm
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
#
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# - Neither the name of the <ORGANIZATION> nor the names of its contributors
#   may be used to endorse or promote products derived from this software without
#   specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
# OF SUCH DAMAGE.
#

use 5.008;
use warnings;
use strict;

{   package DBD::cubrid;

    use DBI ();
    use DynaLoader ();
    use vars qw(@ISA $VERSION $err $errstr $sqlstate $drh $dbh);
    @ISA = qw(DynaLoader);

    require_version DBI 1.61;

    $VERSION = '9.1.0.0001';

    bootstrap DBD::cubrid $VERSION;

    $drh = undef;   # holds driver handle once initialized
    $err = 0;       # holds error code for DBI::err
    $errstr = '';   # holds error string for DBI::errstr
    $sqlstate = ''; # holds five character SQLSTATE code

    sub driver {
        return $drh if $drh;

        my($class, $attr) = @_;

        $class .= "::dr";

        $drh = DBI::_new_drh ($class, {
                'Name' => 'cubrid',
                'Version' => $VERSION,
                'Err'    => \$DBD::cubrid::err,
                'Errstr' => \$DBD::cubrid::errstr,
                'Attribution' => 'DBD::cubrid by Zhang Hui'
            });

        DBD::cubrid::st->install_method ('cubrid_lob_get');
        DBD::cubrid::st->install_method ('cubrid_lob_export');
        DBD::cubrid::st->install_method ('cubrid_lob_import');
        DBD::cubrid::st->install_method ('cubrid_lob_close');

        $drh
    }
}

{   package DBD::cubrid::dr; # ====== DRIVER ======
    use strict;
    
    sub connect {

        my ($drh, $dsn, $user, $passwd, $attrhash) = @_;
        my %connect_attr;

        if ($dsn =~ /=/) {
            my ($n,$v);
            $dsn =~ s/^\s+//;
            $dsn =~ s/\s+$//;
            $dsn =~ s/^DBI:cubrid://;
            $dsn =~ s/^dbi:cubrid://;
            my @dsn = map {
                ($n,$v) = split /\s*=\s*/, $_, -1;
                Carp::carp("DSN component '$_' is not in 'name=value' format")
                    unless defined $v && defined $n;
                (uc($n), $v)
            } split /\s*;\s*/, $dsn;
            my %dsn = @dsn;
            foreach (%dsn) {
                $connect_attr{$_} = $dsn{$_};
            }
        }
        else {
            Carp::carp("DSN $dsn is not in 'name=value' format");
        }

        $user = 'public' if not defined $user;

        my ($host, $port, $dbname);

        if ($connect_attr{HOST}) {
            $host = $connect_attr{HOST};
        } else {
            $host = 'localhost';
        }

        if ($connect_attr{PORT}) {
            $port = $connect_attr{PORT};
        } else {
            $port = 33000;
        }

        if ($connect_attr{DATABASE}) {
            $dbname = $connect_attr{DATABASE};
        } else {
            $dbname = '';
        }

        my $connect_dsn = "cci:cubrid:$host:$port:$dbname" . ':::';
        my $is_connect_attr = 0;

        if ($connect_attr{ALTHOSTS}) {
            $connect_dsn .= "?alhosts=$connect_attr{ALTHOSTS}";
            $is_connect_attr = 1;
        }

        if ($connect_attr{RCTIME}) {
            if ($is_connect_attr) {
                $connect_dsn .= "&rctime=$connect_attr{RCTIME}";
            } else {
                $connect_dsn .= "?rctime=$connect_attr{RCTIME}";
                $is_connect_attr = 1;
            }
        }

        if ($connect_attr{LOGIN_TIMEOUT}) {
            if ($is_connect_attr) {
                $connect_dsn .= "&login_timeout=$connect_attr{LOGIN_TIMEOUT}";
            } else {
                $connect_dsn .= "?login_timeout=$connect_attr{LOGIN_TIMEOUT}";
                $is_connect_attr = 1;
            }
        }

        if ($connect_attr{QUERY_TIMEOUT}) {
            if ($is_connect_attr) {
                $connect_dsn .= "&query_timeout=$connect_attr{QUERY_TIMEOUT}";
            } else {
                $connect_dsn .= "?query_timeout=$connect_attr{QUERY_TIMEOUT}";
                $is_connect_attr = 1;
            }
        }

        if ($connect_attr{DISCONNECT_ON_QUERY_TIMEOUT}) {
            if ($is_connect_attr) {
                $connect_dsn .= "&disconnect_on_query_timeout=$connect_attr{DISCONNECT_ON_QUERY_TIMEOUT}";
            } else {
                $connect_dsn .= "?disconnect_on_query_timeout=$connect_attr{DISCONNECT_ON_QUERY_TIMEOUT}";
                $is_connect_attr = 1;
            }
        }

        my ($dbh) = DBI::_new_dbh ($drh, {
                'Name' => $dbname,
                'User' => $user,
            });

        DBD::cubrid::db::_login($dbh, $connect_dsn, $user, $passwd, $attrhash) or return undef;

        $dbh
    }

} # end the package of DBD::cubrid::dr


{   package DBD::cubrid::db; # ====== DATABASE ======
    use strict;
    use DBI qw(:sql_types);

    sub prepare {

        my ($dbh, $statement, @attribs) = @_;

        return undef if ! defined $statement;

        my $sth = DBI::_new_sth ($dbh, {
                'Statement' => $statement,
            });

        DBD::cubrid::st::_prepare($sth, $statement, @attribs) or return undef;

        $sth
    }

    sub ping {
        my $dbh = shift;
        local $SIG{__WARN__} = sub { } if $dbh->FETCH('PrintError');
        my $ret = DBD::cubrid::db::_ping($dbh);
        return $ret ? 1 : 0;
    }

    sub get_info {
        my ($dbh, $info_type) = @_;
        require DBD::cubrid::GetInfo;
        my $v = $DBD::cubrid::GetInfo::info{int($info_type)};
        $v = $v->($dbh) if ref $v eq 'CODE';
        return $v;
    }

    sub table_info {
        my ($dbh, $catalog, $schema, $table, $type, $attr) = @_;

        my @names = qw(TABLE_CAT TABLE_SCHEM TABLE_NAME TABLE_TYPE REMARKS);
        my @rows;

        my $sponge = DBI->connect("DBI:Sponge:", '','')
            or return $dbh->DBI::set_err($DBI::err, "DBI::Sponge: $DBI::errstr");

        if ((defined $catalog && $catalog eq "%") &&
             (!defined($schema) || $schema eq "") &&
             (!defined($table) || $table eq ""))
        {
            @rows = (); # Empty, because CUBRID doesn't support catalogs (yet)
        }
        elsif ((defined $schema && $schema eq "%") &&
                (!defined($catalog) || $catalog eq "") &&
                (!defined($table) || $table eq ""))
        {
            @rows = (); # Empty, because CUBRID doesn't support schemas (yet)
        }
        elsif ((defined $type && $type eq "%") &&
                (!defined($catalog) || $catalog eq "") &&
                (!defined($schema) || $schema eq "") &&
                (!defined($table) || $table eq ""))
        {
            @rows = (
                [ undef, undef, undef, "TABLE", undef ],
                [ undef, undef, undef, "VIEW",  undef ],
            );
        }
        else
        {
	    $table = '%' unless defined $table;
			
            my ($want_tables, $want_views);
            if (defined $type && $type ne "") {
                if (($type =~ m/^table$/i) || ($type =~ m/^view$/i) || ($type =~ /^'table','view'$/i)) {
                    $want_tables = ($type =~ m/table/i);
                    $want_views  = ($type =~ m/view/i);
                }
                else {
                    Carp::carp ("\$type must be TABLE, VIEW or 'TABELE','VIEW'");
                }
            }
            else {
                $want_tables = $want_views = 1;
            }

            my $sql = "SELECT class_name, class_type FROM db_class where class_name like " . $dbh->quote($table);
            my $sth = $dbh->prepare ($sql) or return undef;
            $sth->execute or return DBI::set_err($dbh, $sth->err(), $sth->errstr());

            while (my $ref = $sth->fetchrow_arrayref()) {
                my $type = (defined $ref->[1] &&
                    $ref->[1] =~ /VCLASS/i) ? 'VIEW' : 'TABLE';
                next if $type eq 'TABLE' && not $want_tables;
                next if $type eq 'VIEW'  && not $want_views;
                push @rows, [ undef, undef, $ref->[0], $type, undef ];
            }
        }

        my $sth = $sponge->prepare("table_info",
            {
                rows          => \@rows,
                NUM_OF_FIELDS => scalar @names,
                NAME          => \@names,
            }
        ) or return $dbh->DBI::set_err($sponge->err(), $sponge->errstr());

        return $sth;
    }

    sub column_info {
        my $dbh = shift;
        my ($catalog, $schema, $table, $column) = @_;

        # ODBC allows a NULL to mean all columns, so we'll accept undef
        $column = '%' unless defined $column;

        my $table_id = $dbh->quote_identifier($table);

        my @names = qw(
            TABLE_CAT TABLE_SCHEM TABLE_NAME COLUMN_NAME DATA_TYPE TYPE_NAME COLUMN_SIZE
            BUFFER_LENGTH DECIMAL_DIGITS NUM_PREC_RADIX NULLABLE REMARKS COLUMN_DEF
            SQL_DATA_TYPE SQL_DATETIME_SUB CHAR_OCTET_LENGTH ORDINAL_POSITION IS_NULLABLE
            CHAR_SET_CAT CHAR_SET_SCHEM CHAR_SET_NAME COLLATION_CAT COLLATION_SCHEM
            COLLATION_NAME UDT_CAT UDT_SCHEM UDT_NAME DOMAIN_CAT DOMAIN_SCHEM DOMAIN_NAME
            SCOPE_CAT SCOPE_SCHEM SCOPE_NAME MAX_CARDINALITY DTD_IDENTIFIER IS_SELF_REF
        );

        my @col_info;

        local $dbh->{FetchHashKeyName} = 'NAME_lc';
        my $desc_sth = $dbh->prepare("SHOW COLUMNS FROM $table_id LIKE " . $dbh->quote($column));
        my $desc = $dbh->selectall_arrayref($desc_sth, { Columns=>{} });

        my $ordinal_pos = 0;
        for my $row (@$desc) {

            my $type = $row->{type};
            if (!defined($type)) {
                $type = "NULL";
            }

            my $info = {
                TABLE_CAT               => $catalog,
                TABLE_SCHEM             => $schema,
                TABLE_NAME              => $table,
                COLUMN_NAME             => $row->{field},
                NULLABLE                => ($row->{null} eq 'YES') ? 1 : 0,
                IS_NULLABLE             => ($row->{null} eq 'YES') ? "YES" : "NO",
                TYPE_NAME               => uc($type),
                COLUMN_DEF              => $row->{default},
                ORDINAL_POSITION        => ++$ordinal_pos,
            };

            if ($type =~ /CHAR/) {
                $info->{DATA_TYPE} = SQL_VARCHAR;
                $info->{DATA_TYPE} = SQL_CHAR if $type =~ /^CHAR/;

                if ($type =~ /\(/) {
                    my @tmp = split /\(/, $type;
                    my @tmp1 = split /\)/, $tmp[1];
                    $info->{COLUMN_SIZE} = $tmp1[0];
                }
                else {
                    $info->{COLUMN_SIZE} = 1073741823;
                }
            }
            elsif ($type =~ /STRING/) {
                $info->{DATA_TYPE} = SQL_VARCHAR;

                if ($type =~ /\(/) {
                    my @tmp = split /\(/, $type;
                    my @tmp1 = split /\)/, $tmp[1];
                    $info->{COLUMN_SIZE} = $tmp1[0];
                }
                else {
                    $info->{COLUMN_SIZE} = 1073741823;
                }
            }
            elsif ($type =~ /INT/) {
                $info->{NUM_PREC_RADIX} = 10;
                if ($type =~ /BIGINT/) {
                    $info->{DATA_TYPE} = SQL_BIGINT;
                    $info->{COLUMN_SIZE} = 19;
                }
                elsif ($type =~ /SMALLINT/) {
                    $info->{DATA_TYPE} = SQL_SMALLINT;
                    $info->{COLUMN_SIZE} = 5;
                }
                else {
                    $info->{DATA_TYPE} = SQL_INTEGER;
                    $info->{COLUMN_SIZE} = 10;
                }
            }
            elsif ($type =~ /SHORT/) {
                $info->{DATA_TYPE} = SQL_SMALLINT;
                $info->{COLUMN_SIZE} = 5;
                $info->{NUM_PREC_RADIX} = 10;
            }
            elsif ($type =~ /NUMERIC/) {
                $info->{DATA_TYPE} = SQL_NUMERIC;
                $info->{COLUMN_SIZE} = 38;
                if ($type =~ /\(/) {
                    my @tmp = split /\(/, $type;
                    my @tmp1 = split /\)/, $tmp[1];
                    my @tmp2 = split /,/, $tmp1[0];
                    $info->{DECIMAL_DIGITS} = $tmp2[1];
                    $info->{NUM_PREC_RADIX} = $tmp2[0];
                }
                else {
                    $info->{DECIMAL_DIGITS} = 15;
                    $info->{NUM_PREC_RADIX} = 10;
                }
            }
            elsif ($type =~ /MONETARY/) {
                $info->{DATA_TYPE} = SQL_FLOAT;
                $info->{COLUMN_SIZE} = 14;
                $info->{DECIMAL_DIGITS} = 2;
            }
            elsif ($type =~ /BLOB/) {
                $info->{DATA_TYPE} = SQL_BLOB;
                $info->{COLUMN_SIZE} = 0;
            }
            elsif ($type =~ /CLOB/) {
                $info->{DATA_TYPE} = SQL_CLOB;
                $info->{COLUMN_SIZE} = 0;
            }
            elsif ($type =~ /FLOAT/) {
                $info->{DATA_TYPE} = SQL_FLOAT;
                $info->{COLUMN_SIZE} = 14;
                $info->{NUM_PREC_RADIX} = 10;
            }
            elsif ($type=~ /DOUBLE/) {
                $info->{DATA_TYPE} = SQL_DOUBLE;
                $info->{COLUMN_SIZE} = 28;
                $info->{NUM_PREC_RADIX} = 10;
            }
            elsif ($type =~ /TIME/) {
                if ($type =~ /TIMESTAMP|DATETIME/) {
                    $info->{DATA_TYPE} = SQL_TYPE_TIMESTAMP;
                    $info->{COLUMN_SIZE} = 19 if $type =~ /^TIMESTAMP/;
                    $info->{COLUMN_SIZE} = 23 if $type =~ /^DATETIME/;
                }
                else {
                    $info->{DATA_TYPE} = SQL_TYPE_TIME;
                    $info->{COLUMN_SIZE} = 8;
                }
            }
            elsif ($type =~ /DATE/) {
                if ($type =~ /DATETIME/) {
                    $info->{DATA_TYPE} = SQL_TYPE_TIMESTAMP;
                    $info->{COLUMN_SIZE} = 23;
                }
                else {
                    $info->{DATA_TYPE} = SQL_TYPE_DATE;
                    $info->{COLUMN_SIZE} = 10;
                }
            }
            elsif ($type =~ /BIT/) {
                if ($type =~ /VARYING/) {
                    $info->{DATA_TYPE} = SQL_VARBINARY;
                }
                else {
                    $info->{DATA_TYPE} = SQL_BINARY;
                }

                if ($type =~ /\(/) {
                    my @tmp = split /\(/, $type;
                    my @tmp1 = split /\)/, $tmp[1];
                    $info->{COLUMN_SIZE} = $tmp1[0];
                }
                else {
                    $info->{COLUMN_SIZE} = 1073741823;
                }
            }
            elsif ($type =~ /^NULL$/) {
                $info->{DATA_TYPE} = SQL_UNKNOWN_TYPE;
            }
            else {
                $info->{DATA_TYPE} = SQL_VARCHAR;
            }

            $info->{SQL_DATA_TYPE} ||= $info->{DATA_TYPE};

            push @col_info, [
                $info->{TABLE_CAT},
                $info->{TABLE_SCHEM},
                $info->{TABLE_NAME},
                $info->{COLUMN_NAME},
                $info->{DATA_TYPE},
                $info->{TYPE_NAME},
                $info->{COLUMN_SIZE},
                $info->{BUFFER_LENGTH},
                $info->{DECIMAL_DIGITS},
                $info->{NUM_PREC_RADIX},
                $info->{NULLABLE},
                $info->{REMARKS},
                $info->{COLUMN_DEF},
                $info->{SQL_DATA_TYPE},
                $info->{SQL_DATETIME_SUB},
                $info->{CHAR_OCTET_LENGTH},
                $info->{ORDINAL_POSITION},
                $info->{IS_NULLABLE},
                $info->{CHAR_SET_CAT},
                $info->{CHAR_SET_SCHEM},
                $info->{CHAR_SET_NAME},
                $info->{COLLATION_CAT},
                $info->{COLLATION_SCHEM},
                $info->{COLLATION_NAME},
                $info->{UDT_CAT},
                $info->{UDT_SCHEM},
                $info->{UDT_NAME},
                $info->{DOMAIN_CAT},
                $info->{DOMAIN_SCHEM},
                $info->{DOMAIN_NAME},
                $info->{SCOPE_CAT},
                $info->{SCOPE_SCHEM},
                $info->{SCOPE_NAME},
                $info->{MAX_CARDINALITY},
                $info->{DTD_IDENTIFIER},
                $info->{IS_SELF_REF}
            ];
        }

        my $sponge = DBI->connect("DBI:Sponge:", '','')
            or return $dbh->DBI::set_err($DBI::err, "DBI::Sponge: $DBI::errstr");
        
        my $sth = $sponge->prepare("column_info $table_id", {
                rows          => \@col_info,
                NUM_OF_FIELDS => scalar @names,
                NAME          => \@names,
            }) or return $dbh->DBI::set_err($sponge->err(), $sponge->errstr());

        return $sth;
    }

    sub primary_key_info {
        my ($dbh, $catalog, $schema, $table) = @_;    

        my @names = qw(
            TABLE_CAT TABLE_SCHEM TABLE_NAME COLUMN_NAME KEY_SEQ PK_NAME    
        );

        my @col_info;

        my $desc = DBD::cubrid::db::_primary_key_info ($dbh, $table);
        for my $row (@$desc) {
            push @col_info, [
                $catalog,
                $schema,
                $row->[0],
                $row->[1],
                $row->[2],
                $row->[3],
            ];
        }

        my $sponge = DBI->connect ("DBI:Sponge:", '','')
            or return $dbh->DBI::set_err ($DBI::err, "DBI::Sponge: $DBI::errstr");
        my $sth= $sponge->prepare ("primary_key_info $table", {
                rows          => \@col_info,
                NUM_OF_FIELDS => scalar @names,
                NAME          => \@names,
            }) or return $dbh->DBI::set_err($sponge->err(), $sponge->errstr());

        return $sth;
    }

    sub foreign_key_info {
        my ($dbh,
            $pk_catalog, $pk_schema, $pk_table,
            $fk_catalog, $fk_schema, $fk_table,
        ) = @_;

        my @names = qw(
            PKTABLE_CAT PKTABLE_SCHEM PKTABLE_NAME PKCOLUMN_NAME
            FKTABLE_CAT FKTABLE_SCHEM FKTABLE_NAME FKCOLUMN_NAME 
            KEY_SEQ  UPDATE_RULE DELETE_RULE FK_NAME PK_NAME DEFERRABILITY
        );

        my @col_info;

        my $desc = DBD::cubrid::db::_foreign_key_info ($dbh, $pk_table, $fk_table);
        for my $row (@$desc) {
            push @col_info, [
                $pk_catalog,
                $pk_schema,
                $row->[0],
                $row->[1],
                $fk_catalog,
                $fk_schema,
                $row->[2],
                $row->[3],
                $row->[4],
                $row->[5],
                $row->[6],
                $row->[7],
                $row->[8],
                undef
            ];
        }

        my $sponge = DBI->connect ("DBI:Sponge:", '','')
            or return $dbh->DBI::set_err ($DBI::err, "DBI::Sponge: $DBI::errstr");
        my $sth= $sponge->prepare ("foreign_key_info", {
                rows          => \@col_info,
                NUM_OF_FIELDS => scalar @names,
                NAME          => \@names,
            }) or return $dbh->DBI::set_err($sponge->err(), $sponge->errstr());

        return $sth;
    } 

    sub type_info_all {
        my ($dbh) = @_;

        my $type_info_all = [
            {
                TYPE_NAME           => 0,
                DATA_TYPE           => 1,
                COLUMN_SIZE         => 2,
                LITERAL_PREFIX      => 3,
                LITERAL_SUFFIX      => 4,
                CREATE_PARAMS       => 5,
                NULLABLE            => 6,
                CASE_SENSITIVE      => 7,
                SEARCHABLE          => 8,
                UNSIGNED_ATTRIBUTE  => 9,
                FIXED_PREC_SCALE    => 10,
                AUTO_UNIQUE_VALUE   => 11,
                LOCAL_TYPE_NAME     => 12,
                MINIMUM_SCALE       => 13,
                MAXIMUM_SCALE       => 14,
                SQL_DATA_TYPE       => 15,
                SQL_DATETIME_SUB    => 16,
                NUM_PREC_RADIX      => 17,
                INTERVAL_PRECISION  => 18,
            },
["CHAR", SQL_CHAR, 1073741823, q{'}, q{'}, "length", 
    1, 0, 3, -1, 0, 0, "CHAR", -1, -1, SQL_CHAR, -1, -1, -1],
["VARCHAR", SQL_VARCHAR, 1073741823, q{'}, q{'}, "length", 
    1, 0, 3, -1, 0, 0, "CHAR VARYING", -1, -1, SQL_VARCHAR, -1, -1, -1],
["BIT", SQL_BINARY, 1073741823 / 8, q{X'}, q{'}, "length",
    1, 0, 3, -1, 0, 0, "BIT", -1, -1, SQL_BINARY, -1, -1, -1],
["BIT VARYING", SQL_VARBINARY, 1073741823 / 8, q{X'}, q{'}, "length",
    1, 0, 3, -1, 0, 0, "BIT VARYING", -1, -1, SQL_VARBINARY, -1, -1, -1],
["NUMERIC", SQL_NUMERIC, 38, undef, undef, "precision, scale",
    1, 0, 2, 0, 0, 0, "NUMERIC", 0, 38, SQL_NUMERIC, -1, 10, -1],
["DECIMAL", SQL_DECIMAL, 38, undef, undef, "precision, scale",
    1, 0, 2, 0, 0, 0, "DECIMAL", 0, 38, SQL_DECIMAL, -1, 10, -1],
["INTEGER", SQL_INTEGER, 10, undef, undef, undef,
    1, 0, 2, 0, 0, 0, "INTEGER", -1, -1, SQL_INTEGER, -1, 10, -1],
["SMALLINT", SQL_SMALLINT, 5, undef, undef, undef,
    1, 0, 2, 0, 0, 0, "SMALLINT", -1, -1, SQL_SMALLINT, -1, 10, -1],
["REAL", SQL_REAL, 14, undef, undef, "precision",
    1, 0, 2, 0, 0, 0, "REAL", -1, -1, SQL_REAL, -1, 10, -1],
["FLOAT", SQL_FLOAT, 14, undef, undef, "precision",
    1, 0, 2, 0, 0, 0, "FLOAT", -1, -1, SQL_FLOAT, -1, 10, -1],
["DOUBLE", SQL_DOUBLE, 28, undef, undef, "precision",
    1, 0, 2, 0, 0, 0, "DOUBLE", -1, -1, SQL_DOUBLE, -1, 10, -1],
["DATE", SQL_TYPE_DATE, 10, q{DATE '}, q{'}, undef,
    1, 0, 2, 0, 0, 0, "DATE", -1, -1, SQL_DATETIME, 1, -1, -1],
["TIME", SQL_TYPE_TIME, 8, q{TIME '}, q{'}, undef,
    1, 0, 2, 0, 0, 0, "TIME", -1, -1, SQL_DATETIME, 2, -1, -1],
["TIMESTAMP", SQL_TYPE_TIMESTAMP, 19, q{TIMESTAMP '}, q{'}, undef,
    1, 0, 2, 0, 0, 0, "TIMESTAMP", -1, -1, SQL_DATETIME, 3, -1, -1],
["BIGINT", SQL_BIGINT, 19, undef, undef, undef, 
    1, 0, 2, 0, 0, 0, "BIGINT", -1, -1, SQL_INTEGER, -1, 10, -1],
["DATETIME", SQL_TYPE_TIMESTAMP, 23, q{DATETIME '}, q{'}, undef,
    1, 0, 2, 0, 0, 0, "DATETIME", -1, -1, SQL_DATETIME, 3, -1, -1],
["ENUM", SQL_VARCHAR, 0, undef, undef, undef,
    1, 0, 3, 0, 0, 0, "ENUM", -1, -1, SQL_VARCHAR, -1, -1, -1],
["BLOB", SQL_BLOB, 0, undef, undef, undef,
    1, 0, 3, 0, 0, 0, "BLOB", -1, -1, SQL_BLOB, -1, -1, -1],
["CLOB", SQL_CLOB, 0, undef, undef, undef,
    1, 0, 3, 0, 0, 0, "CLOB", -1, -1, SQL_CLOB, -1, -1, -1],
        ];

        return $type_info_all;
    }

}   # end of package DBD::cubrid::db


{   package DBD::cubrid::st; # ====== STATEMENT ======


}

1;

__END__

=head1 

DBD::cubrid - CUBRID driver for the Perl5 Database Interface (DBI)

=head1 SYNOPSIS

    use DBI;

    $dsn = "DBI:cubrid:database=$database;host=$hostname;port=$port";
    $dsn = "DBI:cubrid:database=$database;host=$hostname;port=$port;autocommit=$autocommit";

    $dbh = DBI->connect ($dsn, $user, $password);
    $sth = $dbh->prepare("SELECT * FROM TABLE");
    $sth->execute;
    $numFields = $sth->{'NUM_OF_FIELDS'};
    $sth->finish;
    $dbh->disconnect;

=head1 EXAMPLE

    #!/usr/bin/perl

    use strict;
    use DBI;

    # Connect to the database.
    my $dbh = DBI->connect (
        "DBI:cubrid:database=testdb;host=localhost;port=33000", "public", "", 
        {RaiseError => 1, AutoCommit => 0});

    # Drop table 'foo'. This may fail, if 'foo' doesn't exist.
    # Thus we put an eval around it.
    eval { $dbh->do("DROP TABLE foo") };
    print "Dropping foo failed: $@\n" if $@;

    # Create a new table 'foo'. This must not fail, thus we don't
    # catch errors.
    $dbh->do("CREATE TABLE foo (id INTEGER, name VARCHAR(20))");

    # INSERT some data into 'foo'. 
    $dbh->do("INSERT INTO foo VALUES (1, "Tim");

    # Same thing, but using placeholders
    $dbh->do("INSERT INTO foo VALUES (?, ?)", undef, 2, "Jochen");

    # Now retrieve data from the table.
    my $sth = $dbh->prepare("SELECT * FROM foo");
    $sth->execute();
    while (my $ref = $sth->fetchrow_hashref()) {
        print "Found a row: id = $ref->{'id'}, name = $ref->{'name'}\n";
    }
    $sth->finish();

    # Disconnect from the database.
    $dbh->disconnect();

=head1 DESCRIPTION

DBD::cubrid is a Perl module that works with the DBI module to provide access to
CUBRID databases.

=head1 Module Documentation

This documentation describes driver specific behavior and restrictions. It is
not supposed to be used as the only reference for the user. In any case
consult the B<DBI> documentation first!

=for html <a href="http://search.cpan.org/~timb/DBI/DBI.pm">Latest DBI documentation.</a>

=head1 THE DBI CLASS

=head2 DBI Class Methods

=head3 B<connect>

    use DBI;

    $dsn = "DBI:cubrid:database=$database";
    $dsn = "DBI:cubrid:database=$database;host=$hostname";
    $dsn = "DBI:cubrid:database=$database;host=$hostname;port=$port";
    $dsn = "DBI:cubrid:database=$database;host=$hostname;port=$port;autocommit=$autocommit"

    $dbh = DBI->connect ($dsn, $user, $password);


This method creates a database handle by connecting to a database, and is the DBI
equivalent of the "new" method. You can also use "dbi:cubrid..." in $dsn. There 
are some properties you can configure when create the conntion.

If the HA feature is enabled, you munst specify the connection information of the
standby server, which is used for failover when failure occurs, in the url string
argument of this function.

B<althosts>=standby_broker1_host,standby_broker2_host, . . . : String. Specifies the
broker information of the standby server, which is used for failover when it is 
impossible to connect to the active server. You can specify multiple brokers for failover,
and the connection to the brokers is attempted in the order listed in alhosts.

B<rctime> : INT. An interval between the attempts to connect to the active broker in
which failure occurred. After a failure occurs, the system connects to the broker
specified by althosts (failover), terminates the transaction, and then attempts to
connect to he active broker of the master database at every rctime. The default value
is 600 seconds.

B<autocommit> : String. Configures the auto-commit mode. The value maybe true, on, yes,
false, off and no. You can set autocommit property here or in the C<\%attr> parameter.
If you set this property at both places, the latter is effective.

B<login_timeout> : INT. Configures the login timeout of CUBRID.

B<query_timeout>: INT. Configures the query timeout of CUBRID.

B<disconnect_on_query_timeout> : String. Make the query_timeout effective. 
The value maybe true, on, yes, false, off and no.

The following are some examples about different $dsn:

    $db = "testdb";
    $host= "192.168.10.29";
    $port = 33088;
    $dsn = "dbi:cubrid:database=$db;host=$host;port=$port";

    # connection $dsn string when property(alhosts) specified for HA
    $alhosts = "192.168.10.28:31000,192.168.10.27:33055";
    $dsn = "dbi:cubrid:database=$db;host=$host;port=$port;alhosts=$alhosts";

    # connection $dsn string when property(alhosts, rctime) specified for HA
    $rctime = 1000;
    $dsn = "dbi:cubrid:database=$db;host=$host;port=$port;alhosts=$alhosts;rctime=$rctime";

    $query = 600;
    $dsn = "dbi:cubrid:database=$db;host=$host;port=$port;query_timeout=$query;disconnect_on_query_timeout=yes";

=head1 DBI Database Handle Object

=head2 Database Handle Methods

=head3 B<do>

    $rows = $dbh->do ($statement)           or die $dbh->errstr;
    $rows = $dbh->do ($statement, \%attr)   or die $dbh->errstr;
    $rows = $dbh->do ($statement, \%attr, @bind_values);

Prepare and execute a single statement. Returns the number of rows affected or undef 
on error. A return value of -1 means the number of rows is not known, not applicable,
or not available. For example:

    $dbh->do ("CREATE TABLE test_cubrid (id INT, name varchar(60))");
    $dbh->do ("INSERT INTO test_cubrid VALUES (1, 'Jobs')")
    $dbh->do ("INSERT INTO test_cubrid VALUES (?, ?)", undef, (2, "Gate"));

=head3 B<prepare>

    $sth = $dbh->prepare ($statement, \%attr);

Prepares a statement for later execution by the database engine and returns a reference
to a statement handle object. You can get C<$sth-E<gt>{NUM_OF_PARAMS}> after the statement
prepared.

=head3 B<commit>

    $dbh->commit or die $dbh->errstr;

Issues a COMMIT to the server, indicating that the current transaction is finished and
that all changes made will be visible to other processes. If AutoCommit is enabled,
then a warning is given and no COMMIT is issued. Returns true on success, false on error.

=head3 B<rollback>

    $dbh->rollback or dir $dbh->errstr;

Issues a ROLLBACK to the server, which discards any changes made in the current transaction.
If AutoCommit is enabled, then a warning is given and no ROLLBACK is issued. Returns true 
on success, and false on error.

=head3 B<disconnect>

    $dbh->disconnect  or warn $dbh->errstr;

Disconnects from the CUBRID database. If the C<AutoCommit> is on, any uncommitted changes
will be committed before disconnect is called. Otherwise, it will roll back.

=head3 B<selectall_arrayref>

    $ary_ref = $dbh->selectall_arrayref ($statement);
    $ary_ref = $dbh->selectall_arrayref ($statement, \%attr);
    $ary_ref = $dbh->selectall_arrayref ($statement, \%attr, @bind_values);

Returns a reference to an array containing the rows returned by preparing and executing 
the SQL string. See the DBI documentation for full details.

=head3 B<selectall_hashref>

    $hash_ref = $dbh->selectall_hashref ($statement, $key_field);

Returns a reference to a hash containing the rows returned by preparing and executing
the SQL string. See the DBI documentation for full details.

=head3 B<selectcol_arrayref>

    $ary_ref = $dbh->selectcol_arrayref ($statement, \%attr, @bind_values);

Returns a reference to an array containing the first column from each rows returned by
preparing and executing the SQL string. It is possible to specify exactly which columns 
to return. See the DBI documentation for full details.

=head3 B<selectrow_array>

    @row_ary = $dbh->selectrow_array ($statement);
    @row_ary = $dbh->selectrow_array ($statement, \%attr);
    @row_ary = $dbh->selectrow_array ($statement, \%attr, @bind_values);

Returns an array of row information after preparing and executing the provided SQL string.
The rows are returned by calling L</fetchrow_array>. The string can also be a statement handle 
generated by a previous prepare. Note that only the first row of data is returned. If called 
in a scalar context, only the first column of the first row is returned. Because this is not
portable, it is not recommended that you use this method in that way.

=head3 B<selectrow_arrayref>

    @row_ary = $dbh->selectrow_arrayref ($statement);
    @row_ary = $dbh->selectrow_arrayref ($statement, \%attr);
    @row_ary = $dbh->selectrow_arrayref ($statement, \%attr, @bind_values);

Exactly the same as L</selectrow_array>, except that it returns a reference to an array, by 
internal use of the L</fetchrow_arrayref> method.

=head3 B<selectrow_hashref>

    $hash_ref = $dbh->selectrow_hashref ($statement);
    $hash_ref = $dbh->selectrow_hashref ($statement, \%attr);
    $hash_ref = $dbh->selectrow_hashref ($statement, \%attr, @bind_values);

Exactly the same as L</selectrow_array>, except that it returns a reference to an hash, by 
internal use of the L</fetchrow_hashref> method.

=head3 B<last_insert_id>

    $insert_id = $dbh->last_insert_id ($catalog, $schema, $table, $field);
    $insert_id = $dbh->last_insert_id ($catalog, $schema, $table, $field, \%attr);

Attempts to retrieve the ID generated for the AUTO_INCREMENT column which is updated by the
previous INSERT query. This method only be available immediately after the insert statement
has executed for CUBRID. And CUBRID will also ignore the $catalog, $schema, $table and $field
parameters. CUBRID will return NULL if no last insert id. For example:

    $insert_id = $dbh->last_insert_id (undef, undef, undef, undef);

=head3 B<ping>

    $rv = $dbh->ping;

This method is used to check the validity of a database handle. The value returned is either 0,
indicating that the connection is no longer valid, or 1, indicating the connection is valid.

=head3 B<get_info>

    $value = $dbh->get_info ($info_type);

DBD::cubrid supports C<get_info()>, bug (currently) only a few info types.

=head3 B<table_info>

    $sth = $dbh->table_info ($catalog, $schema, $table, $type);

    #then $sth->fetchall_arrayref or $sth->fetchall_hashref etc

DBD::cubrid supports attributes for C<table_info()>.

In CUBRID, this method will return all tables and views visible to the current user. 
CUBRID doesn't support catalog and schema now. The table argument will do a LIKE search
if a percent sign (%) or an underscore (_) is detected in the argment. The type argument
accepts a value of either "TABLE" or "VIEW" (using both is the defualt action). Note
that a statement handle is returned, not a direct list of table. See the examples below
for ways to handle this.

The following fields are returned:

B<TABLE_CAT>: Always NULL, as CUBRID doesn't have the concept of catalogs.

B<TABLE_SCHEM>: Always NULL, as CUBRID doesn't have the concept of schemas.

B<TABLE_NAME>: Name of the table (or view, synonym, etc).

B<TABLE_TYPE>: The type of object returned. It will be "TABLE" or "VIEW".

B<REMARKS>: A description of the table. Always NULL (undef).

Examples of use:

    # Display all tables and views to the current user
    $sth = $dbh->table_info (undef, undef, undef, undef);
    for my $info (@{$sth->fetchall_arrayref({})}) {
        print "\$info->{TABLE_NAME} = $info->{TABLE_NAME} \t \$info->{TABLE_TYPE} = $info->{TABLE_TYPE}\n";
    }

    # Display the specified table
    $sth = $dbh->table (undef, undef, 'test_%', undef);
    for my $info (@{$sth->fetchall_arrayref({})}) {
        print "\$info->{TABLE_NAME} = $info->{TABLE_NAME} \t \$info->{TABLE_TYPE} = $info->{TABLE_TYPE}\n";
    }

=head3 B<tables>

    @names = $dbh->tables ($catalog, $schema, $table, $type);

Simple interface to L</table_info>. Returns a list of matching table names. 
See more information in DBI document;

=head3 B<primary_key_info>

    $sth = $dbh->primary_key_info ($catalog, $schema, $table);

    # then $sth->fetchall_arrayref or $sth->fetchall_hashref etc

CUBRID does not support catalogues and schemas so TABLE_CAT and TABLE_SCHEM are ignored 
as selection criterion. The TABLE_CAT and TABLE_SCHEM fields of a fetched row is always
NULL (undef). 

The result set is ordered by TABLE_NAME, COLUMN_NAME, KEY_SEQ, and PK_NAME.

=head3 B<primary_key>

    @key_column_names = $dbh->primary_key ($catalog, $schema, $table);

Simple interface to the L</primary_key_info> method. Returns a list of the column names
that comprise the primary key of the specified table. The list is in primary key column
sequence order. If there is no primary key then an empty list is returned.

=head3 B<foreign_key_info>

    $sth = $dbh->foreign_key_info ($pk_catalog, $pk_schema, $pk_table,
                                   $fk_catalog, $fk_schema, $fk_table);

    # then $sth->fetchall_arrayref or $sth->fetchall_hashref etc

CUBRID does not support catalogues and schemas so C<$pk_catalog>, C<$pk_schema>, C<$fk_catalog>
and C<$fk_schema> are ignored as selection criteria. The PKTABLE_CAT PKTABLE_SCHEM FKTABLE_CAT
and FKTABLE_SCHEM fields of a fetched row are are always NULL (undef).

The DEFERABILITY field is always NULL, CUBRID does not support it now.

The result set is ordered by PKTABLE_NAME, PKCOLUMN_NAME, FKTABLE_NAME, FKCOLUMN_NAME, KEY_SEQ,
UPDATE_RULE, DELETE_RULE, FK_NAME, PK_NAME.

=head3 B<column_info>

    $sth = $dbh->column_info ($catalog, $schema, $table, $column);

    # then $sth->fetchall_arrayref or $sth->fetchall_hashref etc

CUBRID does not support catalogues and schemas so TABLE_CAT and TABLE_SCHEM are ignored as
selection criterion. The TABLE_CAT and TABLE_SCHEM fields of a fetched row is always NULL(undef).

CUBRID only support the field of TABLE_NAME, COLUMN_NAME, NULLABLE, IS_NULLABLE, TYPE_NAME,
COLUMN_DEF, ORDINAL_POSITION, DATA_TYPE, COLUMN_SIZE, NUM_PREC_RADIX, DECIMAL_DIGITS and
SQL_DATA_TYPE now.

=head3 B<type_info_all>

    $type_info_all = $dbh->type_info_all;

Returns a reference to an array which holds information about each data type variant 
supported by the database and driver. The array and its contents are treated as read-only.

=head3 B<type_info>

    @type_info = $dbh->type_info ($data_type);

Returns a list of hash references holding information about one or more variants of
$data_type. See the DBI documentation for more details.

=head2 Private Database Handle Methods

In order to use the private database handle methods, you must make sure the name prefix 
'cubrid_' is associated with a registered driver. If not, you must manually add it.
To do this, you should install DBI by yourself. First, download the source code of DBI
and unpack it. Second, open DBI.pm with your favourite text editor, find the definition 
of $dbd_prefix_registry. Third, add the line

    cubrid_  => { class => 'DBD::cubrid',         },

save and exit. Then you can execute 

    perl Makefile.PL
    make
    make install

After doing these, you can use the private database handle methods.

=head3 B<cubrid_lob_get>

    $sth->cubrid_lob_get ($column);

This method can get a column of the lob object from CUBRID database. You need to point out
which column you want to fetch as lob object and the column start with 1.

=head3 B<cubrid_lob_export>

    $sth->cubrid_lob_export ($row, $filename);

This method will export a lob object as a file. You must specify the row you want to fetch
and the name of the file you want to export, and the row start with 1. Attention, before
use this function, you need to call B<cubrid_lob_get> first. For example

    $sth = $dbh->prepare ("SELECT * from test_lob");
    $sth->execute;

    $sth->cubrid_lob_get (2); # fetch the second column
    $sth->cubrid_lob_export (3, "1.jpg"); # export the third row as "1.jpg"
    $sth->cubrid_lob_close();

=head3 B<cubrid_lob_import>

    $sth->cubrid_lob_import ($index, $filename, $type);

This method will import a file in CUBRID database. The parameter $index is indicated to
which placeholder you want to bind the file in $filename, and $type can SQL_BLOB or SQL_CLOB.
For example

    $sth = $dbh->prepare ("INSERT INTO test_lob VALUES (?, ?)");
    $sth->bind_param (1, 1);
    $sth->cubrid_lob_import (2, "1.jpg", DBI::SQL_BLOB);
    $sth->execute;

=head3 B<cubrid_lob_close>

    $sth->cubrid_lob_close ();

This method will close the lob object that B<cubrid_lob_get> gets. Once you use B<cubrid_lob_get>,
you'd better use this method when you don't use the lob object any more.

=head2 Database Handle Attributes

=head3 B<AutoCommit> (boolean)

Supported by DBD::cubrid as proposed by DBI. In CUBRID 8.4.0, the default of AutoCommit is OFF.
And in CUBRID 8.4.1, the default of Autocommit is ON. It is highly recommended that you 
explicitly set it when calling L</connect>.

=head3 B<Name> (string, read-only)

Returns the name of the current database.

=head1 DBI STATEMENT HANDLE OBJECTS

=head2 Statement Handle Methods

=head3 B<bind_param>

    $sth->bind_param ($index, $bind_value);
    $sth->bind_param ($index, $bind_value, $bind_type);

Allows the user to bind a value and/or a data type to a placeholder. The value of C<$index>
is a number of using the '?' style placeholder. Generally, you can bind params without specifying
the data type. CUBRID will match it automatically. That means, you don't use C<$bind_type> for
most data types in CUBRID. But it won't work well with some special data types, such as BLOB and
CLOB. The following are data types supported by CUBRID.

    -----------------------------------------
    | CUBRID        | sql_types             |
    -----------------------------------------
    | CHAR          | SQL_CHAR              |
    | VARCHAR       | SQL_VARCHAR           |
    | NUMERIC       | SQL_NUMERIC           |
    | DECIMAL       | SQL_DECIMAL           |
    | INTEGER       | SQL_INTEGER           |
    | SMALLINT      | SQL_SMALLINT          |
    | REAL          | SQL_REAL              |
    | FLOAT         | SQL_FLOAT             |
    | DOUBLE        | SQL_DOUBLE            |
    | DATE          | SQL_TYPE_DATE         |
    | TIME          | SQL_TYPE_TIME         |
    | TIMESTAMP     | SQL_TYPE_TIMESTAMP    |
    | BIGINT        | SQL_BIGINT            |
    | DATETIME      | SQL_TYPE_TIMESTAMP    |
    | ENUM          | SQL_VARCHAR           |
    -----------------------------------------
    | BLOB          | SQL_BLOB              |
    | CLOB          | SQL_CLOB              |
    -----------------------------------------

Note that, DBD:cubrid does not support BIT, SET, MULTISET and SEQUENCE now. And if you want to
bind BLOB/CLOB data, you must specify C<$bind_type>.

Examples of use:

    # CREATE TABLE test_cubrid (id INT, name varchar(50), birthday DATE, salary FLOAT)
    $sth = $dbh->prepare ("INSERT INTO test_cubrid VALUES (?, ?, ?, ?)");
    $sth->bind_param (1, 1);
    $sth->bind_param (2, 'Jobs');
    $sth->bind_param (3, '1979-10-1');
    $sth->bind_param (4, 1000.5);
    $sth->execute;

    # CREATE TABLE test_cubrid (id INT, paper CLOB);
    $sth = $dbh->prepare ("INSERT INTO test_cubrid VALUES (?, ?)");
    $sth->bind_param (1, 10);
    $sth->bind_param (2, "HELLO WORLD", DBI::SQL_CLOB);
    $sth->execute;

=head3 B<execute>

    $rv = $sth->execute;
    $rv = $sth->execute (@bind_values);

Executes a previously prepared statement. In addition to UPDATE, DELETE, INSERT 
statements, for which it returns always the number of affected rows and to SELECT
statements, it returns the number of rows thart will be returned by the query.

=head3 B<fetchrow_arrayref>

    $ary_ref = $sth->fetchrow_arrayref;
    $ary_ref = $sth->fetch;

Fetches the next row of data from the statement handle, and returns a reference 
to an array holding the column values. Any columns that are NULL are returned 
as undef within the array.

If there are no more rows or if an error occurs, the this method return undef.
You should check C<$sth->err> afterwords (or use the C<RaiseError> attribute)
to discover if the undef was due to an error.

Note that the same array reference is returned for each fetch, so don't store 
the reference and then use it after a later fetch. Also, the elements of the array
are also reused for each row, so take care if you want to take a reference to 
an element. 

=head3 B<fetchrow_array>

    @ary = $sth->fetchrow_array;

Similar to the L</fetchrow_array> method, but returns a list of column information 
rather than a reference to a list. Do not use this in a scalar context.

=head3 B<fetchrow_hashref>

    $hash_ref = $sth->fetchrow_hashref;
    $hash_ref = $sth->fetchrow_hashref ($name);

Fetches the next row of data and returns a hashref containing the name of the columns
as the keys and the data itself as the values. Any NULL value is returned as as undef 
value.

If there are no more rows or if an error occurs, the this method return undef. You 
should check C<$sth->err> afterwords (or use the C<RaiseError> attribute) to discover
if the undef returned was due to an error.

The optional C<$name> argument should be either C<NAME>, C<NAME_lc> or C<NAME_uc>,
and indicates what sort of transformation to make to the keys in the hash.

=head3 B<fetchall_arrayref>

    $tbl_ary_ref = $sth->fetchall_arrayref ();
    $tbl_ary_ref = $sth->fetchall_arrayref ($slice);
    $tbl_ary_ref = $sth->fetchall_arrayref ($slice, $max_rows);

Returns a reference to an array of arrays that contains all the remaining rows to be 
fetched from the statement handle. If there are no more rows, an empty arrayref will
be returned. If an error occurs, the data read in so far will be returned. Because of
this, you should always check C<$sth->err> after calling this method, unless C<RaiseError>
has been enabled.

If C<$slice> is an array reference, fetchall_arrayref uses the L</fetchrow_arrayref>
method to fetch each row as an array ref. If the C<$slice> array is not empty then it 
is used as a slice to select individual columns by perl array index number (starting 
at 0, unlike column and parameter numbers which start at 1).

With no parameters, or if $slice is undefined, fetchall_arrayref acts as if passed an
empty array ref.

If C<$slice> is a hash reference, fetchall_arrayref uses L</fetchrow_hashref> to fetch
each row as a hash reference.

See the DBI documentation for a complete discussion.

=head3 B<fetchall_hashref>

    $hash_ref = $sth->fetchall_hashref ($key_field);

Returns a hashref containing all rows to be fetched from the statement handle. 
See the DBI documentation for a full discussion.

=head3 B<finish>

    $rv = $sth->finish;

Indicates to DBI that you are finished with the statement handle and are not 
going to use it again.

=head3 B<rows>

    $rv = $sth->rows;

Returns the number of rows returned by the last query. In contrast to many other
DBD modules, the number of rows is available immediately after calling C<$sth->execute>.
Note that the L</execute> method itself returns the number of rows itself, which 
means that this method is rarely needed.

=head2 Statement Handle Attributes

=head3 B<NUM_OF_FIELDS> (integer, read-only)

Returns the number of columns returned by the current statement. A number will
only be returned for SELECT statements, for SHOW statements (which always
return 1), and for INSERT, UPDATE, and DELETE statements which contain a RETURNING
clause. This method returns undef if called before C<execute()>.

=head3 B<NUM_OF_PARAMS> (integer, read-only)

Returns the number of placeholders in the current statement.

=head3 B<NAME>  (arrayref, read-only)

Returns an arrayref of column names for the current statement. This method will 
only work for SELECT statements, for SHOW statements, and for INSERT, UPDATE, 
and DELETE statements which contain a RETURNING clause. This method returns undef
if called before C<execute()>.

=head3 B<NAME_lc> (arrayref, read-only)

The same as the NAME attribute, except that all column names are forced to lower case.

=head3 B<NAME_uc> (arrayref, read-only)

The same as the NAME attribute, except that all column names are forced to upper case.

=head3 B<NAME_hash> (hashref, read-only)

Similar to the C<NAME> attribute, but returns a hashref of column names instead of 
an arrayref. The names of the columns are the keys of the hash, and the values represent
 the order in which the columns are returned, starting at 0. This method returns undef
 if called before C<execute()>.

=head3 B<NAME_lc_hash> (hashref, read-only)

The same as the NAME_hash attribute, except that all column names are forced to lower case.

=head3 B<NAME_uc_hash> (hashref, read-only)

The same as the NAME_hash attribute, except that all column names are forced to lower case.

=head3 B<TYPE> (arrayref, read-only)

Returns an arrayref indicating the data type for each column in the statement. 
This method returns undef if called before C<execute()>.

=head3 B<PRECISION> (arrayref, read-only)

Returns an arrayref of integer values for each column returned by the statement.
This method returns undef if called before C<execute()>.

=head3 B<SCALE> (arrayref, read-only)

Returns an arrayref of integer values for each column returned by the statement.
This method returns undef if called before C<execute()>.

=head3 B<NULLABLE> (arraryref, read-only)

Returns an arrayref of integer values for each column returned by the statement.
The number indicates if the column is nullable or not. 0 = not nullable, 1 = nullable.
This method returns undef if called before C<execute()>.

=head1 INSTALLATION

=head2 Environment Variables

we will have to install the following software componets in order to usd DBD::cubrid.

=over

=item *

CUBRID DBMS

Install the latest version of CUBRID Database System, and make sure the Environment Variable
%CUBRID% is defined in your system

=item *

Perl Interpreter

If you're new to Perl, you should start by running I<perldoc perlintro> , 
which is a general intro for beginners and provides some background to 
help you navigate the rest of Perl's extensive documentation. Run I<perldoc
perldoc> to learn more things you can do with perldoc.

=item *

DBI module

The DBI is a database access module for the Perl programming language.  It 
defines a set of methods, variables, and conventions that provide a consistent
database interface, independent of the actual database being used.

=back

=head2 Installing with CPAN

To fire up the CPAN module, just get to your command line and run this:

    perl -MCPAN -e shell

If this is the first time you've run CPAN, it's going to ask you a series of 
questions - in most cases the default answer is fine. If you finally receive 
the CPAN prompt, enter

    install DBD::cubrid

=head2 Installing with source code

To build and install from source, you should move into the top-level directory
of the dirstribution and issue the following commands.

    tar zxvf DBD-cubrid-(version)-tar.gz
    cd DBD-cubrid-(version)
    perl Makefile.PL
    make
    make test
    make install

=head1 BUGS

To report a bug, or view the current list of bugs, please visit
http://jira.cubrid.org/browse/APIS

=cut
