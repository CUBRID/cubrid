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

import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.NullValue;
import com.cubrid.jsp.value.Value;

public class PlParamInfo {

    public String name;

    public byte mode;

    public int type;
    public int prec;
    public short scale;
    public byte charset;

    public Value value;

    public PlParamInfo(String name, byte mode, int type, int prec, short scale, byte charset) {
        this.name = name;
        this.mode = mode;
        this.type = type;
        this.prec = prec;
        this.scale = scale;
        this.charset = charset;
        this.value = new NullValue();
    }

    public PlParamInfo(CUBRIDUnpacker unpacker) {
        this.mode = (byte) unpacker.unpackInt();
        this.name = unpacker.unpackCString();

        this.type = unpacker.unpackInt();
        this.prec = unpacker.unpackInt();
        this.scale = (short) unpacker.unpackInt();
        this.charset = (byte) unpacker.unpackInt();

        int has_value = unpacker.unpackInt();
        if (has_value == 1) {
            try {
                int paramType = unpacker.unpackInt();
                this.value = unpacker.unpackValue(paramType);
            } catch (TypeMismatchException e) {
                // TODO: error handling?
                this.value = new NullValue();
            }
        }
    }
}
