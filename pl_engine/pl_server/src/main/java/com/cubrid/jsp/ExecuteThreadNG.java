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

package com.cubrid.jsp;

import com.cubrid.jsp.command.CommandHandler;
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.context.ContextManager;
import com.cubrid.jsp.data.CUBRIDUnpacker;
import com.cubrid.jsp.protocol.Header;
import java.io.IOException;
import java.net.Socket;
import java.nio.ByteBuffer;

public class ExecuteThreadNG extends Thread {
    private final ConnectionHandler connectionHandler;
    private final CommandHandler commandHandler;
    private Context ctx;

    public ExecuteThreadNG(Socket client) throws IOException {
        this.connectionHandler = new ConnectionHandler(client);
        this.commandHandler = new CommandHandler();
    }

    @Override
    public void run() {
        try {
            while (!Thread.interrupted()) {
                ByteBuffer inputBuffer = connectionHandler.receiveBuffer();
                Header header = processHeader(inputBuffer);

                ContextManager.registerThread(Thread.currentThread().getId(), ctx.getSessionId());
                commandHandler.handleRequest(header, ctx, connectionHandler);
                ContextManager.deregisterThread(Thread.currentThread().getId());
            }
        } catch (Exception e) {
            e.printStackTrace();
        } finally {
            connectionHandler.close();
        }
    }

    private Header processHeader(ByteBuffer inputBuffer) throws Exception {
        CUBRIDUnpacker unpacker = new CUBRIDUnpacker();
        unpacker.setBuffer(inputBuffer);
        Header header = new Header(unpacker);
        ctx = ContextManager.getContext(header.id);
        ctx.checkHeader(header);
        return header;
    }
}
