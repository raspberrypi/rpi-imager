#!/usr/bin/env python3
"""
Raspberry Pi Imager Performance Data Analyser

Parses and visualises performance data captured by Raspberry Pi Imager.
Run with: python analyse_performance.py <performance-data.json>

Requirements: pip install matplotlib numpy
"""

import json
import sys
import io
import base64
import html
from datetime import datetime
from pathlib import Path

# Fix Windows console encoding for Unicode characters
if sys.platform == 'win32':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

# Optional imports for visualisation
try:
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


def format_bytes(num_bytes: int) -> str:
    """Format bytes as human-readable string."""
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if abs(num_bytes) < 1024.0:
            return f"{num_bytes:.1f} {unit}"
        num_bytes /= 1024.0
    return f"{num_bytes:.1f} PB"


def format_duration(ms: int) -> str:
    """Format milliseconds as human-readable duration."""
    if ms < 1000:
        return f"{ms} ms"
    elif ms < 60000:
        return f"{ms / 1000:.1f} s"
    else:
        minutes = ms // 60000
        seconds = (ms % 60000) / 1000
        return f"{minutes}m {seconds:.0f}s"


def format_throughput(kbps: int) -> str:
    """Format KB/s as human-readable throughput."""
    if kbps < 1024:
        return f"{kbps} KB/s"
    elif kbps < 1024 * 1024:
        return f"{kbps / 1024:.1f} MB/s"
    else:
        return f"{kbps / (1024 * 1024):.2f} GB/s"


# =============================================================================
# SESSION STATE INFERENCE
# =============================================================================

# Events that are recorded even without an imaging session (background/startup events)
BACKGROUND_EVENT_TYPES = {
    'osListFetch', 'osListParse', 'sublistFetch', 'networkConnectionStats',
    'networkLatency', 'networkRetry', 'driveListPoll', 'fileDialogOpen'
}

def infer_session_state(data: dict) -> str:
    """
    Infer the session state from performance data.
    
    For newer traces (v3+), uses the sessionState field directly.
    For older traces, infers the state from available data:
    - 'never_started': No imaging session was initiated (only background events)
    - 'succeeded': Session completed successfully
    - 'failed': Session failed with an error
    - 'cancelled': Session was cancelled (detected from error message)
    - 'in_progress': Session was still running when exported
    
    Returns the inferred session state string.
    """
    summary = data.get('summary', {})
    
    # If sessionState is present, use it directly
    session_state = summary.get('sessionState', '')
    if session_state:
        return session_state
    
    # Infer from older traces
    events = data.get('events', [])
    histograms = data.get('histograms', {})
    phases = summary.get('phases', {})
    
    # Check if there are any CycleStart events
    has_cycle_start = any(e.get('type') == 'cycleStart' for e in events)
    
    # Check if sessionStartTime is non-zero
    session_start_time = summary.get('sessionStartTime', 0)
    
    # Check if there's any imaging data (throughput samples)
    has_imaging_data = False
    for phase in ['download', 'decompress', 'write', 'verify']:
        phase_data = phases.get(phase, {})
        if phase_data.get('sampleCount', 0) > 0:
            has_imaging_data = True
            break
        if histograms.get(phase):
            has_imaging_data = True
            break
    
    # Check what types of events we have
    event_types = {e.get('type') for e in events}
    has_only_background_events = event_types.issubset(BACKGROUND_EVENT_TYPES)
    
    # Infer state based on available data
    if not has_cycle_start and session_start_time == 0 and not has_imaging_data:
        # No session was ever started
        if has_only_background_events or not events:
            return 'never_started'
    
    # Check for explicit success/failure
    success = summary.get('success', False)
    error_message = summary.get('errorMessage', '').lower()
    
    if success:
        return 'succeeded'
    
    # Check for cancellation indicators in error message
    if error_message:
        if 'cancel' in error_message or 'removed' in error_message or 'user' in error_message:
            return 'cancelled'
        return 'failed'
    
    # Check for CycleEnd events
    cycle_end_events = [e for e in events if e.get('type') == 'cycleEnd']
    if cycle_end_events:
        last_cycle_end = cycle_end_events[-1]
        if last_cycle_end.get('success', False):
            return 'succeeded'
        metadata = str(last_cycle_end.get('metadata', '')).lower()
        if 'cancel' in metadata or 'removed' in metadata:
            return 'cancelled'
        return 'failed'
    
    # If we have imaging data but no completion, it's in progress
    if has_imaging_data or has_cycle_start:
        return 'in_progress'
    
    # Default fallback - treat as never started if no evidence of imaging
    return 'never_started'


# =============================================================================
# IMAGING CYCLE DETECTION
# =============================================================================

class ImagingCycle:
    """Represents a single select-download-write cycle."""
    
    # Events that indicate a cycle completed successfully
    COMPLETION_EVENTS = {'deviceClose', 'finalSync', 'verifyComplete'}
    
    # Events that indicate a cycle failed
    FAILURE_EVENTS = {'writeError', 'verifyError', 'downloadError', 'cancelled', 'hardTimeout'}
    
    def __init__(self, cycle_id: int):
        self.id = cycle_id
        self.start_ms: int = 0
        self.end_ms: int = 0
        self.events: list = []
        self.histogram_slices: dict = {'download': [], 'decompress': [], 'write': [], 'verify': []}
        
        # Derived stats
        self.image_name: str = ''
        self.device_name: str = ''
        self.cache_hit: bool = False
        self.direct_io: bool = False
        self.success: bool = True
        self.is_complete: bool = False  # True if we saw completion events
        self.is_final_cycle: bool = False  # True if this is the last cycle in the capture
        # Completion reasons: 'completed', 'failed', 'cancelled', 'in_progress', 'exported_early', 'never_started'
        self.completion_reason: str = ''
        
    @property
    def duration_ms(self) -> int:
        return self.end_ms - self.start_ms if self.end_ms > self.start_ms else 0
    
    def get_phase_stats(self, phase: str) -> dict:
        """Calculate throughput stats for a phase from histogram slices."""
        slices = self.histogram_slices.get(phase, [])
        if not slices:
            return {}
        
        throughputs = [s[3] for s in slices]  # avg throughput is index 3
        timestamps = [s[0] for s in slices]
        
        return {
            'sampleCount': len(slices),
            'minThroughputKBps': min(throughputs),
            'maxThroughputKBps': max(throughputs),
            'avgThroughputKBps': sum(throughputs) // len(throughputs),
            'startMs': min(timestamps),
            'endMs': max(timestamps),
            'durationMs': max(timestamps) - min(timestamps) + 1000,  # Add window duration
        }
    
    def get_bottleneck(self) -> str:
        """Determine the bottleneck for this cycle."""
        # Look for pipeline timing events
        decomp_time = 0
        write_wait = 0
        ring_wait = 0
        
        for event in self.events:
            if event.get('type') == 'pipelineDecompressionTime':
                decomp_time = event.get('durationMs', 0)
            elif event.get('type') == 'pipelineWriteWaitTime':
                write_wait = event.get('durationMs', 0)
            elif event.get('type') == 'pipelineRingBufferWaitTime':
                ring_wait = event.get('durationMs', 0)
        
        if decomp_time == 0:
            return 'unknown'
        
        actual_decomp = decomp_time - ring_wait
        
        if ring_wait > write_wait and ring_wait > actual_decomp:
            return 'network'
        elif write_wait > ring_wait and write_wait > actual_decomp:
            return 'disk'
        else:
            return 'cpu'


def detect_cycles(data: dict, gap_threshold_ms: int = 30000) -> list:
    """
    Detect imaging cycles from performance data.
    
    Detection methods (in order of preference):
    1. Explicit cycleStart/cycleEnd events (new format)
    2. Large gaps (>gap_threshold_ms) in histogram data (legacy fallback)
    
    Returns list of ImagingCycle objects.
    """
    events = data.get('events', [])
    histograms = data.get('histograms', {})
    
    # First, try to detect cycles from explicit cycleStart/cycleEnd events
    cycles = _detect_cycles_from_events(events, histograms)
    if cycles:
        return cycles
    
    # Fallback: detect cycles from gaps in histogram data (for older captures)
    return _detect_cycles_from_gaps(data, events, histograms, gap_threshold_ms)


def _detect_cycles_from_events(events: list, histograms: dict) -> list:
    """Detect cycles from explicit cycleStart/cycleEnd events."""
    cycles = []
    current_cycle = None
    
    for event in events:
        event_type = event.get('type', '')
        
        if event_type == 'cycleStart':
            # Start a new cycle
            if current_cycle is not None:
                # Previous cycle wasn't ended - mark it as incomplete
                current_cycle.is_complete = False
                current_cycle.completion_reason = 'no_end_event'
                cycles.append(current_cycle)
            
            current_cycle = ImagingCycle(len(cycles) + 1)
            current_cycle.start_ms = event.get('startMs', 0)
            current_cycle.events.append(event)
            
            # Parse metadata for image/device info
            metadata = event.get('metadata', '')
            if 'image:' in metadata:
                parts = {p.split(':')[0].strip(): p.split(':', 1)[1].strip() 
                        for p in metadata.split(';') if ':' in p}
                current_cycle.image_name = parts.get('image', '')
                current_cycle.device_name = parts.get('device', '')
                
        elif event_type == 'cycleEnd':
            if current_cycle is not None:
                current_cycle.end_ms = event.get('startMs', 0)
                current_cycle.events.append(event)
                current_cycle.is_complete = True
                current_cycle.success = event.get('success', False)
                current_cycle.completion_reason = 'completed' if current_cycle.success else 'failed'
                cycles.append(current_cycle)
                current_cycle = None
        else:
            # Add other events to current cycle
            if current_cycle is not None:
                current_cycle.events.append(event)
                _update_cycle_from_event(current_cycle, event)
    
    # Handle incomplete final cycle
    if current_cycle is not None:
        current_cycle.is_complete = False
        current_cycle.is_final_cycle = True
        current_cycle.completion_reason = 'in_progress'
        cycles.append(current_cycle)
    
    # Assign histogram data to cycles based on timestamps
    if cycles:
        _assign_histogram_to_cycles(cycles, histograms)
        
        # Mark the final cycle
        if cycles:
            cycles[-1].is_final_cycle = True
    
    return cycles


def _assign_histogram_to_cycles(cycles: list, histograms: dict) -> None:
    """Assign histogram slices to cycles based on timestamp ranges."""
    for phase, slices in histograms.items():
        for slice_data in slices:
            timestamp = slice_data[0]
            
            # Find the cycle this slice belongs to
            for i, cycle in enumerate(cycles):
                cycle_end = cycles[i + 1].start_ms if i + 1 < len(cycles) else float('inf')
                
                if cycle.start_ms <= timestamp < cycle_end:
                    cycle.histogram_slices[phase].append(slice_data)
                    cycle.end_ms = max(cycle.end_ms, timestamp)
                    break


def _detect_cycles_from_gaps(data: dict, events: list, histograms: dict, gap_threshold_ms: int) -> list:
    """Legacy: Detect cycles from gaps in histogram data."""
    
    # First, detect gaps in histogram data which are the clearest cycle boundaries
    # Combine all histogram timestamps to find the gaps
    all_histogram_points = []
    for phase, slices in histograms.items():
        for slice_data in slices:
            timestamp = slice_data[0]
            all_histogram_points.append({
                'timestamp': timestamp,
                'phase': phase,
                'slice': slice_data
            })
    
    all_histogram_points.sort(key=lambda x: x['timestamp'])
    
    # Find gaps in histogram data
    cycle_boundaries = [0]  # Start of first cycle
    if all_histogram_points:
        prev_ts = all_histogram_points[0]['timestamp']
        for point in all_histogram_points[1:]:
            gap = point['timestamp'] - prev_ts
            if gap > gap_threshold_ms:
                # Found a gap - this is a cycle boundary
                cycle_boundaries.append(point['timestamp'])
            prev_ts = point['timestamp']
    
    if not cycle_boundaries or not all_histogram_points:
        # No clear boundaries found, treat everything as one cycle
        if all_histogram_points:
            cycle_boundaries = [all_histogram_points[0]['timestamp']]
    
    # Create cycles based on boundaries
    cycles = []
    for i, boundary_start in enumerate(cycle_boundaries):
        cycle = ImagingCycle(i + 1)
        cycle.start_ms = boundary_start
        
        # Find end of this cycle (start of next, or last data point)
        if i + 1 < len(cycle_boundaries):
            cycle_end = cycle_boundaries[i + 1]
        else:
            cycle_end = float('inf')
        
        # Assign histogram data to this cycle
        for point in all_histogram_points:
            ts = point['timestamp']
            if boundary_start <= ts < cycle_end:
                cycle.histogram_slices[point['phase']].append(point['slice'])
                cycle.end_ms = max(cycle.end_ms, ts)
        
        cycles.append(cycle)
    
    # Now assign events to cycles based on their timestamps
    for i, event in enumerate(events):
        start_ms = event.get('startMs', 0)
        event_type = event.get('type', '')
        
        # Find which cycle this event belongs to
        target_cycle = None
        for j, cycle in enumerate(cycles):
            cycle_end = cycles[j + 1].start_ms if j + 1 < len(cycles) else float('inf')
            
            # Events with startMs=0 need special handling
            if start_ms == 0:
                continue  # Will be handled by _assign_zero_timestamp_events
            
            if cycle.start_ms - 15000 <= start_ms < cycle_end:
                target_cycle = cycle
                break
        
        if target_cycle is not None:
            target_cycle.events.append(event)
            _update_cycle_from_event(target_cycle, event)
    
    # Post-process: assign events without timestamps to cycles based on file position
    # Events with startMs=0 are ordered in the file, so we can use their position
    _assign_zero_timestamp_events(cycles, events)
    
    # Determine completion status of each cycle
    _determine_cycle_completion(cycles, data)
    
    return cycles


