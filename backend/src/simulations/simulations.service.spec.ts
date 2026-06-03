import { Test, TestingModule } from '@nestjs/testing';
import { SimulationsService } from './simulations.service';
import { BadRequestException } from '@nestjs/common';
import * as fs from 'fs';
import * as path from 'path';

jest.mock('child_process', () => ({
  exec: jest.fn((cmd, options, cb) => {
    const callback = typeof options === 'function' ? options : cb;
    callback(null, { stdout: JSON.stringify({ success: true, solver_name: "GA", execution_time_ms: 10, total_distance: 10, routes: [] }) });
  }),
}));

describe('SimulationsService', () => {
  let service: SimulationsService;

  beforeEach(async () => {
    const module: TestingModule = await Test.createTestingModule({
      providers: [SimulationsService],
    }).compile();

    service = module.get<SimulationsService>(SimulationsService);
  });

  describe('processCsv - Negative Validation', () => {
    it('should throw BadRequestException if no file is provided', () => {
      expect(() => service.processCsv(null)).toThrow(BadRequestException);
    });

    it('should throw BadRequestException on malformed CSV columns', () => {
      const file = {
        buffer: Buffer.from('invalid,headers,no_lat_or_lng\n1,2,3'),
        originalname: 'test.csv'
      } as any;
      expect(() => service.processCsv(file)).toThrow(BadRequestException);
    });

    it('should parse invalid float fields with default fallbacks safely', () => {
      const file = {
        buffer: Buffer.from('id,snapped_lat,snapped_lng,weight,volume\n1,13.7,100.5,invalid,10'),
        originalname: 'test.csv'
      } as any;
      const res = service.processCsv(file);
      expect(res.parcels[0].weight).toBe(5.0);
    });
  });

  describe('create - Process Orchestration & Isolation', () => {
    it('should create separate UUID JSON temp files for concurrent executions', async () => {
      const writeSpy = jest.spyOn(fs, 'writeFileSync').mockImplementation(() => {});
      const unlinkSpy = jest.spyOn(fs, 'unlinkSync').mockImplementation(() => {});
      
      const payload1 = { solver: 'GA', instanceJson: { parcels: [], resources: {} } };
      const payload2 = { solver: 'GA', instanceJson: { parcels: [], resources: {} } };

      await Promise.all([service.create(payload1), service.create(payload2)]);

      expect(writeSpy).toHaveBeenCalledTimes(2);
      const firstPath = writeSpy.mock.calls[0][0] as string;
      const secondPath = writeSpy.mock.calls[1][0] as string;
      expect(firstPath).not.toBe(secondPath);

      writeSpy.mockRestore();
      unlinkSpy.mockRestore();
    });

    it('should clean up temp files and throw on execution timeout or solver crash', async () => {
      const execMock = require('child_process').exec;
      execMock.mockImplementationOnce((cmd, options, cb) => {
        const callback = typeof options === 'function' ? options : cb;
        callback(new Error('Process timed out'), '');
      });

      const payload = { solver: 'GA', instanceJson: { parcels: [], resources: {} } };
      await expect(service.create(payload)).rejects.toThrow('Simulation failed');
    });
  });
});
