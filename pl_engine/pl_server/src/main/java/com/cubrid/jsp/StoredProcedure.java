/*
 * Copyright (C) 2008 Search Solution Corporation.
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.jsp;

import com.cubrid.jsp.classloader.ServerClassLoader;
import com.cubrid.jsp.code.ClassAccess;
import com.cubrid.jsp.code.CompiledCodeSet;
import com.cubrid.jsp.code.Signature;
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.context.ContextManager;
import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.NullValue;
import com.cubrid.jsp.value.SetValue;
import com.cubrid.jsp.value.Value;
import com.cubrid.jsp.value.ValueUtilities;
import cubrid.sql.CUBRIDOID;
import java.lang.reflect.Method;
import java.math.BigDecimal;
import java.sql.Connection;
import java.sql.Date;
import java.sql.ResultSet;
import java.sql.Time;
import java.sql.Timestamp;

public class StoredProcedure {
    private String signature;
    private String authUser;
    private Value[] args;
    private int returnType;
    private int lang;

    private Class<?> targetClass;
    private TargetMethod target;

    private Object[] cachedResolved;

    // METHOD_TYPE in method_def.hpp
    private static final int LANG_JAVASP = 3;
    private static final int LANG_PLCSQL = 4;

    public StoredProcedure(
            String signature, int lang, String authUser, Value[] args, int returnType)
            throws Exception {
        this.signature = signature;
        this.authUser = authUser;
        this.args = args;
        this.returnType = returnType;
        this.lang = lang;

        this.target = findTargetMethod(signature);

        this.cachedResolved = null;

        checkArgs();
    }

    private TargetMethod findTargetMethod(String sigString) throws Exception {
        Context ctx = ContextManager.getContextofCurrentThread();

        Connection conn = ctx.getConnection();
        Signature sig = Signature.parse(sigString);

        Class<?> c = null;
        ClassNotFoundException ex = null;
        if (lang == LANG_PLCSQL) {
            try {
                c = ctx.getSessionCLManager().findClass(sig.getClassName());
                if (c == null) {
                    CompiledCodeSet codeset = ClassAccess.getObjectCode(conn, sig);
                    if (codeset != null) {
                        c = ctx.getSessionCLManager().loadClass(codeset);
                    }
                }
            } catch (ClassNotFoundException e) {
                ex = e;
            }
        } else if (lang == LANG_JAVASP) {
            try {
                c = ctx.getOldClassLoader().loadClass(sig.getClassName());
            } catch (ClassNotFoundException e) {
                ex = e;
            }
        } else {
            assert false;
            throw new ClassNotFoundException(sig.getClassName());
        }

        // find a class in static directory and system loader
        if (c == null) {
            c = ServerClassLoader.getInstance().loadClass(sig.getClassName());
        }

        if (c == null) {
            throw ex;
        }

        targetClass = c;
        TargetMethod target = new TargetMethod(sig);
        return target;
    }

    public Object[] getResolved() {
        if (args == null) {
            return null;
        }

        Object[] resolved = new Object[args.length];
        for (int i = 0; i < args.length; i++) {
            resolved[i] = args[i].getResolved();
        }

        return resolved;
    }

    private void checkArgs() throws TypeMismatchException {
        if (args == null) {
            return;
        }

        Class<?>[] argsTypes = target.getArgsTypes();
        if (argsTypes.length != args.length) {
            throw new TypeMismatchException(
                    "Argument count mismatch: expected "
                            + argsTypes.length
                            + ", but "
                            + args.length);
        }
        for (int i = 0; i < argsTypes.length; i++) {
            Object resolved;
            if (args[i] == null) {
                resolved = null;
            } else if (argsTypes[i] == byte.class || argsTypes[i] == Byte.class) {
                resolved = args[i].toByteObject();
            } else if (argsTypes[i] == short.class || argsTypes[i] == Short.class) {
                resolved = args[i].toShortObject();

            } else if (argsTypes[i] == int.class || argsTypes[i] == Integer.class) {
                resolved = args[i].toIntegerObject();

            } else if (argsTypes[i] == long.class || argsTypes[i] == Long.class) {
                resolved = args[i].toLongObject();

            } else if (argsTypes[i] == float.class || argsTypes[i] == Float.class) {
                resolved = args[i].toFloatObject();

            } else if (argsTypes[i] == double.class || argsTypes[i] == Double.class) {
                resolved = args[i].toDoubleObject();

            } else if (argsTypes[i] == String.class) {
                resolved = args[i].toString();

            } else if (argsTypes[i] == Date.class) {
                resolved = args[i].toDate();

            } else if (argsTypes[i] == Time.class) {
                resolved = args[i].toTime();

            } else if (argsTypes[i] == Timestamp.class) {
                if (args[i].getDbType() == DBType.DB_DATETIME) {
                    resolved = args[i].toDatetime();
                } else {
                    resolved = args[i].toTimestamp();
                }

            } else if (argsTypes[i] == BigDecimal.class) {
                resolved = args[i].toBigDecimal();

            } else if (argsTypes[i] == CUBRIDOID.class) {
                resolved = args[i].toOid();

            } else if (argsTypes[i] == Object.class) {
                resolved = args[i].toObject();

            } else if (argsTypes[i] == byte[].class) {
                resolved = args[i].toByteArray();

            } else if (argsTypes[i] == short[].class) {
                resolved = args[i].toShortArray();

            } else if (argsTypes[i] == int[].class) {
                resolved = args[i].toIntegerArray();

            } else if (argsTypes[i] == long[].class) {
                resolved = args[i].toLongArray();

            } else if (argsTypes[i] == float[].class) {
                resolved = args[i].toFloatArray();

            } else if (argsTypes[i] == double[].class) {
                resolved = args[i].toDoubleArray();

            } else if (argsTypes[i] == String[].class) {
                resolved = args[i].toStringArray();

            } else if (argsTypes[i] == Byte[].class) {
                resolved = args[i].toByteObjArray();

            } else if (argsTypes[i] == Short[].class) {
                resolved = args[i].toShortObjArray();

            } else if (argsTypes[i] == Integer[].class) {
                resolved = args[i].toIntegerObjArray();

            } else if (argsTypes[i] == Long[].class) {
                resolved = args[i].toLongObjArray();

            } else if (argsTypes[i] == Float[].class) {
                resolved = args[i].toFloatObjArray();

            } else if (argsTypes[i] == Double[].class) {
                resolved = args[i].toDoubleObjArray();

            } else if (argsTypes[i] == Date[].class) {
                resolved = args[i].toDateArray();

            } else if (argsTypes[i] == Time[].class) {
                resolved = args[i].toTimeArray();

            } else if (argsTypes[i] == Timestamp[].class) {
                if (args[i].getDbType() == DBType.DB_DATETIME) {
                    resolved = args[i].toDatetimeArray();
                } else {
                    resolved = args[i].toTimestampArray();
                }

            } else if (argsTypes[i] == BigDecimal[].class) {
                resolved = args[i].toBigDecimalArray();

            } else if (argsTypes[i] == CUBRIDOID[].class) {
                resolved = args[i].toOidArray();

            } else if (argsTypes[i] == ResultSet[].class) {
                resolved = args[i].toResultSetArray(null);

            } else if (argsTypes[i] == Object[].class) {
                resolved = args[i].toObjectArray();

            } else if (argsTypes[i] == byte[][].class) {
                resolved = args[i].toByteArrayArray();

            } else if (argsTypes[i] == short[][].class) {
                resolved = args[i].toShortArrayArray();

            } else if (argsTypes[i] == int[][].class) {
                resolved = args[i].toIntegerArrayArray();

            } else if (argsTypes[i] == long[][].class) {
                resolved = args[i].toLongArrayArray();

            } else if (argsTypes[i] == float[][].class) {
                resolved = args[i].toFloatArrayArray();

            } else if (argsTypes[i] == double[].class) {
                resolved = args[i].toDoubleArrayArray();

            } else if (argsTypes[i] == String[][].class) {
                resolved = args[i].toStringArrayArray();

            } else if (argsTypes[i] == Byte[][].class) {
                resolved = args[i].toByteObjArrayArray();

            } else if (argsTypes[i] == Short[][].class) {
                resolved = args[i].toShortObjArrayArray();

            } else if (argsTypes[i] == Integer[][].class) {
                resolved = args[i].toIntegerObjArrayArray();

            } else if (argsTypes[i] == Long[][].class) {
                resolved = args[i].toLongObjArrayArray();

            } else if (argsTypes[i] == Float[][].class) {
                resolved = args[i].toFloatObjArrayArray();

            } else if (argsTypes[i] == Double[][].class) {
                resolved = args[i].toDoubleObjArrayArray();

            } else if (argsTypes[i] == Date[][].class) {
                resolved = args[i].toDateArrayArray();

            } else if (argsTypes[i] == Time[][].class) {
                resolved = args[i].toTimeArrayArray();

            } else if (argsTypes[i] == Timestamp[][].class) {
                if (args[i].getDbType() == DBType.DB_DATETIME) {
                    resolved = args[i].toDatetimeArrayArray();
                } else {
                    resolved = args[i].toTimestampArrayArray();
                }

            } else if (argsTypes[i] == BigDecimal[][].class) {
                resolved = args[i].toBigDecimalArrayArray();

            } else if (argsTypes[i] == CUBRIDOID[][].class) {
                resolved = args[i].toOidArrayArray();

            } else if (argsTypes[i] == ResultSet[][].class) {
                resolved = args[i].toResultSetArrayArray(null);

            } else if (argsTypes[i] == Object[][].class) {
                resolved = args[i].toObjectArrayArray();

            } else {
                throw new TypeMismatchException(
                        "Not supported data type: '" + argsTypes[i].getName() + "'");
            }

            args[i].setResolved(resolved);
        }
    }

    public Value invoke() throws Exception {
        Method m = target.getMethod(targetClass);
        if (cachedResolved == null) {
            cachedResolved = getResolved();
        }
        Object result = m.invoke(null, cachedResolved);
        return ValueUtilities.createValueFrom(result);
    }

    public Value makeOutValue(int idx) throws TypeMismatchException, ExecuteException {
        Class<?>[] argsTypes = target.getArgsTypes();
        if (argsTypes[idx].isArray()) {
            Value resolved = ValueUtilities.createValueFrom(cachedResolved[idx]);
            if (resolved instanceof SetValue) {
                return ((SetValue) resolved).toValueArray()[0];
            } else {
                return resolved;
            }
        } else {
            return new NullValue();
        }
    }

    public int getReturnType() {
        return returnType;
    }

    public Value[] getArgs() {
        return args;
    }

    public void setArgs(Value[] args) {
        this.args = args;
    }

    public String getSignature() {
        return signature;
    }

    public TargetMethod getTarget() {
        return target;
    }

    public String getAuthUser() {
        return authUser;
    }
}
