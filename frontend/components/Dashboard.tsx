'use client';

import dynamic from 'next/dynamic';
import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  Algorithm,
  api,
  ApiError,
  DatasetDetail,
  DatasetSummary,
  FieldError,
  JobResult,
  JobSettings,
  RunDetail,
} from '@/lib/api';
import { ALGORITHM_ORDER } from '@/lib/format';
import Analytics from './Analytics';
import Benchmark from './Benchmark';
import FleetSchedule from './FleetSchedule';
import Modal from './Modal';
import ResultSelector from './ResultSelector';
import Sidebar, { FleetForm, OrToolsForm, SolverForm, ZoningForm } from './Sidebar';

const MapView = dynamic(() => import('./MapView'), {
  ssr: false,
  loading: () => <div className="h-full w-full animate-pulse rounded-xl bg-white" />,
});

type Tab = 'map' | 'schedule' | 'analytics' | 'benchmark';

const TABS: { id: Tab; label: string }[] = [
  { id: 'map', label: 'Simulation Map' },
  { id: 'schedule', label: 'Fleet Schedule' },
  { id: 'analytics', label: 'Analytics' },
  { id: 'benchmark', label: 'Benchmark' },
];

const DEFAULT_FLEET: FleetForm = { size: 5, weightKg: 20, volumeL: 50, shiftStart: '08:00', shiftEnd: '16:00' };

export interface Problem {
  title: string;
  message: string;
  errors?: FieldError[];
}

function isActive(run: RunDetail | null): boolean {
  return !!run && run.jobs.some((job) => job.status === 'queued' || job.status === 'running');
}

