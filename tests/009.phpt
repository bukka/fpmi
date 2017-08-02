--TEST--
FPMI: Test Unix Domain Socket
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

include "include.inc";

$logfile = dirname(__FILE__).'/php-fpmi.log.tmp';
$socket  = dirname(__FILE__).'/php-fpmi.sock';

$cfg = <<<EOT
[global]
error_log = $logfile
[unconfined]
listen = $socket
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
		var_dump(strpos(run_request('unix://'.$socket, -1), 'pong'));
		echo "UDS ok\n";
	} catch (Exception $e) {
		echo "UDS error\n";
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
UDS ok
--CLEAN--
<?php
    $logfile = dirname(__FILE__).'/php-fpmi.log.tmp';
    @unlink($logfile);
?>
