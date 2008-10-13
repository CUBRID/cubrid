package com.cubrid.jsp.value;

import com.cubrid.jsp.exception.TypeMismatchException;

public class BooleanValue extends Value {

    private byte value = 0;
    
    public BooleanValue(boolean b) {
        super();
        if (b) {
            value = 1;
        }
    }

    public BooleanValue(boolean b, int mode) {
        super(mode);
        if (b) {
            value = 1;
        }
    }

    public Object toDefault() throws TypeMismatchException {
        return new Integer(value);
    }

    public Short toShortObject() throws TypeMismatchException {
        return new Short(value);
    }

    public Integer toIntegerObject() throws TypeMismatchException {
        return new Integer(value);
    }

    public Float toFloatObject() throws TypeMismatchException {
        return new Float(value);
    }

    public Double toDoubleObject() throws TypeMismatchException {
        return new Double(value);
    }
    
    public String toString() {
        return "" + value;
    }
}

