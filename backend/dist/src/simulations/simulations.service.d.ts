import { UpdateSimulationDto } from './dto/update-simulation.dto';
export declare class SimulationsService {
    private readonly logger;
    private benchmarkRunning;
    private benchmarkProgress;
    private activeBenchmarkProcess;
    processCsv(file: Express.Multer.File): {
        resources: {
            depot_location: {
                lat: number;
                lng: number;
                osm_id: string;
            };
            num_drivers: number;
            max_weight_capacity: number;
            max_volume_capacity: number;
        };
        parcels: {
            id: number;
            lat: number;
            lng: number;
            weight: number;
            volume: number;
            time_window: number[];
        }[];
    };
    create(createSimulationDto: any): Promise<{
        success: boolean;
        data: any;
    }>;
    findAll(): string;
    findOne(id: number): string;
    update(id: number, updateSimulationDto: UpdateSimulationDto): string;
    remove(id: number): string;
    triggerBenchmark(): {
        success: boolean;
        message: string;
    };
    stopBenchmark(): {
        success: boolean;
        message: string;
    };
    getBenchmarkStatus(): {
        running: boolean;
        progress: {
            current: number;
            total: number;
            activeInstance: string;
        };
    };
    getPrecomputedResults(): unknown[];
}
