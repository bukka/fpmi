--TEST--
FPMI: Test global prefix
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

include "include.inc";

$logfile = 'php-fpmi.log.tmp';
$accfile = 'php-fpmi.acc.tmp';
$slwfile = 'php-fpmi.slw.tmp';
$pidfile = 'php-fpmi.pid.tmp';
$port = 9000+PHP_INT_SIZE;

$cfg = <<<EOT
[global]
error_log = $logfile
pid = $pidfile
[test]
listen = 127.0.0.1:$port
access.log = $accfile
slowlog = $slwfile;
request_slowlog_timeout = 1
ping.path = /ping
ping.response = pong
pm = dynamic
pm.max_children = 5
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 3
EOT;

$fpmi = run_fpmi($cfg, $tail, '--prefix '.__DIR__);
if (is_resource($fpmi)) {
    fpmi_display_log($tail, 2);
    try {
		run_request('127.0.0.1', $port);
		echo "Ping ok\n";
	} catch (Exception $e) {
		echo "Ping error\n";
	}
	printf("File %s %s\n", $logfile, (file_exists(__DIR__.'/'.$logfile) ? "exists" : "missing"));
	printf("File %s %s\n", $accfile, (file_exists(__DIR__.'/'.$accfile) ? "exists" : "missing"));
	printf("File %s %s\n", $slwfile, (file_exists(__DIR__.'/'.$slwfile) ? "exists" : "missing"));
	printf("File %s %s\n", $pidfile, (file_exists(__DIR__.'/'.$pidfile) ? "exists" : "missing"));

	proc_terminate($fpmi);
    fpmi_display_log($tail, -1);
    fclose($tail);
    proc_close($fpmi);
	printf("File %s %s\n", $pidfile, (file_exists(__DIR__.'/'.$pidfile) ? "still exists" : "removed"));
	readfile(__DIR__.'/'.$accfile);
}

?>
--EXPECTF--
[%s] NOTICE: fpmi is running, pid %d
[%s] NOTICE: ready to handle connections
Ping ok
File php-fpmi.log.tmp exists
File php-fpmi.acc.tmp exists
File php-fpmi.slw.tmp exists
File php-fpmi.pid.tmp exists
[%s] NOTICE: Terminating ...
[%s] NOTICE: exiting, bye-bye!
File php-fpmi.pid.tmp removed
127.0.0.1 -  %s "GET /ping" 200
--CLEAN--
<?php
	$logfile = __DIR__.'/php-fpmi.log.tmp';
	$accfile = __DIR__.'/php-fpmi.acc.tmp';
	$slwfile = __DIR__.'/php-fpmi.slw.tmp';
	$pidfile = __DIR__.'/php-fpmi.pid.tmp';
    @unlink($logfile);
    @unlink($accfile);
    @unlink($slwfile);
    @unlink($pidfile);
?>
