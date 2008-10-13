/* This is a simple demonstration of an xmlrpc client. This program
 * constructs an xmlrpc request and sends it to stdout. The output
 * is suitable for input to ./hello_server. 
 */

#include <stdio.h>
#include <stdlib.h>
#include "xmlrpc.h"

int main(int argc, char **argv)
{
  /* args */
  XMLRPC_REQUEST request;
  XMLRPC_VALUE xParamList;
  STRUCT_XMLRPC_REQUEST_OUTPUT_OPTIONS output = {{0}};

  /* create a new request object */
  request = XMLRPC_RequestNew();

  /* Set the method name and tell it we are making a request */
  XMLRPC_RequestSetMethodName(request, "hello");
  XMLRPC_RequestSetRequestType(request, xmlrpc_request_call);

  /* tell it to write out xml-rpc (default). options are: 
   * xmlrpc_version_1_0        // xmlrpc 1.0
	* xmlrpc_version_simple		 // simpleRPC
	* xmlrpc_version_soap_1_1	 // soap 1.1
	*/
  output.version = xmlrpc_version_1_0;
  XMLRPC_RequestSetOutputOptions(request, &output);

  /* Create a parameter list vector */
  xParamList = XMLRPC_CreateVector(NULL, xmlrpc_vector_array);
  
  /* Add our name as first param to the parameter list. */
  XMLRPC_VectorAppendString(xParamList, NULL, "john galt", 0);

  /* add the parameter list to request */
  XMLRPC_RequestSetData(request, xParamList);

  {
     /* serialize client request as XML */
     char *outBuf = XMLRPC_REQUEST_ToXML(request, 0);

     if(outBuf) {
        printf(outBuf);
        free(outBuf);
     }
  }

  if(request) {
     /* Free request */
     XMLRPC_RequestFree(request, 1);
  }
  
  return 0;
}

