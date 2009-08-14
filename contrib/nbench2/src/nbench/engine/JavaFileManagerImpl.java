package nbench.engine;

import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

import javax.tools.FileObject;
import javax.tools.JavaFileManager;
import javax.tools.ForwardingJavaFileManager;
import javax.tools.JavaFileObject;
import javax.tools.StandardLocation;
import javax.tools.JavaFileObject.Kind;


public class JavaFileManagerImpl extends ForwardingJavaFileManager<JavaFileManager> {
	private JavaClassLoaderImpl classLoader;
	private Map<URI, JavaFileObject> fileObjects = new HashMap<URI, JavaFileObject>();

	static private URI toURI(String name) {
		try {
			return new URI(name);
		} catch (URISyntaxException e) {
			throw new RuntimeException(e);
		}
	}

	public JavaFileManagerImpl(JavaFileManager fileManager,
			JavaClassLoaderImpl classLoader) {
		super(fileManager);
		this.classLoader = classLoader;
	}

	public ClassLoader getClassLoader() {
		return classLoader;
	}

	@Override
	public FileObject getFileForInput(Location location, String packageName,
			String relativeName) throws IOException {
		FileObject o = fileObjects
				.get(uri(location, packageName, relativeName));
		if (o != null)
			return o;
		return super.getFileForInput(location, packageName, relativeName);
	}

	public void putFileForInput(StandardLocation location, String packageName,
			String relativeName, JavaFileObject file) {
		fileObjects.put(uri(location, packageName, relativeName), file);
	}

	private URI uri(Location location, String packageName, String relativeName) {
		return toURI(location.getName() + '/' + packageName + '/'
				+ relativeName);
	}

	@Override
	public JavaFileObject getJavaFileForOutput(Location location,
			String qualifiedName, Kind kind, FileObject outputFile)
			throws IOException {
		JavaFileObject file = new JavaFileObjectImpl(qualifiedName, kind);
		classLoader.add(qualifiedName, file);
		return file;
	}

	@Override
	public ClassLoader getClassLoader(JavaFileManager.Location location) {
		return classLoader;
	}

	@Override
	public String inferBinaryName(Location loc, JavaFileObject file) {
		String result;

		if (file instanceof JavaFileObjectImpl)
			result = file.getName();
		else
			result = super.inferBinaryName(loc, file);
		return result;
	}

	@Override
	public Iterable<JavaFileObject> list(Location location, String packageName,
			Set<Kind> kinds, boolean recurse) throws IOException {
		Iterable<JavaFileObject> result = super.list(location, packageName,
				kinds, recurse);
		ArrayList<JavaFileObject> files = new ArrayList<JavaFileObject>();
		if (location == StandardLocation.CLASS_PATH
				&& kinds.contains(JavaFileObject.Kind.CLASS)) {
			for (JavaFileObject file : fileObjects.values()) {
				if (file.getKind() == Kind.CLASS
						&& file.getName().startsWith(packageName))
					files.add(file);
			}
			files.addAll(classLoader.files());
		} else if (location == StandardLocation.SOURCE_PATH
				&& kinds.contains(JavaFileObject.Kind.SOURCE)) {
			for (JavaFileObject file : fileObjects.values()) {
				if (file.getKind() == Kind.SOURCE
						&& file.getName().startsWith(packageName))
					files.add(file);
			}
		}
		for (JavaFileObject file : result) {
			files.add(file);
		}
		return files;
	}
}