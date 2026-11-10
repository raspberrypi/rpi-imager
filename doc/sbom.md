# Software bill of materials

Every build of Raspberry Pi Imager produces an SBOM in two formats from one
recorded component list:

- **SPDX 2.3** JSON — `rpi-imager-<version>-<platform>.spdx.json`
- **CycloneDX 1.6** JSON — `rpi-imager-<version>-<platform>.cdx.json`

`<platform>` is `<os>[-embedded]-<arch>`, for example `linux-x86_64`,
`linux-embedded-aarch64`, `windows-x86_64` or `macos-universal`. The CLI build
is named `rpi-imager-cli-…` instead.

`<arch>` is the compiler's target, taken from its Debian multiarch triplet
where there is one, never the host kernel's: a VM, chroot or container, or
32-bit Pi OS on a 64-bit kernel, would otherwise be mislabelled. Imager's own
components carry it as the purl's `arch` qualifier, and the SPDX root package
says it in its comment. Bundled Debian libraries take theirs from the package
dpkg reports as owning them.

Imager links most of its dependencies statically, so a binary scanner such as
syft cannot see them. The SBOM is therefore built from the build system's own
metadata: each dependency registers itself in CMake where it is declared.

## What is listed

- Imager itself (`rpi-imager` or `rpi-imager-cli`), plus `rpi-imager-writer`
  and, on Windows, `rpi-imager-callback-relay` where they are built.
- Every bundled or vendored library: curl, libarchive, xz (liblzma), zstd, zlib,
  nghttp2, libusb, bzip2, protobuf, and the source copies of yescrypt,
  sha256crypt and qrcodegen. Each carries its version, supplier, repository,
  git tag, the commit of the checkout it was built from when that can be read
  at configure time, an SPDX licence expression, and a purl and CPE where one
  is well established.
- The Qt modules linked, at `Qt6_VERSION`.
- Data compiled in: tzdata (`TZDATA_VERSION`), wireless-regdb, and the Roboto
  font.
- On Linux, the system libraries linked dynamically (GnuTLS, libidn2, nettle,
  libudev, liburing when used, GPGME for the helper), at the versions
  pkg-config reported at configure time. On macOS, the SDK's libcurl.
- Everything the package bundles: the libraries and plugins deployed by
  macdeployqt and windeployqt, the MinGW runtime DLLs, and the AppImage and
  embedded library closures (see below).

Test-only dependencies (Catch2) and build tools (protoc) are not listed. A
component appears only if something reachable from the application uses it,
so nghttp2, for example, drops out on macOS where curl comes from the SDK.

Relationships: the document `DESCRIBES` the application; the application
`DEPENDS_ON` what it links and `CONTAINS` the helper binaries and bundled
files. CycloneDX carries the same graph in `dependencies`.

## File checksums

Every regular file in the package is listed with SHA-1, SHA-256 and SHA-512
checksums, under the component it belongs to: a bundled library or plugin owns
its own files, Imager's executables are matched by name, and anything else
belongs to the application. In SPDX each file is in `files`, the owning
package lists it in `hasFiles` and carries a `packageVerificationCode`; in
CycloneDX the files are `file` components, nested under their owner. A
component that ships as a single file also carries its hashes directly.
Symbolic links are not listed.

The checksums are taken from the package as it ships, so the SBOM is
generated after signing:

- **macOS, signed:** `create_styled_dmg.sh` signs the frameworks, dylibs and
  helper, writes the SBOM into the bundle, then seals the bundle. The seal
  covers the SBOM, so the in-bundle copy cannot hash the main executable or
  `_CodeSignature/CodeResources`. It names them in
  `packageVerificationCodeExcludedFiles` and the `rpi-imager:unhashed`
  property.
- **Windows:** the executables in `deploy/` are signed when they are built, so
  Inno Setup's `signonce` leaves them as hashed.
- **AppImage and embedded:** the SBOM is written after stripping and after
  linuxdeploy, which no longer packs: appimagetool does.

The copy beside each artefact is written last, from the finished tree. It
hashes every file and the artefact itself, which it names as the root
package's `packageFileName` and `checksums` (CycloneDX: the root component's
`hashes`).

