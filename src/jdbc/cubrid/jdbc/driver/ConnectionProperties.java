/*
 * Copyright (C) 2008 Search Solution Corporation. 
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

package cubrid.jdbc.driver;

import java.io.UnsupportedEncodingException;
import java.lang.reflect.Field;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.Properties;
import java.util.StringTokenizer;

import cubrid.jdbc.jci.BrokerHealthCheck;
import cubrid.jdbc.jci.UConnection;

public class ConnectionProperties {
    static ArrayList<Field> PROPERTY_LIST = new ArrayList<Field>();
    static {
	try {
	    Field[] declaredFields = ConnectionProperties.class.getDeclaredFields();

	    for (int i = 0; i < declaredFields.length; i++) {
		if (ConnectionProperties.ConnectionProperty.class
			.isAssignableFrom(declaredFields[i].getType())) {
		    PROPERTY_LIST.add(declaredFields[i]);
		}
	    }
	} catch (Exception e) {
	    RuntimeException rtEx = new RuntimeException();
	    rtEx.initCause(e);
	    throw rtEx;
	}
    }

    public void setProperties(String properties) throws SQLException {
	if (properties == null) {
	    return;
	}

	Properties p = new Properties();
	StringTokenizer st = new StringTokenizer(properties, "?&;");
	while (st.hasMoreTokens()) {
	    String prop = st.nextToken();
	    StringTokenizer pt = new StringTokenizer(prop, "=");
	    if (pt.hasMoreTokens()) {
		String name = pt.nextToken().toLowerCase();
		if (pt.hasMoreTokens()) {
		    String value = pt.nextToken();
		    p.put(name, value);
		}
	    }
	}
	setProperties(p);
    }

    public void setProperties(Properties info) throws SQLException {
	if (info == null) {
	    return;
	}

	int numProperties = PROPERTY_LIST.size();
	for (int i = 0; i < numProperties; i++) {
	    Field propertyField = (Field) PROPERTY_LIST.get(i);

	    try {
		ConnectionProperty prop = (ConnectionProperty) propertyField.get(this);
		String propName = prop.getPropertyName().toLowerCase();
		String propValue = info.getProperty(propName);
		if (propValue == null) {
		    propName = prop.getPropertyName();
		    propValue = info.getProperty(propName);
		}

		if (propValue != null) {
		    prop.setValue(propValue);
		}
	    } catch (IllegalAccessException iae) {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_url,
			" illegal access properties", null);
	    }
	}
	if (this.getConnLoadBal() && this.getAltHosts() == null){
	    this.connLoadBal.setValue("false");
	}
	if (this.getReconnectTime() < (BrokerHealthCheck.MONITORING_INTERVAL / 1000)) {
	    this.rcTime.setValue((Integer)(BrokerHealthCheck.MONITORING_INTERVAL / 1000));
	}
    }

    abstract class ConnectionProperty {
	String propertyName;
	Object defaultValue;
	Object valueAsObject;
	long lowerBound;
	long upperBound;

	public ConnectionProperty() {
	    // dummy constructor
	}

	ConnectionProperty(String propertyName, Object defaultValue,
		long lowerBound, long upperBound) {
	    this.propertyName = propertyName;
	    this.defaultValue = defaultValue;
	    this.valueAsObject = defaultValue;
	    this.lowerBound = lowerBound;
	    this.upperBound = upperBound;
	}

	abstract boolean validateValue(Object o);

	Object getValueAsObject() {
	    return valueAsObject;
	}

	void setValue(Object o) throws SQLException {
	    if (validateValue(o)) {
		valueAsObject = o;
	    } else {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_url,
			" '" + o + "' uncompitable value for the " + propertyName, null);
	    }
	}

	String getPropertyName() {
	    return propertyName;
	}
    }

    class BooleanConnectionProperty extends ConnectionProperty {
	String[] allowableValues = {"true", "false", "yes", "no", "on", "off"};

	BooleanConnectionProperty (String propertyName, Object defaultValue) {
	    super(propertyName, defaultValue, 0, 0);
	}

	boolean getValueAsBoolean() {
	    if (valueAsObject instanceof String) {
		valueAsObject = Boolean.valueOf((String) valueAsObject);
	    }
	    return ((Boolean) valueAsObject).booleanValue();
	}

	@Override
	boolean validateValue(Object o) {
	    if (o instanceof Boolean) {
		return true;
	    } else if (o instanceof String) {
		for (int i = 0; i < allowableValues.length; i++) {
		    if (allowableValues[i].equalsIgnoreCase((String) o)) {
			return true;
		    }
		}
	    }
	    return false;
	}
    }

    class IntegerConnectionProperty extends ConnectionProperty {
	IntegerConnectionProperty (String propertyName, Object defaultValue,
		int lowerBound, int upperBound) {
	    super(propertyName, defaultValue, lowerBound, upperBound);
	}

	int getValueAsInteger() {
	    if (valueAsObject instanceof String) {
		valueAsObject = Integer.valueOf((String) valueAsObject);
	    }
	    return ((Integer) valueAsObject).intValue();
	}

	@Override
	boolean validateValue(Object o) {
	    long value;

	    if (o instanceof Integer) {
		value = ((Integer)o).intValue();
	    } else if (o instanceof String) {
		try {
		    value = Integer.valueOf((String) o).intValue();
		} catch (NumberFormatException e) {
		    return false;
		}
	    } else {
		return false;
	    }

	    if (value < this.lowerBound || value > this.upperBound) {
		return false;
	    } else {
		return true;
	    }
	}
    }

    class LongConnectionProperty extends ConnectionProperty {
	LongConnectionProperty (String propertyName, Object defaultValue,
		long lowerBound, long upperBound) {
	    super(propertyName, defaultValue, lowerBound, upperBound);
	}

	long getValueAsLong() {
	    if (valueAsObject instanceof String) {
		valueAsObject = Long.valueOf((String) valueAsObject);
	    }
	    return ((Long) valueAsObject).longValue();
	}

	@Override
	boolean validateValue(Object o) {
	    if (o instanceof Long || o instanceof Integer) {
		return true;
	    } else if (o instanceof String) {
		try {
		    Long.valueOf((String) o);
		} catch (NumberFormatException e) {
		    return false;
		}
		return true;
	    }
	    return false;
	}
    }

    class StringConnectionProperty extends ConnectionProperty {
	StringConnectionProperty (String propertyName, Object defaultValue) {
	    super(propertyName, defaultValue, 0, 0);
	}

	String getValueAsString() {
	    return (String) valueAsObject;
	}

	@Override
	boolean validateValue(Object o) {
	    if (o != null) {
		return true;
	    }
	    return false;
	}
    }

    class CharSetConnectionProperty extends StringConnectionProperty {
	CharSetConnectionProperty (String propertyName, Object defaultValue) {
	    super(propertyName, defaultValue);
	}

	String getValueAsString() {
	    if (valueAsObject == null) {
		return System.getProperty("file.encoding");
	    }
	    return (String) valueAsObject;
	}

	@Override
	boolean validateValue(Object o) {
	    if (o instanceof String) {
		try {
		    byte[] s = { 0 };
		    new String(s, (String) o);
		    return true;
		} catch (UnsupportedEncodingException e) {
		    return false;
		}
	    }
	    return false;
	}
    }

    class ZeroDateTimeBehaviorConnectionProperty extends StringConnectionProperty {
	ZeroDateTimeBehaviorConnectionProperty (String propertyName, Object defaultValue) {
	    super(propertyName, defaultValue);
	}

	@Override
	boolean validateValue(Object o) {
	    if (o instanceof String) {
		String behavior = (String) o;
		if (behavior.equals(UConnection.ZERO_DATETIME_BEHAVIOR_CONVERT_TO_NULL)
			|| behavior.equals(UConnection.ZERO_DATETIME_BEHAVIOR_EXCEPTION)
			|| behavior.equals(UConnection.ZERO_DATETIME_BEHAVIOR_ROUND)) {
		    return true;
		}
	    }
	    return false;
	}
    }
	
    class ResultWithCUBRIDTypesConnectionProperty extends StringConnectionProperty {
	ResultWithCUBRIDTypesConnectionProperty (String propertyName, Object defaultValue) {
	    super(propertyName, defaultValue);
	}

	@Override
	boolean validateValue(Object o) {
	    if (o instanceof String) {
		String support = (String) o;
		if (support.equals(UConnection.RESULT_WITH_CUBRID_TYPES_YES)
			|| support.equals(UConnection.RESULT_WITH_CUBRID_TYPES_NO)) {
		    return true;
		}
	    }
	    return false;
	}
    }
	
    BooleanConnectionProperty logOnException = new BooleanConnectionProperty(
	    "logOnException", false);

    BooleanConnectionProperty logSlowQueries = new BooleanConnectionProperty(
	    "logSlowQueries", false);

    IntegerConnectionProperty slowQueryThresholdMillis = new IntegerConnectionProperty(
	    "slowQueryThresholdMillis", 60000, 0, Integer.MAX_VALUE);

    StringConnectionProperty logFile = new StringConnectionProperty(
	    "logFile", "cubrid_jdbc.log");

    CharSetConnectionProperty charSet = new CharSetConnectionProperty(
	    "charSet", System.getProperty("file.encoding"));

    IntegerConnectionProperty rcTime = new IntegerConnectionProperty(
	    "rcTime", 600, 0, Integer.MAX_VALUE);

    IntegerConnectionProperty queryTimeout = new IntegerConnectionProperty(
	    "queryTimeout", 0, 0, UConnection.MAX_QUERY_TIMEOUT);

    private int getDefaultConnectTimeout() {
	int timeout = java.sql.DriverManager.getLoginTimeout();
	return timeout > 0 ? timeout : 30;
    }

    IntegerConnectionProperty connectTimeout = new IntegerConnectionProperty(
	    "connectTimeout", getDefaultConnectTimeout(), 0, UConnection.MAX_CONNECT_TIMEOUT);

    StringConnectionProperty altHosts = new StringConnectionProperty(
	    "altHosts", null);

    BooleanConnectionProperty connLoadBal = new BooleanConnectionProperty(
	    "loadBalance", false);

    ZeroDateTimeBehaviorConnectionProperty zeroDateTimeBehavior = new ZeroDateTimeBehaviorConnectionProperty(
	    "zeroDateTimeBehavior", UConnection.ZERO_DATETIME_BEHAVIOR_EXCEPTION);
	
    ResultWithCUBRIDTypesConnectionProperty  resultWithCUBRIDTypes = new ResultWithCUBRIDTypesConnectionProperty(
	    "resultWithCUBRIDTypes", UConnection.RESULT_WITH_CUBRID_TYPES_YES);
    
    BooleanConnectionProperty useLazyConnection = new BooleanConnectionProperty(
	    "useLazyConnection", false);

    BooleanConnectionProperty useOldBooleanValue = new BooleanConnectionProperty(
	    "useOldBooleanValue", false);

    BooleanConnectionProperty oracleStyleEmptyString = new BooleanConnectionProperty(
            "oracleStyleEmptyString", false);

    BooleanConnectionProperty useSSL = new BooleanConnectionProperty("useSSL", false);

    IntegerConnectionProperty clientCacheSize = new IntegerConnectionProperty(
    		"clientCacheSize", 1, 1, 1024);
    
    BooleanConnectionProperty holdCursor = new BooleanConnectionProperty("hold_cursor", true);
    public boolean getLogOnException() {
	return logOnException.getValueAsBoolean();
    }

    public boolean getLogSlowQueries() {
	return logSlowQueries.getValueAsBoolean();
    }

    public int getSlowQueryThresholdMillis() {
	return slowQueryThresholdMillis.getValueAsInteger();
    }

    public String getLogFile() {
	return logFile.getValueAsString();
    }

    public String getCharSet() {
	return charSet.getValueAsString();
    }

    public int getReconnectTime() {
	return rcTime.getValueAsInteger();
    }

    public int getQueryTimeout() {
	return queryTimeout.getValueAsInteger();
    }

    public int getConnectTimeout() {
	return connectTimeout.getValueAsInteger();
    }

    public String getAltHosts() {
	return altHosts.getValueAsString();
    }

    public boolean getConnLoadBal() {
	return connLoadBal.getValueAsBoolean();
    }
    public String getZeroDateTimeBehavior() {
	return zeroDateTimeBehavior.getValueAsString();
    }
	
    public String getResultWithCUBRIDTypes() {
	return resultWithCUBRIDTypes.getValueAsString();
    }	

    public boolean getUseLazyConnection() {
	return useLazyConnection.getValueAsBoolean();
    }

    public boolean getUseOldBooleanValue() {
        return useOldBooleanValue.getValueAsBoolean();
    }

    public boolean getOracleStyleEmptyString() {
        return oracleStyleEmptyString.getValueAsBoolean();
    }

    public boolean getUseSSL() {
        return useSSL.getValueAsBoolean();
    }
    
    public int getClientCacheSize() {
    	return clientCacheSize.getValueAsInteger();
    }

    public int getHoldCursor() {
        int holdability = ResultSet.HOLD_CURSORS_OVER_COMMIT;
        if (holdCursor.getValueAsBoolean() == false) {
            holdability = ResultSet.CLOSE_CURSORS_AT_COMMIT;
        }
        return holdability;
    }
}
