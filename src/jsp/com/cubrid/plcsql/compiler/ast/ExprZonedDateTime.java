/*
 * Copyright (C) 2008 Search Solution Corporation.
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

package com.cubrid.plcsql.compiler.ast;

import com.cubrid.plcsql.compiler.Misc;
import com.cubrid.plcsql.compiler.DateTimeParser;

import java.text.SimpleDateFormat;

import java.time.temporal.TemporalAccessor;
import java.time.ZonedDateTime;
import java.time.LocalDateTime;

public class ExprZonedDateTime implements I_Expr {

    public final TemporalAccessor time;

    public ExprZonedDateTime(TemporalAccessor time) {
        this.time = time;
    }

    @Override
    public String toJavaCode() {

        String offsetStr;
        LocalDateTime localPart;
        if (time instanceof ZonedDateTime) {
            offsetStr = '"' + ((ZonedDateTime) time).getOffset().getId() + '"';
            localPart = ((ZonedDateTime) time).toLocalDateTime();
        } else {
            assert time instanceof LocalDateTime;
            offsetStr = "null";
            localPart = (LocalDateTime) time;
        }

        if (localPart.equals(DateTimeParser.nullDateTime)) {
            return String.format("new ZonedTimestamp(%s, 0 - 1900, 0 - 1, 0, 0, 0, 0, 0)", offsetStr);
        } else {
            return String.format("new ZonedTimestamp(%s, %d - 1900, %d - 1, %d, %d, %d, %d, %d)",
                offsetStr,
                localPart.getYear(),
                localPart.getMonthValue(),
                localPart.getDayOfMonth(),
                localPart.getHour(),
                localPart.getMinute(),
                localPart.getSecond(),
                localPart.getNano()
            );
        }
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------

}
