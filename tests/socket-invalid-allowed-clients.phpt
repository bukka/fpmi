--TEST--
FPMI: Socket for invalid allowed client only
--SKIPIF--
<?php
include "skipif.inc";
?>
--FILE--
<?php

require_once "tester.inc";

$cfg = <<<EOT
[global]
error_log = {{FILE:LOG}}
[unconfined]
listen = {{ADDR}}
listen.allowed_clients = xxx
pm = dynamic
pm.max_children = 5
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 3
catch_workers_output = yes
EOT;

$tester = new FPMI\Tester($cfg);
$tester->start();
$tester->expectLogStartNotices();
$tester->checkRequest('127.0.0.1', 'Req: ok', 'Req: error');
$tester->terminate();
$tester->expectLogLine("ERROR: Wrong IP address 'xxx' in listen.allowed_clients");
$tester->expectLogLine("ERROR: There are no allowed addresses");
$tester->expectLogLine("ERROR: Connection disallowed: IP address '127.0.0.1' has been dropped.");
$tester->expectLogTerminatingNotices();
$tester->close();

?>
Done
--EXPECT--
Req: error
Done
--CLEAN--
<?php
require_once "tester.inc";
FPMI\Tester::clean();
?>
