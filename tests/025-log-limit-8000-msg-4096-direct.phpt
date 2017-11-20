--TEST--
FPMI: Log limit 8000 with 4096 msg
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

require_once "tester.inc";
require_once "logtool.inc";

$cfg = <<<EOT
[global]
error_log = {{FILE:LOG}}
log_limit = 8000
[unconfined]
listen = {{ADDR}}
pm = dynamic
pm.max_children = 5
pm.start_servers = 1
pm.min_spare_servers = 1
pm.max_spare_servers = 3
catch_workers_output = yes
EOT;

$code = <<<EOT
<?php
file_put_contents('php://stderr', str_repeat('a', 4096) . "\n");
EOT;

$tester = new FPMI\Tester($cfg, $code);
$tester->start();
$tester->expectLogStartNotices();
var_dump($tester->request());
$tester->terminate();
$tester->expectLogChildMessage('a', 8000, 4096);
$tester->close();

?>
Done
--EXPECTF--
string(72) "X-Powered-By: PHP/%a
Content-type: text/html; charset=%s


"
Done
--CLEAN--
<?php
require_once "tester.inc";
FPMI\Tester::clean();
?>
