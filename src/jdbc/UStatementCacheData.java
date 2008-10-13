package cubrid.jdbc.jci;

public class UStatementCacheData {

int tuple_count;
UResultTuple[] tuples;
UResultInfo[] resultInfo;
long srvCacheTime;

UStatementCacheData(UStatementCacheData cache_data)
{
  if (cache_data == null) {
    this.tuple_count = 0;
    this.tuples = null;
    this.resultInfo = null;
    this.srvCacheTime = 0L;
  }
  else {
    this.tuple_count = cache_data.tuple_count;
    this.tuples = cache_data.tuples;
    this.resultInfo = cache_data.resultInfo;
    this.srvCacheTime = cache_data.srvCacheTime;
  }
}

void setCacheData(int tuple_count, UResultTuple[] tuples, UResultInfo[] resultInfo)
{
  this.tuple_count = tuple_count;
  this.tuples = tuples;
  this.resultInfo = resultInfo;
  if (resultInfo.length == 1)
    this.srvCacheTime = resultInfo[0].getSrvCacheTime();
  else
    this.srvCacheTime = 0L;
}

}