def _assign_zero_timestamp_events(cycles: list, all_events: list) -> None:
    """
    Assign events with startMs=0 to cycles.
    
    This is tricky because events may not be strictly ordered by time in the file.
    We use a heuristic: look at the NEXT event with a real timestamp after this one
    in the file, and use that to determine which cycle this event belongs to.
    
    Special handling for key events like cacheLookup which mark cycle boundaries.
    """
    if not cycles or len(cycles) < 2:
        # For single cycle, assign everything to it
        if cycles:
            for event in all_events:
                if event not in cycles[0].events:
                    cycles[0].events.append(event)
                    _update_cycle_from_event(cycles[0], event)
        return
    
    # Build a map of position -> next timestamp after that position
    next_timestamp_after = {}
    last_ts = 0
    # Scan backwards to find the next timestamp for each position
    for i in range(len(all_events) - 1, -1, -1):
        ts = all_events[i].get('startMs', 0)
        if ts > 0:
            last_ts = ts
        next_timestamp_after[i] = last_ts
    
    # Also build a map of cycle start times for quick lookup
    cycle_ranges = []
    for i, cycle in enumerate(cycles):
        start = cycle.start_ms
        end = cycles[i + 1].start_ms if i + 1 < len(cycles) else float('inf')
        cycle_ranges.append((start, end, cycle))
    
    # Process events with startMs=0
    for i, event in enumerate(all_events):
        if event.get('startMs', 0) > 0:
            continue  # Has real timestamp
        
        if any(event in c.events for c in cycles):
            continue  # Already assigned
        
        event_type = event.get('type', '')
        
        # Special handling for cacheLookup - it marks the START of a cycle's actual imaging
        # Look at events AFTER this one to determine which cycle
        next_ts = next_timestamp_after.get(i, 0)
        
        # Also look at the PREVIOUS event with a timestamp to bracket
        prev_ts = 0
        for j in range(i - 1, -1, -1):
            ts = all_events[j].get('startMs', 0)
            if ts > 0:
                prev_ts = ts
                break
        
        # If next_ts is very early (< 30s) but this is late in the file (position > 300),
        # it means events are out of order and we're in a later cycle
        is_late_in_file = i > len(all_events) * 0.5
        next_is_early = next_ts < 30000
        
        if is_late_in_file and next_is_early and event_type == 'cacheLookup':
            # This cacheLookup is followed by events from cycle 1 in the file,
            # but it's actually for a later cycle. Assign to last cycle.
            last_cycle = cycles[-1]
            last_cycle.events.append(event)
            _update_cycle_from_event(last_cycle, event)
            continue
        
        # General case: use the next timestamp to find the cycle
        target_cycle = None
        for start, end, cycle in cycle_ranges:
            # Be generous with boundaries - preparation events may occur before histogram starts
            if start - 30000 <= next_ts < end + 30000:
                target_cycle = cycle
                break
        
        if target_cycle is None:
            # Fallback: assign to the cycle whose time range best matches
            for start, end, cycle in cycle_ranges:
                if start - 30000 <= prev_ts < end + 30000:
                    target_cycle = cycle
                    break
        
        if target_cycle is None and cycles:
            # Last resort: assign to first cycle
            target_cycle = cycles[0]
        
        if target_cycle:
            target_cycle.events.append(event)
            _update_cycle_from_event(target_cycle, event)


def _update_cycle_from_event(cycle: 'ImagingCycle', event: dict) -> None:
    """Update cycle metadata based on event information."""
    event_type = event.get('type', '')
    metadata = event.get('metadata', '')
    
    if event_type == 'cacheLookup':
        cycle.cache_hit = 'hit' in metadata
    elif event_type == 'directIOAttempt':
        cycle.direct_io = 'succeeded: yes' in metadata
    elif event_type == 'driveOpen':
        if 'direct_io: yes' in metadata:
            cycle.direct_io = True


def _determine_cycle_completion(cycles: list, data: dict) -> None:
    """Determine whether each cycle completed, failed, cancelled, or is still in progress."""
    if not cycles:
        return
    
    # Infer session state (works for both new and old traces)
    session_state = infer_session_state(data)
    
    for i, cycle in enumerate(cycles):
        cycle.is_final_cycle = (i == len(cycles) - 1)
        
        # Check for completion events
        event_types = {e.get('type') for e in cycle.events}
        has_completion = bool(event_types & ImagingCycle.COMPLETION_EVENTS)
        has_failure = bool(event_types & ImagingCycle.FAILURE_EVENTS)
        
        # Check for failed events
        failed_events = [e for e in cycle.events if not e.get('success', True)]
        
        # Check for cancellation indicators in cycleEnd metadata
        cycle_end_events = [e for e in cycle.events if e.get('type') == 'cycleEnd']
        is_cancelled = any('cancel' in str(e.get('metadata', '')).lower() or 
                          'removed' in str(e.get('metadata', '')).lower()
                          for e in cycle_end_events)
        
        if is_cancelled:
            cycle.is_complete = True
            cycle.success = False
            cycle.completion_reason = 'cancelled'
        elif has_failure or failed_events:
            cycle.is_complete = True
            cycle.success = False
            cycle.completion_reason = 'failed'
        elif has_completion:
            cycle.is_complete = True
            cycle.success = True
            cycle.completion_reason = 'completed'
        elif cycle.is_final_cycle:
            # Final cycle without completion events - could be in progress or exported early
            # Check if there's recent histogram activity
            all_slices = []
            for phase_slices in cycle.histogram_slices.values():
                all_slices.extend(phase_slices)
            
            if all_slices:
                last_activity = max(s[0] for s in all_slices)
                # Use inferred session state for more accurate status
                if session_state == 'cancelled':
                    cycle.is_complete = True
                    cycle.success = False
                    cycle.completion_reason = 'cancelled'
                elif session_state in ('failed', 'never_started'):
                    cycle.is_complete = False
                    cycle.success = False
                    cycle.completion_reason = 'exported_early'
                elif session_state == 'in_progress':
                    cycle.is_complete = False
                    cycle.completion_reason = 'in_progress'
                else:
                    # Has activity but no completion event - likely exported mid-operation
                    cycle.is_complete = False
                    cycle.completion_reason = 'in_progress'
            else:
                cycle.is_complete = False
                cycle.completion_reason = 'in_progress'
        else:
            # Non-final cycle without explicit completion - assume it completed
            # (otherwise how would the next cycle have started?)
            cycle.is_complete = True
            cycle.success = True
            cycle.completion_reason = 'completed'


def _assign_contextual_events(cycles: list, all_events: list) -> None:
    """Assign events without timestamps to cycles based on context."""
    if not cycles:
        return
    
    # Events that should be assigned to the cycle they occur within
    contextual_events = {
        'pipelineDecompressionTime', 'pipelineWriteWaitTime', 'pipelineRingBufferWaitTime',
        'osListParse', 'osListFetch', 'customisation', 'cloudInitGeneration',
        'firstRunGeneration', 'secureBootSetup', 'finalSync', 'deviceClose',
        'cacheLookup', 'imageExtraction'
    }
    
    # Build ordered list of events by their position in the file (proxy for time order)
    event_positions = {id(e): i for i, e in enumerate(all_events)}
    
    # For each cycle, find the position range of its events
    for cycle in cycles:
        if not cycle.events:
            continue
        
        cycle_positions = [event_positions.get(id(e), 0) for e in cycle.events]
        min_pos = min(cycle_positions) if cycle_positions else 0
        max_pos = max(cycle_positions) if cycle_positions else len(all_events)
        
        # Find contextual events that fall within this range
        for i, event in enumerate(all_events):
            event_type = event.get('type', '')
            if event_type in contextual_events:
                if min_pos - 10 <= i <= max_pos + 10:  # Allow some slack
                    if event not in cycle.events:
                        cycle.events.append(event)
                        
                        # Update cycle metadata based on event
                        if event_type == 'cacheLookup':
                            cycle.cache_hit = 'hit' in event.get('metadata', '')
                        elif event_type == 'directIOAttempt':
                            cycle.direct_io = 'succeeded: yes' in event.get('metadata', '')


def print_cycles(cycles: list) -> None:
    """Print summary of detected imaging cycles."""
    if not cycles:
        print("\n[No imaging cycles detected]")
        return
    
    print("\n" + "=" * 70)
    print(f"IMAGING CYCLES DETECTED: {len(cycles)}")
    print("=" * 70)
    
    for cycle in cycles:
        print(f"\n{'─' * 70}")
        
        # Cycle header with status indicator
        status_indicators = {
            'completed': '✓',
            'failed': '✗',
            'cancelled': '⊘',
            'in_progress': '⋯',
            'exported_early': '⚠',
            'never_started': '○'
        }
        status_labels = {
            'completed': 'COMPLETED',
            'failed': 'FAILED',
            'cancelled': 'CANCELLED',
            'in_progress': 'IN PROGRESS (capture exported mid-cycle)',
            'exported_early': 'INCOMPLETE (capture exported early)',
            'never_started': 'NEVER STARTED'
        }
        indicator = status_indicators.get(cycle.completion_reason, '?')
        status_label = status_labels.get(cycle.completion_reason, 'UNKNOWN')
        
        print(f"CYCLE {cycle.id}  [{indicator} {status_label}]")
        print(f"{'─' * 70}")
        
        # Time range
        start_s = cycle.start_ms / 1000
        end_s = cycle.end_ms / 1000
        duration = format_duration(cycle.duration_ms)
        print(f"  Time:      {start_s:.1f}s - {end_s:.1f}s ({duration})")
        
        # Cache and I/O status
        cache_status = "✓ Cache hit" if cycle.cache_hit else "✗ Cache miss (downloaded)"
        io_status = "✓ Direct I/O" if cycle.direct_io else "✗ Buffered I/O"
        print(f"  Cache:     {cache_status}")
        print(f"  I/O Mode:  {io_status}")
        
        # Phase throughput
        print(f"\n  Phase Throughput:")
        for phase in ['download', 'decompress', 'write', 'verify']:
            stats = cycle.get_phase_stats(phase)
            if stats:
                avg = format_throughput(stats['avgThroughputKBps'])
                min_tp = format_throughput(stats['minThroughputKBps'])
                max_tp = format_throughput(stats['maxThroughputKBps'])
                dur = format_duration(stats['durationMs'])
                samples = stats['sampleCount']
                print(f"    {phase.upper():<12} {avg:>12} avg  ({min_tp} - {max_tp})  [{samples} samples, {dur}]")
        
        # Bottleneck
        bottleneck = cycle.get_bottleneck()
        bottleneck_labels = {
            'network': '⚠ Network (download too slow)',
            'disk': '⚠ Disk (writes too slow)',
            'cpu': '⚠ CPU (decompression bound)',
            'unknown': '? Unknown (insufficient data)'
        }
        print(f"\n  Bottleneck: {bottleneck_labels.get(bottleneck, bottleneck)}")
        
        # Key events
        key_event_types = {
            'driveOpen', 'driveUnmount', 'cacheLookup', 'directIOAttempt',
            'pipelineDecompressionTime', 'pipelineWriteWaitTime', 'finalSync', 'deviceClose',
            # Recovery events
            'queueDepthReduction', 'syncFallbackActivated', 'drainAndHotSwap', 
            'watchdogRecovery', 'progressStall', 'deviceIOTimeout'
        }
        key_events = [e for e in cycle.events if e.get('type') in key_event_types]
        if key_events:
            print(f"\n  Key Events:")
            for event in key_events:
                dur = format_duration(event.get('durationMs', 0))
                meta = event.get('metadata', '')[:40]
                print(f"    • {event['type']}: {dur}" + (f" ({meta})" if meta else ""))


