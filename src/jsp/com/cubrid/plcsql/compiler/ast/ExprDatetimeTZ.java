package com.cubrid.plcsql.compiler.ast;

import java.time.temporal.TemporalAccessor;

public class ExprDatetimeTZ extends ExprZonedDateTime {

    public ExprDatetimeTZ(TemporalAccessor time) {
        super(time);
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
