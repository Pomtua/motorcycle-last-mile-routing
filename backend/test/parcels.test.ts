import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { CSV_TEMPLATE, ParcelError, parseParcelCsv } from '../src/domain/parcels';

const HEADER = 'type,id,lat,lng,weight_kg,volume_l,tw_start,tw_end,service_min';

function errorsOf(text: string): { row: number; message: string }[] {
  try {
    parseParcelCsv(text, 1999);
  } catch (error) {
    assert.ok(error instanceof ParcelError);
    return error.errors;
  }
  assert.fail('expected a ParcelError');
}

describe('parseParcelCsv', () => {
  it('reads the template with SI units', () => {
    const parcels = parseParcelCsv(CSV_TEMPLATE, 1999);
    assert.deepEqual(parcels.depot, { lng: 100.5018, lat: 13.7563 });
    assert.equal(parcels.customers.length, 2);
    const first = parcels.customers[0];
    assert.equal(first.externalId, 'P001');
    assert.equal(first.demandWeight, 2.5);
    assert.equal(first.demandVolume, 0.004);
    assert.equal(first.twStartS, 9 * 3600);
    assert.equal(first.twEndS, 12 * 3600);
    assert.equal(first.serviceS, 180);
    assert.deepEqual(parcels.rows, { depot: 2, customers: [3, 4] });
  });

  it('accepts a byte-order mark, upper-case headers and HH:MM:SS times', () => {
    const text = '﻿' + HEADER.toUpperCase() + '\ndepot,D,13.7,100.5,,,,,\ncustomer,A,13.71,100.51,1,1,09:00:30,10:00,0\n';
    const parcels = parseParcelCsv(text, 1999);
    assert.equal(parcels.customers[0].twStartS, 9 * 3600 + 30);
  });

  it('defaults an empty service time to zero', () => {
    const parcels = parseParcelCsv(`${HEADER}\ndepot,D,13.7,100.5,,,,,\ncustomer,A,13.71,100.51,1,1,09:00,10:00,\n`, 1999);
    assert.equal(parcels.customers[0].serviceS, 0);
  });

  it('reports every bad row with its CSV row number', () => {
    const errors = errorsOf(
      [
        HEADER,
        'depot,D,13.7,100.5,,,,,',
        'customer,A,13.7,100.5,-1,2,09:00,10:00,1',
        'customer,B,95,100.5,1,2,09:00,10:00,1',
        'customer,C,13.7,100.5,1,0,09:00,10:00,1',
        'customer,D,13.7,100.5,1,2,11:00,10:00,1',
        'customer,E,13.7,100.5,1,2,9am,10:00,1',
        'customer,F,13.7,100.5,1,2,09:00,10:00,1',
        'customer,F,13.7,100.5,1,2,09:00,10:00,1',
        'truck,G,13.7,100.5,1,2,09:00,10:00,1',
      ].join('\n'),
    );
    assert.deepEqual(
      errors.map((error) => error.row),
      [3, 4, 5, 6, 7, 9, 10],
    );
    assert.match(errors[0].message, /weight_kg/);
    assert.match(errors[1].message, /lat/);
    assert.match(errors[2].message, /volume_l/);
    assert.match(errors[3].message, /tw_start must not be after tw_end/);
    assert.match(errors[4].message, /times like 09:00/);
    assert.match(errors[5].message, /more than once/);
    assert.match(errors[6].message, /depot or customer/);
  });

  it('keeps weight and volume on the 0.01 kg and 0.1 L steps the engine uses', () => {
    const errors = errorsOf(
      [
        HEADER,
        'depot,D,13.7,100.5,,,,,',
        'customer,A,13.7,100.5,1.234,2,09:00,10:00,1',
        'customer,B,13.7,100.5,0.004,2,09:00,10:00,1',
        'customer,C,13.7,100.5,1.23,2.05,09:00,10:00,1',
        'customer,D,13.7,100.5,1.23,0.04,09:00,10:00,1',
        'customer,E,13.7,100.5,0.01,7.9,09:00,10:00,1',
      ].join('\n'),
    );
    assert.deepEqual(
      errors.map((error) => error.row),
      [3, 4, 5, 6],
    );
    assert.match(errors[0].message, /weight_kg must be at least 0.01 with at most 2 decimal places/);
    assert.match(errors[1].message, /weight_kg/);
    assert.match(errors[2].message, /volume_l must be at least 0.1 with at most 1 decimal place/);
    assert.match(errors[3].message, /volume_l/);
  });

  it('requires exactly one depot', () => {
    assert.match(errorsOf(`${HEADER}\ncustomer,A,13.7,100.5,1,2,09:00,10:00,1\n`)[0].message, /exactly one row with type depot/);
    const two = errorsOf(`${HEADER}\ndepot,D,13.7,100.5,,,,,\ndepot,E,13.7,100.5,,,,,\ncustomer,A,13.7,100.5,1,2,09:00,10:00,1\n`);
    assert.deepEqual(two, [{ row: 3, message: 'only one depot row is allowed' }]);
  });

  it('requires at least one customer and all columns', () => {
    assert.match(errorsOf(`${HEADER}\ndepot,D,13.7,100.5,,,,,\n`)[0].message, /at least one customer/);
    assert.match(errorsOf('type,id,lat,lng\ndepot,D,13.7,100.5\n')[0].message, /missing columns: weight_kg/);
  });

  it('enforces the customer limit', () => {
    const rows = [HEADER, 'depot,D,13.7,100.5,,,,,'];
    for (let i = 0; i < 3; i += 1) {
      rows.push(`customer,C${i},13.7,100.5,1,1,09:00,10:00,1`);
    }
    assert.throws(() => parseParcelCsv(rows.join('\n'), 2), (error: unknown) => {
      assert.ok(error instanceof ParcelError);
      assert.match(error.errors[0].message, /at most 2 customers/);
      return true;
    });
  });
});
