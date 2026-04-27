import csv
import json

csv_file = 'data/master_pool/refined_coordinates.csv'
geojson_file = 'data/master_pool/points_visualization.geojson'

features = []
with open(csv_file, mode='r', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    for row in reader:
        feature = {
            "type": "Feature",
            "properties": {"osm_id": row['osm_id'], "dist": row['snap_dist']},
            "geometry": {
                "type": "Point",
                "coordinates": [float(row['snapped_lng']), float(row['snapped_lat'])]
            }
        }
        features.append(feature)

geojson = {"type": "FeatureCollection", "features": features}

with open(geojson_file, 'w') as f:
    json.dump(geojson, f)

print(f"Generated {geojson_file}. Drag and drop this file into https://geojson.io to see the points!")