## Regulatory fields

The fields follow CISA's minimum elements for an SBOM and BSI TR-03183-2,
which guidance under the EU Cyber Resilience Act refers to:

| Element | SPDX | CycloneDX |
| --- | --- | --- |
| SBOM author, timestamp | `creationInfo` | `metadata.authors`, `timestamp` |
| Tool name and version | `creators` | `metadata.tools` |
| Producer, with URL | `supplier` | `metadata.manufacturer`, `supplier.url` |
| Name, version, identifiers | `name`, `versionInfo`, purl and CPE | `name`, `version`, `purl`, `cpe` |
| Hashes | `checksums`, `files` | `hashes`, file components |
| Licence (declared) | `licenseDeclared` | `licenses[].acknowledgement` |
| Dependencies | `relationships` | `dependencies` |
| Generation context | `creationInfo.comment` | `metadata.lifecycles` (`build`, or `post-build` once packaged) |
| Known unknowns | `NOASSERTION`, `creationInfo.comment` | `compositions` (`incomplete`) |
| File name, executable, archive, structured | `packageFileName` | `bsi:component:*` properties |
| Security contact | `creationInfo.comment` | `security-contact` reference |

The security contact is `mailto:security@raspberrypi.com`, from the
`RPI_IMAGER_SBOM_SECURITY_CONTACT` cache variable; configure warns if it is
cleared.

## Where it goes

| Build | Inside the package | Beside the artefact |
| --- | --- | --- |
| All | `<build>/sbom/` | |
| macOS | `Contents/Resources/sbom/` in the app bundle | `imager_<version>.dmg.spdx.json`, … |
| Windows | `{app}\sbom\` (from `<build>/deploy/sbom/`) | `<build>/installer/imager_<version>.exe.spdx.json`, … |
| AppImage | `usr/share/doc/rpi-imager[-cli]/sbom/` | `<name>.AppImage.spdx.json`, … |
| Debian (desktop, CLI) | `/usr/share/doc/rpi-imager[-cli]/sbom/` | |
| Embedded `.deb` | `/usr/share/doc/rpi-imager-embedded/sbom/` | `<name>.deb.spdx.json`, … |

Beside an artefact, the SBOM takes the artefact's whole filename, extension
included, so it names the file it describes.

The `rpi_imager_sbom` target is part of `ALL` and depends on the application,
so it runs after the link and after macdeployqt or windeployqt. The `dmg` and
`inno_installer` targets depend on it, the Inno Setup script installs it, and
on Linux `cmake --install` installs it.

The AppImage and embedded scripts then regenerate it over the staged tree
(`sbom_package_tree` in `debian/lib.sh`) once the tree is final, and write
another beside the artefact (`sbom_publish_beside`). The Debian
packages that wrap the AppImages take those copies:
`debian/stage-appimages.sh` stages them and fails if they are missing,
`debian/sync-appimages.sh` carries them into the AppImage cache, and
`dh_compress` leaves them uncompressed.

## Bundled libraries

On macOS and Windows the generator scans the deployed tree. Anything under a
plugin or QML directory, and anything named like a Qt library, is recorded as
Qt at the Qt version; the GCC and winpthreads runtime DLLs are recognised by
name.

On Linux, `sbom_bundled_manifest` asks dpkg which package provides each
bundled soname on the build system and records that package, its version and a
`pkg:deb` purl. A library dpkg cannot place is still listed, by file name, with
no version; the generator notes its soname.

## Adding a dependency

Register it where it is declared, after it is fetched so the source directory
is known:

```cmake
rpi_imager_sbom_add_component(foo
    VERSION "${FOO_VERSION}"
    SUPPLIER "Foo project"
    VCS_URL "https://github.com/foo/foo.git"
    GIT_TAG "v${FOO_VERSION}"
    SOURCE_DIR "${foo_SOURCE_DIR}"
    LICENSE "MIT"
    PURL "pkg:github/foo/foo@v${FOO_VERSION}"
    CPE "cpe:2.3:a:foo:foo:${FOO_VERSION}:*:*:*:*:*:*:*")
