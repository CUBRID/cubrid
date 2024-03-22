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

import static com.cubrid.plcsql.compiler.antlrgen.PlcParser.*;

import com.cubrid.plcsql.compiler.annotation.Operator;
import com.cubrid.plcsql.compiler.ast.*;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

public class SymbolStack {

    public static final List<String> noParenBuiltInFunc =
            Arrays.asList(
                    "SYS_DATE",
                    "SYS_DATETIME",
                    "SYS_TIME",
                    "SYS_TIMESTAMP",
                    "SYSDATE",
                    "SYSDATETIME",
                    "SYSTIME",
                    "SYSTIMESTAMP");

    public static final int LEVEL_PREDEFINED = 0;
    public static final int LEVEL_MAIN = 1;

    // -------------------------------------------------------
    // Static area - common to all symbol stack instances
    //

    private static final Map<String, FuncOverloads> operators = new HashMap<>();
    private static SymbolTable predefinedSymbols =
            new SymbolTable(new Scope(null, null, "%predefined_0", LEVEL_PREDEFINED));

    private static void addOperatorDecls() {

        // add SpLib staic methods corresponding to operators

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
                    if (opAnnot != null) {

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
        }
    }

    private static void addDbmsOutputProcedures() {

        DeclProc dp;

        // disable
        dp = new DeclProc(null, "DBMS_OUTPUT$DISABLE", new NodeList<DeclParam>());
        putDeclTo(predefinedSymbols, "DBMS_OUTPUT$DISABLE", dp);

        // enable
        dp =
                new DeclProc(
                        null,
                        "DBMS_OUTPUT$ENABLE",
                        new NodeList<DeclParam>()
                                .addNode(new DeclParamIn(null, "size", TypeSpecSimple.INT)));
        putDeclTo(predefinedSymbols, "DBMS_OUTPUT$ENABLE", dp);

        // get_line
        dp =
                new DeclProc(
                        null,
                        "DBMS_OUTPUT$GET_LINE",
                        new NodeList<DeclParam>()
                                .addNode(
                                        new DeclParamOut(
                                                null, "line", TypeSpecSimple.STRING_ANY, false))
                                .addNode(
                                        new DeclParamOut(
                                                null, "status", TypeSpecSimple.INT, true)));
        putDeclTo(predefinedSymbols, "DBMS_OUTPUT$GET_LINE", dp);

        // new_line
        dp = new DeclProc(null, "DBMS_OUTPUT$NEW_LINE", new NodeList<DeclParam>());
        putDeclTo(predefinedSymbols, "DBMS_OUTPUT$NEW_LINE", dp);

        // put_line
        dp =
                new DeclProc(
                        null,
                        "DBMS_OUTPUT$PUT_LINE",
                        new NodeList<DeclParam>()
                                .addNode(new DeclParamIn(null, "s", TypeSpecSimple.STRING_ANY)));
        putDeclTo(predefinedSymbols, "DBMS_OUTPUT$PUT_LINE", dp);

        // put
        dp =
                new DeclProc(
                        null,
                        "DBMS_OUTPUT$PUT",
                        new NodeList<DeclParam>()
                                .addNode(new DeclParamIn(null, "s", TypeSpecSimple.STRING_ANY)));
        putDeclTo(predefinedSymbols, "DBMS_OUTPUT$PUT", dp);
    }

