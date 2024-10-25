/*
 *
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
package com.cubrid.jsp.code;

public class Signature {
    private String className;
    private String methodName;
    private String args;

    public Signature(String cName, String mName, String args) {
        className = cName;
        methodName = mName;
        this.args = args;
    }

    public String getClassName() {
        return className;
    }

    public String getMethodName() {
        return methodName;
    }

    public String getArgs() {
        return args;
    }

    public static Signature parse(String signature) {
        String className = null;
        String methodName = null;
        String args = null;

        int argStart = signature.indexOf('(') + 1;
        if (argStart < 0) {
            throw new IllegalArgumentException("Parenthesis '(' not found");
        }
        int argEnd = signature.indexOf(')');
        if (argEnd < 0) {
            throw new IllegalArgumentException("Parenthesis ')' not found");
        }
        int nameStart = signature.substring(0, argStart).lastIndexOf('.') + 1;

        if (signature.charAt(0) == '\'') {
            className = signature.substring(1, nameStart - 1);
        } else {
            className = signature.substring(0, nameStart - 1);
        }

        methodName = signature.substring(nameStart, argStart - 1);
        args = signature.substring(argStart, argEnd);

        return new Signature(className, methodName, args);
    }
}
