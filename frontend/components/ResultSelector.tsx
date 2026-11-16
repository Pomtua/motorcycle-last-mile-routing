'use client';

import { Algorithm } from '@/lib/api';
import { ALGORITHM_COLOR, ALGORITHM_LABEL } from '@/lib/format';

export default function ResultSelector({
  algorithms,
  shown,
  onShown,
}: {
  algorithms: Algorithm[];
  shown: Algorithm | null;
  onShown: (algorithm: Algorithm) => void;
}) {
  return (
    <div className="flex items-center gap-3">
      <span className="text-sm font-medium text-muted">Showing</span>
      <div className="inline-flex rounded-lg bg-white p-1 shadow-sm ring-1 ring-line">
        {algorithms.map((algorithm) => (
          <button
            key={algorithm}
            onClick={() => onShown(algorithm)}
            className={`flex items-center gap-2 rounded-md px-3 py-1.5 text-sm font-medium transition-colors ${
              shown === algorithm ? 'bg-primary-tint text-primary' : 'text-ink hover:bg-zebra'
            }`}
          >
            <span className="h-2.5 w-2.5 rounded-full" style={{ background: ALGORITHM_COLOR[algorithm] }} />
            {ALGORITHM_LABEL[algorithm]}
          </button>
        ))}
      </div>
    </div>
  );
}
