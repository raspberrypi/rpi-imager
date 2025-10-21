param([string]$url)

# --- Configuration ---
$port = 49629   # must match the port used by imager
# ---------------------

function Send-Url {
    param([string]$u)
    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $iar = $client.BeginConnect('127.0.0.1', $port, $null, $null)
        $wait = $iar.AsyncWaitHandle.WaitOne(300)  # 300 ms connect timeout
        if (-not $wait) {
            $client.Close()
            return $false
        }
        $client.EndConnect($iar)

        $stream = $client.GetStream()
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($u)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        $stream.Close()
        $client.Close()
        return $true
    } catch {
        return $false
    }
}

if (Send-Url $url) {
    exit 0
}

try {
    $exe = Join-Path $PSScriptRoot "rpi-imager.exe"
    if (Test-Path -LiteralPath $exe) {
        Start-Process -FilePath $exe -ArgumentList @("$url") -WorkingDirectory $PSScriptRoot
        exit 0
    } else {
        Add-Type -AssemblyName System.Windows.Forms
        $title = "Raspberry Pi Imager"
        $message = "Could not find Raspberry Pi Imager. Please start the application manually and try again."
        [System.Windows.Forms.MessageBox]::Show($message, $title, [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
        exit 2
    }
} catch {
    # show an error dialog
    Add-Type -AssemblyName System.Windows.Forms
    $title = "Raspberry Pi Imager"
    $message = "Raspberry Pi Imager is not running. Please start the application manually and try again."
    [System.Windows.Forms.MessageBox]::Show($message, $title, [System.Windows.Forms.MessageBoxButtons]::OK, [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    exit 2
}
