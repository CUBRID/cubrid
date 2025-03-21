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
import java.lang.management.ManagementFactory;
import java.lang.management.RuntimeMXBean;
import java.net.ServerSocket;
import java.net.Socket;

public class ListenerThread extends Thread {

    private ServerSocket serverSocket = null;

    // Exponential Backoff
    private static final int MAX_RETRIES = 5;
    private static long[] backoff_times = new long[MAX_RETRIES];

    private int attempt = 0;

    static {
        long initialDelay = 100; // 100ms
        for (int i = 0; i < MAX_RETRIES; i++) {
            backoff_times[i] = initialDelay * (1L << i);
        }
    }

    ListenerThread(ServerSocket serverSocket) {
        super();
        this.serverSocket = serverSocket;
    }

    @Override
    public void run() {

        Socket client;
        ExecuteThread execThread;

        while (!Thread.interrupted() && attempt < MAX_RETRIES) {
            client = null;
            execThread = null;
            try {
                client = serverSocket.accept();
                client.setTcpNoDelay(true);
                execThread = new ExecuteThread(client);
                execThread.start();
                attempt = 0;
            } catch (Throwable e) {
                Server.log(e);

                // For the case when execThread.run() is not invoked due to an exception.
                //   NOTE: execThread.closeSocket() is called at the end of execThread.run()
                if (execThread == null) {
                    if (client != null) {
                        try {
                            client.close();
                        } catch (IOException ee) {
                            // do nothing
                        }
                    }
                } else {
                    execThread.closeSocket(); // client is closed in this method
                }

                try {
                    Thread.sleep(backoff_times[attempt]);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                }
                attempt++;
            }
        }

        try {
            serverSocket.close();
        } catch (IOException e) {
            // do nothing
        }
        serverSocket = null;

        try {
            killProcess();
        } catch (Exception e) {
            Server.log(e);
        }
        Server.stop(1);
    }

    public ServerSocket getServerSocket() {
        return serverSocket;
    }

    private static void killProcess() throws IOException, InterruptedException {
        RuntimeMXBean runtimeMXBean = ManagementFactory.getRuntimeMXBean();
        String jvmName = runtimeMXBean.getName();
        long pid = Long.parseLong(jvmName.split("@")[0]);

        String command = null;
        if (OSValidator.IS_UNIX) {
            command = "kill -SIGABRT " + pid;
        } else {
            command = "taskkill /F /PID " + pid;
        }
        Server.log("Command: " + command);
        Server.log("Process " + pid + " is going to be terminated");

        Server.flushLog();

        Thread.sleep(1000);

        Runtime.getRuntime().exec(command);
    }
}
