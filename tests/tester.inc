<?php

namespace FPMI;

class Tester
{
    /**
     * Configuration template
     *
     * @var string
     */
    private $configTemplate;

    /**
     * The PHP code to execute
     *
     * @var string
     */
    private $code;

    /**
     * @var array
     */
    private $options;

    /**
     * @var string
     */
    private $fileName;

    /**
     * Tester constructor.
     *
     * @param string $configTemplate
     * @param string $code
     * @param array $options
     */
    public function __construct($configTemplate, $code, array $options = [])
    {
        $this->configTemplate = $configTemplate;
        $this->code = $code;
        $this->options = $options;
        $this->fileName = $this->getCallerFileName();
    }

    /**
     * Start PHP-FPMi master process
     */
    public function start()
    {
        //TODO: implement
    }

    /**
     * Execute request
     *
     * @param bool $print
     */
    public function request($print = false)
    {

    }

    /**
     * Display logs
     *
     * @param int $number
     * @param string $ignore
     */
    public function displayLog($number = 1, $ignore = 'systemd')
    {
        //TODO: implement
    }

    /**
     * Get log lines
     *
     * @param int $number
     * @param bool $skipBlank
     * @param string $ignore
     */
    public function getLogLines($number = 1, $skipBlank = false, $ignore = 'systemd')
    {
        //TODO: implement
    }

    /**
     * @return string
     */
    private function getAddr()
    {
        return $this->options['addr'] ?? $this->getHost() . ':' . $this->getPort();
    }

    /**
     * @return string
     */
    private function getCallerFileName()
    {
        $backtrace = debug_backtrace();
        if (isset($backtrace[1]['file'])) {
            $filePath = $backtrace[1]['file'];
        } else {
            $filePath = __FILE__;
        }

        return substr($filePath, 0, -strlen(pathinfo($filePath, PATHINFO_EXTENSION)));
    }

    /**
     * @return int
     */
    private function getPort()
    {
        return 9000 + PHP_INT_SIZE;
    }

    /**
     * @return string
     */
    private function getHost()
    {
        return '127.0.0.1';
    }
}