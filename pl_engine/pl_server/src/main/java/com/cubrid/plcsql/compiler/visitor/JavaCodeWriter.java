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

package com.cubrid.plcsql.compiler.visitor;

import com.cubrid.plcsql.compiler.Coercion;
import com.cubrid.plcsql.compiler.InstanceStore;
import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.ast.*;
import com.cubrid.plcsql.compiler.type.Type;
import com.cubrid.plcsql.compiler.type.TypeRecord;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Set;
import org.antlr.v4.runtime.ParserRuleContext;

public class JavaCodeWriter extends AstVisitor<JavaCodeWriter.CodeToResolve> {

    private InstanceStore iStore;
    private Set<String> javaTypesUsed = new HashSet<>();

    private String getJavaCodeOfType(TypeSpec tySpec) {
        return getJavaCodeOfType(tySpec.type);
    }

    private String getJavaCodeOfType(Type type) {
        javaTypesUsed.add(type.fullJavaType);
        return type.javaCode;
    }

    public JavaCodeWriter(InstanceStore iStore) {
        this.iStore = iStore;
    }

    public List<String> codeLines = new ArrayList<>(); // no LinkedList : frequent access by indexes
    public StringBuilder codeRangeMarkers = new StringBuilder();

    public String buildCodeLines(Unit unit) {

        javaTypesUsed.add("com.cubrid.jsp.Server");
        javaTypesUsed.add("com.cubrid.plcsql.predefined.PlcsqlRuntimeError");
        javaTypesUsed.add("java.util.List");

        CodeToResolve ctr = visitUnit(unit);
        ctr.resolve(0, codeLines, codeRangeMarkers);

        codeLines.add(
                "  private static List<CodeRangeMarker> codeRangeMarkerList = buildCodeRangeMarkerList(\""
                        + codeRangeMarkers
                        + "\");");
        codeLines.add("}");

        return String.join("\n", codeLines);
    }

    // -----------------------------------------------------------------

    @Override
    public <E extends AstNode> CodeTemplateList visitNodeList(NodeList<E> nodeList) {
        CodeTemplateList list = new CodeTemplateList();
        for (E e : nodeList.nodes) {
            list.addElement((CodeTemplate) visit(e));
        }
        return list;
    }

    // -----------------------------------------------------------------
    // Unit
    //
    private static final String tmplGetConn =
            "Connection conn = DriverManager.getConnection(\"jdbc:default:connection::?autonomous_transaction=%s\");";
    private static final String[] tmplUnit =
            new String[] {
                "%'+IMPORTS'%",
                "import static com.cubrid.plcsql.predefined.sp.SpLib.*;",
                "",
                "public class %'CLASS-NAME'% {",
                "",
                "  public static %'RETURN-TYPE'% %'METHOD-NAME'%(",
                "      %'+PARAMETERS'%",
                "    ) throws Exception {",
                "    %'+NULLIFY-OUT-PARAMETERS'%",
                "    try {",
                "      Long[] sql_rowcount = new Long[] { null };",
                "      %'GET-CONNECTION'%",
                "      %'+DECL-CLASS'%",
                "      %'+BODY'%",
                // exceptions that escaped from the exception handlers of the body
                "    } catch (PlcsqlRuntimeError e) {",
                "      Throwable c = e.getCause();",
                "      int[] pos = getPlcLineColumn(codeRangeMarkerList, c == null ? e : c, \"%'CLASS-NAME'%.java\");",
                "      throw e.setPlcLineColumn(pos);",
                // exceptions raised in an exception handler
                "    } catch (OutOfMemoryError e) {",
                "      Server.log(e);",
                "      int[] pos = getPlcLineColumn(codeRangeMarkerList, e, \"%'CLASS-NAME'%.java\");",
                "      throw new STORAGE_ERROR().setPlcLineColumn(pos);",
                "    } catch (Throwable e) {",
                "      Server.log(e);",
                "      int[] pos = getPlcLineColumn(codeRangeMarkerList, e, \"%'CLASS-NAME'%.java\");",
                "      throw new PROGRAM_ERROR().setPlcLineColumn(pos);",
                "    }",
                "  }",
                "  %'+RECORD-DEFS'%",
                "  %'+RECORD-ASSIGN-FUNCS'%"
            };

    @Override
    public CodeToResolve visitUnit(Unit node) {

        if (node.connectionRequired) {
            javaTypesUsed.add("java.sql.*");
        }

        // get connection, if necessary
        String strGetConn =
                node.connectionRequired
                        ? String.format(tmplGetConn, node.autonomousTransaction)
                        : "";

        // declarations
        Object codeDeclClass =
                node.routine.decls == null
                        ? ""
                        : new CodeTemplate(
                                "DeclClass of Unit",
                                Misc.UNKNOWN_LINE_COLUMN,
                                tmplDeclBlock,
                                "%'BLOCK'%",
                                node.routine.getDeclBlockName(),
                                "%'+DECLARATIONS'%",
                                visitNodeList(node.routine.decls));

        // return type
        String strRetType =
                node.routine.retTypeSpec == null
                        ? "void"
                        : getJavaCodeOfType(node.routine.retTypeSpec);

        // parameters
        Object strParamArr =
                Misc.isEmpty(node.routine.paramList)
                        ? ""
                        : visitNodeList(node.routine.paramList).setDelimiter(",");

        // nullify OUT parameters
        String[] strNullifyOutParam = getNullifyOutParamCode(node.routine.paramList);

        // body
        CodeToResolve bodyCode = visit(node.routine.body);

        // record definitions
        List<String> recordLines = new LinkedList<>();
        for (TypeRecord rec : iStore.typeRecord.values()) {
            recordLines.addAll(getRecordDeclCode(rec));
        }
        String[] recordDefs = recordLines.toArray(DUMMY_STRING_ARRAY);

        // add all Java code of record-to-record coercion functions
        recordLines.clear();
        recordLines.addAll(Coercion.RecordToRecord.getAllJavaCode(iStore));
        String[] recordAssignFuncs = recordLines.toArray(DUMMY_STRING_ARRAY);

        // imports
        // CAUTION: importsArray must be made after visiting all the subnodes of this Unit node
        // because javaTypesUsed,
        //  which is the set of Java types to appear in the generated Java code, is built while
        // visiting the submodes
        int i = 0;
        String[] importsArray = new String[javaTypesUsed.size()];
        for (String javaType : javaTypesUsed) {
            if ("com.cubrid.plcsql.predefined.sp.SpLib.Query".equals(javaType)) {
                // no need to import Cursor now
            } else if (javaType.startsWith("java.lang.") && javaType.lastIndexOf('.') == 9) {
                // no need to import java.lang.*;
            } else if (javaType.startsWith("Null")) {
                // NULL type is not a java type but an internal type for convenience in
                // typechecking.
            } else if (javaType.startsWith("$Record_")) {
                // no need to import record types: they are defined in the generated Java code
            } else {
                importsArray[i] = "import " + javaType + ";";
                i++;
            }
        }
        importsArray = Arrays.copyOf(importsArray, i);

        return new CodeTemplate(
                "Unit",
                new int[] {1, 1},
                tmplUnit,
                "%'+IMPORTS'%",
                importsArray,
                "%'CLASS-NAME'%",
                node.getClassName(),
                "%'RETURN-TYPE'%",
                strRetType,
                "%'METHOD-NAME'%",
                node.routine.name,
                "%'+PARAMETERS'%",
                strParamArr,
                "%'+NULLIFY-OUT-PARAMETERS'%",
                strNullifyOutParam,
                "%'GET-CONNECTION'%",
                strGetConn,
                "%'+DECL-CLASS'%",
                codeDeclClass,
                "%'+BODY'%",
                bodyCode,
                "%'+RECORD-DEFS'%",
                recordDefs,
                "%'+RECORD-ASSIGN-FUNCS'%",
                recordAssignFuncs);
    }

    // -----------------------------------------------------------------
    // Routine (Procedure, Function)
    //

    private static final String[] tmplDeclRoutine =
            new String[] {
                "%'RETURN-TYPE'% %'METHOD-NAME'%(",
                "    %'+PARAMETERS'%",
                "  ) throws Exception {",
                "  %'+NULLIFY-OUT-PARAMETERS'%",
                "  %'+DECL-CLASS'%",
                "  %'+BODY'%",
                "}"
            };

    private CodeToResolve visitDeclRoutine(DeclRoutine node) {

        assert node.paramList != null;

        // declarations
        Object codeDeclClass =
                node.decls == null
                        ? ""
                        : new CodeTemplate(
                                "DeclClass of Routine",
                                Misc.UNKNOWN_LINE_COLUMN,
                                tmplDeclBlock,
                                "%'BLOCK'%",
                                node.getDeclBlockName(),
                                "%'+DECLARATIONS'%",
                                visitNodeList(node.decls));

        String[] strNullifyOutParam = getNullifyOutParamCode(node.paramList);

        return new CodeTemplate(
                "DeclRoutine",
                Misc.UNKNOWN_LINE_COLUMN,
                tmplDeclRoutine,
                "%'RETURN-TYPE'%",
                node.retTypeSpec == null ? "void" : getJavaCodeOfType(node.retTypeSpec),
                "%'+PARAMETERS'%",
                visitNodeList(node.paramList).setDelimiter(","),
                "%'+NULLIFY-OUT-PARAMETERS'%",
                strNullifyOutParam,
                "%'+DECL-CLASS'%",
                codeDeclClass,
                "%'+BODY'%",
                visitBody(node.body),
                "%'METHOD-NAME'%",
                node.name);
    }

    @Override
    public CodeToResolve visitDeclFunc(DeclFunc node) {
        return visitDeclRoutine(node);
    }

    @Override
    public CodeToResolve visitDeclProc(DeclProc node) {
        return visitDeclRoutine(node);
    }

    @Override
    public CodeToResolve visitDeclParamIn(DeclParamIn node) {
        String code = String.format("%s %s", getJavaCodeOfType(node.typeSpec), node.name);
        return new CodeTemplate("DeclParamIn", Misc.UNKNOWN_LINE_COLUMN, code);
    }

    @Override
    public CodeToResolve visitDeclParamOut(DeclParamOut node) {
        String code = String.format("%s[] %s", getJavaCodeOfType(node.typeSpec), node.name);
        return new CodeTemplate("DeclParamOut", Misc.UNKNOWN_LINE_COLUMN, code);
    }

    // -----------------------------------------------------------------
    // DeclVar
    //

    private static final String[] tmplNotNullVar =
            new String[] {
                "%'TYPE'%[] %'NAME'% = new %'TYPE'%[] { checkNotNull(",
                "  %'+VAL'%, \"NOT NULL constraint violated\") };"
            };
    private static final String[] tmplNullableVar =
            new String[] {"%'TYPE'%[] %'NAME'% = new %'TYPE'%[] {", "  %'+VAL'%", "};"};

    @Override
    public CodeToResolve visitDeclVar(DeclVar node) {

        Type ty = node.typeSpec.type;
        String tyJava = getJavaCodeOfType(ty);
        if (node.val == null) {

            String code;
            if (ty instanceof TypeRecord) {
                code = String.format("%1$s[] %2$s = new %1$s[] { new %1$s() };", tyJava, node.name);
            } else {
                code = String.format("%1$s[] %2$s = new %1$s[] { null };", tyJava, node.name);
            }
            return new CodeTemplate("DeclVar", Misc.UNKNOWN_LINE_COLUMN, code);
        } else {
            return new CodeTemplate(
                    "DeclVar",
                    node.notNull ? Misc.getLineColumnOf(node.ctx) : Misc.UNKNOWN_LINE_COLUMN,
                    node.notNull ? tmplNotNullVar : tmplNullableVar,
                    "%'TYPE'%",
                    tyJava,
                    "%'NAME'%",
                    node.name,
                    "%'+VAL'%",
                    visit(node.val));
        }
    }

    // -----------------------------------------------------------------
    // DeclConst
    //

    private static final String[] tmplNotNullConst =
            new String[] {
                "final %'TYPE'% %'NAME'% = checkNotNull(",
                "  %'+VAL'%, \"NOT NULL constraint violated\");"
            };
    private static final String[] tmplNullableConst =
            new String[] {"final %'TYPE'% %'NAME'% =", "  %'+VAL'%;"};

    @Override
    public CodeToResolve visitDeclConst(DeclConst node) {

        return new CodeTemplate(
                "DeclConst",
                node.notNull ? Misc.getLineColumnOf(node.ctx) : Misc.UNKNOWN_LINE_COLUMN,
                node.notNull ? tmplNotNullConst : tmplNullableConst,
                "%'TYPE'%",
                getJavaCodeOfType(node.typeSpec),
                "%'NAME'%",
                node.name,
                "%'+VAL'%",
                visit(node.val));
    }

    @Override
    public CodeToResolve visitDeclCursor(DeclCursor node) {

        String code =
                String.format(
                        "final Query %s = new Query(\"%s\"); // param-ref-counts: %s, param-num-of-host-expr: %s",
                        node.name,
                        node.staticSql.rewritten,
                        Arrays.toString(node.paramRefCounts),
                        Arrays.toString(node.paramNumOfHostExpr));
        return new CodeTemplate("DeclCursor", Misc.UNKNOWN_LINE_COLUMN, code);
    }

    @Override
    public CodeToResolve visitDeclLabel(DeclLabel node) {
        throw new RuntimeException("unreachable");
    }

