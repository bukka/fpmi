--TEST--
FPMI: Test IPv4 allowed clients
--SKIPIF--
<?php include "skipif.inc";
      @stream_socket_client('tcp://[::1]:0', $errno);
      if ($errno != 111) die('skip IPv6 not supported.');
?>
--FILE--
<?php

include "include.inc";

$logfile = dirname(__FILE__).'/php-fpmi.log.tmp';
$port = 9000+PHP_INT_SIZE;

$cfg = <<<EOT
[global]
error_log = $logfile
[unconfined]
listen = [::]:$port
listen.allowed_clients = 127.0.0.1
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
		run_request('127.0.0.1', $port);
		echo "IPv4 ok\n";
	} catch (Exception $e) {
		echo "IPv4 error\n";
	}
    try {
		run_request('[::1]', $port);
		echo "IPv6 ok\n";
	} catch (Exception $e) {
		echo "IPv6 error\n";
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
IPv4 ok
IPv6 error
--CLEAN--
<?php
    $logfile = dirname(__FILE__).'/php-fpmi.log.tmp';
    @unlink($logfile);
?>
