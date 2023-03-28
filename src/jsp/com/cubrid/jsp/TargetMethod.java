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

import com.cubrid.jsp.exception.ExecuteException;
import cubrid.sql.CUBRIDOID;
import java.lang.reflect.Method;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.HashMap;

public class TargetMethod {
    private String className;
    private String methodName;
    private Class<?>[] argsTypes;

    private static HashMap<String, Class<?>> argClassMap = new HashMap<String, Class<?>>();
    private static HashMap<String, String> descriptorMap = new HashMap<String, String>();

    static {
        initArgClassMap();
        initdescriptorMap();
    }

    public TargetMethod(String signature) throws Exception {
        int argStart = signature.indexOf('(') + 1;
        if (argStart < 0) {
            throw new IllegalArgumentException("Parenthesis '(' not found");
        }
        int argEnd = signature.indexOf(')');
        if (argEnd < 0) {
            throw new IllegalArgumentException("Parenthesis ')' not found");
        }
        int nameStart = signature.substring(0, argStart).lastIndexOf('.') + 1;

        if (signature.charAt(0) == '\'') {
            className = signature.substring(1, nameStart - 1);
        } else {
            className = signature.substring(0, nameStart - 1);
        }

        methodName = signature.substring(nameStart, argStart - 1);
        String args = signature.substring(argStart, argEnd);
        argsTypes = classesFor(args);
    }

    private Class<?> getClass(String name) throws ClassNotFoundException {
        ClassLoader cl = StoredProcedureClassLoader.getInstance();
        Class<?> c = null;
        try {
            c = cl.loadClass(name);
            return c;
        } catch (ClassNotFoundException e) {
            //
        }

        // TODO: CBRD-24514
        try {
            c = Server.class.getClassLoader().loadClass(name);
            return c;
        } catch (ClassNotFoundException e) {
            //
        }

        return c;
    }

    private Class<?>[] classesFor(String args) throws ClassNotFoundException, ExecuteException {
        args = args.trim();
        if (args.length() == 0) {
            return new Class[0];
        }
        // Count semicolons occurences.
        int semiColons = 0;
        for (int i = 0; i < args.length(); i++) {
            if (args.charAt(i) == ',') {
                semiColons++;
            }
        }
        Class<?>[] classes = new Class[semiColons + 1];

        int index = 0;
        for (int i = 0; i < semiColons; i++) {
            int sep = args.indexOf(',', index);
            classes[i] = classFor(args.substring(index, sep).trim());
            index = sep + 1;
        }

        classes[semiColons] = classFor(args.substring(index).trim());
        return classes;
    }

    private Class<?> classFor(String className) throws ClassNotFoundException, ExecuteException {
        int arrayIndex = className.indexOf("[]");
        if (arrayIndex >= 0) {
            String arrTag;
            if (className.indexOf("[][]") >= 0) {
                if (className.indexOf("[][][]") >= 0) {
                    throw new ExecuteException("Unsupport data type: " + className);
                }
                arrTag = "[[";
            } else {
                arrTag = "[";
            }
            className = arrTag + descriptorFor(className.substring(0, arrayIndex).trim());
        }

        if (argClassMap.containsKey(className)) {
            return argClassMap.get(className);
        } else {
            return getClass(className);
        }
    }

    private String descriptorFor(String className) {
        if (descriptorMap.containsKey(className)) {
            return (String) descriptorMap.get(className);
        } else {
            return "L" + className + ";";
        }
    }

    private static void initArgClassMap() {
        argClassMap.put("boolean", boolean.class);
        argClassMap.put("byte", byte.class);
        argClassMap.put("char", char.class);
        argClassMap.put("short", short.class);
        argClassMap.put("int", int.class);
        argClassMap.put("long", long.class);
        argClassMap.put("float", float.class);
        argClassMap.put("double", double.class);

        argClassMap.put("[Z", boolean[].class);
        argClassMap.put("[B", byte[].class);
        argClassMap.put("[C", char[].class);
        argClassMap.put("[S", short[].class);
        argClassMap.put("[I", int[].class);
        argClassMap.put("[J", long[].class);
        argClassMap.put("[F", float[].class);
        argClassMap.put("[D", double[].class);

        argClassMap.put("java.lang.Boolean", Boolean.class);
        argClassMap.put("java.lang.Byte", Byte.class);
        argClassMap.put("java.lang.Character", Character.class);
        argClassMap.put("java.lang.Short", Short.class);
        argClassMap.put("java.lang.Integer", Integer.class);
        argClassMap.put("java.lang.Long", Long.class);
        argClassMap.put("java.lang.Float", Float.class);
        argClassMap.put("java.lang.Double", Double.class);
        argClassMap.put("java.lang.String", String.class);
        argClassMap.put("java.lang.Object", Object.class);
        argClassMap.put("java.math.BigDecimal", BigDecimal.class);
        argClassMap.put("java.sql.Date", Date.class);
        argClassMap.put("java.sql.Time", Time.class);
        argClassMap.put("java.sql.Timestamp", Timestamp.class);
        argClassMap.put("cubrid.sql.CUBRIDOID", CUBRIDOID.class);

        argClassMap.put("[Ljava.lang.Boolean;", Boolean[].class);
        argClassMap.put("[Ljava.lang.Byte;", Byte[].class);
        argClassMap.put("[Ljava.lang.Character;", Character[].class);
        argClassMap.put("[Ljava.lang.Short;", Short[].class);
        argClassMap.put("[Ljava.lang.Integer;", Integer[].class);
        argClassMap.put("[Ljava.lang.Long;", Long[].class);
        argClassMap.put("[Ljava.lang.Float;", Float[].class);
        argClassMap.put("[Ljava.lang.Double;", Double[].class);
        argClassMap.put("[Ljava.lang.String;", String[].class);
        argClassMap.put("[Ljava.lang.Object;", Object[].class);
        argClassMap.put("[Ljava.math.BigDecimal;", BigDecimal[].class);
        argClassMap.put("[Ljava.sql.Date;", Date[].class);
        argClassMap.put("[Ljava.sql.Time;", Time[].class);
        argClassMap.put("[Ljava.sql.Timestamp;", Timestamp[].class);
        argClassMap.put("[Lcubrid.sql.CUBRIDOID;", CUBRIDOID[].class);

        argClassMap.put("[[Ljava.lang.Integer;", Integer[][].class);
        argClassMap.put("[[Ljava.lang.Float;", Float[][].class);
    }

    private static void initdescriptorMap() {
        descriptorMap.put("boolean", "Z");
        descriptorMap.put("byte", "B");
        descriptorMap.put("char", "C");
        descriptorMap.put("short", "S");
        descriptorMap.put("int", "I");
        descriptorMap.put("long", "J");
        descriptorMap.put("float", "F");
        descriptorMap.put("double", "D");
    }

    public Method getMethod()
            throws SecurityException, NoSuchMethodException, ClassNotFoundException {
        Class<?> c = getClass(className);
        if (c == null) {
            throw new ClassNotFoundException(className);
        }
        Method m = c.getMethod(methodName, argsTypes);
        return m;
    }

    public Class<?>[] getArgsTypes() {
        return argsTypes;
    }
}
