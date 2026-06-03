import { ApiProperty } from '@nestjs/swagger';

export class CreateSimulationDto {
  @ApiProperty({ example: 'GA', description: 'The solver algorithm to use (GA, NN, or ALL)' })
  solver: string;

  @ApiProperty({ description: 'The JSON instance data representing resources and parcels' })
  instanceJson: any;
}
