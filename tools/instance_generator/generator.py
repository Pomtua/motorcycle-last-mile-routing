import csv
import json
import math
import os
import requests
import numpy as np
import hashlib
from tqdm import tqdm

class InstanceGenerator:
    def __init__(self, seed, master_pool_path):
        self.rng = np.random.default_rng(seed)
        self.master_pool_path = master_pool_path
        self.pool = []
        self.pool_by_id = {}
        self.http_session = requests.Session()
        self.service_times = {
            "house": 120,
            "apartments": 240,
            "residential": 150,
            "office": 180,
            "retail": 300,
            "school": 240,
            "university": 240,
            "hospital": 300
        }

    def load_coordinates(self):
        with open(self.master_pool_path, mode='r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            self.pool = list(reader)
        for r in self.pool:
            r['lng'] = float(r['snapped_lng'])
            r['lat'] = float(r['snapped_lat'])
            self.pool_by_id[r['osm_id']] = r

    def select_depot(self):
        candidates = [r for r in self.pool if r['type'] in ('office', 'retail')]
        if not candidates:
            candidates = self.pool
        lats = [r['lat'] for r in self.pool]
        lngs = [r['lng'] for r in self.pool]
        c_lat = sum(lats) / len(lats)
        c_lng = sum(lngs) / len(lngs)
        return min(
            candidates,
            key=lambda r: (r['lat'] - c_lat)**2 + (r['lng'] - c_lng)**2
        )

    def sample_customers(self, n, spatial_class, depot):
        candidates = [r for r in self.pool if r['osm_id'] != depot['osm_id']]
        if spatial_class == 'R':
            indices = self.rng.choice(len(candidates), size=n, replace=False)
            return [candidates[i] for i in indices]
        elif spatial_class == 'C':
            s = max(1, int(math.ceil(n / 25)))
            seed_indices = self.rng.choice(len(candidates), size=s, replace=False)
            seeds = [candidates[i] for i in seed_indices]
            clusters = [[] for _ in range(s)]
            for r in candidates:
                closest_idx, dist = min(
                    ((idx, math.sqrt((r['lat'] - seed['lat'])**2 + (r['lng'] - seed['lng'])**2))
                        for idx, seed in enumerate(seeds)),
                    key=lambda x: x[1]
                )
                prob = math.exp(-dist / 0.05)
                clusters[closest_idx].append((prob, r))
            sampled = []
            per_cluster = n // s
            for idx in range(s):
                cluster_candidates = clusters[idx]
                cluster_candidates.sort(key=lambda x: x[0], reverse=True)
                candidates_only = [x[1] for x in cluster_candidates[:per_cluster * 3]]
                if candidates_only:
                    indices = self.rng.choice(len(candidates_only), size=min(len(candidates_only), per_cluster), replace=False)
                    sampled.extend([candidates_only[i] for i in indices])
            if len(sampled) < n:
                rem = n - len(sampled)
                remaining_candidates = [r for r in candidates if r not in sampled]
                indices = self.rng.choice(len(remaining_candidates), size=rem, replace=False)
                sampled.extend([remaining_candidates[i] for i in indices])
            return sampled[:n]
        elif spatial_class == 'RC':
            n_c = n // 2
            n_r = n - n_c
            sampled_c = self.sample_customers(n_c, 'C', depot)
            remaining_candidates = [r for r in candidates if r not in sampled_c]
            indices = self.rng.choice(len(remaining_candidates), size=n_r, replace=False)
            sampled_r = [remaining_candidates[i] for i in indices]
            return sampled_c + sampled_r

    def generate_demands(self, customers, demand_class, w_max, v_max):
        demands = []
        n = len(customers)
        for i in range(n):
            if demand_class == 'D1':
                w = self.rng.uniform(0.01 * w_max, 0.10 * w_max)
            elif demand_class == 'D2':
                w = self.rng.uniform(0.10 * w_max, 0.30 * w_max)
            elif demand_class == 'D3':
                w = self.rng.uniform(0.10 * w_max, 0.90 * w_max)
            elif demand_class == 'D4':
                w = self.rng.uniform(0.70 * w_max, 1.30 * w_max)
            else:
                w = self.rng.uniform(0.10 * w_max, 0.50 * w_max)
            rho = self.rng.choice([80.0, 200.0, 500.0], p=[0.3, 0.5, 0.2])
            vol = w / rho
            demands.append({
                'osm_id': customers[i]['osm_id'],
                'weight': round(w, 2),
                'volume': round(vol, 4)
            })
        return demands

    def call_osrm_table(self, locations):
        osm_ids = sorted([loc['osm_id'] for loc in locations])
        cache_key = hashlib.md5(",".join(map(str, osm_ids)).encode('utf-8')).hexdigest()
        cache_path = f"infra/osrm/cache/{cache_key}.json"

        if os.path.exists(cache_path):
            with open(cache_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
                return data['durations'], data['distances']
            
        coords_str = ";".join([f"{loc['lng']},{loc['lat']}" for loc in locations])
        url = f"http://localhost:5000/table/v1/motorcycle/{coords_str}?annotations=duration,distance"
        response = self.http_session.get(url)
        res_data = response.json()
        if res_data.get('code') != 'Ok':
            raise Exception("OSRM table call failed")
        
        os.makedirs(os.path.dirname(cache_path), exist_ok=True)
        with open(cache_path, 'w', encoding='utf-8') as f:
            json.dump({
                'durations': res_data['durations'],
                'distances': res_data['distances']
            }, f)
        return res_data['durations'], res_data['distances']

    def generate_reference_routes(self, depot, customers, demands, w_max, v_max, alpha):
        locations = [depot] + customers
        durations, distances = self.call_osrm_table(locations)
        visits = []
        for d in demands:
            w = d['weight']
            v = d['volume']
            m = max(1, int(math.ceil(w / w_max)), int(math.ceil(v / v_max)))
            for chunk_idx in range(m):
                visits.append({
                    'osm_id': d['osm_id'],
                    'original_weight': w,
                    'original_volume': v,
                    'weight': round(w / m, 2),
                    'volume': round(v / m, 4),
                    'chunk_idx': chunk_idx,
                    'total_chunks': m
                })
        cust_id_to_idx = {c['osm_id']: i for i, c in enumerate(customers)}
        for v in visits:
            v['cust_index'] = cust_id_to_idx[v['osm_id']]

        routes = []
        unvisited = list(visits)
        while unvisited:
            current_route = []
            curr_w = 0.0
            curr_v = 0.0
            curr_loc_idx = 0
            while True:
                best_visit = None
                best_dist = float('inf')
                for visit in unvisited:
                    if curr_loc_idx != 0:
                        if curr_w + visit['weight'] > alpha * w_max or curr_v + visit['volume'] > alpha * v_max:
                            continue
                        if any(v['osm_id'] == visit['osm_id'] for v in current_route):
                            continue
                    loc_idx = visit['cust_index'] + 1
                    dist = distances[curr_loc_idx][loc_idx]
                    if dist < best_dist:
                        best_dist = dist
                        best_visit = visit
                if best_visit is not None:
                    current_route.append(best_visit)
                    curr_w += best_visit['weight']
                    curr_v += best_visit['volume']
                    curr_loc_idx = best_visit['cust_index'] + 1
                    unvisited.remove(best_visit)
                else:
                    break
            if current_route:
                routes.append(current_route)
            else:
                break
        return routes, durations, distances

    def simulate_and_refine_routes(self, routes, durations, horizon):
        refined_routes = []
        for r in routes:
            segments = []
            curr_seg = []
            curr_t = 0.0
            prev_loc_idx = 0
            for visit in r:
                loc_idx = visit['cust_index'] + 1
                travel_t = durations[prev_loc_idx][loc_idx]
                cust_type = self.pool_by_id[visit['osm_id']]['type']
                serv_t = self.service_times.get(cust_type, 180)
                arrival_t = curr_t + travel_t
                return_t = arrival_t + serv_t + durations[loc_idx][0]
                if return_t <= horizon:
                    curr_seg.append((visit, arrival_t))
                    curr_t = arrival_t + serv_t
                    prev_loc_idx = loc_idx
                else:
                    if curr_seg:
                        segments.append(curr_seg)
                    curr_seg = [(visit, durations[0][loc_idx])]
                    curr_t = durations[0][loc_idx] + serv_t
                    prev_loc_idx = loc_idx
            if curr_seg:
                segments.append(curr_seg)
            refined_routes.extend(segments)
        return refined_routes

    def generate_time_windows(self, refined_routes, customers, delta_min, delta_max, horizon):
        arrival_map = {}
        for r in refined_routes:
            for visit, arr_t in r:
                osm_id = visit['osm_id']
                if osm_id not in arrival_map:
                    arrival_map[osm_id] = []
                arrival_map[osm_id].append(arr_t)
        time_windows = {}
        for osm_id, arrivals in arrival_map.items():
            t_min = min(arrivals)
            t_max = max(arrivals)
            width = self.rng.uniform(delta_min, delta_max)
            theta = self.rng.uniform(0.0, 1.0)
            a = max(0.0, t_min - theta * width)
            b = min(horizon, a + width)
            a_quant = 300 * math.floor(a / 300)
            b_quant = 300 * math.ceil(b / 300)
            time_windows[osm_id] = {
                'tw_start': int(a_quant),
                'tw_end': int(b_quant)
            }
        return time_windows

    def calculate_difficulty_metrics(self, depot, customers, demands, time_windows, fleet_size, durations, distances, refined_routes, horizon, w_max,
v_max):
        total_w = sum(d['weight'] for d in demands)
        total_v = sum(d['volume'] for d in demands)
        load_f = total_w / (fleet_size * w_max)
        vol_f = total_v / (fleet_size * v_max)
        tw_tights = []
        for osm_id, tw in time_windows.items():
            tw_tights.append((tw['tw_end'] - tw['tw_start']) / horizon)
        mean_tw_tight = sum(tw_tights) / len(tw_tights) if tw_tights else 0.0
        detours = []
        for idx, c in enumerate(customers):
            d_osrm = distances[0][idx + 1]
            dy = (c['lat'] - depot['lat']) * 111300
            dx = (c['lng'] - depot['lng']) * 111300 * math.cos(math.radians(depot['lat']))
            d_eucl = math.sqrt(dx**2 + dy**2)
            if d_eucl > 0:
                detours.append(d_osrm / d_eucl)
        mean_detour = sum(detours) / len(detours) if detours else 1.0
        ref_cost = 0.0
        for r in refined_routes:
            prev_idx = 0
            for visit, _ in r:
                curr_idx = visit['cust_index'] + 1
                ref_cost += distances[prev_idx][curr_idx]
                prev_idx = curr_idx
            ref_cost += distances[prev_idx][0]
        return {
            'load_factor': round(load_f, 4),
            'volume_factor': round(vol_f, 4),
            'tw_tightness': round(mean_tw_tight, 4),
            'detour_ratio': round(mean_detour, 4),
            'reference_cost': round(ref_cost, 2)
        }

    def validate_instance(self, depot, customers, demands, time_windows, fleet_size, durations, distances, refined_routes, horizon, w_max, v_max):
        customer_demands = {d['osm_id']: d['weight'] for d in demands}
        served_weights = {d['osm_id']: 0.0 for d in demands}
        for r in refined_routes:
            r_w = 0.0
            r_v = 0.0
            for visit, _ in r:
                r_w += visit['weight']
                r_v += visit['volume']
                served_weights[visit['osm_id']] += visit['weight']
            if r_w > w_max:
                raise Exception("Route weight exceeds limit")
            if r_v > v_max:
                raise Exception("Route volume exceeds limit")
        for osm_id, w in customer_demands.items():
            if abs(w - served_weights[osm_id]) > 0.05:
                raise Exception("Served weight does not match demand")
        for r in refined_routes:
            curr_t = 0.0
            prev_loc_idx = 0
            for visit, expected_arr in r:
                loc_idx = visit['cust_index'] + 1
                travel_t = durations[prev_loc_idx][loc_idx]
                arrival_t = curr_t + travel_t
                if abs(arrival_t - expected_arr) > 1.0:
                    raise Exception("Arrival time mismatch")
                cust_type = self.pool_by_id[visit['osm_id']]['type']
                serv_t = self.service_times.get(cust_type, 180)
                tw = time_windows[visit['osm_id']]
                if arrival_t < tw['tw_start'] - 300 or arrival_t > tw['tw_end'] + 300:
                    raise Exception("Time window violation")
                curr_t = arrival_t + serv_t
                prev_loc_idx = loc_idx
            return_t = curr_t + durations[prev_loc_idx][0]
            if return_t > horizon + 1.0:
                raise Exception("Route duration exceeds horizon")
        if len(refined_routes) > fleet_size:
            raise Exception("More routes than fleet size")

    def run(self, config, output_path):
        self.rng = np.random.default_rng(config['seed'])
        self.load_coordinates()
        depot = self.select_depot()
        customers = self.sample_customers(config['n'], config['spatial_class'], depot)
        demands = self.generate_demands(customers, config['demand_class'], config['w_max'], config['v_max'])
        routes, durations, distances = self.generate_reference_routes(
            depot, customers, demands, config['w_max'], config['v_max'], config['alpha']
        )
        refined_routes = self.simulate_and_refine_routes(routes, durations, config['horizon'])
        time_windows = self.generate_time_windows(
            refined_routes, customers, config['delta_min'], config['delta_max'], config['horizon']
        )
        fleet_size = int(math.ceil(config['beta'] * len(refined_routes)))
        self.validate_instance(
            depot, customers, demands, time_windows, fleet_size, durations, distances, refined_routes, config['horizon'], config['w_max'],
            config['v_max']
        )
        diff_metrics = self.calculate_difficulty_metrics(
            depot, customers, demands, time_windows, fleet_size, durations, distances, refined_routes, config['horizon'], config['w_max'],
            config['v_max']
        )
        nodes = []
        nodes.append({
            'osm_id': depot['osm_id'],
            'type': depot['type'],
            'lng': depot['lng'],
            'lat': depot['lat'],
            'demand_weight': 0.0,
            'demand_volume': 0.0,
            'tw_start': 0,
            'tw_end': int(config['horizon']),
            'service_time': 0
        })
        for c in customers:
            d = next(x for x in demands if x['osm_id'] == c['osm_id'])
            tw = time_windows[c['osm_id']]
            st = self.service_times.get(c['type'], 180)
            nodes.append({
                'osm_id': c['osm_id'],
                'type': c['type'],
                'lng': c['lng'],
                'lat': c['lat'],
                'demand_weight': d['weight'],
                'demand_volume': d['volume'],
                'tw_start': tw['tw_start'],
                'tw_end': tw['tw_end'],
                'service_time': st
            })
        instance_data = {
            'meta': {
                'seed': config['seed'],
                'n': config['n'],
                'spatial_class': config['spatial_class'],
                'demand_class': config['demand_class'],
                'w_max': config['w_max'],
                'v_max': config['v_max'],
                'horizon': config['horizon'],
                'difficulty': diff_metrics
            },
            'fleet': {
                'size': fleet_size,
                'weight_capacity': config['w_max'],
                'volume_capacity': config['v_max']
            },
            'nodes': nodes,
            'duration_matrix': durations,
            'distance_matrix': distances
        }
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
        with open(output_path, 'w', encoding='utf-8') as f:
            json.dump(instance_data, f, indent=2)
        solution_data = {
            'routes': [
                [{'osm_id': visit['osm_id'], 'chunk_idx': visit['chunk_idx'], 'weight': visit['weight']} for visit, _ in r]
                for r in refined_routes
            ]
        }
        solution_path = output_path.replace('_instance.json', '_solution.json')
        with open(solution_path, 'w', encoding='utf-8') as f:
            json.dump(solution_data, f, indent=2)


def run_task(task_args):
    config, filepath, master_pool_path = task_args
    generator = InstanceGenerator(seed=config['seed'], master_pool_path=master_pool_path)
    try:
        generator.run(config, filepath)
        return True, filepath, None
    except Exception as e:
        return False, filepath, str(e)

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--batch', action='store_true')
    args = parser.parse_args()
    master_pool_path = 'data/master_pool/refined_coordinates.csv'
    if args.batch:
        sizes = [20, 50, 100, 200, 500, 1000]
        spatials = ['R', 'C', 'RC']
        demands = ['D1', 'D2', 'D3', 'D4']
        tw_types = [
            ('tight', 1800.0, 3600.0),
            ('loose', 5400.0, 14400.0)
        ]
        seeds = [42]        
        tasks = []
        for n in sizes:
            for sp in spatials:
                for dm in demands:
                    for tw_name, d_min, d_max in tw_types:
                        for seed in seeds:
                            config = {
                                'seed': seed,
                                'n': n,
                                'spatial_class': sp,
                                'demand_class': dm,
                                'w_max': 50.0,
                                'v_max': 0.125,
                                'horizon': 28800.0,
                                'alpha': 0.85,
                                'beta': 1.1,
                                'delta_min': d_min,
                                'delta_max': d_max
                            }
                            filename = f"{sp}_{dm}_n{n}_{tw_name}_s{seed}_instance.json"
                            filepath = os.path.join('data/instances', filename)
                            tasks.append((config, filepath, master_pool_path))


        print("Pre-fetching OSRM tables for unique customer sets...")
        unique_sets = {}
        for task in tasks:
            config = task[0]
            key = (config['n'], config['spatial_class'], config['seed'])
            if key not in unique_sets:
                unique_sets[key] = config

        generator = InstanceGenerator(seed=42, master_pool_path=master_pool_path)
        generator.load_coordinates()
        for key, config in tqdm(unique_sets.items(), desc="Pre-fetching OSRM"):
            generator.rng = np.random.default_rng(config['seed'])
            depot = generator.select_depot()
            customers = generator.sample_customers(config['n'], config['spatial_class'], depot)
            generator.call_osrm_table([depot] + customers)

        import concurrent.futures
        max_workers = max(1, os.cpu_count())
        print(f"Starting parallel generation with {max_workers} workers (total threads: {os.cpu_count()})")

        total_instances = len(tasks)
        failed_instances = {}
        with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as executor:
            futures = {executor.submit(run_task, task): task for task in tasks}
            pbar = tqdm(concurrent.futures.as_completed(futures), total=total_instances, desc="Generating instances")
            for future in pbar:
                success, filepath, err = future.result()
                if not success:
                    filename = os.path.basename(filepath)
                    failed_instances[filename] = err
                    pbar.write(f"Error {filename}: {err}")

        successful_count = total_instances - len(failed_instances)
        print("\n" + "="*50)
        print("GENERATION SUMMARY")
        print("="*50)
        print("Configurations Structure:")
        print(f"  - Customer Sizes  : {len(sizes)} types {sizes}")
        print(f"  - Spatial Layouts : {len(spatials)} types {spatials}")
        print(f"  - Demand Profiles : {len(demands)} types {demands}")
        print(f"  - Time Windows    : {len(tw_types)} types {[t[0] for t in tw_types]}")
        print(f"  - Seeds per Config: {len(seeds)} (representing {len(seeds)} problem(s) each)")
        print(f"Total Configurations  : {len(sizes) * len(spatials) * len(demands) * len(tw_types)}")
        print(f"Expected Instances    : {total_instances}")
        print(f"Successfully Generated: {successful_count} / {total_instances}")

        if failed_instances:
            print("\nFailed Instances Details:")
            for filename, err in failed_instances.items():
                print(f"  - {filename}: {err}")
        else:
            print("\nStatus: All instances generated successfully! (100% complete)")
        print("="*50)
    else:
        generator = InstanceGenerator(seed=42, master_pool_path=master_pool_path)
        test_config = {
            'seed': 42,
            'n': 50,
            'spatial_class': 'RC',
            'demand_class': 'D3',
            'w_max': 50.0,
            'v_max': 0.125,
            'horizon': 28800.0,
            'alpha': 0.85,
            'beta': 1.1,
            'delta_min': 1800.0,
            'delta_max': 7200.0
        }
        generator.run(test_config, 'data/instances/test_instance.json')
        print("Generated 1/1 instance: test_instance.json")