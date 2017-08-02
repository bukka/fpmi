--TEST--
FPMI: Test IPv6 support
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
listen = [::1]:$port
pm = dynamic
pm.max_children = 5
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 3
EOT;

$fpmi = run_fpmi($cfg, $tail);
if (is_resource($fpmi)) {
    fpmi_display_log($tail, 2);
    $i = 0;
    while (($i++ < 60) && !($fp = fsockopen('[::1]', $port))) {
        usleep(50000);
    }
    if ($fp) {
        echo "Done\n";
        fclose($fp);
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
Done
--CLEAN--
<?php
    $logfile = dirname(__FILE__).'/php-fpmi.log.tmp';
    @unlink($logfile);
?>
