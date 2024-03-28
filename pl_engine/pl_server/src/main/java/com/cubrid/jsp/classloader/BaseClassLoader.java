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
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.util.stream.Stream;

public abstract class BaseClassLoader extends URLClassLoader {

    public BaseClassLoader(Path path, URL[] urls, ClassLoader parent) {
        super(urls, parent);
        init(path);
    }

    private void init(Path path) {
        try {
            addURL(path.toUri().toURL());
            initJar(path);
        } catch (Exception e) {
            Server.log(e);
        }
    }

    private void initJar(Path path) throws IOException {
        try (Stream<Path> files = Files.list(path)) {
            files.filter((file) -> !Files.isDirectory(file) && (file.toString().endsWith(".jar")))
                    .forEach(
                            jar -> {
                                try {
                                    addURL(jar.toUri().toURL());
                                } catch (MalformedURLException e) {
                                    Server.log(e);
                                }
                            });
        } catch (NoSuchFileException e) {
            // ignore
        }
    }

    public Class<?> loadClass(String name) {
        Class c = findLoadedClass(name);
        if (c == null) {
            // find child first
            try {
                c = super.loadClass(name);
            } catch (ClassNotFoundException e) {
                // ignore
            }
        }

        if (c == null && getParent() != null) {
            try {
                c = getParent().loadClass(name);
            } catch (ClassNotFoundException e) {
                // ignore
            }
        }

        if (c == null) {
            try {
                c = getSystemClassLoader().loadClass(name);
            } catch (ClassNotFoundException e) {
                // ignore
            }
        }

        return c;
    }
}