def get_cycle_comparison_table(cycles: list) -> str:
    """Generate a comparison table for multiple cycles."""
    if len(cycles) < 2:
        return ""
    
    lines = [
        "\n" + "=" * 70,
        "CYCLE COMPARISON",
        "=" * 70,
        "",
        f"{'Metric':<25}" + "".join(f"{'Cycle ' + str(c.id):>15}" for c in cycles),
        "-" * (25 + 15 * len(cycles))
    ]
    
    # Status
    row = f"{'Status':<25}"
    for c in cycles:
        status_short = {
            'completed': 'Complete',
            'failed': 'Failed',
            'cancelled': 'Cancelled',
            'in_progress': 'In Progress',
            'exported_early': 'Incomplete',
            'never_started': 'Not Started'
        }
        row += f"{status_short.get(c.completion_reason, '?'):>15}"
    lines.append(row)
    
    # Duration
    row = f"{'Duration':<25}"
    for c in cycles:
        dur = format_duration(c.duration_ms) if c.duration_ms > 0 else 'N/A'
        row += f"{dur:>15}"
    lines.append(row)
    
    # Cache status
    row = f"{'Cache':<25}"
    for c in cycles:
        status = "Hit" if c.cache_hit else "Miss"
        row += f"{status:>15}"
    lines.append(row)
    
    # Direct I/O
    row = f"{'Direct I/O':<25}"
    for c in cycles:
        status = "Yes" if c.direct_io else "No"
        row += f"{status:>15}"
    lines.append(row)
    
    # Write throughput
    row = f"{'Write Avg Throughput':<25}"
    for c in cycles:
        stats = c.get_phase_stats('write')
        if stats:
            row += f"{format_throughput(stats['avgThroughputKBps']):>15}"
        else:
            row += f"{'N/A':>15}"
    lines.append(row)
    
    # Write throughput range
    row = f"{'Write Range':<25}"
    for c in cycles:
        stats = c.get_phase_stats('write')
        if stats:
            range_str = f"{stats['minThroughputKBps']//1024}-{stats['maxThroughputKBps']//1024} MB/s"
            row += f"{range_str:>15}"
        else:
            row += f"{'N/A':>15}"
    lines.append(row)
    
    # Bottleneck
    row = f"{'Bottleneck':<25}"
    for c in cycles:
        row += f"{c.get_bottleneck().upper():>15}"
    lines.append(row)
    
    return "\n".join(lines)


def print_summary(data: dict) -> None:
    """Print a summary of the performance data."""
    summary = data.get('summary', {})
    
    print("\n" + "=" * 60)
    print("PERFORMANCE DATA SUMMARY")
    print("=" * 60)
    
    # Basic info
    print(f"\nImage:    {summary.get('imageName', 'Unknown')}")
    print(f"Device:   {summary.get('deviceName', 'Unknown')}")
    print(f"Size:     {format_bytes(summary.get('imageSize', 0))}")
    print(f"Duration: {format_duration(summary.get('durationMs', 0))}")
    
    # Infer session state (works for both new and old traces)
    session_state = infer_session_state(data)
    state_labels = {
        'succeeded': '[OK] Success',
        'failed': '[FAIL] Failed',
        'cancelled': '[CANCEL] Cancelled by user',
        'in_progress': '[...] In progress',
        'never_started': '[--] No imaging session started'
    }
    result = state_labels.get(session_state, f'[?] {session_state}')
    print(f"Result:   {result}")
    
    if summary.get('errorMessage'):
        print(f"Error:    {summary['errorMessage']}")
    
    # Export time
    export_time = data.get('exportTime', '')
    if export_time:
        print(f"Exported: {export_time}")


def print_events(data: dict) -> None:
    """Print event timing breakdown."""
    events = data.get('events', [])
    event_summary = data.get('summary', {}).get('events', {})
    
    if not events and not event_summary:
        return
    
    print("\n" + "-" * 60)
    print("EVENT TIMING")
    print("-" * 60)
    
    # Use summary if available (aggregated stats)
    if event_summary:
        print(f"\n{'Event':<25} {'Count':>6} {'Total':>10} {'Avg':>10} {'Min':>10} {'Max':>10}")
        print("-" * 73)
        
        for event_type, stats in sorted(event_summary.items()):
            count = stats.get('count', 0)
            total = format_duration(stats.get('totalMs', 0))
            avg = format_duration(stats.get('avgMs', 0))
            min_val = format_duration(stats.get('minMs', 0))
            max_val = format_duration(stats.get('maxMs', 0))
            print(f"{event_type:<25} {count:>6} {total:>10} {avg:>10} {min_val:>10} {max_val:>10}")
    
    # Also show individual events if present
    if events:
        print(f"\n{'Event':<25} {'Start':>10} {'Duration':>10} {'Status':>8}")
        print("-" * 55)
        
        for event in events:
            event_type = event.get('type', 'unknown')
            start = format_duration(event.get('startMs', 0))
            duration = format_duration(event.get('durationMs', 0))
            status = 'OK' if event.get('success', True) else 'FAIL'
            print(f"{event_type:<25} {start:>10} {duration:>10} {status:>8}")
            
            if event.get('metadata'):
                print(f"  └─ {event['metadata']}")


def print_phase_stats(data: dict) -> None:
    """Print throughput statistics for each phase."""
    phases = data.get('summary', {}).get('phases', {})
    
    if not phases:
        return
    
    print("\n" + "-" * 60)
    print("PHASE THROUGHPUT")
    print("-" * 60)
    
    for phase_name in ['download', 'write', 'verify']:
        phase = phases.get(phase_name, {})
        if not phase or phase.get('sampleCount', 0) == 0:
            continue
        
        print(f"\n{phase_name.upper()}")
        print(f"  Duration:   {format_duration(phase.get('durationMs', 0))}")
        print(f"  Data:       {format_bytes(phase.get('bytesTotal', 0))}")
        print(f"  Samples:    {phase.get('sampleCount', 0)}")
        print(f"  Throughput: {format_throughput(phase.get('avgThroughputKBps', 0))} avg")
        print(f"              {format_throughput(phase.get('minThroughputKBps', 0))} min")
        print(f"              {format_throughput(phase.get('maxThroughputKBps', 0))} max")


def analyse_histograms(data: dict) -> dict:
    """Analyse histogram data for anomalies."""
    histograms = data.get('histograms', {})
    analysis = {}
    
    for phase_name, slices in histograms.items():
        if not slices:
            continue
        
        phase_analysis = {
            'slice_count': len(slices),
            'gaps': [],
            'stalls': [],
            'throughput_samples': []
        }
        
        prev_timestamp = None
        window_ms = data.get('schema', {}).get('histogramWindowMs', 1000)
        
        for slice_data in slices:
            timestamp = slice_data[0]
            min_kbps = slice_data[1]
            max_kbps = slice_data[2]
            avg_kbps = slice_data[3]
            
            phase_analysis['throughput_samples'].append({
                'timestamp': timestamp,
                'min': min_kbps,
                'max': max_kbps,
                'avg': avg_kbps
            })
            
            # Check for gaps (missing data)
            if prev_timestamp is not None:
                gap = timestamp - prev_timestamp
                if gap > window_ms * 2:  # More than 2x expected interval
                    phase_analysis['gaps'].append({
                        'start': prev_timestamp,
                        'duration': gap
                    })
            
            # Check for stalls (very low throughput)
            if avg_kbps < 1024:  # Less than 1 MB/s
                phase_analysis['stalls'].append({
                    'timestamp': timestamp,
                    'throughput': avg_kbps
                })
            
            prev_timestamp = timestamp
        
        analysis[phase_name] = phase_analysis
    
    return analysis


def analyse_write_timing_breakdown(data: dict) -> dict:
    """Analyse detailed write timing breakdown for hypothesis testing."""
    events = data.get('events', [])
    
    breakdown = {
        'writeOps': 0,
        'syscallMs': 0,
        'preHashWaitMs': 0,
        'postHashWaitMs': 0,
        'syncMs': 0,
        'syncCount': 0,
        'minWriteSizeKB': 0,
        'maxWriteSizeKB': 0,
        'avgWriteSizeKB': 0,
        'totalBytes': 0,
        'writeCount': 0,
        'beforeSyncKBps': 0,
        'afterSyncKBps': 0,
        'syncImpactPercent': 0,
        'hashWaitPercent': 0,
        'syscallPercent': 0,
    }
    
    for event in events:
        event_type = event.get('type', '')
        metadata = event.get('metadata', '')
        
        if event_type == 'writeTimingBreakdown':
            # Parse: "writeOps: 1234; syscallMs: 5678; preHashWaitMs: 123; postHashWaitMs: 456; syncMs: 789; syncCount: 12"
            parts = {p.split(':')[0].strip(): p.split(':')[1].strip() 
                    for p in metadata.split(';') if ':' in p}
            breakdown['writeOps'] = int(parts.get('writeOps', 0))
            breakdown['syscallMs'] = int(parts.get('syscallMs', 0))
            breakdown['preHashWaitMs'] = int(parts.get('preHashWaitMs', 0))
            breakdown['postHashWaitMs'] = int(parts.get('postHashWaitMs', 0))
            breakdown['syncMs'] = int(parts.get('syncMs', 0))
            breakdown['syncCount'] = int(parts.get('syncCount', 0))
            
        elif event_type == 'writeSizeDistribution':
            # Parse: "minKB: 128; maxKB: 1024; avgKB: 512; totalBytes: 1234567890; count: 1000"
            parts = {p.split(':')[0].strip(): p.split(':')[1].strip() 
                    for p in metadata.split(';') if ':' in p}
            breakdown['minWriteSizeKB'] = int(parts.get('minKB', 0))
            breakdown['maxWriteSizeKB'] = int(parts.get('maxKB', 0))
            breakdown['avgWriteSizeKB'] = int(parts.get('avgKB', 0))
            breakdown['totalBytes'] = int(parts.get('totalBytes', 0))
            breakdown['writeCount'] = int(parts.get('count', 0))
            
        elif event_type == 'writeAfterSyncImpact':
            # Parse: "beforeSyncKBps: 12345; afterSyncKBps: 6789; samples: 100; impactPercent: 45"
            parts = {p.split(':')[0].strip(): p.split(':')[1].strip() 
                    for p in metadata.split(';') if ':' in p}
            breakdown['beforeSyncKBps'] = int(parts.get('beforeSyncKBps', 0))
            breakdown['afterSyncKBps'] = int(parts.get('afterSyncKBps', 0))
            breakdown['syncImpactPercent'] = int(parts.get('impactPercent', 0))
    
    # Calculate percentages
    total_time = (breakdown['syscallMs'] + breakdown['preHashWaitMs'] + 
                  breakdown['postHashWaitMs'] + breakdown['syncMs'])
    if total_time > 0:
        breakdown['syscallPercent'] = int(100 * breakdown['syscallMs'] / total_time)
        breakdown['hashWaitPercent'] = int(100 * (breakdown['preHashWaitMs'] + 
                                                   breakdown['postHashWaitMs']) / total_time)
    
    return breakdown


def print_write_timing_breakdown(breakdown: dict) -> None:
    """Print detailed write timing breakdown analysis."""
    if breakdown['writeOps'] == 0:
        return  # No write timing data available
    
    print("\n" + "-" * 60)
    print("WRITE TIMING BREAKDOWN (Hypothesis Testing)")
    print("-" * 60)
    
    total_time = (breakdown['syscallMs'] + breakdown['preHashWaitMs'] + 
                  breakdown['postHashWaitMs'] + breakdown['syncMs'])
    
    print(f"\n  Write Operations: {breakdown['writeOps']:,}")
    print(f"  Total Time:       {format_duration(total_time)}")
    
    # Timing breakdown
    print(f"\n  Time Breakdown:")
    print(f"    Syscall (write()):     {format_duration(breakdown['syscallMs']):>12} ({breakdown['syscallPercent']}%)")
    print(f"    Pre-hash wait:         {format_duration(breakdown['preHashWaitMs']):>12}")
    print(f"    Post-hash wait:        {format_duration(breakdown['postHashWaitMs']):>12}")
    total_hash = breakdown['preHashWaitMs'] + breakdown['postHashWaitMs']
    print(f"    Total hash wait:       {format_duration(total_hash):>12} ({breakdown['hashWaitPercent']}%)")
    print(f"    Sync (fsync):          {format_duration(breakdown['syncMs']):>12} ({breakdown['syncCount']} calls)")
    
    # Hypothesis evaluation
    print(f"\n  Hypothesis Evaluation:")
    
    # Hypothesis 1: Periodic sync overhead
    if breakdown['syncCount'] > 0:
        avg_sync = breakdown['syncMs'] // max(1, breakdown['syncCount'])
        if breakdown['syncMs'] > total_time * 0.1:
            print(f"    H1 (Periodic Sync): ⚠️  Sync took {breakdown['syncMs']/total_time*100:.1f}% of write time ({breakdown['syncCount']} calls, avg {avg_sync}ms)")
        else:
            print(f"    H1 (Periodic Sync): ✓ Sync overhead low ({breakdown['syncMs']/total_time*100:.1f}% of write time)")
    else:
        print(f"    H1 (Periodic Sync): ✓ No syncs performed (likely direct I/O or skipped)")
    
    # Hypothesis 2: Write chunk size
    if breakdown['writeCount'] > 0:
        if breakdown['avgWriteSizeKB'] < 128:
            print(f"    H2 (Chunk Size): ⚠️  Small chunks ({breakdown['avgWriteSizeKB']} KB avg) - may cause syscall overhead")
        else:
            print(f"    H2 (Chunk Size): ✓ Good chunk size ({breakdown['avgWriteSizeKB']} KB avg)")
    
    # Hypothesis 5: Hash computation blocking
    if total_hash > total_time * 0.2:
        print(f"    H5 (Hash Blocking): ⚠️  Hash wait is {breakdown['hashWaitPercent']}% of write time - may be bottleneck")
    else:
        print(f"    H5 (Hash Blocking): ✓ Hash computation not a bottleneck ({breakdown['hashWaitPercent']}%)")
    
    # Hypothesis 7: F_NOCACHE + fsync interaction
    if breakdown['beforeSyncKBps'] > 0 and breakdown['afterSyncKBps'] > 0:
        if breakdown['syncImpactPercent'] > 20:
            print(f"    H7 (Sync Impact): ⚠️  Throughput drops {breakdown['syncImpactPercent']}% after fsync")
            print(f"        Before sync: {format_throughput(breakdown['beforeSyncKBps'])}")
            print(f"        After sync:  {format_throughput(breakdown['afterSyncKBps'])}")
        else:
            print(f"    H7 (Sync Impact): ✓ No significant throughput drop after fsync ({breakdown['syncImpactPercent']}%)")
    
    # Write size distribution
    if breakdown['writeCount'] > 0:
        print(f"\n  Write Size Distribution:")
        print(f"    Min:   {breakdown['minWriteSizeKB']:>8} KB")
        print(f"    Max:   {breakdown['maxWriteSizeKB']:>8} KB")
        print(f"    Avg:   {breakdown['avgWriteSizeKB']:>8} KB")
        print(f"    Total: {format_bytes(breakdown['totalBytes'])} across {breakdown['writeCount']:,} writes")


