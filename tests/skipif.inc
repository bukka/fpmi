<?php

if (substr(PHP_OS, 0, 3) == 'WIN') {
	die ("skip not for Windows");
}


if (!getmyuid() && !getenv('TEST_FPMI_RUN_AS_ROOT')) {
	die('Refusing to run as root');
}

include dirname(__FILE__)."/include.inc";

if (!get_fpmi_path()) {
	die("skip FPMI not found");
}
?>
