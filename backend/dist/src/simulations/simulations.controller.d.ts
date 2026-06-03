import { SimulationsService } from './simulations.service';
import { CreateSimulationDto } from './dto/create-simulation.dto';
import { UpdateSimulationDto } from './dto/update-simulation.dto';
export declare class SimulationsController {
    private readonly simulationsService;
    constructor(simulationsService: SimulationsService);
    uploadFile(file: Express.Multer.File): {
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
    create(createSimulationDto: CreateSimulationDto): Promise<{
        success: boolean;
        data: any;
    }>;
    runBenchmark(): {
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
    getPrecomputedBenchmark(): unknown[];
    findAll(): string;
    findOne(id: string): string;
    update(id: string, updateSimulationDto: UpdateSimulationDto): string;
    remove(id: string): string;
}
