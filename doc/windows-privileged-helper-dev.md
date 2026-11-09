# Windows privileged helper — FOSS developer signing guide

The Windows helper path (built by default; disable with
`RPI_IMAGER_DISABLE_WINDOWS_HELPER=ON`) uses `RPI_IMAGER_USE_WINDOWS_HELPER=1`
at runtime and enforces **publisher-pinned peer
authentication** before honoring pipe RPCs (`peer_auth_win.cpp`). There is
**no runtime bypass**: the connecting client must be

1. named `rpi-imager.exe` (or whatever `RPI_IMAGER_WINDOWS_CLIENT_EXE` is set to),
2. installed beside `rpi-imager-writer.exe`, and
3. **Authenticode-signed**, with signer certificate **Organization (O=)** matching
   `RPI_IMAGER_PUBLISHER_ORG`, and leaf **SHA-1 thumbprint** in the peer-auth
   allowlist (auto-populated from your cert store by default).

Unsigned local builds therefore fail peer auth. This guide explains how FOSS
developers can sign **both** binaries for local testing without a commercial
certificate.

## What peer auth checks

After `WinVerifyTrust` succeeds on the client binary, the helper inspects the
**leaf code-signing certificate** and requires:

| Check | CMake source | When enforced |
|-------|----------------|---------------|
| Organization (`O=`) | `RPI_IMAGER_PUBLISHER_ORG` → `kPublisherOrganizationWide` | Always |
| SHA-1 thumbprint | `kTrustedSignerThumbprints[]` in generated `rpi_imager_identity.h` | When the list is non-empty |

The thumbprint allowlist is built at **CMake configure time** from:

1. **Auto-discovery** (default, `RPI_IMAGER_AUTO_TRUST_SIGNING_CERT=ON`): on a
   Windows host, scans `CurrentUser\My` and `LocalMachine\My` for valid
   code-signing certs whose `O=` matches `RPI_IMAGER_PUBLISHER_ORG`.
2. **Manual extras**: `RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINTS` (semicolon-separated;
   useful for cert rotation or non-Windows configure hosts).
3. **`IMAGER_SIGNING_CERT_SHA1`**: always merged into the allowlist when set; also
   passed to `signtool` as `/sha1` when `IMAGER_SIGNED_APP=ON`.

Re-run CMake after creating or rotating a cert so `rpi_imager_identity.h` picks
up the new thumbprint.

## Option A — self-signed cert (recommended for local dev)

Self-signed Authenticode is free and sufficient for exercising the helper on your
own machine. The signature is only trusted where you install the certificate.

### 1. Create a code-signing certificate

Pick an Organization string for your dev cert, e.g. `FOSS Dev`. In an elevated
PowerShell session (or Current User store if you prefer):

```powershell
$org = "FOSS Dev"   # your dev publisher name — used in CMake below

$cert = New-SelfSignedCertificate `
  -Type CodeSigningCert `
  -Subject "O=$org, CN=Raspberry Pi Imager Dev" `
  -CertStoreLocation Cert:\CurrentUser\My `
  -KeyExportPolicy Exportable `
  -KeyUsage DigitalSignature `
  -KeyAlgorithm RSA `
  -KeyLength 2048 `
  -HashAlgorithm sha256 `
  -NotAfter (Get-Date).AddYears(3)

$thumb = $cert.Thumbprint -replace ' ', ''
Write-Host "Publisher org:  $org"
Write-Host "Thumbprint:     $thumb"
```

Export and trust the certificate on the dev machine (required for
`WinVerifyTrust` to succeed):

```powershell
$exportPath = "$env:TEMP\rpi-imager-dev-codesign.cer"
Export-Certificate -Cert $cert -FilePath $exportPath | Out-Null
Import-Certificate -FilePath $exportPath -CertStoreLocation Cert:\CurrentUser\Root | Out-Null
Import-Certificate -FilePath $exportPath -CertStoreLocation Cert:\CurrentUser\TrustedPublisher | Out-Null
```

### 2. Configure CMake

On a **Windows** machine with the cert installed, thumbprints are discovered
automatically — you only need to set the publisher org:

```powershell
cmake -S src -B build `
  -DRPI_IMAGER_PUBLISHER_ORG="$org" `
  -DQt6_ROOT=C:\Qt\6.9.0\mingw_64 `
  -DMINGW64_ROOT=C:\Qt\Tools\mingw1310_64
```

CMake should print something like:

```
-- Auto-discovered Windows code-signing thumbprint(s) for 'FOSS Dev': A1B2...
-- Auto-selected IMAGER_SIGNING_CERT_SHA1=A1B2...
```

If auto-discovery is disabled or you configure on a non-Windows host, pass the
thumbprint explicitly:

```powershell
cmake ... `
  -DRPI_IMAGER_AUTO_TRUST_SIGNING_CERT=OFF `
  -DRPI_IMAGER_TRUSTED_SIGNER_THUMBPRINTS="$thumb"
```

For signed release-style builds, enable CMake signing — it uses the same
auto-discovered `IMAGER_SIGNING_CERT_SHA1`:

```powershell
cmake ... -DIMAGER_SIGNED_APP=ON
```

### 3. Build both binaries

```powershell
cmake --build build --target rpi-imager rpi-imager-writer
```

