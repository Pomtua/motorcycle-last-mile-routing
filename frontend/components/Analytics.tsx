'use client';

import { Bar, BarChart, CartesianGrid, Cell, LabelList, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { JobSummary, RunDetail } from '@/lib/api';
import { ALGORITHM_COLOR, ALGORITHM_COLUMN, ALGORITHM_LABEL, ALGORITHM_ORDER, duration, km, minutes, percent, signedPercent } from '@/lib/format';

interface AnalyticsProps {
  run: RunDetail | null;
  history: RunDetail[];
  activeRunId: string | null;
  onOpenRun: (id: string) => void;
}

function Panel({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="rounded-xl border border-line bg-white px-8 py-6">
      <h3 className="mb-4 text-[15px] font-semibold text-ink">{title}</h3>
      {children}
    </div>
  );
}

type Row = { label: string; value: (job: JobSummary) => string; hidden?: boolean };

export default function Analytics({ run, history, activeRunId, onOpenRun }: AnalyticsProps) {
  if (!run) {
    return (
      <div className="rounded-xl border border-line bg-white px-8 py-16 text-center text-sm text-muted">
        Run a simulation to compare the algorithms.
      </div>
    );
  }

  const jobs = ALGORITHM_ORDER.flatMap((algorithm) => run.jobs.filter((job) => job.algorithm === algorithm));
  const nn = run.jobs.find((job) => job.algorithm === 'nn' && job.status === 'succeeded');
  const ok = (job: JobSummary, text: string) => (job.status === 'succeeded' ? text : job.status === 'failed' ? 'failed' : job.status);

  const rows: Row[] = [
    { label: 'Execution Time', value: (job) => ok(job, duration(job.runtime_ms)) },
    { label: 'Total Distance', value: (job) => ok(job, km(job.total_distance_m)) },
    {
      label: 'Distance vs NN',
      value: (job) =>
        ok(
          job,
          job.algorithm === 'nn' || !nn?.total_distance_m || job.total_distance_m === null
            ? '–'
            : signedPercent(((job.total_distance_m - nn.total_distance_m) / nn.total_distance_m) * 100),
        ),
    },
    { label: 'Total Travel Time', value: (job) => ok(job, minutes(job.total_duration_s)) },
    { label: 'Riders Used', value: (job) => ok(job, `${job.vehicles_used} / ${run.fleet_size}`) },
    { label: 'Time Window Valid', value: (job) => ok(job, percent(job.on_time_ratio)) },
    { label: 'Capacity Valid', value: (job) => ok(job, job.capacity_ok ? '✓' : '✗') },
    { label: 'All Constraints Valid', value: (job) => ok(job, job.feasible ? '✓' : '✗') },
    { label: 'Zone Excess', value: (job) => ok(job, String(job.metrics?.route_zone_excess ?? '–')), hidden: !run.zoning },
    { label: 'Time Limit', value: (job) => (job.time_limit_s ? duration(job.time_limit_s * 1000) : 'none') },
  ];

  const chartData = jobs
    .filter((job) => job.status === 'succeeded')
    .map((job) => ({
      name: ALGORITHM_LABEL[job.algorithm],
      algorithm: job.algorithm,
      distance: Number(((job.total_distance_m ?? 0) / 1000).toFixed(2)),
      runtime: Math.max(0.01, job.runtime_ms ?? 0),
    }));

  return (
    <div className="flex flex-col gap-4">
      <Panel title="Algorithm Comparison Matrix">
        <div className="overflow-x-auto">
          <table className="data-table">
            <thead>
              <tr>
                <th>Metric</th>
                {jobs.map((job) => (
                  <th key={job.id}>{ALGORITHM_COLUMN[job.algorithm]}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows
                .filter((row) => !row.hidden)
                .map((row) => (
                  <tr key={row.label}>
                    <td>{row.label}</td>
                    {jobs.map((job) => (
                      <td key={job.id} className="tabular-nums">
                        {row.value(job)}
                      </td>
                    ))}
                  </tr>
                ))}
            </tbody>
          </table>
        </div>
        {jobs.some((job) => job.error) && (
          <ul className="mt-3 space-y-1 text-xs text-accent-red">
            {jobs
              .filter((job) => job.error)
              .map((job) => (
                <li key={job.id}>
                  {ALGORITHM_LABEL[job.algorithm]}: {job.error}
                </li>
              ))}
          </ul>
        )}
      </Panel>

      {chartData.length > 0 && (
        <div className="grid grid-cols-[repeat(2,minmax(0,1fr))] gap-4">
          <Panel title="Total Distance (km)">
            <ResponsiveContainer width="100%" height={240}>
              <BarChart data={chartData} margin={{ top: 20, right: 8, left: 0, bottom: 0 }}>
                <CartesianGrid vertical={false} stroke="#e2e8f0" />
                <XAxis dataKey="name" tick={{ fontSize: 12 }} />
                <YAxis tick={{ fontSize: 12 }} width={48} />
                <Tooltip formatter={(value) => [`${value} km`, 'Distance']} />
                <Bar dataKey="distance" radius={[4, 4, 0, 0]} isAnimationActive={false}>
                  <LabelList dataKey="distance" position="top" fontSize={12} />
                  {chartData.map((entry) => (
                    <Cell key={entry.algorithm} fill={ALGORITHM_COLOR[entry.algorithm]} />
                  ))}
                </Bar>
              </BarChart>
            </ResponsiveContainer>
          </Panel>
          <Panel title="Execution Time (ms, log scale)">
            <ResponsiveContainer width="100%" height={240}>
              <BarChart data={chartData} margin={{ top: 20, right: 8, left: 0, bottom: 0 }}>
                <CartesianGrid vertical={false} stroke="#e2e8f0" />
                <XAxis dataKey="name" tick={{ fontSize: 12 }} />
                <YAxis scale="log" domain={[0.01, 'auto']} allowDataOverflow tick={{ fontSize: 12 }} width={56} />
                <Tooltip formatter={(value) => [duration(Number(value)), 'Execution time']} />
                <Bar dataKey="runtime" radius={[4, 4, 0, 0]} isAnimationActive={false}>
                  <LabelList dataKey="runtime" position="top" fontSize={12} formatter={(value) => duration(Number(value))} />
                  {chartData.map((entry) => (
                    <Cell key={entry.algorithm} fill={ALGORITHM_COLOR[entry.algorithm]} />
                  ))}
                </Bar>
              </BarChart>
            </ResponsiveContainer>
          </Panel>
        </div>
      )}

      <Panel title="Run History">
        <div className="max-h-[360px] overflow-auto">
          <table className="data-table">
            <thead className="sticky top-0">
              <tr>
                <th>Date</th>
                <th>Drivers</th>
                <th>Capacity</th>
                <th>Shift</th>
                <th>Zoning</th>
                <th>Algorithms</th>
                <th>Best Distance</th>
              </tr>
            </thead>
            <tbody>
              {history.map((item) => {
                const finished = item.jobs.filter((job) => job.status === 'succeeded' && job.total_distance_m !== null);
                const best = finished.reduce<JobSummary | null>(
                  (top, job) => (!top || (job.total_distance_m ?? Infinity) < (top.total_distance_m ?? Infinity) ? job : top),
                  null,
                );
                return (
                  <tr
                    key={item.id}
                    className={`clickable ${item.id === activeRunId ? 'selected' : ''}`}
                    onClick={() => onOpenRun(item.id)}
                  >
                    <td>{new Date(item.created_at).toLocaleString()}</td>
                    <td>{item.fleet_size}</td>
                    <td>
                      {item.weight_capacity} kg · {Math.round(item.volume_capacity_l)} L
                    </td>
                    <td>
                      {item.shift_start}–{item.shift_end}
                    </td>
                    <td>{item.zoning ? (item.num_zones ? `${item.num_zones} zones` : 'auto') : 'off'}</td>
                    <td>
                      <span className="inline-flex gap-1">
                        {ALGORITHM_ORDER.filter((algorithm) => item.jobs.some((job) => job.algorithm === algorithm)).map((algorithm) => (
                          <span
                            key={algorithm}
                            className="rounded px-1.5 py-0.5 text-xs font-semibold text-white"
                            style={{ background: ALGORITHM_COLOR[algorithm] }}
                          >
                            {ALGORITHM_LABEL[algorithm]}
                          </span>
                        ))}
                      </span>
                    </td>
                    <td>{best ? `${km(best.total_distance_m)} (${ALGORITHM_LABEL[best.algorithm]})` : '–'}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </Panel>
    </div>
  );
}
