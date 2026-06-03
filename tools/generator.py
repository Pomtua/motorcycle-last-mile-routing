import csv
import json
import random
import os
import requests
from concurrent.futures import ThreadPoolExecutor, as_completed
from tqdm import tqdm

class CVRPTWGenerator:
    def __init__(self, coordinates_file, osrm_url="http://localhost:5000"):
        self.osrm_url = osrm_url
        self.pool = []
        with open(coordinates_file, mode='r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row in reader:
                self.pool.append({
                    "lng": float(row['snapped_lng']),
                    "lat": float(row['snapped_lat']),
                    "osm_id": row['osm_id']
                })
        print(f"Loaded {len(self.pool)} points. OSRM at {osrm_url}")

    def get_route_durations(self, points):
        """Asks OSRM for travel times (minutes) between points in a sequence."""
        coords = ";".join([f"{p['lng']},{p['lat']}" for p in points])
        try:
            url = f"{self.osrm_url}/route/v1/motorcycle/{coords}?overview=false"
            data = requests.get(url, timeout=5).json()
            if data['code'] == 'Ok':
                return [leg['duration'] / 60 for leg in data['routes'][0]['legs']]
            else:
                print(f"\n[!] Warning: OSRM returned error code {data.get('code')} for coords: {coords[:50]}...")
        except Exception as e:
            print(f"\n[!] Warning: OSRM request failed ({e}). Using 5.0 min fallback.")
        return []

    def sample_points(self, n, sample_type="RC"):
        """Spatial Layout Selection: R (Random), C (Clustered), RC (Mixed)"""
        if sample_type == "R":
            return random.sample(self.pool, n)
        if sample_type == "C":
            num_centers = random.randint(2, 4)
            centers = random.sample(self.pool, num_centers)
            selected = []
            for center in centers:
                sorted_p = sorted(self.pool, key=lambda p: (p['lng']-center['lng'])**2 + (p['lat']-center['lat'])**2)
                selected.extend(sorted_p[1:(n // num_centers) + 1])
            while len(selected) < n: selected.append(random.choice(self.pool))
            return selected
        if sample_type == "RC":
            half = n // 2
            return self.sample_points(half, "R") + self.sample_points(n - half, "C")

    def generate_instance(self, test_type, n_nodes, sample_type="RC", weight_diff="Easy", tw_diff="Easy"):
        max_weight = 100.0
        max_volume = 250.0
        num_drivers = max(1, n_nodes // 10)

        while True:
            try:
                fleet_weight_limit = num_drivers * max_weight * 0.90
                fleet_volume_limit = num_drivers * max_volume * 0.90
                current_fleet_weight = 0.0
                current_fleet_volume = 0.0
                min_splits = max(2, n_nodes // 20) if test_type == "Split Capability" else 0
                oversized_count = 0

                if test_type == "Split Capability":
                    usage_target = 0.45
                else:
                    usage_target = 0.95 if (weight_diff == "Hard") else 0.70

                all_points = self.sample_points(n_nodes + 1, sample_type)
                depot, customers = all_points[0], all_points[1:]

                final_parcels, unassigned = [], list(customers)

                for d in range(num_drivers):
                    if not unassigned: break

                    target_count = min(len(unassigned), 12)
                    selected_batch = random.sample(unassigned, target_count)
                    for p in selected_batch: unassigned.remove(p)

                    route = [depot]
                    remaining_to_sort = selected_batch.copy()
                    while remaining_to_sort:
                        last = route[-1]
                        candidates = sorted(remaining_to_sort, key=lambda p: (p['lng']-last['lng'])**2 + (p['lat']-last['lat'])**2)[:3]
                        next_p = random.choice(candidates)
                        route.append(next_p)
                        remaining_to_sort.remove(next_p)

                    leg_times = self.get_route_durations(route)
                    if not leg_times:
                        bad_point = route[-1]
                        if bad_point in self.pool:
                            self.pool.remove(bad_point)
                        raise ValueError(f"NoRoute at point {bad_point.get('osm_id')}. Pool cleaned (Size: {len(self.pool)})")

                    raw_weights = [random.uniform(5.0, 15.0) for _ in range(target_count)]
                    raw_volumes = [w * random.uniform(4.0, 6.0) for w in raw_weights]

                    w_ratio = sum(raw_weights) / max_weight
                    v_ratio = sum(raw_volumes) / max_volume
                    bottleneck = max(w_ratio, v_ratio)
                    scale = usage_target / bottleneck

                    current_time = 480.0  

                    for i, p in enumerate(route[1:]):
                        weight = raw_weights[i] * scale
                        volume = raw_volumes[i] * scale

                        w, v = weight, volume

                        is_split = False
                        if test_type == "Split Capability":
                            remaining_customers = n_nodes - len(final_parcels)
                            needed_splits = min_splits - oversized_count
                            if needed_splits > 0 and (remaining_customers <= needed_splits or random.random() < 0.2):
                                oversized_w = random.uniform(110.0, 150.0)
                                oversized_v = random.uniform(260.0, 320.0)
                                remaining_drivers = num_drivers - d
                                reserved_factor = 0.20 if test_type == "Split Capability" else usage_target
                                reserved_w = (remaining_drivers - 1) * max_weight * reserved_factor
                                reserved_v = (remaining_drivers - 1) * max_volume * reserved_factor
                                if (current_fleet_weight + oversized_w + reserved_w < fleet_weight_limit and
                                    current_fleet_volume + oversized_v + reserved_v < fleet_volume_limit):
                                    w, v = oversized_w, oversized_v
                                    is_split = True
                                    oversized_count += 1

                        current_fleet_weight += w
                        current_fleet_volume += v

                        arrival = current_time + leg_times[i]

                        tw_range = 30 if tw_diff == "Hard" else 120

                        noise = random.uniform(-10.0, 10.0)
                        shifted_arrival = arrival + noise
                        tw_start = max(480, int(shifted_arrival - tw_range/2))
                        tw_end = min(1200, int(shifted_arrival + tw_range))

                        final_parcels.append({
                            "id": len(final_parcels) + 1,
                            "lng": p['lng'], "lat": p['lat'],
                            "weight": round(w, 2),
                            "volume": round(v, 2),
                            "time_window": [tw_start, tw_end],
                            "is_split_test": is_split
                        })
                        current_time = arrival + 5.0

                total_w = sum(p['weight'] for p in final_parcels)
                total_v = sum(p['volume'] for p in final_parcels)
                if total_w > num_drivers * max_weight or total_v > num_drivers * max_volume:
                    raise ValueError(
                        f"Fleet capacity exceeded after generation: "
                        f"w={total_w:.1f}/{num_drivers * max_weight:.1f}, "
                        f"v={total_v:.1f}/{num_drivers * max_volume:.1f}. Retrying..."
                    )

                if test_type == "Split Capability" and oversized_count < min_splits:
                    raise ValueError(
                        f"Could not generate enough oversized parcels ({oversized_count}/{min_splits}). Retrying..."
                    )

                random.shuffle(final_parcels)

                return {
                    "instance_metadata": {
                        "test_type": test_type,
                        "sample_type": sample_type,
                        "difficulty": "Hard" if (weight_diff == "Hard" or tw_diff == "Hard") else "Easy"
                    },
                    "resources": {
                        "depot_location": depot,
                        "num_drivers": num_drivers,
                        "max_weight_capacity": max_weight,
                        "max_volume_capacity": max_volume
                    },
                    "parcels": final_parcels
                }
            except ValueError as e:
                print(f"\n[!] {e}")
                continue

    def save(self, data, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            json.dump(data, f, indent=4)

    def generate_task(self, args):
        """Helper function to run a single generation task in a thread"""
        test_type, n, path, stype, w, t = args
        inst = self.generate_instance(test_type, n, sample_type=stype, weight_diff=w, tw_diff=t)
        self.save(inst, path)

    def generate_all(self, max_workers=10):
        print(f"Starting Parallel Generation with {max_workers} workers...")
        tasks = []
        
        num_replicates = 2

        for n in [50, 200, 500]:
            for i in range(num_replicates):
                tasks.append(("Split Capability", n, f"data/instances/split_capability/split_{n}_{i+1}.json", "RC", "Easy", "Easy"))

        for n in [50, 200, 500]:
            for w in ["Easy", "Hard"]:
                for t in ["Easy", "Hard"]:
                    for i in range(num_replicates):
                        tasks.append(("Tightness", n, f"data/instances/tightness/tight_W{w[0]}_T{t[0]}_{n}_{i+1}.json", "RC", w, t))

        for n in [50, 100, 200, 500, 1000]:
            for stype in ["C", "R", "RC"]:
                for i in range(num_replicates):
                    tasks.append(("Scalability", n, f"data/instances/scale/scale_{stype}_{n}_{i+1}.json", stype, "Easy", "Easy"))

        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            futures = [executor.submit(self.generate_task, t) for t in tasks]

            for future in tqdm(as_completed(futures), total=len(tasks), desc="Generating Instances"):
                try:
                    future.result() 
                except Exception as e:
                    print(f"\n[!] Error generating instance: {e}")

        print("Success. 300 instances generated in parallel.")

if __name__ == "__main__":
    pool_file = 'data/master_pool/refined_coordinates.csv'
    if os.path.exists(pool_file):
        gen = CVRPTWGenerator(pool_file)
        gen.generate_all()
    else:
        print("Error: Please run snapper.py first.")
