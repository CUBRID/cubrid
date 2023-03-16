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

package com.cubrid.plcsql.builtin;

import java.util.LinkedList;

public class MessageBuffer {
    private static final int STATUS_SUCCESS = 0;
    private static final int STATUS_FAILURE = 1;

    private boolean isEnabled;

    private LinkedList<String> lines;
    private StringBuilder builder;
    private int status;
    private int size;

    public MessageBuffer() {
        isEnabled = false;
        builder = null;
        status = STATUS_FAILURE;
        lines = new LinkedList<String>();
    }

    public void enable(Integer size) {
        if (size == null) {
            this.size = 20000;
        } else {
            this.size = size;
        }
        builder = new StringBuilder();
        isEnabled = true;
    }

    public void disable() {
        isEnabled = false;
        clearBuilder();
        lines.clear();
        size = 0;
    }

    public void putLine(String str) {
        if (isEnabled) {
            if (str != null) {
                lines.add(str);
            }
            lines.add(System.lineSeparator());
        }
    }

    public void put(String str) {
        if (isEnabled) {
            builder.append(str);
        }
    }

    public void newLine() {
        if (isEnabled) {
            if (builder.length() > 0) {
                putLine(builder.toString());
                clearBuilder();
            } else {
                putLine(null);
            }
        }
    }

    public String getLine() {
        String res = null;
        if (lines.size() == 0) {
            if (builder != null && builder.length() > 0) {
                res = builder.toString();
                status = STATUS_SUCCESS;
            } else {
                status = STATUS_FAILURE;
            }
        } else {
            res = lines.pollFirst();
            status = STATUS_SUCCESS;
        }
        return res;
    }

    public String[] getLines(int num) {
        if (lines.size() < num) {
            num = lines.size();
        }

        String[] outputs = null;
        if (num > 0) {
            outputs = new String[num];
            for (int i = 0; i < num; i++) {
                outputs[i] = lines.pollFirst();
            }
        }
        return outputs;
    }

    public int getStatus() {
        return status;
    }

    private void clearBuilder() {
        if (builder != null) {
            builder.setLength(0);
        }
    }
}
