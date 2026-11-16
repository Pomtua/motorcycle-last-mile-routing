import { parse } from 'csv-parse/sync';
import { parseClock } from './time';

export interface ParcelCustomer {
  externalId: string;
  lng: number;
  lat: number;
  demandWeight: number;
  demandVolume: number;
  twStartS: number;
  twEndS: number;
  serviceS: number;
}

export interface ParcelSet {
  depot: { lng: number; lat: number };
  customers: ParcelCustomer[];
}

export interface CsvParcelSet extends ParcelSet {
  rows: { depot: number; customers: number[] };
}

export interface RowError {
  row: number;
  message: string;
}

export class ParcelError extends Error {
  constructor(readonly errors: RowError[]) {
    super(errors.map((error) => (error.row > 0 ? `row ${error.row}: ` : '') + error.message).join('; '));
  }
}

export const CSV_COLUMNS = [
  'type',
  'id',
  'lat',
  'lng',
  'weight_kg',
  'volume_l',
  'tw_start',
  'tw_end',
  'service_min',
] as const;

export const CSV_TEMPLATE = [
  CSV_COLUMNS.join(','),
  'depot,DEPOT,13.7563,100.5018,,,,,',
  'customer,P001,13.7468,100.5350,2.5,4,09:00,12:00,3',
  'customer,P002,13.7650,100.5380,1.2,2,10:00,14:00,2',
].join('\n');

const MAX_ERRORS = 50;

export const WEIGHT_STEP_KG = 0.01;
export const VOLUME_STEP_L = 0.1;

export function onGrid(value: number, step: number): boolean {
  const units = value / step;
  return Math.abs(units - Math.round(units)) <= 1e-6;
}

export function parseParcelCsv(text: string, maxCustomers: number): CsvParcelSet {
  let records: Record<string, string>[];
  try {
    records = parse(text.replace(/^﻿/, ''), {
      columns: (header: string[]) => header.map((name) => name.trim().toLowerCase()),
      skip_empty_lines: true,
      trim: true,
    });
  } catch (error) {
    throw new ParcelError([{ row: 0, message: `cannot read the CSV: ${(error as Error).message}` }]);
  }
  if (records.length === 0) {
    throw new ParcelError([{ row: 0, message: 'the CSV has no rows' }]);
  }
  const missing = CSV_COLUMNS.filter((column) => !(column in records[0]));
  if (missing.length > 0) {
    throw new ParcelError([{ row: 1, message: `missing columns: ${missing.join(', ')}` }]);
  }

  const errors: RowError[] = [];
  const customers: ParcelCustomer[] = [];
  const seen = new Set<string>();
  const customerRows: number[] = [];
  let depot: { lng: number; lat: number } | null = null;
  let depotRow = 0;

  for (const [index, record] of records.entries()) {
    const row = index + 2;
    const fail = (message: string) => errors.push({ row, message });
    const type = record.type.toLowerCase();
    const id = record.id;
    const lat = Number(record.lat);
    const lng = Number(record.lng);
    if (record.lat === '' || !Number.isFinite(lat) || lat < -90 || lat > 90) {
      fail('lat must be a number between -90 and 90');
      continue;
    }
    if (record.lng === '' || !Number.isFinite(lng) || lng < -180 || lng > 180) {
      fail('lng must be a number between -180 and 180');
      continue;
    }

    if (type === 'depot') {
      if (depot) {
        fail('only one depot row is allowed');
        continue;
      }
      depot = { lng, lat };
      depotRow = row;
      continue;
    }
    if (type !== 'customer') {
      fail(`type must be depot or customer, got "${record.type}"`);
      continue;
    }

    if (!id) {
      fail('id is required');
      continue;
    }
    if (seen.has(id)) {
      fail(`id ${id} appears more than once`);
      continue;
    }
    seen.add(id);

    const weight = Number(record.weight_kg);
    const volumeL = Number(record.volume_l);
    const serviceMin = record.service_min === '' ? 0 : Number(record.service_min);
    const twStart = parseClock(record.tw_start);
    const twEnd = parseClock(record.tw_end);
    if (record.weight_kg === '' || !Number.isFinite(weight) || weight < WEIGHT_STEP_KG || !onGrid(weight, WEIGHT_STEP_KG)) {
      fail('weight_kg must be at least 0.01 with at most 2 decimal places');
      continue;
    }
    if (record.volume_l === '' || !Number.isFinite(volumeL) || volumeL < VOLUME_STEP_L || !onGrid(volumeL, VOLUME_STEP_L)) {
      fail('volume_l must be at least 0.1 with at most 1 decimal place');
      continue;
    }
    if (!Number.isFinite(serviceMin) || serviceMin < 0) {
      fail('service_min must be zero or a positive number');
      continue;
    }
    if (twStart === null || twEnd === null) {
      fail('tw_start and tw_end must be times like 09:00');
      continue;
    }
    if (twStart > twEnd) {
      fail('tw_start must not be after tw_end');
      continue;
    }
    customers.push({
      externalId: id,
      lng,
      lat,
      demandWeight: weight,
      demandVolume: volumeL / 1000,
      twStartS: twStart,
      twEndS: twEnd,
      serviceS: Math.round(serviceMin * 60),
    });
    customerRows.push(row);
  }

  if (!depot) {
    errors.push({ row: 0, message: 'exactly one row with type depot is required' });
  }
  if (customers.length === 0 && errors.length === 0) {
    errors.push({ row: 0, message: 'at least one customer row is required' });
  }
  if (customers.length > maxCustomers) {
    errors.push({ row: 0, message: `at most ${maxCustomers} customers are supported, got ${customers.length}` });
  }
  if (errors.length > 0) {
    throw new ParcelError(errors.slice(0, MAX_ERRORS));
  }
  return { depot: depot as { lng: number; lat: number }, customers, rows: { depot: depotRow, customers: customerRows } };
}
