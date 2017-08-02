--TEST--
FPMI: Test IPv4 all addresses (bug #68420)
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

include "include.inc";

$logfile = dirname(__FILE__).'/php-fpmi.log.tmp';
$port = 9000+PHP_INT_SIZE;

$cfg = <<<EOT
[global]
error_log = $logfile
[unconfined]
listen = $port
ping.path = /ping
ping.response = pong
pm = dynamic
pm.max_children = 5
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 3
EOT;

$fpmi = run_fpmi($cfg, $tail);
if (is_resource($fpmi)) {
    fpmi_display_log($tail, 2);
    try {
		var_dump(strpos(run_request('127.0.0.1', $port), 'pong'));
		echo "IPv4 ok\n";
	} catch (Exception $e) {
		echo "IPv4 error\n";
	}

	proc_terminate($fpmi);
    stream_get_contents($tail);
    fclose($tail);
    proc_close($fpmi);
}

?>
--EXPECTF--
[%d-%s-%d %d:%d:%d] NOTICE: fpmi is running, pid %d
[%d-%s-%d %d:%d:%d] NOTICE: ready to handle connections
int(%d)
IPv4 ok
--CLEAN--
<?php
    $logfile = dirname(__FILE__).'/php-fpmi.log.tmp';
    @unlink($logfile);
?>
