package com.cubrid.cubridmanager.ui.broker.editor.internal;

public class BrokerIntervalSetting {

	private String serverName = null;
	private String brokerName = null;
	private String interval = null;
	private boolean isOn = false;

	public BrokerIntervalSetting() {
	}

	public BrokerIntervalSetting(String serverName, String brokerName,
			String interval, boolean isOn) {
		super();
		this.serverName = serverName;
		this.brokerName = brokerName;
		this.interval = interval;
		this.isOn = isOn;
	}

	public String getInterval() {
		return interval;
	}

	public void setInterval(String interval) {
		this.interval = interval;
	}

	public boolean isOn() {
		return isOn;
	}

	public void setOn(boolean isOn) {
		this.isOn = isOn;
	}

	public String getServerName() {
		return serverName;
	}

	public void setServerName(String serverName) {
		this.serverName = serverName;
	}

	public String getBrokerName() {
		return brokerName;
	}

	public void setBrokerName(String brokerName) {
		this.brokerName = brokerName;
	}

}
