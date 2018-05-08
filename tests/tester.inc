<?php

namespace FPMI;

use Adoy\FastCGI\Client;

require_once 'fcgi.inc';
require_once 'logtool.inc';
require_once 'response.inc';

class Tester
{
    /**
     * @var array
     */
    static private $supportedFiles = [
        'acc.log',
        'err.log',
        'src.php',
        'ini',
        'skip.ini',
    ];

    /**
     * @var array
     */
    static private $filesToClean = ['.user.ini'];

    /**
     * @var bool
     */
    private $debug;

    /**
     * @var array
     */
    private $clients;

    /**
     * @var LogTool
     */
    private $logTool;

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
     * @var resource
     */
    private $masterProcess;

    /**
     * @var resource
     */
    private $outDesc;

    /**
     * @var array
     */
    private $ports = [];

    /**
     * @var string
     */
    private $error;

    /**
     * The last response for the request call
     *
     * @var Response
     */
    private $response;

    /**
     * Clean all the created files up
     */
    static public function clean()
    {
        $filePrefix = self::getCallerFileName();
        if (substr($filePrefix, -6) === 'clean.') {
            $filePrefix = substr($filePrefix, 0, -6);
        }

        $filesToClean = array_merge(
            array_map(
                function($fileExtension) use ($filePrefix) {
                    return $filePrefix . $fileExtension;
                },
                self::$supportedFiles
            ),
            array_map(
                function($fileExtension) {
                    return __DIR__ . '/' . $fileExtension;
                },
                self::$filesToClean
            )
        );

        foreach ($filesToClean as $filePath) {
            if (is_file($filePath)) {
                unlink($filePath);
            }
        }
    }

    /**
     * @return string
     */
    static private function getCallerFileName()
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
     * @return bool|string
     */
    static public function findExecutable()
    {
        $phpPath = getenv("TEST_PHP_EXECUTABLE");
        for ($i = 0; $i < 2; $i++) {
            $slashPosition = strrpos($phpPath, "/");
            if ($slashPosition) {
                $phpPath = substr($phpPath, 0, $slashPosition);
            } else {
                break;
            }
        }

        if ($phpPath && is_dir($phpPath)) {
            if (file_exists($phpPath."/fpmi/php-fpmi") && is_executable($phpPath."/fpmi/php-fpmi")) {
                /* gotcha */
                return $phpPath."/fpmi/php-fpmi";
            }
            $phpSbinFpmi = $phpPath."/sbin/php-fpmi";
            if (file_exists($phpSbinFpmi) && is_executable($phpSbinFpmi)) {
                return $phpSbinFpmi;
            }
        }

        // try local php-fpmi
        $fpmiPath = dirname(__DIR__) . '/php-fpmi';
        if (file_exists($fpmiPath) && is_executable($fpmiPath)) {
            return $fpmiPath;
        }

        return false;
    }

    /**
     * Skip test if any of the supplied files does not exist.
     *
     * @param mixed $files
     */
    static public function skipIfAnyFileDoesNotExist($files)
    {
        if (!is_array($files)) {
            $files = array($files);
        }
        foreach ($files as $file) {
            if (!file_exists($file)) {
                die("skip File $file does not exist");
            }
        }
    }

    /**
     * Skip test if config file is invalid.
     *
     * @param string $configTemplate
     */
    static public function skipIfConfigFails(string $configTemplate)
    {
        $tester = new self($configTemplate, '', [], self::getCallerFileName());
        $testResult = $tester->testConfig();
        if ($testResult !== null) {
            die("skip $testResult");
        }
    }

    /**
     * Skip test if IPv6 is not supported.
     */
    static public function skipIfIPv6IsNotSupported()
    {
        @stream_socket_client('tcp://[::1]:0', $errno);
        if ($errno != 111) {
            die('skip IPv6 is not supported.');
        }
    }

    /**
     * Tester constructor.
     *
     * @param string $configTemplate
     * @param string $code
     * @param array $options
     * @param string $fileName
     */
    public function __construct(
        string $configTemplate,
        string $code = '',
        array $options = [],
        $fileName = null
    ) {
        $this->configTemplate = $configTemplate;
        $this->code = $code;
        $this->options = $options;
        $this->fileName = $fileName ?: self::getCallerFileName();
        $this->logTool = new LogTool();
        $this->debug = (bool) getenv('TEST_FPMI_DEBUG');
    }

    /**
     * @param string $ini
     */
    public function setUserIni(string $ini)
    {
        $iniFile = __DIR__ . '/.user.ini';
        file_put_contents($iniFile, $ini);
    }

