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
                # Select closest neighbors for each cluster center
                sorted_p = sorted(self.pool, key=lambda p: (p['lng']-center['lng'])**2 + (p['lat']-center['lat'])**2)
                selected.extend(sorted_p[1:(n // num_centers) + 1])
            while len(selected) < n: selected.append(random.choice(self.pool))
            return selected
        if sample_type == "RC":
            half = n // 2
            return self.sample_points(half, "R") + self.sample_points(n - half, "C")

    def generate_instance(self, test_type, n_nodes, sample_type="RC", weight_diff="Easy", tw_diff="Easy"):
        # 1. Setup Constraints
        max_weight = 100.0
        max_volume = 250.0
        num_drivers = max(1, n_nodes // 10)

        while True:
            try:
                fleet_weight_limit = num_drivers * max_weight * 0.90
                fleet_volume_limit = num_drivers * max_volume * 0.90
                current_fleet_weight = 0.0
                current_fleet_volume = 0.0

                if test_type == "Split Capability":
                    usage_target = 0.60
                else:
                    usage_target = 0.95 if (weight_diff == "Hard") else 0.70

                all_points = self.sample_points(n_nodes + 1, sample_type)
                depot, customers = all_points[0], all_points[1:]

                final_parcels, unassigned = [], list(customers)

                for d in range(num_drivers):
                    if not unassigned: break
                    route = [depot]
                    target_count = min(len(unassigned), 12)

                    for _ in range(target_count):
                        last = route[-1]
                        # Find closest unassigned customer (Greedy Proximity)
                        closest = min(unassigned, key=lambda p: (p['lng']-last['lng'])**2 + (p['lat']-last['lat'])**2)
                        route.append(closest)
                        unassigned.remove(closest)

                    leg_times = self.get_route_durations(route)
                    if not leg_times:
                        bad_point = route[-1]
                        if bad_point in self.pool:
                            self.pool.remove(bad_point)
                        raise ValueError(f"NoRoute at point {bad_point.get('osm_id')}. Pool cleaned (Size: {len(self.pool)})")

                    raw_weights = [random.uniform(5.0, 15.0) for _ in range(target_count)]
                    raw_volumes = [w * random.uniform(4.0, 6.0) for w in raw_weights]

                    # Find which constraint is the bottleneck for this specific route
                    w_ratio = sum(raw_weights) / max_weight
                    v_ratio = sum(raw_volumes) / max_volume
                    # Scale everything down so the bottleneck perfectly hits our usage_target
                    bottleneck = max(w_ratio, v_ratio)
                    scale = usage_target / bottleneck

                    leg_times = self.get_route_durations(route)
                    current_time = 480.0  # 8:00 AM

                    for i, p in enumerate(route[1:]):
                        # Apply the feasibility scale
                        weight = raw_weights[i] * scale
                        volume = raw_volumes[i] * scale

                        w, v = weight, volume

                        # Special Case: Split Capability
                        # We intentionally break the feasibility of one parcel to force a split
                        is_split = False
                        if test_type == "Split Capability" and random.random() < 0.2:
                            oversized_w = random.uniform(110.0, 150.0)
                            oversized_v = oversized_w * random.uniform(4.0, 6.0)

                            # Only use oversized if it fits in the fleet's 90% budget
                            if (current_fleet_weight + oversized_w < fleet_weight_limit and
                                current_fleet_volume + oversized_v < fleet_volume_limit):
                                w, v = oversized_w, oversized_v
                                is_split = True

                        current_fleet_weight += w
                        current_fleet_volume += v
                        
                        arrival = current_time + leg_times[i]

                        # Set Time Windows around the arrival
                        tw_range = 30 if tw_diff == "Hard" else 120
                        
                        tw_start = max(480, int(arrival - tw_range/2))
                        tw_end = min(1200, int(arrival + tw_range))

                        final_parcels.append({
                            "id": len(final_parcels) + 1,
                            "lng": p['lng'], "lat": p['lat'],
                            "weight": round(w, 2),
                            "volume": round(v, 2),
                            "time_window": [tw_start, tw_end],
                            "is_split_test": is_split # Helper tag for your analysis
                        })
                        # Advance time (Travel + 5 min service)
                        current_time = arrival + 5.0

                # Shuffle parcels so the algorithm doesn't know the seed order
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

        # 1. Split Delivery
        for n in [50, 200, 500]:
            for i in range(10):
                tasks.append(("Split Capability", n, f"data/instances/split_capability/split_{n}_{i+1}.json", "RC", "Easy", "Easy"))

        # 2. Tightness
        for n in [50, 200, 500]:
            for w in ["Easy", "Hard"]:
                for t in ["Easy", "Hard"]:
                    for i in range(10):
                        tasks.append(("Tightness", n, f"data/instances/tightness/tight_W{w[0]}_T{t[0]}_{n}_{i+1}.json", "RC", w, t))

        # 3. Scalability
        for n in [50, 100, 200, 500, 1000]:
            for stype in ["C", "R", "RC"]:
                for i in range(10):
                    tasks.append(("Scalability", n, f"data/instances/scale/scale_{stype}_{n}_{i+1}.json", stype, "Easy", "Easy"))

        # Execute tasks in parallel
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            futures = [executor.submit(self.generate_task, t) for t in tasks]

            # Use tqdm to track progress as they complete
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