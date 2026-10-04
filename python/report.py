#!/usr/bin/env python3

import argparse
import csv
import os
import sys
from datetime import datetime


SYSTEM_FIELDS = (
    "timestamp",
    "cpu_pct",
    "mem_pct",
    "swap_pct",
    "disk_pct",
    "load1",
)

PROCESS_FIELDS = (
    "timestamp",
    "pid",
    "name",
    "cpu_pct",
    "mem_pct",
)


def parse_timestamp(value):
    """Parse the contract's local ISO timestamp format."""
    try:
        return datetime.strptime(value, "%Y-%m-%dT%H:%M:%S")
    except (TypeError, ValueError):
        return None


def load_system_csv(path):
    """
    Read system.csv.

    Returns:
        (valid_rows, skipped_count)

    Invalid rows are skipped rather than terminating the report.
    """
    rows = []
    skipped = 0

    try:
        with open(path, "r", newline="", encoding="utf-8") as csv_file:
            reader = csv.DictReader(csv_file)

            if reader.fieldnames is None:
                return [], 0

            missing_fields = [
                field for field in SYSTEM_FIELDS
                if field not in reader.fieldnames
            ]

            if missing_fields:
                raise ValueError(
                    "system.csv is missing required columns: "
                    + ", ".join(missing_fields)
                )

            for row in reader:
                if not row:
                    skipped += 1
                    continue

                try:
                    timestamp_text = row["timestamp"].strip()

                    if not timestamp_text:
                        raise ValueError

                    timestamp = parse_timestamp(timestamp_text)

                    if timestamp is None:
                        raise ValueError

                    cpu_pct = float(row["cpu_pct"])
                    mem_pct = float(row["mem_pct"])
                    swap_pct = float(row["swap_pct"])
                    disk_pct = float(row["disk_pct"])
                    load1 = float(row["load1"])

                    rows.append(
                        {
                            "timestamp": timestamp,
                            "timestamp_text": timestamp_text,
                            "cpu_pct": cpu_pct,
                            "mem_pct": mem_pct,
                            "swap_pct": swap_pct,
                            "disk_pct": disk_pct,
                            "load1": load1,
                        }
                    )

                except (KeyError, TypeError, ValueError):
                    skipped += 1

    except OSError as exc:
        raise OSError(
            "cannot read system CSV '{}': {}".format(path, exc)
        )

    return rows, skipped


def load_process_csv(path):
    """
    Read processes.csv.

    Returns:
        (valid_rows, skipped_count)
    """
    rows = []
    skipped = 0

    try:
        with open(path, "r", newline="", encoding="utf-8") as csv_file:
            reader = csv.DictReader(csv_file)

            if reader.fieldnames is None:
                return [], 0

            missing_fields = [
                field for field in PROCESS_FIELDS
                if field not in reader.fieldnames
            ]

            if missing_fields:
                raise ValueError(
                    "processes.csv is missing required columns: "
                    + ", ".join(missing_fields)
                )

            for row in reader:
                if not row:
                    skipped += 1
                    continue

                try:
                    timestamp_text = row["timestamp"].strip()

                    if not timestamp_text:
                        raise ValueError

                    timestamp = parse_timestamp(timestamp_text)

                    if timestamp is None:
                        raise ValueError

                    pid = int(row["pid"])

                    if pid < 0:
                        raise ValueError

                    name = row["name"].strip()

                    if not name:
                        raise ValueError

                    cpu_pct = float(row["cpu_pct"])
                    mem_pct = float(row["mem_pct"])

                    rows.append(
                        {
                            "timestamp": timestamp,
                            "timestamp_text": timestamp_text,
                            "pid": pid,
                            "name": name,
                            "cpu_pct": cpu_pct,
                            "mem_pct": mem_pct,
                        }
                    )

                except (KeyError, TypeError, ValueError):
                    skipped += 1

    except OSError as exc:
        raise OSError(
            "cannot read process CSV '{}': {}".format(path, exc)
        )

    return rows, skipped


