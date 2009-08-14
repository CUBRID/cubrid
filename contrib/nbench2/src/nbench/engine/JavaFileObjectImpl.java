package nbench.engine;

import javax.tools.*;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.net.URI;
import java.net.URISyntaxException;

public class JavaFileObjectImpl extends SimpleJavaFileObject {
	private ByteArrayOutputStream byteCode;
	private CharSequence source;

	static private URI toURI(String name) {
		try {
			return new URI(name);
		} catch (URISyntaxException e) {
			throw new RuntimeException(e);
		}
	}
	
	public JavaFileObjectImpl(String baseName, CharSequence source) {
		super(toURI(baseName
				+ ".java"), Kind.SOURCE);
		this.source = source;
	}

	public JavaFileObjectImpl(String name, Kind kind) {
		super(toURI(name), kind);
		source = null;
	}

	@Override
	public CharSequence getCharContent(boolean ignoreEncodingErrors)
			throws UnsupportedOperationException {
		if (source == null)
			throw new UnsupportedOperationException("getCharContent()");
		return source;
	}

	@Override
	public InputStream openInputStream() {
		return new ByteArrayInputStream(getByteCode());
	}

	@Override
	public OutputStream openOutputStream() {
		byteCode = new ByteArrayOutputStream();
		return byteCode;
	}

	byte[] getByteCode() {
		return byteCode.toByteArray();
	}
}