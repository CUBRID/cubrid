package nbench.engine;

import javax.tools.*;


import java.io.InputStream;
import java.io.ByteArrayInputStream;
import java.util.Collections;
import java.util.Collection;
import java.util.Map;
import java.util.HashMap;

public class JavaClassLoaderImpl extends ClassLoader {
	private Map<String, JavaFileObject> classes = new HashMap<String, JavaFileObject>();

	public JavaClassLoaderImpl(ClassLoader parent) {
		super(parent);
	}

	public Collection<JavaFileObject> files() {
		return Collections.unmodifiableCollection(classes.values());
	}

	@Override
	protected Class<?> findClass(String qualifiedClassName)
			throws ClassNotFoundException {
		JavaFileObject file = classes.get(qualifiedClassName);
		if (file != null) {
			byte[] bytes = ((JavaFileObjectImpl) file).getByteCode();
			return defineClass(qualifiedClassName, bytes, 0, bytes.length);
		}
		try {
			Class<?> c = Class.forName(qualifiedClassName);
			return c;
		} catch (ClassNotFoundException nf) {
		}
		return super.findClass(qualifiedClassName);
	}

	void add(String qualifiedClassName, JavaFileObject javaFile) {
		classes.put(qualifiedClassName, javaFile);
	}

	@Override
	public InputStream getResourceAsStream(String name) {
		if (name.endsWith(".class")) {
			String qualifiedClassName = name.substring(0,
					name.length() - ".class".length()).replace('/', '.');
			JavaFileObjectImpl file = (JavaFileObjectImpl) classes
					.get(qualifiedClassName);
			if (file != null) {
				return new ByteArrayInputStream(file.getByteCode());
			}
		}
		return super.getResourceAsStream(name);
	}
}