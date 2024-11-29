package com.cubrid.jsp.protocol;

import com.cubrid.jsp.SysParam;
import com.cubrid.jsp.data.CUBRIDUnpacker;

public class BootstrapRequest implements UnPackableObject {

    private SysParam[] sysParam;

    public BootstrapRequest(CUBRIDUnpacker unpacker) {
        unpack(unpacker);
    }

    @Override
    public void unpack(CUBRIDUnpacker unpacker) {
        int size = unpacker.unpackInt();
        sysParam = new SysParam[size];

        try {
            for (int i = 0; i < size; i++) {
                sysParam[i] = new SysParam(unpacker);
            }
        } catch (Exception e) {
            // do nothing
        }
    }

    public SysParam[] getSystemParameters() {
        return sysParam;
    }
}
