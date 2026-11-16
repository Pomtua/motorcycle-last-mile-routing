'use client';

import { useState } from 'react';
import { Algorithm } from '@/lib/api';
import { ALGORITHM_LABEL } from '@/lib/format';
import Modal from './Modal';
import type { OrToolsForm, SolverForm, ZoningForm } from './Sidebar';

const BUDGETS = [0.01, 0.02, 0.05, 0.1, 0.2, 0.5];

const DESCRIPTIONS: Record<Algorithm, string> = {
  hgs:
    'Hybrid Genetic Search written in C++: a population of giant tours, a bounded-fleet Split, granular local search with time-warp penalties, and solver-decided split delivery.',
  ortools:
    'Google OR-Tools routing solver (PATH_CHEAPEST_ARC + Guided Local Search) with weight, volume and time-window dimensions. OR-Tools cannot split a delivery by itself, so parcels larger than a rider are pre-split into fixed pieces.',
  nn: 'Nearest Neighbor: each rider repeatedly drives to the closest parcel that still fits its load and time window. It is the baseline required by the project scope and always runs as the reference.',
  i1_ls:
    'Solomon I1 insertion heuristic followed by local search (relocate, swap, 2-opt*). It also builds the starting solution of the C++ Engine.',
};

function Choice<T extends string | number>({
  options,
  value,
  onChange,
  label,
}: {
  options: { value: T; label: string }[];
  value: T;
  onChange: (value: T) => void;
  label: (value: T) => string;
}) {
  return (
    <div className="flex flex-wrap gap-2">
      {options.map((option) => (
        <button
          key={String(option.value)}
          onClick={() => onChange(option.value)}
          className={`rounded-lg border px-3 py-1.5 text-sm ${
            option.value === value ? 'border-primary bg-primary-tint font-semibold text-primary' : 'border-line text-ink hover:border-primary/40'
          }`}
        >
          {label(option.value) || option.label}
        </button>
      ))}
    </div>
  );
}

const fieldClass = 'h-10 w-full rounded-lg border border-line px-3 text-sm outline-none focus:border-primary';

function Footer({ onCancel, onSave, valid }: { onCancel: () => void; onSave: () => void; valid: boolean }) {
  return (
    <>
      <button onClick={onCancel} className="rounded-lg border border-line px-4 py-2 text-sm font-medium text-ink hover:bg-zebra">
        Cancel
      </button>
      <button
        onClick={onSave}
        disabled={!valid}
        className="rounded-lg bg-primary px-4 py-2 text-sm font-semibold text-white disabled:opacity-40"
      >
        Save
      </button>
    </>
  );
}

interface AlgorithmSettingsProps {
  algorithm: Algorithm;
  solver: SolverForm;
  onSolver: (solver: SolverForm) => void;
  orTools: OrToolsForm;
  onOrTools: (orTools: OrToolsForm) => void;
  hgsSelected: boolean;
  customers: number | null;
  onClose: () => void;
}

