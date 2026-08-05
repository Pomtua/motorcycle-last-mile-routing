import csv
import os
import math
import numpy as np
import rasterio

RASTER_PATH = 'data/raw/tha_pd_2020_1km.tif'
CSV_PATH = 'data/master_pool/refined_coordinates.csv'

def main():
    if not os.path.exists(RASTER_PATH):
        raise FileNotFoundError(f"Raster file not found at {RASTER_PATH}")
    if not os.path.exists(CSV_PATH):
        raise FileNotFoundError(f"Master pool CSV not found at {CSV_PATH}")

    with open(CSV_PATH, mode='r', encoding='utf-8') as f:
        reader = csv.DictReader(f, skipinitialspace=True)
        raw_rows = list(reader)
        fieldnames = [k.strip() for k in reader.fieldnames]

    rows = []
    for r in raw_rows:
        rows.append({k.strip(): v.strip() for k, v in r.items() if k is not None})
        
    if 'worldpop' not in fieldnames:
        fieldnames.append('worldpop')

    coords = []
    for r in rows:
        lng = float(r['snapped_lng'])
        lat = float(r['snapped_lat'])
        coords.append((lng, lat))

    sampled_values = []
    nodata_count = 0

    with rasterio.open(RASTER_PATH) as src:
        nodata_val = src.nodata
        samples = src.sample(coords)
        for i, sample_val in enumerate(samples):
            val = float(sample_val[0])
            if (nodata_val is not None and math.isclose(val, nodata_val)) or math.isnan(val) or val < 0:
                val = 0.0
                nodata_count += 1
            sampled_values.append(val)
            rows[i]['worldpop'] = f"{val:.4f}"

    with open(CSV_PATH, mode='w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    arr = np.array(sampled_values)
    print("=" * 50)
    print("MASTER POOL WORLDPOP ENRICHMENT COMPLETED")
    print("=" * 50)
    print(f"Total Points Processed : {len(rows)}")
    print(f"NoData / Invalid Count : {nodata_count} ({nodata_count / len(rows) * 100:.2f}%)")
    print(f"Min Population         : {arr.min():.4f}")
    print(f"Max Population         : {arr.max():.4f}")
    print(f"Mean Population        : {arr.mean():.4f}")
    print(f"Median Population      : {np.median(arr):.4f}")
    print("=" * 50)

if __name__ == '__main__':
    main()