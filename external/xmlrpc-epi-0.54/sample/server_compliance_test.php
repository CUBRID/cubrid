<?php 
// Handles XML-RPC Post Requests, as defined at www.xmlrpc.org

$server_path = "/home/danda/dev/xmlrpc_sourceforge/xmlrpc/sample/server_compliance_test";
$data = $HTTP_RAW_POST_DATA;
$request_path = "/tmp/xmlrpc.tmp.file";

if(isset($data)) {

   $fh = fopen("$request_path", "w");
   if($fh) {
      fwrite($fh, $data);
      fclose($fh);

      echo `$server_path $options < $request_path`;

      unlink($request_path);
   }
}
else {
   echo "<h1>Bogus XML-RPC Request.  No Post Data!!  Go away.</h1>";
}
flush();
exit();
?>

