/*  
    This file is part of libXMLRPC - a C library for xml-encoded function calls
    Copyright (C) 2000  Dan Libby, Epinions.com, Inc.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
    The LGPL is also available online at http://www.gnu.org/copyleft/lgpl.html
    
    The author may be contacted at dan@libby.com.  
    Epinions.com may be contacted at feedback@epinions-inc.com
 */


/* This is a simple demonstration of how to use xmlrpc. This program
 * acts as both client and server.  It instanstiates an xmlrpc
 * server, registers methods, creates an xmlrpc request, and processes
 * the request.  It can optionally print out the request, the response,
 * or both.  There are also several options for output format and
 * which method to call. It also acts as a test-case for each of
 * the xmlrpc data types, as each is implemented in a method.
 *  
 * For usage, see below or execute ./sample --help
 *
 * See also ./client and ./server, which are stand-alone versions
 * of this program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xmlrpc.h"


/* This example shows how to use the introspection API */
void describe_TestStruct(XMLRPC_SERVER server) {
   STRUCT_XMLRPC_ERROR err = {0};
   XMLRPC_VALUE xDesc;

   const char* desc = 

"<?xml version='1.0' ?>\n"
"<introspection version='1.0'>"

 "<typeList>"

  "<!-- define an address type --> "
  "<typeDescription name='address_record' basetype='struct'>"
    "<value type='string' name='street'>street address</value>"
    "<value type='string' name='apt' optional='yes'>Apartment number</value>"
    "<value type='string' name='city'>city</value> "
    "<value type='string' name='state'>state or province</value> "
    "<value type='string' name='zip'>zip or postal code</value>"
    "<value type='string' name='country'>country</value> "
  "</typeDescription>"

  "<!-- define a person's contact info --> "
  "<typeDescription name='contact' basetype='struct'>"
   "<value type='string' name='firstName'>first name</value>"
   "<value type='string' name='lastName'>last name</value>"
   "<value type='string' name='email'  optional='yes'>email address</value>"
   "<value type='string' name='phone'  optional='yes'>phone number</value>"
   "<value type='contact' name='address'  optional='yes'>email address</value>"
  "</typeDescription>"
 "</typeList>"

 "<methodList>"

  "<methodDescription name='method_TestStruct'>"

   "<!-- single strings.  one per method Description. -->"
   "<author>Dan Libby</author>"
   "<purpose>a silly method to test that structs work</purpose>"

   "<!-- signatures. complex element.  multiple per method Description -->"
   "<signatures>"
   "<signature>"
      "<params>"
       "<value type='contact'>a struct representing a person's contact information</value>"
      "</params>"
      "<returns>"
       "<value type='contact'>contact info that was passed in, or John Doe's info</value>"
      "</returns>"
   "</signature>"
   "</signatures>"

   "<!--  single string with name attribute. multiple per method Description -->"
   "<see><item>system.listMethods</item></see>"
   "<example/>"
   "<error/>"
   "<note>"
     "<item>this is a lame example</item>"
     "<item>example of multiple notes</item>"
   "</note>"
   "<bug/>"
   "<todo/>"

  "</methodDescription>"

 "</methodList>"

"</introspection>";


   xDesc = XMLRPC_IntrospectionCreateDescription(desc, &err);
   if(xDesc) {
      XMLRPC_ServerAddIntrospectionData(server, xDesc);
      XMLRPC_ServerSetValidationLevel(server, validation_if_defined);
      XMLRPC_CleanupValue(xDesc); // server does not keep track of this.
   }
   else {
      if(err.xml_elem_error.parser_code) {
         printf("parse error, line: %li, column: %li, message: %s\n",
                err.xml_elem_error.line, err.xml_elem_error.column, err.xml_elem_error.parser_error);
      }
   }
}


