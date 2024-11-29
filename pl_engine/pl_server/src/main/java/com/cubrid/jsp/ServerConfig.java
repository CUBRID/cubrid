package com.cubrid.jsp;

import java.io.File;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.time.ZoneId;
import java.time.ZoneOffset;
import java.util.HashMap;
import org.apache.commons.lang3.StringUtils;

public class ServerConfig {

    private static final String LOG_DIR = "log";

    private final String name;
    private final String version;

    /* Paths */
    private final String rootPath; // $CUBRID
    private final String dbPath; // $CUBRID_DATABASES

    private final String logPath;
    private final String tmpPath;

    private final String socketType; // TCP or UDS
    private final String socketInfo; // port number or socket file path

    // System settings
    private HashMap<Integer, SysParam> systemParameters;

    private Charset serverCharset;
    private ZoneId serverTimeZone;

    public ServerConfig(
            String name, String version, String rPath, String dbPath, String socketInfo) {
        this.name = name;
        this.version = version;

        this.rootPath = rPath;
        this.dbPath = dbPath;

        this.logPath =
                rootPath + File.separatorChar + LOG_DIR + File.separatorChar + name + "_java.log";

        String cubridTmpEnv = System.getenv("CUBRID_TMP");
        this.tmpPath =
                (cubridTmpEnv != null) ? cubridTmpEnv : this.rootPath + File.separatorChar + "tmp";

        this.socketInfo = socketInfo;
        this.socketType = StringUtils.isNumeric(socketInfo) ? "TCP" : "UDS";

        this.systemParameters = new HashMap<Integer, SysParam>();
        this.serverTimeZone = null;
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

    public String getTmpPath() {
        return tmpPath;
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

    public HashMap<Integer, SysParam> getSystemParameters() {
        return systemParameters;
    }

    public ZoneId getTimeZone() {
        if (serverTimeZone == null) {
            // get the timezone from the system parameters
            SysParam sysParam = systemParameters.get(SysParam.TIMEZONE);
            serverTimeZone = ZoneId.of(sysParam.getParamValue().toString());
        }

        if (serverTimeZone == null) {
            // if the timezone is not set, use the default timezone (UTC)
            serverTimeZone = ZoneOffset.UTC;
        }

        return serverTimeZone;
    }

    public String getCharsetString() {
        SysParam sysParam = systemParameters.get(SysParam.INTL_COLLATION);
        String collation = sysParam.getParamValue().toString();
        String codeset = null;
        String[] codesetList = collation.split("_");
        if (codesetList == null) {
            codeset = collation;
        } else {
            codeset = codesetList[0];
        }

        // test charset
        try {
            serverCharset = Charset.forName(codeset);
        } catch (Exception e) {
            // java.nio.charset.IllegalCharsetNameException
            serverCharset = StandardCharsets.UTF_8;
            codeset = "utf-8";
        }
        // System.out.println(serverCharset);
        return codeset;
    }
}
