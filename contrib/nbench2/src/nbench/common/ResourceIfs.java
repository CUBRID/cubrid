package nbench.common;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.URL;

public interface ResourceIfs {
	InputStream getResourceInputStream() throws Exception;

	OutputStream getResourceOutputStream() throws Exception;
	
	void close() throws NBenchException;

	URL getURL() throws Exception;

	String getResourceString();
	
	boolean exists();
}