    /**
     * Test configuration file.
     *
     * @return null|string
     */
    public function testConfig()
    {
        $configFile = $this->createConfig();
        $cmd = self::findExecutable() . ' -t -y ' . $configFile . ' 2>&1';
        exec($cmd, $output, $code);
        if ($code) {
            return preg_replace("/\[.+?\]/", "", $output[0]);
        }

        return null;
    }

    /**
     * Start PHP-FPMi master process
     *
     * @param string $extraArgs
     * @return bool
     */
    public function start(string $extraArgs = '')
    {
        $configFile = $this->createConfig();
        $desc = $this->outDesc ? [] : [1 => array('pipe', 'w')];
        $asRoot = getenv('TEST_FPMI_RUN_AS_ROOT') ? '--allow-to-run-as-root' : '';
        $cmd = self::findExecutable() . " $asRoot -F -O -y $configFile $extraArgs";
        /* Since it's not possible to spawn a process under linux without using a
         * shell in php (why?!?) we need a little shell trickery, so that we can
         * actually kill php-fpmi */
        $this->masterProcess = proc_open(
            "killit () { kill \$child; }; trap killit TERM; $cmd 2>&1 & child=\$!; wait",
            $desc,
            $pipes
        );
        register_shutdown_function(
            function($masterProcess) use($configFile) {
                @unlink($configFile);
                if (is_resource($masterProcess)) {
                    @proc_terminate($masterProcess);
                    while (proc_get_status($masterProcess)['running']) {
                        usleep(10000);
                    }
                }
            },
            $this->masterProcess
        );
        if (!$this->outDesc !== false) {
            $this->outDesc = $pipes[1];
        }

        return true;
    }

    /**
     * Check if connection works.
     *
     * @param string $host
     * @param null|string $successMessage
     * @param null|string $errorMessage
     * @param int $attempts
     * @param int $delay
     */
    public function checkConnection(
        $host,
        $successMessage = 'ok',
        $errorMessage = null,
        $attempts = 20,
        $delay = 50000
    ) {
        $i = 0;
        do {
            if ($i > 0 && $delay > 0) {
                usleep($delay);
            }
            $fp = @fsockopen($host, $this->getPort());
        } while ((++$i < $attempts) && !$fp);

        if ($fp) {
            $this->message($successMessage);
            fclose($fp);
        } else {
            $this->message($errorMessage);
        }
    }


    /**
     * Execute request with parameters ordered for better checking.
     *
     * @param string $address
     * @param string|null $successMessage
     * @param string|null $errorMessage
     * @param string $uri
     * @param string $query
     * @param array $headers
     * @return Response
     */
    public function checkRequest(
        string $address,
        string $successMessage = null,
        string $errorMessage = null,
        $uri = '/ping',
        $query = '',
        $headers = []
    ) {
        return $this->request($query, $headers, $uri, $address, $successMessage, $errorMessage);
    }

    /**
     * Execute and check ping request.
     *
     * @param string $address
     * @param string $pingPath
     * @param string $pingResponse
     */
    public function ping(
        string $address,
        string $pingResponse = 'pong',
        string $pingPath = '/ping'
    ) {
        $response = $this->request('', [], $pingPath, $address);
        $response->expectBody($pingResponse, 'text/plain');
    }

    /**
     * Execute request.
     *
     * @param string $query
     * @param array $headers
     * @param string|null $uri
     * @param string|null $address
     * @param string|null $successMessage
     * @param string|null $errorMessage
     * @return Response
     */
    public function request(
        string $query = '',
        array $headers = [],
        string $uri = null,
        string $address = null,
        string $successMessage = null,
        string $errorMessage = null
    ) {
        if ($this->hasError()) {
            return new Response(null, true);
        }
        if (is_null($uri)) {
            $uri = $this->makeFile('src.php', $this->code);
        }

        $params = array_merge(
            [
                'GATEWAY_INTERFACE' => 'FastCGI/1.0',
                'REQUEST_METHOD'    => 'GET',
                'SCRIPT_FILENAME'   => $uri,
                'SCRIPT_NAME'       => $uri,
                'QUERY_STRING'      => $query,
                'REQUEST_URI'       => $uri . ($query ? '?'.$query : ""),
                'DOCUMENT_URI'      => $uri,
                'SERVER_SOFTWARE'   => 'php/fcgiclient',
                'REMOTE_ADDR'       => '127.0.0.1',
                'REMOTE_PORT'       => '7777',
                'SERVER_ADDR'       => '127.0.0.1',
                'SERVER_PORT'       => '80',
                'SERVER_NAME'       => php_uname('n'),
                'SERVER_PROTOCOL'   => 'HTTP/1.1',
                'DOCUMENT_ROOT'     => __DIR__,
                'CONTENT_TYPE'      => '',
                'CONTENT_LENGTH'    => 0
            ],
            $headers
        );

        try {
            $this->response = new Response(
                $this->getClient($address)->request_data($params, false)
            );
            $this->message($successMessage);
        } catch (\Exception $exception) {
            if ($errorMessage === null) {
                $this->error("Request failed", $exception);
            } else {
                $this->message($errorMessage);
            }
            $this->response = new Response();
        }
        if ($this->debug) {
            $this->response->debugOutput();
        }
        return $this->response;
    }

