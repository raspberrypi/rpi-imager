# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Raspberry Pi Ltd
#
# Emits Authenticode code-signing cert SHA-1 thumbprints from the local cert
# store whose Organization (O=) matches -PublisherOrg. Used at CMake configure
# time to populate peer-auth thumbprint allowlists and IMAGER_SIGNING_CERT_SHA1.
#
# Output format (stdout, one KEY=VALUE per line):
#   THUMBPRINTS=<semicolon-separated 40-char hex, may be empty>
#   PRIMARY=<40-char hex or empty>

param(
    [Parameter(Mandatory = $true)]
    [string]$PublisherOrg
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-CertOrganization {
    param([System.Security.Cryptography.X509Certificates.X509Certificate2]$Cert)
    if ($Cert.Subject -match 'O=([^,]+)') {
        return $Matches[1].Trim()
    }
    return $null
}

$now = Get-Date
$candidates = @()

foreach ($storePath in @('Cert:\CurrentUser\My', 'Cert:\LocalMachine\My')) {
    if (-not (Test-Path $storePath)) {
        continue
    }
    Get-ChildItem $storePath -CodeSigningCert -ErrorAction SilentlyContinue | ForEach-Object {
        if (-not $_.HasPrivateKey) {
            return
        }
        if ($_.NotAfter -lt $now) {
            return
        }
        $org = Get-CertOrganization $_
        if ($null -eq $org) {
            return
        }
        if ($org -ine $PublisherOrg) {
            return
        }
        $thumb = ($_.Thumbprint -replace '\s', '').ToUpperInvariant()
        $candidates += [PSCustomObject]@{
            Thumbprint = $thumb
            NotAfter   = $_.NotAfter
        }
    }
}

$unique = $candidates |
    Sort-Object -Property NotAfter -Descending |
    Select-Object -Property Thumbprint -Unique

$thumbprints = @($unique | ForEach-Object { $_.Thumbprint })
$primary = ''
if ($thumbprints.Count -gt 0) {
    $primary = $thumbprints[0]
}

Write-Output ("THUMBPRINTS=" + ($thumbprints -join ';'))
Write-Output ("PRIMARY=" + $primary)