def analyse_pipeline_timing(data: dict) -> dict:
    """Analyse pipeline timing events to identify bottlenecks and sync overhead."""
    events = data.get('events', [])
    phases = data.get('summary', {}).get('phases', {})
    
    pipeline_analysis = {
        'decompressionTimeMs': 0,
        'writeWaitTimeMs': 0,
        'ringBufferWaitTimeMs': 0,
        'actualDecompressionMs': 0,
        'bottleneck': 'unknown',
        'bottleneckConfidence': 'low',
        'syncOverheadMs': 0,
        'syncOverheadPercent': 0,
        'periodicSyncCount': 0,
        'periodicSyncTotalMs': 0,
        'periodicSyncAvgMs': 0,
        'writeThrottled': False,
        'directIOEnabled': False,
    }
    
    # Extract pipeline timing events
    for event in events:
        event_type = event.get('type', '')
        duration = event.get('durationMs', 0)
        metadata = event.get('metadata', '')
        
        if event_type == 'pipelineDecompressionTime':
            pipeline_analysis['decompressionTimeMs'] = duration
        elif event_type == 'pipelineWriteWaitTime':
            pipeline_analysis['writeWaitTimeMs'] = duration
        elif event_type == 'pipelineRingBufferWaitTime':
            pipeline_analysis['ringBufferWaitTimeMs'] = duration
        elif event_type in ('pageCacheFlush', 'periodicSync'):
            pipeline_analysis['periodicSyncCount'] += 1
            pipeline_analysis['periodicSyncTotalMs'] += duration
        elif event_type == 'directIOAttempt':
            if 'succeeded: yes' in metadata:
                pipeline_analysis['directIOEnabled'] = True
        elif event_type == 'driveOpen':
            if 'direct_io: yes' in metadata:
                pipeline_analysis['directIOEnabled'] = True
    
    # Calculate actual decompression time (excluding ring buffer waits)
    pipeline_analysis['actualDecompressionMs'] = (
        pipeline_analysis['decompressionTimeMs'] - 
        pipeline_analysis['ringBufferWaitTimeMs']
    )
    
    # Calculate average sync time
    if pipeline_analysis['periodicSyncCount'] > 0:
        pipeline_analysis['periodicSyncAvgMs'] = (
            pipeline_analysis['periodicSyncTotalMs'] // 
            pipeline_analysis['periodicSyncCount']
        )
    
    # Determine bottleneck from pipeline timing
    decomp = pipeline_analysis['actualDecompressionMs']
    write_wait = pipeline_analysis['writeWaitTimeMs']
    ring_wait = pipeline_analysis['ringBufferWaitTimeMs']
    
    if decomp > 0 or write_wait > 0 or ring_wait > 0:
        if ring_wait > write_wait and ring_wait > decomp:
            pipeline_analysis['bottleneck'] = 'network'
            pipeline_analysis['bottleneckConfidence'] = 'high'
        elif write_wait > ring_wait and write_wait > decomp:
            pipeline_analysis['bottleneck'] = 'disk'
            pipeline_analysis['bottleneckConfidence'] = 'high'
        elif decomp > ring_wait and decomp > write_wait:
            pipeline_analysis['bottleneck'] = 'cpu'
            pipeline_analysis['bottleneckConfidence'] = 'high'
    
    # Calculate sync overhead by comparing write vs verify throughput
    write_phase = phases.get('write', {})
    verify_phase = phases.get('verify', {})
    
    write_throughput = write_phase.get('avgThroughputKBps', 0)
    verify_throughput = verify_phase.get('avgThroughputKBps', 0)
    write_duration = write_phase.get('durationMs', 0)
    
    # If verify is significantly faster than write (>1.5x), there may be sync overhead
    if verify_throughput > 0 and write_throughput > 0:
        throughput_ratio = verify_throughput / write_throughput
        if throughput_ratio > 1.5:
            pipeline_analysis['writeThrottled'] = True
            # Estimate sync overhead: how much faster could write have been?
            expected_write_duration = write_duration / throughput_ratio
            pipeline_analysis['syncOverheadMs'] = int(write_duration - expected_write_duration)
            pipeline_analysis['syncOverheadPercent'] = int(
                (1 - 1/throughput_ratio) * 100
            )
    
    return pipeline_analysis


def print_system_write_config(data: dict) -> None:
    """Print system write configuration from performance data."""
    system = data.get('system', {})
    write_config = system.get('writeConfig', {})
    buffers = write_config.get('buffers', {})
    
    if not write_config:
        return
    
    print("\n" + "-" * 60)
    print("WRITE CONFIGURATION")
    print("-" * 60)
    
    # Direct I/O and sync settings
    dio = write_config.get('directIOEnabled', False)
    sync_enabled = write_config.get('periodicSyncEnabled', True)
    
    print(f"\n  Direct I/O:      {'✓ Enabled' if dio else '✗ Disabled'}")
    print(f"  Periodic Sync:   {'✓ Enabled' if sync_enabled else '✗ Disabled (skipped with direct I/O)'}")
    
    if sync_enabled:
        sync_bytes = write_config.get('syncIntervalBytes', 0)
        sync_ms = write_config.get('syncIntervalMs', 0)
        print(f"    Sync interval: {format_bytes(sync_bytes)} or {format_duration(sync_ms)}")
    
    # Memory tier
    tier = write_config.get('memoryTier', '')
    if tier:
        print(f"  Memory Tier:     {tier}")
    
    # Buffer sizes
    if buffers:
        print(f"\n  Buffer Configuration:")
        print(f"    Write buffer:  {buffers.get('writeBufferKB', 0):,} KB")
        print(f"    Input buffer:  {buffers.get('inputBufferKB', 0):,} KB")
        print(f"    Input ring:    {buffers.get('inputRingBufferSlots', 0)} slots")
        print(f"    Write ring:    {buffers.get('writeRingBufferSlots', 0)} slots")


def print_pipeline_analysis(pipeline: dict) -> None:
    """Print pipeline bottleneck analysis."""
    print("\n" + "-" * 60)
    print("PIPELINE BOTTLENECK ANALYSIS")
    print("-" * 60)
    
    # Direct I/O status
    dio_status = "✓ Enabled (F_NOCACHE/O_DIRECT)" if pipeline['directIOEnabled'] else "✗ Disabled (buffered I/O)"
    print(f"\n  Direct I/O: {dio_status}")
    
    # Pipeline timing breakdown
    if pipeline['decompressionTimeMs'] > 0:
        print(f"\n  Pipeline Timing:")
        print(f"    Total decompression loop:    {format_duration(pipeline['decompressionTimeMs'])}")
        print(f"      - Waiting for network:     {format_duration(pipeline['ringBufferWaitTimeMs'])} ({100*pipeline['ringBufferWaitTimeMs']//max(1,pipeline['decompressionTimeMs'])}%)")
        print(f"      - CPU decompression work:  {format_duration(pipeline['actualDecompressionMs'])} ({100*pipeline['actualDecompressionMs']//max(1,pipeline['decompressionTimeMs'])}%)")
        print(f"    Waiting for disk writes:     {format_duration(pipeline['writeWaitTimeMs'])}")
    
    # Bottleneck determination
    bottleneck_labels = {
        'network': '🌐 Network (download too slow)',
        'disk': '💾 Disk (writes too slow)', 
        'cpu': '🔥 CPU (decompression bound)',
        'unknown': '❓ Unknown (insufficient data)'
    }
    print(f"\n  Bottleneck: {bottleneck_labels.get(pipeline['bottleneck'], pipeline['bottleneck'])}")
    
    # Periodic sync analysis
    if pipeline['periodicSyncCount'] > 0:
        print(f"\n  Periodic Sync Events:")
        print(f"    Count:       {pipeline['periodicSyncCount']} syncs during write")
        print(f"    Total time:  {format_duration(pipeline['periodicSyncTotalMs'])}")
        print(f"    Average:     {pipeline['periodicSyncAvgMs']} ms per sync")
        
        if pipeline['directIOEnabled'] and pipeline['periodicSyncCount'] > 10:
            print(f"    ⚠️  Many syncs with direct I/O enabled - syncs may be unnecessary")
    
    # Write throttling / sync overhead detection
    if pipeline['writeThrottled']:
        print(f"\n  ⚠️  WRITE THROTTLING DETECTED:")
        print(f"    Verify throughput is significantly faster than write throughput.")
        print(f"    Estimated sync overhead: {format_duration(pipeline['syncOverheadMs'])} ({pipeline['syncOverheadPercent']}% of write time)")
        print(f"    This suggests the disk is capable of faster writes but is being held back.")
        if not pipeline['directIOEnabled']:
            print(f"    Consider enabling direct I/O to bypass page cache.")


def print_recovery_events(events: list) -> None:
    """Print summary of recovery events that occurred during the write."""
    recovery_types = {
        'queueDepthReduction', 'syncFallbackActivated', 'drainAndHotSwap',
        'watchdogRecovery', 'progressStall', 'deviceIOTimeout'
    }
    
    recovery_events = [e for e in events if e.get('type') in recovery_types]
    
    if not recovery_events:
        return
    
    print("\n" + "-" * 60)
    print("RECOVERY EVENTS")
    print("-" * 60)
    
    # Group by type
    by_type = {}
    for event in recovery_events:
        event_type = event.get('type', 'unknown')
        if event_type not in by_type:
            by_type[event_type] = []
        by_type[event_type].append(event)
    
    # Queue depth reductions
    if 'queueDepthReduction' in by_type:
        reductions = by_type['queueDepthReduction']
        print(f"\n  Queue Depth Reductions: {len(reductions)}")
        for event in reductions:
            metadata = event.get('metadata', '')
            print(f"    • {metadata}")
    
    # Stall warnings
    if 'progressStall' in by_type:
        stalls = by_type['progressStall']
        print(f"\n  Progress Stall Warnings: {len(stalls)}")
        max_stall_ms = max(e.get('durationMs', 0) for e in stalls)
        print(f"    Maximum stall duration: {format_duration(max_stall_ms)}")
    
    # Sync fallback
    if 'syncFallbackActivated' in by_type:
        fallbacks = by_type['syncFallbackActivated']
        print(f"\n  ⚠️  Sync Fallback Activated: {len(fallbacks)} time(s)")
        for event in fallbacks:
            metadata = event.get('metadata', '')
            print(f"    Reason: {metadata}")
    
    # Drain and hot-swap
    if 'drainAndHotSwap' in by_type:
        drains = by_type['drainAndHotSwap']
        print(f"\n  Drain & Hot-Swap Attempts: {len(drains)}")
        for event in drains:
            duration_ms = event.get('durationMs', 0)
            success = event.get('success', True)
            metadata = event.get('metadata', '')
            status = "✓ Success" if success else "✗ Failed"
            print(f"    • {status} in {format_duration(duration_ms)} ({metadata})")
    
    # Watchdog recovery
    if 'watchdogRecovery' in by_type:
        watchdog_events = by_type['watchdogRecovery']
        print(f"\n  Watchdog Recovery Actions: {len(watchdog_events)}")
        for event in watchdog_events:
            metadata = event.get('metadata', '')
            success = event.get('success', True)
            status = "✓" if success else "✗"
            print(f"    {status} {metadata}")
    
    # Device I/O timeout
    if 'deviceIOTimeout' in by_type:
        timeouts = by_type['deviceIOTimeout']
        print(f"\n  ⚠️  Device I/O Timeouts: {len(timeouts)}")
        for event in timeouts:
            metadata = event.get('metadata', '')
            print(f"    • {metadata}")
    
    # Summary assessment
    total_recovery = len(recovery_events)
    has_fallback = 'syncFallbackActivated' in by_type or 'watchdogRecovery' in by_type
    
    print(f"\n  Summary:")
    if has_fallback:
        print(f"    ⚠️  Write required recovery intervention - device may be slow or unreliable")
    elif total_recovery > 5:
        print(f"    ℹ️  Multiple queue depth adjustments - device responding slowly")
    elif total_recovery > 0:
        print(f"    ℹ️  Minor recovery events - write completed successfully with adaptation")


