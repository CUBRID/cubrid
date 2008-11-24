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

package cubrid.jdbc.jci;

/**
 * CAS와의 Protocol에서 사용되는 Function code를 define해놓은 class이다.
 * 
 * since 1.0
 */

abstract class UFunctionCode
{
  /* since 1.0 */
  final static byte END_TRANSACTION = 1;
  final static byte PREPARE = 2;
  final static byte EXECUTE = 3;
  final static byte GET_DB_PARAMETER = 4;
  final static byte SET_DB_PARAMETER = 5;
  final static byte CLOSE_USTATEMENT = 6;
  final static byte CURSOR = 7;
  final static byte FETCH = 8;
  final static byte GET_SCHEMA_INFO = 9;
  final static byte GET_BY_OID = 10;
  final static byte PUT_BY_OID = 11;
  final static byte GLO_NEW = 12;
  final static byte GLO_SAVE = 13;
  final static byte GLO_LOAD = 14;
  final static byte GET_DB_VERSION = 15;
  final static byte GET_CLASS_NUMBER_OBJECTS = 16;
  final static byte RELATED_TO_OID = 17;
  final static byte RELATED_TO_COLLECTION = 18;
  /* since 2.0 */
  final static byte NEXT_RESULT = 19;
  final static byte EXECUTE_BATCH_STATEMENT = 20;
  final static byte EXECUTE_BATCH_PREPAREDSTATEMENT = 21;
  final static byte CURSOR_UPDATE = 22;
  final static byte GET_QUERY_INFO = 24;

  /* since 3.0 */
  final static byte GLO_CMD = 25;
  final static byte SAVEPOINT = 26;
  final static byte PARAMETER_INFO = 27;
  final static byte XA_PREPARE = 28;
  final static byte XA_RECOVER = 29;
  final static byte XA_END_TRAN = 30;

  final static byte CON_CLOSE = 31;
  final static byte CHECK_CAS = 32;

  final static byte MAKE_OUT_RS = 33;

  final static byte GET_GENERATED_KEYS = 34;
}
