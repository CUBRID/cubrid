package com.cubrid.jsp;

import java.io.File;
import org.apache.commons.lang3.StringUtils;

public class ServerConfig {

    private static final String LOG_DIR = "log";

    private String name;
    private String version;

    /* Paths */
    private String rootPath; // $CUBRID
    private String dbPath; // $CUBRID_DATABASES

    private String logPath;

    private String socketType; // TCP or UDS
    private String socketInfo; // port number or socket file path

    public ServerConfig(
            String name, String version, String rPath, String dbPath, String socketInfo) {
        this.name = name;
        this.version = version;

        this.rootPath = rPath;
        this.dbPath = dbPath;

        this.logPath =
                rootPath + File.separatorChar + LOG_DIR + File.separatorChar + name + "_java.log";

        this.socketInfo = socketInfo;
        this.socketType = StringUtils.isNumeric(socketInfo) ? "TCP" : "UDS";
    }

    public String getName() {
        return name;
    }

    public String getVersion() {
        return version;
    }

    public String getRootPath() {
        return rootPath;
    }

    public String getLogPath() {
        return logPath;
    }

    public String getSocketType() {
        return socketType;
    }

    public String getDatabasePath() {
        return dbPath;
    }

    public String getSocketInfo() {
        return socketInfo;
    }
}