XMLRPC_VALUE method_TestStruct(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData)
{
  XMLRPC_VALUE xe_struct = XMLRPC_VectorRewind(XMLRPC_RequestGetData(request));

  /* should do better type checking to make sure it is a contact, but we are lazy */
  if(XMLRPC_VectorGetValueWithID(xe_struct, "firstName")) {
     return xe_struct;
  }
  else {
     xe_struct = XMLRPC_CreateVector(NULL, xmlrpc_vector_struct);
     
     XMLRPC_VectorAppendString(xe_struct, "firstName", "John", 0);
     XMLRPC_VectorAppendString(xe_struct, "lastName", "Doe", 0);
     XMLRPC_VectorAppendString(xe_struct, "email", "john@doe.com", 0);
     XMLRPC_VectorAppendString(xe_struct, "phone", "555-555-5555", 0);
  }

  return xe_struct;
}

XMLRPC_VALUE method_TestArray(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData)
{
  const char *string;
  char *testing[3] = {
    "One",
    "Two",
    "Three and four",
  };
  int i;
  XMLRPC_VALUE output;
  XMLRPC_VALUE xIter;

  output = XMLRPC_CreateVector(NULL, xmlrpc_vector_array);

  xIter = XMLRPC_VectorRewind(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)));
  while(xIter) {
     string = XMLRPC_GetValueString(xIter);

     if(string) {
        XMLRPC_VectorAppendString(output, NULL, string, 0);
     }

     xIter = XMLRPC_VectorNext(XMLRPC_RequestGetData(request));
  }

  for(i = 0; i < 3; i++) {
     XMLRPC_VectorAppendString(output, NULL, testing[i], 0);
  }

  return output;
}

XMLRPC_VALUE method_TestBoolean(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   int iVal = 1;
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "boolean");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_boolean) {
      iVal = XMLRPC_GetValueBoolean(xVal);
   }

   return XMLRPC_CreateValueBoolean(NULL, iVal);
}


XMLRPC_VALUE method_TestInt(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   int iVal = 25;
   XMLRPC_VALUE xParams = XMLRPC_RequestGetData(request);
   XMLRPC_VALUE xArg1Struct = XMLRPC_VectorRewind(xParams);
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(xArg1Struct, "int");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_int) {
      iVal = XMLRPC_GetValueInt(xVal);
   }

   return XMLRPC_CreateValueInt(NULL, iVal);
}

XMLRPC_VALUE method_TestString(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   const char* pVal = "Hello World";
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "string");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_string) {
      pVal = XMLRPC_GetValueString(xVal);
   }

   return XMLRPC_CreateValueString(NULL, pVal, 0);
}

XMLRPC_VALUE method_TestDouble(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   double dVal = 25;
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "double");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_double) {
      dVal = XMLRPC_GetValueDouble(xVal);
   }

   return XMLRPC_CreateValueDouble(NULL, dVal);
}

XMLRPC_VALUE method_TestBase64(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   const char* pVal = "U29tZUJhc2U2NFN0cmluZw==";
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "base64");
   int buf_len = 0;

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_base64) {
      pVal = XMLRPC_GetValueBase64(xVal);
      buf_len = XMLRPC_GetValueStringLen(xVal);
   }

   return XMLRPC_CreateValueBase64(NULL, pVal, buf_len);
}

XMLRPC_VALUE method_TestDateTime(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData) {
   const char* pVal = "19980717T14:08:55";
   XMLRPC_VALUE xVal = XMLRPC_VectorGetValueWithID(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)), "datetime");

   if(xVal && XMLRPC_GetValueType(xVal) == xmlrpc_datetime) {
      pVal = XMLRPC_GetValueDateTime_ISO8601(xVal);
   }

   return XMLRPC_CreateValueDateTime_ISO8601(NULL, pVal);
}

XMLRPC_VALUE method_TestNormal(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData)
{
  XMLRPC_VALUE xOutput;

  xOutput = XMLRPC_CreateVector(NULL, xmlrpc_vector_array);

  XMLRPC_AddValuesToVector(xOutput,
                           method_TestStruct(server, request, userData),
                           method_TestArray(server, request, userData),
                           method_TestBoolean(server, request, userData),
                           method_TestInt(server, request, userData),
                           method_TestString(server, request, userData),
                           method_TestDouble(server, request, userData),
                           method_TestDateTime(server, request, userData),
                           method_TestBase64(server, request, userData),
                           NULL);

  return xOutput;
}

