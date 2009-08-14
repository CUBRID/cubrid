package com.cubrid.cubridmanager.core.cubrid.table.model;

import java.text.ParseException;

import com.cubrid.cubridmanager.core.CommonTool;

import junit.framework.TestCase;

public class DBAttributeTest extends
		TestCase {

	public void testGetShownType() {
		String type;
		String shownType;
		String expectedShownType;
//		{"CHAR","character"},
//		{"VARCHAR","character varying(1073741823)"},
//		{"VARCHAR","character varying"},	
		type="character(1)";
		expectedShownType="CHAR(1)";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);		
		
		type="character(4)";
		expectedShownType="CHAR(4)";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="character varying(1073741823)";
		expectedShownType="VARCHAR(1073741823)";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);		
		
		type="character varying(30)";
		expectedShownType="VARCHAR(30)";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
//		{"NCHAR","national character"},
//		{"NCHAR VARYING","national character varying"},
		type="national character(1)";
		expectedShownType="NCHAR(1)";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);		
		
		type="national character varying(4)";
		expectedShownType="NCHAR VARYING(4)";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
//		{"BIT","bit"},
//		{"BIT VARYING","bit varying"},		
		type="bit(10)";
		expectedShownType="BIT(10)";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);		
		
		type="bit varying(30)";
		expectedShownType="BIT VARYING(30)";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
//		{"NUMERIC","numeric"},
//		{"INTEGER","integer"},
//		{"SMALLINT","smallint"},		
//		{"MONETARY","monetary"},
//		{"FLOAT","float"},
//		{"DOUBLE","double"},
		type="numeric(15,0)";
		expectedShownType="NUMERIC(15,0)";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="integer";
		expectedShownType="INTEGER";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="smallint";
		expectedShownType="SMALLINT";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="monetary";
		expectedShownType="MONETARY";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="float";
		expectedShownType="FLOAT";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="double";
		expectedShownType="DOUBLE";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
//		{"DATE","date"},
//		{"TIME","time"},
//		{"TIMESTAMP","timestamp"},		
		
		type="date";
		expectedShownType="DATE";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="time";
		expectedShownType="TIME";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="timestamp";
		expectedShownType="TIMESTAMP";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
//		{"SET","set_of"},
//		{"MULTISET","multiset_of"},
//		{"SEQUENCE","sequence_of"}
		
		type="set_of(numeric(15,0))";
		expectedShownType="SET(NUMERIC(15,0))";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="multiset_of(numeric(15,0))";
		expectedShownType="MULTISET(NUMERIC(15,0))";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="sequence_of(numeric(15,0))";
		expectedShownType="SEQUENCE(NUMERIC(15,0))";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
		
		type="set_of(multiset_of(numeric(15,0)))";
		expectedShownType="SET(MULTISET(NUMERIC(15,0)))";
		shownType=DataType.getShownType(type); 
		assertEquals(expectedShownType, shownType);	
	}
	public void testGetType() {
		String expectedType;
		String type;
		String shownType;
//		{"CHAR","character"},
//		{"VARCHAR","character varying(1073741823)"},
//		{"VARCHAR","character varying"},	
		expectedType="character(1)";
		shownType="CHAR(1)";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);		
		
		expectedType="character(4)";
		shownType="CHAR(4)";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="character varying(1073741823)";
		shownType="VARCHAR(1073741823)";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);		
		
		expectedType="character varying(30)";
		shownType="VARCHAR(30)";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
//		{"NCHAR","national character"},
//		{"NCHAR VARYING","national character varying"},
		expectedType="national character(1)";
		shownType="NCHAR(1)";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);		
		
		expectedType="national character varying(4)";
		shownType="NCHAR VARYING(4)";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
//		{"BIT","bit"},
//		{"BIT VARYING","bit varying"},		
		expectedType="bit(10)";
		shownType="BIT(10)";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);		
		
		expectedType="bit varying(30)";
		shownType="BIT VARYING(30)";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
//		{"NUMERIC","numeric"},
//		{"INTEGER","integer"},
//		{"SMALLINT","smallint"},		
//		{"MONETARY","monetary"},
//		{"FLOAT","float"},
//		{"DOUBLE","double"},
		expectedType="numeric(15,0)";
		shownType="NUMERIC(15,0)";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="integer";
		shownType="INTEGER";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="smallint";
		shownType="SMALLINT";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="monetary";
		shownType="MONETARY";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="float";
		shownType="FLOAT";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="double";
		shownType="DOUBLE";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
