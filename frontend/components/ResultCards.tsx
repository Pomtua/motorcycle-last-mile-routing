'use client';

import { JobSummary, RunDetail } from '@/lib/api';
import { ALGORITHM_LABEL, duration, km, percent, signedPercent } from '@/lib/format';

const ACCENTS = {
  blue: 'border-accent-blue bg-[#eef4ff] text-accent-blue',
  slate: 'border-accent-slate bg-[#f1f5f9] text-[#334155]',
  orange: 'border-accent-orange bg-[#fff6ef] text-[#c2410c]',
  green: 'border-accent-green bg-[#effcf3] text-[#15803d]',
  red: 'border-accent-red bg-[#fef2f2] text-accent-red',
};

function Tile({
  label,
  value,
  note,
  accent,
}: {
  label: string;
  value: string;
  note?: string;
  accent: keyof typeof ACCENTS;
}) {
  return (
    <div className={`rounded-lg border-l-4 px-4 py-3 ${ACCENTS[accent]}`}>
      <p className="text-[13px] font-semibold text-ink/80">{label}</p>
      <p className="mt-0.5 text-[24px] font-bold tabular-nums leading-tight">{value}</p>
      {note && <p className="mt-0.5 text-xs text-ink/60">{note}</p>}
    </div>
  );
}

export default function ResultCards({ run, shownJob }: { run: RunDetail | null; shownJob: JobSummary | null }) {
  const nn = run?.jobs.find((job) => job.algorithm === 'nn' && job.status === 'succeeded') ?? null;
  const job = shownJob;
  const vsNn =
    job && nn && job.algorithm !== 'nn' && job.total_distance_m !== null && nn.total_distance_m
      ? ((job.total_distance_m - nn.total_distance_m) / nn.total_distance_m) * 100
      : null;

  return (
    <div>
      <h3 className="mb-2 px-1 text-[15px] font-semibold text-ink">
        Simulation Results
        {job && <span className="ml-2 text-xs font-medium text-muted">{ALGORITHM_LABEL[job.algorithm]}</span>}
      </h3>
      {!job ? (
        <p className="rounded-lg bg-zebra px-4 py-6 text-center text-sm text-muted">Run a simulation to see the results.</p>
      ) : (
        <div className="grid grid-cols-2 gap-3">
          <Tile
            label="Total Distance"
            value={km(job.total_distance_m)}
            note={vsNn !== null ? `${signedPercent(vsNn)} vs NN` : undefined}
            accent="blue"
          />
          <Tile label="Baseline (NN)" value={km(nn?.total_distance_m)} accent="slate" />
          <Tile label="Execution Time" value={duration(job.runtime_ms)} accent="orange" />
          <Tile
            label="Time Window Valid"
            value={percent(job.on_time_ratio, job.on_time_ratio === 1 ? 0 : 1)}
            accent={job.on_time_ratio === 1 ? 'green' : 'red'}
          />
          <Tile
            label="Capacity Valid"
            value={job.capacity_ok ? '✓ Yes' : '✗ No'}
            note="weight and volume per rider"
            accent={job.capacity_ok ? 'green' : 'red'}
          />
          <Tile
            label="Riders Used"
            value={`${job.vehicles_used ?? '–'} / ${run?.fleet_size ?? '–'}`}
            note={job.feasible ? 'all constraints satisfied' : 'validator found violations'}
            accent={job.feasible ? 'slate' : 'red'}
          />
        </div>
      )}
    </div>
  );
}