Both executables land in `build\` (helper is not copied to `deploy\` until the
main app post-build step runs; for a quick test, copy them to the same folder
manually).

### 4. Sign both executables

When **not** using `-DIMAGER_SIGNED_APP=ON`, sign manually with the same cert:

```powershell
$signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"

& $signtool sign /fd sha256 /sha1 $thumb build\rpi-imager.exe
& $signtool sign /fd sha256 /sha1 $thumb build\rpi-imager-writer.exe
```

With `-DIMAGER_SIGNED_APP=ON`, post-build `signtool` uses `/sha1
$IMAGER_SIGNING_CERT_SHA1` (auto-discovered or set explicitly) for
`rpi-imager.exe`, `rpi-imager-callback-relay.exe`, and `rpi-imager-writer.exe`.

Timestamping (`/tr http://timestamp.digicert.com`) is optional for local dev;
omit it if you are offline.

### 5. Run with the helper opt-in

Both binaries must sit in the **same directory** (peer auth checks co-location).

```powershell
$env:RPI_IMAGER_USE_WINDOWS_HELPER = "1"
.\build\rpi-imager.exe
```

On first write, accept the UAC prompt for the elevated helper. If peer auth
fails, the helper exits immediately and the client sees a broken pipe — check
the troubleshooting table below.

### Troubleshooting

| Symptom | Likely cause |
|--------|----------------|
| Helper exits instantly, client cannot connect | Unsigned client or helper; exes not co-located |
| `WinVerifyTrust` fails | Cert not in `CurrentUser\Root` / `TrustedPublisher` |
| Pinning fails despite valid signature | `RPI_IMAGER_PUBLISHER_ORG` does not match cert `O=` — reconfigure CMake |
| Pinning fails, `O=` is correct | Cert not in store at configure time, or auto-discovery off — set `RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINTS` and reconfigure |
| CMake shows no auto-discovered thumbprints | Cert `O=` mismatch, cert expired, or no private key — fix cert or pass thumbprint manually |
| Works once, fails after rebuild | Forgot to re-sign after linking new binaries |
| Works after cert renewal | Re-run CMake so the new thumbprint is discovered |

Verify a binary’s publisher and thumbprint:

```powershell
Get-AuthenticodeSignature .\build\rpi-imager.exe | Format-List

# Thumbprint of the signer cert (should be in the peer-auth allowlist)
(Get-AuthenticodeSignature .\build\rpi-imager.exe).SignerCertificate.Thumbprint -replace ' ', ''
```

Re-print thumbprints from your dev cert store:

```powershell
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
  Where-Object { $_.Subject -match "O=$org" } |
  Select-Object Subject, @{N='Thumbprint';E={$_.Thumbprint -replace ' ',''}}
```

## Option B — free or subsidised certificates for FOSS releases

Self-signed certs are appropriate for **local development only**. They are not
suitable for redistribution: other users’ machines will not trust them.

For **shipping** open-source builds to end users, you need a certificate chained
to a publicly trusted code-signing CA. Options include:

- **[SignPath Foundation](https://signpath.org/)** — free code signing for
  qualifying open-source projects (common choice for FOSS Windows releases).
- **Commercial EV/OV code-signing CAs** — paid; required for SmartScreen
  reputation without a long “unknown publisher” period.

Set `RPI_IMAGER_PUBLISHER_ORG` to the certificate `Organization` field. On the
Windows release build host, auto-discovery picks up the signing cert thumbprint;
add extra thumbprints via `RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINTS` during cert
rotation. Sign both `rpi-imager.exe` and `rpi-imager-writer.exe` before publishing.

There is no widely used “free Let’s Encrypt equivalent” for Windows Authenticode;
plan on self-signed for local helper testing and a proper CA (or SignPath) for
releases.

## What not to do

- **Do not commit** changed identity values in `rpi_imager_identity.cmake` —
  use CMake cache overrides on your machine only.
- **Do not add a peer-auth bypass** — it would let any local process that can
  reach the pipe masquerade as the client while the helper is elevated.
- **Do not ship** self-signed builds to users; use them only on the machine where
  you installed the dev root cert.

## Related CMake options

| Variable | Purpose |
|----------|---------|
| `RPI_IMAGER_DISABLE_WINDOWS_HELPER` | Omit the helper, PAL backend, and adapter (default: **built**) |
| `RPI_IMAGER_PUBLISHER_ORG` | Publisher `O=` pinned by peer auth (override for dev) |
| `RPI_IMAGER_AUTO_TRUST_SIGNING_CERT` | Auto-discover thumbprints from cert store (default ON) |
| `RPI_IMAGER_TRUSTED_SIGNER_THUMBPRINTS` | Extra thumbprints (manual / rotation / non-Windows configure) |
| `IMAGER_SIGNING_CERT_SHA1` | `signtool /sha1` cert; auto-discovered when empty |
| `RPI_IMAGER_WINDOWS_CLIENT_EXE` | Client basename check (default `rpi-imager.exe`) |
| `IMAGER_SIGNED_APP` | Post-build `signtool` for app, relay, and helper |
| `RPI_IMAGER_USE_WINDOWS_HELPER=1` | Runtime opt-in (environment variable) |

See also `CONTRIBUTING.md` (Windows build).
