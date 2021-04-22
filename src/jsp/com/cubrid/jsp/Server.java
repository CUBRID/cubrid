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

import java.io.File;
import java.io.IOException;
import java.lang.management.ManagementFactory;
import java.lang.management.RuntimeMXBean;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.FileHandler;
import java.util.logging.Level;
import java.util.logging.Logger;

public class Server {
    private static final Logger logger = Logger.getLogger("com.cubrid.jsp");
    private static final String LOG_DIR = "log";

    private static String serverName;
    private static String spPath;
    private static String rootPath;

    private static List<String> jvmArguments = null;

    private ServerSocket serverSocket;
    private Thread socketListener;
    private AtomicBoolean shutdown;

    private static Server serverInstance = null;

    private Server(String name, String path, String version, String rPath, String port)
            throws IOException {
        serverName = name;
        spPath = path;
        rootPath = rPath;
        shutdown = new AtomicBoolean(false);

        try {
            int port_number = Integer.parseInt(port);
            serverSocket = new ServerSocket(port_number);

            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            System.setSecurityManager(new SpSecurityManager());
            System.setProperty("cubrid.server.version", version);

            getJVMArguments(); /* store jvm options */
        } catch (Exception e) {
            log(e);
            e.printStackTrace();
        }

        socketListener =
                new Thread(
                        new Runnable() {
                            public void run() {
                                Socket client = null;
                                while (true) {
                                    try {
                                        client = serverSocket.accept();
                                        client.setTcpNoDelay(true);
                                        new ExecuteThread(client).start();
                                    } catch (IOException e) {
                                        log(e);
                                        break;
                                    }
                                }
                            }
                        });
    }

    private void startSocketListener() {
        socketListener.setDaemon(true);
        socketListener.start();
    }

    private void stopSocketListener() {
        try {
            serverSocket.close();
            serverSocket = null;
        } catch (IOException e) {
            log(e);
        }
    }

    public ServerSocket getServerSocket() {
        return serverSocket;
    }

    public static Server getServer() {
        return serverInstance;
    }

    public static String getServerName() {
        return serverName;
    }

    public static int getServerPort() {
        try {
            return getServer().getServerSocket().getLocalPort();
        } catch (Exception e) {
            return -1;
        }
    }

    public static String getSpPath() {
        return spPath;
    }

    public static List<String> getJVMArguments() {
        if (jvmArguments == null) {
            RuntimeMXBean runtimeMxBean = ManagementFactory.getRuntimeMXBean();
            jvmArguments = runtimeMxBean.getInputArguments();
        }
        
        return jvmArguments;
    }

    public static int start(String[] args) {
        try {
            serverInstance = new Server(args[0], args[1], args[2], args[3], args[4]);
            serverInstance.startSocketListener();
            return serverInstance.getServerSocket().getLocalPort();
        } catch (Exception e) {
            e.printStackTrace();
        }

        return -1;
    }

    public static void stop(int status) {
        getServer().setShutdown();
        getServer().stopSocketListener();
        System.exit(status);
    }

    public static void main(String[] args) {
        Server.start(args);
    }

    public static void log(Throwable ex) {
        FileHandler logHandler = null;

        try {
            logHandler =
                    new FileHandler(
                            rootPath
                                    + File.separatorChar
                                    + LOG_DIR
                                    + File.separatorChar
                                    + serverName
                                    + "_java.log",
                            true);
            logger.addHandler(logHandler);
            logger.log(Level.SEVERE, "", ex);
        } catch (Throwable e) {
        } finally {
            if (logHandler != null) {
                try {
                    logHandler.close();
                    logger.removeHandler(logHandler);
                } catch (Throwable e) {
                }
            }
        }
    }

    public void setShutdown() {
        shutdown.set(true);
    }

    public boolean getShutdown() {
        return shutdown.get();
    }
}
