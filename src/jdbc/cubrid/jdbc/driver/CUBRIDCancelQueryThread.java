/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubrid.jdbc.driver;

import java.sql.*;


/**
 * Title:        CUBRID JDBC Driver
 * Description:
 * @version 2.0
 */


/**
 * 이 class는 CUBRIDStatement와 CUBRIDPreparedStatement의
 * query timeout기능을 구현하는데 사용된다.
 *
 * 생성시에 시간과 CUBRIDStatement object를 받고 저장한 후
 * run()이 호출되면 주어진 시간동안 sleep한 후에
 * 주어진 CUBRIDStatement object의 cancel()을 호출해준다.
 *
 * 주어진 CUBRIDStatement object의 query의 실행이 먼저 끝나게 되면
 * queryended()를 호출하여 알려주게 되어 있다.
 *
 * query의 실행이 server에서 실제로 끝났음에도 불구하고 아직
 * queryended()가 호출되지 않아서 query의 실행을 취소하도록
 * 시도할 때 문제가 발생될 여지가 있으므로,
 * queryended()함수를 호출하는 것과
 * 주어진 CUBRIDStatement object의 cancel()을 호출하는 것을
 * 동기화하여 둘중의 하나가 실행중일때에는 다른 하나가 block되도록 한다.
 */
class CUBRIDCancelQueryThread extends Thread {

/*
 * query를 취소할 때까지 기다릴 시간
 */
private int timeout;

/*
 * 취소할 query를 실행중인 CUBRIDStatement object
 */
private CUBRIDStatement stmt;

/*
 * query의 실행이 끝났는지를 나타내는 flag이다.
 * true이면 query의 실행이 끝났으므로 주어진 CUBRIDStatement object의
 * cancel()을 호출하지 않는다.
 * 이 값은 target CUBRIDStatement object가 query의 실행이 끝나면 true로 set한다.
 */
private boolean end = false;

/*
 * 생성시 시간과 CUBRIDStatement object가 주어진다.
 */
CUBRIDCancelQueryThread(CUBRIDStatement cancel_stmt, int time)
{
  stmt = cancel_stmt;
  timeout = time;
}

/*
 * 주어진 timeout초 동안 sleep한 후에
 * end값이 false이면
 * CUBRIDStatement의 cancel을 호출한다.
 */
public void run()
{
  try {
    Thread.sleep(timeout*1000);
    synchronized (this) {
      if ( end == false ) {
	stmt.cancel();
      }
    }
  }
  catch (Exception e) {
  }
}

/*
 * CUBRIDStatement object에 의해 실행되며
 * query의 실행이 끝났음을 알리기 위해
 * end값을 true로 set한다.
 */
synchronized void queryended()
{
  end = true;
  interrupt();
}

}
