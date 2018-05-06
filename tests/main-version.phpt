--TEST--
FPMI: version string
--SKIPIF--
<?php include "skipif.inc"; ?>
--FILE--
<?php

require_once "tester.inc";

$php = \FPMI\Tester::findExecutable();

var_dump(`$php -n -v`);

echo "Done\n";
?>
--EXPECTF--
string(%d) "PHP %s (fpmi%s (built: %s
Copyright (c) 1997-20%s The PHP Group
Zend Engine v%s, Copyright (c) 1998-20%s Zend Technologies
"
Done
