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
        'driveUnmount': '#9C27B0', 'driveFormat': '#9C27B0',
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
        # Drive (purple)
        'driveListPoll': '#7B1FA2', 'driveOpen': '#7B1FA2', 
        'driveUnmount': '#7B1FA2', 'driveFormat': '#7B1FA2',
        # Cache (cyan)
        'cacheLookup': '#00838F', 'cacheVerification': '#00838F',
        'cacheWrite': '#00838F', 'cacheFlush': '#00838F',
        # Memory (grey)
        'memoryAllocation': '#455A64', 'bufferResize': '#455A64',
        'pageCacheFlush': '#455A64',
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


def main():
    if len(sys.argv) < 2:
        print("Usage: python analyse_performance.py <performance-data.json> [--save-graphs]")
        print("\nOptions:")
        print("  --save-graphs   Save graphs as PNG files instead of displaying")
        print("\nOutput files (with --save-graphs):")
        print("  <input>-timeline.png    Operation sequence timeline (Gantt chart)")
        print("  <input>-throughput.png  Throughput over time with event overlays")
        print("  <input>-network.png     Network & cache operations with throughput")
        sys.exit(1)
    
    json_path = Path(sys.argv[1])
    save_graphs = '--save-graphs' in sys.argv or '--save-graph' in sys.argv
    
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
    
    # Generate visualisations
    if HAS_MATPLOTLIB:
        base_path = json_path.with_suffix('')
        
        # Timeline view
        timeline_path = Path(str(base_path) + '-timeline.png') if save_graphs else None
        plot_timeline(data, timeline_path)
        
        # Throughput view
        throughput_path = Path(str(base_path) + '-throughput.png') if save_graphs else None
        plot_throughput(data, analysis, throughput_path)
        
        # Network operations view
        network_path = Path(str(base_path) + '-network.png') if save_graphs else None
        plot_network(data, network_path)
    else:
        print("\n[Visualisation unavailable - install matplotlib: pip install matplotlib numpy]")
    
    print("\n" + "=" * 60)


if __name__ == '__main__':
    main()

