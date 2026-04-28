export interface AIInference {
  x: number;
  y: number;
  width: number;
  height: number;
  confidence: number;
  class: string;
  class_id: number;
  detection_id: string;
}

export interface AIResponse {
  camera_id: string;
  predictions: AIInference[];
}
