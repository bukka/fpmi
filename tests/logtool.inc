<?php

namespace FPMI;

class LogTool
{
	private $symbol;

	private $limit;

	public function __construct($symbol, $limit)
	{
		$this->symbol = $symbol;
		$this->limit = $limit;
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

	}

	private function checkLine($line)
	{

	}
}
