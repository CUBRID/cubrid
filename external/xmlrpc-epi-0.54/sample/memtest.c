/*
  This file is part of libXMLRPC - a C library for xml-encoded function calls.

  Author: Dan Libby (dan@libby.com)
  Epinions.com may be contacted at feedback@epinions-inc.com
*/

/*  
  Copyright 2000 Epinions, Inc. 

  Subject to the following 3 conditions, Epinions, Inc.  permits you, free 
  of charge, to (a) use, copy, distribute, modify, perform and display this 
  software and associated documentation files (the "Software"), and (b) 
  permit others to whom the Software is furnished to do so as well.  

  1) The above copyright notice and this permission notice shall be included 
  without modification in all copies or substantial portions of the 
  Software.  

  2) THE SOFTWARE IS PROVIDED "AS IS", WITHOUT ANY WARRANTY OR CONDITION OF 
  ANY KIND, EXPRESS, IMPLIED OR STATUTORY, INCLUDING WITHOUT LIMITATION ANY 
  IMPLIED WARRANTIES OF ACCURACY, MERCHANTABILITY, FITNESS FOR A PARTICULAR 
  PURPOSE OR NONINFRINGEMENT.  

  3) IN NO EVENT SHALL EPINIONS, INC. BE LIABLE FOR ANY DIRECT, INDIRECT, 
  SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES OR LOST PROFITS ARISING OUT 
  OF OR IN CONNECTION WITH THE SOFTWARE (HOWEVER ARISING, INCLUDING 
  NEGLIGENCE), EVEN IF EPINIONS, INC.  IS AWARE OF THE POSSIBILITY OF SUCH 
  DAMAGES.    

*/


/* A memory (refcount) test program
 *
 * For usage, see below
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "xmlrpc.h"

/* These are some tests for checking out efficacy of ref-counting values. This program will
 * not have any ouput unless libxmlrpc is compiled with XMLRPC_DEBUG_REFCOUNT defined.
 */

#define TEST_DUPPED_VALUE 1

