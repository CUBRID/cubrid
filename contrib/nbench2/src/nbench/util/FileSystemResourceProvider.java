package nbench.util;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.FileInputStream;
import java.io.OutputStream;
import java.net.URL;

import nbench.common.NBenchException;
import nbench.common.ResourceIfs;
import nbench.common.ResourceProviderIfs;

//
// This resourceIfs provider should be MT-safe
//
public class FileSystemResourceProvider implements ResourceProviderIfs {
	private String base_dir_with_slash;

	private class FileResource implements ResourceIfs {
		private String resourceString;
		private File file;
		private FileInputStream fis;
		private FileOutputStream fos;


		FileResource(String resourceString, File file) throws Exception {
			this.resourceString = resourceString;
			this.file = file;
			fis = null;
			fos = null;
		}

		@Override
		public void close() throws NBenchException {
			if (fis != null) {
				try {
					fis.close();
					fis = null;
				} catch (Exception e) {
					throw new NBenchException(e);
				}
			}
			if (fos != null) {
				try {
					fos.close();
					fos = null;
				} catch (Exception e) {
					throw new NBenchException(e);
				}
			}
		}

		@Override
		public InputStream getResourceInputStream() throws Exception {
			if (fis == null) {
				fis = new FileInputStream(file);
			}
			return fis;
		}

		@Override
		public URL getURL() throws Exception {
			return file.toURI().toURL();
		}

		@Override
		public String getResourceString() {
			return resourceString;
		}
		
		@Override
		public OutputStream getResourceOutputStream() throws Exception {
			if (fos == null) {
				fos = new FileOutputStream(file);
			}
			return fos;
		}
		
		@Override
		public boolean exists() {
			return file.exists();
		}
	}

	public FileSystemResourceProvider(String base_dir) {
		if (base_dir.charAt(base_dir.length() - 1) != File.separatorChar) {
			this.base_dir_with_slash = base_dir + File.separator;
		} else {
			this.base_dir_with_slash = base_dir;
		}
	}

	private void checkResourceFormat(String s) throws NBenchException {
		if (!s.startsWith("res:")) {
			throw new NBenchException("invalid resourceIfs format:" + s);
		}
	}

	@Override
	public ResourceIfs getResource(String resource) throws Exception {
		checkResourceFormat(resource);
		resource = resource.substring("res:".length());
		File file = new File(base_dir_with_slash + resource);
		if (!file.exists()) {
			return null;
		}
		FileResource res = new FileResource(resource, file);
		return res;
	}

	@Override
	public ResourceIfs newResource(String resourceString) throws Exception {
		checkResourceFormat(resourceString);
		resourceString = resourceString.substring("res:".length());

		File df;
		int idx = resourceString.lastIndexOf(File.separator);
		if (idx != -1) {
			String dir_part = resourceString.substring(0, idx);
			df = new File(base_dir_with_slash + dir_part);
		} else {
			df = new File(base_dir_with_slash);
		}
		
		if (df.exists()) {
			if (!df.isDirectory()) {
				throw new NBenchException(df + " is not directory");
			}
		} else {
			df.mkdirs();
		}
		
		File f = new File(base_dir_with_slash + resourceString);
		FileResource res = new FileResource(resourceString, f);
		return res;
	}

	@Override
	public void removeResource(String resource) throws Exception {
		checkResourceFormat(resource);
		resource = resource.substring("res:".length());
		File file = new File(base_dir_with_slash + resource);
		if (file.exists()) {
			file.delete();
		}
	}

}
