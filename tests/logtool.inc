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
		$this->pattern = sprintf('/^(%s %s)"([^"]*)"(, pipe is closed)?$/', self::P_TIME, self::P_PREFIX);
	}

	public function check($lines, $terminated = true)
	{
		foreach ($lines as $idx => $line) {
			if (!$this->checkLine($line) {
				break;
			}
		}

		if ($terminated) {
			$this->checkTerminatorLines($lines, $idx);
		}
	}

	private function checkLine($line)
	{
		if (preg_match($this->pattern,  $line, $matches) === false) {
			echo "ERROR: unexpected line: $line";
		}
	}

	private function checkLine($line)
	{

	}
}
