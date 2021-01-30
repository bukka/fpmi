--TEST--
FPMI: bug80024 - Duplication of info about inherited socket after pool removing
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

require_once "tester.inc";

$cfg['main'] = <<<EOT
[global]
error_log = {{FILE:LOG}}
pid = {{FILE:PID}}
include = {{INCLUDE:CONF}}
EOT;

$cfg['poolTemplate'] = <<<EOT
[pool_%index%]
listen = {{ADDR:UDS[pool_%index%]}}
pm = static
pm.max_children = 1
EOT;

$cfg['count'] = 129;

$tester = new FPMI\Tester($cfg);
$tester->start();
$tester->expectLogStartNotices();
//TODO: make sure all sockets initialized (ping all pools maybe) and add support for config modification
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