<?php

namespace FPMI;

class LogTool
{
    const P_TIME = '\[\d\d-\w\w\w-\d{4} \d\d:\d\d:\d\d\]';
    const P_PREFIX = '\[pool unconfined\] child \d+ said into stderr: ';
    const FINAL_SUFFIX = ', pipe is closed';

    /**
     * @var string
     */
    private $message;

    /**
     * @var string
     */
    private $level;

    /**
     * @var int
     */
    private $position;

    /**
     * @var int
     */
    private $suffixPosition;

    /**
     * @var int
     */
    private $limit;

    /**
     * @var string
     */
    private $pattern;

    /**
     * @var string
     */
    private $error;

    /**
     * @param string $message
     * @param int $limit
     * @param int $repeat
     */
    public function setExpectedMessage(string $message, int $limit, int $repeat = 0)
    {
        $this->message = ($repeat > 0) ? str_repeat($message, $repeat) : $message;
        $this->limit = $limit;
        $this->position = 0;
    }

    /**
     * @return int
     * @todo should be probably part of the message check methods
     */
    public function setExpectedLevel(string $level)
    {
        return $this->level = $level;
    }

    /**
     * @return string
     */
    public function getExpectedLevel(): string
    {
        return $this->level ?: 'WARNING';
    }

    /**
     * @param array $lines
     * @param bool $terminated
     * @return bool
     */
    public function checkTruncatedMessage(array $lines, bool $terminated = true)
    {
        if ($this->message === null) {
            throw new \LogicException('The message has not been set');
        }
        $this->pattern = sprintf(
            '/^(%s %s: )(.*)(\.\.\.)?$/',
            self::P_TIME,
            $this->getExpectedLevel()
        );
        if (preg_match($this->pattern, $lines[0], $matches) === 0) {
            return $this->error("Unexpected truncated message: {$lines[0]}");
        }

        //TODO: add checks for line length and if 3 dots are correctly used (e.g. only for longer messages)


        if ($terminated) {
            return $this->expectTerminatorLines($lines, 0);
        }

        return true;
    }

    /**
     * @param array $lines
     * @param bool $terminated
     * @return bool
     */
    public function checkWrappedMessage(array $lines, bool $terminated = true)
    {
        if ($this->message === null) {
            throw new \LogicException('The message has not been set');
        }
        $this->pattern = sprintf(
            '/^(%s %s: %s)"([^"]*)"(.*)?$/',
            self::P_TIME,
            $this->getExpectedLevel(),
            self::P_PREFIX
        );

        $idx = 0;
        foreach ($lines as $idx => $line) {
            if (!$this->checkLine($line)) {
                break;
            }
        }

        if ($this->suffixPosition > 0) {
            $suffixPattern = sprintf(
                '/^%s %s: %s(.*)$/',
                self::P_TIME, $this->getExpectedLevel(),
                self::P_PREFIX
            );
            $line = $lines[++$idx];
            if (preg_match($suffixPattern, $line, $matches) === 0) {
                return $this->error("Unexpected line: $line");
            }
            if ($matches[1] !== substr(self::FINAL_SUFFIX, $this->suffixPosition)) {
                return $this->error("The suffix has not been finished from position $this->suffixPosition in line: $line");
            }
        }

        if ($terminated) {
            return $this->expectTerminatorLines($lines, $idx);
        }

        return true;
    }

