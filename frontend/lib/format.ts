import type { Algorithm } from './api';

export const ALGORITHM_ORDER: Algorithm[] = ['hgs', 'ortools', 'nn', 'i1_ls'];

export const ALGORITHM_LABEL: Record<Algorithm, string> = {
  hgs: 'C++ Engine',
  ortools: 'OR-Tools',
  nn: 'Baseline (NN)',
  i1_ls: 'I1+LS',
};

export const ALGORITHM_COLUMN: Record<Algorithm, string> = {
  hgs: 'Proposed C++',
  ortools: 'Google OR-Tools',
  nn: 'Nearest Neighbor (Baseline)',
  i1_ls: 'I1+LS',
};

export const ALGORITHM_COLOR: Record<Algorithm, string> = {
  hgs: '#2563EB',
  ortools: '#16A34A',
  nn: '#64748B',
  i1_ls: '#D97706',
};

export const RIDER_COLORS = [
  '#4361EE',
  '#22A447',
  '#E11D48',
  '#F59E0B',
  '#8B5CF6',
  '#0EA5E9',
  '#EC4899',
  '#14B8A6',
  '#F97316',
  '#6366F1',
  '#84CC16',
  '#A855F7',
  '#06B6D4',
  '#EF4444',
  '#10B981',
  '#D946EF',
];

export const ZONE_COLORS = ['#4361EE', '#E11D48', '#22A447', '#F59E0B', '#8B5CF6', '#0EA5E9', '#EC4899', '#14B8A6'];

export function riderColor(index: number): string {
  return RIDER_COLORS[index % RIDER_COLORS.length];
}

export function zoneColor(index: number): string {
  return ZONE_COLORS[index % ZONE_COLORS.length];
}

export function km(meters: number | null | undefined, digits = 1): string {
  return meters === null || meters === undefined ? '–' : `${(meters / 1000).toFixed(digits)} km`;
}

export function duration(ms: number | null | undefined): string {
  if (ms === null || ms === undefined) {
    return '–';
  }
  if (ms < 1000) {
    return `${ms < 1 ? ms.toFixed(2) : ms < 10 ? ms.toFixed(1) : Math.round(ms)} ms`;
  }
  return `${(ms / 1000).toFixed(ms < 10_000 ? 2 : 1)} s`;
}

export function minutes(seconds: number | null | undefined): string {
  if (seconds === null || seconds === undefined) {
    return '–';
  }
  const total = Math.round(seconds / 60);
  return total >= 60 ? `${Math.floor(total / 60)} h ${total % 60} min` : `${total} min`;
}

export function clockToSeconds(clock: string): number {
  const [h, m, s] = clock.split(':').map(Number);
  return h * 3600 + m * 60 + (s || 0);
}

export function clock(secondsOfDay: number): string {
  const whole = Math.round(secondsOfDay);
  const h = Math.floor(whole / 3600) % 24;
  const m = Math.floor((whole % 3600) / 60);
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
}

export function percent(value: number | null | undefined, digits = 1): string {
  return value === null || value === undefined ? '–' : `${(value * 100).toFixed(digits)}%`;
}

export function signedPercent(value: number, digits = 1): string {
  return `${value > 0 ? '+' : value < 0 ? '−' : ''}${Math.abs(value).toFixed(digits)}%`;
}

export function number(value: number, digits = 1): string {
  return Number.isInteger(value) ? String(value) : value.toFixed(digits);
}