def calculate_system_stats(rows):
    """Calculate system-level statistics from valid rows."""

    cpu_values = [row["cpu_pct"] for row in rows]
    mem_values = [row["mem_pct"] for row in rows]
    swap_values = [row["swap_pct"] for row in rows]
    load_values = [row["load1"] for row in rows]

    first_row = rows[0]
    last_row = rows[-1]

    peak_cpu_row = max(rows, key=lambda row: row["cpu_pct"])
    peak_mem_row = max(rows, key=lambda row: row["mem_pct"])
    peak_swap_row = max(rows, key=lambda row: row["swap_pct"])
    peak_load_row = max(rows, key=lambda row: row["load1"])

    duration = last_row["timestamp"] - first_row["timestamp"]

    return {
        "samples": len(rows),

        "first_timestamp": first_row["timestamp_text"],
        "last_timestamp": last_row["timestamp_text"],
        "duration_seconds": duration.total_seconds(),

        "average_cpu": sum(cpu_values) / len(cpu_values),
        "peak_cpu": peak_cpu_row["cpu_pct"],
        "peak_cpu_timestamp": peak_cpu_row["timestamp_text"],

        "average_memory": sum(mem_values) / len(mem_values),
        "peak_memory": peak_mem_row["mem_pct"],
        "peak_memory_timestamp": peak_mem_row["timestamp_text"],

        "average_swap": sum(swap_values) / len(swap_values),
        "peak_swap": peak_swap_row["swap_pct"],

        "last_disk": last_row["disk_pct"],

        "average_load": sum(load_values) / len(load_values),
        "peak_load": peak_load_row["load1"],
        "peak_load_timestamp": peak_load_row["timestamp_text"],
    }


def print_system_stats(stats):
    """Print the Stage 2 system statistics."""

    duration = stats["duration_seconds"]

    print("System Statistics")
    print("=================")

    print("Samples: {}".format(stats["samples"]))
    print("First timestamp: {}".format(stats["first_timestamp"]))
    print("Last timestamp: {}".format(stats["last_timestamp"]))
    print("Duration: {:.1f} seconds".format(duration))

    print()
    print("CPU:")
    print("  Average: {:.1f}%".format(stats["average_cpu"]))
    print(
        "  Peak: {:.1f}% at {}".format(
            stats["peak_cpu"],
            stats["peak_cpu_timestamp"],
        )
    )

    print()
    print("Memory:")
    print("  Average: {:.1f}%".format(stats["average_memory"]))
    print(
        "  Peak: {:.1f}% at {}".format(
            stats["peak_memory"],
            stats["peak_memory_timestamp"],
        )
    )

    print()
    print("Swap:")
    print("  Average: {:.1f}%".format(stats["average_swap"]))
    print("  Peak: {:.1f}%".format(stats["peak_swap"]))

    print()
    print("Disk:")
    print("  Last: {:.1f}%".format(stats["last_disk"]))

    print()
    print("Load:")
    print("  Average: {:.2f}".format(stats["average_load"]))
    print(
        "  Peak: {:.2f} at {}".format(
            stats["peak_load"],
            stats["peak_load_timestamp"],
        )
    )


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Generate a report from Linux system monitor CSV data."
    )

    parser.add_argument(
        "system_csv",
        help="path to system.csv",
    )

    parser.add_argument(
        "--procs",
        help="path to processes.csv; defaults to system.csv directory",
    )

    parser.add_argument(
        "--json",
        dest="json_file",
        help="write report JSON to this file",
    )

    parser.add_argument(
        "--csv",
        dest="csv_file",
        help="write report CSV to this file",
    )

    return parser.parse_args()


def main():
    args = parse_arguments()

    if args.procs is None:
        system_directory = os.path.dirname(
            os.path.abspath(args.system_csv)
        )
        process_csv = os.path.join(
            system_directory,
            "processes.csv",
        )
    else:
        process_csv = args.procs

    try:
        system_rows, system_skipped = load_system_csv(
            args.system_csv
        )

        process_rows = []
        process_skipped = 0

        if os.path.exists(process_csv):
            process_rows, process_skipped = load_process_csv(
                process_csv
            )

    except (OSError, ValueError) as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return 1

    total_skipped = system_skipped + process_skipped

    if total_skipped > 0:
        print(
            "warning: skipped {} malformed row(s).".format(
                total_skipped
            ),
            file=sys.stderr,
        )

    if not system_rows:
        print("no valid samples", file=sys.stderr)
        return 1

    stats = calculate_system_stats(system_rows)

    print_system_stats(stats)

    return 0


if __name__ == "__main__":
    sys.exit(main())