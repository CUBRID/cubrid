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

import java.io.IOException;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;

public class Accepter implements Runnable {

    @Override
    public void run() {
    }
    /*
    private ServerSocketChannel channel = null;
    private SocketAddress address = null;
    private Selector selector = null;

    Acceptor(SocketAddress addr) {
        this.address = addr;
    }

    @Override
    public void run() {
        channel = ServerSocketChannel.open();
        channel.bind(this.address);
        channel.configureBlocking(false);

        Selector selector = Selector.open ();
        channel.register(selector, SelectionKey.OP_ACCEPT);
    
        Socket client = null;
        while (true) {
            selector.select ();


            try {
                client = serverSocket.accept();
                client.setTcpNoDelay(true);
                Thread execThread = new ExecuteThread(client);
                execThread.setContextClassLoader(new StoredProcedureClassLoader());
                execThread.start();
            } catch (IOException e) {
                Server.log(e);
                break;
            }
        }

        try {
            serverSocket.close();
        } catch (IOException e) {
            // do nothing
        }
        serverSocket = null;
    }

    public ServerSocket getServerSocket() {
        return serverSocket;
    }
    */
}
