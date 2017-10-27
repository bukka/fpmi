<?php

namespace FPMI;

class LogTool
{
	const P_TIME = '\[\d\d-\w\w\w-\d{4} \d\d:\d\d:\d\d\]';
	const P_PREFIX = 'WARNING: \[pool unconfined\] child \d+ said into stderr: ';

	private $message;

	private $position;

	private $limit;

	private $pattern;

	public function __construct($message, $limit)
	{
		$this->message = $message;
		$this->limit = $limit;
		$this->position = 0;
		$this->pattern = sprintf('/^(%s %s)"([^"]*)"(.*)?$/', self::P_TIME, self::P_PREFIX);
	}

	public function check($lines, $terminated = true)
	{
		foreach ($lines as $idx => $line) {
			if (!$this->checkLine($line)) {
				break;
			}
		}

		if ($terminated) {
			$this->checkTerminatorLines($lines, $idx);
		}
	}

	private function checkLine($line)
	{
		if (preg_match($this->pattern, $line, $matches) === 0) {
			return $this->error("Unexpected line: $line");
		}

		$rem = strlen($this->message) - $this->position;
		$lineLen = strlen($line);
		$outLen = strlen($matches[2]);
		if ($rem > $outLen) { // continous line
			if ($lineLen !== $this->limit) {
				return $this->error("The continous line lenght is $lineLen but it should be equal to limit $this->limit");
			}
			$this->position += $outLen;
			return true;
		}
		if ($rem === $outLen)  {
			if (!isset($matches[3])) {
				return $this->error("No final suffix");
			}
			//TODO: allow wrapped final suffix
			if ($matches[3] !== ', pipe is closed') {
				return $this->error("The final suffix has to be equal to ', pipe is closed'");
			}
			return true;
		} else {
			return $this->error("Printed more than the message len");
		}
	}

	private function checkTerminatorLines($lines)
	{

	}

	private function error($msg)
	{
		echo "ERROR: $msg\n";
		return false;
	}
}

if (isset($argv[1]) && $argv[1] === 'logtool-selftest') {
	$lines = [
		//'[08-Oct-2017 19:53:14] NOTICE: fpmi is running, pid 23182',
		//'[08-Oct-2017 19:53:14] NOTICE: ready to handle connections',
		'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 968) . '"',
		'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 968) . '"',
		'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 112) . '", pipe is closed'
	];
	$logTool = new LogTool(str_repeat('a', 2048), 1050);
	$logTool->check($lines, false);
	echo "Done\n";
}
