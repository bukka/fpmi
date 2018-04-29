<?php
// Do not run on Windows
if (substr(PHP_OS, 0, 3) == 'WIN') {
    die("skip not for Windows");
}
// Running as root is not allowed without TEST_FPMI_RUN_AS_ROOT env
if (!getmyuid() && !getenv('TEST_FPMI_RUN_AS_ROOT')) {
    die('skip Refusing to run as root');
}

require_once "tester.inc";

if (!FPMI\Tester::findExecutable()) {
    die("skip php-fpmi binary not found");
}