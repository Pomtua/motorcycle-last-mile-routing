import pandas as pd
import matplotlib.pyplot as plt
import os

def analyze():

    if not os.path.exists('benchmark_results.csv'):
        print("Error: benchmark_results.csv not found")
        return

    df = pd.read_csv('benchmark_results.csv')

    if 'GA_ms' in df.columns:
        df['GA_ms'] = df['GA_ms'].apply(lambda x: max(x, 0))

    def get_category(name):
        if name.startswith('scale'): return 'Scale'
        if name.startswith('split'): return 'Split'
        if name.startswith('tight'): return 'Tight'
        return 'Other'

    df['Category'] = df['Instance'].apply(get_category)

    df['GA_Improvement'] = (df['NN_Cost'] - df['GA_Cost']) / df['NN_Cost'] * 100
    df['ORT_Improvement'] = (df['NN_Cost'] - df['ORT_Cost']) / df['NN_Cost'] * 100
    df['NN_Success'] = df['NN_Undelivered'] == 0
    df['GA_Success'] = df['GA_Undelivered'] == 0
    df['ORT_Success'] = df['ORT_Undelivered'] == 0

    summary = df.groupby('Category').agg({
        'GA_Improvement': 'mean',
        'ORT_Improvement': 'mean',
        'GA_Success': 'mean',
        'NN_Success': 'mean',
        'ORT_Success': 'mean',
        'GA_ms': 'mean',
        'NN_ms': 'mean',
        'ORT_ms': 'mean',
        'Prep_ms': 'mean'
    }).round(2)

    print("\n--- Benchmark Summary ---")
    print(summary)

    with open('analysis_summary.md', 'w') as f:
        f.write("# Benchmark Analysis Summary\n\n")
        f.write("```\n")
        f.write(summary.to_string())
        f.write("\n```\n")

    plt.style.use('ggplot')
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))

    def get_size(name):
        import re
        match = re.search(r'_(\d+)_', name)
        return int(match.group(1)) if match else 0
    df['Size'] = df['Instance'].apply(get_size)

    # 1. Execution time vs instance size
    ax1 = axes[0, 0]
    time_data = df.groupby('Size').agg({'NN_ms': 'mean', 'GA_ms': 'mean', 'ORT_ms': 'mean'})
    time_data.plot(kind='bar', ax=ax1)
    ax1.set_title('Execution Time vs Instance Size')
    ax1.set_ylabel('Time (ms)')
    ax1.set_xlabel('Instance Size (N)')

    # 2. Solution quality (Cost) vs instance size
    ax2 = axes[0, 1]
    cost_data = df.groupby('Size').agg({'NN_Cost': 'mean', 'GA_Cost': 'mean', 'ORT_Cost': 'mean'})
    cost_data.plot(kind='bar', ax=ax2)
    ax2.set_title('Solution Quality (Total Cost) vs Instance Size')
    ax2.set_ylabel('Total Cost')
    ax2.set_xlabel('Instance Size (N)')

    # 3. GA and ORT Improvement by Category
    ax3 = axes[1, 0]
    categories = df['Category'].unique()
    
    # We use a grouped bar chart instead of a boxplot since there are multiple solvers
    imp_data = df.groupby('Category').agg({'GA_Improvement': 'mean', 'ORT_Improvement': 'mean'})
    imp_data.plot(kind='bar', ax=ax3)
    ax3.set_title('Cost Improvement (%) over NN by Category')
    ax3.set_ylabel('Improvement (%)')

    # 4. Success Rate by Category
    ax4 = axes[1, 1]
    success_data = df.groupby('Category').agg({'NN_Success': 'mean', 'GA_Success': 'mean', 'ORT_Success': 'mean'}) * 100
    success_data.plot(kind='bar', ax=ax4)
    ax4.set_title('Feasible Solution Success Rate (%) by Category')
    ax4.set_ylabel('Success Rate (%)')
    ax4.set_ylim(0, 105)

    plt.tight_layout()
    plt.savefig('benchmark_charts.png')
    print("\nCharts saved to benchmark_charts.png")

if __name__ == "__main__":
    analyze()