export function AlgorithmSettingsModal(props: AlgorithmSettingsProps) {
  const [solver, setSolver] = useState(props.solver);
  const [orTools, setOrTools] = useState(props.orTools);
  const { algorithm } = props;

  const solverValid =
    solver.timeBudgetPerCustomer > 0 && Number.isInteger(solver.seed) && solver.seed >= 0 && solver.seed < 2 ** 32;
  const orToolsValid = orTools.mode === 'match_hgs' || (orTools.seconds > 0 && orTools.seconds <= 3600);
  const editable = algorithm === 'hgs' || algorithm === 'ortools';

  const save = () => {
    if (algorithm === 'hgs') {
      props.onSolver(solver);
    }
    if (algorithm === 'ortools') {
      props.onOrTools(orTools);
    }
    props.onClose();
  };

  return (
    <Modal
      title={`${ALGORITHM_LABEL[algorithm]} settings`}
      onClose={props.onClose}
      footer={editable ? <Footer onCancel={props.onClose} onSave={save} valid={algorithm === 'hgs' ? solverValid : orToolsValid} /> : undefined}
    >
      <p className="text-sm leading-relaxed text-muted">{DESCRIPTIONS[algorithm]}</p>

      {algorithm === 'hgs' && (
        <div className="mt-4 space-y-4">
          <div>
            <span className="mb-1.5 block text-sm font-medium text-ink">Time budget per customer</span>
            <Choice
              options={BUDGETS.map((value) => ({ value, label: '' }))}
              value={solver.timeBudgetPerCustomer}
              onChange={(timeBudgetPerCustomer) => setSolver({ ...solver, timeBudgetPerCustomer })}
              label={(value) => `${value} s`}
            />
            <p className="mt-1.5 text-xs text-muted">
              0.1 s per customer is the best trade-off in the time-budget experiment: beyond it the distance improves very
              little, below it large instances (500–1,000 customers) get clearly worse.
              {props.customers !== null &&
                ` For this dataset: ${(solver.timeBudgetPerCustomer * props.customers).toFixed(1)} s in total.`}
            </p>
          </div>
          <div>
            <span className="mb-1.5 block text-sm font-medium text-ink">Split delivery</span>
            <Choice
              options={[
                { value: 'dynamic' as const, label: 'Dynamic' },
                { value: 'fixed' as const, label: 'Fixed' },
              ]}
              value={solver.splitMode}
              onChange={(splitMode) => setSolver({ ...solver, splitMode })}
              label={(value) => (value === 'dynamic' ? 'Dynamic (solver decides)' : 'Fixed (fill-first pieces)')}
            />
          </div>
          <label className="block">
            <span className="mb-1.5 block text-sm font-medium text-ink">Random seed</span>
            <input
              type="number"
              min={0}
              step={1}
              className={fieldClass}
              value={Number.isFinite(solver.seed) ? solver.seed : ''}
              onChange={(event) => setSolver({ ...solver, seed: event.target.value === '' ? NaN : Number(event.target.value) })}
            />
            <span className="mt-1 block text-xs text-muted">Also seeds the zone clustering, so every algorithm sees the same zones.</span>
          </label>
        </div>
      )}

      {algorithm === 'ortools' && (
        <div className="mt-4 space-y-3">
          <span className="block text-sm font-medium text-ink">Time limit</span>
          <Choice
            options={[
              { value: 'match_hgs' as const, label: '' },
              { value: 'custom' as const, label: '' },
            ]}
            value={orTools.mode}
            onChange={(mode) => setOrTools({ ...orTools, mode })}
            label={(value) => (value === 'match_hgs' ? 'Same as C++ Engine' : 'Custom')}
          />
          {orTools.mode === 'match_hgs' ? (
            <p className="text-xs text-muted">
              OR-Tools starts after the C++ Engine finishes and gets exactly the time the C++ Engine used, as in the benchmark.
              {!props.hgsSelected && ' The C++ Engine is not selected, so the custom limit below is used instead.'}
            </p>
          ) : null}
          {(orTools.mode === 'custom' || !props.hgsSelected) && (
            <label className="block">
              <span className="mb-1.5 block text-sm text-muted">Seconds</span>
              <input
                type="number"
                min={0.1}
                step={1}
                className={fieldClass}
                value={Number.isFinite(orTools.seconds) ? orTools.seconds : ''}
                onChange={(event) =>
                  setOrTools({ ...orTools, seconds: event.target.value === '' ? NaN : Number(event.target.value) })
                }
              />
            </label>
          )}
        </div>
      )}
    </Modal>
  );
}

export function ZoneSettingsModal({
  zoning,
  onZoning,
  onClose,
}: {
  zoning: ZoningForm;
  onZoning: (zoning: ZoningForm) => void;
  onClose: () => void;
}) {
  const [draft, setDraft] = useState(zoning);
  const valid =
    (draft.numZones === 'auto' || (Number.isInteger(draft.numZones) && draft.numZones >= 1 && draft.numZones <= 64)) &&
    Number.isFinite(draft.penaltyAlpha) &&
    draft.penaltyAlpha >= 0;

  return (
    <Modal
      title="Zone Clustering settings"
      onClose={onClose}
      footer={
        <Footer
          onCancel={onClose}
          onSave={() => {
            onZoning({ ...draft, enabled: true });
            onClose();
          }}
          valid={valid}
        />
      }
    >
      <p className="text-sm leading-relaxed text-muted">
        Parcels are grouped into delivery zones with k-means++ on their coordinates. Every extra zone a rider enters adds a
        penalty, so routes stay inside as few zones as possible. Saving turns zone clustering on.
      </p>
      <div className="mt-4 space-y-4">
        <div>
          <span className="mb-1.5 block text-sm font-medium text-ink">Number of zones</span>
          <div className="flex items-center gap-2">
            <Choice
              options={[
                { value: 'auto' as const, label: '' },
                { value: 'fixed' as const, label: '' },
              ]}
              value={draft.numZones === 'auto' ? 'auto' : 'fixed'}
              onChange={(mode) => setDraft({ ...draft, numZones: mode === 'auto' ? 'auto' : 4 })}
              label={(value) => (value === 'auto' ? 'Auto (best silhouette)' : 'Fixed')}
            />
            {draft.numZones !== 'auto' && (
              <input
                type="number"
                min={1}
                max={64}
                className="h-9 w-24 rounded-lg border border-line px-3 text-sm outline-none focus:border-primary"
                value={Number.isFinite(draft.numZones) ? draft.numZones : ''}
                onChange={(event) => setDraft({ ...draft, numZones: event.target.value === '' ? NaN : Number(event.target.value) })}
              />
            )}
          </div>
        </div>
        <label className="block">
          <span className="mb-1.5 block text-sm font-medium text-ink">Zone penalty α</span>
          <input
            type="number"
            min={0}
            step={0.05}
            className={fieldClass}
            value={Number.isFinite(draft.penaltyAlpha) ? draft.penaltyAlpha : ''}
            onChange={(event) => setDraft({ ...draft, penaltyAlpha: event.target.value === '' ? NaN : Number(event.target.value) })}
          />
          <span className="mt-1 block text-xs text-muted">
            Penalty per extra zone = α × (I1+LS distance per route). 0.25 is the value used in the benchmark.
          </span>
        </label>
      </div>
    </Modal>
  );
}
