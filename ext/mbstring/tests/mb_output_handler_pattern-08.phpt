--TEST--
mb_output_handler() and mbstring.http_output_conv_mimetypes (8)
--SKIPIF--
<?php extension_loaded('mbstring') or die('skip mbstring not available'); ?>
--INI--
mbstring.http_output_conv_mimetypes=html
--FILE--
<?php
mb_http_output("EUC-JP");
header("Content-Type: text/plain");
ob_start();
ob_start('mb_output_handler');
echo "ใในใ";
ob_end_flush();
var_dump(bin2hex(ob_get_clean()));
?>
--EXPECT--
string(18) "e38386e382b9e38388"
