"use strict";
var __decorate = (this && this.__decorate) || function (decorators, target, key, desc) {
    var c = arguments.length, r = c < 3 ? target : desc === null ? desc = Object.getOwnPropertyDescriptor(target, key) : desc, d;
    if (typeof Reflect === "object" && typeof Reflect.decorate === "function") r = Reflect.decorate(decorators, target, key, desc);
    else for (var i = decorators.length - 1; i >= 0; i--) if (d = decorators[i]) r = (c < 3 ? d(r) : c > 3 ? d(target, key, r) : d(target, key)) || r;
    return c > 3 && r && Object.defineProperty(target, key, r), r;
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.InstancesService = void 0;
const common_1 = require("@nestjs/common");
const fs = require("fs");
const path = require("path");
let InstancesService = class InstancesService {
    findAll() {
        const baseDir = path.resolve(process.cwd(), '../data/instances');
        const categories = ['scale', 'split_capability', 'tightness'];
        const result = {};
        categories.forEach(cat => {
            const catDir = path.join(baseDir, cat);
            if (fs.existsSync(catDir)) {
                const files = fs.readdirSync(catDir)
                    .filter(file => file.endsWith('.json'))
                    .sort((a, b) => {
                    const aMatch = a.match(/_(\d+)(?:_(\d+))?\.json$/);
                    const bMatch = b.match(/_(\d+)(?:_(\d+))?\.json$/);
                    if (aMatch && bMatch) {
                        const aVal = parseInt(aMatch[1]);
                        const bVal = parseInt(bMatch[1]);
                        if (aVal !== bVal) {
                            return aVal - bVal;
                        }
                        const aSub = aMatch[2] ? parseInt(aMatch[2]) : 0;
                        const bSub = bMatch[2] ? parseInt(bMatch[2]) : 0;
                        return aSub - bSub;
                    }
                    return a.localeCompare(b);
                });
                result[cat] = files;
            }
            else {
                result[cat] = [];
            }
        });
        return result;
    }
    findOne(type, filename) {
        const baseDir = path.resolve(process.cwd(), '../data/instances');
        const filePath = path.join(baseDir, type, filename);
        if (!fs.existsSync(filePath)) {
            throw new common_1.NotFoundException(`Instance file ${filename} of type ${type} not found`);
        }
        try {
            const content = fs.readFileSync(filePath, 'utf-8');
            return JSON.parse(content);
        }
        catch (error) {
            throw new Error(`Failed to parse instance JSON: ${error.message}`);
        }
    }
};
exports.InstancesService = InstancesService;
exports.InstancesService = InstancesService = __decorate([
    (0, common_1.Injectable)()
], InstancesService);
//# sourceMappingURL=instances.service.js.map