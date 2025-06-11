package com.cubrid.jsp;

import com.cubrid.jsp.data.CUBRIDPacker;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.protocol.PackableObject;
import com.cubrid.jsp.protocol.UnPackableObject;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;

public class SysParam implements UnPackableObject, PackableObject {

    // see src/base/system_parameter.h
    public static final int ORACLE_STYLE_EMPTY_STRING = 95;
    public static final int COMPAT_NUMERIC_DIVISION_SCALE = 100;
    public static final int INTL_NUMBER_LANG = 193;
    public static final int INTL_DATE_LANG = 194;
    public static final int INTL_COLLATION = 206;
    public static final int TIMEZONE = 249;
    public static final int ORACLE_COMPAT_NUMBER_BEHAVIOR = 334;
    public static final int STORED_PROCEDURE_DUMP_ICODE = 354;

    // PL session specific parameters
    public static final int PL_SESSION_PARAM_START = 100000;
    public static final int DBMS_OUTPUT = PL_SESSION_PARAM_START;

    // paramType
    public static final int PRM_TYPE_INTEGER = 0;
    public static final int PRM_TYPE_FLOAT = 1;
    public static final int PRM_TYPE_BOOLEAN = 2;
    public static final int PRM_TYPE_KEYWORD = 3;
    public static final int PRM_TYPE_BIGINT = 4;
    public static final int PRM_TYPE_STRING = 5;
    public static final int PRM_TYPE_INTEGER_LIST = 6;

    // codeset
    public static final int CODESET_ASCII = 0;
    public static final int CODESET_RAW_BITS = 1;
    public static final int CODESET_RAW_BYTES = 2;
    public static final int CODESET_ISO88591 = 3;
    public static final int CODESET_KSC5601_EUC = 4;
    public static final int CODESET_UTF8 = 5;

    public static Charset CHARSET_EUCKR = null;

    public static String getCodesetString(int codeset) {
        switch (codeset) {
            case CODESET_ASCII:
                return StandardCharsets.US_ASCII.toString();
            case CODESET_RAW_BITS:
            case CODESET_RAW_BYTES:
                break;
            case CODESET_ISO88591:
                return StandardCharsets.ISO_8859_1.toString();
            case CODESET_KSC5601_EUC:
                if (CHARSET_EUCKR == null) {
                    CHARSET_EUCKR = Charset.forName("EUC-KR");
                }
                return CHARSET_EUCKR.toString();
            default:
                break;
        }

        return "UTF-8"; // default
    }

    public static int getCodesetId(Charset charset) {
        if (charset.equals(StandardCharsets.US_ASCII)) {
            return CODESET_ASCII;
        } else if (charset.equals(StandardCharsets.ISO_8859_1)) {
            return CODESET_ISO88591;
        } else if (charset.equals(CHARSET_EUCKR)) {
            return CODESET_KSC5601_EUC;
        } else {
            return CODESET_UTF8;
        }
    }

    private int paramId;
    private int paramType;
    private String paramValue;

    public SysParam(int id, int type, Object value) {
        this.paramId = id;
        this.paramType = type;

        switch (this.paramType) {
            case PRM_TYPE_INTEGER:
                setParamValueInteger((Integer) value);
                break;
            case PRM_TYPE_BOOLEAN:
                setParamValueBoolean((Boolean) value);
                break;
            case PRM_TYPE_FLOAT:
                setParamValueFloat((Float) value);
                break;
            case PRM_TYPE_STRING:
                setParamValueString((String) value);
                break;
        }
    }

    public SysParam(CUBRIDUnpacker unpacker) {
        unpack(unpacker);
    }

    public int getParamId() {
        return paramId;
    }

    public String getParamValue() {
        return paramValue;
    }

    public int getParamType() {
        return paramType;
    }

    public String toString() {
        return "SystemParameter [paramId="
                + paramId
                + ", paramType="
                + paramType
                + ", paramValue="
                + paramValue
                + "]";
    }

    // setters
    public void setParamValueBoolean(boolean val) {
        this.paramValue = val ? "t" : "f";
    }

    public void setParamValueInteger(int val) {
        this.paramValue = Integer.toString(val);
    }

    public void setParamValueString(String val) {
        this.paramValue = val;
    }

    public void setParamValueFloat(float val) {
        this.paramValue = Float.toString(val);
    }

    // getters
    public boolean getParamValueBoolean() {
        return this.paramValue.equals("t");
    }

    public int getParamValueInteger() {
        return Integer.parseInt(this.paramValue);
    }

    public String getParamValueString() {
        return this.paramValue;
    }

    public float getParamValueFloat() {
        return Float.parseFloat(this.paramValue);
    }

    @Override
    public void unpack(CUBRIDUnpacker unpacker) {
        this.paramId = unpacker.unpackInt(); // paramId
        this.paramType = unpacker.unpackInt(); // paramType
        this.paramValue = new String(unpacker.unpackCStringByteArray());
    }

    @Override
    public void pack(CUBRIDPacker packer) {
        packer.packInt(this.paramId);
        packer.packInt(this.paramType);
        packer.packString(this.paramValue);
    }
}
