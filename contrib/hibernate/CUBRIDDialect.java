package org.hibernate.dialect;

import java.sql.Types;

import org.hibernate.Hibernate;
import org.hibernate.MappingException;
import org.hibernate.cfg.Environment;
import org.hibernate.dialect.function.NoArgSQLFunction;
import org.hibernate.dialect.function.StandardSQLFunction;
import org.hibernate.dialect.function.VarArgsSQLFunction;

public class CUBRIDDialect extends Dialect {
	
    @Override
    protected String getIdentityColumnString() throws MappingException {
		return "auto_increment"; //starts with 1, implicitly
	}
    
    @Override
    public String getIdentitySelectString(String table, String column, int type)
    		throws MappingException {
    	return "select current_val from db_serial where name = '" + (table + "_ai_id").toLowerCase() + "'";
    }
    
    public CUBRIDDialect()
    {
      super();
    
      registerColumnType(Types.BIT,         "bit(8)"            );
      registerColumnType(Types.BIGINT,      "numeric(19,0)"     );
      registerColumnType(Types.SMALLINT,    "short"             );
      registerColumnType(Types.TINYINT,     "short"             );
      registerColumnType(Types.INTEGER,     "integer"           );
      registerColumnType(Types.CHAR,        "char(1)"           );
      registerColumnType(Types.VARCHAR, 4000, "varchar($l)"     );
      registerColumnType(Types.FLOAT,       "float"             );
      registerColumnType(Types.DOUBLE,      "double"            );
      registerColumnType(Types.DATE,        "date"              );
      registerColumnType(Types.TIME,        "time"              );
      registerColumnType(Types.TIMESTAMP,   "timestamp"         );
      registerColumnType(Types.VARBINARY, 2000,"bit varying($l)");
      registerColumnType(Types.NUMERIC,     "numeric($p,$s)"    );
      registerColumnType(Types.BLOB,        "blob"              );
      //registerColumnType(Types.CLOB,        "clob"              );
      registerColumnType(Types.CLOB,        "string"              );
    
      getDefaultProperties().setProperty(Environment.USE_STREAMS_FOR_BINARY, "true");
      getDefaultProperties().setProperty(Environment.STATEMENT_BATCH_SIZE, DEFAULT_BATCH_SIZE);
    
      registerFunction("substring", new StandardSQLFunction("substr",       Hibernate.STRING)   );
      registerFunction("trim",      new StandardSQLFunction("trim")                             );
      registerFunction("length",    new StandardSQLFunction("length",       Hibernate.INTEGER)  );
      registerFunction("bit_length",new StandardSQLFunction("bit_length",   Hibernate.INTEGER)  );
      registerFunction("coalesce",  new StandardSQLFunction("coalesce")                         );
      registerFunction("nullif",    new StandardSQLFunction("nullif")                           );
      registerFunction("abs",       new StandardSQLFunction("abs")                              );
      registerFunction("mod",       new StandardSQLFunction("mod")                              );
      registerFunction("upper",     new StandardSQLFunction("upper")                            );
      registerFunction("lower",     new StandardSQLFunction("lower")                            );
    
      registerFunction("power",     new StandardSQLFunction("power")                            );
      registerFunction("stddev",    new StandardSQLFunction("stddev")                           );
      registerFunction("variance",  new StandardSQLFunction("variance")                         );
      registerFunction("round",     new StandardSQLFunction("round")                            );
      registerFunction("trunc",     new StandardSQLFunction("trunc")                            );
      registerFunction("ceil",      new StandardSQLFunction("ceil")                             );
      registerFunction("floor",     new StandardSQLFunction("floor")                            );
      registerFunction("ltrim",     new StandardSQLFunction("ltrim")                            );
      registerFunction("rtrim",     new StandardSQLFunction("rtrim")                            );
      registerFunction("nvl",       new StandardSQLFunction("nvl")                              );
      registerFunction("nvl2",      new StandardSQLFunction("nvl2")                             );
      registerFunction("sign",      new StandardSQLFunction("sign",         Hibernate.INTEGER)  );
      registerFunction("chr",       new StandardSQLFunction("chr",          Hibernate.CHARACTER));
      registerFunction("to_char",   new StandardSQLFunction("to_char",      Hibernate.STRING)   );
      registerFunction("to_date",   new StandardSQLFunction("to_date",      Hibernate.TIMESTAMP));
      registerFunction("last_day",  new StandardSQLFunction("last_day",     Hibernate.DATE)     );
      registerFunction("instr",     new StandardSQLFunction("instr",        Hibernate.INTEGER)  );
      registerFunction("instrb",    new StandardSQLFunction("instrb",       Hibernate.INTEGER)  );
      registerFunction("lpad",      new StandardSQLFunction("lpad",         Hibernate.STRING)   );
      registerFunction("replace",   new StandardSQLFunction("replace",      Hibernate.STRING)   );
      registerFunction("rpad",      new StandardSQLFunction("rpad",         Hibernate.STRING)   );
      registerFunction("substr",    new StandardSQLFunction("substr",       Hibernate.STRING)   );
      registerFunction("substrb",   new StandardSQLFunction("substrb",      Hibernate.STRING)   );
      registerFunction("translate", new StandardSQLFunction("translate",    Hibernate.STRING)   );
      registerFunction("add_months",        new StandardSQLFunction("add_months",       Hibernate.DATE)             );
      registerFunction("months_between",    new StandardSQLFunction("months_between",   Hibernate.FLOAT)            );
    
      registerFunction("current_date",      new NoArgSQLFunction("current_date",        Hibernate.DATE,     false)  );
      registerFunction("current_time",      new NoArgSQLFunction("current_time",        Hibernate.TIME,     false)  );
      registerFunction("current_timestamp", new NoArgSQLFunction("current_timestamp",   Hibernate.TIMESTAMP,false)  );
      registerFunction("sysdate",           new NoArgSQLFunction("sysdate",             Hibernate.DATE,     false)  );
      registerFunction("systime",           new NoArgSQLFunction("systime",             Hibernate.TIME,     false)  );
      registerFunction("systimestamp",      new NoArgSQLFunction("systimestamp",        Hibernate.TIMESTAMP,false)  );
      registerFunction("user",              new NoArgSQLFunction("user",                Hibernate.STRING,   false)  );
      registerFunction("rownum",            new NoArgSQLFunction("rownum",              Hibernate.LONG,     false)  );
      registerFunction("concat",            new VarArgsSQLFunction(Hibernate.STRING, "", "||", ""));
    
      //registerFunction("locate",  new StandardSQLFunction("instr", Hibernate.INTEGER)); EJB-QL
      //registerFunction("sqrt",    new StandardSQLFunction("sqrt", Hibernate.DOUBLE)); EJB-QL
    
    }
    
