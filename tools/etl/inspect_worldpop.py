import csv
import os
import numpy as np
import matplotlib.pyplot as plt

CSV_PATH = 'data/master_pool/refined_coordinates.csv'

def main():
    if not os.path.exists(CSV_PATH):
        raise FileNotFoundError(f"Master pool CSV not found at {CSV_PATH}")

    with open(CSV_PATH, mode='r', encoding='utf-8') as f:
        reader = csv.DictReader(f, skipinitialspace=True)
        raw_rows = list(reader)

    rows = [{k.strip(): v.strip() for k, v in r.items() if k is not None} for r in raw_rows]
    
    if not rows or 'worldpop' not in rows[0]:
        raise KeyError("Column 'worldpop' not found in CSV. Run enrich_master_pool_worldpop.py first.")

    pop_values = np.array([float(r['worldpop']) for r in rows], dtype=np.float64)
    total_nodes = len(pop_values)

    min_val = float(np.min(pop_values))
    max_val = float(np.max(pop_values))
    mean_val = float(np.mean(pop_values))
    median_val = float(np.median(pop_values))

    zero_count = int(np.sum(pop_values == 0))
    zero_pct = (zero_count / total_nodes * 100)

    p1, p5, p25, p75, p90, p95, p99 = np.percentile(
        pop_values, [1, 5, 25, 75, 90, 95, 99]
    )

    non_zero = pop_values[pop_values > 0]
    min_pos = float(np.min(non_zero)) if len(non_zero) > 0 else 0.01
    eps = min_pos * 0.5

    print("--- Refined Coordinates WorldPop Diagnostics ---")
    print(f"Total Candidate Nodes : {total_nodes:,}")
    print(f"Zero Population Nodes : {zero_count} ({zero_pct:.2f}% of total candidates)")
    print()
    print("--- Summary Statistics (People / km²) ---")
    print(f"Min Value       : {min_val:.4f}")
    print(f"Max Value       : {max_val:.4f}")
    print(f"Mean Value      : {mean_val:.4f}")
    print(f"Median (P50)    : {median_val:.4f}")
    print(f"P1  : {p1:.4f} | P5  : {p5:.4f}")
    print(f"P25 : {p25:.4f} | P75 : {p75:.4f}")
    print(f"P90 : {p90:.4f} | P95 : {p95:.4f} | P99 : {p99:.4f}")
    print()
    print(f"Smallest Non-Zero Value: {min_pos:.6f}")
    print(f"Suggested Epsilon Range (0.1 * min_pos to min_pos): {0.1 * min_pos:.6f} - {min_pos:.6f}")

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    axes[0].hist(pop_values, bins=50, color='skyblue', edgecolor='black')
    axes[0].set_title("Linear Scale Population Density (Candidate Nodes)")
    axes[0].set_xlabel("People / km²")
    axes[0].set_ylabel("Frequency (Candidate Nodes)")

    log_data = np.log10(pop_values + eps)
    axes[1].hist(log_data, bins=50, color='salmon', edgecolor='black')
    axes[1].set_title(f"Log10 Scale Population Density (eps = {eps:.6f})")
    axes[1].set_xlabel("log10(People / km² + eps)")
    axes[1].set_ylabel("Frequency (Candidate Nodes)")

    plt.tight_layout()
    plt.show()

if __name__ == '__main__':
    main()