XMLRPC_VALUE method_TestFault(XMLRPC_SERVER server, XMLRPC_REQUEST input, void* userData)
{
    return XMLRPC_UtilityCreateFault(404, "Page Not Found");
}


void print_help() {
   printf("Usage: sample [OPTION VALUE]\n\n");
   printf("\t-help    this help message\n");
   printf("\t-encoding <encoding>     (any standard character encoding)\n");
   printf("\t-escaping <markup | cdata | non-ascii | non-xml | none> (may repeat)\n");
   printf("\t-method <methodname>\n");
   printf("\t-output <request | response | both>\n");
   printf("\t-verbosity <pretty | none | newlines>\n");
   printf("\t-version <xmlrpc | soap | simple>\n");
   printf("\n\tavailable methods:\n"
          "\t\tmethod_TestNormal\n" 
          "\t\tmethod_TestFault\n"
          "\t\tmethod_TestStruct\n" 
          "\t\tmethod_TestArray\n"
          "\t\tmethod_TestBoolean\n"
          "\t\tmethod_TestInt\n"
          "\t\tmethod_TestString\n"
          "\t\tmethod_TestDouble\n" 
          "\t\tmethod_TestBase64\n"
          "\t\tmethod_TestDateTime\n");
}


int main(int argc, char **argv)
{
  int i;
  XMLRPC_SERVER  server;
  XMLRPC_REQUEST request, response;
  XMLRPC_VALUE param1;
  STRUCT_XMLRPC_REQUEST_OUTPUT_OPTIONS call_options;

  /* args */
  int verbosity = 0;
  int version = 0;
  int output = 0;
  char *methodName = "method_TestNormal";
  char *encoding = 0;
  int escaping = xml_elem_no_escaping;

  /* for every argument (after the program name) */
  for(i=1; i<argc; i++) {
     char* arg = argv[i];

     if(*arg == '-') {
        char* key = arg + 1;
        char* val = argv[i+1];

        if(key && ( !strcmp(key, "help") || !strcmp(key, "-help"))) {
           print_help();
           return 0;
        }

        if(key && val) {
           if(!strcmp(key, "verbosity")) {
              if(!strcmp(val, "pretty")) {
                 verbosity = 0;
              }
              else if(!strcmp(val, "none")) {
                 verbosity = 1;
              }
              else if(!strcmp(val, "newlines")) {
                 verbosity = 2;
              }
           }
           else if(!strcmp(key, "version")) {
              if(!strcmp(val, "xmlrpc")) {
                 version = 0;
              }
              else if(!strcmp(val, "simple")) {
                 version = 1;
              }
              else if(!strcmp(val, "soap")) {
                 version = 2;
              }
           }
           else if(!strcmp(key, "escaping")) {
              if(!strcmp(val, "markup")) {
                 escaping |= xml_elem_markup_escaping ;
              }
              else if(!strcmp(val, "cdata")) {
                 escaping |= xml_elem_cdata_escaping;
              }
              else if(!strcmp(val, "non-ascii")) {
                 escaping |= xml_elem_non_ascii_escaping;
              }
              else if(!strcmp(val, "non-print")) {
                 escaping |= xml_elem_non_print_escaping;
              }
           }
           else if(!strcmp(key, "encoding")) {
              encoding = val;
           }
           else if(!strcmp(key, "output")) {
              if(!strcmp(val, "response")) {
                 output = 0;
              }
              else if(!strcmp(val, "request")) {
                 output = 1;
              }
              else if(!strcmp(val, "both")) {
                 output = 2;
              }
           }
           else if(!strcmp(key, "method")) {
              methodName = val;
           }

           i++;
        }
     }
  }

  /* create a new server object */
  server = XMLRPC_ServerCreate();

  /* Register some public methods with the server */
  describe_TestStruct(server);
  XMLRPC_ServerRegisterMethod(server, "method_TestStruct", method_TestStruct);
  XMLRPC_ServerRegisterMethod(server, "method_TestFault", method_TestFault);
  XMLRPC_ServerRegisterMethod(server, "method_TestNormal", method_TestNormal);
  XMLRPC_ServerRegisterMethod(server, "method_TestArray", method_TestArray);
  XMLRPC_ServerRegisterMethod(server, "method_TestBoolean", method_TestBoolean);
  XMLRPC_ServerRegisterMethod(server, "method_TestInt", method_TestInt);
  XMLRPC_ServerRegisterMethod(server, "method_TestString", method_TestString);
  XMLRPC_ServerRegisterMethod(server, "method_TestDouble", method_TestDouble);
  XMLRPC_ServerRegisterMethod(server, "method_TestBase64", method_TestBase64);
  XMLRPC_ServerRegisterMethod(server, "method_TestDateTime", method_TestDateTime);

  /* Now, let's do some client stuff.... */

  /* create a new request object */
  request = XMLRPC_RequestNew();

  /* create a struct arg */
  param1 = XMLRPC_CreateVector(NULL, xmlrpc_vector_struct);

  /* Set various xml output options.  Or we could just use defaults. */
  call_options.xml_elem_opts.verbosity = verbosity == 1 ? xml_elem_no_white_space : (verbosity == 2 ? xml_elem_newlines_only : xml_elem_pretty);
  call_options.xml_elem_opts.escaping = escaping;
  call_options.version = (version == 1) ? xmlrpc_version_simple : (version == 2) ? xmlrpc_version_soap_1_1 : xmlrpc_version_1_0;
  call_options.xml_elem_opts.encoding = encoding;
  XMLRPC_RequestSetOutputOptions(request, &call_options);

  /* Set the method name and tell it we are making a request */
  XMLRPC_RequestSetMethodName(request, methodName);
  XMLRPC_RequestSetRequestType(request, xmlrpc_request_call);

  /* Create a vector and assign it to the request object */
  XMLRPC_RequestSetData(request, XMLRPC_CreateVector(NULL, xmlrpc_vector_struct));
  XMLRPC_AddValueToVector(XMLRPC_RequestGetData(request), param1);

  /* Add various data to the request. */
  XMLRPC_VectorAppendString(param1, "string", "This Is A test", 0);
  XMLRPC_VectorAppendString(param1, "string", "This Is A test", 0);
  XMLRPC_VectorAppendInt(param1, "int", 234);
  XMLRPC_VectorAppendDouble(param1, "double", 234);
  XMLRPC_VectorAppendDateTime(param1, "datetime", 0);
  XMLRPC_VectorAppendBase64(param1, "base64", "Testing Base64", 0);

  /* Now we are acting as the server again... */

  /* create a respons struct */
  response = XMLRPC_RequestNew();
  XMLRPC_RequestSetRequestType(response, xmlrpc_request_response);
  XMLRPC_RequestSetOutputOptions(response, &call_options);

  /* call server method with client request and assign the response to our response struct */
  XMLRPC_RequestSetData(response, XMLRPC_ServerCallMethod(server, request, NULL));

  if(output == 1 || output == 2) {
     /* serialize client request as XML */
     char *outBuf = XMLRPC_REQUEST_ToXML(request, 0);

     if(outBuf) {
        printf(outBuf);
        free(outBuf);
     }
  }

  if(output == 0 || output == 2) {
     /* serialize server response as XML */
     char *outBuf = XMLRPC_REQUEST_ToXML(response, 0);

     if(outBuf) {
        printf(outBuf);
        free(outBuf);
     }
  }

  if(request) {
     /* Free request */
     XMLRPC_RequestFree(request, 1);
  }
  if(response) {
     /* free response */
     XMLRPC_RequestFree(response, 1);
  }
  if(server) {
     XMLRPC_ServerDestroy(server);
  }

  return 0;
}

