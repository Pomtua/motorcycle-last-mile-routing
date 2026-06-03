import json
import argparse
import sys

def calculate_utilization(instance_file, result_file):
    with open(instance_file, 'r') as f:
        instance = json.load(f)
        
    with open(result_file, 'r') as f:
        result = json.load(f)
        
    vehicle_capacity = {v['id']: {'weight': v['capacity_weight'], 'volume': v['capacity_volume']} for v in instance['vehicles']}
    parcel_props = {p['id']: {'weight': p['weight'], 'volume': p['volume'], 'tw_start': p['time_window']['start'], 'tw_end': p['time_window']['end']} for p in instance['parcels']}
    
    print("--- Vehicle Utilization Report ---")
    
    total_w_util = 0
    total_v_util = 0
    route_count = len(result['routes'])
    late_deliveries = 0
    total_deliveries = 0
    
    for route in result['routes']:
        vid = route['vehicle_id']
        cap_w = vehicle_capacity[vid]['weight']
        cap_v = vehicle_capacity[vid]['volume']
        
        used_w = sum(parcel_props[pid]['weight'] for pid in route['parcel_ids'])
        used_v = sum(parcel_props[pid]['volume'] for pid in route['parcel_ids'])
        
        w_util = (used_w / cap_w) * 100 if cap_w > 0 else 0
        v_util = (used_v / cap_v) * 100 if cap_v > 0 else 0
        
        total_w_util += w_util
        total_v_util += v_util
        
        print(f"Vehicle {vid}: Weight = {w_util:.2f}% ({used_w:.1f}/{cap_w:.1f}), Volume = {v_util:.2f}% ({used_v:.1f}/{cap_v:.1f})")
        
        for i, pid in enumerate(route['parcel_ids']):
            arrival = route['arrival_times'][i + 1] # index 0 is depot
            tw_end = parcel_props[pid]['tw_end']
            if arrival > tw_end:
                late_deliveries += 1
            total_deliveries += 1
            
    print("-" * 30)
    print(f"Average Fleet Weight Util: {total_w_util/route_count:.2f}%")
    print(f"Average Fleet Volume Util: {total_v_util/route_count:.2f}%")
    print(f"Time Window Adherence: {total_deliveries - late_deliveries}/{total_deliveries} on time ({(total_deliveries-late_deliveries)/total_deliveries*100 if total_deliveries else 0:.2f}%)")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("instance_file", help="Path to the instance JSON file")
    parser.add_argument("result_file", help="Path to the result JSON file")
    args = parser.parse_args()
    
    try:
        calculate_utilization(args.instance_file, args.result_file)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)