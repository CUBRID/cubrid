package com.cubrid.jsp.protocol;

import com.cubrid.jsp.SysParam;
import com.cubrid.jsp.data.CUBRIDUnpacker;

public class BootstrapRequest implements UnPackableObject {

    private SysParam[] sysParam;
    private String dbLocale;

    public BootstrapRequest(CUBRIDUnpacker unpacker) {
        unpack(unpacker);
    }

    @Override
    public void unpack(CUBRIDUnpacker unpacker) {
        // NOTE: the order here must follow BOOTSTRAP_REQ_ARGS () in pl_sr.cpp
        int size = (int) unpacker.unpackBigint();
        sysParam = new SysParam[size];
        for (int i = 0; i < size; i++) {
            sysParam[i] = new SysParam(unpacker);
        }
        dbLocale = unpacker.unpackCString();
    }

    public SysParam[] getSystemParameters() {
        return sysParam;
    }

    // locale of the database, e.g. "en_US" or "tr_TR"
    public String getDbLocale() {
        return dbLocale;
    }
}
