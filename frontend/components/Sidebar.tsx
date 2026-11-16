'use client';

import { useRef, useState } from 'react';
import { Algorithm, api, DatasetDetail, DatasetSummary, JobSummary, RunDetail } from '@/lib/api';
import { ALGORITHM_LABEL, ALGORITHM_ORDER } from '@/lib/format';
import ResultCards from './ResultCards';
import RunProgress from './RunProgress';
import { AlgorithmSettingsModal, ZoneSettingsModal } from './SettingsModals';

export interface FleetForm {
  size: number;
  weightKg: number;
  volumeL: number;
  shiftStart: string;
  shiftEnd: string;
}

export interface SolverForm {
  timeBudgetPerCustomer: number;
  splitMode: 'dynamic' | 'fixed';
  seed: number;
}

export interface OrToolsForm {
  mode: 'match_hgs' | 'custom';
  seconds: number;
}

export interface ZoningForm {
  enabled: boolean;
  numZones: number | 'auto';
  penaltyAlpha: number;
}

interface SidebarProps {
  datasets: DatasetSummary[];
  datasetId: string | null;
  dataset: DatasetDetail | null;
  onDataset: (id: string) => void;
  onDeleteDataset: () => void;
  onUpload: (file: File, maxSnapDistanceM: number) => void;
  uploading: boolean;
  fleet: FleetForm;
  onFleet: (fleet: FleetForm) => void;
  selected: Set<Algorithm>;
  onSelected: (selected: Set<Algorithm>) => void;
  solver: SolverForm;
  onSolver: (solver: SolverForm) => void;
  orTools: OrToolsForm;
  onOrTools: (orTools: OrToolsForm) => void;
  zoning: ZoningForm;
  onZoning: (zoning: ZoningForm) => void;
  run: RunDetail | null;
  starting: boolean;
  onRun: () => void;
  onCancel: () => void;
  shownJob: JobSummary | null;
}

function Card({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="rounded-xl border border-line bg-white px-6 py-5">
      <h3 className="mb-3 text-center text-[15px] font-semibold text-ink">{title}</h3>
      {children}
    </div>
  );
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <label className="block">
      <span className="mb-1.5 block text-center text-sm text-muted">{label}</span>
      {children}
    </label>
  );
}

const inputClass =
  'h-11 w-full rounded-lg border border-line bg-white px-3 text-center text-[15px] text-ink outline-none focus:border-primary focus:ring-2 focus:ring-primary/20';

function NumberInput({ value, onChange, min, step }: { value: number; onChange: (value: number) => void; min: number; step: number }) {
  return (
    <input
      type="number"
      className={inputClass}
      value={Number.isFinite(value) ? value : ''}
      min={min}
      step={step}
      onChange={(event) => onChange(event.target.value === '' ? NaN : Number(event.target.value))}
    />
  );
}

function GearIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden>
      <circle cx="12" cy="12" r="3" />
      <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z" />
    </svg>
  );
}

