import csv
import requests
import os
from tqdm import tqdm

OSRM_URL = "http://localhost:5000/nearest/v1/motorcycle/"

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

    print(f"Starting snapping process for {len(raw_points)} points...")
    refined_data = []

    for row in tqdm(raw_points, desc="Processing"):
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

                if row['snap_dist'] > 30:
                       continue
                
                refined_data.append(row)
            else:
                continue

        except Exception:
            continue

    if refined_data:
        fieldnames = list(refined_data[0].keys())
        
        with open(output_file, mode='w', encoding='utf-8', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(refined_data)
        
        print(f"\nTask completed. Refined Master Pool saved at: {output_file}")
    else:
        print("\nWarning: No points were successfully snapped. Verify that the OSRM container is running.")

if __name__ == "__main__":
    snap_points()