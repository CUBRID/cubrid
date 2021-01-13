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

package cubrid.jdbc.util;

import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Iterator;

public class ByteArrayBuffer {
    private static final int UnitSize = 102400;

    private ArrayList<byte[]> byteArrayList;
    private byte[] baseByteArray;
    private byte[] buffer;
    private int pos;
    private int dataSize;

    public ByteArrayBuffer() {
	baseByteArray = new byte[UnitSize];
	reset();
    }

    public void writeByte(byte v) {
	write (v);
    }

    public final void writeShort(int v) throws IOException {
        write((v >>> 8) & 0xFF);
        write((v >>> 0) & 0xFF);
    }

    public final void writeInt(int v) {
        write((v >>> 24) & 0xFF);
        write((v >>> 16) & 0xFF);
        write((v >>>  8) & 0xFF);
        write((v >>>  0) & 0xFF);
    }

    private byte writeBuffer[] = new byte[8];
    public final void writeLong(long v) {
        writeBuffer[0] = (byte)(v >>> 56);
        writeBuffer[1] = (byte)(v >>> 48);
        writeBuffer[2] = (byte)(v >>> 40);
        writeBuffer[3] = (byte)(v >>> 32);
        writeBuffer[4] = (byte)(v >>> 24);
        writeBuffer[5] = (byte)(v >>> 16);
        writeBuffer[6] = (byte)(v >>>  8);
        writeBuffer[7] = (byte)(v >>>  0);
        write(writeBuffer, 0, 8);
    }

    public final void writeFloat(float v) throws IOException {
        writeInt(Float.floatToIntBits(v));
    }

    public final void writeDouble(double v) throws IOException {
        writeLong(Double.doubleToLongBits(v));
    }

    public final void write(byte[] b, int off, int len) {
        if (b == null) {
            throw new NullPointerException();
        } else if ((off < 0) || (off > b.length) || (len < 0) ||
                   ((off + len) > b.length) || ((off + len) < 0)) {
            throw new IndexOutOfBoundsException();
        } else if (len == 0) {
            return;
        }
        for (int i = 0 ; i < len ; i++) {
            write(b[off + i]);
        }
    }

    private void write(int b) {
	if (pos == UnitSize) {
	    byteArrayList.add(buffer);
	    buffer = new byte[UnitSize];
	    pos = 0;
	}
	buffer[pos] = (byte)b;
	pos++;
	dataSize++;
    }

    public void merge(int size, ByteArrayBuffer buf) {
	writeInt(size);

	byte[] dest = new byte[pos];
	System.arraycopy(buffer, 0, dest, 0, pos);
	byteArrayList.add(dest);

	Iterator<byte[]> i = buf.byteArrayList.iterator();
	while (i.hasNext()) {
	    byte[] b = i.next();
	    byteArrayList.add(b);
	}

	dest = new byte[buf.pos];
	System.arraycopy(buf.buffer, 0, dest, 0, buf.pos);
	byteArrayList.add(dest);

	buffer = new byte[UnitSize];
	pos = 0;
	dataSize += size;
    }

    private void writeHeader(byte[] info) {
	byte[] header = null;

	if (byteArrayList.size() == 0) {
	    header = buffer;
	} else {
	    header = byteArrayList.get(0);
	}
        header[0] = (byte) ((dataSize >>> 24) & 0xFF);
        header[1] = (byte) ((dataSize >>> 16) & 0xFF);
        header[2] = (byte) ((dataSize >>>  8) & 0xFF);
        header[3] = (byte) ((dataSize >>>  0) & 0xFF);
        header[4] = info[0];
        header[5] = info[1];
        header[6] = info[2];
        header[7] = info[3];
    }

    public void writeToStream(byte[] info, OutputStream o) throws IOException {
	DataOutputStream os = new DataOutputStream(o);
	writeHeader(info);

	Iterator<byte[]> i = byteArrayList.iterator();
	while (i.hasNext()) {
	    byte[] b = i.next();
	    if (b != null) {
		os.write(b);
	    }
	}
	os.write(buffer, 0, pos);
	os.flush();
    }

    public void reset() {
	byteArrayList = new ArrayList<byte[]>();
	buffer = baseByteArray;
	pos = 8;
	dataSize = 0;
    }

}