    @Override
    public CodeToResolve visitDeclException(DeclException node) {
        String code = "class " + node.name + " extends $APP_ERROR {}";
        return new CodeTemplate("DeclException", Misc.UNKNOWN_LINE_COLUMN, code);
    }

    // -------------------------------------------------------------------------
    // ExprBetween
    //

    private static String[] tmplExprBetween =
            new String[] {
                "opBetween%'OP-EXTENSION'%(",
                "  %'+TARGET'%,",
                "  %'+LOWER-BOUND'%,",
                "  %'+UPPER-BOUND'%",
                ")"
            };

    @Override
    public CodeToResolve visitExprBetween(ExprBetween node) {
        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprBetween",
                        Misc.getLineColumnOf(node.ctx),
                        tmplExprBetween,
                        "%'OP-EXTENSION'%",
                        node.opExtension,
                        "%'+TARGET'%",
                        visit(node.target),
                        "%'+LOWER-BOUND'%",
                        visit(node.lowerBound),
                        "%'+UPPER-BOUND'%",
                        visit(node.upperBound));

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprBinaryOp
    //

    private static String[] tmplExprBinaryOp =
            new String[] {
                "%'OPT-NEGATE'%op%'OPERATION'%%'OP-EXTENSION'%(",
                "  %'+LEFT-OPERAND'%,",
                "  %'+RIGHT-OPERAND'%",
                ")"
            };

