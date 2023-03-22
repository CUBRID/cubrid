package com.cubrid.jsp.data;

public class ColumnInfo {
    public int type;
    public int setType;

    public String className;
    public String attrName;

    public short scale;
    public int prec;
    public byte charset;
    public String colName;
    public byte isNotNull;

    public byte autoIncrement;
    public byte uniqueKey;
    public byte primaryKey;
    public byte reverseIndex;
    public byte reverseUnique;
    public byte foreignKey;
    public byte shared;
    public String defaultValueString;

    public ColumnInfo() { } // for mock server API

    public ColumnInfo(CUBRIDUnpacker unpacker) {
        type = unpacker.unpackInt();
        setType = unpacker.unpackInt();

        charset = (byte) unpacker.unpackInt();
        scale = unpacker.unpackShort();
        prec = unpacker.unpackInt();

        colName = unpacker.unpackCString();
        attrName = unpacker.unpackCString();
        className = unpacker.unpackCString();
        defaultValueString = unpacker.unpackCString();

        isNotNull = (byte) unpacker.unpackInt();
        autoIncrement = (byte) unpacker.unpackInt();
        uniqueKey = (byte) unpacker.unpackInt();
        primaryKey = (byte) unpacker.unpackInt();
        reverseIndex = (byte) unpacker.unpackInt();
        reverseUnique = (byte) unpacker.unpackInt();
        foreignKey = (byte) unpacker.unpackInt();
        shared = (byte) unpacker.unpackInt();
    }

    public int getColumnType() {
        return type;
    }

    public int getCollectionType() {
        return setType;
    }

    public int getColumnPrecision() {
        return prec;
    }

    public int getColumnScale() {
        return scale;
    }

    public boolean getIsNotNull() {
        return (isNotNull == 1);
    }

    public String getClassName() {
        return className;
    }

    public String getColumnName() {
        return colName;
    }

    public String getColumnCharsetName() {
        return getJavaCharsetName(charset);
    }

    public boolean isAutoIncrement() {
        return (autoIncrement == 1);
    }

    private static String getJavaCharsetName(byte cubridCharset) {
        switch (cubridCharset) {
            case 0:
                return "ASCII";
            case 2:
                return "BINARY";
            case 3:
                return "ISO8859_1";
            case 4:
                return "EUC_KR";
            case 5:
                return "UTF8";
            default:
        }
        return null;
    }
}
