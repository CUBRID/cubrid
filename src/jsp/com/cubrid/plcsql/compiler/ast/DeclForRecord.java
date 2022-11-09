package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;

public class DeclForRecord extends Decl implements I_DeclId {

    public final String name;

    public DeclForRecord(String name) {
        this.name = name;
    }

    public TypeSpec typeSpec() {
        assert false: "unreachable";    // records without field do not appear in a program
        throw new RuntimeException("unreachable");
    }

    @Override
    public String typeStr() {
        return "for-loop-record";
    }

    @Override
    public String toJavaCode() {
        assert false; // unreachable: currently, used only in for-dynamic-sql-loop, not in any ordinary declration list
        throw new RuntimeException("unreachable");
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