def print_analysis(analysis: dict) -> None:
    """Print histogram analysis results."""
    print("\n" + "-" * 60)
    print("ANALYSIS")
    print("-" * 60)
    
    issues_found = False
    
    for phase_name, phase_analysis in analysis.items():
        gaps = phase_analysis.get('gaps', [])
        stalls = phase_analysis.get('stalls', [])
        
        if gaps or stalls:
            issues_found = True
            print(f"\n{phase_name.upper()} Issues:")
            
            if gaps:
                print(f"  [!] {len(gaps)} gap(s) detected (missing data):")
                for gap in gaps[:5]:  # Show first 5
                    print(f"    - {format_duration(gap['duration'])} gap at {format_duration(gap['start'])}")
                if len(gaps) > 5:
                    print(f"    ... and {len(gaps) - 5} more")
            
            if stalls:
                print(f"  [!] {len(stalls)} stall(s) detected (throughput < 1 MB/s):")
                for stall in stalls[:5]:  # Show first 5
                    print(f"    - {format_throughput(stall['throughput'])} at {format_duration(stall['timestamp'])}")
                if len(stalls) > 5:
                    print(f"    ... and {len(stalls) - 5} more")
    
    if not issues_found:
        print("\n[OK] No significant issues detected")


def plot_network(data: dict, output_path: Path = None) -> None:
    """Plot network operations with their throughput as a bar chart over time."""
    if not HAS_MATPLOTLIB:
        return
    
    events = data.get('events', [])
    
    # Filter to network-related events with bytes transferred
    network_types = {'osListFetch', 'osListParse', 'sublistFetch', 'networkLatency',
                    'cacheLookup', 'cacheVerification', 'cacheWrite', 'cacheFlush'}
    
    network_events = [e for e in events 
                     if e.get('type') in network_types or e.get('bytesTransferred', 0) > 0]
    
    if not network_events:
        return
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8))
    
    # Colours by event type
    colours = {
        'osListFetch': '#2196F3', 'osListParse': '#1976D2', 
        'sublistFetch': '#1565C0', 'networkLatency': '#0D47A1',
        'cacheLookup': '#00BCD4', 'cacheVerification': '#00ACC1',
        'cacheWrite': '#0097A7', 'cacheFlush': '#00838F',
    }
    
    # Top plot: Timeline of network operations
    y_positions = {}
    current_y = 0
    for event in network_events:
        event_type = event.get('type', 'unknown')
        if event_type not in y_positions:
            y_positions[event_type] = current_y
            current_y += 1
    
    for event in network_events:
        event_type = event.get('type', 'unknown')
        start_s = event.get('startMs', 0) / 1000
        duration_s = max(event.get('durationMs', 0) / 1000, 0.05)  # Min visible width
        y = y_positions[event_type]
        colour = colours.get(event_type, '#666')
        success = event.get('success', True)
        
        ax1.barh(y, duration_s, left=start_s, height=0.6,
                color=colour if success else '#F44336', alpha=0.8,
                edgecolor='white', linewidth=0.5)
    
    ax1.set_yticks(list(y_positions.values()))
    ax1.set_yticklabels(list(y_positions.keys()))
    ax1.set_xlabel('Time (seconds)')
    ax1.set_title('Network & Cache Operations Timeline')
    ax1.grid(True, axis='x', alpha=0.3)
    ax1.invert_yaxis()
    
    # Bottom plot: Throughput bar chart for events with bytes transferred
    transfer_events = [e for e in network_events if e.get('bytesTransferred', 0) > 0]
    
    if transfer_events:
        labels = []
        throughputs = []
        bar_colours = []
        
        for i, event in enumerate(transfer_events):
            event_type = event.get('type', 'unknown')
            bytes_transferred = event.get('bytesTransferred', 0)
            duration_ms = event.get('durationMs', 1)
            
            # Calculate throughput in MB/s
            throughput_mbps = (bytes_transferred / (1024 * 1024)) / (duration_ms / 1000) if duration_ms > 0 else 0
            
            label = f"{event_type}\n({format_bytes(bytes_transferred)})"
            labels.append(label)
            throughputs.append(throughput_mbps)
            bar_colours.append(colours.get(event_type, '#666'))
        
        x_pos = range(len(labels))
        bars = ax2.bar(x_pos, throughputs, color=bar_colours, alpha=0.8, edgecolor='white')
        ax2.set_xticks(x_pos)
        ax2.set_xticklabels(labels, fontsize=8)
        ax2.set_ylabel('Throughput (MB/s)')
        ax2.set_title('Network & Cache Throughput by Operation')
        ax2.grid(True, axis='y', alpha=0.3)
        
        # Add value labels on bars
        for bar, tp in zip(bars, throughputs):
            if tp > 0:
                ax2.annotate(f'{tp:.1f}', xy=(bar.get_x() + bar.get_width()/2, bar.get_height()),
                           ha='center', va='bottom', fontsize=8)
    else:
        ax2.text(0.5, 0.5, 'No transfer data available\n(bytesTransferred not recorded)',
                ha='center', va='center', transform=ax2.transAxes, fontsize=12, color='#666')
        ax2.set_axis_off()
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        print(f"[Network graph saved to {output_path}]")
    else:
        plt.show()


def plot_timeline(data: dict, output_path: Path = None, cycles: list = None) -> None:
    """Plot a timeline/Gantt chart showing the sequence of all operations."""
    if not HAS_MATPLOTLIB:
        return
    
    events = data.get('events', [])
    histograms = data.get('histograms', {})
    summary = data.get('summary', {})
    cycles = cycles or []
    
    if not events and not histograms:
        return
    
    fig, ax = plt.subplots(figsize=(14, 8))
    
    # Colour scheme by category
    category_colours = {
        # Network & OS list
        'osListFetch': '#2196F3', 'osListParse': '#2196F3', 
        'sublistFetch': '#2196F3', 'networkLatency': '#2196F3',
        # Drive operations
        'driveListPoll': '#9C27B0', 'driveOpen': '#9C27B0', 
        'driveUnmount': '#9C27B0', 'driveUnmountVolumes': '#9C27B0',
        'driveDiskClean': '#9C27B0', 'driveRescan': '#9C27B0',
        'driveFormat': '#9C27B0',
        # Recovery events (orange/red for warnings)
        'queueDepthReduction': '#FF9800', 'syncFallbackActivated': '#FF5722',
        'drainAndHotSwap': '#FF9800', 'watchdogRecovery': '#F44336',
        'progressStall': '#FFC107', 'deviceIOTimeout': '#D32F2F',
        # Cache operations
        'cacheLookup': '#00BCD4', 'cacheVerification': '#00BCD4',
        'cacheWrite': '#00BCD4', 'cacheFlush': '#00BCD4',
        # Memory management
        'memoryAllocation': '#607D8B', 'bufferResize': '#607D8B',
        'pageCacheFlush': '#607D8B', 'ringBufferStarvation': '#D32F2F',
        'writeRingBufferStats': '#8BC34A',  # Light green for write buffer stats
        # Image processing
        'imageDecompressInit': '#795548', 'imageExtraction': '#795548',
        'hashComputation': '#795548',
        # Customisation
        'customisation': '#E91E63', 'cloudInitGeneration': '#E91E63',
        'firstRunGeneration': '#E91E63', 'secureBootSetup': '#E91E63',
        # Finalisation
        'partitionTableWrite': '#FF5722', 'fatPartitionSetup': '#FF5722',
        'finalSync': '#FF5722', 'deviceClose': '#FF5722',
        # Phases
        'download': '#2196F3', 'write': '#4CAF50', 'verify': '#FF9800',
    }
    
    y_labels = []
    y_positions = []
    current_y = 0
    
    # Calculate total duration
    total_duration_ms = summary.get('durationMs', 0)
    if total_duration_ms == 0:
        # Estimate from events and histograms
        for event in events:
            end_time = event.get('startMs', 0) + event.get('durationMs', 0)
            total_duration_ms = max(total_duration_ms, end_time)
        for phase, slices in histograms.items():
            if slices:
                total_duration_ms = max(total_duration_ms, slices[-1][0] + 1000)
    
    total_duration_s = total_duration_ms / 1000
    
    # Add phase bars (download, write, verify)
    phases = summary.get('phases', {})
    phase_order = ['download', 'write', 'verify']
    
    # Estimate phase start times from histograms
    phase_times = {}
    for phase_name in phase_order:
        if phase_name in histograms and histograms[phase_name]:
            slices = histograms[phase_name]
            start_ms = slices[0][0]
            end_ms = slices[-1][0] + 1000  # Add window duration
            phase_times[phase_name] = (start_ms, end_ms - start_ms)
    
    for phase_name in phase_order:
        if phase_name in phase_times:
            start_ms, duration_ms = phase_times[phase_name]
            colour = category_colours.get(phase_name, '#666')
            ax.barh(current_y, duration_ms / 1000, left=start_ms / 1000, 
                   height=0.6, color=colour, alpha=0.7, edgecolor='white')
            y_labels.append(f"[{phase_name.upper()}]")
            y_positions.append(current_y)
            current_y += 1
    
    # Add a separator
    if phase_times:
        current_y += 0.5
    
    # Group events by type for cleaner display
    event_rows = {}  # type -> list of (start, duration)
    for event in events:
        event_type = event.get('type', 'unknown')
        start_ms = event.get('startMs', 0)
        duration_ms = event.get('durationMs', 0)
        
        if event_type not in event_rows:
            event_rows[event_type] = []
        event_rows[event_type].append((start_ms, duration_ms, event.get('success', True)))
    
    # Sort event types by first occurrence
    sorted_types = sorted(event_rows.keys(), 
                         key=lambda t: min(e[0] for e in event_rows[t]))
    
    for event_type in sorted_types:
        occurrences = event_rows[event_type]
        colour = category_colours.get(event_type, '#666')
        
        for start_ms, duration_ms, success in occurrences:
            bar_colour = colour if success else '#F44336'
            # Minimum visible width for very short events
            visible_duration = max(duration_ms, total_duration_ms * 0.005)
            ax.barh(current_y, visible_duration / 1000, left=start_ms / 1000,
                   height=0.5, color=bar_colour, alpha=0.8, edgecolor='white', linewidth=0.5)
        
        y_labels.append(event_type)
        y_positions.append(current_y)
        current_y += 1
    
    # Configure axes
    ax.set_yticks(y_positions)
    ax.set_yticklabels(y_labels)
    ax.set_xlabel('Time (seconds)')
    ax.set_title('Operation Timeline')
    ax.set_xlim(0, total_duration_s * 1.02)  # Small margin
    ax.grid(True, axis='x', alpha=0.3)
    ax.invert_yaxis()  # Top to bottom
    
    # Add cycle boundary markers
    if cycles and len(cycles) > 1:
        for cycle in cycles:
            if cycle.start_ms > 0:
                ax.axvline(x=cycle.start_ms / 1000, color='#FF5722', linestyle='--', 
                          linewidth=2, alpha=0.7, zorder=10)
                ax.annotate(f'Cycle {cycle.id}', xy=(cycle.start_ms / 1000, 0),
                           xytext=(5, 5), textcoords='offset points',
                           fontsize=9, fontweight='bold', color='#FF5722')
    
    # Add legend for categories
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor='#2196F3', label='Network/Download'),
        Patch(facecolor='#4CAF50', label='Write'),
        Patch(facecolor='#FF9800', label='Verify'),
        Patch(facecolor='#9C27B0', label='Drive ops'),
        Patch(facecolor='#00BCD4', label='Cache'),
        Patch(facecolor='#E91E63', label='Customisation'),
        Patch(facecolor='#FF5722', label='Finalisation'),
        Patch(facecolor='#F44336', label='Failed'),
    ]
    if cycles and len(cycles) > 1:
        legend_elements.append(Patch(facecolor='#FF5722', label='Cycle boundary', alpha=0.5))
    ax.legend(handles=legend_elements, loc='upper right', fontsize=8)
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        print(f"[Timeline saved to {output_path}]")
    else:
        plt.show()


