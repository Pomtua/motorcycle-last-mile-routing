export const API_URL = (process.env.NEXT_PUBLIC_API_URL ?? 'http://localhost:4000/api').replace(/\/+$/, '');

export type Algorithm = 'hgs' | 'ortools' | 'nn' | 'i1_ls';
export type JobStatus = 'queued' | 'running' | 'succeeded' | 'failed' | 'cancelled';

export interface DatasetDefaults {
  fleetSize: number;
  weightCapacityKg: number;
  volumeCapacityL: number;
  shiftStart: string;
  shiftEnd: string;
}

export interface DatasetSummary {
  id: string;
  name: string;
  source: 'csv' | 'instance';
  defaults: DatasetDefaults | null;
  created_at: string;
  customer_count: number;
  run_count: number;
}

export interface DatasetCustomer {
  id: string;
  node_index: number;
  external_id: string;
  lng: number;
  lat: number;
  weight_kg: number;
  volume_l: number;
  tw_start: string;
  tw_end: string;
  service_min: number;
  snap_distance_m: number | null;
}

export interface DatasetDetail extends Omit<DatasetSummary, 'run_count'> {
  depot: { lng: number; lat: number };
  depot_snap_distance_m: number | null;
  max_snap_distance_m: number | null;
  total_weight_kg: number;
  total_volume_l: number;
  customers: DatasetCustomer[];
}

export type JobSettings =
  | { algorithm: 'hgs'; timeBudgetPerCustomer: number; splitMode: 'dynamic' | 'fixed' }
  | { algorithm: 'ortools'; timeLimit: 'match_hgs' | number }
  | { algorithm: 'nn' }
  | { algorithm: 'i1_ls' };

export interface JobMetrics {
  runtime_ms: number;
  total_distance: number;
  vehicles_used: number;
  fleet_size: number;
  route_zone_excess?: number;
  time_window_ok?: boolean;
  violations?: string[];
  geometry_error?: string;
  stop_reason?: string;
  iterations?: number;
  zones?: { count: number; penalty: number; penalty_alpha: number; silhouette_score: number | null } | null;
  [key: string]: unknown;
}

export interface JobSummary {
  id: string;
  algorithm: Algorithm;
  settings: JobSettings;
  status: JobStatus;
  error: string | null;
  started_at: string | null;
  finished_at: string | null;
  time_limit_s: number | null;
  runtime_ms: number | null;
  total_distance_m: number | null;
  total_duration_s: number | null;
  vehicles_used: number | null;
  feasible: boolean | null;
  capacity_ok: boolean | null;
  on_time_ratio: number | null;
  metrics: JobMetrics | null;
}

export interface RunDetail {
  id: string;
  dataset_id: string;
  seed: number;
  fleet_size: number;
  weight_capacity: number;
  volume_capacity_l: number;
  shift_start: string;
  shift_end: string;
  zoning: boolean;
  num_zones: number | null;
  zone_penalty_alpha: number;
  created_at: string;
  jobs: JobSummary[];
}

export interface ResultStop {
  sequence: number;
  customer_id: string;
  external_id: string;
  lng: number;
  lat: number;
  tw_start: string;
  tw_end: string;
  delivered_weight: number;
  delivered_volume_l: number;
  arrival_s: number;
  service_start_s: number;
  departure_s: number;
  on_time: boolean;
  zone_index: number | null;
  split_count: number;
}

export interface ResultRoute {
  vehicle_index: number;
  distance_m: number;
  duration_s: number;
  load_weight: number;
  load_volume_l: number;
  path: { type: 'LineString'; coordinates: [number, number][] } | null;
  stops: ResultStop[];
}

export interface ResultZone {
  zone_index: number;
  customer_count: number;
  hull: GeoJSON.Polygon | GeoJSON.MultiPolygon;
}

export interface JobResult {
  job: JobSummary & {
    run_id: string;
    dataset_id: string;
    shift_start: string;
    shift_end: string;
    shift_start_s: number;
    fleet_size: number;
    weight_capacity: number;
    volume_capacity_l: number;
    depot: { lng: number; lat: number };
  };
  routes: ResultRoute[];
  zones: ResultZone[];
}

export interface RunRequestBody {
  datasetId: string;
  seed: number;
  fleet: { size: number; weightCapacityKg: number; volumeCapacityL: number };
  shift: { start: string; end: string };
  zoning: { enabled: boolean; numZones: number | 'auto'; penaltyAlpha: number };
  algorithms: JobSettings[];
}

export interface BenchmarkSolverRow {
  group_by: string;
  group_value: string | number;
  case: string;
  instances: number;
  solved: number;
  valid: number;
  mean_gap_vs_i1_ls_pct: number | null;
  median_runtime_ms: number | null;
  mean_route_count: number | null;
  mean_route_zone_excess: number | null;
}

export interface BenchmarkPairRow {
  candidate: string;
  baseline: string;
  group_by: string;
  group_value: string | number;
  matched: number;
  wins: number;
  ties: number;
  losses: number;
  mean_gap_pct: number | null;
}

export interface BenchmarkData {
  final: { solvers: BenchmarkSolverRow[]; pairs: BenchmarkPairRow[] } | null;
  budgets: Record<string, number | string | null>[] | null;
}

export interface FieldError {
  row: number;
  message: string;
}

export class ApiError extends Error {
  constructor(
    message: string,
    readonly errors: FieldError[] = [],
  ) {
    super(message);
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  let response: Response;
  try {
    response = await fetch(`${API_URL}${path}`, init);
  } catch {
    throw new ApiError(`Cannot reach the API at ${API_URL}`);
  }
  if (response.status === 204) {
    return undefined as T;
  }
  const body = await response.json().catch(() => null);
  if (!response.ok) {
    const message = Array.isArray(body?.message) ? body.message.join('; ') : body?.message ?? response.statusText;
    throw new ApiError(message, Array.isArray(body?.errors) ? body.errors : []);
  }
  return body as T;
}

function json(method: string, body: unknown): RequestInit {
  return { method, headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
}

export const api = {
  datasets: () => request<DatasetSummary[]>('/datasets'),
  dataset: (id: string) => request<DatasetDetail>(`/datasets/${id}`),
  deleteDataset: (id: string) => request<void>(`/datasets/${id}`, { method: 'DELETE' }),
  upload: (file: File, maxSnapDistanceM: number) => {
    const form = new FormData();
    form.append('file', file);
    form.append('name', file.name.replace(/\.[^.]+$/, ''));
    form.append('maxSnapDistanceM', String(maxSnapDistanceM));
    const path = file.name.toLowerCase().endsWith('.json') ? '/datasets/instance' : '/datasets';
    return request<DatasetDetail>(path, { method: 'POST', body: form });
  },
  createRun: (body: RunRequestBody) => request<{ runId: string; jobIds: string[] }>('/runs', json('POST', body)),
  run: (id: string) => request<RunDetail>(`/runs/${id}`),
  runs: (datasetId: string) => request<RunDetail[]>(`/runs?datasetId=${datasetId}`),
  cancelRun: (id: string) => request<RunDetail>(`/runs/${id}/cancel`, { method: 'POST' }),
  result: (jobId: string) => request<JobResult>(`/jobs/${jobId}/result`),
  benchmark: () => request<BenchmarkData>('/benchmark'),
  events: (runId: string) => new EventSource(`${API_URL}/runs/${runId}/events`),
  templateUrl: `${API_URL}/datasets/template.csv`,
  exportUrl: (jobId: string) => `${API_URL}/jobs/${jobId}/export.csv`,
};
