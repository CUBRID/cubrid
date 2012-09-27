#include "cubrid.h"

MODULE = DBD::cubrid PACKAGE = DBD::cubrid

INCLUDE: cubrid.xsi

MODULE = DBD::cubrid	PACKAGE = DBD::cubrid::dr




MODULE = DBD::cubrid	PACKAGE = DBD::cubrid::db

void
_ping( dbh )
    SV *dbh
    CODE:
    ST(0) = sv_2mortal(newSViv(dbd_db_ping(dbh)));

void
do( dbh, statement, attr=Nullsv, ... )
    SV *dbh
    char *statement
    SV *attr
    PROTOTYPE: $$;$@
    CODE:
{
    int retval;

    if (statement[0] == '\0') {
        XST_mUNDEF (0);
        return;
    }

    if (items < 4) {
        imp_sth_t *imp_sth;
        SV * const sth = dbixst_bounce_method ("prepare", 3);
        if (!SvROK (sth))
            XSRETURN_UNDEF;
        imp_sth = (imp_sth_t*)(DBIh_COM (sth));
        retval = dbd_st_execute (sth, imp_sth);
    } else {
        imp_sth_t *imp_sth;
        SV * const sth = dbixst_bounce_method ("prepare", 3);
        if (!SvROK (sth))
            XSRETURN_UNDEF;
        imp_sth = (imp_sth_t*)(DBIh_COM (sth));
        if (!dbdxst_bind_params (sth, imp_sth, items-2, ax+2))
            XSRETURN_UNDEF;
        retval = dbd_st_execute (sth, imp_sth);
    }

    if (retval == 0)
        XST_mPV (0, "0E0");
    else if (retval < -1)
        XST_mUNDEF (0);
    else
        XST_mIV (0, retval);
}

void
_primary_key_info( dbh, table )
    SV *dbh
    char *table
    CODE:
    ST(0) = _cubrid_primary_key (dbh, table);

void
_foreign_key_info( dbh, pk_table = Nullsv, fk_table = Nullsv)
    SV *dbh
    SV *pk_table
    SV *fk_table
    CODE:
{
    STRLEN len;
    char *pktable = (SvOK(pk_table)) ? SvPV(pk_table, len) : "";
    char *fktable = (SvOK(fk_table)) ? SvPV(fk_table, len) : "";

    ST(0) = _cubrid_foreign_key (dbh, pktable, fktable);
}

void
quote(dbh, str, type=NULL)
    SV* dbh
    SV* str
    SV* type
    PROTOTYPE: $$;$
    CODE:
{
    D_imp_dbh(dbh);

    SV* quoted = dbd_db_quote(dbh, str, type);
    ST(0) = quoted ? sv_2mortal(quoted) : str;
    XSRETURN(1);
}

MODULE = DBD::cubrid    PACKAGE = DBD::cubrid::st

void
cubrid_lob_get( sth, col )
    SV *sth
    int col
    CODE:
    ST(0) =  sv_2mortal (newSViv (cubrid_st_lob_get(sth, col)));

void 
cubrid_lob_export( sth, index, filename )
    SV *sth
    int index 
    char *filename
    CODE:
    ST(0) = sv_2mortal (newSViv (cubrid_st_lob_export(sth, index, filename)));

void 
cubrid_lob_import( sth, index, filename, sql_type )
    SV *sth
    int index
    char *filename
    IV sql_type
    CODE:
    ST(0) = sv_2mortal (newSViv (cubrid_st_lob_import(sth, index, filename, sql_type)));

void 
cubrid_lob_close( sth )
    SV *sth
    CODE:
    ST(0) = sv_2mortal (newSViv (cubrid_st_lob_close(sth)));

