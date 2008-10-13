package com.cubrid.jsp.value;

import java.sql.SQLException;

import com.cubrid.jsp.Server;
import com.cubrid.jsp.exception.TypeMismatchException;

import cubrid.sql.CUBRIDOID;

public class OidValue extends Value {

    private CUBRIDOID oid;
    
    public OidValue(CUBRIDOID oid) {
        super();
        this.oid = oid;
    }

    public OidValue(CUBRIDOID oid, int mode, int dbType) {
        super(mode);
        this.oid = oid;
        this.dbType = dbType;
    }

    public CUBRIDOID toOid() throws TypeMismatchException {
        return oid;
    }
    
    public CUBRIDOID[] toOidArray() throws TypeMismatchException {
        return new CUBRIDOID[] {oid};
    }

    public String toString() {
        try {
            return oid.getOidString();
        } catch (SQLException e) {
            Server.log(e);
        }
        return null;
    }
    
    public String[] toStringArray() throws TypeMismatchException {
        return new String[] {toString()};
    }
    
    public Object toObject() throws TypeMismatchException {
        return toOid();
    }

    public Object[] toObjectArray() throws TypeMismatchException {
        return new Object[] {toObject()};
    }
}