def plot_throughput(data: dict, analysis: dict, output_path: Path = None, cycles: list = None) -> None:
    """Plot throughput over time for each phase, with event markers overlaid."""
    if not HAS_MATPLOTLIB:
        print("\n[Visualisation unavailable - install matplotlib: pip install matplotlib numpy]")
        return
    
    histograms = data.get('histograms', {})
    events = data.get('events', [])
    cycles = cycles or []
    
    if not histograms:
        print("\n[No histogram data to plot]")
        return
    
    # Count phases with data
    phases_with_data = [p for p in ['download', 'write', 'verify'] if p in histograms and histograms[p]]
    if not phases_with_data:
        return
    
    # Event colours by category
    event_colours = {
        # Network (blue)
        'osListFetch': '#1565C0', 'osListParse': '#1565C0', 
        'sublistFetch': '#1565C0', 'networkLatency': '#1565C0',
        'networkRetry': '#D32F2F', 'networkConnectionStats': '#1565C0',
        # Drive (purple)
        'driveListPoll': '#7B1FA2', 'driveOpen': '#7B1FA2', 
        'driveUnmount': '#7B1FA2', 'driveUnmountVolumes': '#7B1FA2',
        'driveDiskClean': '#7B1FA2', 'driveRescan': '#7B1FA2',
        'driveFormat': '#7B1FA2',
        # Recovery events (orange/red for warnings)
        'queueDepthReduction': '#FF9800', 'syncFallbackActivated': '#FF5722',
        'drainAndHotSwap': '#FF9800', 'watchdogRecovery': '#F44336',
        'progressStall': '#FFC107', 'deviceIOTimeout': '#D32F2F',
        # Cache (cyan)
        'cacheLookup': '#00838F', 'cacheVerification': '#00838F',
        'cacheWrite': '#00838F', 'cacheFlush': '#00838F',
        # Memory (grey) - ringBufferStarvation in RED to highlight issues
        'memoryAllocation': '#455A64', 'bufferResize': '#455A64',
        'pageCacheFlush': '#455A64', 'ringBufferStarvation': '#D32F2F',
        'writeRingBufferStats': '#8BC34A',  # Light green for write buffer stats
        # Image processing (brown)
        'imageDecompressInit': '#5D4037', 'imageExtraction': '#5D4037',
        'hashComputation': '#5D4037',
        # Customisation (pink)
        'customisation': '#C2185B', 'cloudInitGeneration': '#C2185B',
        'firstRunGeneration': '#C2185B', 'secureBootSetup': '#C2185B',
        # Finalisation (deep orange)
        'partitionTableWrite': '#E64A19', 'fatPartitionSetup': '#E64A19',
        'finalSync': '#E64A19', 'deviceClose': '#E64A19',
    }
    
    fig, axes = plt.subplots(len(phases_with_data), 1, figsize=(14, 5 * len(phases_with_data)))
    if len(phases_with_data) == 1:
        axes = [axes]
    
    phase_colours = {'download': '#2196F3', 'write': '#4CAF50', 'verify': '#FF9800'}
    
    for ax, phase_name in zip(axes, phases_with_data):
        samples = analysis.get(phase_name, {}).get('throughput_samples', [])
        if not samples:
            continue
        
        timestamps = [s['timestamp'] / 1000 for s in samples]  # Convert to seconds
        avg_throughput = [s['avg'] / 1024 for s in samples]    # Convert to MB/s
        min_throughput = [s['min'] / 1024 for s in samples]
        max_throughput = [s['max'] / 1024 for s in samples]
        
        phase_start = min(timestamps)
        phase_end = max(timestamps)
        max_tp = max(max_throughput) if max_throughput else 100
        
        # Plot average with min/max range
        ax.fill_between(timestamps, min_throughput, max_throughput, 
                       alpha=0.3, color=phase_colours.get(phase_name, '#666'))
        ax.plot(timestamps, avg_throughput, 
               color=phase_colours.get(phase_name, '#666'), linewidth=1.5, label='Throughput')
        
        # Overlay events that occurred during this phase
        event_labels_added = set()
        for event in events:
            event_start_s = event.get('startMs', 0) / 1000
            event_duration_s = event.get('durationMs', 0) / 1000
            event_end_s = event_start_s + event_duration_s
            event_type = event.get('type', 'unknown')
            
            # Check if event overlaps with this phase
            if event_end_s >= phase_start and event_start_s <= phase_end:
                colour = event_colours.get(event_type, '#666')
                
                # Draw event as a vertical span
                ax.axvspan(event_start_s, event_end_s, 
                          alpha=0.3, color=colour, zorder=1)
                
                # Add label at top (only once per event type)
                if event_type not in event_labels_added:
                    # Position label at the event's midpoint
                    label_x = (event_start_s + event_end_s) / 2
                    ax.annotate(event_type, xy=(label_x, max_tp * 0.95),
                               fontsize=7, ha='center', va='top',
                               color=colour, rotation=90,
                               bbox=dict(boxstyle='round,pad=0.2', facecolor='white', 
                                        edgecolor=colour, alpha=0.8))
                    event_labels_added.add(event_type)
        
        # Mark stalls
        stalls = analysis.get(phase_name, {}).get('stalls', [])
        if stalls:
            stall_times = [s['timestamp'] / 1000 for s in stalls]
            stall_values = [s['throughput'] / 1024 for s in stalls]
            ax.scatter(stall_times, stall_values, color='red', marker='v', 
                      s=50, zorder=5, label='Stalls')
        
        # Add cycle boundary markers
        if cycles and len(cycles) > 1:
            for cycle in cycles:
                if cycle.start_ms > 0:
                    cycle_start_s = cycle.start_ms / 1000
                    if phase_start <= cycle_start_s <= phase_end:
                        ax.axvline(x=cycle_start_s, color='#FF5722', linestyle='--', 
                                  linewidth=2, alpha=0.7, zorder=10)
                        ax.annotate(f'Cycle {cycle.id}', xy=(cycle_start_s, max_tp * 0.9),
                                   fontsize=8, fontweight='bold', color='#FF5722')
        
        ax.set_xlabel('Time (seconds)')
        ax.set_ylabel('Throughput (MB/s)')
        ax.set_title(f'{phase_name.title()} Phase Throughput (with concurrent events)')
        ax.grid(True, alpha=0.3)
        ax.set_ylim(bottom=0, top=max_tp * 1.1)
        ax.legend(loc='upper right')
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        print(f"[Throughput graph saved to {output_path}]")
    else:
        plt.show()


def figure_to_base64(fig) -> str:
    """Convert a matplotlib figure to base64-encoded PNG."""
    buf = io.BytesIO()
    fig.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    img_base64 = base64.b64encode(buf.read()).decode('utf-8')
    plt.close(fig)
    return img_base64


def create_timeline_figure(data: dict):
    """Create timeline figure and return it (for HTML embedding)."""
    if not HAS_MATPLOTLIB:
        return None
    
    events = data.get('events', [])
    histograms = data.get('histograms', {})
    summary = data.get('summary', {})
    
    if not events and not histograms:
        return None
    
    fig, ax = plt.subplots(figsize=(14, 8))
    
    # Colour scheme by category
    category_colours = {
        'osListFetch': '#2196F3', 'osListParse': '#2196F3', 
        'sublistFetch': '#2196F3', 'networkLatency': '#2196F3',
        'networkRetry': '#D32F2F', 'networkConnectionStats': '#2196F3',
        'driveListPoll': '#9C27B0', 'driveOpen': '#9C27B0', 
        'driveUnmount': '#9C27B0', 'driveFormat': '#9C27B0',
        'driveUnmountVolumes': '#9C27B0', 'driveDiskClean': '#9C27B0',
        'driveRescan': '#9C27B0',
        'cacheLookup': '#00BCD4', 'cacheVerification': '#00BCD4',
        'cacheWrite': '#00BCD4', 'cacheFlush': '#00BCD4',
        'memoryAllocation': '#607D8B', 'bufferResize': '#607D8B',
        'pageCacheFlush': '#607D8B', 'ringBufferStarvation': '#D32F2F',
        'writeRingBufferStats': '#8BC34A',  # Light green for write buffer stats
        'imageDecompressInit': '#795548', 'imageExtraction': '#795548',
        'hashComputation': '#795548',
        'customisation': '#E91E63', 'cloudInitGeneration': '#E91E63',
        'firstRunGeneration': '#E91E63', 'secureBootSetup': '#E91E63',
        'partitionTableWrite': '#FF5722', 'fatPartitionSetup': '#FF5722',
        'finalSync': '#FF5722', 'deviceClose': '#FF5722',
        'download': '#2196F3', 'write': '#4CAF50', 'verify': '#FF9800',
    }
    
    y_labels = []
    y_positions = []
    current_y = 0
    
    total_duration_ms = summary.get('durationMs', 0)
    if total_duration_ms == 0:
        for event in events:
            end_time = event.get('startMs', 0) + event.get('durationMs', 0)
            total_duration_ms = max(total_duration_ms, end_time)
        for phase, slices in histograms.items():
            if slices:
                total_duration_ms = max(total_duration_ms, slices[-1][0] + 1000)
    
    total_duration_s = total_duration_ms / 1000
    
    phases = summary.get('phases', {})
    phase_order = ['download', 'write', 'verify']
    
    phase_times = {}
    for phase_name in phase_order:
        if phase_name in histograms and histograms[phase_name]:
            slices = histograms[phase_name]
            start_ms = slices[0][0]
            end_ms = slices[-1][0] + 1000
            phase_times[phase_name] = (start_ms, end_ms - start_ms)
    
    for phase_name in phase_order:
        if phase_name in phase_times:
            start_ms, duration_ms = phase_times[phase_name]
            colour = category_colours.get(phase_name, '#666')
            ax.barh(current_y, duration_ms / 1000, left=start_ms / 1000, 
                   height=0.6, color=colour, alpha=0.7, edgecolor='white')
            y_labels.append(f"[{phase_name.upper()}]")
            y_positions.append(current_y)
            current_y += 1
    
    if phase_times:
        current_y += 0.5
    
    event_rows = {}
    for event in events:
        event_type = event.get('type', 'unknown')
        start_ms = event.get('startMs', 0)
        duration_ms = event.get('durationMs', 0)
        if event_type not in event_rows:
            event_rows[event_type] = []
        event_rows[event_type].append((start_ms, duration_ms, event.get('success', True)))
    
    sorted_types = sorted(event_rows.keys(), 
                         key=lambda t: min(e[0] for e in event_rows[t]))
    
    for event_type in sorted_types:
        occurrences = event_rows[event_type]
        colour = category_colours.get(event_type, '#666')
        
        for start_ms, duration_ms, success in occurrences:
            bar_colour = colour if success else '#F44336'
            visible_duration = max(duration_ms, total_duration_ms * 0.005)
            ax.barh(current_y, visible_duration / 1000, left=start_ms / 1000,
                   height=0.5, color=bar_colour, alpha=0.8, edgecolor='white', linewidth=0.5)
        
        y_labels.append(event_type)
        y_positions.append(current_y)
        current_y += 1
    
    ax.set_yticks(y_positions)
    ax.set_yticklabels(y_labels)
    ax.set_xlabel('Time (seconds)')
    ax.set_title('Operation Timeline')
    ax.set_xlim(0, total_duration_s * 1.02)
    ax.grid(True, axis='x', alpha=0.3)
    ax.invert_yaxis()
    
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor='#2196F3', label='Network/Download'),
        Patch(facecolor='#4CAF50', label='Write'),
        Patch(facecolor='#FF9800', label='Verify'),
        Patch(facecolor='#9C27B0', label='Drive ops'),
        Patch(facecolor='#00BCD4', label='Cache'),
        Patch(facecolor='#E91E63', label='Customisation'),
        Patch(facecolor='#FF5722', label='Finalisation'),
        Patch(facecolor='#F44336', label='Failed'),
    ]
    ax.legend(handles=legend_elements, loc='upper right', fontsize=8)
    
    plt.tight_layout()
    return fig


