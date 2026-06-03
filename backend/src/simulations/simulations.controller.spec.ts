import { Test, TestingModule } from '@nestjs/testing';
import { SimulationsController } from './simulations.controller';
import { SimulationsService } from './simulations.service';
import { CreateSimulationDto } from './dto/create-simulation.dto';

describe('SimulationsController', () => {
  let controller: SimulationsController;
  let service: SimulationsService;

  beforeEach(async () => {
    const module: TestingModule = await Test.createTestingModule({
      controllers: [SimulationsController],
      providers: [
        {
          provide: SimulationsService,
          useValue: {
            processCsv: jest.fn().mockReturnValue({ resources: {}, parcels: [] }),
            create: jest.fn().mockResolvedValue({ success: true, data: {} }),
          },
        },
      ],
    }).compile();

    controller = module.get<SimulationsController>(SimulationsController);
    service = module.get<SimulationsService>(SimulationsService);
  });

  it('should be defined', () => {
    expect(controller).toBeDefined();
  });

  describe('uploadFile', () => {
    it('should call processCsv and return JSON instance', () => {
      const file = {
        buffer: Buffer.from('lat,lng,weight\n13.7,100.5,5.0'),
        originalname: 'test.csv',
      } as any;

      const result = controller.uploadFile(file);
      expect(service.processCsv).toHaveBeenCalledWith(file);
      expect(result).toEqual({ resources: {}, parcels: [] });
    });
  });

  describe('create', () => {
    it('should call create simulation', async () => {
      const dto: CreateSimulationDto = { solver: 'GA', instanceJson: {} };
      const result = await controller.create(dto);
      expect(service.create).toHaveBeenCalledWith(dto);
      expect(result).toEqual({ success: true, data: {} });
    });
  });
});
