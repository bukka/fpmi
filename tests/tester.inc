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
    static private $supportedFiles = ['log', 'src.php', 'ini'];

    /**
     * @var Client
     */
    private $client;

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
     * @var string
     */
    private $error;

    /**
     * Clean all the created files up
     */
    static public function clean()
    {
        $filePrefix = self::getCallerFileName();
        if (substr($filePrefix, -6) === 'clean.') {
            $filePrefix = substr($filePrefix, 0, -6);
        }

        foreach (self::$supportedFiles as $fileExtension) {
            $filePath = $filePrefix . $fileExtension;
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
     * Tester constructor.
     *
     * @param string $configTemplate
     * @param string $code
     * @param array $options
     */
    public function __construct(string $configTemplate, string $code, array $options = [])
    {
        $this->configTemplate = $configTemplate;
        $this->code = $code;
        $this->options = $options;
        $this->fileName = self::getCallerFileName();
        $this->client = new Client($this->getHost(), $this->getPort());
        $this->logTool = new LogTool();
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
     * Execute request
     * @param string $uri
     * @param string $query
     * @param array $headers
     * @return Response
     */
    public function request(string $query = '', array $headers = [], string $uri = null)
    {
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
                'REMOTE_PORT'       => '9985',
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
            return new Response($this->client->request($params, false));
        } catch (\Exception $exception) {
            $this->error("Request failed", $exception);
            return new Response();
        }
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
     * @return string
     */
    private function createConfig()
    {
        $replacementMap = [
            'FILE:LOG' => $this->getFile('log'),
            'ADDR' => $this->getAddr(),
        ];

        $config = $this->configTemplate;
        foreach ($replacementMap as $placeholder => $value) {
            $config = str_replace('{{' . $placeholder . '}}', $value, $config);
        }

        return $this->makeFile('ini', $config);
    }

    /**
     * @return string
     */
    private function getAddr()
    {
        return $this->getHost() . ':' . $this->getPort();
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

    /**
     * @param $msg
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
     * Expect starting lines to be logged
     */
    public function expectLogStartNotices()
    {
        $this->logTool->expectStartingLines($this->getLogLines(2));
    }

    /**
     * @param string $message
     * @param int $limit
     * @param int $repeat
     * @param bool $wrapped
     */
    public function expectLogMessage(string $message, int $limit, int $repeat = 0, bool $wrapped = true)
    {
        $this->logTool->setExpectedMessage($message, $limit, $repeat);
        if ($wrapped) {
            $this->logTool->checkWrappedMessage($this->getLogLines(-1, true));
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
}