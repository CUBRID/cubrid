package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

import java.util.List;
import java.util.ArrayList;

public class NodeList<N extends AstNode> implements AstNode {

    public final List<N> nodes = new ArrayList<>();

    public NodeList() {}

    public NodeList<N> addNode(N p) {
        assert p != null;
        nodes.add(p);
        return this;
    }

    @Override
    public String toJavaCode() {
        return toJavaCode(null, "\n");
    }

    public String toJavaCode(String delim) {
        return toJavaCode(null, delim);
    }

    public String toJavaCode(String prefix, String delim) {

        StringBuffer sbuf = new StringBuffer();

        boolean first = true;
        for (N p: nodes) {

            if (delim != null) {
                if (first) {
                    first = false;
                } else {
                    sbuf.append(delim);
                }
            }

            if (prefix != null) {
                sbuf.append(prefix);
            }

            sbuf.append(p.toJavaCode());
        }

        return sbuf.toString();
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
