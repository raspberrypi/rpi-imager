# Performance Data Capture

Raspberry Pi Imager includes a built-in performance data capture feature that records detailed timing and throughput information during image writing operations. This data is invaluable for diagnosing slow writes, identifying bottlenecks, and understanding system behaviour.

> **Note for bug reports:** If you're experiencing performance issues and wish to report them, you **must** include a performance data file captured using the method described below. Performance-related issues submitted without this data will not be considered, as it's simply not possible to diagnose throughput problems without the detailed timing information this feature provides.

## Capturing Performance Data

Performance data is captured automatically during every write operation. To export the data:

1. **Press `Ctrl+Shift+P`** at any time (during or after a write operation)
2. A native file dialog will appear — choose where to save the JSON file
3. The file will contain all captured performance metrics

The export process is designed to have zero impact on ongoing operations. All complex data processing (histogram generation, statistics calculation) happens only when you trigger the export.

## What's Captured

### Discrete Events

The following operations are timed individually, grouped by category:

**Network & OS List**
| Event | Description |
|-------|-------------|
| `osListFetch` | Time to fetch the OS list from the server |
| `osListParse` | Time to parse the OS list JSON |
| `sublistFetch` | Time to fetch OS sublists |
| `networkLatency` | Network round-trip measurements |
| `networkRetry` | Network connection retry (includes error type, offset, sleep duration) |
| `networkConnectionStats` | CURL connection timing (DNS, connect, TLS, TTFB, speed, HTTP version) |

**Drive Operations**
| Event | Description |
|-------|-------------|
| `driveListPoll` | Time for drive enumeration |
| `driveOpen` | Time to open and prepare the drive for writing (includes direct_io mode) |
| `driveUnmount` | Time to unmount drive partitions (Linux/macOS) |
| `driveUnmountVolumes` | Time to unmount/lock volumes (Windows) |
| `driveDiskClean` | Time to clean disk/remove partitions (Windows) |
| `driveRescan` | Time to rescan disk after cleaning (Windows) |
| `driveFormat` | Time to format drive (for multi-file zips) |

**Cache Operations**
| Event | Description |
|-------|-------------|
| `cacheLookup` | Time to look up file in cache |
| `cacheVerification` | Time to verify cached file hash |
| `cacheWrite` | Time to write data to cache file |
| `cacheFlush` | Time to flush cache to disk |

**Memory Management**
| Event | Description |
|-------|-------------|
| `memoryAllocation` | Time for large memory allocations |
| `bufferResize` | Time to resize buffers |
| `pageCacheFlush` | Time to flush system page cache |
| `ringBufferStarvation` | Ring buffer stall (producer waiting for disk, or consumer waiting for network) |

**Image Processing**
| Event | Description |
|-------|-------------|
| `imageDecompressInit` | Time to initialise decompression |
| `imageExtraction` | Time for archive extraction setup |
| `hashComputation` | Time spent computing hashes |

**Customisation**
| Event | Description |
|-------|-------------|
| `customisation` | Time to apply OS customisation |
| `cloudInitGeneration` | Time to generate cloud-init config |
| `firstRunGeneration` | Time to generate firstrun script |
| `secureBootSetup` | Time to set up secure boot files |

**Finalisation**
| Event | Description |
|-------|-------------|
| `partitionTableWrite` | Time to write the partition table |
| `fatPartitionSetup` | Time to set up the FAT partition |
| `finalSync` | Time for final sync/flush operations |
| `deviceClose` | Time to close device handles |

### Throughput Histograms

For the download, decompress, write, and verify phases, throughput is captured as a time-series of histograms. Each one-second window contains:

- **Timestamp** (milliseconds from operation start)
- **Min/Max/Average throughput** for that window
- **Histogram buckets** showing the distribution of throughput samples

The histogram uses logarithmic buckets (in MB/s):
- 0–1, 1–2, 2–4, 4–8, 8–16, 16–32, 32–64, 64–128, 128–256, 256–512, 512–1024, 1024+

This approach captures both the typical throughput and any variability or stalls.

**Phase descriptions:**
- **Download**: Compressed bytes received from network or read from cache
- **Decompress**: Uncompressed bytes output from the decompressor (xz, gzip, zstd, etc.)
- **Write**: Uncompressed bytes written to the storage device
- **Verify**: Bytes read back from the device during verification

By separating decompress from write, you can identify whether the bottleneck is CPU-bound decompression or I/O-bound disk writes.

## JSON Schema

```json
{
    "version": 2,
    "exportTime": "2025-11-28T14:32:15",
    "summary": {
        "imageName": "Raspberry Pi OS (64-bit)",
        "deviceName": "\\\\.\\PhysicalDrive2",
        "imageSize": 4294967296,
        "sessionStartTime": 1732800735000,
        "sessionEndTime": 1732800915000,
        "durationMs": 180000,
        "success": true,
        "events": {
            "driveOpen": {
                "count": 1,
                "totalMs": 234,
                "avgMs": 234,
                "minMs": 234,
                "maxMs": 234
            }
        },
        "phases": {
            "download": {
                "sampleCount": 150,
                "bytesTotal": 1073741824,
                "durationMs": 25000,
                "minThroughputKBps": 32768,
                "maxThroughputKBps": 65536,
                "avgThroughputKBps": 42000
            },
            "decompress": { ... },
            "write": { ... },
            "verify": { ... }
        }
    },
    "events": [
        {
            "type": "driveOpen",
            "startMs": 150,
            "durationMs": 234,
            "success": true,
            "metadata": "/dev/sdb"
        }
    ],
    "histograms": {
        "download": [ ... ],
        "decompress": [ ... ],
        "write": [
            [0, 28672, 45056, 35000, 0, 0, 2, 8, 5, 1, 0, 0, 0, 0, 0, 0],
            [1000, 30720, 43008, 36500, 0, 0, 1, 9, 6, 0, 0, 0, 0, 0, 0, 0]
        ],
        "verify": [ ... ]
    },
    "schema": {
        "histogramSliceFormat": [
            "timestampMs", "minKBps", "maxKBps", "avgKBps",
            "bucket_0-1MB", "bucket_1-2MB", "bucket_2-4MB", "bucket_4-8MB",
            "bucket_8-16MB", "bucket_16-32MB", "bucket_32-64MB", "bucket_64-128MB",
            "bucket_128-256MB", "bucket_256-512MB", "bucket_512-1024MB", "bucket_1024+MB"
        ],
        "histogramWindowMs": 1000,
        "throughputUnit": "KB/s"
    }
}
```

