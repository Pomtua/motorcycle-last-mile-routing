import { InstancesService } from './instances.service';
export declare class InstancesController {
    private readonly instancesService;
    constructor(instancesService: InstancesService);
    findAll(): {
        [key: string]: string[];
    };
    findOne(type: string, filename: string): any;
}