export default function Sidebar(props: SidebarProps) {
  const fileInput = useRef<HTMLInputElement>(null);
  const [dragging, setDragging] = useState(false);
  const [settingsFor, setSettingsFor] = useState<Algorithm | null>(null);
  const [zoneSettings, setZoneSettings] = useState(false);
  const [maxSnap, setMaxSnap] = useState(200);
  const snapValid = Number.isFinite(maxSnap) && maxSnap > 0 && maxSnap <= 100_000;
  const snapDistances = props.dataset
    ? [props.dataset.depot_snap_distance_m, ...props.dataset.customers.map((c) => c.snap_distance_m)].filter(
        (value): value is number => value !== null,
      )
    : [];
  const farthestSnap = snapDistances.length > 0 ? Math.max(...snapDistances) : null;
  const send = (file: File) => {
    if (snapValid) {
      props.onUpload(file, maxSnap);
    }
  };
  const { fleet, onFleet, selected } = props;
  const active = !!props.run?.jobs.some((job) => job.status === 'queued' || job.status === 'running');

  const toggle = (algorithm: Algorithm) => {
    const next = new Set(selected);
    if (next.has(algorithm)) {
      if (next.size > 1) {
        next.delete(algorithm);
      }
    } else {
      next.add(algorithm);
    }
    props.onSelected(next);
  };

  const fleetValid =
    Number.isInteger(fleet.size) && fleet.size >= 1 && fleet.weightKg > 0 && fleet.volumeL > 0 && fleet.shiftStart < fleet.shiftEnd;

  return (
    <aside className="relative flex w-[400px] shrink-0 flex-col gap-3 overflow-y-auto [scrollbar-width:thin] border-r border-line bg-white px-5 pb-6">
      <h1 className="pt-6 pb-1 text-center text-[24px] font-bold tracking-tight text-ink">Motorcycle Routing Module</h1>

      <div
        role="button"
        tabIndex={0}
        onClick={() => fileInput.current?.click()}
        onKeyDown={(event) => event.key === 'Enter' && fileInput.current?.click()}
        onDragOver={(event) => {
          event.preventDefault();
          setDragging(true);
        }}
        onDragLeave={() => setDragging(false)}
        onDrop={(event) => {
          event.preventDefault();
          setDragging(false);
          const file = event.dataTransfer.files[0];
          if (file) {
            send(file);
          }
        }}
        className={`flex cursor-pointer flex-col items-center gap-2 rounded-xl border-2 border-dashed px-4 py-5 transition-colors ${
          dragging ? 'border-primary bg-primary-tint' : 'border-line hover:border-primary/50'
        }`}
      >
        {props.uploading ? (
          <span className="h-8 w-8 animate-spin rounded-full border-2 border-primary border-t-transparent" />
        ) : (
          <svg width="34" height="34" viewBox="0 0 24 24" fill="none" stroke="#64748b" strokeWidth="1.8" aria-hidden>
            <path d="M12 15V4M7 9l5-5 5 5M4 15v4a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-4" />
          </svg>
        )}
        <span className="text-[15px] font-medium text-ink">
          {props.uploading ? 'Importing and building the road matrix…' : 'Upload Parcel CSV'}
        </span>
        <span className="text-xs text-muted">or a benchmark instance JSON · drag & drop</span>
        <input
          ref={fileInput}
          type="file"
          accept=".csv,.json"
          className="hidden"
          onChange={(event) => {
            const file = event.target.files?.[0];
            if (file) {
              send(file);
            }
            event.target.value = '';
          }}
        />
      </div>
      <div className="-mt-1 flex flex-col items-center gap-1.5 text-xs">
        <a href={api.templateUrl} className="font-medium text-primary hover:underline">
          Download template CSV
        </a>
        <label
          className="flex items-center gap-1.5 text-muted"
          title="Every CSV point is moved onto its nearest road; points farther than this are rejected"
        >
          Max distance to road
          <input
            type="number"
            min={1}
            step={10}
            value={maxSnap}
            onChange={(event) => setMaxSnap(Number(event.target.value))}
            className={`h-7 w-20 rounded-md border px-1.5 text-right text-ink ${snapValid ? 'border-line' : 'border-accent-red'}`}
          />
          m
        </label>
      </div>

      <Card title="Fleet & Constraints">
        <div className="space-y-3">
          <Field label="Number of Available Drivers">
            <NumberInput value={fleet.size} min={1} step={1} onChange={(size) => onFleet({ ...fleet, size })} />
          </Field>
          <div className="grid grid-cols-2 gap-3">
            <Field label="Max Weight (kg)">
              <NumberInput value={fleet.weightKg} min={0.1} step={1} onChange={(weightKg) => onFleet({ ...fleet, weightKg })} />
            </Field>
            <Field label="Max Volume (L)">
              <NumberInput value={fleet.volumeL} min={0.1} step={1} onChange={(volumeL) => onFleet({ ...fleet, volumeL })} />
            </Field>
            <Field label="Shift Start">
              <input type="time" className={inputClass} value={fleet.shiftStart} onChange={(e) => onFleet({ ...fleet, shiftStart: e.target.value })} />
            </Field>
            <Field label="Shift End">
              <input type="time" className={inputClass} value={fleet.shiftEnd} onChange={(e) => onFleet({ ...fleet, shiftEnd: e.target.value })} />
            </Field>
          </div>
        </div>
      </Card>

      <Card title="Simulation Settings">
        <div className="space-y-4">
          <div>
            <span className="mb-1.5 block text-sm text-muted">Dataset</span>
            <div className="flex gap-2">
              <select
                className="h-11 min-w-0 flex-1 rounded-lg border border-line bg-white px-3 text-[15px] outline-none focus:border-primary"
                value={props.datasetId ?? ''}
                onChange={(event) => props.onDataset(event.target.value)}
              >
                {props.datasets.length === 0 && <option value="">No dataset yet — upload a CSV</option>}
                {props.datasets.map((dataset) => (
                  <option key={dataset.id} value={dataset.id}>
                    {dataset.name} ({dataset.customer_count})
                  </option>
                ))}
              </select>
              <button
                onClick={props.onDeleteDataset}
                disabled={!props.datasetId || active}
                title="Delete dataset"
                className="h-11 w-11 shrink-0 rounded-lg border border-line text-muted hover:border-accent-red hover:text-accent-red disabled:opacity-40"
              >
                <svg className="mx-auto" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <path d="M3 6h18M8 6V4h8v2M19 6l-1 14H6L5 6" />
                </svg>
              </button>
            </div>
            {props.dataset && (
              <p className="mt-1.5 text-xs text-muted">
                {props.dataset.customer_count} parcels · {props.dataset.total_weight_kg.toFixed(1)} kg ·{' '}
                {props.dataset.total_volume_l.toFixed(1)} L
                {farthestSnap !== null && (
                  <>
                    {' '}
                    · snapped ≤ {Math.round(farthestSnap)} m (limit {props.dataset.max_snap_distance_m} m)
                  </>
                )}
              </p>
            )}
          </div>

          <div>
            <span className="mb-1.5 block text-sm text-muted">Algorithm · select one or more</span>
            <div className="grid grid-cols-2 gap-2">
              {ALGORITHM_ORDER.map((algorithm) => {
                const on = selected.has(algorithm);
                return (
                  <div
                    key={algorithm}
                    className={`flex h-10 items-stretch overflow-hidden rounded-lg border text-[13px] ${
                      on ? 'border-primary/40 bg-primary-tint font-semibold text-primary' : 'border-line bg-white text-ink'
                    }`}
                  >
                    <button className="flex min-w-0 flex-1 items-center gap-1.5 pl-2.5" onClick={() => toggle(algorithm)} aria-pressed={on} title={ALGORITHM_LABEL[algorithm]}>
                      <span
                        className={`flex h-4 w-4 shrink-0 items-center justify-center rounded border ${on ? 'border-primary bg-primary' : 'border-slate-300'}`}
                      >
                        {on && (
                          <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="4">
                            <path d="M5 12l5 5L20 7" />
                          </svg>
                        )}
                      </span>
                      <span className="truncate">{ALGORITHM_LABEL[algorithm]}</span>
                    </button>
                    <button
                      onClick={() => setSettingsFor(algorithm)}
                      title={`${ALGORITHM_LABEL[algorithm]} settings`}
                      className={`shrink-0 px-1.5 ${on ? 'text-primary hover:bg-primary/10' : 'text-muted hover:bg-zebra'}`}
                    >
                      <GearIcon />
                    </button>
                  </div>
                );
              })}
            </div>
            {!selected.has('nn') && (
              <p className="mt-1.5 text-xs text-muted">Baseline (NN) always runs as the reference for the results.</p>
            )}
          </div>

          <div className="flex items-center justify-between rounded-lg border border-line px-3 py-2.5">
            <label className="flex cursor-pointer items-center gap-3 text-[15px]">
              <span
                className={`relative h-5 w-9 rounded-full transition-colors ${props.zoning.enabled ? 'bg-primary' : 'bg-slate-300'}`}
              >
                <span
                  className={`absolute top-0.5 h-4 w-4 rounded-full bg-white shadow transition-all ${
                    props.zoning.enabled ? 'left-[18px]' : 'left-0.5'
                  }`}
                />
              </span>
              <input
                type="checkbox"
                className="sr-only"
                checked={props.zoning.enabled}
                onChange={(event) => props.onZoning({ ...props.zoning, enabled: event.target.checked })}
              />
              Zone Clustering
              <span className="text-xs text-muted">
                {props.zoning.enabled ? (props.zoning.numZones === 'auto' ? 'auto zones' : `${props.zoning.numZones} zones`) : 'off'}
              </span>
            </label>
            <button onClick={() => setZoneSettings(true)} className="rounded-md p-1.5 text-muted hover:bg-zebra" title="Zone settings">
              <GearIcon />
            </button>
          </div>

          <RunProgress
            run={props.run}
            starting={props.starting}
            disabled={!props.datasetId || !fleetValid}
            onRun={props.onRun}
            onCancel={props.onCancel}
          />
          {!fleetValid && <p className="-mt-2 text-center text-xs text-accent-red">Check the fleet values and shift times.</p>}
        </div>
      </Card>

      <ResultCards run={props.run} shownJob={props.shownJob} />

      {settingsFor && (
        <AlgorithmSettingsModal
          algorithm={settingsFor}
          solver={props.solver}
          onSolver={props.onSolver}
          orTools={props.orTools}
          onOrTools={props.onOrTools}
          hgsSelected={selected.has('hgs')}
          customers={props.dataset?.customer_count ?? null}
          onClose={() => setSettingsFor(null)}
        />
      )}
      {zoneSettings && <ZoneSettingsModal zoning={props.zoning} onZoning={props.onZoning} onClose={() => setZoneSettings(false)} />}
    </aside>
  );
}
