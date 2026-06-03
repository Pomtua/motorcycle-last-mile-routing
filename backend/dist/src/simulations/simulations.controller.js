"use strict";
var __decorate = (this && this.__decorate) || function (decorators, target, key, desc) {
    var c = arguments.length, r = c < 3 ? target : desc === null ? desc = Object.getOwnPropertyDescriptor(target, key) : desc, d;
    if (typeof Reflect === "object" && typeof Reflect.decorate === "function") r = Reflect.decorate(decorators, target, key, desc);
    else for (var i = decorators.length - 1; i >= 0; i--) if (d = decorators[i]) r = (c < 3 ? d(r) : c > 3 ? d(target, key, r) : d(target, key)) || r;
    return c > 3 && r && Object.defineProperty(target, key, r), r;
};
var __metadata = (this && this.__metadata) || function (k, v) {
    if (typeof Reflect === "object" && typeof Reflect.metadata === "function") return Reflect.metadata(k, v);
};
var __param = (this && this.__param) || function (paramIndex, decorator) {
    return function (target, key) { decorator(target, key, paramIndex); }
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.SimulationsController = void 0;
const common_1 = require("@nestjs/common");
const simulations_service_1 = require("./simulations.service");
const create_simulation_dto_1 = require("./dto/create-simulation.dto");
const update_simulation_dto_1 = require("./dto/update-simulation.dto");
const platform_express_1 = require("@nestjs/platform-express");
const swagger_1 = require("@nestjs/swagger");
let SimulationsController = class SimulationsController {
    constructor(simulationsService) {
        this.simulationsService = simulationsService;
    }
    uploadFile(file) {
        return this.simulationsService.processCsv(file);
    }
    create(createSimulationDto) {
        return this.simulationsService.create(createSimulationDto);
    }
    runBenchmark() {
        return this.simulationsService.triggerBenchmark();
    }
    stopBenchmark() {
        return this.simulationsService.stopBenchmark();
    }
    getBenchmarkStatus() {
        return this.simulationsService.getBenchmarkStatus();
    }
    getPrecomputedBenchmark() {
        return this.simulationsService.getPrecomputedResults();
    }
    findAll() {
        return this.simulationsService.findAll();
    }
    findOne(id) {
        return this.simulationsService.findOne(+id);
    }
    update(id, updateSimulationDto) {
        return this.simulationsService.update(+id, updateSimulationDto);
    }
    remove(id) {
        return this.simulationsService.remove(+id);
    }
};
exports.SimulationsController = SimulationsController;
__decorate([
    (0, common_1.Post)('upload'),
    (0, swagger_1.ApiOperation)({ summary: 'Upload a CSV file containing parcel data to convert to instance JSON format' }),
    (0, swagger_1.ApiConsumes)('multipart/form-data'),
    (0, swagger_1.ApiBody)({
        schema: {
            type: 'object',
            properties: {
                file: {
                    type: 'string',
                    format: 'binary',
                },
            },
        },
    }),
    (0, common_1.UseInterceptors)((0, platform_express_1.FileInterceptor)('file')),
    __param(0, (0, common_1.UploadedFile)()),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", [Object]),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "uploadFile", null);
__decorate([
    (0, common_1.Post)(),
    (0, swagger_1.ApiOperation)({ summary: 'Create and run a new simulation using the C++ engine' }),
    __param(0, (0, common_1.Body)()),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", [create_simulation_dto_1.CreateSimulationDto]),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "create", null);
__decorate([
    (0, common_1.Post)('benchmark'),
    (0, swagger_1.ApiOperation)({ summary: 'Run the algorithm benchmark on all 300 instances' }),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", []),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "runBenchmark", null);
__decorate([
    (0, common_1.Post)('benchmark/stop'),
    (0, swagger_1.ApiOperation)({ summary: 'Interrupt and stop the active batch benchmark execution' }),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", []),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "stopBenchmark", null);
__decorate([
    (0, common_1.Get)('benchmark/status'),
    (0, swagger_1.ApiOperation)({ summary: 'Get current on-the-fly benchmark execution status' }),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", []),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "getBenchmarkStatus", null);
__decorate([
    (0, common_1.Get)('benchmark/precomputed'),
    (0, swagger_1.ApiOperation)({ summary: 'Get instantly parsed pre-computed benchmark results' }),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", []),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "getPrecomputedBenchmark", null);
__decorate([
    (0, common_1.Get)(),
    (0, swagger_1.ApiOperation)({ summary: 'Get all simulation runs' }),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", []),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "findAll", null);
__decorate([
    (0, common_1.Get)(':id'),
    (0, swagger_1.ApiOperation)({ summary: 'Get a specific simulation run by ID' }),
    __param(0, (0, common_1.Param)('id')),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", [String]),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "findOne", null);
__decorate([
    (0, common_1.Patch)(':id'),
    (0, swagger_1.ApiOperation)({ summary: 'Update a simulation run' }),
    __param(0, (0, common_1.Param)('id')),
    __param(1, (0, common_1.Body)()),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", [String, update_simulation_dto_1.UpdateSimulationDto]),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "update", null);
__decorate([
    (0, common_1.Delete)(':id'),
    (0, swagger_1.ApiOperation)({ summary: 'Delete a simulation run' }),
    __param(0, (0, common_1.Param)('id')),
    __metadata("design:type", Function),
    __metadata("design:paramtypes", [String]),
    __metadata("design:returntype", void 0)
], SimulationsController.prototype, "remove", null);
exports.SimulationsController = SimulationsController = __decorate([
    (0, swagger_1.ApiTags)('Simulations'),
    (0, common_1.Controller)('simulations'),
    __metadata("design:paramtypes", [simulations_service_1.SimulationsService])
], SimulationsController);
//# sourceMappingURL=simulations.controller.js.map