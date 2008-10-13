package cubrid.jdbc.driver;

import cubrid.jdbc.jci.UConKey;

public class CUBRIDConnectionKey {

/*=======================================================================
 |	PRIVATE VARIABLES
 =======================================================================*/

private UConKey	conKey;

/*=======================================================================
 |	CONSTRUCTOR
 =======================================================================*/

public CUBRIDConnectionKey(String s)
{
  conKey = new UConKey(s);
}

/*=======================================================================
 |	PUBLIC METHODS
 =======================================================================*/

public UConKey getKey()
{
  return conKey;
}

} // end of class CUBRIDConnectionKey
