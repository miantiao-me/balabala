import { join } from "node:path";
import type { CourseUnit } from "./course-pack";
import { buildCurriculumUnits } from "./build-curriculum";

export function buildCurriculum(root: string): CourseUnit[] {
  return buildCurriculumUnits(root, join(root, ".build/curriculum"));
}
