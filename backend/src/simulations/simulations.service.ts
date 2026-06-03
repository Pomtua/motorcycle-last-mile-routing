import { Injectable, Logger, BadRequestException } from '@nestjs/common';
import { CreateSimulationDto } from './dto/create-simulation.dto';
import { UpdateSimulationDto } from './dto/update-simulation.dto';
import { exec, spawn } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import * as util from 'util';
import * as crypto from 'crypto';
import { parse } from 'csv-parse/sync';

const execAsync = util.promisify(exec);

@Injectable()
export class SimulationsService {
  private readonly logger = new Logger(SimulationsService.name);
  private benchmarkRunning = false;
  private benchmarkProgress = { current: 0, total: 0, activeInstance: '' };
  private activeBenchmarkProcess: any = null;



  processCsv(file: Express.Multer.File) {
    if (!file) {
      throw new BadRequestException('No file uploaded');
    }

    try {
      const csvContent = file.buffer.toString('utf-8');
      const records = parse(csvContent, {
        columns: true,
        skip_empty_lines: true,
      });

      // Simple transformation assuming standard headers like id, lat, lng, weight, volume, time_window_start, time_window_end
      const parcels = records.map((record: any, index: number) => {
        const lat = parseFloat(record.lat || record.snapped_lat);
        const lng = parseFloat(record.lng || record.snapped_lng);

        if (isNaN(lat) || isNaN(lng)) {
          throw new Error('Invalid coordinates: snapped_lat/snapped_lng or lat/lng must be valid numbers');
        }

        const parsedWeight = parseFloat(record.weight);
        const parsedVolume = parseFloat(record.volume);

        return {
          id: record.id ? parseInt(record.id) : index + 1,
          lat,
          lng,
          weight: isNaN(parsedWeight) ? 5.0 : parsedWeight,
          volume: isNaN(parsedVolume) ? 10.0 : parsedVolume,
          time_window: [
            parseInt(record.time_window_start || record.tw_start || 480),
            parseInt(record.time_window_end || record.tw_end || 1200)
          ]
        };
      });

      // Default resources
      const resources = {
        depot_location: {
          lat: 13.7563,
          lng: 100.5018,
          osm_id: "depot_1"
        },
        num_drivers: 5,
        max_weight_capacity: 20,
        max_volume_capacity: 50
      };

      return {
        resources,
        parcels
      };
    } catch (error) {
      this.logger.error(`Error parsing CSV: ${error.message}`);
      throw new BadRequestException('Invalid CSV format');
    }
  }

  async create(createSimulationDto: any) {
    const { solver, instanceJson } = createSimulationDto;
    
    // Save instance to a temporary file
    const tempDir = path.join(process.cwd(), 'temp');
    if (!fs.existsSync(tempDir)) {
      fs.mkdirSync(tempDir);
    }
    const tempFileName = `${crypto.randomUUID()}.json`;
    const tempFilePath = path.join(tempDir, tempFileName);
    fs.writeFileSync(tempFilePath, JSON.stringify(instanceJson));

    const enginePath = path.resolve(process.cwd(), '../algorithm/build/routing_engine');

    try {
      // Execute C++ routing engine
      this.logger.log(`Running engine for solver ${solver} on ${tempFilePath}`);
      const command = `${enginePath} --json --solver ${solver} ${tempFilePath}`;
      const { stdout, stderr } = await execAsync(command, { env: { ...process.env } });

      // Try to parse the last valid JSON block from stdout
      const lines = stdout.split('\n');
      let jsonStr = stdout;
      for (let i = 0; i < lines.length; i++) {
         if (lines[i].trim().startsWith('{')) {
             jsonStr = lines.slice(i).join('\n');
             break;
         }
      }

      const result = JSON.parse(jsonStr);

      // Clean up temp file
      fs.unlinkSync(tempFilePath);

      return {
        success: true,
        data: result
      };

    } catch (error) {
      this.logger.error(`Failed to run simulation: ${error.message}`);
      if (fs.existsSync(tempFilePath)) {
        fs.unlinkSync(tempFilePath);
      }
      throw new Error(`Simulation failed: ${error.message}`);
    }
  }

  findAll() {
    return `This action returns all simulations`;
  }

  findOne(id: number) {
    return `This action returns a #${id} simulation`;
  }

  update(id: number, updateSimulationDto: UpdateSimulationDto) {
    return `This action updates a #${id} simulation`;
  }

  remove(id: number) {
    return `This action removes a #${id} simulation`;
  }

  triggerBenchmark() {
    if (this.benchmarkRunning) {
      return { success: true, message: 'Benchmark already running' };
    }
    this.benchmarkRunning = true;
    this.benchmarkProgress = { current: 0, total: 0, activeInstance: '' };
    const buildDir = path.resolve(process.cwd(), '../algorithm/build');
    const binaryPath = path.resolve(buildDir, 'run_benchmark');
    const child = spawn(binaryPath, [], { cwd: buildDir, env: { ...process.env } });
    this.activeBenchmarkProcess = child;
    let stdoutBuffer = '';
    child.stdout.on('data', (data) => {
      stdoutBuffer += data.toString();
      const lines = stdoutBuffer.split('\n');
      stdoutBuffer = lines.pop() || '';
      for (const line of lines) {
        const match = line.match(/\[(\d+)\/(\d+)\] Processing:\s*(\S+)/);
        if (match) {
          this.benchmarkProgress = {
            current: parseInt(match[1]),
            total: parseInt(match[2]),
            activeInstance: match[3]
          };
        }
      }
    });
    child.on('close', () => {
      this.benchmarkRunning = false;
      this.activeBenchmarkProcess = null;
    });
    child.on('error', () => {
      this.benchmarkRunning = false;
      this.activeBenchmarkProcess = null;
    });
    return { success: true, message: 'Benchmark started' };
  }

  stopBenchmark() {
    if (this.benchmarkRunning && this.activeBenchmarkProcess) {
      this.activeBenchmarkProcess.kill('SIGINT');
      this.benchmarkRunning = false;
      this.activeBenchmarkProcess = null;
      return { success: true, message: 'Benchmark interrupted' };
    }
    return { success: false, message: 'No active benchmark' };
  }

  getBenchmarkStatus() {
    return {
      running: this.benchmarkRunning,
      progress: this.benchmarkProgress
    };
  }

  getPrecomputedResults() {
    const csvPath = path.resolve(process.cwd(), '../algorithm/build/benchmark_results.csv');
    if (!fs.existsSync(csvPath)) {
      throw new BadRequestException('Benchmark results CSV file not found.');
    }
    try {
      const csvContent = fs.readFileSync(csvPath, 'utf-8');
      const records = parse(csvContent, {
        columns: true,
        skip_empty_lines: true,
        cast: true
      });
      return records;
    } catch (error) {
      throw new Error(`Failed to parse benchmark results CSV: ${error.message}`);
    }
  }
}

