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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.SocketTimeoutException;

import cubrid.jdbc.net.BrokerHandler;

public class UTimedDataInputStream {
    public final static int PING_TIMEOUT = 5000;
    private DataInputStream stream = null;
    private String ip = null;
    private int port = 0;
    private int timeout = 0;
    private int pid = 0;
    private byte session[] = {0, 0, 0, 0};

    public UTimedDataInputStream(InputStream stream, String ip, int port) {
        this(stream, ip, port, 0);
    }

    public UTimedDataInputStream(InputStream stream, String ip, int port, int pid, byte session[], int timeout) {
        this.stream = new DataInputStream(stream);
        this.ip = ip;
        this.port = port;
        this.pid = pid;
        for (int i = 0; i < 4; i++) this.session[i] = session[i + 8];
        this.timeout = timeout;
    }

    public UTimedDataInputStream(InputStream stream, String ip, int port, int timeout) {
        this.stream = new DataInputStream(stream);
        this.ip = ip;
        this.port = port;
        this.timeout = timeout;
    }

    public int readInt(int timeout) throws IOException, UJciException {
        long begin = System.currentTimeMillis();

        while (true) {
          try {
            return stream.readInt();
          } catch (SocketTimeoutException e) {
            if (timeout > 0 && timeout - (System.currentTimeMillis() - begin) <= 0) {
              String msg = UErrorCode.codeToMessage(UErrorCode.ER_TIMEOUT);
              throw new SocketTimeoutException(msg);
            }
            BrokerHandler.pingBroker(ip, port, PING_TIMEOUT);
          }
        }
    }

    public int readInt() throws IOException, UJciException {
        return readInt(timeout);
    }

    public void readFully(byte[] b) throws IOException, UJciException {
        readFully(b, timeout);
    }

    public void readFully(byte[] b, int timeout) throws IOException, UJciException {
        long begin = System.currentTimeMillis();

        while (true) {
          try {
            stream.readFully(b);
            return;
          } catch (SocketTimeoutException e) {
            if (timeout > 0 && timeout - (System.currentTimeMillis() - begin) <= 0) {
              String msg = UErrorCode.codeToMessage(UErrorCode.ER_TIMEOUT);
              throw new SocketTimeoutException(msg);
            }
            BrokerHandler.pingBroker(ip, port, PING_TIMEOUT);
          }
        }
    }

    public int readByte(byte[] b, int timeout) throws IOException, UJciException {
        long begin = System.currentTimeMillis();

        while (true) {
          try {
            return stream.read(b);
          } catch (SocketTimeoutException e) {
            if (timeout > 0 && timeout - (System.currentTimeMillis() - begin) <= 0) {
              String msg = UErrorCode.codeToMessage(UErrorCode.ER_TIMEOUT);
              throw new SocketTimeoutException(msg);
            }
            BrokerHandler.pingBroker(ip, port, PING_TIMEOUT);
          }
        }
    }

    public int readByte(byte[] b) throws IOException, UJciException {
        return readByte(b, timeout);
    }

    public int read(byte[] b, int off, int len, int timeout) throws IOException, UJciException {
        long begin = System.currentTimeMillis();
        boolean retry = false;

        while (true) {
          try {
            return stream.read(b, off, len);
          } catch (SocketTimeoutException e) {
            if (timeout > 0 && timeout - (System.currentTimeMillis() - begin) <= 0) {
              String msg = UErrorCode.codeToMessage(UErrorCode.ER_TIMEOUT);
              throw new SocketTimeoutException(msg);
            }
            if (UConnection.protoVersionIsLower(UConnection.PROTOCOL_V9)) {
              BrokerHandler.pingBroker(ip, port, PING_TIMEOUT);
              continue;
            }
            if (BrokerHandler.statusBroker(ip, port, pid, session, PING_TIMEOUT) != 1) {
              if (retry) {
                throw new UJciException(UErrorCode.ER_COMMUNICATION);
              }
              retry = true;
            }
          }
        }
    }

    public int read(byte[] b, int off, int len) throws IOException, UJciException {
        return read(b, off, len, timeout);
    }
    
    public void close() throws IOException {
        stream.close();
    }

}
