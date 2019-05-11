--TEST--
FPMI: bug77934 - php-fpm kill -USR2 not working
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

require_once "tester.inc";

$cfg = <<<EOT
[global]
error_log = {{FILE:LOG}}
pid = {{FILE:PID}}
process_control_timeout=20
[unconfined]
listen = {{ADDR}}
pm = dynamic
pm.max_children = 5
pm.start_servers = 1
pm.min_spare_servers = 1
pm.max_spare_servers = 1
EOT;

$tester = new FPMI\Tester($cfg);
$tester->start();
$tester->expectLogStartNotices();
$tester->signal('USR2');
$tester->expectLogNotice('Reloading in progress ...');
$tester->expectLogNotice('reloading: .*');
$tester->expectLogNotice('using inherited socket fd=\d+, "127.0.0.1:\d+"');
$tester->expectLogStartNotices();
$tester->terminate();
$tester->expectLogTerminatingNotices();
$tester->close();

?>
Done
--EXPECT--
Done
--CLEAN--
<?php
require_once "tester.inc";
FPMI\Tester::clean();
?>
