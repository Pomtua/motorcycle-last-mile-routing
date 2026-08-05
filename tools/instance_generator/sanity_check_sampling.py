import os
import numpy as np
import matplotlib.pyplot as plt
from scipy.spatial import cKDTree
from generator import InstanceGenerator

MASTER_POOL_PATH = 'data/master_pool/refined_coordinates.csv'

def mean_nearest_neighbor_dist(customers):
    coords = np.array([[c['lng'], c['lat']] for c in customers])
    tree = cKDTree(coords)
    dists, _ = tree.query(coords, k=2)
    return dists[:, 1].mean()

def main():
    if not os.path.exists(MASTER_POOL_PATH):
        raise FileNotFoundError(f"Master pool CSV not found at {MASTER_POOL_PATH}")

    gen = InstanceGenerator(seed=42, master_pool_path=MASTER_POOL_PATH)
    gen.load_coordinates()
    depot = gen.select_depot()
    pool_pop = np.array([p['worldpop'] for p in gen.pool])
    pool_mean_pop = pool_pop.mean()

    print("=" * 80)
    print("SPATIAL SAMPLING DIAGNOSTIC SWEEP")
    print(f"Master Pool Total Nodes: {len(gen.pool):,} | Original Pool Mean Pop: {pool_mean_pop:.1f}")
    print("=" * 80)
    print(f"{'n':>5} | {'Class':>5} | {'Seed':>4} | {'Count':>5} | {'Mean NN (deg)':>14} | {'Mean Pop':>10}")
    print("-" * 80)

    sizes = [20, 50, 100, 500, 1000]
    spatials = ['R', 'C', 'RC']
    seeds = [42, 43, 44]

    for n in sizes:
        for sp in spatials:
            for seed in seeds:
                gen.rng = np.random.default_rng(seed)
                customers = gen.sample_customers(n=n, spatial_class=sp, depot=depot)

                assert len(customers) == n, f"Expected {n} customers for {sp} (seed {seed}), got {len(customers)}"

                nn_dist = mean_nearest_neighbor_dist(customers)
                cust_pop = np.array([c['worldpop'] for c in customers])
                cust_mean = cust_pop.mean()

                print(f"{n:5d} | {sp:>5s} | {seed:4d} | {len(customers):5d} | {nn_dist:14.5f} | {cust_mean:10.1f}")

    print("=" * 80)

    fig, axes = plt.subplots(1, 3, figsize=(18, 5))
    colors = {'R': 'blue', 'C': 'green', 'RC': 'purple'}

    pool_lngs = [c['lng'] for c in gen.pool]
    pool_lats = [c['lat'] for c in gen.pool]

    for i, sp_class in enumerate(spatials):
        gen.rng = np.random.default_rng(42)
        customers = gen.sample_customers(n=100, spatial_class=sp_class, depot=depot)

        axes[i].scatter(pool_lngs, pool_lats, color='lightgray', s=2, alpha=0.3, label='Master Pool')

        cust_lngs = [c['lng'] for c in customers]
        cust_lats = [c['lat'] for c in customers]
        axes[i].scatter(cust_lngs, cust_lats, color=colors[sp_class], s=25, label=f'Customers ({sp_class})')
        axes[i].scatter([depot['lng']], [depot['lat']], color='red', marker='^', s=100, label='Depot')

        axes[i].set_title(f"Spatial Class: {sp_class} (n=100, seed=42)")
        axes[i].set_xlabel("Longitude")
        axes[i].set_ylabel("Latitude")
        axes[i].legend()

    plt.tight_layout()
    plt.show()

if __name__ == '__main__':
    main()
