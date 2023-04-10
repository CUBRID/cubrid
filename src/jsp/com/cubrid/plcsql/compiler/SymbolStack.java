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
import com.cubrid.plcsql.compiler.visitor.AstVisitor;
import com.cubrid.plcsql.compiler.annotation.Operator;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
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

    private static final Map<String, FuncOverloads> operators = new HashMap<>();
    private static final Map<String, FuncOverloads> cubridFuncs = new HashMap<>();
    private static SymbolTable predefinedSymbols =
            new SymbolTable(new Scope(null, null, "%predefined_0", 0));

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
            for (Method m : methods) {
                if ((m.getModifiers() & Modifier.STATIC) > 0) {
                    String name = m.getName();
                    if (name.startsWith("op")) {
                        // System.out.println("temp: " + m.getName());

                        Operator opAnnot = m.getAnnotation(Operator.class);
                        assert opAnnot != null;

                        // parameter types
                        Class[] paramTypes = m.getParameterTypes();
                        NodeList<DeclParam> params = new NodeList<>();

                        int i = 0;
                        for (Class pt : paramTypes) {
                            String typeName = pt.getTypeName();
                            // System.out.println("  " + typeName);
                            TypeSpec paramType = TypeSpec.ofJavaName(typeName);
                            assert paramType != null;

                            DeclParamIn p = new DeclParamIn(null, "p" + i, paramType);
                            params.addNode(p);
                            i++;
                        }

                        // return type
                        String typeName = m.getReturnType().getTypeName();
                        // System.out.println("  =>" + typeName);
                        TypeSpec retType = TypeSpec.ofJavaName(typeName);
                        assert retType != null;

                        // add op
                        DeclFunc op = new DeclFunc(null, name, params, retType);
                        putOperator(name, op, opAnnot.coercionScheme());
                    }
                }
            }

            // add exceptions
            DeclException de;
            for (String s : predefinedExceptions) {
                de = new DeclException(null, s);
                putDeclTo(predefinedSymbols, de.name, de);
            }

            // add procedures
            DeclProc dp =
                    new DeclProc(
                            null,
                            "PUT_LINE",
                            new NodeList<DeclParam>()
                                    .addNode(new DeclParamIn(null, "s", TypeSpecSimple.STRING)),
                            null,
                            null);
            putDeclTo(predefinedSymbols, "PUT_LINE", dp);

            // add constants TODO implement SQLERRM and SQLCODE properly
            DeclConst dc =
                    new DeclConst(
                            null, "SQLERRM", TypeSpecSimple.STRING, false, new ExprNull(null));
            putDeclTo(predefinedSymbols, "SQLERRM", dc);

            dc = new DeclConst(null, "SQLCODE", TypeSpecSimple.INT, false, new ExprNull(null));
            putDeclTo(predefinedSymbols, "SQLCODE", dc);

            dc = new DeclConst(null, "SYSDATE", TypeSpecSimple.DATE, false, new ExprNull(null));
            putDeclTo(predefinedSymbols, "SYSDATE", dc);
        }
    }

    public static DeclFunc
    getOperator(List<Coerce> outCoercions, String name, int lineNoOfCall, TypeSpec... argTypes) {
        return getFuncOverload(outCoercions, operators, name, lineNoOfCall, argTypes);
    }

    /*
    public static DeclFunc
    getCubridFunc(List<Coerce> outCoercions, String name, int lineNoOfCall, TypeSpec... argTypes) {
        return getFuncOverload(outCoercions, cubridFuncs, name, lineNoOfCall, argTypes);
    }
     */

    // -----------------------------------------------------------------------------
    // end of Static
    //

    SymbolStack() {
        symbolTableStack.addFirst(predefinedSymbols);
        currSymbolTable =
                new SymbolTable(
                        new Scope(null, null, "unit_1", 1)); // for the main procedure/function
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
                throw new SemanticError(decl.lineNo(),  // s061
                    "label " + name + " has already been declared in the same scope");
            }
        } else {
            if (symbolTable.ids.containsKey(name)
                    || symbolTable.procs.containsKey(name)
                    || symbolTable.funcs.containsKey(name)
                    || symbolTable.exceptions.containsKey(name)) {
                throw new SemanticError(decl.lineNo(),  // s062
                    name + " has already been declared in the same scope");
            }
            if (symbolTable.scope.level == 1 && map.size() == 0) {
                // the first symbol added to the level 1 is the top-level procedure/function being created or replaced

                assert map == symbolTable.procs || map == symbolTable.funcs;    // top-level procedure/function
                if (cubridFuncs.containsKey(name)) {
                    throw new SemanticError(decl.lineNo(),  // s063
                        "procedure/function cannot be created with the same name as a built-in function");
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

    DeclVar getDeclVar(String name) {
        return getDecl(DeclVar.class, name);
    }

    DeclFunc getDeclFunc(String name) {
        DeclFunc ret = getDecl(DeclFunc.class, name);
        return ret;
        /* TODO: restore
        if (ret == null) {
            // search the predefined functions too
            FuncOverloads overloaded = cubridFuncs.get(name);
            if (overloaded == null) {
                return null;
            } else {
                assert overloaded.overloads.size() == 1
                        : "getting an overloaded function "
                                + name
                                + " only by its name is ambiguous";
                return overloaded.overloads.values().iterator().next();
            }
        } else {
            return ret;
        }
         */
    }

    DeclFunc getDeclFunc(String name, List<TypeSpec> argTypes) {
        DeclFunc ret = getDecl(DeclFunc.class, name);
        return ret;
        /* TODO: restore
        if (ret == null) {
            // search the predefined functions too
            FuncOverloads overloaded = cubridFuncs.get(name);
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
         */
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
        DeclFunc declFunc = getDeclFunc(name, EMPTY_TYPE_SPEC); // one with no parameters

        if (declFunc == null) {
            return declId;
        } else if (declId == null) {
            return declFunc;
        } else {
            if (declId.scope().level > declFunc.scope().level) {
                return declId;
            } else {
                assert declId.scope().level
                        < declFunc.scope().level; // they cannot be on the same level
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

    private static void putOperator(String name, DeclFunc df, CoercionScheme cs) {
        putFuncOverload(operators, name, df, cs);
    }

    /*
    private static void putCubridFunc(String name, DeclFunc df) {
        putFuncOverload(cubridFuncs, name, df, CoercionScheme.Individual);
    }
     */

    private static DeclFunc getFuncOverload(List<Coerce> outCoercions,
            Map<String, FuncOverloads> map, String name, int lineNoOfCall, TypeSpec... argTypes) {
        FuncOverloads overloads = map.get(name);
        if (overloads == null) {
            return null;
        } else {
            return overloads.get(outCoercions, Arrays.asList(argTypes), lineNoOfCall);
        }
    }

    private static void putFuncOverload(
            Map<String, FuncOverloads> map, String name, DeclFunc df, CoercionScheme cs) {

        FuncOverloads overloads = map.get(name);
        if (overloads == null) {
            overloads = new FuncOverloads(name, cs);
            map.put(name, overloads);
        } else {
            assert overloads.coercionScheme == cs;
        }

        df.setScope(predefinedSymbols.scope);
        overloads.put(df);
    }

    private static class SymbolTable {
        final Scope scope;

        final Map<String, DeclId> ids = new HashMap<>();
        final Map<String, DeclProc> procs = new HashMap<>();
        final Map<String, DeclFunc> funcs = new HashMap<>();
        final Map<String, DeclException> exceptions = new HashMap<>();
        final Map<String, DeclLabel> labels = new HashMap<>();

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

    // FuncOverloads class corresponds to operators (+, -, etc) and system provided functions
    // (substr, trim, etc) which can be overloaded for argument types unlike user defined procedures and functions.
    private static class FuncOverloads {

        FuncOverloads(String name, CoercionScheme cs) {
            this.name = name;
            this.coercionScheme = cs;
        }

        void put(DeclFunc decl) {
            List<TypeSpec> paramTypes =
                    decl.paramList.nodes.stream()
                            .map(e -> e.typeSpec())
                            .collect(Collectors.toList());
            DeclFunc old = overloads.put(paramTypes, decl);
            // system predefined operators and functions must be unique with their names and
            // argument types.
            assert old == null;
        }

        DeclFunc get(List<Coerce> outCoercions, List<TypeSpec> argTypes, int lineNoOfCall) {

            List<TypeSpec> paramTypes = coercionScheme.getCoercions(outCoercions, argTypes, name);
            if (paramTypes == null) {
                throw new SemanticError(lineNoOfCall,
                    "argument types are not compatible in the call of function " + name);
            } else {
                assert argTypes.size() == outCoercions.size();
                DeclFunc declFunc = overloads.get(paramTypes);
                if (name.equals("opIn")) {
                    // opIn is the only operation that uses variadic parameters
                    TypeSpec ty = paramTypes.get(0);
                    paramTypes.clear();
                    paramTypes.add(ty);
                    paramTypes.add(new TypeSpecVariadic((TypeSpecSimple) ty));
                }

                declFunc = overloads.get(paramTypes);
                assert declFunc != null : paramTypes + " do not have a matching version of op " + name;
                return declFunc;
            }
        }

        // ---------------------------------------------------------
        //
        // ---------------------------------------------------------

        private final Map<List<TypeSpec>, DeclFunc> overloads =
                new HashMap<>(); // (arg types --> func decl) map
        private final CoercionScheme coercionScheme;
        private final String name;
    }
}
