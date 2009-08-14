package nbench.engine.sql;

import java.sql.Connection;
import java.sql.Driver;
import java.sql.DriverPropertyInfo;
import java.sql.SQLException;
import java.util.Properties;

/*
 * DriverManager will refuse to use a driver that is not loaded by the system
 * ClassLoader. As a workaround for this, create a hook JDBC driver instance
 * that delegates actual driver works.
 */
public class HookDriver implements Driver {
	private static Driver driver;

	public HookDriver() {
	}
	
	public synchronized static void setDriver(Driver driver) {
		HookDriver.driver = driver;
	}

	@Override
	public boolean acceptsURL(String url) throws SQLException {
		return driver.acceptsURL(url);
	}

	@Override
	public Connection connect(String url, Properties info)
			throws SQLException {
		return driver.connect(url, info);
	}

	@Override
	public int getMajorVersion() {
		return driver.getMajorVersion();
	}

	@Override
	public int getMinorVersion() {
		return driver.getMinorVersion();
	}

	@Override
	public DriverPropertyInfo[] getPropertyInfo(String url, Properties info)
			throws SQLException {
		return driver.getPropertyInfo(url, info);
	}

	@Override
	public boolean jdbcCompliant() {
		return driver.jdbcCompliant();
	}
}
