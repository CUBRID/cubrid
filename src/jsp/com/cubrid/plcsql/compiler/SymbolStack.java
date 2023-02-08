/*
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

package com.cubrid.plcsql.compiler;

import static com.cubrid.plcsql.compiler.antlrgen.PcsParser.*;

import com.cubrid.plcsql.compiler.ast.*;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

import java.util.Arrays;
import java.util.List;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.Map;
import java.util.TreeMap;
import java.util.stream.Collectors;

public class SymbolStack {

    // -------------------------------------------------------
    // Static area - common to all symbol stack instances
    //

    private static List<String> predefinedExceptions =
            Arrays.asList(
                    "$APP_ERROR", // for raise_application_error
                    "CASE_NOT_FOUND",
                    "CURSOR_ALREADY_OPEN",
                    "DUP_VAL_ON_INDEX",
                    "INVALID_CURSOR",
                    "LOGIN_DENIED",
                    "NO_DATA_FOUND",
                    "PROGRAM_ERROR",
                    "ROWTYPE_MISMATCH",
                    "STORAGE_ERROR",
                    "TOO_MANY_ROWS",
                    "VALUE_ERROR",
                    "ZERO_DIVIDE");

    private static final Map<String, OverloadedFunc> operators = new TreeMap<>();
    private static final Map<String, OverloadedFunc> predefinedFuncs = new TreeMap<>();
    private static SymbolTable predefinedSymbols = new SymbolTable(new Scope(null, null, "%predefined_0", 0));

    static {

        // add SpLib staic methods corresponding to operators
        {
            Class c = null;
            try {
                c = Class.forName("com.cubrid.plcsql.predefined.sp.SpLib");
            } catch (ClassNotFoundException e) {
                assert false : "SpLib class not found";
            }

            Method[] methods = c.getMethods();
            for (Method m: methods) {
                if ((m.getModifiers() & Modifier.STATIC) > 0) {
                    String name = m.getName();
                    if (name.startsWith("op")) {
                        //System.out.println("temp: " + m.getName());

                        // parameter types
                        Class[] paramTypes = m.getParameterTypes();
                        NodeList<DeclParam> params;
                        if (paramTypes.length == 0) {
                            params = null;
                        } else {
                            params = new NodeList<>();
                            int i = 0;
                            for (Class pt: paramTypes) {
                                String typeName = pt.getTypeName();
                                //System.out.println("  " + typeName);
                                DeclParamIn p = new DeclParamIn("p" + i, TypeSpecSimple.of(typeName));
                                params.addNode(p);
                            }
                        }

                        // return type
                        String typeName = m.getReturnType().getName();
                        //System.out.println("  =>" + typeName);
                        TypeSpec retType = TypeSpecSimple.of(typeName);

                        // add op
                        DeclFunc op = new DeclFunc(name, params, retType);
                        putOperator(name, op);
                    }
                }
            }

            // add exceptions
            DeclException de;
            for (String s : predefinedExceptions) {
                de = new DeclException(s);
                putDeclTo(predefinedSymbols, de.name, de);
            }

            // add procedures
            DeclProc dp =
                    new DeclProc(
                            "PUT_LINE",
                            new NodeList<DeclParam>()
                                    .addNode(new DeclParamIn("s", TypeSpecSimple.of("java.lang.Object"))),
                            null,
                            null);
            putDeclTo(predefinedSymbols, "PUT_LINE", dp);

            // add constants TODO implement SQLERRM and SQLCODE properly
            DeclConst dc = new DeclConst("SQLERRM", TypeSpecSimple.of("java.lang.String"), false, ExprNull.SINGLETON);
            putDeclTo(predefinedSymbols, "SQLERRM", dc);

            dc = new DeclConst("SQLCODE", TypeSpecSimple.of("java.lang.Integer"), false, ExprNull.SINGLETON);
            putDeclTo(predefinedSymbols, "SQLCODE", dc);

            dc = new DeclConst("SYSDATE", TypeSpecSimple.of("java.time.LocalDate"), false, ExprNull.SINGLETON);
            putDeclTo(predefinedSymbols, "SYSDATE", dc);
        }
    }

    private static void putOperator(String name, DeclFunc df) {

        OverloadedFunc overload = operators.get(name);
        if (overload == null) {
            overload = new OverloadedFunc();
            operators.put(name, overload);
        }

        df.setScope(predefinedSymbols.scope);
        overload.put(df);
    }

    private static void putPredefinedFunc(String name, DeclFunc df) {

        OverloadedFunc overload = predefinedFuncs.get(name);
        if (overload == null) {
            overload = new OverloadedFunc();
            predefinedFuncs.put(name, overload);
        }

        df.setScope(predefinedSymbols.scope);
        overload.put(df);
    }

    public static DeclFunc getOperator(String name, TypeSpec... argTypes) {

        OverloadedFunc overload = operators.get(name);
        if (overload == null) {
            return null;
        } else {
            return overload.get(Arrays.asList(argTypes));
        }
    }

    // -----------------------------------------------------------------------------
    // end of Static
    //

    SymbolStack() {
        symbolTableStack.addFirst(predefinedSymbols);
        currSymbolTable = new SymbolTable(new Scope(null, null, "unit_1", 1)); // for the main procedure/function
        symbolTableStack.addFirst(currSymbolTable);
    }

    int pushSymbolTable(String name, Misc.RoutineType routineType) {

        int level = symbolTableStack.size();
        name = name.toLowerCase();

        String routine;
        if (routineType == null) {
            if (currSymbolTable == null) {
                routineType = null;
                routine = null;
            } else {
                // inherit them from its parent
                routineType = currSymbolTable.scope.routineType;
                routine = currSymbolTable.scope.routine;
            }
        } else {
            routine = name.toUpperCase();
        }

        String block = name + "_" + level;

        currSymbolTable = new SymbolTable(new Scope(routineType, routine, block, level));
        symbolTableStack.addFirst(currSymbolTable);

        return level;
    }

    void popSymbolTable() {
        assert symbolTableStack.size() > 0;
        symbolTableStack.removeFirst();
        currSymbolTable = symbolTableStack.peek();
    }

    int getSize() {
        return symbolTableStack.size();
    }

    Scope getCurrentScope() {
        return currSymbolTable.scope;
    }

    // ----------------------------------------
    //

    // ----------------------------------------
    //

    <D extends Decl> void putDecl(String name, D decl) {
        putDeclTo(currSymbolTable, name, decl);
    }

    static <D extends Decl> void putDeclTo(SymbolTable symbolTable, String name, D decl) {
        assert decl != null;

        Map<String, D> map = symbolTable.<D>map((Class<D>) decl.getClass());
        assert map != null;
        if (map == symbolTable.labels) {
            if (map.containsKey(name)) {
                assert false : name + " has already been declared in the same scope";
                throw new RuntimeException("unreachable");
            }
        } else {
            if (symbolTable.ids.containsKey(name) ||
                symbolTable.procs.containsKey(name) ||
                symbolTable.funcs.containsKey(name) ||
                symbolTable.exceptions.containsKey(name)) {
                assert false : name + " has already been declared in the same scope";
                throw new RuntimeException("unreachable");
            }
            if (symbolTable.scope.level == 0 && map == symbolTable.funcs) {
                if (predefinedFuncs.containsKey(name)) {
                    assert false : name + " is a predefined function";
                    throw new RuntimeException("unreachable");
                }
            }
        }

        decl.setScope(symbolTable.scope);
        map.put(name, decl);
    }

    DeclId getDeclId(String name) {
        return getDecl(DeclId.class, name);
    }

    DeclProc getDeclProc(String name) {
        return getDecl(DeclProc.class, name);
    }

    DeclFunc getDeclFunc(String name) {
        DeclFunc ret = getDecl(DeclFunc.class, name);
        if (ret == null) {
            // search the predefined functions too
            OverloadedFunc overloaded = predefinedFuncs.get(name);
            if (overloaded == null) {
                return null;
            } else {
                assert overloaded.overloads.size() == 1 :
                    "getting an overloaded function " + name + " only by its name is ambiguous";
                return overloaded.overloads.values().iterator().next();
            }
        } else {
            return ret;
        }
    }

    DeclFunc getDeclFunc(String name, List<TypeSpec> argTypes) {
        DeclFunc ret = getDecl(DeclFunc.class, name);
        if (ret == null) {
            // search the predefined functions too
            OverloadedFunc overloaded = predefinedFuncs.get(name);
            if (overloaded == null) {
                return null;
            } else {
                return overloaded.get(argTypes);
            }
        } else {
            if (argTypes.equals(ret.getParamTypes())) {
                return ret;
            } else {
                return null;
            }
        }
    }

    DeclException getDeclException(String name) {
        return getDecl(DeclException.class, name);
    }

    DeclLabel getDeclLabel(String name) {
        return getDecl(DeclLabel.class, name);
    }

    // return DeclId or DeclFunc for an identifier expression
    Decl getDeclForIdExpr(String name) {
        DeclId declId = getDeclId(name);
        DeclFunc declFunc = getDeclFunc(name, EMPTY_TYPE_SPEC);   // one with no parameters

        if (declFunc == null) {
            return declId;
        } else if (declId == null) {
            return declFunc;
        } else {
            if (declId.scope().level > declFunc.scope().level) {
                return declId;
            } else {
                assert declId.scope().level < declFunc.scope().level;   // they cannot be on the same level
                return declFunc;
            }
        }
    }

    // ----------------------------------------------------
    // Private
    // ----------------------------------------------------

    private static final List<TypeSpec> EMPTY_TYPE_SPEC = new ArrayList<TypeSpec>();

    private SymbolTable currSymbolTable;

    private LinkedList<SymbolTable> symbolTableStack = new LinkedList<>();

    private static class SymbolTable {
        final Scope scope;

        final Map<String, DeclId> ids = new TreeMap<>();
        final Map<String, DeclProc> procs = new TreeMap<>();
        final Map<String, DeclFunc> funcs = new TreeMap<>();
        final Map<String, DeclException> exceptions = new TreeMap<>();
        final Map<String, DeclLabel> labels = new TreeMap<>();

        SymbolTable(Scope scope) {
            this.scope = scope;
        }

        <D extends Decl> Map<String, D> map(Class<D> declClass) {
            if (DeclId.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) ids;
            } else if (DeclProc.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) procs;
            } else if (DeclFunc.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) funcs;
            } else if (DeclException.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) exceptions;
            } else if (DeclLabel.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) labels;
            } else {
                assert false : ("unknown declaration type: " + declClass.getSimpleName());
                return null;
            }
        }
    }

    private <D extends Decl> D getDecl(Class<D> declClass, String name) {
        assert name != null;

        int size = symbolTableStack.size();
        for (int i = 0; i < size; i++) {
            SymbolTable t = symbolTableStack.get(i);
            Map<String, D> map = t.<D>map(declClass);
            if (map.containsKey(name)) {
                return map.get(name);
            }
        }

        return null;
    }

    // OverloadedFunc class corresponds to operators (+, -, etc) and system provided functions (substr, trim, strcomp, etc)
    // which can be overloaded unlike user defined procedures and functions.
    // It implements DeclId in order to be inserted ids map for system provided functions.
    private static class OverloadedFunc extends DeclBase {

        private final Map<String, DeclFunc> overloads = new TreeMap<>();   // (arguments types --> function decl) map

        void put(DeclFunc decl) {
            List<TypeSpec> paramTypes =
                decl.paramList.nodes.stream()
                    .map(e -> e.typeSpec())
                    .collect(Collectors.toList());
            String key = getKey(paramTypes);
            DeclFunc old = overloads.put(key, decl);
            // system predefined operators and functions must be unique with their names and argument types.
            assert old == null;
        }

        DeclFunc get(List<TypeSpec> argTypes) {
            String key = getKey(argTypes);
            return overloads.get(key);
        }

        @Override
        public String kind() {
            return "operator";
        }

        @Override
        public String toJavaCode() {
            assert false: "unreachable";
            throw new RuntimeException("unreachagle");
        }

        // -----------------------------------------------
        // Private
        // -----------------------------------------------

        String getKey(List<TypeSpec> types) {

            StringBuffer sbuf = new StringBuffer();

            for (TypeSpec t: types) {

                if (sbuf.length() > 0) {
                    sbuf.append(',');
                }
                sbuf.append(t.name);
            }

            return sbuf.toString();
        }
    }
}
