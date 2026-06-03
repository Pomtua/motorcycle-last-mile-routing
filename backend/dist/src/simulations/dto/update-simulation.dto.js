"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.UpdateSimulationDto = void 0;
const mapped_types_1 = require("@nestjs/mapped-types");
const create_simulation_dto_1 = require("./create-simulation.dto");
class UpdateSimulationDto extends (0, mapped_types_1.PartialType)(create_simulation_dto_1.CreateSimulationDto) {
}
exports.UpdateSimulationDto = UpdateSimulationDto;
//# sourceMappingURL=update-simulation.dto.js.map