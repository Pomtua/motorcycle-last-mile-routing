import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { ParcelError } from '../src/domain/parcels';
import { DEFAULT_MAX_SNAP_DISTANCE_M, parseMaxSnapDistance, snapErrors } from '../src/domain/snapping';

describe('parseMaxSnapDistance', () => {
  it('uses the default when the field is missing', () => {
    assert.equal(parseMaxSnapDistance(undefined), DEFAULT_MAX_SNAP_DISTANCE_M);
    assert.equal(parseMaxSnapDistance(''), DEFAULT_MAX_SNAP_DISTANCE_M);
  });

  it('reads metres from the form field', () => {
    assert.equal(parseMaxSnapDistance('350'), 350);
    assert.equal(parseMaxSnapDistance('12.5'), 12.5);
  });

  it('rejects values that are not a positive distance', () => {
    for (const value of ['0', '-5', 'abc', '100001']) {
      assert.throws(() => parseMaxSnapDistance(value), ParcelError);
    }
  });
});

describe('snapErrors', () => {
  it('lists every point beyond the limit with its CSV row', () => {
    const errors = snapErrors(
      [
        { row: 2, label: 'Depot', distance: 12 },
        { row: 3, label: 'Parcel P001', distance: 200 },
        { row: 4, label: 'Parcel P002', distance: 850.4 },
      ],
      200,
    );
    assert.deepEqual(errors, [{ row: 4, message: 'Parcel P002 is 850 m from the nearest road (limit 200 m)' }]);
  });
});
