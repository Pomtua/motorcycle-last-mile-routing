'use client';

import { useEffect, useState } from 'react';
import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import { api, BenchmarkData, BenchmarkSolverRow } from '@/lib/api';
import { duration, signedPercent } from '@/lib/format';

const CASES: { id: string; label: string; color: string }[] = [
  { id: 'hgs', label: 'C++ Engine (HGS dynamic)', color: '#2563EB' },
  { id: 'hgs_fixed', label: 'HGS fixed', color: '#F97316' },
  { id: 'ortools_split_hgs_time', label: 'OR-Tools (same time)', color: '#16A34A' },
  { id: 'i1_ls', label: 'I1+LS', color: '#D97706' },
  { id: 'nn', label: 'Nearest Neighbor', color: '#64748B' },
  { id: 'hgs_zoned', label: 'C++ Engine, zoned', color: '#1E40AF' },
  { id: 'ortools_split_zoned_hgs_time', label: 'OR-Tools, zoned (same time)', color: '#15803D' },
];

const PAIRS: { candidate: string; baseline: string; label: string }[] = [
  { candidate: 'hgs', baseline: 'ortools_split_hgs_time', label: 'C++ Engine vs OR-Tools' },
  { candidate: 'hgs_zoned', baseline: 'ortools_split_zoned_hgs_time', label: 'C++ Engine vs OR-Tools (zoned)' },
  { candidate: 'hgs', baseline: 'hgs_fixed', label: 'Dynamic vs fixed split' },
];

interface SweepLine {
  id: string;
  label: string;
  color: string;
  dashed: boolean;
}

const SWEEP_LINES: SweepLine[] = [
  { id: 'hgs', label: 'HGS dynamic', color: '#2563EB', dashed: false },
  { id: 'hgs_fixed', label: 'HGS fixed', color: '#F97316', dashed: false },
  { id: 'ortools_split_hgs_time', label: 'OR-Tools', color: '#16A34A', dashed: false },
  { id: 'hgs_zoned', label: 'HGS zoned', color: '#1E40AF', dashed: true },
  { id: 'ortools_split_zoned_hgs_time', label: 'OR-Tools zoned', color: '#15803D', dashed: true },
];

const ZONED_LINES = SWEEP_LINES.filter((line) => line.dashed);

const DASH = '5 4';

function LineKey({ lines }: { lines: SweepLine[] }) {
  return (
    <div className="flex flex-wrap justify-center gap-x-4 gap-y-1 pt-2 text-xs">
      {lines.map((line) => (
        <span key={line.id} className="inline-flex items-center gap-1.5" style={{ color: line.color }}>
          <svg width="28" height="10" aria-hidden>
            <line x1="1" y1="5" x2="27" y2="5" stroke={line.color} strokeWidth="2" strokeDasharray={line.dashed ? DASH : undefined} />
            <circle cx="14" cy="5" r="3" fill="#fff" stroke={line.color} strokeWidth="1.5" />
          </svg>
          {line.label}
        </span>
      ))}
    </div>
  );
}

function SweepChart({
  data,
  lines,
  suffix,
  digits,
}: {
  data: Record<string, number | string>[];
  lines: SweepLine[];
  suffix: string;
  digits: number;
}) {
  return (
    <ResponsiveContainer width="100%" height={260}>
      <LineChart data={data} margin={{ top: 8, right: 16, left: 0, bottom: 0 }}>
        <CartesianGrid stroke="#e2e8f0" />
        <XAxis dataKey="budget" tick={{ fontSize: 12 }} label={{ value: 's per customer', position: 'insideBottomRight', offset: -2, fontSize: 11 }} />
        <YAxis tick={{ fontSize: 12 }} width={40} />
        <Tooltip formatter={(value) => `${Number(value).toFixed(digits)}${suffix}`} />
        <Legend content={() => <LineKey lines={lines} />} />
        <ReferenceLine x="0.1" stroke="#0f172a" strokeDasharray="4 4" />
        {lines.map((line) => (
          <Line
            key={line.id}
            dataKey={line.id}
            name={line.label}
            stroke={line.color}
            strokeWidth={2}
            strokeDasharray={line.dashed ? DASH : undefined}
            dot={{ strokeDasharray: '0' }}
            isAnimationActive={false}
          />
        ))}
      </LineChart>
    </ResponsiveContainer>
  );
}

