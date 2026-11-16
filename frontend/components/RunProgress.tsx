'use client';

import { useEffect, useState } from 'react';
import { JobSummary, RunDetail } from '@/lib/api';
import { ALGORITHM_LABEL, ALGORITHM_ORDER, duration } from '@/lib/format';

interface RunProgressProps {
  run: RunDetail | null;
  starting: boolean;
  disabled: boolean;
  onRun: () => void;
  onCancel: () => void;
}

function useNow(active: boolean): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (!active) {
      return;
    }
    const timer = window.setInterval(() => setNow(Date.now()), 250);
    return () => window.clearInterval(timer);
  }, [active]);
  return now;
}

function StatusIcon({ status }: { status: JobSummary['status'] }) {
  if (status === 'running') {
    return <span className="h-3.5 w-3.5 animate-spin rounded-full border-2 border-primary border-t-transparent" />;
  }
  const color =
    status === 'succeeded' ? 'bg-accent-green' : status === 'failed' ? 'bg-accent-red' : status === 'cancelled' ? 'bg-slate-400' : 'bg-slate-300';
  return <span className={`h-2.5 w-2.5 rounded-full ${color}`} />;
}

export default function RunProgress({ run, starting, disabled, onRun, onCancel }: RunProgressProps) {
  const active = !!run?.jobs.some((job) => job.status === 'queued' || job.status === 'running');
  const now = useNow(active);
  const jobs = run ? ALGORITHM_ORDER.flatMap((algorithm) => run.jobs.filter((job) => job.algorithm === algorithm)) : [];

  return (
    <div className="space-y-2">
      {active ? (
        <button
          onClick={onCancel}
          className="h-11 w-full rounded-lg border-2 border-accent-red text-[15px] font-semibold text-accent-red hover:bg-red-50"
        >
          Cancel Simulation
        </button>
      ) : (
        <button
          onClick={onRun}
          disabled={disabled || starting}
          className="h-11 w-full rounded-lg bg-primary text-[15px] font-semibold text-white shadow-sm hover:bg-blue-800 disabled:opacity-50"
        >
          {starting ? 'Starting…' : 'Run Simulation'}
        </button>
      )}
      {jobs.length > 0 && (active || jobs.some((job) => job.status === 'failed' || job.status === 'cancelled')) && (
        <ul className="space-y-1.5 rounded-lg bg-zebra p-3 text-sm">
          {jobs.map((job) => {
            const elapsed =
              job.status === 'running' && job.started_at ? now - new Date(job.started_at).getTime() : job.runtime_ms;
            return (
              <li key={job.id}>
                <div className="flex items-center gap-2">
                  <StatusIcon status={job.status} />
                  <span className="flex-1 font-medium">{ALGORITHM_LABEL[job.algorithm]}</span>
                  <span className="tabular-nums text-muted">
                    {job.status === 'queued'
                      ? job.settings.algorithm === 'ortools' && job.settings.timeLimit === 'match_hgs'
                        ? 'waits for C++'
                        : 'queued'
                      : job.status === 'running'
                        ? `${duration(Math.max(0, elapsed ?? 0))}${job.time_limit_s ? ` / ${duration(job.time_limit_s * 1000)}` : ''}`
                        : job.status === 'succeeded'
                          ? duration(job.runtime_ms)
                          : job.status}
                  </span>
                </div>
                {job.error && <p className="mt-0.5 pl-5 text-xs text-accent-red">{job.error}</p>}
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}
