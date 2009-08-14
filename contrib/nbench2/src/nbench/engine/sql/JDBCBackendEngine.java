package nbench.engine.sql;

import java.util.Properties;
import java.sql.Driver;
import java.sql.DriverManager;

import javax.script.ScriptEngineManager;
import nbench.common.*;

public class JDBCBackendEngine implements BackendEngineIfs {
	String url;
	FrameImpl impl;
	ScriptEngineManager MGR;

	
	@Override
	public void configure(Properties props, ResourceProviderIfs rp,
			ClassLoader loader) throws NBenchException {
		try {
			MGR = new ScriptEngineManager();
			
			url = props.getProperty("jdbc_url");
			Driver d = (Driver) Class.forName(props.getProperty("driver"),
					true, loader).newInstance();
			HookDriver.setDriver(d);
			DriverManager.registerDriver(new HookDriver());
			ResourceIfs resource = rp.getResource(props
					.getProperty("frame_impl"));
			FrameImplXMLParser parser = new FrameImplXMLParser();
			try {
				impl = parser.parseFrameImplXML(resource);
			} catch (Exception e) {
				throw new NBenchException(e);
			}
			
		} catch (Exception e) {
			throw new NBenchException(e);
		}
	}

	@Override
	public BackendEngineClientIfs createClient(
			PerfLogIfs listener) throws NBenchException {
		try {
			return new SQLClientImpl(MGR.getEngineByName("JavaScript"), impl,
					listener, DriverManager.getConnection(url), null, false);
		} catch (Exception e) {
			throw new NBenchException(e);
		}
	}
}
