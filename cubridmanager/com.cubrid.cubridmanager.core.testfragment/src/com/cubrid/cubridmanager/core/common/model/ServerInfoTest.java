package com.cubrid.cubridmanager.core.common.model;

import java.util.ArrayList;
import java.util.HashMap;

import junit.framework.TestCase;

public class ServerInfoTest extends TestCase {
	public void testModelServerInfo() {
		ServerInfo bean = new ServerInfo();
		ServerUserInfo serverUserInfo = (new ServerUserInfo());
		serverUserInfo.setUserName("a");
		bean.addServerUserInfo(serverUserInfo);
		serverUserInfo = (new ServerUserInfo());
		serverUserInfo.setUserName("b");
		bean.addServerUserInfo(serverUserInfo);
		bean.getServerUserInfo("a");
		bean.setServerName("serverName");
		assertEquals(bean.getServerName(), "serverName");
		bean.setHostAddress("localhost");
		assertEquals(bean.getHostAddress(), "localhost");
		bean.setHostMonPort(11);
		assertEquals(bean.getHostMonPort(), 11);
		bean.setHostJSPort(10);
		assertEquals(bean.getHostJSPort(), 10);
		bean.setUserName("userName");
		assertEquals(bean.getUserName(), "userName");
		bean.setUserPassword("userPassword");
		assertEquals(bean.getUserPassword(), "userPassword");
		bean.setHostToken("hostToken");
		assertEquals(bean.getHostToken(), "hostToken");
		bean.setLoginedUserInfo(new ServerUserInfo());
		assertEquals(bean.getLoginedUserInfo() != null, true);
		bean.setServerUserInfoList(new ArrayList());
		assertEquals(bean.getServerUserInfoList().size(), 0);
		bean.setEnvInfo(new EnvInfo());
		assertEquals(bean.getEnvInfo() == null, false);
		bean.setCubridConfParaMap(new HashMap());
		assertEquals(bean.getCubridConfParaMap().size(), 0);
		bean.setBrokerConfParaMap(new HashMap());
		assertEquals(bean.getBrokerConfParaMap().size(), 0);
		bean.setCmConfParaMap(new HashMap());
		assertEquals(bean.getCmConfParaMap().size(), 0);
		bean.setBrokerInfos(null);
		assertEquals(bean.getBrokerInfos(), null);
		bean.setAllDatabaseList(null);
		assertEquals(bean.getAllDatabaseList(), null);
		bean.getPathSeparator();
		bean.isConnected();
		bean.setConnected(false);
		bean.disConnect();
		bean.getMonitoringTask();
		bean.getServerUserInfo("aaa");
		bean.removeServerUserInfo(new ServerUserInfo());
		bean.removeAllServerUserInfo();
		bean.getLogInfoManager();
		bean.removeAllDatabase();
		bean.isLocalServer();
		bean.getServerOsInfo();
		bean.getCubridConfPara("a", "b");
		bean.setConnected(true);
		bean.removeAllServerUserInfo();
		bean.isLocalServer();
		bean.getCubridConfPara("demodb", "demodb");
	}

}