```

The id (`foo`) may use letters, digits, `.` and `-`; when the dependency is
fetched with `rpi_imager_fetch_git_or_vendor()`, use the same name so the
SBOM records whether the vendored tree or a git fetch was used.

| Argument | Meaning |
| --- | --- |
| `NAME` | Display name; defaults to the id |
| `VERSION` | Upstream version |
| `SUPPLIER` | Organisation, or `Person: <name>` |
| `DOWNLOAD_LOCATION` | Release URL, when there is no repository |
| `VCS_URL`, `GIT_TAG`, `COMMIT` | Repository, tag and commit |
| `SOURCE_DIR` | Checkout to read the commit from; ignored unless it is the root of its own git checkout |
| `LICENSE` | SPDX licence expression; `NOASSERTION` if unknown |
| `LICENSE_TEXT` | Text for a `LicenseRef-…` licence |
| `PURL`, `CPE` | Package URL and CPE 2.3 name |
| `TYPE` | `library` (default), `application`, `framework` or `data` |
| `DESCRIPTION`, `ORIGIN` | Free text |
| `USED_BY` | Ids of the components that link it; defaults to `rpi-imager` |
| `RELATION` | `DEPENDS_ON` (default) or `CONTAINS` |

Qt modules are registered with `rpi_imager_sbom_add_qt(<module>...)`; keep the
list in `src/CMakeLists.txt` in step with `target_link_libraries()`.

## Generator inputs

`src/cmake/GenerateSbom.cmake` runs in script mode. Configure writes its inputs
to `<build>/sbom/sbom-inputs.cmake`; the build passes that as `SBOM_INPUTS`.
These may be overridden with `-D`:

| Variable | Meaning |
| --- | --- |
| `SBOM_OUTPUT_DIR` | Where to write; defaults to `<build>/sbom` |
| `SBOM_SCAN_ROOT`, `SBOM_SCAN_DIRS` | Deployed tree to scan for bundled libraries |
| `SBOM_COPY_DIRS` | Further directories to write into |
| `SBOM_BUNDLE_MANIFEST` | Tab-separated bundled files: path, name, version, supplier, purl, licence |
| `SBOM_HASH_ROOT` | Package tree to hash; defaults to the app bundle on macOS and `deploy` on Windows |
| `SBOM_HASH_EXCLUDE` | Paths under it, relative, to leave unhashed |
| `SBOM_ARTEFACT` | Release artefact to hash as the root package |
| `SBOM_OUTPUT_STEM` | Write `<stem>.{spdx,cdx}.json` into `SBOM_OUTPUT_DIR`; defaults to the artefact's filename when `SBOM_ARTEFACT` is set |
| `SBOM_LIFECYCLE` | CycloneDX lifecycle phase; `build` by default |
| `SBOM_STAMP` | File touched on success |

`SBOM_OUTPUT_STEM` and `SBOM_ARTEFACT` may contain `@IMAGER_VERSION_STR@`.
Without a stem each output directory holds one SBOM: older `*.spdx.json` and
`*.cdx.json` files there are removed first.

## Reproducibility

- Timestamps come from `SOURCE_DATE_EPOCH` when it is set (`debian/rules`
  sets it from the changelog).
- Components and relationships are sorted by id.
- The SPDX document namespace and the CycloneDX serial number are a name-based
  (SHA-1) UUID of the document name and a hash of its content, so an
  unchanged build produces an identical SBOM.
- Nothing is fetched to produce it; it needs only CMake (no Python), plus dpkg
  for the Linux bundled-library versions.

## Known limits

- tzdata and wireless-regdb are downloaded at build time and fall back to the
  lists in the source tree when offline; the SBOM records the version the
  build asked for, not which of the two was used. wireless-regdb is taken from
  the tip of its repository and has no version.
- System library versions are those found at configure time. On Linux they
  are the build chroot's, which is also what the AppImage bundles.
- Licences are declared from each project's own notices; none are
  concluded from scanning.
- Statically linked components ship inside Imager's executables, so they
  have no hashes of their own.
- The dependencies of listed components (Qt's own third-party code, for
  example) are not enumerated; the SBOM says so in `compositions`.
- A plain `cmake --install` on Linux installs the build's SBOM, which hashes
  nothing: the installed files are known only to the packaging scripts.
