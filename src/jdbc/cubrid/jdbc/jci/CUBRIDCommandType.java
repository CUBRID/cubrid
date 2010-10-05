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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package @CUBRID_JCI@;

/**
 * CUBRID의 Command Type을 정의하는 class이다.
 * 
 * since 1.0
 */

abstract public class CUBRIDCommandType
{
  public final static byte CUBRID_STMT_ALTER_CLASS = 0;
  public final static byte CUBRID_STMT_ALTER_SERIAL = 1;
  public final static byte CUBRID_STMT_COMMIT_WORK = 2;
  public final static byte CUBRID_STMT_REGISTER_DATABASE = 3;
  public final static byte CUBRID_STMT_CREATE_CLASS = 4;
  public final static byte CUBRID_STMT_CREATE_INDEX = 5;
  public final static byte CUBRID_STMT_CREATE_TRIGGER = 6;
  public final static byte CUBRID_STMT_CREATE_SERIAL = 7;
  public final static byte CUBRID_STMT_DROP_DATABASE = 8;
  public final static byte CUBRID_STMT_DROP_CLASS = 9;
  public final static byte CUBRID_STMT_DROP_INDEX = 10;
  public final static byte CUBRID_STMT_DROP_LABEL = 11;
  public final static byte CUBRID_STMT_DROP_TRIGGER = 12;
  public final static byte CUBRID_STMT_DROP_SERIAL = 13;
  public final static byte CUBRID_STMT_EVALUATE = 14;
  public final static byte CUBRID_STMT_RENAME_CLASS = 15;
  public final static byte CUBRID_STMT_ROLLBACK_WORK = 16;
  public final static byte CUBRID_STMT_GRANT = 17;
  public final static byte CUBRID_STMT_REVOKE = 18;
  public final static byte CUBRID_STMT_STATISTICS = 19;
  public final static byte CUBRID_STMT_INSERT = 20;
  public final static byte CUBRID_STMT_SELECT = 21;
  public final static byte CUBRID_STMT_UPDATE = 22;
  public final static byte CUBRID_STMT_DELETE = 23;
  public final static byte CUBRID_STMT_CALL = 24;
  public final static byte CUBRID_STMT_GET_ISO_LVL = 25;
  public final static byte CUBRID_STMT_GET_TIMEOUT = 26;
  public final static byte CUBRID_STMT_GET_OPT_LVL = 27;
  public final static byte CUBRID_STMT_SET_OPT_LVL = 28;
  public final static byte CUBRID_STMT_SCOPE = 29;
  public final static byte CUBRID_STMT_GET_TRIGGER = 30;
  public final static byte CUBRID_STMT_SET_TRIGGER = 31;
  public final static byte CUBRID_STMT_SAVEPOINT = 32;
  public final static byte CUBRID_STMT_PREPARE = 33;
  public final static byte CUBRID_STMT_ATTACH = 34;
  public final static byte CUBRID_STMT_USE = 35;
  public final static byte CUBRID_STMT_REMOVE_TRIGGER = 36;
  public final static byte CUBRID_STMT_RENAME_TRIGGER = 37;
  public final static byte CUBRID_STMT_ON_LDB = 38;
  public final static byte CUBRID_STMT_GET_LDB = 39;
  public final static byte CUBRID_STMT_SET_LDB = 40;
  public final static byte CUBRID_STMT_GET_STATS = 41;

  public final static byte CUBRID_STMT_CALL_SP = 0x7e;
  public final static byte CUBRID_STMT_UNKNOWN = 0x7f;
}
