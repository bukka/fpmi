--TEST--
FPMI: bug76895 - Child blocks reload after fatal error
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
pid = {{FILE:PID}}
[unconfined]
listen = {{ADDR}}
ping.path = /ping
ping.response = pong
pm = dynamic
pm.max_children = 5
pm.start_servers = 1
pm.min_spare_servers = 1
pm.max_spare_servers = 1
EOT;

$code = <<<EOT
<?php
\$variable = 'test';
if (!empty(\$variable)) {
  break;
}
EOT;

$tester = new FPMI\Tester($cfg, $code, ['opcache' => true]);
$tester->start();
$tester->expectLogStartNotices();
$expectedBody = "/^<br \\/>\n<b>Fatal error<\\/b>:  'break' not in the 'loop' or 'switch' context in/";
$body = $tester->request()->expectBodyRegExp($expectedBody);
$tester->signal('USR2');
$tester->expectLogNotice('Reloading in progress ...');
$tester->expectLogNotice('reloading: .*');
$tester->expectLogNotice('using inherited socket fd=\d+, "127.0.0.1:\d+"');
$tester->expectLogStartNotices();
$tester->ping('{{ADDR}}');
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
