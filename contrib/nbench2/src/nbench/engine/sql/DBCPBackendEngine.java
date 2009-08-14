package nbench.engine.sql;

import java.sql.Driver;
import java.sql.DriverManager;
import java.util.Properties;

import javax.script.ScriptEngineManager;

import nbench.common.BackendEngineClientIfs;
import nbench.common.BackendEngineIfs;
import nbench.common.PerfLogIfs;
import nbench.common.NBenchException;
import nbench.common.ResourceIfs;
import nbench.common.ResourceProviderIfs;
import org.apache.commons.dbcp.BasicDataSource;

public class DBCPBackendEngine implements BackendEngineIfs {
	String url;
	FrameImpl impl;
	int max_connection_pool;
	boolean pool_prepared_statement;
	int max_open_prepared_statement;
	boolean reconnect;
	ScriptEngineManager MGR;
	BasicDataSource ds;

	@Override
	public void configure(Properties props, ResourceProviderIfs rp,
			ClassLoader loader) throws NBenchException {
		try {
			// initialize script engine
			MGR = new ScriptEngineManager();

			// compile frame file
			ResourceIfs resource = rp.getResource(props
					.getProperty("frame_impl"));
			FrameImplXMLParser parser = new FrameImplXMLParser();
			try {
				impl = parser.parseFrameImplXML(resource);
			} catch (Exception e) {
				throw new NBenchException(e);
			}

			// DBCP driver setting
			Driver d = (Driver) Class.forName(props.getProperty("driver"),
					true, loader).newInstance();
			HookDriver.setDriver(d);
			DriverManager.registerDriver(new HookDriver());
			url = props.getProperty("url");
			max_connection_pool = Integer.valueOf(props
					.getProperty("max_connection_pool"));
			pool_prepared_statement = Boolean.valueOf(props
					.getProperty("pool_prepared_statement"));
			max_open_prepared_statement = Integer.valueOf(props
					.getProperty("max_open_prepared_statement"));
			if (props.getProperty("reconnect") != null) {
				reconnect = Boolean.valueOf(props.getProperty("reconnect"));
			} else {
				reconnect = false;
			}

			ds = new BasicDataSource();
			ds.setDriverClassName("nbench.engine.sql.HookDriver");
			ds.setUrl(url);
			ds.setMaxActive(max_connection_pool);
			if (pool_prepared_statement) {
				ds.setPoolPreparedStatements(true);
				ds.setMaxOpenPreparedStatements(max_open_prepared_statement);
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
					listener, ds.getConnection(), ds, reconnect);
		} catch (Exception e) {
			throw new NBenchException(e);
		}
	}
}
