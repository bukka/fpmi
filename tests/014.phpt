--TEST--
FPMI: Test for pm.start_servers default calculation message being a notice and not a warning #68458
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
log_level = warning
[unconfined]
listen = 127.0.0.1:$port
pm = dynamic
pm.max_children = 5
;pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 3
EOT;

$fpmi = run_fpmi($cfg, $tail);
if (is_resource($fpmi)) {
    $i = 0;
	while (($i++ < 60) && !($fp = @fsockopen('127.0.0.1', $port))) {
		usleep(50000);
	}
	if ($fp) {
		echo "Started\n";
		fclose($fp);
	}
	proc_terminate($fpmi);
	if (!feof($tail)) {
		echo stream_get_contents($tail);
	}
	fclose($tail);
	proc_close($fpmi);
}

?>
Done
--EXPECTF--
Started
Done
--CLEAN--
<?php
	$logfile = dirname(__FILE__).'/php-fpmi.log.tmp';
	@unlink($logfile);
?>
