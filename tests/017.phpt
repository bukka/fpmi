--TEST--
FPMI: Test fastcgi_finish_request function
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

include "include.inc";

$logfile = __DIR__.'/php-fpmi.log.tmp';
$srcfile = __DIR__.'/php-fpmi.tmp.php';
$port = 9000+PHP_INT_SIZE;

$cfg = <<<EOT
[global]
error_log = $logfile
[unconfined]
listen = 127.0.0.1:$port
pm = dynamic
pm.max_children = 5
pm.start_servers = 1
pm.min_spare_servers = 1
pm.max_spare_servers = 3
EOT;

$code = <<<EOT
<?php
echo "Test Start\n";
fastcgi_finish_request();
echo "Test End\n";
EOT;
file_put_contents($srcfile, $code);

$fpmi = run_fpmi($cfg, $tail);
if (is_resource($fpmi)) {
    fpmi_display_log($tail, 2);
    try {
		$req = run_request('127.0.0.1', $port, $srcfile);
		echo strstr($req, "Test Start");
		echo "Request ok\n";
	} catch (Exception $e) {
		echo "Request error\n";
	}
    proc_terminate($fpmi);
    fpmi_display_log($tail, -1);
    fclose($tail);
    proc_close($fpmi);
}

?>
Done
--EXPECTF--
[%s] NOTICE: fpmi is running, pid %d
[%s] NOTICE: ready to handle connections
Test Start

Request ok
[%s] NOTICE: Terminating ...
[%s] NOTICE: exiting, bye-bye!
Done
--CLEAN--
<?php
	$logfile = __DIR__.'/php-fpmi.log.tmp';
	$srcfile = __DIR__.'/php-fpmi.tmp.php';
    @unlink($logfile);
    @unlink($srcfile);
?>