    /**
     * Get client.
     *
     * @param string $address
     * @return Client
     */
    private function getClient(string $address = null)
    {
        $address = $address ? $this->processTemplate($address) : $this->getAddr();
        if ($address[0] === '[') {
            $addressParts = explode(']:', $address);
            $host = $addressParts[0];
            if (isset($addressParts[1])) {
                $host .= ']';
                $port = $addressParts[1];
            } else {
                $port = $this->getPort();
            }
        } else {
            $addressParts = explode(':', $address);
            $host = $addressParts[0];
            $port = $addressParts[1] ?? $this->getPort();
        }
        if (!isset($this->clients[$host][$port])) {
            $this->clients[$host][$port] = new Client($host, $port);
        }

        return $this->clients[$host][$port];
    }

    /**
     * Display logs
     *
     * @param int $number
     * @param string $ignore
     */
    public function displayLog(int $number = 1, string $ignore = 'systemd')
    {
        /* Read $number lines or until EOF */
        while ($number > 0 || ($number < 0 && !feof($this->outDesc))) {
            $a = fgets($this->outDesc);
            if (empty($ignore) || !strpos($a, $ignore)) {
                echo $a;
                $number--;
            }
        }
    }

    /**
     * Get log lines
     *
     * @param int $number
     * @param bool $skipBlank
     * @param string $ignore
     * @return array
     */
    public function getLogLines(int $number = 1, bool $skipBlank = false, string $ignore = 'systemd')
    {
        $lines = [];
        /* Read $n lines or until EOF */
        while ($number > 0 || ($number < 0 && !feof($this->outDesc))) {
            $line = fgets($this->outDesc);
            if ((empty($ignore) || !strpos($line, $ignore)) && (!$skipBlank || strlen(trim($line)) > 0)) {
                $lines[] = $line;
                $number--;
            }
        }

        return $lines;
    }

    /**
     * @return mixed|string
     */
    public function getLastLogLine()
    {
        $lines = $this->getLogLines();

        return $lines[0] ?? '';
    }

    /**
     * Terminate master process
     */
    public function terminate()
    {
        proc_terminate($this->masterProcess);
    }

    /**
     * Close all open descriptors and process resources
     *
     * @param bool $terminate
     */
    public function close($terminate = false)
    {
        if ($terminate) {
            $this->terminate();
        }
        fclose($this->outDesc);
        proc_close($this->masterProcess);
    }

    /**
     * Create a config file.
     *
     * @param string $extension
     * @return string
     */
    private function createConfig($extension = 'ini')
    {
        return $this->makeFile($extension, $this->processTemplate($this->configTemplate));
    }

    /**
     * Process template string.
     *
     * @param string $template
     * @return string
     */
    private function processTemplate(string $template)
    {
        $vars = [
            'FILE:LOG:ACC' => ['getFile', 'acc.log'],
            'FILE:LOG:ERR' => ['getFile', 'err.log'],
            'ADDR:IPv4' => ['getAddr', 'ipv4'],
            'ADDR:IPv4:ANY' => ['getAddr', 'ipv4-any'],
            'ADDR:IPv6' => ['getAddr', 'ipv6'],
            'ADDR:IPv6:ANY' => ['getAddr', 'ipv6-any'],
            'PORT' => ['getPort'],
        ];
        $aliases = [
            'ADDR' => 'ADDR:IPv4',
            'FILE:LOG' => 'FILE:LOG:ERR',
        ];
        foreach ($aliases as $aliasName => $aliasValue) {
            $vars[$aliasName] = $vars[$aliasValue];
        }

        return preg_replace_callback(
            '/{{([a-zA-Z0-9:]+)(\[\w+\])?}}/',
            function ($matches) use ($vars) {
                $varName = $matches[1];
                if (!isset($vars[$varName])) {
                    $this->error("Invalid config variable $varName");
                    return 'INVALID';
                }
                $pool = $matches[2] ?? 'default';
                $varArgs = $vars[$varName];
                $functionName = array_shift($varArgs);
                $varArgs[] = $pool;
                return call_user_func_array([$this, $functionName], $varArgs);
            },
            $template
        );
    }

