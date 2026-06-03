import { Injectable, NotFoundException } from '@nestjs/common';
import * as fs from 'fs';
import * as path from 'path';

@Injectable()
export class InstancesService {
  findAll() {
    const baseDir = path.resolve(process.cwd(), '../data/instances');
    const categories = ['scale', 'split_capability', 'tightness'];
    const result: { [key: string]: string[] } = {};

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
      } else {
        result[cat] = [];
      }
    });

    return result;
  }

  findOne(type: string, filename: string) {
    const baseDir = path.resolve(process.cwd(), '../data/instances');
    const filePath = path.join(baseDir, type, filename);
    
    if (!fs.existsSync(filePath)) {
      throw new NotFoundException(`Instance file ${filename} of type ${type} not found`);
    }

    try {
      const content = fs.readFileSync(filePath, 'utf-8');
      return JSON.parse(content);
    } catch (error) {
      throw new Error(`Failed to parse instance JSON: ${error.message}`);
    }
  }
}