//		{"DATE","date"},
//		{"TIME","time"},
//		{"TIMESTAMP","timestamp"},		
		
		expectedType="date";
		shownType="DATE";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="time";
		shownType="TIME";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="timestamp";
		shownType="TIMESTAMP";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
//		{"SET","set_of"},
//		{"MULTISET","multiset_of"},
//		{"SEQUENCE","sequence_of"}
		
		expectedType="set_of(numeric(15,0))";
		shownType="SET(NUMERIC(15,0))";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="multiset_of(numeric(15,0))";
		shownType="MULTISET(NUMERIC(15,0))";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="sequence_of(numeric(15,0))";
		shownType="SEQUENCE(NUMERIC(15,0))";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
		
		expectedType="set_of(multiset_of(numeric(15,0)))";
		shownType="SET(MULTISET(NUMERIC(15,0)))";
		type=DataType.getType(shownType); 
		assertEquals(expectedType, type);	
	}
	String atttype=null;
	String attdeft=null;
	String retattdeft=null;

	public void testTime() throws ParseException{
		atttype="time";
		
		attdeft="am 09:53:06";
		retattdeft="TIME'09:53:06'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53:06 am";
		retattdeft="TIME'09:53:06'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="19:53:06";
		retattdeft="TIME'19:53:06'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="am 09:53";
		retattdeft="TIME'09:53:00'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53 am";
		retattdeft="TIME'09:53:00'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="19:53";
		retattdeft="TIME'19:53:00'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="sysTime";
		retattdeft="systime";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="currentTime";
		retattdeft="current_time";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	}
	
	public void testDate() throws ParseException{
		atttype="Date";
		
		attdeft="02/23/2009";
		retattdeft="DATE'02/23/2009'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009/02/23";
		retattdeft="DATE'02/23/2009'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009-02-23";
		retattdeft="DATE'02/23/2009'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="02/23";
		retattdeft="DATE'02/23'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="sysDate";
		retattdeft="sysdate";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="currentdATe";
		retattdeft="current_date";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
	}
	public void testTimestamp() throws ParseException{
		atttype="tImestamp";
		
		attdeft="2009/02/23 am 09:53:08";
		retattdeft="TIMESTAMP'02/23/2009 09:53:08'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009-02-23 am 09:53:08";
		retattdeft="TIMESTAMP'02/23/2009 09:53:08'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009/02/23 09:53:08";
		retattdeft="TIMESTAMP'02/23/2009 09:53:08'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009-02-23 09:53:08";
		retattdeft="TIMESTAMP'02/23/2009 09:53:08'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53:08 am 02/23/2009";
		retattdeft="TIMESTAMP'02/23/2009 09:53:08'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53:08 02/23/2009";
		retattdeft="TIMESTAMP'02/23/2009 09:53:08'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009/02/23 am 09:53";
		retattdeft="TIMESTAMP'02/23/2009 09:53:00'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009-02-23 am 09:53";
		retattdeft="TIMESTAMP'02/23/2009 09:53:00'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009/02/23 09:53";
		retattdeft="TIMESTAMP'02/23/2009 09:53:00'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009-02-23 09:53";
		retattdeft="TIMESTAMP'02/23/2009 09:53:00'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53 am 02/23/2009";
		retattdeft="TIMESTAMP'02/23/2009 09:53:00'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53 02/23/2009";
		retattdeft="TIMESTAMP'02/23/2009 09:53:00'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="systImestamp";
		retattdeft="systimestamp";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="currenttImestamp";
		retattdeft="current_timestamp";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	
		attdeft="09:53:05 02/23";
		retattdeft="TIMESTAMP'09:53:05 02/23'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53:05 pm 02/23";
		retattdeft="TIMESTAMP'09:53:05 pm 02/23'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		

		attdeft="02/23 09:53:05";
		retattdeft="TIMESTAMP'02/23 09:53:05'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="02/23 09:53:05 am";
		retattdeft="TIMESTAMP'02/23 09:53:05 am'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	}
	public void test(){
		attdeft="09:53:08.333 02/23/2009";
		boolean t=CommonTool.validateTimestamp(attdeft,"HH:mm:ss.SSS MM/dd/yyyy");
		System.out.println(t);
	}
	public void testDatetime() throws ParseException{
		atttype="datetime";
		
		attdeft="2009/02/23 am 09:53:08.333";
		retattdeft="DATETIME'2009-02-23 09:53:08.333'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009-02-23 am 09:53:08.333";
		retattdeft="DATETIME'2009-02-23 09:53:08.333'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009/02/23 09:53:08.333";
		retattdeft="DATETIME'2009-02-23 09:53:08.333'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009-02-23 09:53:08.333";
		retattdeft="DATETIME'2009-02-23 09:53:08.333'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53:08.333 am 02/23/2009";
		retattdeft="DATETIME'2009-02-23 09:53:08.333'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53:08.333 02/23/2009";
		retattdeft="DATETIME'2009-02-23 09:53:08.333'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009/02/23 am 09:53";
		retattdeft="DATETIME'2009-02-23 09:53:00.000'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009-02-23 am 09:53";
		retattdeft="DATETIME'2009-02-23 09:53:00.000'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009/02/23 09:53";
		retattdeft="DATETIME'2009-02-23 09:53:00.000'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="2009-02-23 09:53";
		retattdeft="DATETIME'2009-02-23 09:53:00.000'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53 am 02/23/2009";
		retattdeft="DATETIME'2009-02-23 09:53:00.000'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53 02/23/2009";
		retattdeft="DATETIME'2009-02-23 09:53:00.000'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="sysDatetime";
		retattdeft="sysdatetime";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="sys_Datetime";
		retattdeft="sysdatetime";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="current_Datetime";
		retattdeft="current_datetime";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	
		attdeft="09:53:05 02/23";
		retattdeft="DATETIME'09:53:05 02/23'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="09:53:05 pm 02/23";
		retattdeft="DATETIME'09:53:05 pm 02/23'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		

		attdeft="02/23 09:53:05";
		retattdeft="DATETIME'02/23 09:53:05'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="02/23 09:53:05 am";
		retattdeft="DATETIME'02/23 09:53:05 am'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	}
	public void testChar() throws ParseException{
		atttype="char";
		
		attdeft="'abc'";
		retattdeft="'abc'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="'ab''c'";
		retattdeft="'ab''c'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="abc";
		retattdeft="'abc'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="abc ";
		retattdeft="'abc '";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="ab'c";
		retattdeft="'ab''c'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	}
	
	public void testVarchar() throws ParseException{
		atttype="varchar";
		
		attdeft="'abc'";
		retattdeft="'abc'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="'ab''c'";
		retattdeft="'ab''c'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="abc";
		retattdeft="'abc'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="abc ";
		retattdeft="'abc '";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="ab'c";
		retattdeft="'ab''c'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	}
	public void testNchar() throws ParseException{
		atttype="nchar";
		
		attdeft="N'Ù∏‰Ûbc'";
		retattdeft="N'Ù∏‰Ûbc'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="N'Ù∏‰Ûb''c'";
		retattdeft="N'Ù∏‰Ûb''c'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="abc";
		retattdeft="N'abc'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="ab'c";
		retattdeft="N'ab''c'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	}
	
	public void testNcharVar() throws ParseException{
		atttype="nchar varying";
		
		attdeft="N'Ù∏‰Ûbc'";
		retattdeft="N'Ù∏‰Ûbc'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="N'Ù∏‰Ûb''c'";
		retattdeft="N'Ù∏‰Ûb''c'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="abc";
		retattdeft="N'abc'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="ab'c";
		retattdeft="N'ab''c'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	}
	public void testBit() throws ParseException{
		atttype="bit";
		
		attdeft="B'001'";
		retattdeft="B'001'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="X'001'";
		retattdeft="X'001'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="001";
		retattdeft="B'001'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));	
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	}
	public void testBitVar() throws ParseException{
		atttype="bit varying";
		
		attdeft="B'001'";
		retattdeft="B'001'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="X'001'";
		retattdeft="X'001'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
		
		attdeft="001";
		retattdeft="B'001'";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));	
		
		attdeft="";
		retattdeft="";
		assertEquals(retattdeft, DBAttribute.formatValue(atttype, attdeft));
	}

}