## Analysing the Data

### Using the Provided Script

A Python script is provided to parse and visualise performance data:

```bash
cd doc/performance
python analyse_performance.py /path/to/performance-data.json --save-graphs
```

This will produce:
- **Text summary** — Operation overview, event timing breakdown, throughput statistics
- **Timeline graph** (`-timeline.png`) — Gantt chart showing the sequence of all operations
- **Throughput graph** (`-throughput.png`) — Throughput over time with event markers overlaid
- **Network graph** (`-network.png`) — Network and cache operations with their throughput

The event overlays on the throughput graph are particularly useful — you can see exactly when operations like `pageCacheFlush` or `customisation` occurred and how they affected throughput.

Requirements:
```bash
pip install matplotlib numpy
```

### Interpreting the Results

**Healthy write operation:**
- Consistent throughput throughout each phase
- Event durations in expected ranges (drive open < 5s, customisation < 30s)
- No significant gaps in the histogram timeline

**Signs of trouble:**
- Large gaps in histogram data (indicates stalls)
- High variance in throughput (histogram spread across many buckets)
- Unexpectedly long event durations
- Many samples in the lowest throughput buckets

**Common issues:**

| Symptom | Likely Cause |
|---------|--------------|
| Low decompress throughput | Single-threaded decompression (gzip/zstd) or slow CPU |
| Decompress faster than write | I/O-bound; SD card/USB is the bottleneck |
| Write faster than decompress | CPU-bound; consider images with faster compression |
| Low write throughput, high verify throughput | Slow SD card write speed |
| Periodic throughput drops | USB hub or driver issues |
| Long `driveOpen` time | Drive enumeration problems |
| Long `driveUnmountVolumes` time (Windows) | Volume in use by another application |
| Long `driveDiskClean` time (Windows) | Disk locked or antivirus scanning |
| Long `customisation` time | Complex customisation or slow FAT operations |
| Long `finalSync` time | Large page cache, slow drive flush |
| `ringBufferStarvation` with `producer_stall` | Disk/decompression slower than download; ring buffer full |
| `ringBufferStarvation` with `consumer_stall` | Network slower than processing; ring buffer empty |

## Adding Instrumentation

If you're developing Raspberry Pi Imager and want to add timing for additional operations, use the `PerformanceStats` API:

```cpp
// Start timing an event
int eventId = _performanceStats->beginEvent(
    PerformanceStats::EventType::OsListFetch,
    "https://downloads.raspberrypi.com/os_list.json"  // optional metadata
);

// ... perform the operation ...

// End timing (records duration automatically)
_performanceStats->endEvent(eventId, success, "optional extra info");
```

For operations timed elsewhere:
```cpp
_performanceStats->recordEvent(
    PerformanceStats::EventType::HashComputation,
    durationMs,
    true,  // success
    "SHA-256"
);
```

## Reporting Performance Issues

If you're experiencing slow writes or other performance problems, please include the following when opening an issue:

1. **Performance data file** (required) — captured using `Ctrl+Shift+P`
2. **System information** — OS version, RAM, CPU
3. **Storage device details** — make/model of SD card or USB drive
4. **Description** — what you expected vs. what happened

Without a performance data file, we cannot diagnose the issue. "It's slow" doesn't give us anything to work with; the performance data shows us exactly where time is being spent.

Run the analysis script yourself first — it may identify the problem immediately:

```bash
python analyse_performance.py your-performance-data.json
```

## Privacy

Performance data contains:
- The name of the OS image written
- The device path (e.g., `/dev/sdb` or `\\.\PhysicalDrive2`)
- Timing information
- System information (RAM, CPU cores, OS version)
- Target device description (e.g., "USB Flash Drive", size)
- Customisation choices (what was configured, not values)
- Full Imager build info (version, build type, binary SHA256, Qt versions)

It does **not** contain:
- Any image content
- Personal information or unique identifiers
- Network credentials, passwords, or SSIDs
- Hostnames, usernames, or other user-configured values
- Device serial numbers

For customisation events, we log *what* was configured and relevant characteristics, but never actual values:

- `hostname: set` — a hostname was configured (not the actual name)
- `username: set` — a user account was configured (not the username)
- `wifi: secured (WPA2)` or `wifi: open` — network type, not SSID or password
- `ssh: key-auth` or `ssh: password-auth` — auth method, not the actual keys/passwords
- `locale: set`, `timezone: set` — that these were configured

This helps diagnose issues like "key generation is slow" or "WPA3 setup takes longer" without exposing credentials.

The data is stored locally and only exported when you explicitly trigger the export.

