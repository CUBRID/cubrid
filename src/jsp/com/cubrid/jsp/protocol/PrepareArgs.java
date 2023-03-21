package com.cubrid.jsp.protocol;

import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.Value;

public class PrepareArgs {

    private long groupId = -1;
    private Value[] arguments = null;

    public PrepareArgs(CUBRIDUnpacker unpacker) throws TypeMismatchException {
        readArgs(unpacker);
    }

    public void setArgs(Value[] args) {
        this.arguments = args;
    }

    public Value[] getArgs() {
        return arguments;
    }

    public int getArgCount() {
        if (arguments == null) {
            return -1;
        } else {
            return arguments.length;
        }
    }

    public void readArgs(CUBRIDUnpacker unpacker) throws TypeMismatchException {
        groupId = unpacker.unpackBigint();
        int argCount = unpacker.unpackInt();

        if (arguments == null || argCount != arguments.length) {
            arguments = new Value[argCount];
        }

        for (int i = 0; i < arguments.length; i++) {
            int paramType = unpacker.unpackInt();

            Value arg = unpacker.unpackValue(paramType);
            arguments[i] = (arg);
        }
    }
}
