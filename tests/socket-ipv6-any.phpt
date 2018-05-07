--TEST--
FPMI: Socket for IPv6 connection
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

require_once "tester.inc";

$cfg = <<<EOT
[global]
error_log = {{FILE:LOG}}
[unconfined]
listen = {{ADDR:IPv6:ANY}}
pm = dynamic
pm.max_children = 5
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 3
EOT;

$tester = new FPMI\Tester($cfg);
$tester->start();
$tester->expectLogStartNotices();
$tester->checkConnection('127.0.0.1', 'IPv4: ok');
$tester->checkConnection('[::1]', 'IPv6: ok');
$tester->terminate();
$tester->expectLogTerminatingNotices();
$tester->close();

?>
Done
--EXPECT--
IPv4: ok
IPv6: ok
Done
--CLEAN--
<?php
require_once "tester.inc";
FPMI\Tester::clean();
?>