def create_throughput_figure(data: dict, analysis: dict):
    """Create throughput figure and return it (for HTML embedding)."""
    if not HAS_MATPLOTLIB:
        return None
    
    histograms = data.get('histograms', {})
    events = data.get('events', [])
    
    if not histograms:
        return None
    
    phases_with_data = [p for p in ['download', 'write', 'verify'] if p in histograms and histograms[p]]
    if not phases_with_data:
        return None
    
    event_colours = {
        'osListFetch': '#1565C0', 'osListParse': '#1565C0', 
        'sublistFetch': '#1565C0', 'networkLatency': '#1565C0',
        'driveListPoll': '#7B1FA2', 'driveOpen': '#7B1FA2', 
        'driveUnmount': '#7B1FA2', 'driveUnmountVolumes': '#7B1FA2',
        'driveDiskClean': '#7B1FA2', 'driveRescan': '#7B1FA2',
        'driveFormat': '#7B1FA2',
        'cacheLookup': '#00838F', 'cacheVerification': '#00838F',
        'cacheWrite': '#00838F', 'cacheFlush': '#00838F',
        'memoryAllocation': '#455A64', 'bufferResize': '#455A64',
        'pageCacheFlush': '#455A64',
        'imageDecompressInit': '#5D4037', 'imageExtraction': '#5D4037',
        'hashComputation': '#5D4037',
        'customisation': '#C2185B', 'cloudInitGeneration': '#C2185B',
        'firstRunGeneration': '#C2185B', 'secureBootSetup': '#C2185B',
        'partitionTableWrite': '#E64A19', 'fatPartitionSetup': '#E64A19',
        'finalSync': '#E64A19', 'deviceClose': '#E64A19',
    }
    
    fig, axes = plt.subplots(len(phases_with_data), 1, figsize=(14, 5 * len(phases_with_data)))
    if len(phases_with_data) == 1:
        axes = [axes]
    
    phase_colours = {'download': '#2196F3', 'write': '#4CAF50', 'verify': '#FF9800'}
    
    for ax, phase_name in zip(axes, phases_with_data):
        samples = analysis.get(phase_name, {}).get('throughput_samples', [])
        if not samples:
            continue
        
        timestamps = [s['timestamp'] / 1000 for s in samples]
        avg_throughput = [s['avg'] / 1024 for s in samples]
        min_throughput = [s['min'] / 1024 for s in samples]
        max_throughput = [s['max'] / 1024 for s in samples]
        
        phase_start = min(timestamps)
        phase_end = max(timestamps)
        max_tp = max(max_throughput) if max_throughput else 100
        
        ax.fill_between(timestamps, min_throughput, max_throughput, 
                       alpha=0.3, color=phase_colours.get(phase_name, '#666'))
        ax.plot(timestamps, avg_throughput, 
               color=phase_colours.get(phase_name, '#666'), linewidth=1.5, label='Throughput')
        
        event_labels_added = set()
        for event in events:
            event_start_s = event.get('startMs', 0) / 1000
            event_duration_s = event.get('durationMs', 0) / 1000
            event_end_s = event_start_s + event_duration_s
            event_type = event.get('type', 'unknown')
            
            if event_end_s >= phase_start and event_start_s <= phase_end:
                colour = event_colours.get(event_type, '#666')
                ax.axvspan(event_start_s, event_end_s, alpha=0.3, color=colour, zorder=1)
                
                if event_type not in event_labels_added:
                    label_x = (event_start_s + event_end_s) / 2
                    ax.annotate(event_type, xy=(label_x, max_tp * 0.95),
                               fontsize=7, ha='center', va='top',
                               color=colour, rotation=90,
                               bbox=dict(boxstyle='round,pad=0.2', facecolor='white', 
                                        edgecolor=colour, alpha=0.8))
                    event_labels_added.add(event_type)
        
        stalls = analysis.get(phase_name, {}).get('stalls', [])
        if stalls:
            stall_times = [s['timestamp'] / 1000 for s in stalls]
            stall_values = [s['throughput'] / 1024 for s in stalls]
            ax.scatter(stall_times, stall_values, color='red', marker='v', 
                      s=50, zorder=5, label='Stalls')
        
        ax.set_xlabel('Time (seconds)')
        ax.set_ylabel('Throughput (MB/s)')
        ax.set_title(f'{phase_name.title()} Phase Throughput (with concurrent events)')
        ax.grid(True, alpha=0.3)
        ax.set_ylim(bottom=0, top=max_tp * 1.1)
        ax.legend(loc='upper right')
    
    plt.tight_layout()
    return fig


def create_network_figure(data: dict):
    """Create network figure and return it (for HTML embedding)."""
    if not HAS_MATPLOTLIB:
        return None
    
    events = data.get('events', [])
    
    network_types = {'osListFetch', 'osListParse', 'sublistFetch', 'networkLatency',
                    'cacheLookup', 'cacheVerification', 'cacheWrite', 'cacheFlush'}
    
    network_events = [e for e in events 
                     if e.get('type') in network_types or e.get('bytesTransferred', 0) > 0]
    
    if not network_events:
        return None
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8))
    
    colours = {
        'osListFetch': '#2196F3', 'osListParse': '#1976D2', 
        'sublistFetch': '#1565C0', 'networkLatency': '#0D47A1',
        'cacheLookup': '#00BCD4', 'cacheVerification': '#00ACC1',
        'cacheWrite': '#0097A7', 'cacheFlush': '#00838F',
    }
    
    y_positions = {}
    current_y = 0
    for event in network_events:
        event_type = event.get('type', 'unknown')
        if event_type not in y_positions:
            y_positions[event_type] = current_y
            current_y += 1
    
    for event in network_events:
        event_type = event.get('type', 'unknown')
        start_s = event.get('startMs', 0) / 1000
        duration_s = max(event.get('durationMs', 0) / 1000, 0.05)
        y = y_positions[event_type]
        colour = colours.get(event_type, '#666')
        success = event.get('success', True)
        
        ax1.barh(y, duration_s, left=start_s, height=0.6,
                color=colour if success else '#F44336', alpha=0.8,
                edgecolor='white', linewidth=0.5)
    
    ax1.set_yticks(list(y_positions.values()))
    ax1.set_yticklabels(list(y_positions.keys()))
    ax1.set_xlabel('Time (seconds)')
    ax1.set_title('Network & Cache Operations Timeline')
    ax1.grid(True, axis='x', alpha=0.3)
    ax1.invert_yaxis()
    
    transfer_events = [e for e in network_events if e.get('bytesTransferred', 0) > 0]
    
    if transfer_events:
        labels = []
        throughputs = []
        bar_colours = []
        
        for i, event in enumerate(transfer_events):
            event_type = event.get('type', 'unknown')
            bytes_transferred = event.get('bytesTransferred', 0)
            duration_ms = event.get('durationMs', 1)
            
            throughput_mbps = (bytes_transferred / (1024 * 1024)) / (duration_ms / 1000) if duration_ms > 0 else 0
            
            label = f"{event_type}\n({format_bytes(bytes_transferred)})"
            labels.append(label)
            throughputs.append(throughput_mbps)
            bar_colours.append(colours.get(event_type, '#666'))
        
        x_pos = range(len(labels))
        bars = ax2.bar(x_pos, throughputs, color=bar_colours, alpha=0.8, edgecolor='white')
        ax2.set_xticks(x_pos)
        ax2.set_xticklabels(labels, fontsize=8)
        ax2.set_ylabel('Throughput (MB/s)')
        ax2.set_title('Network & Cache Throughput by Operation')
        ax2.grid(True, axis='y', alpha=0.3)
        
        for bar, tp in zip(bars, throughputs):
            if tp > 0:
                ax2.annotate(f'{tp:.1f}', xy=(bar.get_x() + bar.get_width()/2, bar.get_height()),
                           ha='center', va='bottom', fontsize=8)
    else:
        ax2.text(0.5, 0.5, 'No transfer data available\n(bytesTransferred not recorded)',
                ha='center', va='center', transform=ax2.transAxes, fontsize=12, color='#666')
        ax2.set_axis_off()
    
    plt.tight_layout()
    return fig


