package com.cubrid.jsp.impl;

import java.io.UnsupportedEncodingException;

import com.cubrid.jsp.ExecuteThread;
import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.DBType;

public class SUBindParameter extends SUParameter {
    private static final byte PARAM_MODE_UNKNOWN = 0;
    private static final byte PARAM_MODE_IN = 1;
    private static final byte PARAM_MODE_OUT = 2;

    private boolean isBinded[];
    private byte paramMode[];

    SUBindParameter(int pNumber) {
        super(pNumber);

        isBinded = new boolean[pNumber];
        paramMode = new byte[pNumber];

        clear ();
    }
    
    void clear() {
        for (int i = 0; i < number; i++) {
            isBinded[i] = false;
            paramMode[i] = PARAM_MODE_UNKNOWN;
            values[i] = null;
            types[i] = DBType.DB_NULL;
            // types[i] = UUType.U_TYPE_NULL;
        }
    }

    boolean checkAllBinded() {
        for (int i = 0; i < number; i++) {
            if (isBinded[i] == false && paramMode[i] == PARAM_MODE_UNKNOWN) return false;
        }
        return true;
    }

    void close() {
        for (int i = 0; i < number; i++) {
            values[i] = null;
        }

        isBinded = null;
        paramMode = null;
        values = null;
        types = null;
    }

    public void setParameter(int index, int bType, Object bValue) {
        if (index < 0 || index >= number) {
            // TODO: throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
        }

        types[index] = bType;
        values[index] = bValue;

        isBinded[index] = true;
        paramMode[index] |= PARAM_MODE_IN;
    }

    public void setOutParam(int index, int sqlType) {
        if (index < 0 || index >= number) {
            // TODO: throw new UJciException(UErrorCode.ER_INVALID_ARGUMENT);
        }

        paramMode[index] |= PARAM_MODE_OUT;
    }

    synchronized void pack(CUBRIDPacker packer) throws UnsupportedEncodingException {
        int cnt = paramMode.length;
        packer.packInt(cnt);
        for (int i = 0; i < cnt; i++) {
            packer.packValue(values[i], types[i], ExecuteThread.charSet);
        }
    }
}
