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