export default function Dashboard() {
  const [tab, setTab] = useState<Tab>('map');
  const [datasets, setDatasets] = useState<DatasetSummary[]>([]);
  const [datasetId, setDatasetId] = useState<string | null>(null);
  const [dataset, setDataset] = useState<DatasetDetail | null>(null);
  const [fleet, setFleet] = useState<FleetForm>(DEFAULT_FLEET);
  const [selected, setSelected] = useState<Set<Algorithm>>(new Set(['hgs']));
  const [solver, setSolver] = useState<SolverForm>({ timeBudgetPerCustomer: 0.1, splitMode: 'dynamic', seed: 42 });
  const [orTools, setOrTools] = useState<OrToolsForm>({ mode: 'match_hgs', seconds: 10 });
  const [zoning, setZoning] = useState<ZoningForm>({ enabled: false, numZones: 'auto', penaltyAlpha: 0.25 });
  const [run, setRun] = useState<RunDetail | null>(null);
  const [history, setHistory] = useState<RunDetail[]>([]);
  const [preferred, setPreferred] = useState<Algorithm>('hgs');
  const [results, setResults] = useState<Record<string, JobResult>>({});
  const [uploading, setUploading] = useState(false);
  const [starting, setStarting] = useState(false);
  const [problem, setProblem] = useState<Problem | null>(null);

  const report = useCallback((title: string, error: unknown) => {
    const apiError = error instanceof ApiError ? error : null;
    setProblem({ title, message: (error as Error).message, errors: apiError?.errors });
  }, []);

  const showRun = useCallback((detail: RunDetail) => {
    setRun(detail);
    setFleet({
      size: detail.fleet_size,
      weightKg: detail.weight_capacity,
      volumeL: detail.volume_capacity_l,
      shiftStart: detail.shift_start,
      shiftEnd: detail.shift_end,
    });
    setZoning({ enabled: detail.zoning, numZones: detail.num_zones ?? 'auto', penaltyAlpha: detail.zone_penalty_alpha });
    const settings = detail.jobs.map((job) => job.settings);
    const chosen = settings.map((job) => job.algorithm).filter((algorithm) => algorithm !== 'nn' || settings.length === 1);
    setSelected(new Set(chosen));
    const hgs = settings.find((job) => job.algorithm === 'hgs');
    setSolver((current) => ({
      seed: detail.seed,
      timeBudgetPerCustomer: hgs?.algorithm === 'hgs' ? hgs.timeBudgetPerCustomer : current.timeBudgetPerCustomer,
      splitMode: hgs?.algorithm === 'hgs' ? hgs.splitMode : current.splitMode,
    }));
    const ortools = settings.find((job) => job.algorithm === 'ortools');
    if (ortools?.algorithm === 'ortools') {
      setOrTools((current) =>
        ortools.timeLimit === 'match_hgs' ? { ...current, mode: 'match_hgs' } : { mode: 'custom', seconds: ortools.timeLimit },
      );
    }
  }, []);

  const refreshDatasets = useCallback(async () => {
    const list = await api.datasets();
    setDatasets(list);
    return list;
  }, []);

  useEffect(() => {
    refreshDatasets()
      .then((list) => setDatasetId((current) => current ?? list[0]?.id ?? null))
      .catch((error) => report('Cannot load datasets', error));
  }, [refreshDatasets, report]);

  useEffect(() => {
    setDataset(null);
    setRun(null);
    setHistory([]);
    setResults({});
    if (!datasetId) {
      return;
    }
    let cancelled = false;
    Promise.all([api.dataset(datasetId), api.runs(datasetId)])
      .then(([detail, runs]) => {
        if (cancelled) {
          return;
        }
        setDataset(detail);
        setHistory(runs);
        if (runs[0]) {
          showRun(runs[0]);
          return;
        }
        setRun(null);
        if (detail.defaults) {
          const d = detail.defaults;
          setFleet({
            size: d.fleetSize,
            weightKg: d.weightCapacityKg,
            volumeL: d.volumeCapacityL,
            shiftStart: d.shiftStart,
            shiftEnd: d.shiftEnd,
          });
        }
      })
      .catch((error) => !cancelled && report('Cannot load the dataset', error));
    return () => {
      cancelled = true;
    };
  }, [datasetId, report, showRun]);

  const active = isActive(run);
  const runId = run?.id ?? null;

  useEffect(() => {
    if (!runId || !active) {
      return;
    }
    const source = api.events(runId);
    const refresh = () =>
      api
        .run(runId)
        .then((detail) => {
          setRun((current) => (current?.id === runId ? detail : current));
          setHistory((list) => list.map((item) => (item.id === runId ? detail : item)));
        })
        .catch(() => undefined);
    source.addEventListener('job', refresh);
    source.onerror = () => undefined;
    void refresh();
    return () => source.close();
  }, [runId, active]);

  const succeeded = useMemo(
    () => ALGORITHM_ORDER.filter((algorithm) => run?.jobs.some((j) => j.algorithm === algorithm && j.status === 'succeeded')),
    [run],
  );

  const shown = succeeded.includes(preferred) ? preferred : succeeded[0] ?? null;

  const shownJob = run?.jobs.find((job) => job.algorithm === shown && job.status === 'succeeded') ?? null;
  const shownJobId = shownJob?.id ?? null;

  useEffect(() => {
    if (!shownJobId || results[shownJobId]) {
      return;
    }
    api
      .result(shownJobId)
      .then((result) => setResults((cache) => ({ ...cache, [shownJobId]: result })))
      .catch((error) => report('Cannot load the result', error));
  }, [shownJobId, results, report]);

  const upload = async (file: File, maxSnapDistanceM: number) => {
    setUploading(true);
    try {
      const created = await api.upload(file, maxSnapDistanceM);
      await refreshDatasets();
      setDatasetId(created.id);
      setTab('map');
    } catch (error) {
      report(`Cannot import ${file.name}`, error);
    } finally {
      setUploading(false);
    }
  };

  const startRun = async () => {
    if (!datasetId) {
      setProblem({ title: 'No dataset', message: 'Upload a parcel CSV or choose a dataset first.' });
      return;
    }
    const algorithms: JobSettings[] = ALGORITHM_ORDER.filter((algorithm) => selected.has(algorithm)).map((algorithm) => {
      if (algorithm === 'hgs') {
        return { algorithm, timeBudgetPerCustomer: solver.timeBudgetPerCustomer, splitMode: solver.splitMode };
      }
      if (algorithm === 'ortools') {
        return {
          algorithm,
          timeLimit: orTools.mode === 'match_hgs' && selected.has('hgs') ? 'match_hgs' : orTools.seconds,
        };
      }
      return { algorithm };
    });
    setStarting(true);
    try {
      const { runId: created } = await api.createRun({
        datasetId,
        seed: solver.seed,
        fleet: { size: fleet.size, weightCapacityKg: fleet.weightKg, volumeCapacityL: fleet.volumeL },
        shift: { start: fleet.shiftStart, end: fleet.shiftEnd },
        zoning: { enabled: zoning.enabled, numZones: zoning.numZones, penaltyAlpha: zoning.penaltyAlpha },
        algorithms,
      });
      const detail = await api.run(created);
      setRun(detail);
      setHistory((list) => [detail, ...list]);
      setPreferred(selected.has('hgs') ? 'hgs' : ALGORITHM_ORDER.find((algorithm) => selected.has(algorithm)) ?? 'hgs');
    } catch (error) {
      report('Cannot start the simulation', error);
    } finally {
      setStarting(false);
    }
  };

  const cancelRun = async () => {
    if (!run) {
      return;
    }
    try {
      setRun(await api.cancelRun(run.id));
    } catch (error) {
      report('Cannot cancel the run', error);
    }
  };

  const openRun = (id: string) => {
    const found = history.find((item) => item.id === id);
    if (found) {
      showRun(found);
      setTab('map');
    }
  };

  const removeDataset = async () => {
    if (!datasetId || !window.confirm('Delete this dataset and all of its runs?')) {
      return;
    }
    try {
      await api.deleteDataset(datasetId);
      const list = await refreshDatasets();
      setDatasetId(list[0]?.id ?? null);
    } catch (error) {
      report('Cannot delete the dataset', error);
    }
  };

  const result = shownJobId ? results[shownJobId] ?? null : null;

  return (
    <div className="relative flex h-screen min-w-[1200px] overflow-hidden">
      <Sidebar
        datasets={datasets}
        datasetId={datasetId}
        dataset={dataset}
        onDataset={setDatasetId}
        onDeleteDataset={removeDataset}
        onUpload={upload}
        uploading={uploading}
        fleet={fleet}
        onFleet={setFleet}
        selected={selected}
        onSelected={setSelected}
        solver={solver}
        onSolver={setSolver}
        orTools={orTools}
        onOrTools={setOrTools}
        zoning={zoning}
        onZoning={setZoning}
        run={run}
        starting={starting}
        onRun={startRun}
        onCancel={cancelRun}
        shownJob={shownJob}
      />
      <main className="flex min-w-0 flex-1 flex-col">
        <nav className="flex h-[78px] shrink-0 items-end gap-2 border-b border-line px-1">
          {TABS.map((item) => (
            <button
              key={item.id}
              onClick={() => setTab(item.id)}
              className={`-mb-px border-b-2 px-4 pb-5 text-[19px] font-semibold transition-colors ${
                tab === item.id ? 'border-primary text-primary' : 'border-transparent text-ink hover:text-primary'
              }`}
            >
              {item.label}
            </button>
          ))}
        </nav>
        <section className="flex min-h-0 flex-1 flex-col gap-4 overflow-auto bg-page p-6">
          {(tab === 'map' || tab === 'schedule') && succeeded.length > 1 && (
            <ResultSelector algorithms={succeeded} shown={shown} onShown={setPreferred} />
          )}
          {tab === 'map' && (
            <div className="min-h-[560px] flex-1 overflow-hidden rounded-xl shadow-sm ring-1 ring-line">
              <MapView dataset={dataset} result={result} zoning={!!run?.zoning} />
            </div>
          )}
          {tab === 'schedule' && <FleetSchedule result={result} loading={!!shownJobId && !result} />}
          {tab === 'analytics' && (
            <Analytics run={run} history={history} onOpenRun={openRun} activeRunId={run?.id ?? null} />
          )}
          {tab === 'benchmark' && <Benchmark onError={(error) => report('Cannot load benchmark results', error)} />}
        </section>
      </main>
      {problem && (
        <Modal title={problem.title} onClose={() => setProblem(null)}>
          <p className="text-sm text-ink">{problem.message}</p>
          {problem.errors && problem.errors.length > 0 && (
            <ul className="mt-3 max-h-72 space-y-1 overflow-auto rounded-lg bg-zebra p-3 text-sm">
              {problem.errors.map((error, index) => (
                <li key={index}>
                  {error.row > 0 && <span className="font-semibold">Row {error.row}: </span>}
                  {error.message}
                </li>
              ))}
            </ul>
          )}
        </Modal>
      )}
    </div>
  );
}