function PerHourTable({
  data,
  valueKey,
  hoursKey,
  valueLabel,
  perHourLabel,
  gain,
  format,
}: {
  data: Record<string, number | string>[];
  valueKey: string;
  hoursKey: string;
  valueLabel: string;
  perHourLabel: string;
  gain: (previous: number, current: number) => number;
  format: (value: number) => string;
}) {
  return (
    <table className="data-table">
      <thead>
        <tr>
          <th>Budget</th>
          <th>Total Hours</th>
          <th>{valueLabel}</th>
          <th>{perHourLabel}</th>
        </tr>
      </thead>
      <tbody>
        {data.map((entry, index) => {
          const previous = data[index - 1];
          const hours = Number(entry[hoursKey]);
          const value = Number(entry[valueKey]);
          const perHour = previous
            ? gain(Number(previous[valueKey]), value) / (hours - Number(previous[hoursKey]))
            : null;
          return (
            <tr key={String(entry.budget)} className={entry.budget === '0.1' ? 'selected' : ''}>
              <td>{entry.budget} s</td>
              <td className="tabular-nums">{Number.isFinite(hours) ? hours.toFixed(2) : '–'}</td>
              <td className="tabular-nums">{Number.isFinite(value) ? format(value) : '–'}</td>
              <td className="tabular-nums">{perHour === null || !Number.isFinite(perHour) ? '–' : perHour.toFixed(2)}</td>
            </tr>
          );
        })}
      </tbody>
    </table>
  );
}

function Panel({
  title,
  note,
  scroll = false,
  children,
}: {
  title: string;
  note?: string;
  scroll?: boolean;
  children: React.ReactNode;
}) {
  return (
    <div className="min-w-0 rounded-xl border border-line bg-white px-6 py-6">
      <h3 className="text-[15px] font-semibold text-ink">{title}</h3>
      {note && <p className="mt-1 text-xs text-muted">{note}</p>}
      <div className={scroll ? 'mt-4 overflow-x-auto' : 'mt-4'}>{children}</div>
    </div>
  );
}

function rowFor(rows: BenchmarkSolverRow[], groupBy: string, value: string | number, id: string) {
  return rows.find((row) => row.group_by === groupBy && String(row.group_value) === String(value) && row.case === id);
}