int main(int argc, char **argv)
{
#if 0
  XMLRPC_VALUE xVector1 = XMLRPC_CreateVector("vector_1", xmlrpc_vector_struct);
  XMLRPC_VALUE xVector2 = XMLRPC_CreateVector("vector_2", xmlrpc_vector_struct);
  XMLRPC_REQUEST xRequest = XMLRPC_RequestNew();
#endif

  /* shallow cycle. bad!  currently segfaults */
#ifdef TEST_CYCLE_DIRECT
  XMLRPC_VALUE xVector1 = XMLRPC_CreateVector("vector_1", xmlrpc_vector_struct);
  XMLRPC_AddValueToVector(xVector1, xVector1);
  XMLRPC_AddValueToVector(xVector1, xVector1);
  XMLRPC_CleanupValue(xVector1);
#endif

/* deep cycle.  bad!  currently segfaults. */
#ifdef TEST_CYCLE_INDIRECT
  XMLRPC_VALUE xVector1 = XMLRPC_CreateVector("vector_1", xmlrpc_vector_struct);
  XMLRPC_VALUE xVector2 = XMLRPC_CreateVector("vector_2", xmlrpc_vector_struct);
  XMLRPC_AddValueToVector(xVector1, xVector2);
  XMLRPC_AddValueToVector(xVector2, xVector1);
  XMLRPC_CleanupValue(xVector1);
#endif

#ifdef TEST_DUPPED_VALUE
  XMLRPC_VALUE xVector1 = XMLRPC_CreateVector("vector_1", xmlrpc_vector_struct), xVector2 = NULL;
  XMLRPC_VALUE xString = XMLRPC_CreateValueString("string", "a string", 0);
  XMLRPC_AddValueToVector(xVector1, xString);

  xVector2 = XMLRPC_DupValueNew(xVector1);
  XMLRPC_SetValueID(xVector2, "vector_1: dupped", 0);
  XMLRPC_CleanupValue(xVector1);
  XMLRPC_CleanupValue(xVector2);
#endif

  /* normal usage */
#ifdef TEST_MULTI_REFERENCED_STRING
  XMLRPC_REQUEST xRequest = XMLRPC_RequestNew();
  XMLRPC_VALUE xVector1 = XMLRPC_CreateVector("vector_1", xmlrpc_vector_struct);
  XMLRPC_VALUE xVector2 = XMLRPC_CreateVector("vector_2", xmlrpc_vector_struct);
  XMLRPC_VALUE xString = XMLRPC_CreateValueString("string", "a string", 0);
  XMLRPC_VALUE xString2 = XMLRPC_CopyValue(xString);

  /* Add various data to the request. */
  XMLRPC_AddValueToVector(xVector1, xString);
  XMLRPC_AddValueToVector(xVector2, xString);
  XMLRPC_RequestSetData(xRequest, xVector1);
  XMLRPC_RequestFree(xRequest, 0);
  XMLRPC_CleanupValue(xVector1);
  XMLRPC_CleanupValue(xVector2);
  XMLRPC_CleanupValue(xString2);
#endif  

#ifdef TEST_INTROSPECTION_LOOP
  int i;
  XMLRPC_VALUE xResponse;

  for(i = 0; i < 10; i++) {
     XMLRPC_SERVER server = XMLRPC_ServerCreate();
     XMLRPC_REQUEST request = XMLRPC_RequestNew();
     XMLRPC_RequestSetMethodName(request, "system.describeMethods");
     XMLRPC_RequestSetRequestType(request, xmlrpc_request_call);
     xResponse = XMLRPC_ServerCallMethod(server, request, 0);
     XMLRPC_CleanupValue(xResponse);
     XMLRPC_RequestFree(request, 1);
     XMLRPC_ServerDestroy(server);
     printf(".");fflush(stdout);
  }
  printf("\n");
#endif

#ifdef XML_TEST

static const char* xsm_introspection_xml =
"<?xml version='1.0' ?>"

"<introspection version='1.0'>"
 "<typeList>"

 "<typeDescription name='system.value' basetype='struct' desc='description of a value'>"
   "<value type='string' name='name' optional='yes'>value identifier</value>"
   "<value type='string' name='type'>value&apos;s xmlrpc or user-defined type</value>"
   "<value type='string' name='description'>value&apos;s textual description</value> "
   "<value type='boolean' name='optional'>true if value is optional, else it is required</value> "
   "<value type='any' name='member' optional='yes'>a child of this element. n/a for scalar types</value> "
 "</typeDescription>"

 "<typeDescription name='system.valueList' basetype='array' desc='list of value descriptions'>"
   "<value type='system.value'/>"
 "</typeDescription>"

 "</typeList>"

 "<methodList>"

 "<!-- system.describeMethods -->"
 "<methodDescription name='system.describeMethods'>"
  "<author>Dan Libby</author>"
  "<purpose>fully describes the methods and types implemented by this XML-RPC server.</purpose>"
  "<version>1.1</version>"
  "<signatures>"
   "<signature>"
    "<params>"
     "<value type='array' name='methodList' optional='yes' desc='a list of methods to be described. if omitted, all are described.'>"
      "<value type='string'>a valid method name</value>"
     "</value>"
    "</params>"
    "<returns>"
     "<value type='struct' desc='contains methods list and types list'>"
      "<value type='array' name='methodList' desc='a list of methods'>"
       "<value type='struct' desc='representation of a single method'>"
        "<value type='string' name='name'>method name</value>"
        "<value type='string' name='version'>method version</value>"
        "<value type='string' name='author'>method author</value>"
        "<value type='string' name='purpose'>method purpose</value>"
        "<value type='array' name='signatures' desc='list of method signatures'>"
         "<value type='struct' desc='representation of a single signature'>"
          "<value type='system.valueList' name='params'>parameter list</value>"
          "<value type='system.valueList' name='returns'>return value list</value>"
         "</value>"
        "</value>"
       "</value>"
      "</value>"
      "<value type='array' name='typeList' desc='a list of types'>"
       "<value type='system.value'>a type description</value>"
      "</value>"
     "</value>"
    "</returns>"
   "</signature>"
  "</signatures>"
  "<see>"
   "<item name='system.listMethods' />"
   "<item name='system.methodSignature' />"
   "<item name='system.methodHelp' />"
  "</see>"
  "<example/>"
  "<error/>"
  "<note/>"
  "<bug/>"
  "<todo/>"
 "</methodDescription>"

 "<!-- system.listMethods -->"
 "<methodDescription name='system.listMethods'>"
  "<author>Dan Libby</author>"
  "<purpose>enumerates the methods implemented by this XML-RPC server.</purpose>"
  "<version>1.0</version>"
  "<signatures>"
   "<signature>"
    "<returns>"
     "<value type='array' desc='an array of strings'>"
      "<value type='string'>name of a method implemented by the server.</value>"
     "</value>"
    "</returns>"
   "</signature>"
  "</signatures>"
  "<see>"
   "<item name='system.describeMethods' />"
   "<item name='system.methodSignature' />"
   "<item name='system.methodHelp' />"
  "</see>"
  "<example/>"
  "<error/>"
  "<note/>"
  "<bug/>"
  "<todo/>"
 "</methodDescription>"

 "<!-- system.methodHelp -->"
 "<methodDescription name='system.methodHelp'>"
  "<author>Dan Libby</author>"
  "<purpose>provides documentation string for a single method</purpose>"
  "<version>1.0</version>"
  "<signatures>"
   "<signature>"
    "<params>"
     "<value type='string' name='methodName'>name of the method for which documentation is desired</value>"
    "</params>"
    "<returns>"
     "<value type='string'>help text if defined for the method passed, otherwise an empty string</value>"
    "</returns>"
   "</signature>"
  "</signatures>"
  "<see>"
   "<item name='system.listMethods' />"
   "<item name='system.methodSignature' />"
   "<item name='system.methodHelp' />"
  "</see>"
  "<example/>"
  "<error/>"
  "<note/>"
  "<bug/>"
  "<todo/>"
 "</methodDescription>"

 "<!-- system.methodSignature -->"
 "<methodDescription name='system.methodSignature'>"
  "<author>Dan Libby</author>"
  "<purpose>provides 1 or more signatures for a single method</purpose>"
  "<version>1.0</version>"
  "<signatures>"
   "<signature>"
    "<params>"
     "<value type='string' name='methodName'>name of the method for which documentation is desired</value>"
    "</params>"
    "<returns>"
     "<value type='array' desc='a list of arrays, each representing a signature'>"
      "<value type='array' desc='a list of strings. the first element represents the method return value. subsequent elements represent parameters.'>"
       "<value type='string'>a string indicating the xmlrpc type of a value. one of: string, int, double, base64, datetime, array, struct</value>"
      "</value>"
     "</value>"
    "</returns>"
   "</signature>"
  "</signatures>"
  "<see>"
   "<item name='system.listMethods' />"
   "<item name='system.methodHelp' />"
   "<item name='system.describeMethods' />"
  "</see>"
  "<example/>"
  "<error/>"
  "<note/>"
  "<bug/>"
  "<todo/>"
 "</methodDescription>"

 "<!-- system.multiCall -->"
 "<methodDescription name='system.multiCall'>"
  "<author>Dan Libby</author>"
  "<purpose>executes multiple methods in sequence and returns the results</purpose>"
  "<version>1.0</version>"
  "<signatures>"
   "<signature>"
    "<params>"
     "<value type='array' name='methodList' desc='an array of method call structs'>"
      "<value type='struct' desc='a struct representing a single method call'>"
       "<value type='string' name='methodName' desc='name of the method to be executed'/>"
       "<value type='array' name='params' desc='an array representing the params to a method. sub-elements should match method signature'/>"
      "</value>"
     "</value>"
    "</params>"
    "<returns>"
     "<value type='array' desc='an array of method responses'>"
      "<value type='array' desc='an array containing a single value, which is the method&apos;s response'/>"
     "</value>"
    "</returns>"
   "</signature>"
  "</signatures>"
  "<see>"
   "<item name='system.listMethods' />"
   "<item name='system.methodHelp' />"
   "<item name='system.describeMethods' />"
  "</see>"
  "<example/>"
  "<error/>"
  "<note/>"
  "<bug/>"
  "<todo/>"
 "</methodDescription>"

 "<!-- system.interopFaultsVersion -->"
 "<methodDescription name='system.interopFaultsVersion'>"
  "<author>Dan Libby</author>"
  "<purpose>returns the version of fault code interop spec that this server is implemented according to. See http://xmlrpc-epi.sourceforge.net/specs/</purpose>"
  "<version>1.0</version>"
  "<signatures>"
   "<signature>"
    "<params>"
     "<value type='array' name='methodList' desc='an array of method call structs'>"
      "<value type='struct' desc='a struct representing a single method call'>"
       "<value type='string' name='methodName' desc='name of the method to be executed'/>"
       "<value type='array' name='params' desc='an array representing the params to a method. sub-elements should match method signature'/>"
      "</value>"
     "</value>"
    "</params>"
    "<returns>"
     "<value type='string'>version string.  this matches the version string found in the spec.</value>"
    "</returns>"
   "</signature>"
  "</signatures>"
  "<see>"
   "<item name='system.listMethods' />"
   "<item name='system.methodHelp' />"
   "<item name='system.describeMethods' />"
  "</see>"
  "<example/>"
  "<error/>"
  "<note/>"
  "<bug/>"
  "<todo/>"
 "</methodDescription>"

 "</methodList>"
"</introspection>";


  XMLRPC_VALUE xDesc = XMLRPC_IntrospectionCreateDescription(xsm_introspection_xml, NULL);
  XMLRPC_CleanupValue(xDesc);


#endif

  return 0;
}

