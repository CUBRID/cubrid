/* This is a minimalist xmlrpc server.
 * This program reads an XML document from standard input and
 * says hello when the hello() method is called.
 */

#include <stdio.h>
#include <stdlib.h>
#include "xmlrpc.h"

#define VERBOSE 1

/* this function will be called by xmlrpc engine when registered method is found  */
/* typically, most developer time will be spent writing these types of functions. */
XMLRPC_VALUE hello_callback(XMLRPC_SERVER server, XMLRPC_REQUEST request, void* userData)
{
   char buf[1024];
#ifdef VERBOSE
   XMLRPC_VALUE xParams = XMLRPC_RequestGetData(request);   // obtain method params from request
   XMLRPC_VALUE xFirstParam = XMLRPC_VectorRewind(xParams); // obtain first parameter
   const char * name = XMLRPC_GetValueString(xFirstParam);  // get string value
#else
   const char* name = XMLRPC_GetValueString(XMLRPC_VectorRewind(XMLRPC_RequestGetData(request)));
#endif 

   snprintf(buf, sizeof(buf), "hello %s", name ? name : "stranger");
   return XMLRPC_CreateValueString(NULL, buf, 0);
}


/* with the exception of the registration calls, most everything in main
 * only needs to be written once per server.
 */
int main(int argc, char **argv)
{
  XMLRPC_SERVER  server;
  XMLRPC_REQUEST request=0;
  XMLRPC_REQUEST response;

  /* create a new server object */
  server = XMLRPC_ServerCreate();

  /* Register public methods with the server */
  XMLRPC_ServerRegisterMethod(server, "hello", hello_callback);

  /* Now, let's get the client's request from stdin.... */
  {
     char filebuf[4096];  // not that intelligent.  sue me.
     int len = fread(filebuf, sizeof(char), sizeof(filebuf)-1, stdin);

     if(len) {
        filebuf[len] = 0;

        // parse the xml into a request structure
        request = XMLRPC_REQUEST_FromXML((const char*)filebuf, len, NULL);
     }
  }

  if(!request) {
     fprintf(stderr, "bogus xmlrpc request\n");
     return 1;
  }


  /* create a response struct */
  response = XMLRPC_RequestNew();
  XMLRPC_RequestSetRequestType(response, xmlrpc_request_response);

  /* call server method with client request and assign the response to our response struct */
  XMLRPC_RequestSetData(response, XMLRPC_ServerCallMethod(server, request, NULL));

  /* be courteous. reply in same vocabulary/manner as the request. */
  XMLRPC_RequestSetOutputOptions(response, XMLRPC_RequestGetOutputOptions(request) );

  /* serialize server response as XML */
  if(1) {
     char *outBuf = XMLRPC_REQUEST_ToXML(response, 0);

     if(outBuf) {
        printf(outBuf);
        free(outBuf);
     }
  }

  // cleanup.  null safe.
  XMLRPC_RequestFree(request, 1);
  XMLRPC_RequestFree(response, 1);
  XMLRPC_ServerDestroy(server);

  return 0;
}

