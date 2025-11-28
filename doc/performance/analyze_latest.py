#!/usr/bin/env python3
import json

with open('perf.json') as f:
    data = json.load(f)

print('=== SUMMARY ===')
s = data.get('summary', {})
phases = s.get('phases', {})
for name, phase in phases.items():
    dur = phase.get('durationMs', 0)
    print(f"{name}: {dur/1000:.1f}s")

print()
print('=== NETWORK ===')
net = [e for e in data['events'] if e['type'] == 'networkConnectionStats']
for e in net:
    print(e['metadata'])

print()
print('=== THROUGHPUT ===')
throughput = data.get('throughputHistogram', {})
download = throughput.get('download', {})
write = throughput.get('write', {})
print(f"Download: min={download.get('minKBps',0)/1024:.1f} MB/s, max={download.get('maxKBps',0)/1024:.1f} MB/s, avg={download.get('avgKBps',0)/1024:.1f} MB/s")
print(f"Write: min={write.get('minKBps',0)/1024:.1f} MB/s, max={write.get('maxKBps',0)/1024:.1f} MB/s, avg={write.get('avgKBps',0)/1024:.1f} MB/s")

print()
print('=== RING BUFFER STALLS ===')
stalls = [e for e in data['events'] if e['type'] == 'ringBufferStarvation']
print(f'Total stalls: {len(stalls)}')
producer = [e for e in stalls if 'producer_stall' in e.get('metadata', '')]
consumer = [e for e in stalls if 'consumer_stall' in e.get('metadata', '')]
print(f'Producer stalls: {len(producer)}')
print(f'Consumer stalls: {len(consumer)}')
if stalls:
    total_stall = sum(e['durationMs'] for e in stalls)
    print(f'Total stall time: {total_stall}ms ({total_stall/1000:.1f}s)')

print()
print('=== DRIVE ===')
drive = [e for e in data['events'] if e['type'] == 'driveOpen']
for e in drive:
    print(f"Duration: {e['durationMs']}ms, {e.get('metadata', '')}")

# Direct I/O attempt details
direct_io = [e for e in data['events'] if e['type'] == 'directIOAttempt']
for e in direct_io:
    success = e.get('success', False)
    metadata = e.get('metadata', '')
    print(f"Direct I/O: {'ENABLED' if success else 'DISABLED'}")
    if metadata:
        # Parse metadata to show key fields
        parts = {p.split(':')[0].strip(): p.split(':')[1].strip() for p in metadata.split(';') if ':' in p}
        print(f"  attempted: {parts.get('attempted', '?')}")
        print(f"  succeeded: {parts.get('succeeded', '?')}")
        print(f"  currently_enabled: {parts.get('currently_enabled', '?')}")
        if parts.get('error_code', '0') != '0':
            print(f"  error_code: {parts.get('error_code', '?')}")
            print(f"  error: {parts.get('error', '?')}")

print()
print('=== PIPELINE TIMING (where time is spent) ===')
decomp = [e for e in data['events'] if e['type'] == 'pipelineDecompressionTime']
write_wait = [e for e in data['events'] if e['type'] == 'pipelineWriteWaitTime']
ring_wait = [e for e in data['events'] if e['type'] == 'pipelineRingBufferWaitTime']

if decomp:
    d = decomp[0]
    print(f"Decompression (total archive_read_data time): {d['durationMs']/1000:.1f}s")
if ring_wait:
    r = ring_wait[0]
    print(f"  - Ring buffer wait (waiting for download): {r['durationMs']/1000:.1f}s")
if decomp and ring_wait:
    actual_decomp = decomp[0]['durationMs'] - ring_wait[0]['durationMs']
    print(f"  - Actual decompression CPU work: {actual_decomp/1000:.1f}s")
if write_wait:
    w = write_wait[0]
    print(f"Write wait (blocked on disk writes): {w['durationMs']/1000:.1f}s")

# Calculate bottleneck
if decomp and write_wait and ring_wait:
    decomp_ms = decomp[0]['durationMs']
    ring_ms = ring_wait[0]['durationMs']
    write_ms = write_wait[0]['durationMs']
    actual_decomp_ms = decomp_ms - ring_ms
    
    print()
    print('=== BOTTLENECK ANALYSIS ===')
    total = decomp_ms + write_ms
    print(f"Time in decompression loop: {decomp_ms/1000:.1f}s")
    print(f"  - Waiting for network data: {ring_ms/1000:.1f}s ({100*ring_ms/decomp_ms:.0f}%)")
    print(f"  - Decompressing: {actual_decomp_ms/1000:.1f}s ({100*actual_decomp_ms/decomp_ms:.0f}%)")
    print(f"Time waiting for disk writes: {write_ms/1000:.1f}s")
    
    if ring_ms > write_ms and ring_ms > actual_decomp_ms:
        print("BOTTLENECK: Network download (decompressor waiting for data)")
    elif write_ms > ring_ms and write_ms > actual_decomp_ms:
        print("BOTTLENECK: Disk writes (decompressor blocked waiting for write to finish)")
    else:
        print("BOTTLENECK: Decompression CPU (decompressor is compute-bound)")

