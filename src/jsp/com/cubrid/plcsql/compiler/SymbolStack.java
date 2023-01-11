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
import java.util.Arrays;
import java.util.List;
import java.util.LinkedList;
import java.util.Map;
import java.util.TreeMap;
import java.util.stream.Collectors;

public class SymbolStack {

    void setUpPredefined() {

        // add exceptions
        DeclException de;
        for (String s : predefinedExceptions) {
            de = new DeclException(s);
            putDecl(de.name, de);
        }

        // add procedures
        DeclProc dp =
                new DeclProc(
                        "PUT_LINE",
                        new NodeList<DeclParam>()
                                .addNode(new DeclParamIn("s", new TypeSpec("Object"))),
                        null,
                        null,
                        0);
        putDecl("PUT_LINE", dp);

        // add functions
        DeclFunc df = new DeclFunc("SYSDATE", new NodeList<DeclParam>(), new TypeSpec("LocalDate"), null, null, 0);
        putDecl("SYSDATE", df);

        // add constants TODO implement SQLERRM and SQLCODE properly
        DeclConst dc = new DeclConst("SQLERRM", new TypeSpec("String"), ExprNull.instance());
        putDecl("SQLERRM", dc);

        dc = new DeclConst("SQLCODE", new TypeSpec("Integer"), ExprNull.instance());
        putDecl("SQLCODE", dc);

        dc = new DeclConst("SQL", new TypeSpec("ResultSet"), ExprNull.instance());
        putDecl("SQL", dc);
    }

    int pushSymbolTable(String name, boolean forRoutine) {

        int level = symbolTableStack.size();
        name = name.toLowerCase();

        String routine;
        if (forRoutine) {
            routine = name.toUpperCase();
        } else {
            routine = (currSymbolTable == null) ? null : currSymbolTable.scope.routine;
        }

        String block = name + "_" + level;

        currSymbolTable = new SymbolTable(new Scope(routine, block, level));
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
    private void putOperator(String name, DeclFunc df) {

        assert currSymbolTable.scope.level == 0;

        OverloadedFunc overload = operators.get(name);
        if (overload == null) {
            overload = new OverloadedFunc();
            operators.put(name, overload);
        }

        df.setScope(currSymbolTable.scope);
        overload.put(df);
    }

    private void putPredefinedFunc(String name, DeclFunc df) {

        assert currSymbolTable.scope.level == 0;

        OverloadedFunc overload = predefinedFuncs.get(name);
        if (overload == null) {
            overload = new OverloadedFunc();
            predefinedFuncs.put(name, overload);
        }

        df.setScope(currSymbolTable.scope);
        overload.put(df);
    }

    DeclFunc getOperator(String name, List<TypeSpec> argTypes) {

        OverloadedFunc overload = operators.get(name);
        if (overload == null) {
            return null;
        } else {
            return overload.get(argTypes);
        }
    }

    // ----------------------------------------
    //

    <D extends Decl> void putDecl(String name, D decl) {
        assert decl != null;

        Map<String, D> map = currSymbolTable.<D>map((Class<D>) decl.getClass());
        assert map != null;
        if (map == currSymbolTable.labels) {
            if (map.containsKey(name)) {
                assert false : name + " has already been declared in the same scope";
                throw new RuntimeException("unreachable");
            }
        } else {
            if (currSymbolTable.ids.containsKey(name) ||
                currSymbolTable.procs.containsKey(name) ||
                currSymbolTable.funcs.containsKey(name) ||
                currSymbolTable.exceptions.containsKey(name)) {
                assert false : name + " has already been declared in the same scope";
                throw new RuntimeException("unreachable");
            }
            if (currSymbolTable.scope.level == 0 && map == currSymbolTable.funcs) {
                if (predefinedFuncs.containsKey(name)) {
                    assert false : name + " is a predefined function";
                    throw new RuntimeException("unreachable");
                }
            }
        }

        decl.setScope(currSymbolTable.scope);
        map.put(name, decl);
    }

    DeclId getDeclId(String name) {
        return getDecl(DeclId.class, name);
    }

    DeclProc getDeclProc(String name) {
        return getDecl(DeclProc.class, name);
    }

    // TODO: remove this
    DeclFunc getDeclFunc(String name) {
        DeclFunc ret = getDecl(DeclFunc.class, name);
        if (ret == null) {
            // search the predefined functions too
            OverloadedFunc overloaded = predefinedFuncs.get(name);
            if (overloaded == null) {
                return null;
            } else {
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
            return ret;
        }
    }

    DeclException getDeclException(String name) {
        return getDecl(DeclException.class, name);
    }

    DeclLabel getDeclLabel(String name) {
        return getDecl(DeclLabel.class, name);
    }

    // ----------------------------------------------------
    // Private
    // ----------------------------------------------------

    private SymbolTable currSymbolTable;

    private final Map<String, OverloadedFunc> operators = new TreeMap<>();
    private final Map<String, OverloadedFunc> predefinedFuncs = new TreeMap<>();

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


}
