<?php

namespace FPMI;

class LogTool
{
	const P_TIME = '\[\d\d-\w\w\w-\d{4} \d\d:\d\d:\d\d\]';
	const P_PREFIX = 'WARNING: \[pool unconfined\] child \d+ said into stderr: ';
	const FINAL_SUFFIX = ', pipe is closed';

	private $message;

	private $position;

	private $suffixPosition;

	private $limit;

	private $pattern;

	private $error;

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

		if ($this->suffixPosition > 0) {
			$suffixPattern = sprintf('/^%s %s(.*)$/', self::P_TIME, self::P_PREFIX);
			$line = $lines[++$idx];
			if (preg_match($suffixPattern, $line, $matches) === 0) {
				return $this->error("Unexpected line: $line");
			}
			if ($matches[1] !== substr(self::FINAL_SUFFIX, $this->suffixPosition)) {
				return $this->error("The suffix has not been finished from position $this->suffixPosition in line: $line");
			}
		}

		if ($terminated) {
			$this->checkTerminatorLines($lines, $idx);
		}

		return true;
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
				if ($lineLen + ($rem - $outLen) < $this->limit) {
					return $this->error("Printed less than the message len");
				}
				return $this->error(
					"The continous line lenght is $lineLen but it should be equal to limit $this->limit"
				);
			}
			$this->position += $outLen;
			return true;
		}
		if ($rem !== $outLen)  {
			return $this->error("Printed more than the message len");
		}
		if (!isset($matches[3])) {
			return $this->error("No final suffix");
		}
		if (strpos(self::FINAL_SUFFIX, $matches[3]) === false) {
			return $this->error("The final suffix has to be equal to ', pipe is closed'");
		}
		if (self::FINAL_SUFFIX !== $matches[3]) {
			$this->suffixPosition = strlen($matches[3]);
		}
		// complete final suffix printed
		return false;
	}

	private function checkTerminatorLines($lines, $idx)
	{
		if (count($lines) - $idx < 3) {
			return $this->error("No terminating lines");
		}

		$pattern = sprintf('/^%s NOTICE: (.*)$/', self::P_TIME);

		return (
			$this->checkTerminatingLine($lines[++$idx], $pattern, 'Terminating ...') &&
			$this->checkTerminatingLine($lines[++$idx], $pattern, 'exiting, bye-bye!')
		);
	}

	private function checkTerminatingLine($line, $pattern, $expectedMessage)
	{
		if (preg_match($pattern, $line, $matches) === 0) {
			return $this->error("Unexpected terminating line: $line");
		}
		if (!isset($matches[1]) || $matches[1] !== $expectedMessage) {
			return $this->error("The terminating message '${matches[1]}' does not match expected message '$expectedMessage'");
		}

		return true;
	}

	private function error($msg)
	{
		$this->error = $msg;
		echo "ERROR: $msg\n";
		return false;
	}

	public function getError()
	{
		return $this->error;
	}
}

if (isset($argv[1]) && $argv[1] === 'logtool-selftest') {
	$cases = [
		[
			'limit' => 1050,
			'lines' => [
				'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 968) . '"',
				'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 968) . '"',
				'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 112) . '", pipe is closed',
				'[08-Oct-2017 19:53:55] NOTICE: Terminating ...',
				'[08-Oct-2017 19:53:55] NOTICE: exiting, bye-bye!',
			],
			'message' => str_repeat('a', 2048),
		],
		[
			'limit' => 1050,
			'lines' => [
				'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 968) . '"',
				'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 968) . '"',
				'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 964) . '", pip',
				'[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: e is closed',
				'[08-Oct-2017 19:53:55] NOTICE: Terminating ...',
				'[08-Oct-2017 19:53:55] NOTICE: exiting, bye-bye!',
			],
			'message' => str_repeat('a', 2900),
		]
	];
	foreach ($cases as $case) {
		printf("Test message with len %d and limit %d: ", strlen($case['message']), $case['limit']);
		$logTool = new LogTool($case['message'], $case['limit']);
		$logTool->check($case['lines']);
		if (!$logTool->getError()) {
			echo "OK\n";
		}
	}
	echo "Done\n";
}
