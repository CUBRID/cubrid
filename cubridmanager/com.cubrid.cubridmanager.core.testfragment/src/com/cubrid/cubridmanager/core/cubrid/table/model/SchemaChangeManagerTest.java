package com.cubrid.cubridmanager.core.cubrid.table.model;

import junit.framework.TestCase;

public class SchemaChangeManagerTest extends
		TestCase {
	SchemaChangeManager schemaChangeManager=new SchemaChangeManager();
	//schema info
	String tableName = "game";
	String owner = "PUBLIC";
	String type = "user";
	String virtual = "normal";
	//instance attribute
	String attributeName = "nation_code";
	String dataType = "integer";
	String inherit = "game";
	boolean isNotNull = true;
	boolean shared = false;
	boolean unique = true;
	String defaultv = "";

	//pk info
	String pkName = "pk_game_host_year_event_code_athlete_code";
	String pkType = "PRIMARY KEY";
	String[] attributes = { "host_year", "event_code", "athlete_code" };

	//fk info
	String fkName = "fk_game_event_code";
	String fkType = "FOREIGN KEY";
	String[] fkAttributes = { "event_code" };
	String[] fkRules = { "REFERENCES event", "ON DELETE RESTRICT",
			"ON UPDATE RESTRICT" };

	public void testGetDDL() {
		SchemaInfo schema1 = new SchemaInfo();

		schema1.setClassname(tableName);
		schema1.setOwner(owner);
		schema1.setVirtual(virtual);
		schema1.setType(type);



		DBAttribute a = new DBAttribute();
		a.setName(attributeName);
		a.setType(dataType);
		a.setInherit(inherit);
		a.setNotNull(isNotNull);
		a.setShared(false);
		a.setUnique(unique);
		a.setDefault(defaultv);
		schema1.addAttribute(a);


		schema1.addClassAttribute(a);


		Constraint pk = new Constraint();
		pk.setName(pkName);
		pk.setType(pkType);
		pk.addAttribute(attributes[0]);
		pk.addAttribute(attributes[1]);
		pk.addAttribute(attributes[2]);
		schema1.addConstraint(pk);
//
//		System.out.println(schemaChangeManager.getDDL(schema1));

	}
}
