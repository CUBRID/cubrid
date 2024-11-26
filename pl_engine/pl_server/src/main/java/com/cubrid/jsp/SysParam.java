package com.cubrid.jsp;

import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.protocol.UnPackableObject;
import com.cubrid.jsp.value.Value;

public class SysParam implements UnPackableObject {
    public static final int CHARSET = 0;
    public static final int COMPAT_NUMERIC_DIVISION_SCALE = 1;
    public static final int ORACLE_COMPAT_NUMBER_BEHAVIOR = 2;
    public static final int ORACLE_STYLE_EMPTY_STRING = 3;
    public static final int TIMEZONE = 4;
    public static final int INTL_DATE_LANG = 5;
    public static final int INTL_DATE_FORMAT = 6;
    public static final int INTL_COLLATION = 7;

    // codeset
    public static final int CODESET_ASCII = 0;
    public static final int CODESET_RAW_BITS = 1;
    public static final int CODESET_RAW_BYTES = 2;
    public static final int CODESET_ISO88591 = 3;
    public static final int CODESET_KSC5601_EUC = 4;
    public static final int CODESET_UTF8 = 5;

    private int paramNum;
    private int paramId;
    private int paramType;
    private Value paramValue;

    public SysParam(CUBRIDUnpacker unpacker) {
        unpack(unpacker);
    }

    public int getParamNum() {
        return paramNum;
    }

    public int getParamId() {
        return paramId;
    }

    public Value getParamValue() {
        return paramValue;
    }

    public int getParamType() {
        return paramType;
    }

    public String toString() {
        return "SystemParameter [paramNum="
                + paramNum
                + ", paramId="
                + paramId
                + ", paramType="
                + paramType
                + ", paramValue="
                + paramValue
                + "]";
    }

    @Override
    public void unpack(CUBRIDUnpacker unpacker) {
        try {
            this.paramNum = unpacker.unpackInt(); // paramNum
            this.paramId = unpacker.unpackInt(); // paramId
            this.paramType = unpacker.unpackInt(); // paramType
            this.paramValue = unpacker.unpackValue(paramType);
        } catch (TypeMismatchException e) {
        }
    }
}
