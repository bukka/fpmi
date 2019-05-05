--TEST--
FPMI: bug76895 - Child blocks reload after fatal error
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

require_once "tester.inc";

$cfg = <<<EOT
[global]
error_log = {{FILE:LOG}}
pid = {{FILE:PID}}
[unconfined]
listen = {{ADDR}}
pm = ondemand
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
$body = $tester->request()->getBody();
$expectedBody = "/^<br \\/>\n<b>Fatal error<\\/b>:  'break' not in the 'loop' or 'switch' context in <b>.*\.php<\\/b> on line <b>4<\\/b><br \\/>$/";
if (!preg_match($expectedBody, $body)) {
  echo 'ERROR: expected body ' . $expectedBody . "\n" . 'does not match actual body: ' . $body . "\n";
}
// Alternatively error may be suppressed in response and directed to log.
// $tester->expectLogWarning('child \d+ said into stderr: "NOTICE: PHP message: PHP Fatal error:  \'break\' not in the \'loop\' or \'switch\' context in .* on line 4"', 'unconfined');
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
