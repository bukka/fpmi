<?php

namespace FPMI;

class Response
{
    const HEADER_SEPARATOR = "\r\n\r\n";

    /**
     * @var string
     */
    private $rawData;

    /**
     * @var string
     */
    private $rawHeaders;

    /**
     * @var string
     */
    private $rawBody;

    /**
     * @var array
     */
    private $headers;

    /**
     * @var bool
     */
    private $valid;

    /**
     * @var bool
     */
    private $expectInvalid;

    /**
     * @param string|null $rawData
     * @param bool $expectInvalid
     */
    public function __construct($rawData = null, $expectInvalid = false)
    {
        $this->rawData = $rawData;
        $this->expectInvalid = $expectInvalid;
        if ($rawData === null || strpos($rawData, self::HEADER_SEPARATOR) === false) {
            $this->valid = false;
            return;
        }

        list ($this->rawHeaders, $this->rawBody) = array_map(
            'trim',
            explode(self::HEADER_SEPARATOR, $this->rawData)
        );
        $this->valid = true;
    }

    /**
     * @param $body
     * @return Response
     */
    public function expectBody($body)
    {
        if ($this->checkIfValid() && $this->checkDefaultHeaders() && $body !== $this->rawBody) {
            $this->error("The expected body '$body' does not match actual body '$this->rawBody'");
        }

        return $this;
    }

    /**
     * @return Response
     */
    public function expectEmptyBody()
    {
        return $this->expectBody('');
    }

    /**
     * @return string|null
     */
    public function getBody()
    {
        return ($this->checkIfValid() && $this->checkDefaultHeaders()) ? $this->rawBody : null;
    }

    /**
     * Print raw body
     */
    public function dumpBody()
    {
        var_dump($this->getBody());
    }

    /**
     * Print raw body
     */
    public function printBody()
    {
        echo $this->getBody();
    }

    /**
     * Check if the response is valid and if not emit error message
     * @return bool
     */
    private function checkIfValid()
    {
        if ($this->valid) {
            return true;
        }

        if (!$this->expectInvalid) {
            $this->error("The response is invalid: $this->rawData");
        }

        return false;
    }

    /**
     * @return bool
     */
    private function checkDefaultHeaders()
    {
        // check default headers
        return (
            $this->checkHeader('X-Powered-By', '|^PHP/7|', true) &&
            $this->checkHeader('Content-type', '|^text/html; charset=\w+|', true)
        );
    }

    /**
     * @param string $name
     * @param string $value
     * @param bool $useRegex
     * @return bool
     */
    private function checkHeader(string $name, string $value, $useRegex = false)
    {
        $headers = $this->getHeaders();
        if (!isset($headers[$name])) {
            return $this->error("The header $name is not present");
        }
        $header = $headers[$name];

        if (!$useRegex) {
            if ($header === $value) {
                return true;
            }
            return $this->error("The header $name value '$header' is not the same as '$value'");
        }

        if (!preg_match($value, $header)) {
            return $this->error("The header $name value '$header' does not match RegExp '$value'");
        }

        return true;
    }

    /**
     * @return array|null
     */
    private function getHeaders()
    {
        if (!$this->valid) {
            return null;
        }

        if (is_array($this->headers)) {
            return $this->headers;
        }

        $headerRows = explode("\r\n", $this->rawHeaders);
        $headers = [];
        foreach ($headerRows as $headerRow) {
            $colonPosition = strpos($headerRow, ':');
            if ($colonPosition === false) {
                $this->error("Invalid header row (no colon): $headerRow");
            }
            $headers[substr($headerRow, 0, $colonPosition)] = trim(
                substr($headerRow, $colonPosition + 1)
            );
        }

        return ($this->headers = $headers);
    }

    /**
     * Emit error message
     *
     * @param string $message
     * @return bool
     */
    private function error($message)
    {
        echo "ERROR: $message\n";

        return false;
    }
}