    @Override
    public CodeToResolve visitExprBinaryOp(ExprBinaryOp node) {

        CodeTemplate tmpl;
        if (node.recordTypeOfOperands == null) {
            tmpl =
                    new CodeTemplate(
                            "ExprBinaryOp - for non-records",
                            Misc.getLineColumnOf(node.ctx),
                            tmplExprBinaryOp,
                            "%'OPT-NEGATE'%",
                            "",
                            "%'OPERATION'%",
                            node.opStr,
                            "%'OP-EXTENSION'%",
                            node.opExtension,
                            "%'+LEFT-OPERAND'%",
                            visit(node.left),
                            "%'+RIGHT-OPERAND'%",
                            visit(node.right));
        } else {

            javaTypesUsed.add("java.util.Objects");

            boolean isEq = node.opStr.equals("Eq");

            tmpl =
                    new CodeTemplate(
                            "ExprBinaryOp - for records",
                            Misc.getLineColumnOf(node.ctx),
                            tmplExprBinaryOp,
                            "%'OPT-NEGATE'%",
                            (isEq ? "" : "!"),
                            "%'OPERATION'%",
                            "Eq",
                            "%'OP-EXTENSION'%",
                            node.recordTypeOfOperands.javaCode,
                            "%'+LEFT-OPERAND'%",
                            visit(node.left),
                            "%'+RIGHT-OPERAND'%",
                            visit(node.right));
        }

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprCase
    //

    private static String[] tmplExprCase =
            new String[] {
                "(new Object() { %'RESULT-TYPE'% invoke(%'SELECTOR-TYPE'% selector) // simple case expression",
                "   throws Exception {",
                "  return",
                "    %'+WHEN-PARTS'%",
                "    %'+ELSE-PART'%;",
                "}}.invoke(",
                "  %'+SELECTOR-VALUE'%))"
            };

    @Override
    public CodeToResolve visitExprCase(ExprCase node) {

        assert node.selectorType != null;
        assert node.resultType != null;

        CodeTemplate tmpl;

        if (node.resultType == Type.NULL) {
            // in this case, every branch including else-part has null as its expression.
            tmpl = new CodeTemplate("ExprCase", Misc.UNKNOWN_LINE_COLUMN, "null");
        } else {
            tmpl =
                    new CodeTemplate(
                            "ExprCase",
                            Misc.UNKNOWN_LINE_COLUMN,
                            tmplExprCase,
                            "%'SELECTOR-TYPE'%",
                            getJavaCodeOfType(node.selectorType),
                            "%'+SELECTOR-VALUE'%",
                            visit(node.selector),
                            "%'+WHEN-PARTS'%",
                            visitNodeList(node.whenParts),
                            "%'+ELSE-PART'%",
                            node.elsePart == null ? "null" : visit(node.elsePart),
                            "%'RESULT-TYPE'%",
                            getJavaCodeOfType(node.resultType));
        }

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprCond
    //

    private static String[] tmplExprCond =
            new String[] {"(", "  %'+COND-PARTS'%", "  %'+ELSE-PART'%)"};

    @Override
    public CodeToResolve visitExprCond(ExprCond node) {

        assert node.resultType != null;

        CodeTemplate tmpl;

        if (node.resultType == Type.NULL) {
            // in this case, every branch including else has null as its expression.
            tmpl = new CodeTemplate("ExprCond", Misc.UNKNOWN_LINE_COLUMN, "null");
        } else {
            tmpl =
                    new CodeTemplate(
                            "ExprCond",
                            Misc.UNKNOWN_LINE_COLUMN,
                            tmplExprCond,
                            "%'+COND-PARTS'%",
                            visitNodeList(node.condParts),
                            "%'+ELSE-PART'%",
                            node.elsePart == null ? "null" : visit(node.elsePart));
        }

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprCursorAttr
    //

    private static final String tmplIsOpen =
            "((%'CURSOR'% == null) ? Boolean.FALSE : %'CURSOR'%.%'METHOD'%())";

    private static final String tmplOthers =
            "((%'CURSOR'% == null) ? (%'JAVA-TYPE'%) throwInvalidCursor(\"%'SUBMSG'%\") : %'CURSOR'%.%'METHOD'%())";

    @Override
    public CodeToResolve visitExprCursorAttr(ExprCursorAttr node) {

        CodeTemplate tmpl;

        if (node.attr == ExprCursorAttr.Attr.ISOPEN) {
            tmpl =
                    new CodeTemplate(
                            "ExprCursorAttr",
                            Misc.getLineColumnOf(node.ctx),
                            tmplIsOpen,
                            "%'CURSOR'%",
                            node.id.javaCode(),
                            "%'METHOD'%",
                            node.attr.method);
        } else {
            tmpl =
                    new CodeTemplate(
                            "ExprCursorAttr",
                            Misc.getLineColumnOf(node.ctx),
                            tmplOthers,
                            "%'CURSOR'%",
                            node.id.javaCode(),
                            "%'JAVA-TYPE'%",
                            getJavaCodeOfType(node.attr.ty),
                            "%'SUBMSG'%",
                            "tried to retrieve an attribute from an unopened SYS_REFCURSOR",
                            "%'METHOD'%",
                            node.attr.method);
        }

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprDate(ExprDate node) {

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprDate", Misc.UNKNOWN_LINE_COLUMN, node.javaCode(javaTypesUsed));
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprDatetime(ExprDatetime node) {

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprDatetime", Misc.UNKNOWN_LINE_COLUMN, node.javaCode(javaTypesUsed));
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprFalse(ExprFalse node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprFalse", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprField(ExprField node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprField", Misc.getLineColumnOf(node.ctx), node.javaCode());
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprGlobalFuncCall
    //

    private static String[] tmplExprGlobalFuncCall =
            new String[] {
                "(new Object() { // global function call: %'FUNC-NAME'%",
                "  %'RETURN-TYPE'% invoke(%'PARAMETERS'%) throws Exception {",
                "    try {",
                "      String dynSql = \"%'DYNAMIC-SQL'%\";",
                "      CallableStatement stmt = conn.prepareCall(dynSql);",
                "      stmt.registerOutParameter(1, java.sql.Types.OTHER);",
                "      %'+SET-GLOBAL-FUNC-ARGS'%",
                "      stmt.execute();",
                "      %'RETURN-TYPE'% ret = (%'RETURN-TYPE'%) stmt.getObject(1);",
                "      %'+UPDATE-GLOBAL-FUNC-OUT-ARGS'%",
                "      stmt.close();",
                "      return ret;",
                "    } catch (SQLException e) {",
                "      Server.log(e);",
                "      throw new SQL_ERROR(e.getMessage());",
                "    }",
                "  }",
                "}.invoke(",
                "  %'+ARGUMENTS'%",
                "))"
            };

    @Override
    public CodeToResolve visitExprGlobalFuncCall(ExprGlobalFuncCall node) {

        assert node.decl != null;

        int argSize = node.args.nodes.size();
        String dynSql = String.format("?= call %s(%s)", node.name, getQuestionMarks(argSize));
        String wrapperParam = getCallWrapperParam(argSize, node.args, node.decl.paramList);
        GlobalCallCodeSnippets code =
                getGlobalCallCodeSnippets(argSize, 2, node.args, node.decl.paramList);

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprGlobalFuncCall",
                        Misc.getLineColumnOf(node.ctx),
                        tmplExprGlobalFuncCall,
                        "%'FUNC-NAME'%",
                        node.name,
                        "%'DYNAMIC-SQL'%",
                        dynSql,
                        "%'RETURN-TYPE'%",
                        getJavaCodeOfType(node.decl.retTypeSpec),
                        "%'PARAMETERS'%",
                        wrapperParam,
                        "%'+SET-GLOBAL-FUNC-ARGS'%",
                        code.setArgs,
                        "%'+UPDATE-GLOBAL-FUNC-OUT-ARGS'%",
                        code.updateOutArgs,
                        "%'+ARGUMENTS'%",
                        visitArguments(node.args, node.decl.paramList, false));

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprId
    //

    @Override
    public CodeToResolve visitExprId(ExprId node) {

        CodeTemplate tmpl = new CodeTemplate("ExprId", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprIn
    //

    private static String[] tmplExprIn =
            new String[] {"opIn%'OP-EXTENSION'%(", "  %'+TARGET'%,", "  %'+IN-ELEMENTS'%", ")"};

    @Override
    public CodeToResolve visitExprIn(ExprIn node) {

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprIn",
                        Misc.getLineColumnOf(node.ctx),
                        tmplExprIn,
                        "%'OP-EXTENSION'%",
                        node.opExtension,
                        "%'+TARGET'%",
                        visit(node.target),
                        "%'+IN-ELEMENTS'%",
                        visitNodeList(node.inElements).setDelimiter(","));

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprLike
    //

    private static String[] tmplExprLike =
            new String[] {"opLike(", "  %'+TARGET'%,", "  %'+PATTERN'%,", "  %'ESCAPE'%", ")"};

    @Override
    public CodeToResolve visitExprLike(ExprLike node) {

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprLike",
                        Misc.getLineColumnOf(node.ctx),
                        tmplExprLike,
                        "%'+TARGET'%",
                        visit(node.target),
                        "%'+PATTERN'%",
                        visit(node.pattern),
                        "%'ESCAPE'%",
                        node.escape == null ? "null" : node.escape.javaCode());

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprBuiltInFuncCall
    //

    private static String[] tmplExprBuiltinFuncCall =
            new String[] {
                "(%'RESULT-TYPE'%) invokeBuiltinFunc(conn, \"%'NAME'%\", %'RESULT-TYPE-CODE'%,",
                "  %'+ARGS'%",
                ")"
            };

    @Override
    public CodeToResolve visitExprBuiltinFuncCall(ExprBuiltinFuncCall node) {

        assert node.args != null;
        assert node.resultType != null;
        String ty = getJavaCodeOfType(node.resultType);

        CodeTemplate tmpl;

        if (node.args.nodes.size() == 0) {
            tmpl =
                    new CodeTemplate(
                            "ExprBuiltinFuncCall",
                            Misc.getLineColumnOf(node.ctx),
                            String.format(
                                    "(%s) invokeBuiltinFunc(conn, \"%s\", %d)",
                                    ty, node.name, node.resultType.idx));
        } else {
            tmpl =
                    new CodeTemplate(
                            "ExprBuiltinFuncCall",
                            Misc.getLineColumnOf(node.ctx),
                            tmplExprBuiltinFuncCall,
                            "%'RESULT-TYPE'%",
                            ty,
                            "%'NAME'%",
                            node.name,
                            "%'RESULT-TYPE-CODE'%",
                            Integer.toString(node.resultType.idx),
                            // assumption: built-in functions do not have OUT parameters
                            "%'+ARGS'%",
                            visitNodeList(node.args).setDelimiter(","));
        }

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprLocalFuncCall
    //

    private static String[] tmplExprLocalFuncCall =
            new String[] {
                "(new Object() { // local function call: %'FUNC-NAME'%",
                "  %'RETURN-TYPE'% invoke(%'PARAMETERS'%) throws Exception {",
                "    %'+ALLOC-COERCED-OUT-ARGS'%",
                "    %'RETURN-TYPE'% ret = %'BLOCK'%%'FUNC-NAME'%(%'ARGS'%);",
                "    %'+UPDATE-OUT-ARGS'%",
                "    return ret;",
                "  }",
                "}.invoke(",
                "  %'+ARGUMENTS'%",
                "))"
            };

    @Override
    public CodeToResolve visitExprLocalFuncCall(ExprLocalFuncCall node) {

        assert node.decl != null;

        int paramSize = node.decl.paramList.nodes.size();
        String wrapperParam = getCallWrapperParam(paramSize, node.args, node.decl.paramList);
        LocalCallCodeSnippets code =
                getLocalCallCodeSnippets(paramSize, node.args, node.decl.paramList);
        String block = node.prefixDeclBlock ? node.decl.scope().block + "." : "";

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprLocalFuncCall",
                        Misc.getLineColumnOf(node.ctx),
                        tmplExprLocalFuncCall,
                        "%'BLOCK'%",
                        block,
                        "%'FUNC-NAME'%",
                        node.name,
                        "%'RETURN-TYPE'%",
                        getJavaCodeOfType(node.decl.retTypeSpec),
                        "%'PARAMETERS'%",
                        wrapperParam,
                        "%'+ALLOC-COERCED-OUT-ARGS'%",
                        code.allocCoercedOutArgs,
                        "%'ARGS'%",
                        code.argsToLocal,
                        "%'+UPDATE-OUT-ARGS'%",
                        code.updateOutArgs,
                        "%'+ARGUMENTS'%",
                        visitArguments(node.args, node.decl.paramList, true));

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprNull
    //

    @Override
    public CodeToResolve visitExprNull(ExprNull node) {

        CodeTemplate tmpl = new CodeTemplate("ExprNull", Misc.UNKNOWN_LINE_COLUMN, "null");

        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprUint(ExprUint node) {
        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprUnit", Misc.UNKNOWN_LINE_COLUMN, node.javaCode(javaTypesUsed));
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprFloat(ExprFloat node) {
        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprFloat", Misc.UNKNOWN_LINE_COLUMN, node.javaCode(javaTypesUsed));
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprSerialVal
    //

    private static String[] tmplExprSerialVal =
            new String[] {
                "(new Object() {",
                "  BigDecimal getSerialVal() throws Exception {",
                "    try {",
                "      BigDecimal ret;",
                "      String dynSql = \"select %'SERIAL-NAME'%.%'SERIAL-VAL'%\";",
                "      PreparedStatement stmt = conn.prepareStatement(dynSql);",
                "      ResultSet r = stmt.executeQuery();",
                "      if (r.next()) {",
                "        ret = r.getBigDecimal(1);",
                "        if (ret != null && r.wasNull()) {",
                "          ret = null;",
                "        }",
                "      } else {",
                "        ret = null;",
                "      }",
                "      stmt.close();",
                "      return ret;",
                "    } catch (SQLException e) {",
                "      Server.log(e);",
                "      throw new SQL_ERROR(e.getMessage());",
                "    }",
                "  }",
                "}.getSerialVal())"
            };

    @Override
    public CodeToResolve visitExprSerialVal(ExprSerialVal node) {

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprSerialVal",
                        Misc.getLineColumnOf(node.ctx),
                        tmplExprSerialVal,
                        "%'SERIAL-NAME'%",
                        node.name,
                        "%'SERIAL-VAL'%",
                        (node.mode == ExprSerialVal.SerialVal.CURR_VAL)
                                ? "CURRENT_VALUE"
                                : "NEXT_VALUE");
        javaTypesUsed.add("java.math.BigDecimal");
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprSqlRowCount(ExprSqlRowCount node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprSqlRowCount", Misc.UNKNOWN_LINE_COLUMN, "sql_rowcount[0]");
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprStr(ExprStr node) {

        CodeTemplate tmpl = new CodeTemplate("ExprStr", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprTime(ExprTime node) {

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprTime", Misc.UNKNOWN_LINE_COLUMN, node.javaCode(javaTypesUsed));
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprTrue(ExprTrue node) {

        CodeTemplate tmpl = new CodeTemplate("ExprTrue", Misc.UNKNOWN_LINE_COLUMN, "true");
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // ExprUnaryOp
    //

    private static String[] tmplExprUnaryOp =
            new String[] {"op%'OPERATION'%(", "  %'+OPERAND'%", ")"};

    @Override
    public CodeToResolve visitExprUnaryOp(ExprUnaryOp node) {

        CodeTemplate tmpl;
        tmpl =
                new CodeTemplate(
                        "ExprUnaryOp",
                        Misc.getLineColumnOf(node.ctx),
                        tmplExprUnaryOp,
                        "%'OPERATION'%",
                        node.opStr,
                        "%'+OPERAND'%",
                        visit(node.operand));
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprTimestamp(ExprTimestamp node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprTimestamp", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprAutoParam(ExprAutoParam node) {

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprAutoParam", Misc.UNKNOWN_LINE_COLUMN, node.javaCode(javaTypesUsed));
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprSqlCode(ExprSqlCode node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprSqlCode", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    @Override
    public CodeToResolve visitExprSqlErrm(ExprSqlErrm node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprSqlCode", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl, node.ctx);
    }

    // -------------------------------------------------------------------------
    // StmtAssign
    //

    private static final String[] tmplAssignNullToRecord =
            new String[] {"(", "%'+DST-EXPR'%).setNull(", "  %'+SRC-EXPR'%);"};

    private static final String[] tmplAssignRecordToRecord =
            new String[] {
                "setFieldsOf%'SRC-RECORD'%_To_%'DST-RECORD'%(",
                "  %'+SRC-EXPR'%,",
                "  %'+DST-EXPR'%);"
            };

    private static final String[] tmplAssignNotNull =
            new String[] {
                "%'+TARGET'% = checkNotNull(", "  %'+VAL'%, \"NOT NULL constraint violated\");"
            };
    private static final String[] tmplAssignNullable =
            new String[] {"%'+TARGET'% =", "  %'+VAL'%;"};

    @Override
    public CodeToResolve visitStmtAssign(StmtAssign node) {

        if (node.val.coercion instanceof Coercion.NullToRecord) {

            node.val.coercion = null; // small optimization: suppressing null record creation

            return new CodeTemplate(
                    "StmtAssign - null to record",
                    Misc.getLineColumnOf(node.ctx),
                    tmplAssignNullToRecord,
                    "%'+SRC-EXPR'%",
                    visit(node.val),
                    "%'+DST-EXPR'%",
                    visit(node.target));

        } else if (node.val.coercion instanceof Coercion.RecordToRecord) {

            Coercion c = node.val.coercion;
            node.val.coercion = null; // small optimization: suppressing null record creation

            return new CodeTemplate(
                    "StmtAssign - record to record",
                    Misc.getLineColumnOf(node.ctx),
                    tmplAssignRecordToRecord,
                    "%'SRC-RECORD'%",
                    c.src.javaCode,
                    "%'DST-RECORD'%",
                    c.dst.javaCode,
                    "%'+SRC-EXPR'%",
                    visit(node.val),
                    "%'+DST-EXPR'%",
                    visit(node.target));
        }

        boolean checkNotNull = false;
        if (node.target instanceof ExprId) {
            ExprId targetId = (ExprId) node.target;
            if (targetId.decl instanceof DeclVar && ((DeclVar) targetId.decl).notNull) {
                checkNotNull = true;
            }
        }
        if (checkNotNull) {

            return new CodeTemplate(
                    "StmtAssign - not null",
                    Misc.getLineColumnOf(node.ctx),
                    tmplAssignNotNull,
                    "%'+TARGET'%",
                    visit(node.target),
                    "%'+VAL'%",
                    visit(node.val));
        } else {

            return new CodeTemplate(
                    "StmtAssign - nullable",
                    Misc.UNKNOWN_LINE_COLUMN,
                    tmplAssignNullable,
                    "%'+TARGET'%",
                    visit(node.target),
                    "%'+VAL'%",
                    visit(node.val));
        }
    }

    // -------------------------------------------------------------------------
    // StmtBasicLoop
    //

    // NOTE: why I use 'while(opNot(false))' instead of simpler 'while(true)':
    // Compiling Java code below with javac causes 'unreachable statement' error
    //     while (true) {
    //         ... // no break
    //     }
    //     ... // a statement
    // However, compiling the following does not
    //     while (opNot(false)) {
    //         ... // no break
    //     }
    //     ... // a statement
    // It seems that static analysis of javac does not go beyond method call boundaries

    private static String[] tmplStmtBasicLoop =
            new String[] {"%'OPT-LABEL'%", "while (opNot(false)) {", "  %'+STATEMENTS'%", "}"};

    @Override
    public CodeToResolve visitStmtBasicLoop(StmtBasicLoop node) {

        CodeTemplate ret =
                new CodeTemplate(
                        "StmtBasicLoop",
                        Misc.UNKNOWN_LINE_COLUMN,
                        tmplStmtBasicLoop,
                        "%'OPT-LABEL'%",
                        node.declLabel == null ? "" : node.declLabel.javaCode(),
                        "%'+STATEMENTS'%",
                        visitNodeList(node.stmts));
        if (node.loopOptimizable == null || node.loopOptimizable.isEmpty()) {
            return ret;
        } else {
            return wrapWithStmtDeclareAndClose(node, ret);
        }
    }

    // -------------------------------------------------------------------------
    // StmtBlock
    //

    private static String[] tmplStmtBlock =
            new String[] {"{", "  %'+DECL-CLASS'%", "", "  %'+BODY'%", "}"};

    @Override
    public CodeToResolve visitStmtBlock(StmtBlock node) {

        Object declClass =
                node.decls == null
                        ? ""
                        : new CodeTemplate(
                                "DeclClass of Block",
                                Misc.UNKNOWN_LINE_COLUMN,
                                tmplDeclBlock,
                                "%'BLOCK'%",
                                node.block,
                                "%'+DECLARATIONS'%",
                                visitNodeList(node.decls));

        return new CodeTemplate(
                "StmtBlock",
                Misc.UNKNOWN_LINE_COLUMN,
                tmplStmtBlock,
                "%'+DECL-CLASS'%",
                declClass,
                "%'+BODY'%",
                visit(node.body));
    }

    @Override
    public CodeToResolve visitStmtExit(StmtExit node) {
        return new CodeTemplate("StmtExit", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
    }

    // -------------------------------------------------------------------------
    // StmtCase
    //
    private static String[] tmplStmtCase =
            new String[] {
                "{",
                "  %'SELECTOR-TYPE'% selector_%'LEVEL'% =",
                "    %'+SELECTOR-VALUE'%;",
                "  %'+WHEN-PARTS'% else {",
                "    %'+ELSE-PART'%",
                "  }",
                "}"
            };

    @Override
    public CodeToResolve visitStmtCase(StmtCase node) {

        assert node.selectorType != null;

        return new CodeTemplate(
                "StmtCase",
                Misc.getLineColumnOf(node.ctx),
                tmplStmtCase,
                "%'SELECTOR-TYPE'%",
                getJavaCodeOfType(node.selectorType),
                "%'+SELECTOR-VALUE'%",
                visit(node.selector),
                "%'+WHEN-PARTS'%",
                visitNodeList(node.whenParts).setDelimiter(" else"),
                "%'+ELSE-PART'%",
                node.elsePart == null ? "throw new CASE_NOT_FOUND();" : visit(node.elsePart),
                "%'LEVEL'%",
                Integer.toString(node.level) // level replacement must go last
                );
    }

    // -------------------------------------------------------------------------
    // StmtCommit
    //

    private static String[] tmplStmtCommit =
            new String[] {
                "try {",
                "  conn.commit();",
                "  sql_rowcount[0] = 0L;",
                "} catch (SQLException e) {",
                "  Server.log(e);",
                "  throw new SQL_ERROR(e.getMessage());",
                "}"
            };

    @Override
    public CodeToResolve visitStmtCommit(StmtCommit node) {
        return new CodeTemplate("StmtCommit", Misc.getLineColumnOf(node.ctx), tmplStmtCommit);
    }

    @Override
    public CodeToResolve visitStmtContinue(StmtContinue node) {
        return new CodeTemplate("StmtContinue", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
    }

    // -------------------------------------------------------------------------
    // CursorClose
    //

    private static String[] tmplStmtCursorClose =
            new String[] {
                "// cursor close",
                "if (%'CURSOR'% != null && %'CURSOR'%.isOpen()) {",
                "  %'CURSOR'%.close();",
                "} else {",
                "  throw new INVALID_CURSOR(\"tried to close an unopened cursor\");",
                "}"
            };

    @Override
    public CodeToResolve visitStmtCursorClose(StmtCursorClose node) {

        return new CodeTemplate(
                "StmtCursorClose",
                Misc.getLineColumnOf(node.ctx),
                tmplStmtCursorClose,
                "%'CURSOR'%",
                node.id.javaCode());
    }

    // -------------------------------------------------------------------------
    // StmtCursorFetch
    //

    private static String[] tmplStmtCursorFetch =
            new String[] {
                "{ // cursor fetch",
                "  if (%'CURSOR'% == null || !%'CURSOR'%.isOpen()) {",
                "    throw new INVALID_CURSOR(\"tried to fetch values with an unopened cursor\");",
                "  }",
                "  ResultSet rs = %'CURSOR'%.rs;",
                "  if (rs.next()) {",
                "    %'CURSOR'%.updateRowCount();",
                "    %'+SET-INTO-VARIABLES'%",
                "  } else {",
                "    ;", // TODO: setting nulls to into-variables?
                "  }",
                "}"
            };

    private String[] getSetIntoTargetsCode(StmtCursorFetch node) {

        List<String> ret = new LinkedList<>();

        assert node.coercions != null;
        assert node.coercions.size() == node.intoTargetList.size();

        int i = 0;
        for (Expr target : node.intoTargetList) {

            assert target instanceof AssignTarget;

            String resultStr;
            if (node.columnTypeList == null) {
                resultStr = String.format("rs.getObject(%d)", i + 1);
            } else {
                resultStr =
                        String.format(
                                "(%s) rs.getObject(%d)",
                                getJavaCodeOfType(node.columnTypeList.get(i)), i + 1);
            }

            Coercion c = node.coercions.get(i);
            String idCode = ((AssignTarget) target).javaCode();
            ret.add(String.format("%s = %s;", idCode, c.javaCode(resultStr)));
            ret.add(String.format("if (%1$s != null && rs.wasNull()) { %1$s = null; }", idCode));

            i++;
        }

        return ret.toArray(DUMMY_STRING_ARRAY);
    }

    @Override
    public CodeToResolve visitStmtCursorFetch(StmtCursorFetch node) {

        String[] setIntoTargets = getSetIntoTargetsCode(node);
        return new CodeTemplate(
                "StmtCursorFetch",
                Misc.getLineColumnOf(node.ctx),
                tmplStmtCursorFetch,
                "%'CURSOR'%",
                node.id.javaCode(),
                "%'+SET-INTO-VARIABLES'%",
                setIntoTargets);
    }

    // -------------------------------------------------------------------------
    // StmtCursorOpen
    //

    private static String[] tmplStmtCursorOpenWithoutHostExprs =
            new String[] {"{ // cursor open", "  %'CURSOR'%.open(conn);", "}"};

    private static String[] tmplStmtCursorOpenWithHostExprs =
            new String[] {
                "{ // cursor open",
                "  %'+DUPLICATE-CURSOR-ARG'%",
                "  %'CURSOR'%.open(conn, new Object[] {",
                "    %'+HOST-EXPRS'%});",
                "}"
            };

    @Override
    public CodeToResolve visitStmtCursorOpen(StmtCursorOpen node) {

        DeclCursor decl = (DeclCursor) node.cursor.decl;
        if (decl.paramNumOfHostExpr.length == 0) {

            return new CodeTemplate(
                    "StmtCursorOpen",
                    Misc.getLineColumnOf(node.ctx),
                    tmplStmtCursorOpenWithoutHostExprs,
                    "%'CURSOR'%",
                    node.cursor.javaCode());
        } else {

            Object dupCursorArgs = getDupCursorArgs(node.args, decl.paramRefCounts);
            CodeTemplateList hostExprs =
                    getHostExprs(
                            node.cursor, node.args, decl.paramNumOfHostExpr, decl.paramRefCounts);

            return new CodeTemplate(
                    "StmtCursorOpen",
                    Misc.getLineColumnOf(node.ctx),
                    tmplStmtCursorOpenWithHostExprs,
                    "%'+DUPLICATE-CURSOR-ARG'%",
                    dupCursorArgs,
                    "%'CURSOR'%",
                    node.cursor.javaCode(),
                    "%'+HOST-EXPRS'%",
                    hostExprs,
                    "%'LEVEL'%",
                    Integer.toString(node.cursor.scope.level));
        }
    }

    // -------------------------------------------------------------------------
    // visitStmtSql (visitStmtExecImme, visitStmtStaticSql)
    //

    private static String[] tmplStmtSqlNotInLoop =
            new String[] {
                "{ // %'KIND'% SQL statement",
                "  PreparedStatement pstmt_%'SQL-SERIAL-NO'% = null;",
                "  try {",
                "    String dynSql_%'LEVEL'% = checkNotNull(",
                "      %'+SQL'%, \"SQL part was evaluated to NULL\");",
                "    pstmt_%'SQL-SERIAL-NO'% = conn.prepareStatement(dynSql_%'LEVEL'%);",
                "    %'+BAN-INTO-CLAUSE'%",
                "    %'+SET-USED-EXPR'%",
                "    if (pstmt_%'SQL-SERIAL-NO'%.execute()) {",
                // not from the Oracle specification, but from Oracle 19.0.0.0 behavior
                "      sql_rowcount[0] = 0L;",
                "      %'+HANDLE-INTO-CLAUSE'%",
                "    } else {",
                "      sql_rowcount[0] = (long) pstmt_%'SQL-SERIAL-NO'%.getUpdateCount();",
                "    }",
                "  } catch (SQLException e) {",
                "    Server.log(e);",
                "    throw new SQL_ERROR(e.getMessage());",
                "  } finally {",
                "    if (pstmt_%'SQL-SERIAL-NO'% != null) {",
                "      pstmt_%'SQL-SERIAL-NO'%.close();",
                "    }",
                "  }",
                "}"
            };

    private static String[] tmplStmtSqlInLoop =
            new String[] {
                "{ // %'KIND'% SQL statement",
                "  try {",
                // no PrepareStatement declaration: it is done right before the outermost loop
                "    String dynSql_%'LEVEL'% = checkNotNull(",
                "      %'+SQL'%, \"SQL part was evaluated to NULL\");",
                "    if (pstmt_%'SQL-SERIAL-NO'% == null) {",
                // check if it is null to prepare the statement only once
                "      pstmt_%'SQL-SERIAL-NO'% = conn.prepareStatement(dynSql_%'LEVEL'%);",
                "    }",
                "    %'+BAN-INTO-CLAUSE'%",
                "    %'+SET-USED-EXPR'%",
                "    if (pstmt_%'SQL-SERIAL-NO'%.execute()) {",
                // not from the Oracle specification, but from Oracle 19.0.0.0 behavior
                "      sql_rowcount[0] = 0L;",
                "      %'+HANDLE-INTO-CLAUSE'%",
                "    } else {",
                "      sql_rowcount[0] = (long) pstmt_%'SQL-SERIAL-NO'%.getUpdateCount();",
                "    }",
                "  } catch (SQLException e) {",
                "    Server.log(e);",
                "    throw new SQL_ERROR(e.getMessage());",
                // no PreparedStatement.close() call in a finally clause: it is done right after the
                // outermost loop
                "  }",
                "}"
            };

    private static String[] tmplHandleIntoClause =
            new String[] {
                "ResultSet r%'LEVEL'% = pstmt_%'SQL-SERIAL-NO'%.getResultSet();",
                "if (r%'LEVEL'% == null) {",
                // EXECUTE IMMEDIATE 'CALL ...' INTO ... leads to this line
                "  throw new SQL_ERROR(\"no result set\");",
                "}",
                "int i%'LEVEL'% = 0;",
                "while (r%'LEVEL'%.next()) {",
                "  i%'LEVEL'%++;",
                "  if (i%'LEVEL'% > 1) {",
                "    break;",
                "  } else {",
                "    %'+SET-RESULTS'%",
                "  }",
                "}",
                "if (i%'LEVEL'% == 0) {",
                "  throw new NO_DATA_FOUND();",
                "} else if (i%'LEVEL'% == 1) {",
                "  sql_rowcount[0] = 1L;",
                "} else {",
                "  sql_rowcount[0] = 1L;", // Surprise? Refer to the Spec.
                "  throw new TOO_MANY_ROWS();",
                "}"
            };

    private static String[] tmplBanIntoClause =
            new String[] {
                "ResultSetMetaData rsmd_%'LEVEL'% = pstmt_%'SQL-SERIAL-NO'%.getMetaData();",
                "if (rsmd_%'LEVEL'% == null || rsmd_%'LEVEL'%.getColumnCount() < 1) {",
                "  throw new SQL_ERROR(\"INTO clause must be used with a SELECT statement\");",
                "}"
            };

    private String[] getSetResultsCode(StmtSql node, List<Expr> intoTargetList) {

        List<String> ret = new LinkedList<>();

        int size = intoTargetList.size();
        assert node.coercions.size() == size;
        assert node.dynamic || (node.columnTypeList != null && node.columnTypeList.size() == size);

        int i = 0;
        for (Expr target : node.intoTargetList) {

            assert target instanceof AssignTarget;

            String resultStr;
            if (node.dynamic) {
                resultStr = String.format("r%%'LEVEL'%%.getObject(%d)", i + 1);
            } else {
                resultStr =
                        String.format(
                                "(%s) r%%'LEVEL'%%.getObject(%d)",
                                getJavaCodeOfType(node.columnTypeList.get(i)), i + 1);
            }

            Coercion c = node.coercions.get(i);
            String targetCode = ((AssignTarget) target).javaCode();
            ret.add(String.format("%s = %s;", targetCode, c.javaCode(resultStr)));
            ret.add(
                    String.format(
                            "if (%1$s != null && r%%'LEVEL'%%.wasNull()) { %1$s = null; }",
                            targetCode));

            if (target instanceof ExprId) {

                ExprId id = (ExprId) target;

                assert id.decl instanceof DeclVar || id.decl instanceof DeclParamOut
                        : "only variables or out-parameters can be used in into-clauses";

                if ((id.decl instanceof DeclVar) && ((DeclVar) id.decl).notNull) {
                    ret.add(
                            String.format(
                                    "checkNotNull(%s, \"NOT NULL constraint violated\");",
                                    targetCode));
                }
            }

            i++;
        }

        return ret.toArray(DUMMY_STRING_ARRAY);
    }

    private CodeToResolve visitStmtSql(StmtSql node) {

        Object setUsedExpr = getSetUsedExpr(node.usedExprList);

        Object handleIntoClause, banIntoClause;
        if (node.intoTargetList == null) {
            assert node.coercions == null;
            handleIntoClause = banIntoClause = "";
        } else {
            assert node.coercions != null;
            String[] setResults = getSetResultsCode(node, node.intoTargetList);
            handleIntoClause =
                    new CodeTemplate(
                            "into clause in SQL",
                            Misc.UNKNOWN_LINE_COLUMN,
                            tmplHandleIntoClause,
                            "%'+SET-RESULTS'%",
                            setResults);
            banIntoClause = tmplBanIntoClause;
        }

        return new CodeTemplate(
                "StmtSql",
                Misc.getLineColumnOf(node.ctx),
                node.outermostLoop == null ? tmplStmtSqlNotInLoop : tmplStmtSqlInLoop,
                "%'KIND'%",
                node.dynamic ? "dynamic" : "static",
                "%'SQL-SERIAL-NO'%",
                "" + node.sqlSerialNo,
                "%'+SQL'%",
                visit(node.sql),
                "%'+BAN-INTO-CLAUSE'%",
                banIntoClause,
                "%'+SET-USED-EXPR'%",
                setUsedExpr,
                "%'+HANDLE-INTO-CLAUSE'%",
                handleIntoClause,
                "%'LEVEL'%",
                Integer.toString(node.level));
    }

    @Override
    public CodeToResolve visitStmtExecImme(StmtExecImme node) {
        return visitStmtSql(node);
    }

    @Override
    public CodeToResolve visitStmtStaticSql(StmtStaticSql node) {
        return visitStmtSql(node);
    }

    // -------------------------------------------------------------------------
    // StmtForCursorLoop
    //

    private static String[] tmplStmtForCursorLoopWithoutHostExprs =
            new String[] {
                "try { // for loop with a cursor",
                "  %'RECORD-CLASS'%[] %'RECORD'% = new %'RECORD-CLASS'%[] { new %'RECORD-CLASS'%() };",
                "  %'CURSOR'%.open(conn);",
                "  ResultSet %'RECORD'%_r%'LEVEL'% = %'CURSOR'%.rs;",
                "  %'LABEL'%",
                "  while (%'RECORD'%_r%'LEVEL'%.next()) {",
                "    %'CURSOR'%.updateRowCount();",
                "    %'RECORD'%[0].set(",
                "      %'+RECORD-FIELD-VALUES'%",
                "    );",
                "    %'+STATEMENTS'%",
                "  }",
                "  %'CURSOR'%.close();",
                "} catch (SQLException e) {",
                "  Server.log(e);",
                "  throw new SQL_ERROR(e.getMessage());",
                "}"
            };

    private static String[] tmplStmtForCursorLoopWithHostExprs =
            new String[] {
                "try { // for loop with a cursor",
                "  %'RECORD-CLASS'%[] %'RECORD'% = new %'RECORD-CLASS'%[] { new %'RECORD-CLASS'%() };",
                "  %'+DUPLICATE-CURSOR-ARG'%",
                "  %'CURSOR'%.open(conn,",
                "    %'+HOST-EXPRS'%);",
                "  ResultSet %'RECORD'%_r%'LEVEL'% = %'CURSOR'%.rs;",
                "  %'LABEL'%",
                "  while (%'RECORD'%_r%'LEVEL'%.next()) {",
                "    %'CURSOR'%.updateRowCount();",
                "    %'RECORD'%[0].set(",
                "      %'+RECORD-FIELD-VALUES'%",
                "    );",
                "    %'+STATEMENTS'%",
                "  }",
                "  %'CURSOR'%.close();",
                "} catch (SQLException e) {",
                "  Server.log(e);",
                "  throw new SQL_ERROR(e.getMessage());",
                "}"
            };

    private String[] getRecordSetArgs(String record, TypeRecord recTy, int level) {

        int i = 1;

        List<String> ret = new LinkedList<>();
        for (Misc.Pair<String, Type> field : recTy.selectList) {
            ret.add(
                    String.format(
                            "%s(%s) getFieldWithIndex(%s_r%d, %d)",
                            (i > 1 ? ", " : ""), getJavaCodeOfType(field.e2), record, level, i));
            i++;
        }

        return ret.toArray(DUMMY_STRING_ARRAY);
    }

    @Override
    public CodeToResolve visitStmtForCursorLoop(StmtForCursorLoop node) {

        String[] recordSetArgs =
                getRecordSetArgs(node.record, node.recordType, node.cursor.scope.level);

        DeclCursor decl = (DeclCursor) node.cursor.decl;
        CodeTemplate ret;
        if (decl.paramNumOfHostExpr.length == 0) {

            ret =
                    new CodeTemplate(
                            "StmtForCursorLoop",
                            Misc.getLineColumnOf(node.ctx),
                            tmplStmtForCursorLoopWithoutHostExprs,
                            "%'RECORD-CLASS'%",
                            node.recordType.javaCode,
                            "%'+RECORD-FIELD-VALUES'%",
                            recordSetArgs,
                            "%'CURSOR'%",
                            node.cursor.javaCode(),
                            "%'RECORD'%",
                            node.record,
                            "%'LABEL'%",
                            node.label == null ? "" : node.label + "_%'LEVEL'%:",
                            "%'LEVEL'%",
                            Integer.toString(node.cursor.scope.level),
                            "%'+STATEMENTS'%",
                            visitNodeList(node.stmts));
        } else {

            Object dupCursorArgs = getDupCursorArgs(node.cursorArgs, decl.paramRefCounts);
            CodeTemplateList hostExprs =
                    getHostExprs(
                            node.cursor,
                            node.cursorArgs,
                            decl.paramNumOfHostExpr,
                            decl.paramRefCounts);

            ret =
                    new CodeTemplate(
                            "StmtForCursorLoop",
                            Misc.getLineColumnOf(node.ctx),
                            tmplStmtForCursorLoopWithHostExprs,
                            "%'RECORD-CLASS'%",
                            node.recordType.javaCode,
                            "%'+RECORD-FIELD-VALUES'%",
                            recordSetArgs,
                            "%'+DUPLICATE-CURSOR-ARG'%",
                            dupCursorArgs,
                            "%'CURSOR'%",
                            node.cursor.javaCode(),
                            "%'+HOST-EXPRS'%",
                            hostExprs,
                            "%'RECORD'%",
                            node.record,
                            "%'LABEL'%",
                            node.label == null ? "" : node.label + "_%'LEVEL'%:",
                            "%'LEVEL'%",
                            Integer.toString(node.cursor.scope.level),
                            "%'+STATEMENTS'%",
                            visitNodeList(node.stmts));
        }

        if (node.loopOptimizable == null || node.loopOptimizable.isEmpty()) {
            return ret;
        } else {
            return wrapWithStmtDeclareAndClose(node, ret);
        }
    }

    // -------------------------------------------------------------------------
    // StmtForIterLoop
    //

    private static String[] tmplStmtForIterLoop =
            new String[] {
                "{ // for loop with integer iterator",
                "  int l%'LVL'% =",
                "    %'+LOWER-BOUND'%;",
                "  int u%'LVL'% =",
                "    %'+UPPER-BOUND'%;",
                "  int s%'LVL'% = checkForLoopIterStep(",
                "    %'+STEP'%);",
                "  int[] %'I'%_i%'LVL'% = new int[1];",
                "  %'OPT-LABEL'%",
                "  for (%'I'%_i%'LVL'%[0] = l%'LVL'%; %'I'%_i%'LVL'%[0] <= u%'LVL'%; %'I'%_i%'LVL'%[0] += s%'LVL'%) {",
                "    %'+STATEMENTS'%",
                "  }",
                "}"
            };

    private static String[] tmplStmtForIterLoopReverse =
            new String[] {
                "{ // for loop with integer iterator (reverse)",
                "  int l%'LVL'% =",
                "    %'+LOWER-BOUND'%;",
                "  int u%'LVL'% =",
                "    %'+UPPER-BOUND'%;",
                "  int s%'LVL'% = checkForLoopIterStep(",
                "    %'+STEP'%);",
                "  int[] %'I'%_i%'LVL'% = new int[1];",
                "  %'OPT-LABEL'%",
                "  for (%'I'%_i%'LVL'%[0] = u%'LVL'%; %'I'%_i%'LVL'%[0] >= l%'LVL'%; %'I'%_i%'LVL'%[0] -= s%'LVL'%) {",
                "    %'+STATEMENTS'%",
                "  }",
                "}"
            };

    @Override
    public CodeToResolve visitStmtForIterLoop(StmtForIterLoop node) {

        String labelStr = node.declLabel == null ? "" : node.declLabel.javaCode();

        CodeTemplate ret =
                new CodeTemplate(
                        "StmtForIterLoop",
                        Misc.getLineColumnOf(node.ctx),
                        node.reverse ? tmplStmtForIterLoopReverse : tmplStmtForIterLoop,
                        "%'LVL'%",
                        Integer.toString(node.iter.scope.level),
                        "%'OPT-LABEL'%",
                        labelStr,
                        "%'I'%",
                        node.iter.name,
                        "%'+LOWER-BOUND'%",
                        visit(node.lowerBound),
                        "%'+UPPER-BOUND'%",
                        visit(node.upperBound),
                        "%'+STEP'%",
                        node.step == null ? "1" : visit(node.step),
                        "%'+STATEMENTS'%",
                        visitNodeList(node.stmts));
        if (node.loopOptimizable == null || node.loopOptimizable.isEmpty()) {
            return ret;
        } else {
            return wrapWithStmtDeclareAndClose(node, ret);
        }
    }

    // -------------------------------------------------------------------------
    // StmtForStaticSqlLoop
    //

    private static String[] tmplStmtForStaticSqlLoop =
            new String[] {
                "{ // for loop with static SQL",
                "  PreparedStatement pstmt_%'SQL-SERIAL-NO'% = null;",
                "  try {",
                "    %'RECORD-CLASS'%[] %'RECORD'% = new %'RECORD-CLASS'%[] { new %'RECORD-CLASS'%() };",
                "    String sql_%'LEVEL'% =",
                "      %'+SQL'%;",
                "    pstmt_%'SQL-SERIAL-NO'% = conn.prepareStatement(sql_%'LEVEL'%);",
                "    %'+SET-USED-EXPR'%",
                "    ResultSet %'RECORD'%_r%'LEVEL'% = pstmt_%'SQL-SERIAL-NO'%.executeQuery();", // never
                // null
                "    %'LABEL'%",
                "    while (%'RECORD'%_r%'LEVEL'%.next()) {",
                "      %'RECORD'%[0].set(",
                "        %'+RECORD-FIELD-VALUES'%",
                "      );",
                "      %'+STATEMENTS'%",
                "    }",
                "  } catch (SQLException e) {",
                "    Server.log(e);",
                "    throw new SQL_ERROR(e.getMessage());",
                "  } finally {",
                "    if (pstmt_%'SQL-SERIAL-NO'% != null) {",
                "      pstmt_%'SQL-SERIAL-NO'%.close();",
                "    }",
                "  }",
                "}"
            };

    @Override
    public CodeToResolve visitStmtForStaticSqlLoop(StmtForStaticSqlLoop node) {

        Type recTy = node.record.type();
        assert recTy instanceof TypeRecord;

        String[] recordSetArgs =
                getRecordSetArgs(node.record.name(), (TypeRecord) recTy, node.record.scope.level);
        Object setUsedExpr = getSetUsedExpr(node.usedExprList);

        CodeTemplate ret =
                new CodeTemplate(
                        "StmtForSqlLoop",
                        Misc.getLineColumnOf(node.ctx),
                        tmplStmtForStaticSqlLoop,
                        "%'SQL-SERIAL-NO'%",
                        "" + node.sqlSerialNo,
                        "%'RECORD-CLASS'%",
                        node.record.type().javaCode,
                        "%'+SQL'%",
                        visit(node.sql),
                        "%'+SET-USED-EXPR'%",
                        setUsedExpr,
                        "%'RECORD'%",
                        node.record.name(),
                        "%'LABEL'%",
                        node.label == null ? "" : node.label + "_%'LEVEL'%:",
                        "%'+RECORD-FIELD-VALUES'%",
                        recordSetArgs,
                        "%'LEVEL'%",
                        Integer.toString(node.record.scope.level),
                        "%'+STATEMENTS'%",
                        visitNodeList(node.stmts));
        if (node.loopOptimizable == null || node.loopOptimizable.isEmpty()) {
            return ret;
        } else {
            return wrapWithStmtDeclareAndClose(node, ret);
        }
    }

    // -------------------------------------------------------------------------
    // StmtGlobalProcCall
    //

    private static String[] tmplStmtGlobalProcCall =
            new String[] {
                "new Object() { // global procedure call: %'PROC-NAME'%",
                "  void invoke(%'PARAMETERS'%) throws Exception {",
                "    try {",
                "      String dynSql = \"%'DYNAMIC-SQL'%\";",
                "      CallableStatement stmt = conn.prepareCall(dynSql);",
                "      %'+SET-GLOBAL-PROC-ARGS'%",
                "      stmt.execute();",
                "      %'+UPDATE-GLOBAL-PROC-OUT-ARGS'%",
                "      stmt.close();",
                "    } catch (SQLException e) {",
                "      Server.log(e);",
                "      throw new SQL_ERROR(e.getMessage());",
                "    }",
                "  }",
                "}.invoke(",
                "  %'+ARGUMENTS'%",
                ");"
            };

    @Override
    public CodeToResolve visitStmtGlobalProcCall(StmtGlobalProcCall node) {

        assert node.decl != null;

        int argSize = node.args.nodes.size();
        String dynSql = String.format("call %s(%s)", node.name, getQuestionMarks(argSize));
        String wrapperParam = getCallWrapperParam(argSize, node.args, node.decl.paramList);
        GlobalCallCodeSnippets code =
                getGlobalCallCodeSnippets(argSize, 1, node.args, node.decl.paramList);

        return new CodeTemplate(
                "StmtGlobalProcCall",
                Misc.getLineColumnOf(node.ctx),
                tmplStmtGlobalProcCall,
                "%'PROC-NAME'%",
                node.name,
                "%'DYNAMIC-SQL'%",
                dynSql,
                "%'PARAMETERS'%",
                wrapperParam,
                "%'+SET-GLOBAL-PROC-ARGS'%",
                code.setArgs,
                "%'+UPDATE-GLOBAL-PROC-OUT-ARGS'%",
                code.updateOutArgs,
                "%'+ARGUMENTS'%",
                visitArguments(node.args, node.decl.paramList, false));
    }

    // -------------------------------------------------------------------------
    // StmtIf
    //

    private static String[] tmplStmtIfWithoutElse = new String[] {"%'+COND-PARTS'%"};

    private static String[] tmplStmtIfWithElse =
            new String[] {"%'+COND-PARTS'% else {", "  %'+ELSE-PART'%", "}"};

    @Override
    public CodeToResolve visitStmtIf(StmtIf node) {
        if (node.forIfStmt && node.elsePart == null) {

            return new CodeTemplate(
                    "StmtIf",
                    Misc.UNKNOWN_LINE_COLUMN,
                    tmplStmtIfWithoutElse,
                    "%'+COND-PARTS'%",
                    visitNodeList(node.condStmtParts).setDelimiter(" else"));
        } else {

            Object elsePart =
                    node.elsePart == null
                            ? "throw new CASE_NOT_FOUND();"
                            : visitNodeList(node.elsePart);

            return new CodeTemplate(
                    "StmtIf",
                    Misc.getLineColumnOf(node.ctx),
                    tmplStmtIfWithElse,
                    "%'+COND-PARTS'%",
                    visitNodeList(node.condStmtParts).setDelimiter(" else"),
                    "%'+ELSE-PART'%",
                    elsePart);
        }
    }

    // -------------------------------------------------------------------------
    // StmtLocalProcCall
    //

    private static String[] tmplStmtLocalProcCall =
            new String[] {
                "new Object() { // local procedure call: %'PROC-NAME'%",
                "  void invoke(%'PARAMETERS'%) throws Exception {",
                "    %'+ALLOC-COERCED-OUT-ARGS'%",
                "    %'BLOCK'%%'PROC-NAME'%(%'ARGS'%);",
                "    %'+UPDATE-OUT-ARGS'%",
                "  }",
                "}.invoke(",
                "  %'+ARGUMENTS'%",
                ");"
            };

    @Override
    public CodeToResolve visitStmtLocalProcCall(StmtLocalProcCall node) {

        assert node.decl != null;

        int paramSize = node.decl.paramList.nodes.size();
        String wrapperParam = getCallWrapperParam(paramSize, node.args, node.decl.paramList);
        LocalCallCodeSnippets code =
                getLocalCallCodeSnippets(paramSize, node.args, node.decl.paramList);
        String block = node.prefixDeclBlock ? node.decl.scope().block + "." : "";

        return Misc.isEmpty(node.decl.paramList)
                ? new CodeTemplate(
                        "StmtLocalProcCall", Misc.UNKNOWN_LINE_COLUMN, block + node.name + "();")
                : new CodeTemplate(
                        "StmtLocalProcCall",
                        Misc.UNKNOWN_LINE_COLUMN,
                        tmplStmtLocalProcCall,
                        "%'BLOCK'%",
                        block,
                        "%'PROC-NAME'%",
                        node.name,
                        "%'PARAMETERS'%",
                        wrapperParam,
                        "%'+ALLOC-COERCED-OUT-ARGS'%",
                        code.allocCoercedOutArgs,
                        "%'ARGS'%",
                        code.argsToLocal,
                        "%'+UPDATE-OUT-ARGS'%",
                        code.updateOutArgs,
                        "%'+ARGUMENTS'%",
                        visitArguments(node.args, node.decl.paramList, true));
    }

    @Override
    public CodeToResolve visitStmtNull(StmtNull node) {
        return new CodeTemplate("StmtNull", Misc.UNKNOWN_LINE_COLUMN, ";");
    }

    // -------------------------------------------------------------------------
    // StmtOpenFor
    //

    private static String[] tmplStmtOpenForWithHV =
            new String[] {
                "{ // open-for statement",
                "  %'REF-CURSOR'% = new Query(%'QUERY'%);",
                "  %'REF-CURSOR'%.open(conn,",
                "    %'+HOST-EXPRS'%);",
                "}"
            };

    private static String[] tmplStmtOpenForWithoutHV =
            new String[] {
                "{ // open-for statement",
                "  %'REF-CURSOR'% = new Query(%'QUERY'%);",
                "  %'REF-CURSOR'%.open(conn);",
                "}"
            };

    @Override
    public CodeToResolve visitStmtOpenFor(StmtOpenFor node) {

        if (node.staticSql.hostExprs.size() == 0) {
            return new CodeTemplate(
                    "StmtOpenFor",
                    Misc.getLineColumnOf(node.ctx),
                    tmplStmtOpenForWithoutHV,
                    "%'REF-CURSOR'%",
                    node.id.javaCode(),
                    "%'QUERY'%",
                    '"' + node.staticSql.rewritten + '"');
        } else {

            CodeTemplateList hostExprs = new CodeTemplateList();
            for (Expr e : node.staticSql.hostExprs.keySet()) {
                hostExprs.addElement((CodeTemplate) visit(e));
            }

            return new CodeTemplate(
                    "StmtOpenFor",
                    Misc.getLineColumnOf(node.ctx),
                    tmplStmtOpenForWithHV,
                    "%'REF-CURSOR'%",
                    node.id.javaCode(),
                    "%'QUERY'%",
                    '"' + node.staticSql.rewritten + '"',
                    "%'+HOST-EXPRS'%",
                    hostExprs.setDelimiter(","));
        }
    }

    @Override
    public CodeToResolve visitStmtRaise(StmtRaise node) {

        String code;
        if (node.exName == null) {
            code = "throw e" + node.exHandlerDepth + ";";
        } else {
            String block = node.exName.prefixDeclBlock ? node.exName.decl.scope().block + "." : "";
            code = String.format("throw %s new %s();", block, node.exName.name);
        }

        return new CodeTemplate("StmtRaise", Misc.getLineColumnOf(node.ctx), code);
    }

    // -------------------------------------------------------------------------
    // StmtRaiseAppErr
    //

    private static String[] tmplStmtRaiseAppErr =
            new String[] {"throw new $APP_ERROR(", "  %'+ERR-CODE'%,", "  %'+ERR-MSG'%);"};

    @Override
    public CodeToResolve visitStmtRaiseAppErr(StmtRaiseAppErr node) {

        return new CodeTemplate(
                "StmtRaiseAppErr",
                Misc.getLineColumnOf(node.ctx),
                tmplStmtRaiseAppErr,
                "%'+ERR-CODE'%",
                visit(node.errCode),
                "%'+ERR-MSG'%",
                visit(node.errMsg));
    }

    // -------------------------------------------------------------------------
    // StmtReturn
    //

    private static String[] tmplStmtReturn = new String[] {"return", "  %'+RETVAL'%;"};

    @Override
    public CodeToResolve visitStmtReturn(StmtReturn node) {
        if (node.retVal == null) {
            return new CodeTemplate("StmtReturn", Misc.UNKNOWN_LINE_COLUMN, "return;");
        } else {
            return new CodeTemplate(
                    "StmtReturn",
                    Misc.UNKNOWN_LINE_COLUMN,
                    tmplStmtReturn,
                    "%'+RETVAL'%",
                    visit(node.retVal));
        }
    }

    // -------------------------------------------------------------------------
    // StmtRollback
    //

    private static String[] tmplStmtRollback =
            new String[] {
                "try {",
                "  conn.rollback();",
                "  sql_rowcount[0] = 0L;",
                "} catch (SQLException e) {",
                "  Server.log(e);",
                "  throw new SQL_ERROR(e.getMessage());",
                "}"
            };

    @Override
    public CodeToResolve visitStmtRollback(StmtRollback node) {
        return new CodeTemplate("StmtRollback", Misc.getLineColumnOf(node.ctx), tmplStmtRollback);
    }

    // -------------------------------------------------------------------------
    // StmtWhileLoop
    //

    private static String[] tmplStmtWhileLoop =
            new String[] {
                "%'OPT-LABEL'%",
                "while (Boolean.TRUE.equals(",
                "    %'+EXPRESSION'%)) {",
                "  %'+STATEMENTS'%",
                "}"
            };

    @Override
    public CodeToResolve visitStmtWhileLoop(StmtWhileLoop node) {

        CodeTemplate ret =
                new CodeTemplate(
                        "StmtWhileLoop",
                        Misc.UNKNOWN_LINE_COLUMN,
                        tmplStmtWhileLoop,
                        "%'OPT-LABEL'%",
                        node.declLabel == null ? "" : node.declLabel.javaCode(),
                        "%'+EXPRESSION'%",
                        node.cond instanceof ExprTrue ? "opNot(Boolean.FALSE)" : visit(node.cond),
                        "%'+STATEMENTS'%",
                        visitNodeList(node.stmts));
        if (node.loopOptimizable == null || node.loopOptimizable.isEmpty()) {
            return ret;
        } else {
            return wrapWithStmtDeclareAndClose(node, ret);
        }
    }

    // -------------------------------------------------------------------------
    // Body
    //

    private static String[] tmplBody =
            new String[] {
                "try {",
                "  try {", // convert every exception from the STATEMENTS into PlcsqlRuntimeError
                "    %'+STATEMENTS'%",
                "  } catch (PlcsqlRuntimeError e) {",
                "    throw e;",
                "  } catch (OutOfMemoryError e) {",
                "    Server.log(e);",
                "    throw new STORAGE_ERROR().initCause(e);",
                "  } catch (Throwable e) {",
                "    Server.log(e);",
                "    throw new PROGRAM_ERROR().initCause(e);",
                "  }",
                "}",
                "%'+CATCHES'%"
            };

    @Override
    public CodeToResolve visitBody(Body node) {

        return node.exHandlers.nodes.size() == 0
                ? visitNodeList(node.stmts)
                : new CodeTemplate(
                        "Body",
                        Misc.UNKNOWN_LINE_COLUMN,
                        tmplBody,
                        "%'+STATEMENTS'%",
                        visitNodeList(node.stmts),
                        "%'+CATCHES'%",
                        visitNodeList(node.exHandlers));
    }

    // -------------------------------------------------------------------------
    // ExHandler
    //

    private static String[] tmplExHandler =
            new String[] {"catch (%'EXCEPTIONS'% e%'DEPTH'%) {", "  %'+STATEMENTS'%", "}"};

    @Override
    public CodeToResolve visitExHandler(ExHandler node) {

        boolean first = true;
        StringBuffer sbuf = new StringBuffer();
        for (ExName ex : node.exNames) {

            if (first) {
                first = false;
            } else {
                sbuf.append(" | ");
            }

            if ("OTHERS".equals(ex.name)) {
                sbuf.append("PlcsqlRuntimeError");
            } else if (ex.prefixDeclBlock) {
                sbuf.append("Decl_of_" + ex.decl.scope().block + "." + ex.name);
            } else {
                sbuf.append(ex.name);
            }
        }

        return new CodeTemplate(
                "ExHandler",
                Misc.UNKNOWN_LINE_COLUMN,
                tmplExHandler,
                "%'EXCEPTIONS'%",
                sbuf.toString(),
                "%'DEPTH'%",
                Integer.toString(node.depth),
                "%'+STATEMENTS'%",
                visitNodeList(node.stmts));
    }

    @Override
    public CodeToResolve visitExName(ExName node) {
        // depends on the context in which this node is located and must not be called directly
        throw new RuntimeException("unreachable");
    }

    @Override
    public CodeToResolve visitTypeSpec(TypeSpec node) {
        // TypeSpecs are not visited
        throw new RuntimeException("unreachable");
    }

    @Override
    public CodeToResolve visitTypeSpecPercent(TypeSpecPercent node) {
        // TypeSpecs are not visited
        throw new RuntimeException("unreachable");
    }

    // -------------------------------------------------------------------------
    // CaseExpr
    //

    private static String[] tmplCaseExpr =
            new String[] {
                "Boolean.TRUE.equals(opEq%'OP-EXTENSION'%(selector,",
                "    %'+VALUE'%)) ?",
                "  %'+EXPRESSION'% :"
            };

    @Override
    public CodeToResolve visitCaseExpr(CaseExpr node) {

        return new CodeTemplate(
                "CaseExpr",
                Misc.getLineColumnOf(node.ctx),
                tmplCaseExpr,
                "%'OP-EXTENSION'%",
                node.opExtension,
                "%'+VALUE'%",
                visit(node.val),
                "%'+EXPRESSION'%",
                visit(node.expr));
    }

    // -------------------------------------------------------------------------
    // CaseStmt
    //

    private static String[] tmplCaseStmt =
            new String[] {
                "if (Boolean.TRUE.equals(opEq%'OP-EXTENSION'%(selector_%'LEVEL'%,",
                "    %'+VALUE'%))) {",
                "  %'+STATEMENTS'%",
                "}"
            };

    @Override
    public CodeToResolve visitCaseStmt(CaseStmt node) {

        return new CodeTemplate(
                "CaseStmt",
                Misc.getLineColumnOf(node.ctx),
                tmplCaseStmt,
                "%'OP-EXTENSION'%",
                node.opExtension,
                "%'+VALUE'%",
                visit(node.val),
                "%'+STATEMENTS'%",
                visitNodeList(node.stmts));
    }

    // -------------------------------------------------------------------------
    // CondExpr
    //

    private static String[] tmplCondExpr =
            new String[] {"Boolean.TRUE.equals(", "    %'+CONDITION'%) ?", "  %'+EXPRESSION'% :"};

    @Override
    public CodeToResolve visitCondExpr(CondExpr node) {

        return new CodeTemplate(
                "CondExpr",
                Misc.UNKNOWN_LINE_COLUMN,
                tmplCondExpr,
                "%'+CONDITION'%",
                visit(node.cond),
                "%'+EXPRESSION'%",
                visit(node.expr));
    }

    // -------------------------------------------------------------------------
    // CondStmt
    //

    private static String[] tmplCondStmt =
            new String[] {
                "if (Boolean.TRUE.equals(", "    %'+CONDITION'%)) {", "  %'+STATEMENTS'%", "}"
            };

    @Override
    public CodeToResolve visitCondStmt(CondStmt node) {

        return new CodeTemplate(
                "CondStmt",
                Misc.UNKNOWN_LINE_COLUMN,
                tmplCondStmt,
                "%'+CONDITION'%",
                visit(node.cond),
                "%'+STATEMENTS'%",
                visitNodeList(node.stmts));
    }

    interface CodeToResolve {
        void resolve(int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers);
    }

    // -----------------------------------------------------------------
    // Private
    // -----------------------------------------------------------------

    private static final int[] POSITION_IGNORED = new int[] {-1, -1};
    private static final String[] DUMMY_STRING_ARRAY = new String[0];

    // -----------------------------------------------------------------

    private static String[] tmplDupCursorArg =
            new String[] {"Object a%'INDEX'%_%'LEVEL'% =", "  %'+ARG'%;"};

    private Object getDupCursorArgs(NodeList<Expr> args, int[] paramRefCounts) {

        CodeTemplateList ret = new CodeTemplateList();

        int size = paramRefCounts.length;
        for (int i = 0; i < size; i++) {

            if (paramRefCounts[i] > 1) {

                Expr arg = args.nodes.get(i);
                ret.addElement(
                        new CodeTemplate(
                                "duplicate cursor argument",
                                Misc.UNKNOWN_LINE_COLUMN,
                                tmplDupCursorArg,
                                "%'INDEX'%",
                                Integer.toString(i),
                                "%'+ARG'%",
                                visit(arg)));
            }
        }

        return ret.elements.isEmpty() ? "" : ret;
    }

    public CodeTemplateList getHostExprs(
            ExprId cursor, NodeList<Expr> args, int[] paramNumOfHostExpr, int[] paramRefCounts) {

        int size = paramNumOfHostExpr.length;
        assert size > 0;

        DeclCursor decl = (DeclCursor) cursor.decl;
        ArrayList<Expr> hostExprs = new ArrayList<>(decl.staticSql.hostExprs.keySet());
        assert size == hostExprs.size();

        CodeTemplateList ret = new CodeTemplateList();
        for (int i = 0; i < size; i++) {

            int m = paramNumOfHostExpr[i];
            if (m > 0) {
                int k = m - 1;
                if (paramRefCounts[k] > 1) {
                    // parameter-k appears more than once in the select statement
                    ret.addElement(
                            new CodeTemplate(
                                    "dup cursor argument for host expressions",
                                    Misc.UNKNOWN_LINE_COLUMN, // inserted by the compiler
                                    "a" + k + "_%'LEVEL'%"));
                } else {
                    assert paramRefCounts[k] == 1;
                    ret.addElement((CodeTemplate) visit(args.nodes.get(k)));
                }
            } else {
                Expr e = hostExprs.get(i);
                if (e instanceof ExprId) {
                    ExprId var = (ExprId) e;
                    assert var.decl != null;
                    var.prefixDeclBlock = var.decl.scope().declDone;
                }
                ret.addElement((CodeTemplate) visit(e));
            }
        }

        return ret.setDelimiter(",");
    }

    private CodeTemplateList visitArguments(
            NodeList<Expr> args, NodeList<DeclParam> paramList, boolean local) {

        assert args != null;
        assert paramList != null;

        int paramCnt = paramList.nodes.size();
        int argsCnt = args.nodes.size();
        assert argsCnt <= paramCnt;

        CodeTemplateList ret = new CodeTemplateList();

        int i = 0;
        for (Expr a : args.nodes) {

            CodeTemplate tmpl;
            DeclParam dp = paramList.nodes.get(i);
            if (dp instanceof DeclParamOut) {
                assert a instanceof ExprId; // by a previous check
                tmpl =
                        new CodeTemplate(
                                "to OUT",
                                Misc.UNKNOWN_LINE_COLUMN,
                                ((ExprId) a).javaCodeForOutParam());
            } else {
                tmpl = (CodeTemplate) visit(a);
            }

            ret.addElement(tmpl);
            i++;
        }
        if ((argsCnt < paramCnt) && local) {
            while (i < paramCnt) {

                DeclParam dp = paramList.nodes.get(i);
                assert dp.hasDefault();
                DeclParamIn dpi = (DeclParamIn) dp;
                ret.addElement((CodeTemplate) visit(dpi.defaultVal));

                i++;
            }
        }

        return ret.setDelimiter(",");
    }

    private static String[] getNullifyOutParamCode(NodeList<DeclParam> paramList) {

        List<String> ret = new LinkedList<>();

        for (DeclParam dp : paramList.nodes) {
            if (dp instanceof DeclParamOut && !((DeclParamOut) dp).alsoIn) {
                if (dp.typeSpec.type instanceof TypeRecord) {
                    ret.add(String.format("%s[0].setNull(null);", ((DeclParamOut) dp).name));
                } else {
                    ret.add(String.format("%s[0] = null;", ((DeclParamOut) dp).name));
                }
            }
        }

        return ret.toArray(DUMMY_STRING_ARRAY);
    }

    private static String getQuestionMarks(int n) {
        StringBuffer sbuf = new StringBuffer();
        boolean first = true;
        for (int i = 0; i < n; i++) {

            if (first) {
                first = false;
            } else {
                sbuf.append(", ");
            }

            sbuf.append("?");
        }

        return sbuf.toString();
    }

    // -------------------------------------------------------------------------
    // set used values
    //

    private static final String[] tmplSetObject =
            new String[] {"pstmt_%'SQL-SERIAL-NO'%.setObject(%'INDEX'%,", "  %'+VALUE'%", ");"};

    private Object getSetUsedExpr(List<? extends Expr> exprList) {

        if (exprList == null || exprList.size() == 0) {
            return "";
        }

        CodeTemplateList ret = new CodeTemplateList();

        int size = exprList.size();
        for (int i = 0; i < size; i++) {
            Expr expr = exprList.get(i);

            CodeTemplate tmpl =
                    new CodeTemplate(
                            "used values",
                            Misc.getLineColumnOf(expr.ctx),
                            tmplSetObject,
                            "%'INDEX'%",
                            Integer.toString(i + 1),
                            "%'+VALUE'%",
                            visit(expr));
            ret.addElement(tmpl);
        }

        return ret;
    }

    // -------------------------------------------------------------------------
    //

    private static final String[] tmplDeclBlock =
            new String[] {
                "class Decl_of_%'BLOCK'% {",
                "  Decl_of_%'BLOCK'%() throws Exception {};",
                "  %'+DECLARATIONS'%",
                "}",
                "Decl_of_%'BLOCK'% %'BLOCK'% = new Decl_of_%'BLOCK'%();"
            };

    private static final String[] tmplCastCoercion = new String[] {"(%'TYPE'%)", "  %'+EXPR'%"};
    private static final String[] tmplConvCoercion =
            new String[] {"conv%'SRC-TYPE'%To%'DST-TYPE'%(", "  %'+EXPR'%)"};
    private static final String[] tmplCoerceAndCheckPrec =
            new String[] {"checkPrecision(%'PREC'%, (short) %'SCALE'%,", "  %'+EXPR'%)"};
    private static final String[] tmplCoerceAndCheckStrLength =
            new String[] {"checkStrLength(%'IS-CHAR'%, %'LENGTH'%,", "  %'+EXPR'%)"};
    private static final String[] tmplNullToRecord =
            new String[] {
                "new %'DST-RECORD'%().setNull(", "  %'+EXPR'%)",
            };
    private static final String[] tmplRecordToRecord =
            new String[] {
                "setFieldsOf%'SRC-RECORD'%_To_%'DST-RECORD'%(",
                "  %'+EXPR'%,",
                "  new %'DST-RECORD'%())"
            };

    private CodeToResolve applyCoercion(Coercion c, CodeTemplate exprCode, ParserRuleContext ctx) {

        if (c == null || c instanceof Coercion.Identity) {
            return exprCode;
        } else {

            if (c instanceof Coercion.Cast) {
                Coercion.Cast cast = (Coercion.Cast) c;
                return new CodeTemplate(
                        "cast coercion",
                        Misc.UNKNOWN_LINE_COLUMN,
                        tmplCastCoercion,
                        "%'TYPE'%",
                        getJavaCodeOfType(cast.dst),
                        "%'+EXPR'%",
                        exprCode);
            } else if (c instanceof Coercion.Conversion) {
                Coercion.Conversion conv = (Coercion.Conversion) c;
                int[] exprPlcsqlPos = Misc.getLineColumnOf(ctx);

                return new CodeTemplate(
                        "conversion coercion",
                        exprPlcsqlPos,
                        tmplConvCoercion,
                        "%'SRC-TYPE'%",
                        Type.getTypeByIdx(conv.src.idx).plcName,
                        "%'DST-TYPE'%",
                        Type.getTypeByIdx(conv.dst.idx).plcName,
                        "%'+EXPR'%",
                        exprCode);
            } else if (c instanceof Coercion.CoerceAndCheckPrecision) {
                Coercion.CoerceAndCheckPrecision checkPrec = (Coercion.CoerceAndCheckPrecision) c;
                int[] exprPlcsqlPos = Misc.getLineColumnOf(ctx);

                return new CodeTemplate(
                        "coerce and check precision",
                        exprPlcsqlPos,
                        tmplCoerceAndCheckPrec,
                        "%'PREC'%",
                        Integer.toString(checkPrec.prec),
                        "%'SCALE'%",
                        Short.toString(checkPrec.scale),
                        "%'+EXPR'%",
                        applyCoercion(checkPrec.c, exprCode, ctx));
            } else if (c instanceof Coercion.CoerceAndCheckStrLength) {
                Coercion.CoerceAndCheckStrLength checkStrLen = (Coercion.CoerceAndCheckStrLength) c;
                int[] exprPlcsqlPos = Misc.getLineColumnOf(ctx);

                return new CodeTemplate(
                        "coerce and check precision",
                        exprPlcsqlPos,
                        tmplCoerceAndCheckStrLength,
                        "%'IS-CHAR'%",
                        checkStrLen.isChar ? "true" : "false",
                        "%'LENGTH'%",
                        "" + checkStrLen.length,
                        "%'+EXPR'%",
                        applyCoercion(checkStrLen.c, exprCode, ctx));
            } else if (c instanceof Coercion.NullToRecord) {
                Coercion.NullToRecord nullToRec = (Coercion.NullToRecord) c;
                int[] exprPlcsqlPos = Misc.getLineColumnOf(ctx);

                return new CodeTemplate(
                        "null to record coercion",
                        exprPlcsqlPos,
                        tmplNullToRecord,
                        "%'DST-RECORD'%",
                        nullToRec.dst.javaCode,
                        "%'+EXPR'%",
                        exprCode);
            } else if (c instanceof Coercion.RecordToRecord) {
                Coercion.RecordToRecord recToRec = (Coercion.RecordToRecord) c;
                int[] exprPlcsqlPos = Misc.getLineColumnOf(ctx);

                return new CodeTemplate(
                        "record to record coercion",
                        exprPlcsqlPos,
                        tmplRecordToRecord,
                        "%'SRC-RECORD'%",
                        recToRec.src.javaCode,
                        "%'DST-RECORD'%",
                        recToRec.dst.javaCode,
                        "%'+EXPR'%",
                        exprCode);
            } else {
                throw new RuntimeException("unreachable");
            }
        }
    }

    private static class CodeTemplateList implements CodeToResolve {

        boolean resolved;
        final List<CodeTemplate> elements;
        String delimiter;

        CodeTemplateList() {
            elements = new ArrayList<>();
        }

        void addElement(CodeTemplate element) { // NOTE: CodeTemplate, not CodeToResolve
            assert element != null;
            elements.add(element);
        }

        CodeTemplateList setDelimiter(String delimiter) {
            this.delimiter = delimiter;
            return this;
        }

        public void resolve(
                int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers) {

            assert !resolved : "already resolved";

            if (delimiter == null) {
                for (CodeTemplate t : elements) {
                    t.resolve(indentLevel, codeLines, codeRangeMarkers);
                }
            } else {
                boolean first = true;
                for (CodeTemplate t : elements) {
                    if (first) {
                        first = false;
                    } else {
                        int lastIdx = codeLines.size() - 1;
                        assert lastIdx >= 0;
                        codeLines.set(lastIdx, codeLines.get(lastIdx) + delimiter);
                    }
                    t.resolve(indentLevel, codeLines, codeRangeMarkers);
                }
            }

            resolved = true;
        }
    }

    private String getCallWrapperParam(
            int size, NodeList<Expr> args, NodeList<DeclParam> paramList) {

        if (size == 0) {
            return "";
        }

        StringBuffer sbuf = new StringBuffer();
        boolean first = true;
        for (int i = 0; i < size; i++) {

            DeclParam param = paramList.nodes.get(i);

            if (first) {
                first = false;
            } else {
                sbuf.append(", ");
            }

            if (param instanceof DeclParamOut) {
                ExprId id = (ExprId) args.nodes.get(i);
                DeclIdTypeSpeced declId = (DeclIdTypeSpeced) id.decl;
                sbuf.append(String.format("%s[] o%d", getJavaCodeOfType(declId.typeSpec()), i));
            } else {
                sbuf.append(String.format("%s o%d", getJavaCodeOfType(param.typeSpec), i));
            }
        }

        return sbuf.toString();
    }

    private static class GlobalCallCodeSnippets {
        String[] setArgs;
        String[] updateOutArgs;
    }

    private GlobalCallCodeSnippets getGlobalCallCodeSnippets(
            int size, int argOffset, NodeList<Expr> args, NodeList<DeclParam> paramList) {

        List<String> setArgs = new LinkedList<>();
        List<String> updateOutArgs = new LinkedList<>();

        for (int i = 0; i < size; i++) {

            DeclParam param = paramList.nodes.get(i);

            if (param instanceof DeclParamOut) {

                // fill setArgs
                setArgs.add(
                        String.format(
                                "stmt.registerOutParameter(%d, java.sql.Types.OTHER);",
                                i + argOffset));

                ExprId id = (ExprId) args.nodes.get(i);
                Coercion c = id.coercion;
                assert c != null;

                if (((DeclParamOut) param).alsoIn) {
                    String paramVal = "o" + i + "[0]";
                    setArgs.add(
                            String.format(
                                    "stmt.setObject(%d, %s);",
                                    i + argOffset, c.javaCode(paramVal)));
                }

                // fill updateOutArgs
                Coercion cRev = c.getReversion(iStore);
                assert cRev != null; // by earlier check
                String outVal =
                        String.format(
                                "(%s) stmt.getObject(%d)",
                                getJavaCodeOfType(param.typeSpec), i + argOffset);
                updateOutArgs.add(String.format("o%d[0] = %s;", i, cRev.javaCode(outVal)));

                DeclId declId = id.decl;
                if (declId instanceof DeclVar && ((DeclVar) declId).notNull) {
                    updateOutArgs.add(
                            String.format(
                                    "checkNotNull(o%d[0], \"a not-null variable %s was set NULL by this call\");",
                                    i, id.name));
                }
            } else {
                setArgs.add(String.format("stmt.setObject(%d, o%d);", i + argOffset, i));
            }
        }

        GlobalCallCodeSnippets ret = new GlobalCallCodeSnippets();
        ret.setArgs = setArgs.toArray(DUMMY_STRING_ARRAY);
        ret.updateOutArgs = updateOutArgs.toArray(DUMMY_STRING_ARRAY);
        return ret;
    }

    private static class LocalCallCodeSnippets {
        String[] allocCoercedOutArgs;
        String argsToLocal;
        String[] updateOutArgs;
    }

    private LocalCallCodeSnippets getLocalCallCodeSnippets(
            int size, NodeList<Expr> args, NodeList<DeclParam> paramList) {

        List<String> allocCoercedOutArgs = new LinkedList<>();
        StringBuilder argsToLocal = new StringBuilder();
        List<String> update = new LinkedList<>();

        boolean first = true;
        for (int i = 0; i < size; i++) {

            if (first) {
                first = false;
            } else {
                argsToLocal.append(", ");
            }

            DeclParam param = paramList.nodes.get(i);

            if (param instanceof DeclParamOut) {

                ExprId id = (ExprId) args.nodes.get(i);

                Coercion c = id.coercion;
                assert c != null;
                if (c instanceof Coercion.Identity) {
                    argsToLocal.append("o" + i);
                } else {
                    String paramType = getJavaCodeOfType(param.typeSpec);
                    allocCoercedOutArgs.add(
                            String.format(
                                    "%s[] p%d = new %s[] { %s };",
                                    paramType, i, paramType, c.javaCode("o" + i + "[0]")));

                    argsToLocal.append("p" + i);

                    Coercion cRev = c.getReversion(iStore);
                    assert cRev != null; // by earlier check
                    update.add(String.format("o%d[0] = %s;", i, cRev.javaCode("p" + i + "[0]")));
                }

                DeclId declId = id.decl;
                if (declId instanceof DeclVar && ((DeclVar) declId).notNull) {
                    update.add(
                            String.format(
                                    "checkNotNull(o%d[0], \"a not-null variable %s was set NULL by this function call\");",
                                    i, id.name));
                }
            } else {
                argsToLocal.append("o" + i);
            }
        }

        LocalCallCodeSnippets ret = new LocalCallCodeSnippets();
        ret.allocCoercedOutArgs = allocCoercedOutArgs.toArray(DUMMY_STRING_ARRAY);
        ret.argsToLocal = argsToLocal.toString();
        ret.updateOutArgs = update.toArray(DUMMY_STRING_ARRAY);
        return ret;
    }

    private static class CodeTemplate implements CodeToResolve {

        boolean resolved;

        int[] plcsqlPos; // not final: can be cleared later

        final String astNode;
        final String[] template;
        final LinkedHashMap<String, Object> substitutions = new LinkedHashMap<>();
        // key (String) - template hole name
        // value (Object) - String, String[] or CodeToResolve to fill the hole

        CodeTemplate(String astNode, int[] plcsqlPos, String template, Object... pairs) {
            this(astNode, plcsqlPos, new String[] {template}, pairs);
        }

        CodeTemplate(String astNode, int[] plcsqlPos, String[] template, Object... pairs) {

            assert plcsqlPos != null && plcsqlPos.length == 2;
            assert template != null;

            this.astNode = astNode;
            this.plcsqlPos = plcsqlPos;

            int plcsqlLine = plcsqlPos[0];
            int plcsqlColumn = plcsqlPos[1];

            assert (plcsqlLine < 0 && plcsqlColumn < 0) || (plcsqlLine > 0 && plcsqlColumn > 0)
                    : String.format(
                            "%s - line and column numbers of code templates must be positive integers: (%d, %d)",
                            astNode, plcsqlLine, plcsqlColumn);

            for (String s : template) {
                assert s != null;
            }
            this.template = template;

            int len = pairs.length;
            assert len % 2 == 0
                    : astNode
                            + " - the number of substitution pairs elements must be an even number";
            for (int i = 0; i < len; i += 2) {

                assert pairs[i] instanceof String
                        : astNode + " - first element of each pair must be a String: " + pairs[i];
                String hole = (String) pairs[i];
                assert hole.startsWith("%'") && hole.endsWith("'%")
                        : astNode
                                + " - first element of each pair must indicate a hole: "
                                + pairs[i];

                Object thing = pairs[i + 1];
                if (thing instanceof String
                        || thing instanceof String[]
                        || thing instanceof CodeToResolve) {
                    // String is for a small hole, and the String[] and CodeToResolve are for a big
                    // hole
                    this.substitutions.put(hole, thing);
                } else {
                    throw new RuntimeException("unreachable");
                }
            }
        }

        public void resolve(
                int indentLevel, List<String> codeLines, StringBuilder codeRangeMarkers) {

            assert !resolved : "already resolved";

            // Critical Condition:
            // The range (start and end line numbers) of code of an AST node resolved by this method
            // does not change
            // once this method is done for the AST node.

            boolean markCodeRange = plcsqlPos[0] > 0; // line > 0
            if (markCodeRange) {
                codeRangeMarkers.append(
                        String.format(
                                " (%d,%d,%d", codeLines.size() + 1, plcsqlPos[0], plcsqlPos[1]));
            }

            for (String line : template) {
                assert line.indexOf("\n") == -1
                        : "every line of a templates must be a single line: '" + line + "'";
                resolveTemplateLine(line, indentLevel, codeLines, codeRangeMarkers);
            }

            if (markCodeRange) {
                codeRangeMarkers.append(String.format(" )%d", codeLines.size() + 1));
            }

            resolved = true;
        }

        // -----------------------------------------------
        // Private
        // -----------------------------------------------

        private String substituteSmallHolesInLine(String line) {

            for (String hole : substitutions.keySet()) {
                if (!isBigHole(hole)) {
                    Object substitute = substitutions.get(hole);
                    if (substitute instanceof String) {
                        line = line.replace(hole, (String) substitute);
                    } else {
                        throw new RuntimeException(
                                String.format(
                                        "unreachable: non-string (%s) substitute for a small hole '%s'",
                                        substitute.getClass().getSimpleName(), hole));
                    }
                }
            }

            return line;
        }

        private void resolveTemplateLine(
                String line,
                int indentLevel,
                List<String> codeLines,
                StringBuilder codeRangeMarkers) {

            assert line != null;

            assert !line.endsWith(" ")
                    : "a template line may not have a trailing space: '" + line + "'";

            Set<String> smallHoles = new HashSet<>();
            String bigHole = getHoles(smallHoles, line);
            if (bigHole == null) {

                // case 1: word replacements in a single line (namely, small holes)

                if (smallHoles.size() > 0) {
                    line = substituteSmallHolesInLine(line);
                }
                if (line.trim().length() > 0) {
                    String indent = Misc.getIndent(indentLevel);
                    codeLines.add(indent + line);
                }
            } else {
                assert smallHoles.size() == 0;

                // case 2: expanded to multiple lines (namely, a single big hole)

                int spaces = line.indexOf(bigHole);
                int indentLevelDelta = spaces / Misc.INDENT_SIZE;
                String indent = Misc.getIndent(indentLevel + indentLevelDelta);

                String remainder = line.substring(spaces + bigHole.length());

                Object substitute = substitutions.get(bigHole);
                assert substitute != null : ("no substitute for a big hole " + bigHole);
                if (substitute instanceof String) {
                    String l = (String) substitute;
                    if (l.indexOf("%'") == -1) {
                        if (l.length() > 0) {
                            codeLines.add(indent + l);
                        }
                    } else {
                        resolveTemplateLine(
                                l, indentLevel + indentLevelDelta, codeLines, codeRangeMarkers);
                    }
                } else if (substitute instanceof String[]) {
                    for (String l : (String[]) substitute) {
                        if (l.indexOf("%'") == -1) {
                            if (l.length() > 0) {
                                codeLines.add(indent + l);
                            }
                        } else {
                            resolveTemplateLine(
                                    l, indentLevel + indentLevelDelta, codeLines, codeRangeMarkers);
                        }
                    }
                } else if (substitute instanceof CodeToResolve) {
                    int startLineIdx = codeLines.size();

                    ((CodeToResolve) substitute)
                            .resolve(indentLevel + indentLevelDelta, codeLines, codeRangeMarkers);

                    // Replace small holes, if any, returned from the subnodes.
                    // NOTE: Big holes do not exist in the code resolved by subnodes because the
                    // code templates are
                    //       written so. Otherwise, the code range of sub-subnodes, if any, should
                    // be altered.
                    int upper = codeLines.size();
                    for (int i = startLineIdx; i < upper; i++) {

                        String l = codeLines.get(i);
                        smallHoles.clear();
                        bigHole = getHoles(smallHoles, l);
                        assert bigHole == null
                                : "a line resolved by a subnode has a big hole: '" + l + "'";
                        // this condition is critical for the code range markers not to change once
                        // settled.
                        if (smallHoles.size() > 0) {
                            l = substituteSmallHolesInLine(l);
                            codeLines.set(i, l);
                        }
                    }

                } else {
                    throw new RuntimeException("unreachable: substitute=" + substitute);
                }

                // Append the remainder, if any, to the last line.
                // This is mainly for the commas after expressions.
                if (remainder.length() > 0) {
                    int lastLineIndex = codeLines.size() - 1;
                    String lastLine = codeLines.get(lastLineIndex);
                    codeLines.set(lastLineIndex, lastLine + remainder);
                }
            }
        }

        private static boolean isBigHole(String hole) {
            return hole.startsWith("%'+");
        }

        // collect small holes into 'holes' and return null.
        // Or return a big hole immediately if one found
        private static String getHoles(Set<String> holes, String line) {

            int i = 0;
            int len = line.length();
            boolean first = true;
            while (i < len) {
                int begin = line.indexOf("%'", i);
                if (begin == -1) {
                    return null; // no more holes
                }
                int end = line.indexOf("'%", begin + 2);
                if (end == -1) {
                    return null; // no more holes
                }

                if (end == begin + 2) {
                    // %''%
                    i = begin + 3;
                    continue; // not a hole
                }

                int j = begin + 2;
                if (line.charAt(j) == '+') {
                    // this is possible for a big hole
                    j++;
                }
                // Hole names can consist of dashes and capital letters
                for (; j < end; j++) {
                    char c = line.charAt(j);
                    if (c == '-' || (c >= 'A' && c <= 'Z')) {
                        // OK
                    } else {
                        break;
                    }
                }
                if (j < end) {
                    i = j;
                    continue; // not a hole
                }

                i = end + 2;

                String hole = line.substring(begin, i);
                if (first) {
                    first = false;
                    if (isBigHole(hole)) {
                        for (j = 0; j < begin; j++) {
                            assert line.charAt(j) == ' '
                                    : "only spaces allowed before a big hole: '" + line + "'";
                        }
                        assert line.indexOf("%'", i) == -1
                                : "no more holes after a big hole: '" + line + "'";

                        return hole;
                    }
                } else {
                    assert !isBigHole(hole)
                            : "big holes must be the only hole in the line: '" + hole + "'";
                }

                holes.add(hole);
            }

            return null;
        }
    }

    private List<String> getRecordFieldsDeclCode(List<Misc.Pair<String, Type>> selectList) {

        List<String> lines = new LinkedList<>();
        for (Misc.Pair<String, Type> f : selectList) {
            String tyJava = getJavaCodeOfType(f.e2);
            lines.add(String.format("  %1$s[] %2$s = new %1$s[1];", tyJava, f.e1));
        }

        return lines;
    }

    private List<String> getSetParamCode(List<Misc.Pair<String, Type>> selectList) {

        List<String> lines = new LinkedList<>();
        for (Misc.Pair<String, Type> f : selectList) {
            lines.add(f.e2.javaCode + " " + f.e1);
        }

        return lines;
    }

    private List<String> getRecordDeclCode(TypeRecord rec) {

        List<String> lines = new LinkedList<>();

        lines.add("private static class " + rec.javaCode + " {");

        List<String> fieldDecls = getRecordFieldsDeclCode(rec.selectList);
        lines.addAll(fieldDecls);

        // set method
        List<String> setParams = getSetParamCode(rec.selectList);
        lines.add(String.format("  %s set(%s) {", rec.javaCode, String.join(", ", setParams)));
        for (Misc.Pair<String, Type> f : rec.selectList) {
            lines.add(String.format("    this.%1$s[0] = %1$s;", f.e1));
        }
        lines.add(String.format("    return this;"));
        lines.add("  }");

        // setNull method
        lines.add(
                String.format(
                        "  %s setNull(Object dummy) {",
                        rec.javaCode)); // o: to take null expression
        for (Misc.Pair<String, Type> f : rec.selectList) {
            lines.add(String.format("    this.%s[0] = null;", f.e1));
        }
        lines.add(String.format("    return this;"));
        lines.add("  }");

        lines.add("}");

        if (rec.generateEq) {
            lines.add(
                    String.format(
                            "private static boolean opEq%1$s(%1$s l, %1$s r) {", rec.javaCode));
            int i = 0;
            for (Misc.Pair<String, Type> f : rec.selectList) {
                lines.add(
                        String.format(
                                "  %1$s Objects.equals(l.%2$s[0], r.%2$s[0])",
                                (i == 0 ? "return" : "  &&"), f.e1));
                i++;
            }
            lines.add("  ;");
            lines.add("}");
        }

        return lines;
    }

    // ------------------------------------------------------
    //

    private static String[] tmplLoopOptimizable =
            new String[] {
                "// declaring PreparedStatement variables out of the loop below",
                "%'+DECLARE-STATEMENTS'%",
                "%'+LOOP'%",
                "// closing PreparedStatement objects out of the loop above",
                "%'+CLOSE-STATEMENTS'%"
            };

    private CodeToResolve wrapWithStmtDeclareAndClose(StmtLoop node, CodeToResolve loop) {

        CodeTemplateList decls = new CodeTemplateList();
        CodeTemplateList closes = new CodeTemplateList();

        assert !node.loopOptimizable.sql.isEmpty();
        for (StmtSql sql : node.loopOptimizable.sql) {
            String decl = String.format("PreparedStatement pstmt_%d = null;", sql.sqlSerialNo);
            decls.addElement(
                    new CodeTemplate("StatementDeclMoved", Misc.UNKNOWN_LINE_COLUMN, decl));

            String close =
                    String.format(
                            "if (pstmt_%1$d != null) { pstmt_%1$d.close(); }", sql.sqlSerialNo);
            closes.addElement(
                    new CodeTemplate("StatementCloseMoved", Misc.UNKNOWN_LINE_COLUMN, close));
        }

        return new CodeTemplate(
                "LoopWithOptimizable",
                Misc.getLineColumnOf(node.ctx),
                tmplLoopOptimizable,
                "%'+DECLARE-STATEMENTS'%",
                decls,
                "%'+LOOP'%",
                loop,
                "%'+CLOSE-STATEMENTS'%",
                closes);
    }
}