    private static void addBuiltinFunctions() {

        List<String> funcNames =
                Arrays.asList(

                        // bit
                        "BIT_AND",
                        "BIT_COUNT",
                        "BIT_OR",
                        "BIT_XOR",

                        // string
                        "ASCII",
                        "BIN",
                        "BIT_LENGTH",
                        "CHAR_LENGTH",
                        "CHARACTER_LENGTH",
                        "LENGTHB",
                        "LENGTH",
                        "CHR",
                        "CONCAT",
                        "CONCAT_WS",
                        "ELT",
                        "FIELD",
                        "FIND_IN_SET",
                        "FROM_BASE64",
                        "INSERT",
                        "INSTR",
                        "LCASE",
                        "LOWER",
                        "LEFT",
                        "LOCATE",
                        "LPAD",
                        "LTRIM",
                        "MID",
                        "OCTET_LENGTH",
                        "POSITION",
                        "REPEAT",
                        "REPLACE",
                        "REVERSE",
                        "RIGHT",
                        "RPAD",
                        "RTRIM",
                        "SPACE",
                        "STRCMP",
                        "SUBSTR",
                        "SUBSTRB", // not in the user manual
                        "SUBSTRING",
                        "SUBSTRING_INDEX",
                        "TO_BASE64",
                        "TRANSLATE",
                        "TRIM",
                        "UCASE",
                        "UPPER",

                        // regular expression
                        "REGEXP_COUNT",
                        "REGEXP_INSTR",
                        "REGEXP_LIKE",
                        "REGEXP_REPLACE",
                        "REGEXP_SUBSTR",

                        // numeric/mathematical
                        "ABS",
                        "ACOS",
                        "ASIN",
                        "ATAN",
                        "ATAN2",
                        "CEIL",
                        "CONV",
                        "COS",
                        "COT",
                        "CRC32",
                        "DEGREES",
                        "DRANDOM",
                        "DRAND",
                        "EXP",
                        "FLOOR",
                        "HEX",
                        "LN",
                        "LOG2",
                        "LOG10",
                        "MOD",
                        "PI",
                        "POW",
                        "POWER",
                        "RADIANS",
                        "RANDOM",
                        "RAND",
                        "ROUND",
                        "SIGN",
                        "SIN",
                        "SQRT",
                        "TAN",
                        "TRUNC",
                        "TRUNCATE",
                        "WIDTH_BUCKET",

                        // date/time
                        "ADD_MONTHS",
                        "ADDDATE",
                        "ADDTIME",
                        "CURDATE",
                        "CURRENT_DATE",
                        "CURRENT_DATETIME",
                        "CURRENT_TIME",
                        "CURRENT_TIMESTAMP",
                        "CURTIME",
                        "DATE",
                        "DATE_ADD",
                        "DATE_SUB",
                        "DATEDIFF",
                        "DAY",
                        "DAYOFMONTH",
                        "DAYOFWEEK",
                        "DAYOFYEAR",
                        "EXTRACT",
                        "FROM_DAYS",
                        "FROM_TZ",
                        "FROM_UNIXTIME",
                        "HOUR",
                        "LAST_DAY",
                        "LOCALTIME",
                        "LOCALTIMESTAMP",
                        "MAKEDATE",
                        "MAKETIME",
                        "MINUTE",
                        "MONTH",
                        "MONTHS_BETWEEN",
                        "NEW_TIME",
                        "NOW",
                        "QUARTER", /* "ROUND", dup */
                        "SEC_TO_TIME",
                        "SECOND",
                        "SUBDATE",
                        "SYS_DATE",
                        "SYS_DATETIME",
                        "SYS_TIME",
                        "SYS_TIMESTAMP",
                        "SYSDATE",
                        "SYSDATETIME",
                        "SYSTIME",
                        "SYSTIMESTAMP",
                        "TIME",
                        "TIME_TO_SEC",
                        "TIMEDIFF",
                        "TIMESTAMP",
                        "TO_DAYS", /* "TRUNC", dup */
                        "TZ_OFFSET",
                        "UNIX_TIMESTAMP",
                        "UTC_DATE",
                        "UTC_TIME",
                        "WEEK",
                        "WEEKDAY",
                        "YEAR",

                        // json
                        "JSON_ARRAY",
                        "JSON_ARRAY_APPEND",
                        "JSON_ARRAY_INSERT",
                        "JSON_CONTAINS",
                        "JSON_CONTAINS_PATH",
                        "JSON_DEPTH",
                        "JSON_EXTRACT",
                        "JSON_INSERT",
                        "JSON_KEYS",
                        "JSON_LENGTH",
                        "JSON_MERGE",
                        "JSON_MERGE_PATCH",
                        "JSON_MERGE_PRESERVE",
                        "JSON_OBJECT",
                        "JSON_PRETTY",
                        "JSON_QUOTE",
                        "JSON_REMOVE",
                        "JSON_REPLACE",
                        "JSON_SEARCH",
                        "JSON_SET",
                        "JSON_TABLE",
                        "JSON_TYPE",
                        "JSON_UNQUOTE",
                        "JSON_VALID",

                        // lob
                        "BIT_TO_BLOB",
                        "BLOB_FROM_FILE",
                        "BLOB_LENGTH",
                        "BLOB_TO_BIT",
                        "CHAR_TO_BLOB",
                        "CHAR_TO_CLOB",
                        "CLOB_FROM_FILE",
                        "CLOB_LENGTH",
                        "CLOB_TO_CHAR",

                        // data type casting
                        "CAST",
                        "DATE_FORMAT",
                        "FORMAT",
                        "STR_TO_DATE",
                        "TIME_FORMAT",
                        "TO_CHAR",
                        "TO_DATE",
                        "TO_DATETIME",
                        "TO_DATETIME_TZ",
                        "TO_NUMBER",
                        "TO_TIME",
                        "TO_TIMESTAMP",
                        "TO_TIMESTAMP_TZ",

                        // aggregate and analytic
                        "AVG",
                        "COUNT",
                        "CUME_DIST",
                        "DENSE_RANK",
                        "FIRST_VALUE",
                        "GROUP_CONCAT",
                        "JSON_ARRAYAGG",
                        "JSON_OBJECTAGG",
                        "LAG",
                        "LAST_VALUE",
                        "LEAD",
                        "MAX",
                        "MEDIAN",
                        "MIN",
                        "NTH_VALUE",
                        "NTILE",
                        "PERCENTILE_CONT",
                        "PERCENTILE_DISC",
                        "PERCENT_RANK",
                        "RANK",
                        "ROW_NUMBER",
                        "STDDEV",
                        "STDDEV_POP",
                        "STDDEV_SAMP",
                        "SUM",
                        "VARIANCE",
                        "VAR_POP",
                        "VAR_SAMP",

                        // click counter
                        "DECR",
                        "INCR",

                        // rownum
                        "GROUPBY_NUM",
                        "INST_NUM",
                        "ORDERBY_NUM",
                        "ROWNUM",

                        // information
                        "CHARSET",
                        "COERCIBILITY",
                        "COLLATION",
                        "CURRENT_USER",
                        "DATABASE",
                        "DBTIMEZONE",
                        "DEFAULT",
                        "DISK_SIZE",
                        "INDEX_CARDINALITY",
                        "INET_ATON",
                        "INET_NTOA",
                        "LAST_INSERT_ID",
                        "LIST_DBS",
                        "ROW_COUNT",
                        "SCHEMA",
                        "SESSIONTIMEZONE",
                        "SYSTEM_USER",
                        "USER",
                        "VERSION",

                        // encryption
                        "MD5",
                        "SHA1",
                        "SHA2",

                        // comparison
                        "COALESCE",
                        "DECODE",
                        "GREATEST",
                        "IF",
                        "IFNULL",
                        "ISNULL",
                        "LEAST",
                        "NULLIF",
                        "NVL",
                        "NVL2",

                        // others
                        "SLEEP",
                        "SYS_GUID");

        for (String s : funcNames) {
            DeclFunc df =
                    new DeclFunc(null, s, null, null); // only name is used for builtin functions
            putDeclTo(predefinedSymbols, df.name, df);
        }
    }