    /**
     * @param string $line
     * @return bool
     */
    private function checkLine(string $line)
    {
        if (preg_match($this->pattern, $line, $matches) === 0) {
            return $this->error("Unexpected line: $line");
        }

        $rem = strlen($this->message) - $this->position;
        $lineLen = strlen($line);
        if ($lineLen > $this->limit) {
            return $this->error("The line length is $lineLen which is higher than limit $this->limit");
        }
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

    /**
     * @param array $lines
     * @return bool
     */
    public function expectStartingLines(array $lines)
    {
        if ($this->getError()) {
            return false;
        }

        if (count($lines) < 2) {
            return $this->error("No starting lines");
        }

        return (
            $this->expectNotice($lines[0], 'fpmi is running, pid \d+') &&
            $this->expectNotice($lines[1], 'ready to handle connections')
        );
    }

    /**
     * @param array $lines
     * @param int $idx
     * @return bool
     */
    public function expectTerminatorLines(array $lines, int $idx)
    {
        if ($this->getError()) {
            return false;
        }

        if (count($lines) - $idx < 3) {
            return $this->error("No terminating lines");
        }

        return (
            $this->expectNotice($lines[++$idx], 'Terminating ...') &&
            $this->expectNotice($lines[++$idx], 'exiting, bye-bye!')
        );
    }

    /**
     * @param string $type
     * @param string $line
     * @param string $expectedMessage
     * @return bool
     */
    public function expectEntry(string $type, string $line, string $expectedMessage)
    {
        if ($this->getError()) {
            return false;
        }

        $line = rtrim($line);
        $pattern = sprintf('/^%s %s: %s$/', self::P_TIME, $type, $expectedMessage);

        if (preg_match($pattern, $line, $matches) === 0) {
            return $this->error("The $type '$line' does not match expected message '$expectedMessage'");
        }
    }

    /**
     * @param string $line
     * @param string $expectedMessage
     * @return bool
     */
    public function expectDebug(string $line, string $expectedMessage)
    {
        return $this->expectEntry('DEBUG', $line, $expectedMessage);
    }

    /**
     * @param string $line
     * @param string $expectedMessage
     * @return bool
     */
    public function expectNotice(string $line, string $expectedMessage)
    {
        return $this->expectEntry('NOTICE', $line, $expectedMessage);
    }

    /**
     * @param string $line
     * @param string $expectedMessage
     * @return bool
     */
    public function expectWarning(string $line, string $expectedMessage)
    {
        return $this->expectEntry('WARNING', $line, $expectedMessage);
    }

    /**
     * @param string $line
     * @param string $expectedMessage
     * @return bool
     */
    public function expectError(string $line, string $expectedMessage)
    {
        return $this->expectEntry('ERROR', $line, $expectedMessage);
    }

    /**
     * @param string $line
     * @param string $expectedMessage
     * @return bool
     */
    public function expectAlert(string $line, string $expectedMessage)
    {
        return $this->expectEntry('ALERT', $line, $expectedMessage);
    }


    /**
     * @param string $msg
     * @return bool
     */
    private function error(string $msg)
    {
        $this->error = $msg;
        echo "ERROR: $msg\n";
        return false;
    }

    /**
     * @return string
     */
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
            'type' => 'stdio',
        ],
        [
            'limit' => 1050,
            'lines' => [
                '[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 968) . '"',
                '[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 968) . '"',
                '[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: "' . str_repeat('a', 964) . '", pi',
                '[08-Oct-2017 19:53:50] WARNING: [pool unconfined] child 23183 said into stderr: pe is closed',
                '[08-Oct-2017 19:53:55] NOTICE: Terminating ...',
                '[08-Oct-2017 19:53:55] NOTICE: exiting, bye-bye!',
            ],
            'message' => str_repeat('a', 2900),
            'type' => 'stdio',
        ],
        [
            'limit' => 1024,
            'lines' => [
                '[08-Oct-2017 19:53:50] WARNING: ' . str_repeat('a',989) . '...',
                '[08-Oct-2017 19:53:55] NOTICE: Terminating ...',
                '[08-Oct-2017 19:53:55] NOTICE: exiting, bye-bye!',
            ],
            'message' => str_repeat('a', 2900),
            'type' => 'message',
        ],
    ];
    foreach ($cases as $case) {
        printf("Test message with len %d and limit %d: ", strlen($case['message']), $case['limit']);
        $logTool = new LogTool();
        $logTool->setExpectedMessage($case['message'], $case['limit']);
        if ($case['type'] === 'stdio') {
            $logTool->checkWrappedMessage($case['lines']);
        } else {
            $logTool->checkTruncatedMessage($case['lines']);
        }
        if (!$logTool->getError()) {
            echo "OK\n";
        }
    }
    echo "Done\n";
}
