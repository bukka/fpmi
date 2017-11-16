<?php

namespace FPMI;

class Tester
{
    /**
     * @var array
     */
    static private $supportedFiles = ['log', 'src.php', 'ini'];

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
        echo $fpmiPath;
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
        $this->fileName = $this->getCallerFileName();
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
     *
     * @param bool $print
     */
    public function request(bool $print = false)
    {

    }

    /**
     * Display logs
     *
     * @param int $number
     * @param string $ignore
     */
    public function displayLog(int $number = 1, string $ignore = 'systemd')
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
    public function getLogLines(int $number = 1, bool $skipBlank = false, string $ignore = 'systemd')
    {
        //TODO: implement
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
}