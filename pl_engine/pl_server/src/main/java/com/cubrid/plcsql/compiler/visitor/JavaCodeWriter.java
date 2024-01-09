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
import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.ast.*;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Set;

public class JavaCodeWriter extends AstVisitor<JavaCodeWriter.CodeToResolve> {

    public List<String> codeLines = new ArrayList<>(); // no LinkedList : frequent access by indexes
    public StringBuilder codeRangeMarkers = new StringBuilder();

    public String buildCodeLines(Unit unit) {

        CodeToResolve ctr = visitUnit(unit);
        ctr.resolve(0, codeLines, codeRangeMarkers);

        codeLines.add(
                "  private static List<CodeRangeMarker> codeRangeMarkerList = buildCodeRangeMarkerList(\""
                        + codeRangeMarkers
                        + "\");");
        codeLines.add("}");

        return String.join("\n", codeLines.toArray(DUMMY_STRING_ARRAY));
    }

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
                "  }"
            };

    @Override
    public CodeToResolve visitUnit(Unit node) {

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

        // imports
        String[] importsArray = node.getImportsArray();
        int len = importsArray.length;
        for (int i = 0; i < len; i++) {
            importsArray[i] = "import " + importsArray[i] + ";";
        }

        // parameters
        Object strParamArr =
                Misc.isEmpty(node.routine.paramList)
                        ? ""
                        : visitNodeList(node.routine.paramList).setDelimiter(",");

        // nullify OUT parameters
        String[] strNullifyOutParam = getNullifyOutParamCode(node.routine.paramList);

        return new CodeTemplate(
                "Unit",
                new int[] {1, 1},
                tmplUnit,
                "%'+IMPORTS'%",
                importsArray,
                "%'CLASS-NAME'%",
                node.getClassName(),
                "%'RETURN-TYPE'%",
                node.routine.retType == null ? "void" : node.routine.retType.javaCode(),
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
                visit(node.routine.body));
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
                Misc.getLineColumnOf(node.ctx),
                tmplDeclRoutine,
                "%'RETURN-TYPE'%",
                node.retType == null ? "void" : node.retType.javaCode(),
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
        String code = String.format("%s %s", node.typeSpec.javaCode(), node.name);
        return new CodeTemplate("DeclParamIn", Misc.getLineColumnOf(node.ctx), code);
    }

    @Override
    public CodeToResolve visitDeclParamOut(DeclParamOut node) {
        String code = String.format("%s[] %s", node.typeSpec.javaCode(), node.name);
        return new CodeTemplate("DeclParamOut", Misc.getLineColumnOf(node.ctx), code);
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

        String ty = node.typeSpec.javaCode();
        if (node.val == null) {
            String code = String.format("%s[] %s = new %s[] { null };", ty, node.name, ty);
            return new CodeTemplate("DeclVar", Misc.getLineColumnOf(node.ctx), code);
        } else {
            return new CodeTemplate(
                    "DeclVar",
                    Misc.getLineColumnOf(node.ctx),
                    node.notNull ? tmplNotNullVar : tmplNullableVar,
                    "%'TYPE'%",
                    ty,
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
                Misc.getLineColumnOf(node.ctx),
                node.notNull ? tmplNotNullConst : tmplNullableConst,
                "%'TYPE'%",
                node.typeSpec.javaCode(),
                "%'NAME'%",
                node.name,
                "%'+VAL'%",
                visit(node.val));
    }

    @Override
    public CodeToResolve visitDeclCursor(DeclCursor node) {

        String code =
                String.format(
                        "final Query %s = new Query(\"%s\"); // param-ref-counts: %s, param-marks: %s",
                        node.name,
                        node.staticSql.rewritten,
                        Arrays.toString(node.paramRefCounts),
                        Arrays.toString(node.paramNumOfHostExpr));
        return new CodeTemplate("DeclCursor", Misc.getLineColumnOf(node.ctx), code);
    }

    @Override
    public CodeToResolve visitDeclLabel(DeclLabel node) {
        throw new RuntimeException("unreachable");
    }

    @Override
    public CodeToResolve visitDeclException(DeclException node) {
        String code = "class " + node.name + " extends $APP_ERROR {}";
        return new CodeTemplate("DeclException", Misc.getLineColumnOf(node.ctx), code);
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

        return applyCoercion(node.coercion, tmpl);
    }

    // -------------------------------------------------------------------------
    // ExprBinaryOp
    //

    private static String[] tmplExprBinaryOp =
            new String[] {
                "op%'OPERATION'%%'OP-EXTENSION'%(",
                "  %'+LEFT-OPERAND'%,",
                "  %'+RIGHT-OPERAND'%",
                ")"
            };

    @Override
    public CodeToResolve visitExprBinaryOp(ExprBinaryOp node) {
        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprBinaryOp",
                        Misc.getLineColumnOf(node.ctx),
                        tmplExprBinaryOp,
                        "%'OPERATION'%",
                        node.opStr,
                        "%'OP-EXTENSION'%",
                        node.opExtension,
                        "%'+LEFT-OPERAND'%",
                        visit(node.left),
                        "%'+RIGHT-OPERAND'%",
                        visit(node.right));

        return applyCoercion(node.coercion, tmpl);
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

        if (node.resultType.equals(TypeSpecSimple.NULL)) {
            // in this case, every branch including else-part has null as its expression.
            tmpl = new CodeTemplate("ExprCase", Misc.getLineColumnOf(node.ctx), "null");
        } else {
            tmpl =
                    new CodeTemplate(
                            "ExprCase",
                            Misc.getLineColumnOf(node.ctx),
                            tmplExprCase,
                            "%'SELECTOR-TYPE'%",
                            node.selectorType.javaCode(),
                            "%'+SELECTOR-VALUE'%",
                            visit(node.selector),
                            "%'+WHEN-PARTS'%",
                            visitNodeList(node.whenParts),
                            "%'+ELSE-PART'%",
                            node.elsePart == null ? "null" : visit(node.elsePart),
                            "%'RESULT-TYPE'%",
                            node.resultType.javaCode());
        }

        return applyCoercion(node.coercion, tmpl);
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

        if (node.resultType.equals(TypeSpecSimple.NULL)) {
            // in this case, every branch including else has null as its expression.
            tmpl = new CodeTemplate("ExprCond", Misc.getLineColumnOf(node.ctx), "null");
        } else {
            tmpl =
                    new CodeTemplate(
                            "ExprCond",
                            Misc.getLineColumnOf(node.ctx),
                            tmplExprCond,
                            "%'+COND-PARTS'%",
                            visitNodeList(node.condParts),
                            "%'+ELSE-PART'%",
                            node.elsePart == null ? "null" : visit(node.elsePart));
        }

        return applyCoercion(node.coercion, tmpl);
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
                            node.attr.ty.javaCode(),
                            "%'SUBMSG'%",
                            "tried to retrieve an attribute from an unopened SYS_REFCURSOR",
                            "%'METHOD'%",
                            node.attr.method);
        }

        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprDate(ExprDate node) {

        CodeTemplate tmpl = new CodeTemplate("ExprDate", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprDatetime(ExprDatetime node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprDatetime", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprFalse(ExprFalse node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprFalse", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprField(ExprField node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprField", Misc.getLineColumnOf(node.ctx), node.javaCode());
        return applyCoercion(node.coercion, tmpl);
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
                        node.decl.retType.javaCode(),
                        "%'PARAMETERS'%",
                        wrapperParam,
                        "%'+SET-GLOBAL-FUNC-ARGS'%",
                        code.setArgs,
                        "%'+UPDATE-GLOBAL-FUNC-OUT-ARGS'%",
                        code.updateOutArgs,
                        "%'+ARGUMENTS'%",
                        visitArguments(node.args, node.decl.paramList));

        return applyCoercion(node.coercion, tmpl);
    }

    // -------------------------------------------------------------------------
    // ExprId
    //

    @Override
    public CodeToResolve visitExprId(ExprId node) {

        CodeTemplate tmpl = new CodeTemplate("ExprId", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
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

        return applyCoercion(node.coercion, tmpl);
    }

    // -------------------------------------------------------------------------
    // ExprLike
    //

    private static String[] tmplExprLike =
            new String[] {"opLike(", "  %'+TARGET'%,", "  %'PATTERN'%,", "  %'ESCAPE'%", ")"};

    @Override
    public CodeToResolve visitExprLike(ExprLike node) {

        CodeTemplate tmpl =
                new CodeTemplate(
                        "ExprLike",
                        Misc.getLineColumnOf(node.ctx),
                        tmplExprLike,
                        "%'+TARGET'%",
                        visit(node.target),
                        "%'PATTERN'%",
                        node.pattern.javaCode(),
                        "%'ESCAPE'%",
                        node.escape == null ? "null" : node.escape.javaCode());

        return applyCoercion(node.coercion, tmpl);
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
        String ty = node.resultType.javaCode();

        CodeTemplate tmpl;

        if (node.args.nodes.size() == 0) {
            tmpl =
                    new CodeTemplate(
                            "ExprBuiltinFuncCall",
                            Misc.getLineColumnOf(node.ctx),
                            String.format(
                                    "(%s) invokeBuiltinFunc(conn, \"%s\", %d)",
                                    ty, node.name, node.resultType.simpleTypeIdx));
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
                            Integer.toString(node.resultType.simpleTypeIdx),
                            // assumption: built-in functions do not have OUT parameters
                            "%'+ARGS'%",
                            visitNodeList(node.args).setDelimiter(","));
        }

        return applyCoercion(node.coercion, tmpl);
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

        int argSize = node.args.nodes.size();
        String wrapperParam = getCallWrapperParam(argSize, node.args, node.decl.paramList);
        LocalCallCodeSnippets code =
                getLocalCallCodeSnippets(argSize, node.args, node.decl.paramList);
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
                        node.decl.retType.javaCode(),
                        "%'PARAMETERS'%",
                        wrapperParam,
                        "%'+ALLOC-COERCED-OUT-ARGS'%",
                        code.allocCoercedOutArgs,
                        "%'ARGS'%",
                        code.argsToLocal,
                        "%'+UPDATE-OUT-ARGS'%",
                        code.updateOutArgs,
                        "%'+ARGUMENTS'%",
                        visitArguments(node.args, node.decl.paramList));

        return applyCoercion(node.coercion, tmpl);
    }

    // -------------------------------------------------------------------------
    // ExprNull
    //

    @Override
    public CodeToResolve visitExprNull(ExprNull node) {

        CodeTemplate tmpl = new CodeTemplate("ExprNull", Misc.UNKNOWN_LINE_COLUMN, "null");

        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprUint(ExprUint node) {
        CodeTemplate tmpl = new CodeTemplate("ExprUnit", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprFloat(ExprFloat node) {
        CodeTemplate tmpl =
                new CodeTemplate("ExprFloat", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
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
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprSqlRowCount(ExprSqlRowCount node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprSqlRowCount", Misc.UNKNOWN_LINE_COLUMN, "sql_rowcount[0]");
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprStr(ExprStr node) {

        CodeTemplate tmpl = new CodeTemplate("ExprStr", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprTime(ExprTime node) {

        CodeTemplate tmpl = new CodeTemplate("ExprTime", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprTrue(ExprTrue node) {

        CodeTemplate tmpl = new CodeTemplate("ExprTrue", Misc.UNKNOWN_LINE_COLUMN, "true");
        return applyCoercion(node.coercion, tmpl);
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
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprTimestamp(ExprTimestamp node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprTimestamp", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprAutoParam(ExprAutoParam node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprAutoParam", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprSqlCode(ExprSqlCode node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprSqlCode", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    @Override
    public CodeToResolve visitExprSqlErrm(ExprSqlErrm node) {

        CodeTemplate tmpl =
                new CodeTemplate("ExprSqlCode", Misc.UNKNOWN_LINE_COLUMN, node.javaCode());
        return applyCoercion(node.coercion, tmpl);
    }

    // -------------------------------------------------------------------------
    // StmtAssign
    //

    private static final String[] tmplAssignNotNull =
            new String[] {
                "%'VAR'% = checkNotNull(", "  %'+VAL'%, \"NOT NULL constraint violated\");"
            };
    private static final String[] tmplAssignNullable = new String[] {"%'VAR'% =", "  %'+VAL'%;"};

    @Override
    public CodeToResolve visitStmtAssign(StmtAssign node) {

        boolean checkNotNull =
                (node.var.decl instanceof DeclVar) && ((DeclVar) node.var.decl).notNull;
        if (checkNotNull) {

            return new CodeTemplate(
                    "ExprBinaryOp",
                    Misc.getLineColumnOf(node.ctx),
                    tmplAssignNotNull,
                    "%'VAR'%",
                    node.var.javaCode(),
                    "%'+VAL'%",
                    visit(node.val));
        } else {

            return new CodeTemplate(
                    "ExprBinaryOp",
                    Misc.getLineColumnOf(node.ctx),
                    tmplAssignNullable,
                    "%'VAR'%",
                    node.var.javaCode(),
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
        return new CodeTemplate(
                "StmtBasicLoop",
                Misc.getLineColumnOf(node.ctx),
                tmplStmtBasicLoop,
                "%'OPT-LABEL'%",
                node.declLabel == null ? "" : node.declLabel.javaCode(),
                "%'+STATEMENTS'%",
                visitNodeList(node.stmts));
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
                Misc.getLineColumnOf(node.ctx),
                tmplStmtBlock,
                "%'+DECL-CLASS'%",
                declClass,
                "%'+BODY'%",
                visit(node.body));
    }

    @Override
    public CodeToResolve visitStmtExit(StmtExit node) {
        return new CodeTemplate("StmtExit", Misc.getLineColumnOf(node.ctx), node.javaCode());
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
                node.selectorType.javaCode(),
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
        return new CodeTemplate("StmtContinue", Misc.getLineColumnOf(node.ctx), node.javaCode());
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
                "    %'+SET-INTO-VARIABLES'%",
                "  } else {",
                "    ;", // TODO: setting nulls to into-variables?
                "  }",
                "}"
            };

    private static String[] getSetIntoVarsCode(StmtCursorFetch node) {

        List<String> ret = new LinkedList<>();

        assert node.coercions != null;
        assert node.coercions.size() == node.intoVarList.size();

        int i = 0;
        for (ExprId id : node.intoVarList) {

            String resultStr;
            if (node.columnTypeList == null) {
                resultStr = String.format("rs.getObject(%d)", i + 1);
            } else {
                resultStr =
                        String.format(
                                "(%s) rs.getObject(%d)",
                                node.columnTypeList.get(i).javaCode(), i + 1);
            }

            Coercion c = node.coercions.get(i);
            ret.add(String.format("%s = %s;", id.javaCode(), c.javaCode(resultStr)));

            i++;
        }

        return ret.toArray(DUMMY_STRING_ARRAY);
    }

    @Override
    public CodeToResolve visitStmtCursorFetch(StmtCursorFetch node) {

        String[] setIntoVars = getSetIntoVarsCode(node);
        return new CodeTemplate(
                "StmtCursorFetch",
                Misc.getLineColumnOf(node.ctx),
                tmplStmtCursorFetch,
                "%'CURSOR'%",
                node.id.javaCode(),
                "%'+SET-INTO-VARIABLES'%",
                setIntoVars);
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
                "  %'CURSOR'%.open(conn,",
                "    %'+HOST-EXPRS'%);",
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

            Object dupCursorArgs = getDupCursorArgs(node, decl.paramRefCounts);
            CodeTemplateList hostExprs =
                    getHostExprs(node, decl.paramNumOfHostExpr, decl.paramRefCounts);

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

    private static String[] tmplStmtSql =
            new String[] {
                "{ // %'KIND'% SQL statement",
                "  PreparedStatement stmt_%'LEVEL'% = null;",
                "  try {",
                "    String dynSql_%'LEVEL'% = checkNotNull(",
                "      %'+SQL'%, \"SQL part was evaluated to NULL\");",
                "    stmt_%'LEVEL'% = conn.prepareStatement(dynSql_%'LEVEL'%);",
                "    %'+BAN-INTO-CLAUSE'%",
                "    %'+SET-USED-EXPR'%",
                "    if (stmt_%'LEVEL'%.execute()) {",
                // not from the Oracle specification, but from Oracle 19.0.0.0 behavior
                "      sql_rowcount[0] = 0L;",
                "      %'+HANDLE-INTO-CLAUSE'%",
                "    } else {",
                "      sql_rowcount[0] = (long) stmt_%'LEVEL'%.getUpdateCount();",
                "    }",
                "  } catch (SQLException e) {",
                "    Server.log(e);",
                "    throw new SQL_ERROR(e.getMessage());",
                "  } finally {",
                "    if (stmt_%'LEVEL'% != null) {",
                "      stmt_%'LEVEL'%.close();",
                "    }",
                "  }",
                "}"
            };

    private static String[] tmplHandleIntoClause =
            new String[] {
                "ResultSet r%'LEVEL'% = stmt_%'LEVEL'%.getResultSet();",
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
                "ResultSetMetaData rsmd_%'LEVEL'% = stmt_%'LEVEL'%.getMetaData();",
                "if (rsmd_%'LEVEL'% == null || rsmd_%'LEVEL'%.getColumnCount() < 1) {",
                "  throw new SQL_ERROR(\"INTO clause must be used with a SELECT statement\");",
                "}"
            };

    private String[] getSetResultsCode(StmtSql node, List<ExprId> intoVarList) {

        List<String> ret = new LinkedList<>();

        int size = intoVarList.size();
        assert node.coercions.size() == size;
        assert node.dynamic || (node.columnTypeList != null && node.columnTypeList.size() == size);

        int i = 0;
        for (ExprId id : node.intoVarList) {

            assert id.decl instanceof DeclVar || id.decl instanceof DeclParamOut
                    : "only variables or out-parameters can be used in into-clauses";

            String resultStr;
            if (node.dynamic) {
                resultStr = String.format("r%%'LEVEL'%%.getObject(%d)", i + 1);
            } else {
                resultStr =
                        String.format(
                                "(%s) r%%'LEVEL'%%.getObject(%d)",
                                node.columnTypeList.get(i).javaCode(), i + 1);
            }

            Coercion c = node.coercions.get(i);
            boolean checkNotNull = (id.decl instanceof DeclVar) && ((DeclVar) id.decl).notNull;
            if (checkNotNull) {
                ret.add(
                        String.format(
                                "%s = checkNotNull(%s, \"NOT NULL constraint violated\");",
                                id.javaCode(), c.javaCode(resultStr)));
            } else {
                ret.add(String.format("%s = %s;", id.javaCode(), c.javaCode(resultStr)));
            }

            i++;
        }

        return ret.toArray(DUMMY_STRING_ARRAY);
    }

    private CodeToResolve visitStmtSql(StmtSql node) {

        Object setUsedExpr = getSetUsedExpr(node.usedExprList);

        Object handleIntoClause, banIntoClause;
        if (node.intoVarList == null) {
            assert node.coercions == null;
            handleIntoClause = banIntoClause = "";
        } else {
            assert node.coercions != null;
            String[] setResults = getSetResultsCode(node, node.intoVarList);
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
                tmplStmtSql,
                "%'KIND'%",
                node.dynamic ? "dynamic" : "static",
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
                "  %'CURSOR'%.open(conn);",
                "  ResultSet %'RECORD'%_r%'LEVEL'% = %'CURSOR'%.rs;",
                "  %'LABEL'%",
                "  while (%'RECORD'%_r%'LEVEL'%.next()) {",
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
                "  %'+DUPLICATE-CURSOR-ARG'%",
                "  %'CURSOR'%.open(conn,",
                "    %'+HOST-EXPRS'%);",
                "  ResultSet %'RECORD'%_r%'LEVEL'% = %'CURSOR'%.rs;",
                "  %'LABEL'%",
                "  while (%'RECORD'%_r%'LEVEL'%.next()) {",
                "    %'+STATEMENTS'%",
                "  }",
                "  %'CURSOR'%.close();",
                "} catch (SQLException e) {",
                "  Server.log(e);",
                "  throw new SQL_ERROR(e.getMessage());",
                "}"
            };

    @Override
    public CodeToResolve visitStmtForCursorLoop(StmtForCursorLoop node) {

        DeclCursor decl = (DeclCursor) node.cursor.decl;
        if (decl.paramNumOfHostExpr.length == 0) {

            return new CodeTemplate(
                    "StmtForCursorLoop",
                    Misc.getLineColumnOf(node.ctx),
                    tmplStmtForCursorLoopWithoutHostExprs,
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

            Object dupCursorArgs = getDupCursorArgs(node, decl.paramRefCounts);
            CodeTemplateList hostExprs =
                    getHostExprs(node, decl.paramNumOfHostExpr, decl.paramRefCounts);

            return new CodeTemplate(
                    "StmtForCursorLoop",
                    Misc.getLineColumnOf(node.ctx),
                    tmplStmtForCursorLoopWithHostExprs,
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

        return new CodeTemplate(
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
    }

    // -------------------------------------------------------------------------
    // StmtForSqlLoop (StmtForStaticSqlLoop, StmtForExecImmeLoop)
    //

    private static String[] tmplStmtForSqlLoop =
            new String[] {
                "{ // for loop with %'KIND'% SQL",
                "  PreparedStatement stmt_%'LEVEL'% = null;",
                "  try {",
                "    String sql_%'LEVEL'% = checkNotNull(",
                "      %'+SQL'%, \"SQL part was evaluated to NULL\");",
                "    stmt_%'LEVEL'% = conn.prepareStatement(sql_%'LEVEL'%);",
                "    ResultSetMetaData rsmd_%'LEVEL'% = stmt_%'LEVEL'%.getMetaData();",
                "    if (rsmd_%'LEVEL'% == null || rsmd_%'LEVEL'%.getColumnCount() < 1) {",
                "      throw new SQL_ERROR(\"not a SELECT statement\");",
                "    }",
                "    %'+SET-USED-EXPR'%",
                "    if (!stmt_%'LEVEL'%.execute()) {",
                "      throw new SQL_ERROR(\"use a SELECT statement\");", // double check
                "    }",
                "    ResultSet %'RECORD'%_r%'LEVEL'% = stmt_%'LEVEL'%.getResultSet();",
                "    if (%'RECORD'%_r%'LEVEL'% == null) {",
                // EXECUTE IMMDIATE 'CALL ...' leads to this line
                "      throw new SQL_ERROR(\"no result set\");",
                "    }",
                "    %'LABEL'%",
                "    while (%'RECORD'%_r%'LEVEL'%.next()) {",
                "      %'+STATEMENTS'%",
                "    }",
                "  } catch (SQLException e) {",
                "    Server.log(e);",
                "    throw new SQL_ERROR(e.getMessage());",
                "  } finally {",
                "    if (stmt_%'LEVEL'% != null) {",
                "      stmt_%'LEVEL'%.close();",
                "    }",
                "  }",
                "}"
            };

    private CodeToResolve visitStmtForSqlLoop(StmtForSqlLoop node) {

        Object setUsedExpr = getSetUsedExpr(node.usedExprList);

        return new CodeTemplate(
                "StmtForSqlLoop",
                Misc.getLineColumnOf(node.ctx),
                tmplStmtForSqlLoop,
                "%'KIND'%",
                node.dynamic ? "dynamic" : "static",
                "%'+SQL'%",
                visit(node.sql),
                "%'+SET-USED-EXPR'%",
                setUsedExpr,
                "%'RECORD'%",
                node.record.name,
                "%'LABEL'%",
                node.label == null ? "" : node.label + "_%'LEVEL'%:",
                "%'LEVEL'%",
                Integer.toString(node.record.scope.level),
                "%'+STATEMENTS'%",
                visitNodeList(node.stmts));
    }

    @Override
    public CodeToResolve visitStmtForStaticSqlLoop(StmtForStaticSqlLoop node) {
        return visitStmtForSqlLoop(node);
    }

    @Override
    public CodeToResolve visitStmtForExecImmeLoop(StmtForExecImmeLoop node) {
        return visitStmtForSqlLoop(node);
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
                visitArguments(node.args, node.decl.paramList));
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
                    Misc.getLineColumnOf(node.ctx),
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

        int argSize = node.args.nodes.size();
        String wrapperParam = getCallWrapperParam(argSize, node.args, node.decl.paramList);
        LocalCallCodeSnippets code =
                getLocalCallCodeSnippets(argSize, node.args, node.decl.paramList);
        String block = node.prefixDeclBlock ? node.decl.scope().block + "." : "";

        return Misc.isEmpty(node.args)
                ? new CodeTemplate(
                        "StmtLocalProcCall",
                        Misc.getLineColumnOf(node.ctx),
                        block + node.name + "();")
                : new CodeTemplate(
                        "StmtLocalProcCall",
                        Misc.getLineColumnOf(node.ctx),
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
                        visitArguments(node.args, node.decl.paramList));
    }

    @Override
    public CodeToResolve visitStmtNull(StmtNull node) {
        return new CodeTemplate("StmtNull", Misc.getLineColumnOf(node.ctx), ";");
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
            return new CodeTemplate("StmtReturn", Misc.getLineColumnOf(node.ctx), "return;");
        } else {
            return new CodeTemplate(
                    "StmtReturn",
                    Misc.getLineColumnOf(node.ctx),
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

        return new CodeTemplate(
                "StmtWhileLoop",
                Misc.getLineColumnOf(node.cond.ctx),
                tmplStmtWhileLoop,
                "%'OPT-LABEL'%",
                node.declLabel == null ? "" : node.declLabel.javaCode(),
                "%'+EXPRESSION'%",
                node.cond instanceof ExprTrue ? "opNot(Boolean.FALSE)" : visit(node.cond),
                "%'+STATEMENTS'%",
                visitNodeList(node.stmts));
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
                        Misc.getLineColumnOf(node.ctx),
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
                Misc.getLineColumnOf(node.ctx),
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
    public CodeToResolve visitTypeSpecPercent(TypeSpecPercent node) {
        // TypeSpecs are not visited
        throw new RuntimeException("unreachable");
    }

    @Override
    public CodeToResolve visitTypeSpecSimple(TypeSpecSimple node) {
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
                Misc.getLineColumnOf(node.ctx),
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
                Misc.getLineColumnOf(node.ctx),
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

    private Object getDupCursorArgs(StmtCursorOpen node, int[] paramRefCounts) {

        CodeTemplateList ret = new CodeTemplateList();

        int size = paramRefCounts.length;
        for (int i = 0; i < size; i++) {

            if (paramRefCounts[i] > 1) {

                Expr arg = node.args.nodes.get(i);
                ret.addElement(
                        new CodeTemplate(
                                "duplicate cursor argument",
                                Misc.getLineColumnOf(arg.ctx),
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
            StmtCursorOpen node, int[] paramNumOfHostExpr, int[] paramRefCounts) {

        int size = paramNumOfHostExpr.length;
        assert size > 0;

        DeclCursor decl = (DeclCursor) node.cursor.decl;
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
                    ret.addElement((CodeTemplate) visit(node.args.nodes.get(k)));
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

    private CodeTemplateList visitArguments(NodeList<Expr> args, NodeList<DeclParam> paramList) {

        assert args != null;
        assert paramList != null;
        assert args.nodes.size() == paramList.nodes.size();

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
                                Misc.getLineColumnOf(a.ctx),
                                ((ExprId) a).javaCodeForOutParam());
            } else {
                tmpl = (CodeTemplate) visit(a);
            }

            ret.addElement(tmpl);
            i++;
        }

        return ret.setDelimiter(",");
    }

    private static String[] getNullifyOutParamCode(NodeList<DeclParam> paramList) {

        List<String> ret = new LinkedList<>();

        for (DeclParam dp : paramList.nodes) {
            if (dp instanceof DeclParamOut && !((DeclParamOut) dp).alsoIn) {
                ret.add(String.format("%s[0] = null;", ((DeclParamOut) dp).name));
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
            new String[] {"stmt_%'LEVEL'%.setObject(%'INDEX'%,", "  %'+VALUE'%", ");"};

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

    private CodeToResolve applyCoercion(Coercion c, CodeTemplate exprCode) {

        if (c == null || c instanceof Coercion.Identity) {
            return exprCode;
        } else if (c instanceof Coercion.Cast) {
            Coercion.Cast cast = (Coercion.Cast) c;
            return new CodeTemplate(
                    "cast coercion",
                    Misc.UNKNOWN_LINE_COLUMN,
                    tmplCastCoercion,
                    "%'TYPE'%",
                    cast.dst.javaCode(),
                    "%'+EXPR'%",
                    exprCode);
        } else if (c instanceof Coercion.Conversion) {
            Coercion.Conversion conv = (Coercion.Conversion) c;
            return new CodeTemplate(
                    "conversion coercion",
                    Misc.UNKNOWN_LINE_COLUMN,
                    tmplConvCoercion,
                    "%'SRC-TYPE'%",
                    conv.src.plcName,
                    "%'DST-TYPE'%",
                    conv.dst.plcName,
                    "%'+EXPR'%",
                    exprCode);
        } else if (c instanceof Coercion.CoerceAndCheckPrecision) {
            Coercion.CoerceAndCheckPrecision checkPrec = (Coercion.CoerceAndCheckPrecision) c;
            return new CodeTemplate(
                    "coerce and check precision",
                    Misc.UNKNOWN_LINE_COLUMN,
                    tmplCoerceAndCheckPrec,
                    "%'PREC'%",
                    Integer.toString(checkPrec.prec),
                    "%'SCALE'%",
                    Short.toString(checkPrec.scale),
                    "%'+EXPR'%",
                    applyCoercion(checkPrec.c, exprCode));
        } else if (c instanceof Coercion.CoerceAndCheckStrLength) {
            Coercion.CoerceAndCheckStrLength checkStrLen = (Coercion.CoerceAndCheckStrLength) c;
            return new CodeTemplate(
                    "coerce and check precision",
                    Misc.UNKNOWN_LINE_COLUMN,
                    tmplCoerceAndCheckStrLength,
                    "%'IS-CHAR'%",
                    checkStrLen.isChar ? "true" : "false",
                    "%'LENGTH'%",
                    "" + checkStrLen.length,
                    "%'+EXPR'%",
                    applyCoercion(checkStrLen.c, exprCode));
        } else {
            throw new RuntimeException("unreachable");
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

    private static String getCallWrapperParam(
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
                DeclIdTyped declId = (DeclIdTyped) id.decl;
                sbuf.append(String.format("%s[] o%d", declId.typeSpec().javaCode, i));
            } else {
                sbuf.append(String.format("%s o%d", param.typeSpec.javaCode, i));
            }
        }

        return sbuf.toString();
    }

    private static class GlobalCallCodeSnippets {
        String[] setArgs;
        String[] updateOutArgs;
    }

    private static GlobalCallCodeSnippets getGlobalCallCodeSnippets(
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
                Coercion cRev = c.getReversion();
                assert cRev != null; // by earlier check
                String outVal =
                        String.format(
                                "(%s) stmt.getObject(%d)", param.typeSpec.javaCode, i + argOffset);
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

    private static LocalCallCodeSnippets getLocalCallCodeSnippets(
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
                    String paramType = param.typeSpec.javaCode;
                    allocCoercedOutArgs.add(
                            String.format(
                                    "%s[] p%d = new %s[] { %s };",
                                    paramType, i, paramType, c.javaCode("o" + i + "[0]")));

                    argsToLocal.append("p" + i);

                    Coercion cRev = c.getReversion();
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

        final String astNode;
        final String plcsqlLineColumn;
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

            int plcsqlLine = plcsqlPos[0];
            int plcsqlColumn = plcsqlPos[1];

            // used: plcsqlLineColumn, template, substitutions
            if (plcsqlLine < 0 && plcsqlColumn < 0) {
                this.plcsqlLineColumn = null; // do not mark code range in this case
            } else {
                assert plcsqlLine > 0 && plcsqlColumn > 0
                        : String.format(
                                "%s - line and column numbers of code templates must be positive integers: (%d, %d)",
                                astNode, plcsqlLine, plcsqlColumn);
                this.plcsqlLineColumn = String.format("%d,%d", plcsqlLine, plcsqlColumn);
            }

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

            boolean markCodeRange = plcsqlLineColumn != null;
            if (markCodeRange) {
                codeRangeMarkers.append(
                        String.format(" (%d,%s", codeLines.size() + 1, plcsqlLineColumn));
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
                        throw new RuntimeException("unreachable");
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
}