    /**
     * @param string $type
     * @param string $pool
     * @return string
     */
    public function getAddr(string $type = 'ipv4', $pool = 'default')
    {
        return $this->getHost($type) . ':' . $this->getPort($pool);
    }

    /**
     * @param string $pool
     * @return int
     */
    public function getPort($pool = 'default')
    {
        if (isset($this->ports['values'][$pool])) {
            return $this->ports['values'][$pool];
        }
        $port = ($this->ports['last'] ?? 9000 + PHP_INT_SIZE - 1) + 1;
        $this->ports['values'][$pool] = $this->ports['last'] = $port;

        return $port;
    }

    /**
     * @param string $type
     * @return string
     */
    public function getHost(string $type = 'ipv4')
    {
        switch ($type) {
            case 'ipv6-any':
                return '[::]';
            case 'ipv6':
                return '[::1]';
            case 'ipv4-any':
                return '0.0.0.0';
            default:
                return '127.0.0.1';
        }
    }


    /**
     * @param string $extension
     * @return string
     */
    private function getFile(string $extension)
    {
        if (!in_array($extension, self::$supportedFiles)) {
            throw new \InvalidArgumentException("The extension $extension is not supported");
        }

        return $this->fileName . $extension;
    }

    /**
     * @param string $extension
     * @param string $content
     * @return string
     */
    private function makeFile(string $extension, string $content = '')
    {
        $filePath = $this->getFile($extension);
        file_put_contents($filePath, $content);

        return $filePath;
    }

    /**
     * @param string|null $msg
     */
    private function message($msg)
    {
        if ($msg !== null) {
            echo "$msg\n";
        }
    }

    /**
     * @param string $msg
     * @param \Exception|null $exception
     */
    private function error($msg, \Exception $exception = null)
    {
        $this->error =  'ERROR: ' . $msg;
        if ($exception) {
            $this->error .= '; EXCEPTION: ' . $exception->getMessage();
        }
        $this->error .= "\n";

        echo $this->error;
    }

    /**
     * @return bool
     */
    private function hasError()
    {
        return !is_null($this->error) || !is_null($this->logTool->getError());
    }

    /**
     * Expect starting lines to be logged.
     */
    public function expectLogStartNotices()
    {
        $this->logTool->expectStartingLines($this->getLogLines(2));
    }

    /**
     * Expect terminating lines to be logged.
     */
    public function expectLogTerminatingNotices()
    {
        $this->logTool->expectTerminatorLines($this->getLogLines(-1));
    }

    /**
     * @param string $message
     * @param int $limit
     * @param int $repeat
     * @param bool $wrapped
     * @param bool $decorated
     */
    public function expectLogMessage(
        string $message,
        int $limit,
        int $repeat = 0,
        bool $wrapped = true,
        bool $decorated = true
    ) {
        $this->logTool->setExpectedMessage($message, $limit, $repeat);
        if ($wrapped) {
            $logLines = $this->getLogLines(-1, true);
            if ($this->debug) {
                $this->message("-------------- LOG LINES: -------------");
                var_dump($logLines);
                $this->message("---------------------------------------\n");
            }
            $this->logTool->checkWrappedMessage($logLines, true, $decorated);
        } else {
            $this->logTool->checkTruncatedMessage($this->response->getErrorData());
        }
    }

    /**
     * @param string $message
     */
    public function expectLogDebug(string $message)
    {
        $this->logTool->expectDebug($this->getLastLogLine(), $message);
    }

    /**
     * @param string $message
     */
    public function expectLogNotice(string $message)
    {
        $this->logTool->expectNotice($this->getLastLogLine(), $message);
    }

    /**
     * @param string $message
     */
    public function expectLogWarning(string $message)
    {
        $this->logTool->expectWarning($this->getLastLogLine(), $message);
    }

    /**
     * @param string $message
     */
    public function expectLogError(string $message)
    {
        $this->logTool->expectError($this->getLastLogLine(), $message);
    }

    /**
     * @param string $message
     */
    public function expectLogAlert(string $message)
    {
        $this->logTool->expectAlert($this->getLastLogLine(), $message);
    }

    /**
     * Print content of access log.
     */
    public function printAccessLog()
    {
        $accessLog = $this->getFile('acc.log');
        if (is_file($accessLog)) {
            print file_get_contents($accessLog);
        }
    }
}