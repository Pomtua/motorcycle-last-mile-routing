import argparse
import csv
import os
import statistics

import requests
from tqdm import tqdm

DEFAULT_OSRM_URL = "http://localhost:5000"
DEFAULT_REFERENCE = (100.538306, 13.764943)
MAX_TABLE_SIZE = 2000


def compute_adaptive_threshold(distances: list[float], method: str = "iqr") -> float:
    """
    Adaptive threshold derived from snap distance distribution.
    Outliers are identified using Tukey's fence (Q3 + 1.5*IQR)
    Ref: J. W. Tukey, Exploratory Data Analysis, 1977.
    """
    if method == "iqr":
        q1 = statistics.quantiles(distances, n=4)[0]
        q3 = statistics.quantiles(distances, n=4)[2]
        iqr = q3 - q1
        threshold = q3 + 1.5 * iqr
    elif method == "percentile":
        sorted_d = sorted(distances)
        idx = int(0.95 * len(sorted_d))
        threshold = sorted_d[idx]
    else:
        raise ValueError(f"Unknown method: {method}")

    return round(threshold, 2)


def routable_snaps(session, osrm_url, reference, points):
    """
    Snap points the way OSRM routes them: a /table request from a reference point on the
    main road network. Points whose nearest road is cut off from that network are moved to
    the nearest connected road, which is the location every distance matrix will use.
    """
    chunk_size = MAX_TABLE_SIZE - 1
    snaps = []
    for start in tqdm(range(0, len(points), chunk_size), desc="Querying OSRM /table"):
        chunk = points[start:start + chunk_size]
        coords = ";".join(f"{lng},{lat}" for lng, lat in [reference] + chunk)
        response = session.get(
            f"{osrm_url}/table/v1/motorcycle/{coords}",
            params={"sources": "0", "annotations": "distance"},
            timeout=300,
        )
        response.raise_for_status()
        data = response.json()
        if data.get("code") != "Ok":
            raise RuntimeError(f"OSRM /table failed: {data.get('code')} {data.get('message', '')}")
        destinations = data["destinations"][1:]
        distances = data["distances"][0][1:]
        if len(destinations) != len(chunk):
            raise RuntimeError("OSRM /table returned the wrong number of snapped points")
        for waypoint, road_distance in zip(destinations, distances):
            snaps.append({
                "location": waypoint["location"],
                "distance": waypoint["distance"],
                "reachable": road_distance is not None,
            })
    return snaps


def snap_points(input_file, output_file, osrm_url, reference):
    if not os.path.exists(input_file):
        print(f"Error: File {input_file} not found. Please run the database extraction script first.")
        return

    with open(input_file, mode='r', encoding='utf-8') as f:
        raw_points = list(csv.DictReader(f))

    print(f"Pass 1/2 — Snapping {len(raw_points)} points onto the routable road network...")
    with requests.Session() as session:
        snaps = routable_snaps(
            session, osrm_url, reference, [(float(row['lng']), float(row['lat'])) for row in raw_points]
        )

    snapped_rows = []
    unreachable = 0
    for row, snap in zip(raw_points, snaps):
        if not snap["reachable"]:
            unreachable += 1
            continue
        row['snapped_lng'] = snap["location"][0]
        row['snapped_lat'] = snap["location"][1]
        row['snap_dist'] = round(snap["distance"], 2)
        snapped_rows.append(row)

    if not snapped_rows:
        print("\nWarning: No points were successfully snapped. Verify that the OSRM container is running.")
        return

    all_distances = [row['snap_dist'] for row in snapped_rows]
    threshold = compute_adaptive_threshold(all_distances, method="iqr")

    print(f"\nPass 2/2 — Filtering outliers")
    print(f"  Unreachable from the reference point: {unreachable}")
    print(f"  Snap distance stats : min={min(all_distances):.1f}m  "
          f"median={statistics.median(all_distances):.1f}m  "
          f"max={max(all_distances):.1f}m")
    print(f"  Adaptive threshold  : {threshold:.1f}m  ")

    refined_data = [r for r in snapped_rows if r['snap_dist'] <= threshold]
    removed = len(snapped_rows) - len(refined_data)
    print(f"  Removed {removed} outlier point(s) ({removed/len(snapped_rows)*100:.1f}%)")

    fieldnames = list(refined_data[0].keys())
    with open(output_file, mode='w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(refined_data)

    print(f"\nDone. Saved {len(refined_data)} points → {output_file}")


def main():
    parser = argparse.ArgumentParser(description="Snap raw coordinates onto the routable motorcycle road network.")
    parser.add_argument("--input", default="data/master_pool/raw_coordinates.csv")
    parser.add_argument("--output", default="data/master_pool/refined_coordinates.csv")
    parser.add_argument("--osrm-url", default=os.environ.get("OSRM_BASE_URL", DEFAULT_OSRM_URL))
    parser.add_argument("--reference", type=float, nargs=2, metavar=("LNG", "LAT"), default=DEFAULT_REFERENCE,
                        help="a point on the main road network (default: Victory Monument, Bangkok)")
    args = parser.parse_args()
    snap_points(args.input, args.output, args.osrm_url.rstrip("/"), tuple(args.reference))


if __name__ == "__main__":
    main()