export default function Benchmark({ onError }: { onError: (error: unknown) => void }) {
  const [data, setData] = useState<BenchmarkData | null>(null);

  useEffect(() => {
    api.benchmark().then(setData).catch(onError);
  }, [onError]);

  if (!data) {
    return <div className="h-40 animate-pulse rounded-xl bg-white" />;
  }
  if (!data.final) {
    return (
      <div className="rounded-xl border border-line bg-white px-8 py-16 text-center text-sm text-muted">
        results/final/summary.json was not found on the server.
      </div>
    );
  }

  const solvers = data.final.solvers;
  const sizes = [...new Set(solvers.filter((row) => row.group_by === 'size').map((row) => Number(row.group_value)))].sort(
    (a, b) => a - b,
  );
  const bySize = sizes.map((size) => {
    const entry: Record<string, number | string> = { size: String(size) };
    for (const id of ['hgs', 'hgs_fixed', 'ortools_split_hgs_time']) {
      const gap = rowFor(solvers, 'size', size, id)?.mean_gap_vs_i1_ls_pct;
      if (gap !== null && gap !== undefined) {
        entry[id] = Number((-gap).toFixed(2));
      }
    }
    return entry;
  });

  const sweep = (data.budgets ?? [])
    .filter((row) => row.size === 'all')
    .map((row) => {
      const entry: Record<string, number | string> = { budget: String(row.budget_seconds_per_customer) };
      for (const line of SWEEP_LINES) {
        const gap = row[`${line.id}_mean_gap_vs_i1_ls_pct`];
        if (typeof gap === 'number') {
          entry[line.id] = Number((-gap).toFixed(4));
        }
        const excess = row[`${line.id}_mean_route_zone_excess`];
        if (typeof excess === 'number') {
          entry[`${line.id}_excess`] = Number(excess.toFixed(4));
        }
        const total = row[`${line.id}_total_runtime_ms`];
        if (typeof total === 'number') {
          entry[`${line.id}_hours`] = Number((total / 3_600_000).toFixed(4));
        }
      }
      return entry;
    });

  return (
    <div className="flex flex-col gap-4">
      <Panel
        scroll
        title="Benchmark Summary — all 432 instances"
        note="Offline results used in the algorithm report: sizes 20–1,000, demand classes D1–D4, clustered/random/mixed locations, loose and tight time windows, seeds 42–44. Budget 0.1 s per customer. Distance is relative to I1+LS on the same instance; negative is shorter."
      >
        <table className="data-table compact">
          <thead>
            <tr>
              <th>Algorithm</th>
              <th>Valid</th>
              <th>Distance vs I1+LS</th>
              <th>Median Time</th>
              <th>Mean Routes</th>
              <th>Zone Excess</th>
            </tr>
          </thead>
          <tbody>
            {CASES.map((item) => {
              const row = rowFor(solvers, 'overall', 'all', item.id);
              if (!row) {
                return null;
              }
              return (
                <tr key={item.id}>
                  <td className="!text-left">
                    <span className="inline-flex items-center gap-2">
                      <span className="h-2.5 w-2.5 rounded-full" style={{ background: item.color }} />
                      {item.label}
                    </span>
                  </td>
                  <td>
                    {row.valid} / {row.instances}
                  </td>
                  <td className="tabular-nums">{row.mean_gap_vs_i1_ls_pct === null ? '–' : signedPercent(row.mean_gap_vs_i1_ls_pct, 2)}</td>
                  <td className="tabular-nums">{duration(row.median_runtime_ms)}</td>
                  <td className="tabular-nums">{row.mean_route_count?.toFixed(1) ?? '–'}</td>
                  <td className="tabular-nums">{row.mean_route_zone_excess?.toFixed(2) ?? '–'}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </Panel>

      <div className="grid grid-cols-1 gap-4 min-[1700px]:grid-cols-2">
        <Panel scroll title="Head-to-head (same instance, same time)">
          <table className="data-table compact">
            <thead>
              <tr>
                <th>Comparison</th>
                <th>Wins</th>
                <th>Ties</th>
                <th>Losses</th>
                <th>Mean Gap</th>
              </tr>
            </thead>
            <tbody>
              {PAIRS.map((pair) => {
                const row = data.final?.pairs.find(
                  (item) =>
                    item.group_by === 'overall' && item.candidate === pair.candidate && item.baseline === pair.baseline,
                );
                return row ? (
                  <tr key={pair.label}>
                    <td className="!text-left">{pair.label}</td>
                    <td className="font-semibold text-accent-green">{row.wins}</td>
                    <td>{row.ties}</td>
                    <td className="text-accent-red">{row.losses}</td>
                    <td className="tabular-nums">{row.mean_gap_pct === null ? '–' : signedPercent(row.mean_gap_pct, 2)}</td>
                  </tr>
                ) : null;
              })}
            </tbody>
          </table>
        </Panel>

        <Panel title="Distance reduction vs I1+LS by size (%)">
          <ResponsiveContainer width="100%" height={220}>
            <LineChart data={bySize} margin={{ top: 8, right: 16, left: 0, bottom: 0 }}>
              <CartesianGrid stroke="#e2e8f0" />
              <XAxis dataKey="size" tick={{ fontSize: 12 }} label={{ value: 'customers', position: 'insideBottomRight', offset: -2, fontSize: 11 }} />
              <YAxis tick={{ fontSize: 12 }} width={40} />
              <Tooltip formatter={(value) => `${value}%`} />
              <Legend wrapperStyle={{ fontSize: 12 }} />
              {CASES.slice(0, 3).map((item) => (
                <Line key={item.id} dataKey={item.id} name={item.label} stroke={item.color} strokeWidth={2} dot isAnimationActive={false} />
              ))}
            </LineChart>
          </ResponsiveContainer>
        </Panel>
      </div>

      {sweep.length > 0 ? (
        <>
          <div className="grid grid-cols-1 gap-4 min-[1700px]:grid-cols-2">
            <Panel
              title="Time budget sweep — distance reduction vs I1+LS (%)"
              note="144 instances (seed 42). The dashed vertical line marks 0.1 s per customer, the budget used by the dashboard; dashed series are zoned runs."
            >
              <SweepChart data={sweep} lines={SWEEP_LINES} suffix="%" digits={2} />
            </Panel>
            <Panel scroll title="Added distance reduction per extra hour (C++ Engine)" note="How much each step up in budget buys, per hour of total runtime over the 144 instances.">
              <PerHourTable
                data={sweep}
                valueKey="hgs"
                hoursKey="hgs_hours"
                valueLabel="Reduction"
                perHourLabel="Added per Hour"
                gain={(previous, current) => current - previous}
                format={(value) => `${value.toFixed(2)}%`}
              />
            </Panel>
          </div>
          <div className="grid grid-cols-1 gap-4 min-[1700px]:grid-cols-2">
            <Panel
              title="Time budget sweep — zone excess"
              note="Zone excess of one instance is the sum over routes of (zones visited − 1), averaged over the 144 instances. Lower is better."
            >
              <SweepChart
                data={sweep.map((entry) =>
                  Object.fromEntries([['budget', entry.budget], ...ZONED_LINES.map((line) => [line.id, entry[`${line.id}_excess`]])]),
                )}
                lines={ZONED_LINES}
                suffix=""
                digits={2}
              />
            </Panel>
            <Panel scroll title="Zone excess removed per extra hour (C++ Engine, zoned)" note="How much zone excess each step up in budget removes, per hour of total runtime over the 144 instances.">
              <PerHourTable
                data={sweep}
                valueKey="hgs_zoned_excess"
                hoursKey="hgs_zoned_hours"
                valueLabel="Zone Excess"
                perHourLabel="Removed per Hour"
                gain={(previous, current) => previous - current}
                format={(value) => value.toFixed(2)}
              />
            </Panel>
          </div>
        </>
      ) : (
        <Panel title="Time budget sweep">
          <p className="text-sm text-muted">results/budgets/budgets.csv was not found on the server.</p>
        </Panel>
      )}
    </div>
  );
}
