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
import java.util.List;
import java.util.LinkedList;
import java.util.Map;
import java.util.TreeMap;
import java.util.stream.Collectors;

public class SymbolStack {

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
    void putOperator(String name, DeclFunc df) {

        Operator op = ops.get(name);
        if (op == null) {
            op = new Operator();
            ops.put(name, op);
        }

        assert currSymbolTable.scope.level == 0;
        df.setScope(currSymbolTable.scope);
        op.put(df);
    }

    DeclFunc getOperator(String name, List<TypeSpec> argTypes) {

        Operator op = ops.get(name);
        if (op == null) {
            return null;
        } else {
            return op.get(argTypes);
        }
    }

    // ----------------------------------------
    //

    <D extends Decl> void putDecl(String name, D decl) {
        assert decl != null;

        Map<String, D> map = currSymbolTable.<D>map((Class<D>) decl.getClass());
        if (map.containsKey(name)) {
            assert false
                    : decl.kind() + " " + name + " has already been declared in the same scope";
            throw new RuntimeException("unreachable");
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

    DeclFunc getDeclFunc(String name) {
        return getDecl(DeclFunc.class, name);
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

    private final Map<String, Operator> ops = new TreeMap<>();

    private LinkedList<SymbolTable> symbolTableStack = new LinkedList<>();

    private static class SymbolTable {
        final Scope scope;

        final Map<String, DeclId> ids = new TreeMap<>();
        final Map<String, DeclException> exceptions = new TreeMap<>();
        final Map<String, DeclLabel> labels = new TreeMap<>();

        SymbolTable(Scope scope) {
            this.scope = scope;
        }

        <D extends Decl> Map<String, D> map(Class<D> declClass) {
            if (DeclId.class.isAssignableFrom(declClass) ||
                DeclProc.class.isAssignableFrom(declClass) ||
                DeclFunc.class.isAssignableFrom(declClass)) {
                return (Map<String, D>) ids;
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

    // Operator class corresponds to operators (+, -, etc) and system provided functions (substr, trim, strcomp, etc)
    // which can be overloaded unlike user defined procedures and functions.
    // It implements DeclId in order to be inserted ids map for system provided functions.
    private static class Operator extends DeclBase implements DeclId {

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
