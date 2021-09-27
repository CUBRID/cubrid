package com.cubrid.jsp.io;
import cubrid.jdbc.jci.UErrorCode;
import cubrid.jdbc.jci.UJciException;
import cubrid.jdbc.jci.UUType;

public class SUColumnInfo {    
    private static final byte SET_FLAG = (byte) 0040;
    private static final byte MULTISET_FLAG = (byte) 0100;
    private static final byte SEQUENCE_FLAG = (byte) 0140;

    private byte type;
    private byte collectionBaseType;
    private short scale;
    private int precision;
    private String charsetName;

    private String name, className, attributeName;

    private boolean isNullable;
    private String defaultValue;

    private byte isAutoIncrement;
    private byte isUniqueKey;
    private byte isPrimaryKey;
    private byte isForeignKey;
    private byte isReverseIndex;
    private byte isReverseUnique;
    private byte isShared;

    public SUColumnInfo (
        byte type, short scale, int precision, String name, byte collectionFlag, String charset)
    {
        byte realType[];

        realType = SUColumnInfo.confirmType(type, collectionFlag);
        this.type = realType[0];
        this.collectionBaseType = realType[1];
        this.scale = scale;
        this.precision = precision;
        this.charsetName = charset;
        this.name = name;
        this.className = null;
        this.attributeName = null;
        this.isNullable = false;

        defaultValue = null;
        isAutoIncrement = 0;
        isUniqueKey = 0;
        isPrimaryKey = 0;
        isForeignKey = 0;
        isReverseIndex = 0;
        isReverseUnique = 0;
        isShared = 0;
    }

    public static byte[] confirmType (byte originalType, byte collectionFlags)
    {
        byte typeInfo[];

        typeInfo = new byte[2];
        switch (collectionFlags) {
            case 0:
                typeInfo[0] = originalType;
                typeInfo[1] = -1;
                return typeInfo;
            case SET_FLAG:
                typeInfo[0] = UUType.U_TYPE_SET;
                typeInfo[1] = originalType;
                if (typeInfo[1] < UUType.U_TYPE_MIN || typeInfo[1] > UUType.U_TYPE_MAX)
                    ; //throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
                return typeInfo;
            case MULTISET_FLAG:
                typeInfo[0] = UUType.U_TYPE_MULTISET;
                typeInfo[1] = originalType;
                if (typeInfo[1] < UUType.U_TYPE_MIN || typeInfo[1] > UUType.U_TYPE_MAX)
                    ; //throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
                return typeInfo;
            case SEQUENCE_FLAG:
                typeInfo[0] = UUType.U_TYPE_SEQUENCE;
                typeInfo[1] = originalType;
                if (typeInfo[1] < UUType.U_TYPE_MIN || typeInfo[1] > UUType.U_TYPE_MAX)
                    ; //throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
                return typeInfo;
            default:
                typeInfo[0] = UUType.U_TYPE_NULL;
                typeInfo[1] = -1;
        }
        return typeInfo;
    }

        /* get functions */
        public String getDefaultValue() {
            return defaultValue;
        }
    
        public byte getIsAutoIncrement() {
            return isAutoIncrement;
        }
    
        public byte getIsUniqueKey() {
            return isUniqueKey;
        }
    
        public byte getIsPrimaryKey() {
            return isPrimaryKey;
        }
    
        public byte getIsForeignKey() {
            return isForeignKey;
        }
    
        public byte getIsReverseIndex() {
            return isReverseIndex;
        }
    
        public byte getIsReverseUnique() {
            return isReverseUnique;
        }
    
        public byte getIsShared() {
            return isShared;
        }
    
        public boolean isNullable() {
            return isNullable;
        }
    
        public String getClassName() {
            return className;
        }
    
        public int getCollectionBaseType() {
            return collectionBaseType;
        }
    
        public String getColumnName() {
            return name;
        }
    
        public int getColumnPrecision() {
            return precision;
        }
    
        public int getColumnScale() {
            return (int) scale;
        }
    
        public String getColumnCharset() {
            return charsetName;
        }
    
        public byte getColumnType() {
            return type;
        }

        /*
        public String getFQDN() {
            // return FQDN;
            return (findFQDN(type, precision, collectionBaseType));
        }
        */
    
        public String getRealColumnName() {
            return attributeName;
        }

    /* set the extra fields */
    synchronized void setExtraData(
        String defValue, byte bAI, byte bUK, byte bPK, byte bFK, byte bRI, byte bRU, byte sh) {
    defaultValue = defValue;
    isAutoIncrement = bAI;
    isUniqueKey = bUK;
    isPrimaryKey = bPK;
    isForeignKey = bFK;
    isReverseIndex = bRI;
    isReverseUnique = bRU;
    isShared = sh;
}

}
