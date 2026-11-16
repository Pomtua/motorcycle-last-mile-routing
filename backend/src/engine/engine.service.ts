import { Injectable } from '@nestjs/common';
import { spawn } from 'node:child_process';
import { access, constants } from 'node:fs/promises';
import { config } from '../config';
import { EngineRequest, EngineResult } from './engine.types';

export class EngineError extends Error {}

export class EngineCancelled extends Error {
  constructor() {
    super('cancelled');
  }
}

@Injectable()
export class EngineService {
  async available(): Promise<boolean> {
    try {
      await access(config.enginePath, constants.X_OK);
      return true;
    } catch {
      return false;
    }
  }

  solve(request: EngineRequest, signal: AbortSignal): Promise<EngineResult> {
    return new Promise((resolve, reject) => {
      if (signal.aborted) {
        reject(new EngineCancelled());
        return;
      }
      const child = spawn(config.enginePath, [], { stdio: ['pipe', 'pipe', 'pipe'] });
      const stdout: Buffer[] = [];
      const stderr: Buffer[] = [];
      let settled = false;

      const finish = (error: Error | null, result?: EngineResult) => {
        if (settled) {
          return;
        }
        settled = true;
        signal.removeEventListener('abort', onAbort);
        if (error) {
          reject(error);
        } else {
          resolve(result as EngineResult);
        }
      };
      const onAbort = () => {
        child.kill('SIGTERM');
        finish(new EngineCancelled());
      };
      signal.addEventListener('abort', onAbort, { once: true });

      child.stdout.on('data', (chunk: Buffer) => stdout.push(chunk));
      child.stderr.on('data', (chunk: Buffer) => stderr.push(chunk));
      child.stdin.on('error', () => undefined);
      child.on('error', (error) =>
        finish(new EngineError(`cannot start the routing engine at ${config.enginePath}: ${error.message}`)),
      );
      child.on('close', (code, killSignal) => {
        const text = Buffer.concat(stdout).toString('utf8').trim();
        let parsed: { status?: string; message?: string } | undefined;
        try {
          parsed = text ? JSON.parse(text) : undefined;
        } catch {
          parsed = undefined;
        }
        if (parsed?.status === 'ok' && code === 0) {
          finish(null, parsed as EngineResult);
          return;
        }
        const detail =
          parsed?.message ??
          (Buffer.concat(stderr).toString('utf8').trim() ||
            `engine exited with ${killSignal ? `signal ${killSignal}` : `code ${code}`}`);
        finish(new EngineError(detail));
      });

      child.stdin.end(JSON.stringify(request));
    });
  }
}