def generate_html_report(data: dict, analysis: dict, output_path: Path, cycles: list = None) -> None:
    """Generate a self-contained HTML report with embedded graphs and data."""
    summary = data.get('summary', {})
    events = data.get('events', [])
    system = data.get('system', {})
    cycles = cycles or []
    
    # Generate graphs as base64-encoded images
    graphs = {}
    if HAS_MATPLOTLIB:
        fig = create_timeline_figure(data)
        if fig:
            graphs['timeline'] = figure_to_base64(fig)
        
        fig = create_throughput_figure(data, analysis)
        if fig:
            graphs['throughput'] = figure_to_base64(fig)
        
        fig = create_network_figure(data)
        if fig:
            graphs['network'] = figure_to_base64(fig)
    
    # Build HTML
    html_parts = ['''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Raspberry Pi Imager Performance Report</title>
    <style>
        :root {
            --bg-primary: #1a1a2e;
            --bg-secondary: #16213e;
            --bg-card: #1f2937;
            --text-primary: #e2e8f0;
            --text-secondary: #94a3b8;
            --accent: #c026d3;
            --accent-light: #e879f9;
            --success: #22c55e;
            --error: #ef4444;
            --warning: #f59e0b;
            --border: #374151;
        }
        
        * { box-sizing: border-box; margin: 0; padding: 0; }
        
        body {
            font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
            background: var(--bg-primary);
            color: var(--text-primary);
            line-height: 1.6;
            padding: 2rem;
        }
        
        .container { max-width: 1400px; margin: 0 auto; }
        
        h1 {
            font-size: 2rem;
            font-weight: 600;
            margin-bottom: 0.5rem;
            background: linear-gradient(135deg, var(--accent), var(--accent-light));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
        }
        
        h2 {
            font-size: 1.25rem;
            font-weight: 600;
            margin: 2rem 0 1rem;
            color: var(--text-primary);
            border-bottom: 2px solid var(--accent);
            padding-bottom: 0.5rem;
        }
        
        h3 {
            font-size: 1rem;
            font-weight: 600;
            margin: 1.5rem 0 0.75rem;
            color: var(--text-secondary);
        }
        
        .subtitle {
            color: var(--text-secondary);
            font-size: 0.9rem;
            margin-bottom: 2rem;
        }
        
        .card {
            background: var(--bg-card);
            border-radius: 12px;
            padding: 1.5rem;
            margin-bottom: 1.5rem;
            border: 1px solid var(--border);
        }
        
        .grid { display: grid; gap: 1.5rem; }
        .grid-2 { grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }
        .grid-3 { grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); }
        
        .stat {
            text-align: center;
            padding: 1rem;
        }
        
        .stat-value {
            font-size: 1.75rem;
            font-weight: 700;
            color: var(--accent-light);
        }
        
        .stat-label {
            font-size: 0.85rem;
            color: var(--text-secondary);
            margin-top: 0.25rem;
        }
        
        .success { color: var(--success); }
        .error { color: var(--error); }
        .warning { color: var(--warning); }
        
        table {
            width: 100%;
            border-collapse: collapse;
            font-size: 0.9rem;
        }
        
        th, td {
            padding: 0.75rem;
            text-align: left;
            border-bottom: 1px solid var(--border);
        }
        
        th {
            color: var(--text-secondary);
            font-weight: 600;
            font-size: 0.8rem;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        
        tr:hover { background: rgba(255,255,255,0.02); }
        
        .graph-container {
            background: var(--bg-secondary);
            border-radius: 8px;
            padding: 1rem;
            margin: 1rem 0;
            text-align: center;
        }
        
        .graph-container img {
            max-width: 100%;
            height: auto;
            border-radius: 4px;
        }
        
        .tag {
            display: inline-block;
            padding: 0.25rem 0.5rem;
            border-radius: 4px;
            font-size: 0.75rem;
            font-weight: 600;
        }
        
        .tag-ok { background: rgba(34, 197, 94, 0.2); color: var(--success); }
        .tag-fail { background: rgba(239, 68, 68, 0.2); color: var(--error); }
        
        details {
            margin: 1rem 0;
        }
        
        summary {
            cursor: pointer;
            padding: 0.75rem;
            background: var(--bg-secondary);
            border-radius: 8px;
            font-weight: 600;
        }
        
        summary:hover { background: rgba(255,255,255,0.05); }
        
        pre {
            background: var(--bg-secondary);
            padding: 1rem;
            border-radius: 8px;
            overflow-x: auto;
            font-size: 0.8rem;
            margin-top: 0.5rem;
        }
        
        code { font-family: 'Consolas', 'Monaco', monospace; }
        
        .metadata {
            font-size: 0.8rem;
            color: var(--text-secondary);
            margin-top: 0.25rem;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Raspberry Pi Imager Performance Report</h1>
        <p class="subtitle">Generated: ''' + html.escape(data.get('exportTime', 'Unknown')) + '''</p>
''']

    # Summary section - infer session state (works for both new and old traces)
    session_state = infer_session_state(data)
    
    state_classes = {
        'succeeded': 'success',
        'failed': 'error',
        'cancelled': 'warning',
        'in_progress': 'warning',
        'never_started': 'error'
    }
    state_texts = {
        'succeeded': 'Success',
        'failed': 'Failed',
        'cancelled': 'Cancelled',
        'in_progress': 'In Progress',
        'never_started': 'No Session'
    }
    
    status_class = state_classes.get(session_state, 'error')
    status_text = state_texts.get(session_state, session_state)
    
    html_parts.append(f'''
        <div class="card">
            <div class="grid grid-3">
                <div class="stat">
                    <div class="stat-value">{html.escape(summary.get('imageName', 'Unknown'))}</div>
                    <div class="stat-label">Image</div>
                </div>
                <div class="stat">
                    <div class="stat-value">{format_duration(summary.get('durationMs', 0))}</div>
                    <div class="stat-label">Total Duration</div>
                </div>
                <div class="stat">
                    <div class="stat-value {status_class}">{status_text}</div>
                    <div class="stat-label">Result</div>
                </div>
            </div>
        </div>
''')

    # System info section
    if system:
        memory = system.get('memory', {})
        device = system.get('targetDevice', {})
        platform = system.get('platform', {})
        
        html_parts.append('''
        <h2>System Information</h2>
        <div class="card">
            <div class="grid grid-3">
''')
        
        if memory:
            html_parts.append(f'''
                <div>
                    <h3>Memory</h3>
                    <table>
                        <tr><td>Total RAM</td><td><strong>{memory.get('totalMB', 0):,} MB</strong></td></tr>
                        <tr><td>Available at Start</td><td><strong>{memory.get('availableMB', 0):,} MB</strong></td></tr>
                    </table>
                </div>
''')
        
        if device:
            html_parts.append(f'''
                <div>
                    <h3>Target Device</h3>
                    <table>
                        <tr><td>Description</td><td><strong>{html.escape(device.get('description', 'Unknown'))}</strong></td></tr>
                        <tr><td>Size</td><td><strong>{device.get('sizeMB', 0):,} MB</strong></td></tr>
                        <tr><td>USB</td><td>{'Yes' if device.get('isUsb') else 'No'}</td></tr>
                        <tr><td>Removable</td><td>{'Yes' if device.get('isRemovable') else 'No'}</td></tr>
                    </table>
                </div>
''')
        
        if platform:
            html_parts.append(f'''
                <div>
                    <h3>Platform</h3>
                    <table>
                        <tr><td>OS</td><td><strong>{html.escape(platform.get('os', 'Unknown'))}</strong></td></tr>
                        <tr><td>Version</td><td>{html.escape(platform.get('osVersion', 'Unknown'))}</td></tr>
                        <tr><td>Architecture</td><td>{html.escape(platform.get('cpuArchitecture', 'Unknown'))}</td></tr>
                        <tr><td>CPU Cores</td><td>{platform.get('cpuCores', 0)}</td></tr>
                    </table>
                </div>
''')
        
        html_parts.append('</div>')
        
        # Imager build info (separate card for prominence)
        imager = system.get('imager', {})
        if imager:
            sha256 = imager.get('binarySha256', '')
            sha256_display = f'{sha256[:16]}...{sha256[-8:]}' if len(sha256) > 24 else sha256
            
            html_parts.append(f'''
        <div class="card" style="margin-top: 1rem;">
            <h3>Raspberry Pi Imager Build</h3>
            <table>
                <tr><td>Version</td><td><strong>{html.escape(imager.get('version', 'Unknown'))}</strong></td></tr>
                <tr><td>Build Type</td><td>{html.escape(imager.get('buildType', 'Unknown'))}</td></tr>
                <tr><td>Binary SHA256</td><td><code title="{html.escape(sha256)}">{html.escape(sha256_display)}</code></td></tr>
                <tr><td>Qt Runtime</td><td>{html.escape(imager.get('qtRuntime', 'Unknown'))}</td></tr>
                <tr><td>Qt Build</td><td>{html.escape(imager.get('qtBuild', 'Unknown'))}</td></tr>
            </table>
        </div>
''')
        else:
            html_parts.append('</div>')

    # Imaging Cycles section
    if cycles and len(cycles) > 0:
        html_parts.append('''
        <h2>Imaging Cycles</h2>
        <div class="card">
''')
        if len(cycles) > 1:
            html_parts.append(f'<p style="margin-bottom: 1rem; color: var(--text-secondary);">Multiple imaging cycles detected in this capture.</p>')
        
        for cycle in cycles:
            status_colors = {
                'completed': 'var(--success)',
                'failed': 'var(--error)',
                'cancelled': 'var(--warning)',
                'in_progress': 'var(--warning)',
                'exported_early': 'var(--warning)',
                'never_started': 'var(--error)'
            }
            status_labels = {
                'completed': 'Completed',
                'failed': 'Failed',
                'cancelled': 'Cancelled',
                'in_progress': 'In Progress',
                'exported_early': 'Incomplete',
                'never_started': 'Not Started'
            }
            status_color = status_colors.get(cycle.completion_reason, 'var(--text-secondary)')
            status_label = status_labels.get(cycle.completion_reason, 'Unknown')
            
            html_parts.append(f'''
            <div style="border-left: 4px solid {status_color}; padding-left: 1rem; margin-bottom: 1.5rem;">
                <h3 style="margin-top: 0;">Cycle {cycle.id} <span class="tag" style="background: {status_color}20; color: {status_color};">{status_label}</span></h3>
                <table>
                    <tr><td>Time Range</td><td>{cycle.start_ms/1000:.1f}s - {cycle.end_ms/1000:.1f}s ({format_duration(cycle.duration_ms)})</td></tr>
                    <tr><td>Cache</td><td>{'Hit (used cached image)' if cycle.cache_hit else 'Miss (downloaded)'}</td></tr>
                    <tr><td>Direct I/O</td><td>{'Enabled' if cycle.direct_io else 'Disabled'}</td></tr>
                    <tr><td>Bottleneck</td><td>{cycle.get_bottleneck().upper()}</td></tr>
                </table>
''')
            # Add phase throughput for this cycle
            phase_stats_html = []
            for phase in ['download', 'decompress', 'write', 'verify']:
                stats = cycle.get_phase_stats(phase)
                if stats:
                    phase_stats_html.append(f'''
                    <tr>
                        <td>{phase.title()}</td>
                        <td>{format_throughput(stats['avgThroughputKBps'])} avg</td>
                        <td>{format_throughput(stats['minThroughputKBps'])} - {format_throughput(stats['maxThroughputKBps'])}</td>
                        <td>{stats['sampleCount']} samples</td>
                    </tr>''')
            
            if phase_stats_html:
                html_parts.append('''
                <h4 style="margin-top: 1rem;">Phase Throughput</h4>
                <table>
                    <thead><tr><th>Phase</th><th>Avg</th><th>Range</th><th>Samples</th></tr></thead>
                    <tbody>
''')
                html_parts.extend(phase_stats_html)
                html_parts.append('</tbody></table>')
            
            html_parts.append('</div>')
        
        html_parts.append('</div>')

    # Throughput section
    phases = summary.get('phases', {})
    if phases:
        html_parts.append('''
        <h2>Throughput Summary</h2>
        <div class="card">
            <div class="grid grid-3">
''')
        for phase_name in ['download', 'write', 'verify']:
            phase = phases.get(phase_name, {})
            if phase.get('sampleCount', 0) > 0:
                html_parts.append(f'''
                <div class="stat">
                    <div class="stat-value">{format_throughput(phase.get('avgThroughputKBps', 0))}</div>
                    <div class="stat-label">{phase_name.title()} (avg)</div>
                    <div class="metadata">{format_throughput(phase.get('minThroughputKBps', 0))} - {format_throughput(phase.get('maxThroughputKBps', 0))}</div>
                </div>
''')
        html_parts.append('</div></div>')

    # Graphs
    if graphs:
        html_parts.append('<h2>Visualisations</h2>')
        
        if 'timeline' in graphs:
            html_parts.append(f'''
        <div class="card">
            <h3>Operation Timeline</h3>
            <div class="graph-container">
                <img src="data:image/png;base64,{graphs['timeline']}" alt="Timeline">
            </div>
        </div>
''')
        
        if 'throughput' in graphs:
            html_parts.append(f'''
        <div class="card">
            <h3>Throughput Over Time</h3>
            <div class="graph-container">
                <img src="data:image/png;base64,{graphs['throughput']}" alt="Throughput">
            </div>
        </div>
''')
        
        if 'network' in graphs:
            html_parts.append(f'''
        <div class="card">
            <h3>Network & Cache Operations</h3>
            <div class="graph-container">
                <img src="data:image/png;base64,{graphs['network']}" alt="Network">
            </div>
        </div>
''')

    # Events table
    if events:
        html_parts.append('''
        <h2>Event Log</h2>
        <div class="card">
            <table>
                <thead>
                    <tr>
                        <th>Event</th>
                        <th>Start</th>
                        <th>Duration</th>
                        <th>Status</th>
                        <th>Details</th>
                    </tr>
                </thead>
                <tbody>
''')
        for event in events:
            status_tag = '<span class="tag tag-ok">OK</span>' if event.get('success', True) else '<span class="tag tag-fail">FAIL</span>'
            metadata = html.escape(event.get('metadata', ''))
            bytes_info = ''
            if event.get('bytesTransferred', 0) > 0:
                bytes_info = f" ({format_bytes(event['bytesTransferred'])})"
            
            html_parts.append(f'''
                    <tr>
                        <td><code>{html.escape(event.get('type', 'unknown'))}</code></td>
                        <td>{format_duration(event.get('startMs', 0))}</td>
                        <td>{format_duration(event.get('durationMs', 0))}</td>
                        <td>{status_tag}</td>
                        <td class="metadata">{metadata}{bytes_info}</td>
                    </tr>
''')
        html_parts.append('</tbody></table></div>')

    # Raw JSON data
    html_parts.append('''
        <h2>Raw Data</h2>
        <details>
            <summary>View JSON Data</summary>
            <pre><code>''' + html.escape(json.dumps(data, indent=2)) + '''</code></pre>
        </details>
''')

    # Footer
    html_parts.append('''
    </div>
</body>
</html>
''')

    # Write HTML file
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(''.join(html_parts))
    
    print(f"[HTML report saved to {output_path}]")


def main():
    if len(sys.argv) < 2:
        print("Usage: python analyse_performance.py <performance-data.json> [options]")
        print("\nOptions:")
        print("  --save-graphs   Save graphs as PNG files")
        print("  --html          Generate self-contained HTML report (recommended)")
        print("  --no-cycles     Skip cycle detection (legacy mode)")
        print("\nOutput files:")
        print("  --html          <input>.html              Complete report with embedded graphs")
        print("  --save-graphs   <input>-timeline.png      Operation sequence timeline")
        print("                  <input>-throughput.png    Throughput over time")
        print("                  <input>-network.png       Network & cache operations")
        sys.exit(1)
    
    json_path = Path(sys.argv[1])
    save_graphs = '--save-graphs' in sys.argv or '--save-graph' in sys.argv
    generate_html = '--html' in sys.argv
    skip_cycles = '--no-cycles' in sys.argv
    
    if not json_path.exists():
        print(f"Error: File not found: {json_path}")
        sys.exit(1)
    
    try:
        with open(json_path, 'r') as f:
            data = json.load(f)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON: {e}")
        sys.exit(1)
    
    # Print textual analysis
    print_summary(data)
    
    # Check for "never started" session and provide helpful guidance
    # Uses inference to detect this condition even in older traces
    session_state = infer_session_state(data)
    if session_state == 'never_started':
        print("\n" + "!" * 60)
        print("WARNING: No imaging session was captured in this data file.")
        print("!" * 60)
        print("""
This typically means the performance data was exported before or without
performing an actual write operation. Possible causes:

  1. The app was restarted after a write - performance data is stored
     in memory and does not persist across app restarts.
     
  2. The data was exported before starting a write operation.
  
  3. The write was performed in a different app instance.

To capture complete performance data:
  - Start the write operation
  - Wait for it to complete (or cancel it)
  - Export performance data BEFORE closing the app
""")
    
    # Detect and analyse imaging cycles
    cycles = []
    if not skip_cycles:
        cycles = detect_cycles(data)
        if len(cycles) > 1:
            print_cycles(cycles)
            print(get_cycle_comparison_table(cycles))
        elif len(cycles) == 1:
            print_cycles(cycles)
    
    # Print legacy event breakdown (less prominent if cycles detected)
    if not cycles or skip_cycles:
        print_events(data)
    
    print_phase_stats(data)
    
    # Print write configuration from system info
    print_system_write_config(data)
    
    # Analyse histograms
    analysis = analyse_histograms(data)
    print_analysis(analysis)
    
    # Pipeline bottleneck and sync overhead analysis
    pipeline = analyse_pipeline_timing(data)
    print_pipeline_analysis(pipeline)
    
    # Recovery events (queue depth reduction, sync fallback, etc.)
    events = data.get('events', [])
    print_recovery_events(events)
    
    # Detailed write timing breakdown (for hypothesis testing)
    write_breakdown = analyse_write_timing_breakdown(data)
    print_write_timing_breakdown(write_breakdown)
    
    # Generate HTML report
    if generate_html:
        html_path = json_path.with_suffix('.html')
        generate_html_report(data, analysis, html_path, cycles)
    
    # Generate separate graph files
    if save_graphs:
        if HAS_MATPLOTLIB:
            base_path = json_path.with_suffix('')
            
            # Timeline view
            timeline_path = Path(str(base_path) + '-timeline.png')
            plot_timeline(data, timeline_path, cycles)
            
            # Throughput view
            throughput_path = Path(str(base_path) + '-throughput.png')
            plot_throughput(data, analysis, throughput_path, cycles)
            
            # Network operations view
            network_path = Path(str(base_path) + '-network.png')
            plot_network(data, network_path)
        else:
            print("\n[Visualisation unavailable - install matplotlib: pip install matplotlib numpy]")
    
    # Show graphs interactively if neither option specified
    if not save_graphs and not generate_html and HAS_MATPLOTLIB:
        plot_timeline(data, None, cycles)
        plot_throughput(data, analysis, None, cycles)
        plot_network(data, None)
    
    print("\n" + "=" * 60)


if __name__ == '__main__':
    main()