    private static void addPredefinedExceptions() {

        List<String> predefinedExceptions =
                Arrays.asList(
                        "$APP_ERROR", // for raise_application_error
                        "CASE_NOT_FOUND",
                        "CURSOR_ALREADY_OPEN",
                        "INVALID_CURSOR",
                        "NO_DATA_FOUND",
                        "PROGRAM_ERROR",
                        "STORAGE_ERROR",
                        "SQL_ERROR",
                        "TOO_MANY_ROWS",
                        "VALUE_ERROR",
                        "ZERO_DIVIDE");

        for (String s : predefinedExceptions) {
            DeclException de = new DeclException(null, s);
            putDeclTo(predefinedSymbols, de.name, de);
        }
    }

    static {
        addOperatorDecls();
        addDbmsOutputProcedures();
        addBuiltinFunctions();
        addPredefinedExceptions();
    }

    public static DeclFunc getOperator(
            List<Coercion> outCoercions, String name, TypeSpec... argTypes) {
        return getFuncOverload(outCoercions, operators, name, argTypes);
    }

    //
    // end of Static
    // -----------------------------------------------------------------------------

    SymbolStack() {
        symbolTableStack.addFirst(predefinedSymbols);
        currSymbolTable =
                new SymbolTable(
                        new Scope(
                                null,
                                null,
                                "unit_1",
                                LEVEL_MAIN)); // for the main procedure/function
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

    void putDeclLabel(String name, DeclLabel decl) {
        if (getDeclLabel(name) != null) {
            throw new SemanticError(
                    Misc.getLineColumnOf(decl.ctx), // s061
                    "label " + name + " has already been declared");
        }

        decl.setScope(currSymbolTable.scope);
        currSymbolTable.labels.put(name, decl);
    }

    void putDecl(String name, Decl decl) {
        assert !(decl instanceof DeclLabel);
        putDeclTo(currSymbolTable, name, decl);
    }

    private static void putDeclTo(SymbolTable symbolTable, String name, Decl decl) {
        assert decl != null;

        Map<String, Decl> map = symbolTable.decls;

        if (map.containsKey(name)) {
            throw new SemanticError(
                    Misc.getLineColumnOf(decl.ctx), // s062
                    name + " has already been declared in the same scope");
        }
        if (symbolTable.scope.level == 1 && map.size() == 0) {
            // the first symbol added to the level 1 is the top-level procedure/function being
            // created or replaced

            assert decl instanceof DeclRoutine; // top-level procedure/function
            if (predefinedSymbols.decls.containsKey(name)) {
                throw new SemanticError(
                        Misc.getLineColumnOf(decl.ctx), // s063
                        "procedure/function cannot be created with the same name as a built-in function");
            }
        }

        decl.setScope(symbolTable.scope);
        map.put(name, decl);
    }

    DeclId getDeclId(String name) {
        Decl d = getDecl(name);
        if (d instanceof DeclId) {
            return (DeclId) d;
        } else {
            if (d == null) {
                return null;
            } else {
                throw new SemanticError(
                        Misc.getLineColumnOf(d.ctx), // s071
                        name + " is not an identifier but " + d.kind() + " in this scope");
            }
        }
    }

    DeclProc getDeclProc(String name) {
        Decl d = getDecl(name);
        if (d instanceof DeclProc) {
            return (DeclProc) d;
        } else {
            if (d == null) {
                return null;
            } else {
                throw new SemanticError(
                        Misc.getLineColumnOf(d.ctx), // s072
                        name + " is not a procedure but " + d.kind() + " in this scope");
            }
        }
    }

    DeclFunc getDeclFunc(String name) {
        Decl d = getDecl(name);
        if (d instanceof DeclFunc) {
            return (DeclFunc) d;
        } else {
            if (d == null) {
                return null;
            } else {
                throw new SemanticError(
                        Misc.getLineColumnOf(d.ctx), // s073
                        name + " is not a function but " + d.kind() + " in this scope");
            }
        }
    }

    DeclException getDeclException(String name) {
        Decl d = getDecl(name);
        if (d instanceof DeclException) {
            return (DeclException) d;
        } else {
            if (d == null) {
                return null;
            } else {
                throw new SemanticError(
                        Misc.getLineColumnOf(d.ctx), // s074
                        name + " is not an exception but " + d.kind() + " in this scope");
            }
        }
    }

    DeclLabel getDeclLabel(String name) {
        assert name != null;

        for (SymbolTable t : symbolTableStack) {

            if (t.labels.containsKey(name)) {
                return t.labels.get(name);
            }
        }

        return null;
    }

    // return DeclId or DeclFunc for an identifier expression
    Decl getDeclForIdExpr(String name) {
        Decl d = getDecl(name);
        if (d instanceof DeclId || d instanceof DeclFunc) {
            return d;
        } else {
            if (d == null) {
                return null;
            } else {
                throw new SemanticError(
                        Misc.getLineColumnOf(d.ctx), // s075
                        name
                                + " is neither an identifier nor a function but "
                                + d.kind()
                                + " in this scope");
            }
        }
    }

    // ----------------------------------------------------
    // Private
    // ----------------------------------------------------

    private SymbolTable currSymbolTable;

    private LinkedList<SymbolTable> symbolTableStack = new LinkedList<>();

    private static void putOperator(String name, DeclFunc df, CoercionScheme cs) {
        putFuncOverload(operators, name, df, cs);
    }

    private static DeclFunc getFuncOverload(
            List<Coercion> outCoercions,
            Map<String, FuncOverloads> map,
            String name,
            TypeSpec... argTypes) {

        FuncOverloads overloads = map.get(name);
        if (overloads == null) {
            return null; // TODO: throw?
        } else {
            return overloads.get(outCoercions, Arrays.asList(argTypes));
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
        final Map<String, Decl> decls = new HashMap<>();
        final Map<String, DeclLabel> labels = new HashMap<>();

        SymbolTable(Scope scope) {
            this.scope = scope;
        }
    }

    private Decl getDecl(String name) {
        assert name != null;

        for (SymbolTable t : symbolTableStack) {

            if (t.decls.containsKey(name)) {
                return t.decls.get(name);
            }
        }

        return null;
    }

    // FuncOverloads class corresponds to operators (+, -, etc) and system provided functions
    // (substr, trim, etc) which can be overloaded for argument types unlike user defined procedures
    // and functions.
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

        DeclFunc get(List<Coercion> outCoercions, List<TypeSpec> argTypes) {

            List<TypeSpec> paramTypes = coercionScheme.getCoercions(outCoercions, argTypes, name);
            if (paramTypes == null) {
                return null; // no match
            } else {
                assert argTypes.size() == outCoercions.size();
                if (name.equals("opIn")) {
                    // opIn is the only operation that uses variadic parameters
                    TypeSpec ty = paramTypes.get(0);
                    paramTypes.clear();
                    paramTypes.add(ty);
                    paramTypes.add(new TypeSpecVariadic((TypeSpecSimple) ty));
                }

                DeclFunc declFunc = overloads.get(paramTypes);
                assert declFunc != null
                        : paramTypes + " do not have a matching version of op " + name;
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
