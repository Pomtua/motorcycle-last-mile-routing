"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.UpdateInstanceDto = void 0;
const mapped_types_1 = require("@nestjs/mapped-types");
const create_instance_dto_1 = require("./create-instance.dto");
class UpdateInstanceDto extends (0, mapped_types_1.PartialType)(create_instance_dto_1.CreateInstanceDto) {
}
exports.UpdateInstanceDto = UpdateInstanceDto;
//# sourceMappingURL=update-instance.dto.js.map