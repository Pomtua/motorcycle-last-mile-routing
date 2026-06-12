import csv
import requests
import os
import statistics
from tqdm import tqdm

OSRM_URL = "http://localhost:5000/nearest/v1/motorcycle/"

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

def snap_points():
    input_file = 'data/master_pool/raw_coordinates.csv'
    output_file = 'data/master_pool/refined_coordinates.csv'
    
    if not os.path.exists(input_file):
        print(f"Error: File {input_file} not found. Please run the database extraction script first.")
        return

    raw_points = []
    with open(input_file, mode='r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        raw_points = list(reader)

    print(f"Pass 1/2 — Fetching snap distances for {len(raw_points)} points...")
    snapped_rows = []
    all_distances = []

    for row in tqdm(raw_points, desc="Querying OSRM"):
        lng = row['lng']
        lat = row['lat']

        try:
            response = requests.get(f"{OSRM_URL}{lng},{lat}?number=1", timeout=5)
            data = response.json()

            if data.get('code') == 'Ok' and data.get('waypoints'):
                waypoint = data['waypoints'][0]
                
                row['snapped_lng'] = waypoint['location'][0]
                row['snapped_lat'] = waypoint['location'][1]
                row['snap_dist'] = round(waypoint['distance'], 2)

                snapped_rows.append(row)
                all_distances.append(row['snap_dist'])
        except Exception:
            continue
    

    if not snapped_rows:
        print("\nWarning: No points were successfully snapped. Verify that the OSRM container is running.")
        return
    
    threshold = compute_adaptive_threshold(all_distances, method="iqr")

    print(f"\nPass 2/2 — Filtering outliers")
    print(f"  Snap distance stats : min={min(all_distances):.1f}m  "
          f"median={statistics.median(all_distances):.1f}m  "
          f"max={max(all_distances):.1f}m")
    print(f"  Adaptive threshold  : {threshold:.1f}m  ")

    refined_data = [r for r in snapped_rows if r['snap_dist'] <= threshold]
    removed = len(snapped_rows) - len(refined_data)
    print(f"  Removed {removed} outlier point(s) ({removed/len(snapped_rows)*100:.1f}%)")
    
    if refined_data:
        fieldnames = list(refined_data[0].keys())
        
        with open(output_file, mode='w', encoding='utf-8', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(refined_data)
        
        print(f"\nDone. Saved {len(refined_data)} points → {output_file}")
    else:
        print("\nWarning: No points were successfully snapped. Verify that the OSRM container is running.")

if __name__ == "__main__":
    snap_points()