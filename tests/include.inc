<?php

function get_fpmi_path() /* {{{ */
{
	$php_path = getenv("TEST_PHP_EXECUTABLE");
	for ($i = 0; $i < 2; $i++) {
		$slash_pos = strrpos($php_path, "/");
		if ($slash_pos) {
			$php_path = substr($php_path, 0, $slash_pos);
		} else {
			return false;
		}
	}


	if ($php_path && is_dir($php_path)) {
		if (file_exists($php_path."/fpmi/php-fpmi") && is_executable($php_path."/fpmi/php-fpmi")) {
			/* gotcha */
			return $php_path."/fpmi/php-fpmi";
		}
		$php_sbin_fpmi = $php_path."/sbin/php-fpmi";
		if (file_exists($php_sbin_fpmi) && is_executable($php_sbin_fpmi)) {
			return $php_sbin_fpmi;
		}
	}
	return false;
}
/* }}} */

function run_fpmi($config, &$out = false, $extra_args = '') /* {{{ */
{
    $cfg = dirname(__FILE__).'/test-fpmi-config.tmp';
    file_put_contents($cfg, $config);
    $desc = [];
    if ($out !== false) {
        $desc = [1 => array('pipe', 'w')];
    }
    /* Since it's not possible to spawn a process under linux without using a
     * shell in php (why?!?) we need a little shell trickery, so that we can
     * actually kill php-fpmi */
    $asroot = getenv('TEST_FPMI_RUN_AS_ROOT') ? '--allow-to-run-as-root' : '';
    $cmd = get_fpmi_path()." $asroot -F -O -y $cfg $extra_args";
    $fpmi = proc_open("killit () { kill \$child; }; trap killit TERM; $cmd 2>&1 & child=\$!; wait",
                     $desc, $pipes);
    register_shutdown_function(
            function($fpmi) use($cfg) {
                    @unlink($cfg);
                    if (is_resource($fpmi)) {
                        @proc_terminate($fpmi);
                        while (proc_get_status($fpmi)['running']) {
                            usleep(10000);
                        }
                    }
            },
                    $fpmi
            );
    if ($out !== false) {
        $out = $pipes[1];
    }
    return $fpmi;
}
/* }}} */

function test_fpmi_conf($config, &$msg = NULL) { /* {{{ */
	$cfg = dirname(__FILE__).'/test-fpmi-config.tmp';
	file_put_contents($cfg, $config);
	exec(get_fpmi_path() . ' -t -y ' . $cfg . ' 2>&1', $output, $code);	
	if ($code) {
		$msg = preg_replace("/\[.+?\]/", "", $output[0]);
		return false;
	}
	return true;
}
/* }}} */

function run_fpmi_till($needle, $config, $max = 10) /* {{{ */
{
    $i = 0;
    $fpmi = run_fpmi($config, $tail);
    if (is_resource($fpmi)) {
        while($i < $max) {
            $i++;
            $line = fgets($tail);
            if(preg_match($needle, $line) === 1) {
                break;
            }
        }
        if ($i >= $max) {
            $line = false;
        }
        proc_terminate($fpmi);
        stream_get_contents($tail);
        fclose($tail);
        proc_close($fpmi);
    }
    return $line;
}
/* }}} */

function fpmi_display_log($tail, $n=1, $ignore='systemd') { /* {{{ */
	/* Read $n lines or until EOF */
	while ($n>0 || ($n<0 && !feof($tail))) {
		$a = fgets($tail);
		if (empty($ignore) || !strpos($a, $ignore)) {
			echo $a;
			$n--;
		}
	}
} /* }}} */

function fpmi_get_log_lines($tail, $n=1, $skip_blank=false, $ignore='systemd') { /* {{{ */
	$lines = [];
	/* Read $n lines or until EOF */
	while ($n>0 || ($n<0 && !feof($tail))) {
		$a = fgets($tail);
		if ((empty($ignore) || !strpos($a, $ignore)) && (!$skip_blank || strlen(trim($a)) > 0)) {
			$lines[] = $a;
			$n--;
		}
	}
	return $lines;
} /* }}} */

function run_request($host, $port, $uri='/ping', $query='', $headers=array()) {  /* {{{ */
	require_once 'fcgi.inc';
	$client = new Adoy\FastCGI\Client($host, $port);
	$params = array_merge(array(
		'GATEWAY_INTERFACE' => 'FastCGI/1.0',
		'REQUEST_METHOD'    => 'GET',
		'SCRIPT_FILENAME'   => $uri,
		'SCRIPT_NAME'       => $uri,
		'QUERY_STRING'      => $query,
		'REQUEST_URI'       => $uri . ($query ? '?'.$query : ""),
		'DOCUMENT_URI'      => $uri,
		'SERVER_SOFTWARE'   => 'php/fcgiclient',
		'REMOTE_ADDR'       => '127.0.0.1',
		'REMOTE_PORT'       => '9985',
		'SERVER_ADDR'       => '127.0.0.1',
		'SERVER_PORT'       => '80',
		'SERVER_NAME'       => php_uname('n'),
		'SERVER_PROTOCOL'   => 'HTTP/1.1',
		'DOCUMENT_ROOT'     => __DIR__,
		'CONTENT_TYPE'      => '',
		'CONTENT_LENGTH'    => 0
	), $headers);
	return $client->request($params, false)."\n";
}
/* }}} */
