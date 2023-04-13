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
import java.time.Instant;
import java.util.Hashtable;

public class ClassLoaderManager {
    private static Hashtable<Path, Instant> lastModifiedMap = new Hashtable<>();

    private static Path root = null;
    private static Path server = null;
    private static Path dynamic = null;

    public static Path getRootPath() {
        if (root == null) {
            String rootPath = Server.getSpPath() + "/java/";
            root = Paths.get(rootPath);
            createDirIfNotExists(root);
        }
        return root;
    }

    public static Path getDynamicPath() {
        if (dynamic == null) {
            dynamic = getRootPath().resolve("dynamic/");
            createDirIfNotExists(dynamic);
        }
        return dynamic;
    }

    public static Path getServerPath() {
        if (server == null) {
            server = getRootPath().resolve("server/");
            createDirIfNotExists(server);
        }
        return server;
    }

    public static boolean isModified(Path path) {
        Instant currentModified = getLastModifiedTimeOfPath(path).toInstant();
        Instant prevModified = lastModifiedMap.get(path);
        if (prevModified != null && currentModified.compareTo(prevModified) == 0) {
            return false;
        } else {
            lastModifiedMap.put(path, currentModified);
            return true;
        }
    }

    public static FileTime getLastModifiedTimeOfPath(Path path) {
        FileTime lastModifiedTime;
        try {
            lastModifiedTime = Files.getLastModifiedTime(path);
        } catch (IOException e) {
            // should not be here...
            return null;
        }
        return lastModifiedTime;
    }

    public static FileTime setLastModifiedTime(Path path, FileTime lastModifiedTime) {
        try {
            lastModifiedTime = Files.getLastModifiedTime(path);
        } catch (IOException e) {
            // should not be here...
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
