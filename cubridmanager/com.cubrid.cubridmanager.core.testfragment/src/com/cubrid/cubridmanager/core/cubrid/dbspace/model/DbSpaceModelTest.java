package com.cubrid.cubridmanager.core.cubrid.dbspace.model;

import java.util.List;

import junit.framework.TestCase;

public class DbSpaceModelTest extends TestCase {
	public void testModelAddVolumeDbInfo() {
		AddVolumeDbInfo bean = new AddVolumeDbInfo();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setVolname("volname");
		assertEquals(bean.getVolname(), "volname");
		bean.setPurpose("purpose");
		assertEquals(bean.getPurpose(), "purpose");
		bean.setPath("path");
		assertEquals(bean.getPath(), "path");
		bean.setNumberofpage("numberofpage");
		assertEquals(bean.getNumberofpage(), "numberofpage");
		bean.setSize_need_mb("size_need_mb");
		assertEquals(bean.getSize_need_mb(), "size_need_mb");
		assertEquals(bean.getTaskName(), "addvoldb");
	}

	public void testModelAutoAddVolumeLogInfo() {
		AutoAddVolumeLogInfo bean = new AutoAddVolumeLogInfo();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setVolname("volname");
		assertEquals(bean.getVolname(), "volname");
		bean.setPurpose("purpose");
		assertEquals(bean.getPurpose(), "purpose");
		bean.setPage("page");
		assertEquals(bean.getPage(), "page");
		bean.setTime("time");
		assertEquals(bean.getTime(), "time");
		bean.setOutcome("outcome");
		assertEquals(bean.getOutcome(), "outcome");
	}

	public void testModelAutoAddVolumeLogList() {
		AutoAddVolumeLogList bean = new AutoAddVolumeLogList();
		bean.setAutoAddVolumeLogList(null);
		assertEquals(bean.getAutoAddVolumeLogList(), null);
		assertEquals(bean.getTaskName(), "getautoaddvollog");
	}

	public void testModelDbSpaceInfo() {
		DbSpaceInfo bean = new DbSpaceInfo();
		bean.setSpacename("spacename");
		assertEquals(bean.getSpacename(), "spacename");
		bean.setType("type");
		assertEquals(bean.getType(), "type");
		bean.setLocation("location");
		assertEquals(bean.getLocation(), "location");
		bean.setTotalpage(9);
		assertEquals(bean.getTotalpage(), 9);
		bean.setFreepage(8);
		assertEquals(bean.getFreepage(), 8);
		bean.setDate("date");
		assertEquals(bean.getDate(), "date");
		bean.setTotalPageStr("totalPageStr");
		assertEquals(bean.getTotalPageStr(), "totalPageStr");
		bean.setTotalSizeStr("totalSizeStr");
		assertEquals(bean.getTotalSizeStr(), "totalSizeStr");
		bean.getVolumeCount();
		bean.plusVolumeCount();
	}

	public void testModelDbSpaceInfoList() {
		DbSpaceInfoList bean = new DbSpaceInfoList();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setPagesize(8);
		assertEquals(bean.getPagesize(), 8);
		bean.setFreespace(9);
		assertEquals(bean.getFreespace(), 9);
		bean.addSpaceinfo(new DbSpaceInfo());
		assertEquals(bean.getSpaceinfo() instanceof List, true);
		bean.setSpaceinfo(null);
		assertEquals(bean.getSpaceinfo(), null);
		bean.removeSpaceinfo(new DbSpaceInfo());
		bean.getTaskName();
		bean.getSpaceInfoMap();
	}

	public void testModelGetAddVolumeStatusInfo() {
		GetAddVolumeStatusInfo bean = new GetAddVolumeStatusInfo();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setNote("note");
		assertEquals(bean.getNote(), "note");
		bean.setFreespace("freespace");
		assertEquals(bean.getFreespace(), "freespace");
		bean.setVolpath("volpath");
		assertEquals(bean.getVolpath(), "volpath");
		assertEquals(bean.getTaskName(), "getaddvolstatus");
	}

	public void testModelGetAutoAddVolumeInfo() {
		GetAutoAddVolumeInfo bean = new GetAutoAddVolumeInfo();
		bean.setDbname("dbname");
		assertEquals(bean.getDbname(), "dbname");
		bean.setData("data");
		assertEquals(bean.getData(), "data");
		bean.setData_warn_outofspace("data_warn_outofspace");
		assertEquals(bean.getData_warn_outofspace(), "data_warn_outofspace");
		bean.setData_ext_page("data_ext_page");
		assertEquals(bean.getData_ext_page(), "data_ext_page");
		bean.setIndex("index");
		assertEquals(bean.getIndex(), "index");
		bean.setIndex_warn_outofspace("index_warn_outofspace");
		assertEquals(bean.getIndex_warn_outofspace(), "index_warn_outofspace");
		bean.setIndex_ext_page("index_ext_page");
		assertEquals(bean.getIndex_ext_page(), "index_ext_page");
		assertEquals(bean.getTaskName(), "getautoaddvol");

	}

	public void testModelVolumeType() {
		assertEquals(VolumeType.ACTIVE_LOG.toString(), "ACTIVE_LOG");
	}

}
