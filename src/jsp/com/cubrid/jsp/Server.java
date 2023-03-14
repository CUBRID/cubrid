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
import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.management.ManagementFactory;
import java.lang.management.RuntimeMXBean;
import java.net.ServerSocket;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Logger;
import org.newsclub.net.unix.AFUNIXServerSocket;
import org.newsclub.net.unix.AFUNIXSocketAddress;

public class Server {
    private static final Logger logger = Logger.getLogger("com.cubrid.jsp");
    private static final String LOG_DIR = "log";

    private static String serverName;
    private static String spPath;
    private static String rootPath;
    private static String udsPath;

    private static List<String> jvmArguments = null;

    private static final int udsPortNumber = -1;
    private int portNumber = 0;
    private Thread socketListener = null;
    private AtomicBoolean shutdown;

    private static Server serverInstance = null;

    private static LoggingThread loggingThread = null;

    private Server(
            String name, String path, String version, String rPath, String uPath, String port)
            throws IOException, ClassNotFoundException {
        serverName = name;
        spPath = path;
        rootPath = rPath;
        udsPath = uPath;
        shutdown = new AtomicBoolean(false);

        ServerSocket serverSocket = null;
        int port_number = Integer.parseInt(port);
        try {
            String logFilePath =
                    rootPath
                            + File.separatorChar
                            + LOG_DIR
                            + File.separatorChar
                            + serverName
                            + "_java.log";
            loggingThread = new LoggingThread(logFilePath);
            loggingThread.start();

            if (OSValidator.IS_UNIX && port_number == -1) {
                final File socketFile = new File(udsPath);
                if (socketFile.exists()) {
                    socketFile.delete();
                }

                /* check parent directory */
                File socketDir = socketFile.getParentFile();
                if (!socketDir.exists()) {
                    socketDir.mkdirs();
                }

                AFUNIXSocketAddress sockAddr = AFUNIXSocketAddress.of(socketFile);
                serverSocket = AFUNIXServerSocket.bindOn(sockAddr);
                portNumber = udsPortNumber;
            } else {
                serverSocket = new ServerSocket(port_number);
                portNumber = serverSocket.getLocalPort();
            }
        } catch (Exception e) {
            log(e);
            e.printStackTrace();
            System.exit(1);
        }

        if (serverSocket != null) {
            socketListener = new ListenerThread(serverSocket);

            System.setSecurityManager(new SpSecurityManager());
            System.setProperty("cubrid.server.version", version);
            Class.forName("com.cubrid.jsp.jdbc.CUBRIDServerSideDriver");

            getJVMArguments(); /* store jvm options */
            Files.createDirectories(Paths.get(StoredProcedureClassLoader.ROOT_PATH));
        } else {
            /* error, serverSocket is not properly initialized */
            System.exit(1);
        }
    }

    private void startSocketListener() {
        if (socketListener != null) {
            socketListener.setDaemon(true);
            socketListener.start();
        }
    }

    private void stopSocketListener() {
        if (socketListener != null) {
            socketListener.interrupt();
            socketListener = null;
        }
    }

    public int getPortNumber() {
        return portNumber;
    }

    public static Server getServer() {
        return serverInstance;
    }

    public static String getServerName() {
        return serverName;
    }

    public static int getServerPort() {
        try {
            return getServer().getPortNumber();
        } catch (Exception e) {
            return -1;
        }
    }

    public static String getRootPath() {
        return rootPath;
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
            serverInstance = new Server(args[0], args[1], args[2], args[3], args[4], args[5]);
            serverInstance.startSocketListener();
            return getServerPort();
        } catch (Exception e) {
            e.printStackTrace();
        }

        return -1;
    }

    public static void stop(int status) {
        getServer().setShutdown();
        getServer().stopSocketListener();
        loggingThread.interrupt();
        System.exit(status);
    }

    public static void main(String[] args) {
        Server.start(args);
    }

    public static void log(Throwable ex) {
        StringWriter sw = new StringWriter();
        ex.printStackTrace(new PrintWriter(sw));
        String exceptionAsString = sw.toString();
        loggingThread.log(exceptionAsString);
    }

    public void setShutdown() {
        shutdown.set(true);
    }

    public boolean getShutdown() {
        return shutdown.get();
    }
}
