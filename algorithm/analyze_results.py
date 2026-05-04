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
    df['NN_Success'] = df['NN_Undelivered'] == 0
    df['GA_Success'] = df['GA_Undelivered'] == 0

    summary = df.groupby('Category').agg({
        'GA_Improvement': 'mean',
        'GA_Success': 'mean',
        'NN_Success': 'mean',
        'GA_ms': 'mean',
        'OSRM_ms': 'mean',
        'Prep_ms': 'mean'
    }).round(2)

    print("\n--- Benchmark Summary ---")
    print(summary)

    with open('analysis_summary.md', 'w') as f:
        f.write("
        f.write("```\n")
        f.write(summary.to_string())
        f.write("\n```\n")

    plt.style.use('ggplot')
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))

    categories = df['Category'].unique()
    data_to_plot = [df[df['Category'] == cat]['GA_Improvement'].dropna() for cat in categories]
    axes[0, 0].boxplot(data_to_plot, labels=categories)
    axes[0, 0].set_title('GA Cost Improvement (%) over Nearest Neighbor')
    axes[0, 0].set_ylabel('Improvement %')

    time_cols = ['Prep_ms', 'OSRM_ms', 'NN_ms', 'GA_ms']
    time_data = df.groupby('Category')[time_cols].mean()
    time_data.plot(kind='bar', stacked=True, ax=axes[0, 1])
    axes[0, 1].set_title('Average Execution Time Breakdown (ms)')
    axes[0, 1].set_ylabel('Time (ms)')

    success_data = df.groupby('Category').agg({'NN_Success': 'mean', 'GA_Success': 'mean'}) * 100
    success_data.plot(kind='bar', ax=axes[1, 0])
    axes[1, 0].set_title('Success Rate (%) - All Parcels Delivered')
    axes[1, 0].set_ylabel('Success Rate %')
    axes[1, 0].set_ylim(0, 105)

    def get_size(name):
        import re
        match = re.search(r'_(\d+)_', name)
        return int(match.group(1)) if match else 0
    df['Size'] = df['Instance'].apply(get_size)

    for cat in categories:
        cat_df = df[df['Category'] == cat].groupby('Size')['GA_ms'].mean().reset_index()
        axes[1, 1].plot(cat_df['Size'], cat_df['GA_ms'], marker='o', label=cat)

    axes[1, 1].set_title('GA Execution Time Scaling')
    axes[1, 1].set_ylabel('Time (ms)')
    axes[1, 1].set_xlabel('Instance Size (Parcels)')
    axes[1, 1].legend()

    plt.tight_layout()
    plt.savefig('benchmark_charts.png')
    print("\nCharts saved to benchmark_charts.png")

if __name__ == "__main__":
    analyze()
