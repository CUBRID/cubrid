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
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.BasicFileAttributes;
import java.nio.file.attribute.FileTime;
import java.util.stream.Stream;

public class StoredProcedureClassLoader extends URLClassLoader {
    private static volatile StoredProcedureClassLoader instance = null;

    private static final String ROOT_PATH = Server.getSpPath() + "/java/";
    private static final Path root = Paths.get(ROOT_PATH);

    private FileTime lastModified = null;

    /* For singleton */
    public static synchronized StoredProcedureClassLoader getInstance() {
        if (instance == null) {
            instance = new StoredProcedureClassLoader();
        }

        return instance;
    }

    private StoredProcedureClassLoader() {
        super(new URL[0]);
        init();
    }

    private void init() {
        try {
            addURL(root.toUri().toURL());
            initJar ();
            lastModified = getLastModifiedTime(root);
        } catch (Exception e) {
            Server.log(e);
        }
    }

    private void initJar() {
        try(Stream<Path> files = Files.list(root)){
            files.filter((file) -> !Files.isDirectory(file) &&
            (file.toString().endsWith(".jar")))
            .forEach(jar -> {
                try {
                    addURL(jar.toUri().toURL());
                } catch (MalformedURLException e) {
                    Server.log(e);
                }
            });
        } catch (NoSuchFileException nsfe) {
            // ignore
        } catch (IOException e) {
            Server.log(e);
        } 
    }

    public Class<?> loadClass(String name) throws ClassNotFoundException {
        try {
            if (!isModified()) {
                return super.loadClass(name);
            }

            instance = new StoredProcedureClassLoader();
            return instance.loadClass(name);
        } catch (IOException e) {
            return null;
        }
    }

    private boolean isModified() throws IOException {
        return lastModified.compareTo(getLastModifiedTime(root)) != 0;
    }

    private FileTime getLastModifiedTime(Path path) throws IOException {
        BasicFileAttributes attr = Files.readAttributes(path, BasicFileAttributes.class);
        FileTime lastModifiedTime = attr.lastModifiedTime();
        return lastModifiedTime;
    }
}
