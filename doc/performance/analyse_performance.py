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
    print(f"Result:   {'[OK] Success' if summary.get('success') else '[FAIL] Failed'}")
    
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


def plot_timeline(data: dict, output_path: Path = None) -> None:
    """Plot a timeline/Gantt chart showing the sequence of all operations."""
    if not HAS_MATPLOTLIB:
        return
    
    events = data.get('events', [])
    histograms = data.get('histograms', {})
    summary = data.get('summary', {})
    
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
        # Cache operations
        'cacheLookup': '#00BCD4', 'cacheVerification': '#00BCD4',
        'cacheWrite': '#00BCD4', 'cacheFlush': '#00BCD4',
        # Memory management
        'memoryAllocation': '#607D8B', 'bufferResize': '#607D8B',
        'pageCacheFlush': '#607D8B',
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
    ax.legend(handles=legend_elements, loc='upper right', fontsize=8)
    
    plt.tight_layout()
    
    if output_path:
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        print(f"[Timeline saved to {output_path}]")
    else:
        plt.show()


def plot_throughput(data: dict, analysis: dict, output_path: Path = None) -> None:
    """Plot throughput over time for each phase, with event markers overlaid."""
    if not HAS_MATPLOTLIB:
        print("\n[Visualisation unavailable - install matplotlib: pip install matplotlib numpy]")
        return
    
    histograms = data.get('histograms', {})
    events = data.get('events', [])
    
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
        # Cache (cyan)
        'cacheLookup': '#00838F', 'cacheVerification': '#00838F',
        'cacheWrite': '#00838F', 'cacheFlush': '#00838F',
        # Memory (grey) - ringBufferStarvation in RED to highlight issues
        'memoryAllocation': '#455A64', 'bufferResize': '#455A64',
        'pageCacheFlush': '#455A64', 'ringBufferStarvation': '#D32F2F',
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


def generate_html_report(data: dict, analysis: dict, output_path: Path) -> None:
    """Generate a self-contained HTML report with embedded graphs and data."""
    summary = data.get('summary', {})
    events = data.get('events', [])
    system = data.get('system', {})
    
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

    # Summary section
    success = summary.get('success', False)
    status_class = 'success' if success else 'error'
    status_text = 'Success' if success else 'Failed'
    
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
        print("\nOutput files:")
        print("  --html          <input>.html              Complete report with embedded graphs")
        print("  --save-graphs   <input>-timeline.png      Operation sequence timeline")
        print("                  <input>-throughput.png    Throughput over time")
        print("                  <input>-network.png       Network & cache operations")
        sys.exit(1)
    
    json_path = Path(sys.argv[1])
    save_graphs = '--save-graphs' in sys.argv or '--save-graph' in sys.argv
    generate_html = '--html' in sys.argv
    
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
    print_events(data)
    print_phase_stats(data)
    
    # Analyse histograms
    analysis = analyse_histograms(data)
    print_analysis(analysis)
    
    # Generate HTML report
    if generate_html:
        html_path = json_path.with_suffix('.html')
        generate_html_report(data, analysis, html_path)
    
    # Generate separate graph files
    if save_graphs:
        if HAS_MATPLOTLIB:
            base_path = json_path.with_suffix('')
            
            # Timeline view
            timeline_path = Path(str(base_path) + '-timeline.png')
            plot_timeline(data, timeline_path)
            
            # Throughput view
            throughput_path = Path(str(base_path) + '-throughput.png')
            plot_throughput(data, analysis, throughput_path)
            
            # Network operations view
            network_path = Path(str(base_path) + '-network.png')
            plot_network(data, network_path)
        else:
            print("\n[Visualisation unavailable - install matplotlib: pip install matplotlib numpy]")
    
    # Show graphs interactively if neither option specified
    if not save_graphs and not generate_html and HAS_MATPLOTLIB:
        plot_timeline(data, None)
        plot_throughput(data, analysis, None)
        plot_network(data, None)
    
    print("\n" + "=" * 60)


if __name__ == '__main__':
    main()

