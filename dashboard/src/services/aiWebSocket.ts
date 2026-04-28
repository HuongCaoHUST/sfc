import { AIInference, AIResponse } from '../types';

const AI_CONTAINER_WS_URL = `ws://${window.location.host}`;

export type PredictionCallback = (predictions: AIInference[]) => void;
export const wsListeners = new Map<string, PredictionCallback>();

let sharedWs: WebSocket | null = null;
let wsConnecting = false;

export function connectSharedWS() {
  if (sharedWs?.readyState === WebSocket.OPEN || wsConnecting) return;
  wsConnecting = true;

  const ws = new WebSocket(AI_CONTAINER_WS_URL);
  ws.onopen = () => {
    sharedWs = ws;
    wsConnecting = false;
  };
  ws.onmessage = (event) => {
    try {
      const data: AIResponse = JSON.parse(event.data);
      const cb = wsListeners.get(data.camera_id);
      if (cb) cb(data.predictions);
    } catch (_) {}
  };
  ws.onclose = () => {
    sharedWs = null;
    wsConnecting = false;
    wsListeners.forEach((cb) => cb([]));
    setTimeout(connectSharedWS, 2000);
  };
  ws.onerror = () => ws.close();
}

// Tự động kết nối khi module được load
if (typeof window !== 'undefined') {
  connectSharedWS();
}
