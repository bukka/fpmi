--TEST--
FPMI: Log limit 1050 with 2048 msg
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

require_once "include.inc";
require_once "logtool.inc";

$logfile = __DIR__.'/php-fpmi-025-log-limit-1050-direct.log';
$srcfile = __DIR__.'/php-fpmi-025-log-limit-1050-direct.php';
$port = 9000+PHP_INT_SIZE;

$cfg = <<<EOT
[global]
error_log = $logfile
log_limit = 1050
[unconfined]
listen = 127.0.0.1:$port
pm = dynamic
pm.max_children = 5
pm.start_servers = 1
pm.min_spare_servers = 1
pm.max_spare_servers = 3
catch_workers_output = yes
EOT;

$code = <<<EOT
<?php
file_put_contents('php://stderr', str_repeat('a', 2048) . "\n");
EOT;
file_put_contents($srcfile, $code);

$fpmi = run_fpmi($cfg, $tail);
if (is_resource($fpmi)) {
	fpmi_display_log($tail, 2);
	try {
		$req = run_request('127.0.0.1', $port, $srcfile);
		var_dump($req);
		echo "Request ok\n";
	} catch (Exception $e) {
		echo "Request error\n";
	}
	proc_terminate($fpmi);
	$lines = fpmi_get_log_lines($tail, -1, true);
	fclose($tail);
	proc_close($fpmi);
	$logtool = new FPMI\LogTool();
	$logtool->setExpectedChildMessage(str_repeat('a', 2048), 1050);
	$logtool->checkChildMessage($lines);
}

?>
Done
--EXPECTF--
[%s] NOTICE: fpmi is running, pid %d
[%s] NOTICE: ready to handle connections
string(72) "X-Powered-By: PHP/%a
Content-type: text/html; charset=%s


"
Request ok
Done
--CLEAN--
<?php
$logfile = __DIR__.'/php-fpmi-025-log-limit-1050-direct.log';
$srcfile = __DIR__.'/php-fpmi-025-log-limit-1050-direct.php';
@unlink($logfile);
@unlink($srcfile);
?>
