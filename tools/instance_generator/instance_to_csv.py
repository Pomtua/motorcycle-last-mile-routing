import argparse
import csv
import json
import sys
from pathlib import Path

COLUMNS = ["type", "id", "lat", "lng", "weight_kg", "volume_l", "tw_start", "tw_end", "service_min"]


def clock(shift_start_s: int, offset_s: float) -> str:
    total = shift_start_s + int(round(offset_s))
    if total >= 24 * 3600:
        raise ValueError(f"time {offset_s} s after the shift start passes midnight")
    hours, rest = divmod(total, 3600)
    minutes, seconds = divmod(rest, 60)
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}" if seconds else f"{hours:02d}:{minutes:02d}"


def parse_shift_start(text: str) -> int:
    hours, minutes = text.split(":")
    return int(hours) * 3600 + int(minutes) * 60


def convert(instance: dict, shift_start_s: int, limit: int | None) -> list[list[str]]:
    nodes = instance["nodes"]
    depot, customers = nodes[0], nodes[1:]
    if limit is not None:
        customers = customers[:limit]
    rows = [["depot", "DEPOT", f"{depot['lat']:.6f}", f"{depot['lng']:.6f}", "", "", "", "", ""]]
    seen: dict[str, int] = {}
    for index, node in enumerate(customers, start=1):
        base = str(node.get("osm_id") or f"P{index:04d}")
        seen[base] = seen.get(base, 0) + 1
        parcel_id = base if seen[base] == 1 else f"{base}-{seen[base]}"
        rows.append(
            [
                "customer",
                parcel_id,
                f"{node['lat']:.6f}",
                f"{node['lng']:.6f}",
                f"{node['demand_weight']:g}",
                f"{node['demand_volume'] * 1000:g}",
                clock(shift_start_s, node["tw_start"]),
                clock(shift_start_s, node["tw_end"]),
                f"{node['service_time'] / 60:g}",
            ]
        )
    return rows


def fleet_row(name: str, instance: dict, parcels: int, shift_start_s: int) -> list[str]:
    fleet = instance["fleet"]
    return [
        name,
        str(parcels),
        str(fleet["size"]),
        f"{fleet['weight_capacity']:g}",
        f"{fleet['volume_capacity'] * 1000:g}",
        clock(shift_start_s, 0),
        clock(shift_start_s, instance["meta"]["horizon"]),
    ]


FLEET_COLUMNS = ["file", "parcels", "riders", "max_weight_kg", "max_volume_l", "shift_start", "shift_end"]


def write_csv(path: Path, rows: list[list[str]]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(COLUMNS)
        writer.writerows(rows)


def convert_directory(source: Path, output: Path, shift_start_s: int, limit: int | None) -> None:
    files = sorted(source.glob("*_instance.json"))
    if not files:
        raise SystemExit(f"no *_instance.json files in {source}")
    output.mkdir(parents=True, exist_ok=True)
    fleets = []
    for path in files:
        instance = json.loads(path.read_text())
        name = path.name.removesuffix("_instance.json") + ".csv"
        rows = convert(instance, shift_start_s, limit)
        write_csv(output / name, rows)
        fleets.append(fleet_row(name, instance, len(rows) - 1, shift_start_s))
    with (output / "fleets.csv").open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(FLEET_COLUMNS)
        writer.writerows(fleets)
    print(f"wrote {len(files)} parcel CSVs and fleets.csv to {output}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert benchmark instance JSON into parcel CSVs for the web dashboard. "
        "Give a file and an output CSV, or a directory and an output directory."
    )
    parser.add_argument("instance", type=Path, help="an *_instance.json file or a directory of them")
    parser.add_argument("output", type=Path, help="output CSV file, or output directory when converting a directory")
    parser.add_argument("--shift-start", default="08:00", help="clock time of t = 0 in the instance (default 08:00)")
    parser.add_argument("--limit", type=int, default=None, help="keep only the first N customers")
    args = parser.parse_args()
    shift_start = parse_shift_start(args.shift_start)

    if args.instance.is_dir():
        convert_directory(args.instance, args.output, shift_start, args.limit)
        return 0

    instance = json.loads(args.instance.read_text())
    rows = convert(instance, shift_start, args.limit)
    write_csv(args.output, rows)
    fleet = dict(zip(FLEET_COLUMNS, fleet_row(args.output.name, instance, len(rows) - 1, shift_start)))
    print(f"wrote {len(rows) - 1} parcels to {args.output}")
    print(
        f"fleet from the instance: riders {fleet['riders']}, max weight {fleet['max_weight_kg']} kg, "
        f"max volume {fleet['max_volume_l']} L, shift {fleet['shift_start']}-{fleet['shift_end']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
