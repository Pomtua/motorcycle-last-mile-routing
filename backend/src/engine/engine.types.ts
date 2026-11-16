export type Algorithm = 'hgs' | 'i1_ls' | 'nn' | 'ortools';

export const ALGORITHMS: readonly Algorithm[] = ['hgs', 'i1_ls', 'nn', 'ortools'];

export interface InstanceNode {
  osm_id: string;
  type: string;
  lng: number;
  lat: number;
  demand_weight: number;
  demand_volume: number;
  tw_start: number;
  tw_end: number;
  service_time: number;
}

export interface InstanceJson {
  meta: { seed: number; n: number; horizon: number; w_max: number; v_max: number };
  fleet: { size: number; weight_capacity: number; volume_capacity: number };
  nodes: InstanceNode[];
  duration_matrix: number[][];
  distance_matrix: number[][];
}

export interface EngineRequest {
  algorithm: Algorithm;
  time_limit_seconds?: number;
  split_mode?: 'dynamic' | 'fixed';
  seed: number;
  zoning: { enabled: boolean; num_zones: number | 'auto'; penalty_alpha: number };
  instance: InstanceJson;
}

export interface EngineStop {
  node: number;
  weight: number;
  volume: number;
  arrival: number;
  start: number;
  departure: number;
  on_time: boolean;
  zone: number | null;
}

export interface EngineRoute {
  vehicle: number;
  distance: number;
  duration: number;
  load_weight: number;
  load_volume: number;
  stops: EngineStop[];
}

export interface EngineResult {
  status: 'ok';
  routes: EngineRoute[];
  zones: {
    count: number;
    policy: string;
    silhouette_score: number | null;
    penalty_alpha: number;
    penalty: number;
    preprocessing_ms: number;
    zone_of: number[];
  } | null;
  metrics: {
    runtime_ms: number;
    total_distance: number;
    total_duration: number;
    vehicles_used: number;
    fleet_size: number;
    route_zone_excess?: number;
    [key: string]: unknown;
  };
  validation: {
    feasible: boolean;
    capacity_ok: boolean;
    time_window_ok: boolean;
    on_time_stops: number;
    total_stops: number;
    violations: string[];
  };
}
