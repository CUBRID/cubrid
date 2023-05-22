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

package com.cubrid.jsp.data;

import java.io.DataOutputStream;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;

public class DataUtilities {
    public static final int CHAR_BYTES = (Byte.SIZE / 8);
    public static final int SHORT_BYTES = (Short.SIZE / 8);
    public static final int INT_BYTES = (Integer.SIZE / 8);
    public static final int LONG_BYTES = (Long.SIZE / 8);
    public static final int FLOAT_BYTES = (Float.SIZE / 8);
    public static final int DOUBLE_BYTES = (Double.SIZE / 8);

    public static final int CHAR_ALIGNMENT = CHAR_BYTES;
    public static final int SHORT_ALIGNMENT = SHORT_BYTES;
    public static final int INT_ALIGNMENT = INT_BYTES;
    public static final int LONG_ALIGNMENT = LONG_BYTES;
    public static final int FLOAT_ALIGNMENT = FLOAT_BYTES;
    public static final int DOUBLE_ALIGNMENT = DOUBLE_BYTES;
    public static final int MAX_ALIGNMENT = DOUBLE_ALIGNMENT;

    public static final int MAX_SMALL_STRING_SIZE = 255;

    public static final int LARGE_STRING_CODE = 0xff;

    public static final int OID_BYTE_SIZE = 8;

    public static int align(ByteBuffer buffer, int alignment) {
        int newOffset = (buffer.position() + alignment - 1) & ~(alignment - 1);
        buffer.position(newOffset);
        return newOffset;
    }

    public static int alignedPosition(int currentPosition, int alignment) {
        int newOffset = (currentPosition + alignment - 1) & ~(alignment - 1);
        return newOffset;
    }

    public static int alignedPosition(ByteBuffer buffer, int alignment) {
        int newOffset = (buffer.position() + alignment - 1) & ~(alignment - 1);
        return newOffset;
    }

    public static boolean checkRange(ByteBuffer buffer, int size) {
        if (size > buffer.remaining()) {
            return false;
        }
        return true;
    }

    // TODO: The following utility functions are legacy Functions to be removed
    // commands being used by javasp utilites are using these functions
    public static void packAndSendRawString(String str, DataOutputStream out) throws IOException {
        byte b[] = str.getBytes();

        int len = b.length + 1;
        int bits = len & 3;
        int pad = 0;

        if (bits != 0) pad = 4 - bits;

        out.writeInt(len + pad);
        out.write(b);
        for (int i = 0; i <= pad; i++) {
            out.writeByte(0);
        }
    }

    public static void packAndSendRawString(String str, ByteBuffer buf) {
        byte b[] = str.getBytes();

        int len = b.length + 1;
        int bits = len & 3;
        int pad = 0;

        if (bits != 0) pad = 4 - bits;

        buf.putInt(len + pad);
        buf.put(b);
        for (int i = 0; i <= pad; i++) {
            buf.put((byte) 0);
        }
    }

    public static void packAndSendString(String str, ByteBuffer buf, String charset)
            throws UnsupportedEncodingException {
        byte b[] = str.getBytes(charset);

        int len = b.length + 1;
        int bits = len & 3;
        int pad = 0;

        if (bits != 0) pad = 4 - bits;

        buf.putInt(len + pad);
        buf.put(b);
        for (int i = 0; i <= pad; i++) {
            buf.put((byte) 0);
        }
    }

    public static int getLengthtoSend(String str) {
        byte b[] = str.getBytes();

        int len = b.length + 1;

        int bits = len & 3;
        int pad = 0;

        if (bits != 0) pad = 4 - bits;

        return len + pad;
    }

    public static int bytes2int(byte[] b, int startIndex) {
        int data = 0;
        int endIndex = startIndex + 4;

        for (int i = startIndex; i < endIndex; i++) {
            data <<= 8;
            data |= (b[i] & 0xff);
        }

        return data;
    }

    public static short bytes2short(byte[] b, int startIndex) {
        short data = 0;
        int endIndex = startIndex + 2;

        for (int i = startIndex; i < endIndex; i++) {
            data <<= 8;
            data |= (b[i] & 0xff);
        }
        return data;
    }
}
