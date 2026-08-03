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

package com.cubrid.jsp.classloader;

import com.cubrid.jsp.Server;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.FileTime;

public class ClassPathManager {
    private static Path dbPath = null;
    private static Path staticPath = null;
    private static Path dynamicPath = null;

    private static Path getDbPath() {
        if (dbPath == null) {
            dbPath = Paths.get(Server.getServerConfig().getDatabasePath());
            assert dbPath.toFile().exists();
        }
        return dbPath;
    }

    public static Path getDynamicPath() {
        if (dynamicPath == null) {
            dynamicPath = getDbPath().resolve("java/");
            createDirIfNotExists(dynamicPath);
        }
        return dynamicPath;
    }

    public static Path getStaticPath() {
        if (staticPath == null) {
            staticPath = getDbPath().resolve("java_static/");
            createDirIfNotExists(staticPath);
        }
        return staticPath;
    }

    public static FileTime getLastModifiedTimeOfDynamicPath() {
        FileTime lastModifiedTime;
        try {
            lastModifiedTime = Files.getLastModifiedTime(getDynamicPath());
        } catch (IOException e) {
            Server.log(e);
            return null;
        }
        return lastModifiedTime;
    }

    private static void createDirIfNotExists(Path path) {
        if (path.toFile().exists() == false) {
            try {
                Files.createDirectories(path);
            } catch (IOException e) {
                Server.log(e);
                System.exit(1);
            }
        }
    }
}
