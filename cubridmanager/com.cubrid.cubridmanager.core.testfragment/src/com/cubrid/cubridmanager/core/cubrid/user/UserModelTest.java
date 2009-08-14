package com.cubrid.cubridmanager.core.cubrid.user;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.cubrid.cubridmanager.core.SetupEnvTestCase;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfo;
import com.cubrid.cubridmanager.core.cubrid.user.model.DbUserInfoList;
import com.cubrid.cubridmanager.core.cubrid.user.model.UserGroup;

public class UserModelTest extends SetupEnvTestCase {
	public void testModelDbUserInfo() {
		DbUserInfo bean = new DbUserInfo();
		bean.setDbName("dbName");
		assertEquals(bean.getDbName(), "dbName");
		bean.setName("name");
		assertEquals(bean.getName(), "name");
		bean.setPassword("password");
		assertEquals(bean.getPassword(), "password");
		bean.setNoEncryptPassword("noEncryptPassword");
		assertEquals(bean.getNoEncryptPassword(), "noEncryptPassword");
		bean.addGroups(new UserGroup());
		assertEquals(bean.getGroups() instanceof UserGroup, true);
		bean.addAuthorization(new HashMap());
		assertEquals(bean.getAuthorization() instanceof Map, true);
		bean.setDbaAuthority(true);
		assertEquals(bean.isDbaAuthority(), true);
	}

	public void testModelDbUserInfoList() {
		DbUserInfoList bean = new DbUserInfoList();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.removeUser(new DbUserInfo());
		assertEquals(bean.getTaskName(), "userinfo");
		bean.getUserList();
		bean.addUser(new DbUserInfo());
		assertEquals(bean.getUserList().size(), 1);
		assertEquals(bean.getUserMap() instanceof Map, true);
	}

	public void testModelUserGroup() {
		UserGroup bean = new UserGroup();
		bean.addGroup(new String());
		assertEquals(bean.getGroup() instanceof List, true);
	}
}
