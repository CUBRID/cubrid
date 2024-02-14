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

import com.cubrid.jsp.classloader.ClassLoaderManager;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.management.ManagementFactory;
import java.lang.management.RuntimeMXBean;
import java.net.ServerSocket;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.logging.Logger;
import org.newsclub.net.unix.AFUNIXServerSocket;
import org.newsclub.net.unix.AFUNIXSocketAddress;

public class Server {
    private static final Logger logger = Logger.getLogger("com.cubrid.jsp");

    public static final int PORT_NUMBER_UNKNOWN = -2;
    public static final int PORT_NUMBER_UDS = -1;

    private static AtomicBoolean shutdown = new AtomicBoolean(false);

    private static ServerConfig config = null;

    private static List<String> jvmArguments = null;
    private static int portNumber = PORT_NUMBER_UNKNOWN;

    private static Server serverInstance = null;

    private static Thread socketListener = null;
    private static LoggingThread loggingThread = null;

    private Server(ServerConfig conf) throws IOException, ClassNotFoundException {
        config = conf;

        // Server's security manager should be set first
        System.setSecurityManager(new SpSecurityManager());

        // initialize env
        initailizeEnvironments(config);

        // initialize logger
        initializeLogger(config);

        // initialize class loader
        Files.createDirectories(ClassLoaderManager.getRootPath());

        // initialize socket
        initializeSocket(config);

        System.setProperty("cubrid.server.version", config.getVersion());
        Class.forName("com.cubrid.jsp.jdbc.CUBRIDServerSideDriver");

        // store JVM options
        getJVMArguments();
    }

    private synchronized void initailizeEnvironments(ServerConfig config) {
        System.setProperty("java.io.tmpdir", config.getTmpPath());
    }

    private synchronized void initializeLogger(ServerConfig config)
            throws SecurityException, IOException {
        Path logPath = Paths.get(config.getLogPath());
        if (Files.notExists(logPath)) {
            Files.createDirectories(logPath.getParent());
            Files.createFile(logPath);
        }

        loggingThread = new LoggingThread(logPath);
        loggingThread.start();
    }

    private synchronized void initializeSocket(ServerConfig config) throws IOException {
        ServerSocket serverSocket = null;
        if (config.getSocketType().equals("UDS")) {
            final Path socketFile = Paths.get(config.getSocketInfo());

            // create parent directory if exists
            if (!Files.exists(socketFile.getParent())) {
                Files.createDirectories(socketFile.getParent());
            }

            // remove previous socket file
            Files.deleteIfExists(socketFile);

            AFUNIXSocketAddress sockAddr = AFUNIXSocketAddress.of(socketFile);
            serverSocket = AFUNIXServerSocket.bindOn(sockAddr);
            portNumber = PORT_NUMBER_UDS;
        } else {
            portNumber = Integer.parseInt(config.getSocketInfo());
            serverSocket = new ServerSocket(portNumber);
            portNumber = serverSocket.getLocalPort();
        }

        socketListener = new ListenerThread(serverSocket);
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

    public static Server getServer() {
        return serverInstance;
    }

    public static ServerConfig getServerConfig() {
        return config;
    }

    public String getServerName() {
        return config.getName();
    }

    public int getServerPort() {
        return portNumber;
    }

    public Path getRootPath() {
        if (getServer() != null) {
            return Paths.get(config.getRootPath());
        } else {
            return null;
        }
    }

    public Path getDatabasePath() {
        if (getServer() != null) {
            return Paths.get(config.getDatabasePath());
        } else {
            return null;
        }
    }

    public static List<String> getJVMArguments() {
        if (jvmArguments == null) {
            RuntimeMXBean runtimeMxBean = ManagementFactory.getRuntimeMXBean();
            jvmArguments = runtimeMxBean.getInputArguments();
        }

        return jvmArguments;
    }

    /* For JNI */
    public static int start(String[] args) throws Exception {
        try {
            String name = args[0];
            String dbPath = args[1];
            String version = args[2];
            String envRoot = args[3];
            String udsPath = args[4];
            String port = args[5];

            String socketInfo = null;
            int port_number = Integer.parseInt(port);
            if (OSValidator.IS_UNIX && port_number == PORT_NUMBER_UDS) {
                socketInfo = udsPath;
            } else {
                socketInfo = port;
            }

            ServerConfig config = new ServerConfig(name, version, envRoot, dbPath, socketInfo);

            return startWithConfig(config);
        } catch (Exception e) {
            /* error, serverSocket is not properly initialized */
            shutdown.set(true);
            if (loggingThread != null && loggingThread.isRunning()) {
                log(e);
                loggingThread.interrupt();
            }
            throw e;
        }
    }

    /* Entry point (main) */
    public static int startWithConfig(ServerConfig config)
            throws ClassNotFoundException, IOException {
        if (config == null) {
            throw new IllegalArgumentException("ServerConfig is null");
        }

        serverInstance = new Server(config);
        serverInstance.startSocketListener();

        return Server.getServer().getServerPort();
    }

    public static void stop(int status) {
        if (serverInstance != null) {
            serverInstance.setShutdown();
            serverInstance.stopSocketListener();

            loggingThread.interrupt();

            serverInstance = null;
        }
    }

    public static void main(String[] args) throws Exception {
        Server.start(args);
    }

    public static void log(Throwable ex) {
        StringWriter sw = new StringWriter();
        ex.printStackTrace(new PrintWriter(sw));
        String exceptionAsString = sw.toString();
        loggingThread.log(exceptionAsString);
    }

    public static void log(String str) {
        loggingThread.log(str);
    }

    public void setShutdown() {
        shutdown.set(true);
    }

    public boolean getShutdown() {
        return shutdown.get();
    }
}
