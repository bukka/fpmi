--TEST--
FPMI: Buffered worker output plain log with msg with flush split in buffer
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

require_once "tester.inc";

$cfg = <<<EOT
[global]
error_log = {{FILE:LOG}}
[unconfined]
listen = {{ADDR}}
pm = dynamic
pm.max_children = 5
pm.start_servers = 1
pm.min_spare_servers = 1
pm.max_spare_servers = 3
catch_workers_output = yes
decorate_workers_output = no
EOT;

$code = <<<EOT
<?php
file_put_contents('php://stderr', str_repeat('a', 1021) . "\0fabc");
EOT;

$tester = new FPMI\Tester($cfg, $code);
$tester->start();
$tester->expectLogStartNotices();
$tester->request()->expectEmptyBody();
$tester->terminate();
$lines = $tester->getLogLines(2);
var_dump($lines[0] === str_repeat('a', 1021)  . "\0f\n");
var_dump($lines[1] === "abc\n");
$tester->close();

?>
Done
--EXPECT--
bool(true)
bool(true)
Done
--CLEAN--
<?php
require_once "tester.inc";
FPMI\Tester::clean();
?>