    public String getAddColumnString()
    {
      return "add";
    }
    
    public String getSequenceNextValString(String sequenceName)
    {
      return "select " + sequenceName + ".next_value from table({1}) as T(X)";
    }
    
    public String getCreateSequenceString(String sequenceName)
    {
      return "create serial " + sequenceName;
    }
    
    public String getDropSequenceString(String sequenceName)
    {
      return "drop serial " + sequenceName;
    }
    
    public boolean supportsSequences()
    {
      return true;
    }
    
    public String getQuerySequencesString()
    {
      return "select name from db_serial";
    }
    
    public boolean dropConstraints()
    {
      return false;
    }
    
    public boolean supportsLimit() 
    {
    	return true;
    }
    
    public String getLimitString(String sql, boolean hasOffset) 
    {
    	sql = sql.trim();
    	StringBuffer pagingSelect = new StringBuffer( sql.length()+100 );
    	
    	pagingSelect.append("select * from ( ");
    	pagingSelect.append(sql);
    	
    	if (hasOffset) {
    		pagingSelect.append(" ) rownum_tbl where rownum >= ? and rownum <= ?");
    	}
    	else {
    		pagingSelect.append(" ) rownum_tbl where rownum <= ?");
    	}
    	return pagingSelect.toString();
    }
    
    public boolean bindLimitParametersInReverseOrder() 
    {
    	return true;
    }
    
    public boolean useMaxForLimit() 
    {
    	return true;
    }
    
    public boolean forUpdateOfColumns() 
    {
    	return true;
    }
    
    
    /*
    // Deprcated 
    public SQLExceptionConverter buildSQLExceptionConverter()
    {
      return new CUBRIDExceptionConverter(getViolatedConstraintNameExtracter());
    }
    
    private static class CUBRIDExceptionConverter extends ErrorCodeConverter
    {
      final static private int[] sqlGrammarCodes = { -493 };
      final static private int[] integrityViolationCodes = { -225, -670 };
      final static private int[] lockAcquisitionCodes = { -73, -74, -75, -76 };
    
      public CUBRIDExceptionConverter(ViolatedConstraintNameExtracter extracter) {
        super(extracter);
      }
    
      protected int[] getSQLGrammarErrorCodes() {
        return sqlGrammarCodes;
      }
    
      protected int[] getIntegrityViolationErrorCodes() {
        return integrityViolationCodes;
      }
    
      protected int[] getLockAcquisitionErrorCodes() {
        return lockAcquisitionCodes;
      }
    }
    */
    
    public char closeQuote() 
    {
    	return ']';
    }
    
    public char openQuote() 
    {
    	return '[';
    }
    
    public boolean hasAlterTable()
    {
      return false;
    }
    
    public String getForUpdateString()
    {
      return " ";
    }
    
    public boolean supportsUnionAll() 
    {
    	return true;
    }
    
    public boolean supportsCommentOn() 
    {
    	return false;
    }
    
    public boolean supportsTemporaryTables() 
    {
    	return false;
    }
    
    public boolean supportsCurrentTimestampSelection() 
    {
    	return true;
    }
    
    public String getCurrentTimestampSelectString() 
    {
    	return "select systimestamp from table({1}) as T(X)";
    }
    
    public boolean isCurrentTimestampSelectStringCallable() 
    {
    	return false;
    }
}
