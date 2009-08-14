package com.cubrid.cubridmanager.core.cubrid.sp.model;

import java.util.ArrayList;

import com.cubrid.cubridmanager.core.SetupJDBCTestCase;

public class SpModelTest extends SetupJDBCTestCase {

	public void testModelSPInfo() {
		SPInfo bean = new SPInfo("spName");
		bean.setSpName("spName");
		assertEquals(bean.getSpName(), "spName");
		bean.setSpType(SPType.FUNCTION);
		assertEquals(bean.getSpType(),SPType.FUNCTION);
		bean.setSpType(SPType.PROCEDURE);
		assertEquals(bean.getSpType(),SPType.PROCEDURE);;

		
		bean.setReturnType("returnType");
		assertEquals(bean.getReturnType(), "returnType");
		bean.setLanguage("language");
		assertEquals(bean.getLanguage(), "language");
		bean.setOwner("owner");
		assertEquals(bean.getOwner(), "owner");
		bean.setTarget("target");
		assertEquals(bean.getTarget(), "target");
		bean.setArgsInfoList(new ArrayList());
		assertEquals(bean.getArgsInfoList().size(), 0);
		 bean.addSPArgsInfo(new SPArgsInfo("spName","spName",
				 -1,"spName",SPArgsType.IN));
		 bean.removeSPArgsInfo(new SPArgsInfo());
	}

	public void testModelSPArgsInfo() {
		SPArgsInfo bean = new SPArgsInfo();
		bean.setSpName("spName");
		assertEquals(bean.getSpName(), "spName");
		bean.setIndex(5);
		assertEquals(bean.getIndex(), 5);
		bean.setArgName("argName");
		assertEquals(bean.getArgName(), "argName");
		bean.setDataType("dataType");
		assertEquals(bean.getDataType(), "dataType");
		bean.setSpArgsType(SPArgsType.IN);
		assertEquals(bean.getSpArgsType(), SPArgsType.IN);
	}
}
