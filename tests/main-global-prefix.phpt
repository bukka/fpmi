--TEST--
FPMI: Main invocation with prefix
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

require_once "tester.inc";

$cfg = <<<EOT
[global]
error_log = {{FILE:LOG:ERR}}
pid = {{FILE:PID}}
[unconfined]
listen = {{ADDR}}
access.log = {{FILE:LOG:ACC}}
slowlog = {{FILE:LOG:SLOW}};
request_slowlog_timeout = 1
ping.path = /ping
ping.response = pong
pm = dynamic
pm.max_children = 5
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 3
catch_workers_output = yes
EOT;

$tester = new FPMI\Tester($cfg, '', ['prefix' => __DIR__]);
$tester->start();
$tester->expectLogStartNotices();
$tester->expectFile(FPMI\Tester::FILE_EXT_LOG_ACC);
$tester->expectFile(FPMI\Tester::FILE_EXT_LOG_ERR);
$tester->expectFile(FPMI\Tester::FILE_EXT_LOG_SLOW);
$tester->expectFile(FPMI\Tester::FILE_EXT_PID);
$tester->ping();
$tester->terminate();
$tester->expectLogTerminatingNotices();
$tester->close();
$tester->expectNoFile(FPMI\Tester::FILE_EXT_PID);

?>
Done
--EXPECT--
Done
--CLEAN--
<?php
require_once "tester.inc";
FPMI\Tester::clean();
?>
