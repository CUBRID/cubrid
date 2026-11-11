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

package com.cubrid.plcsql.predefined;

public class PlcsqlRuntimeError extends RuntimeException {

    public final int code;

    public PlcsqlRuntimeError(int code, String msg) {
        super(msg);
        this.code = code;
    }

    public int getLine() {
        return line;
    }

    public int getColumn() {
        return column;
    }

    public int getFrameIndex() {
        return frameIndex;
    }

    // pos: the line and column, and optionally the frame index for the caller (see
    // SpLib.getPlcLineColumn())
    public PlcsqlRuntimeError setPlcLineColumn(int[] pos) {
        this.line = pos[0];
        this.column = pos[1];
        if (pos.length > 2) {
            this.frameIndex = pos[2];
        }
        return this;
    }

    @Override
    public String getMessage() {
        return nestedMsg == null ? super.getMessage() : nestedMsg;
    }

    // line and column are -1 if the error is not from the PL/CSQL code (e.g., from the PL engine
    // code or a package spec member without its implementation), and 0 if it is from the PL/CSQL
    // code but its position is unknown (e.g., in a stack frame cut due to too many nests)
    public static String prependLocationToErrMsg(int line, int column, String errMsg) {

        String ret;

        if (line == -1 && column == -1) {
            ret = String.format("\n  %s", errMsg);
        } else if (line == 0 && column == 0) {
            ret = String.format("\n  (line ?, column ?) %s", errMsg);
        } else {
            ret = String.format("\n  (line %d, column %d) %s", line, column, errMsg);
        }

        return ret;
    }

    // An error of a directly called routine reaches its caller as it is. Make its message the same
    // as the one of an error from a routine called through the server: the position in the callee
    // goes into the message, and the position is to be set again in the caller.
    // Only the positions in the MAX_NEST_LEVEL routines nearest to the error go into the message.
    // A line "..." stands for those in the routines further out, except the outermost one, whose
    // position the server puts in front of the message.
    public PlcsqlRuntimeError nestInCaller() {
        String msg = getMessage();
        if (nestLevel > MAX_NEST_LEVEL) {
            // leave the message as it is
        } else if (nestLevel == MAX_NEST_LEVEL) {
            // put "..." after the first line
            assert msg.startsWith(MSG_SP_EXECUTE_ERROR);
            nestedMsg =
                    String.format(
                            "%s\n  ...%s",
                            MSG_SP_EXECUTE_ERROR, msg.substring(MSG_SP_EXECUTE_ERROR.length()));
        } else {
            nestedMsg = MSG_SP_EXECUTE_ERROR + prependLocationToErrMsg(line, column, msg);
        }

        if (nestLevel <= MAX_NEST_LEVEL) {
            nestLevel++;
        }

        line = -1;
        column = -1;
        return this;
    }

    // --------------------------------------------
    // Private
    // --------------------------------------------

    // the message of ER_SP_EXECUTE_ERROR in en_US
    private static final String MSG_SP_EXECUTE_ERROR = "Stored procedure execute error: ";

    // the same as METHOD_MAX_RECURSION_DEPTH, the limit of nested calls through the server
    private static final int MAX_NEST_LEVEL = 15;

    // -1: not set yet, or the error is not from the PL/CSQL code
    // 0: from the PL/CSQL code, but its position is unknown
    private int line = -1;
    private int column = -1;
    private String nestedMsg = null;
    private int nestLevel = 0; // the number of times this has been nested

    // the index of the stack trace frame from which the routine catching this searches its frames
    private int frameIndex = 0;
}
