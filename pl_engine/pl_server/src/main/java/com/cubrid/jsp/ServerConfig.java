/*
 *
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
        this.serverCharset = StandardCharsets.UTF_8;
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

    public Charset getServerCharset() {
        return serverCharset;
    }

    public int getServerCodesetId() {
        return SysParam.getCodesetId(serverCharset);
    }

    public void initializeCharset() {
        SysParam sysParam = systemParameters.get(SysParam.INTL_COLLATION);
        String collation = sysParam.getParamValue().toString();
        String codeset = null;
        String[] codesetList = collation.split("_");
        if (codesetList == null) {
            codeset = collation;
        } else {
            codeset = codesetList[0];
        }

        // tune the codeset name java understands
        if (codeset.equalsIgnoreCase("utf-8") || codeset.equalsIgnoreCase("utf8")) {
            codeset = "UTF-8";
        } else if (codeset.equalsIgnoreCase("ksc-euc") || codeset.equalsIgnoreCase("euckr")) {
            codeset = "EUC-KR";
        } else if (codeset.equalsIgnoreCase("iso88591")) {
            codeset = "ISO-8859-1";
        } else if (codeset.equalsIgnoreCase("ascii")) {
            codeset = "UTF-8"; // ascii is a subset of UTF-8
        }

        try {
            serverCharset = Charset.forName(codeset);
        } catch (Exception e) {
            // java.nio.charset.IllegalCharsetNameException
            Server.log(e);
            serverCharset = StandardCharsets.UTF_8;
        }
        System.setProperty("file.encoding", serverCharset.toString());
    }
}
