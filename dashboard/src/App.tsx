/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React, { useEffect, useState, useRef } from 'react';
import { Shield, Settings, WifiOff } from 'lucide-react';

const CAMERAS = [
  { id: 'cam1', name: 'WEBCAM AI', url: `http://${window.location.hostname}:8889/cam1` },
  { id: 'cam2', name: 'Bãi đỗ xe (Parking Lot)', url: `http://${window.location.hostname}:8889/cam2` },
  { id: 'cam3', name: 'Hành lang tầng 1 (Hallway L1)', url: `http://${window.location.hostname}:8889/cam3` },
  { id: 'cam4', name: 'Kho hàng (Warehouse)', url: `http://${window.location.hostname}:8889/cam4` },
];

// Định nghĩa kiểu dữ liệu chính xác theo JSON bạn cung cấp
interface AIInference {
  x: number;
  y: number;
  width: number;
  height: number;
  confidence: number;
  class: string;
  class_id: number;
  detection_id: string;
}

interface AIResponse {
  camera_id: string;
  predictions: AIInference[];
}

// Cấu hình WebSocket tới Container AI
const AI_CONTAINER_WS_URL = `ws://${window.location.host}`;

// Shared WebSocket singleton — tất cả camera dùng chung 1 connection
type PredictionCallback = (predictions: AIInference[]) => void;
const wsListeners = new Map<string, PredictionCallback>();
let sharedWs: WebSocket | null = null;
let wsConnecting = false;

function connectSharedWS() {
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
connectSharedWS();
// ==========================================
// COMPONENT: Xử lý WebRTC & AI Bounding Box
// ==========================================
const WebRTCCamera = ({ streamUrl, camId }: { streamUrl: string; camId: string }) => {
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  const [streamError, setStreamError] = useState<string | null>(null);
  const [aiError, setAiError] = useState<boolean>(false);
  const [predictions, setPredictions] = useState<AIInference[]>([]);

  // 1. Kết nối WebRTC (Video Stream) - Chạy hoàn toàn độc lập
  useEffect(() => {
    if (!streamUrl) return;

    let isMounted = true;
    const peerConnection = new RTCPeerConnection({ iceServers: [] });

    peerConnection.ontrack = (event) => {
      console.log("🎥 Đã nhận track video!");
      if (videoRef.current) {
        videoRef.current.srcObject = event.streams[0];
      }
    };

    peerConnection.addTransceiver('video', { direction: 'recvonly' });

    const connectWHEP = async () => {
      try {
        const offer = await peerConnection.createOffer();
        await peerConnection.setLocalDescription(offer);

        await new Promise(resolve => setTimeout(resolve, 500)); // Đợi ICE

        const response = await fetch(`${streamUrl}/whep`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/sdp' },
          body: peerConnection.localDescription?.sdp
        });

        if (!response.ok) throw new Error('WHEP Failed');

        const answerSdp = await response.text();
        await peerConnection.setRemoteDescription({ type: 'answer', sdp: answerSdp });
        
        if (isMounted) setStreamError(null);
      } catch (err: any) {
        if (isMounted) setStreamError("LỖI KẾT NỐI CAMERA");
      }
    };

    connectWHEP();

    return () => {
      isMounted = false;
      peerConnection.close();
      if (videoRef.current) videoRef.current.srcObject = null;
    };
  }, [streamUrl]);

  // 2. Kết nối WebSocket (AI Data) — dùng shared connection
  useEffect(() => {
    const cb: PredictionCallback = (preds) => {
      setPredictions(preds);
      setAiError(false);
    };
    wsListeners.set(camId, cb);
    connectSharedWS();

    return () => {
      wsListeners.delete(camId);
    };
  }, [camId]);

  // 3. Vẽ Bounding Box — chỉ vẽ lại khi predictions thay đổi
  useEffect(() => {
    const video = videoRef.current;
    const canvas = canvasRef.current;
    if (!canvas || !video) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Cập nhật kích thước vật lý của canvas bằng với kích thước hiển thị của thẻ video
    canvas.width = video.clientWidth;
    canvas.height = video.clientHeight;
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    if (aiError || predictions.length === 0 || !video.videoWidth) return;

    // Tính toán tỷ lệ khung hình
    const videoRatio = video.videoWidth / video.videoHeight;
    const canvasRatio = canvas.width / canvas.height;

    let drawWidth, drawHeight;
    let offsetX = 0;
    let offsetY = 0;

    if (videoRatio > canvasRatio) {
      drawWidth = canvas.width;
      drawHeight = canvas.width / videoRatio;
      offsetY = (canvas.height - drawHeight) / 2;
    } else {
      drawHeight = canvas.height;
      drawWidth = canvas.height * videoRatio;
      offsetX = (canvas.width - drawWidth) / 2;
    }

    const inferenceWidth = 640;
    const inferenceHeight = 640;
    const scaleX = drawWidth / inferenceWidth;
    const scaleY = drawHeight / inferenceHeight;

    predictions.forEach(box => {
      const scaledX = (box.x * scaleX) + offsetX;
      const scaledY = (box.y * scaleY) + offsetY;
      const scaledW = box.width * scaleX;
      const scaledH = box.height * scaleY;

      const color = box.class_id === 1 ? '#00ff00' : '#ff3333';

      ctx.lineWidth = 2;
      ctx.strokeStyle = color;
      ctx.fillStyle = color + '22';
      ctx.beginPath();
      ctx.rect(scaledX, scaledY, scaledW, scaledH);
      ctx.stroke();
      ctx.fill();

      ctx.fillStyle = color;
      ctx.font = 'bold 11px monospace';
      const confPercent = (box.confidence * 100).toFixed(0);
      const labelText = `${box.class} ${confPercent}%`;
      const textWidth = ctx.measureText(labelText).width;

      ctx.fillRect(scaledX, scaledY - 18, textWidth + 8, 18);
      ctx.fillStyle = '#000000';
      ctx.fillText(labelText, scaledX + 4, scaledY - 5);
    });
  }, [predictions, aiError]);

  // Nếu bản thân Camera (MediaMTX) chết, hiện lỗi to ở giữa
  if (streamError) {
    return (
      <div className="w-full h-full bg-black flex items-center justify-center">
        <span className="text-red-500 text-xs font-mono animate-pulse">{streamError}</span>
      </div>
    );
  }

  // Render Video bình thường
  return (
    <>
      <video ref={videoRef} autoPlay playsInline muted className="absolute inset-0 w-full h-full object-contain bg-black z-0" />
      <canvas ref={canvasRef} className="absolute inset-0 w-full h-full object-contain pointer-events-none z-10" />
      
      {/* Cảnh báo AI Offline thu nhỏ ở góc trên bên phải */}
      {aiError && (
        <div className="absolute top-3 right-3 flex items-center gap-1.5 text-orange-400 bg-black/80 px-2.5 py-1 rounded border border-orange-500/30 z-20 shadow-lg pointer-events-none">
          <WifiOff className="w-3.5 h-3.5 animate-pulse" />
          <span className="text-[10px] font-mono font-bold">AI OFFLINE</span>
        </div>
      )}
    </>
  );
};

// ==========================================
// COMPONENT CHÍNH: App (Giữ nguyên)
// ==========================================
export default function App() {
  const [maximizedCam, setMaximizedCam] = useState<string | null>(null);
  const [currentTime, setCurrentTime] = useState(new Date());

  useEffect(() => {
    const timer = setInterval(() => setCurrentTime(new Date()), 1000);
    return () => clearInterval(timer);
  }, []);

  const handleMaximize = (camId: string) => {
    if (maximizedCam === camId) setMaximizedCam(null);
    else setMaximizedCam(camId);
  };

  return (
    <div className="min-h-screen bg-[#050505] text-white font-sans flex items-center justify-center p-0 md:p-4">
      <div className="w-full max-w-[1920px] h-screen md:h-auto md:aspect-video bg-[#0a0a0a] flex flex-col shadow-2xl shadow-black/50 border-0 md:border md:border-[#222] md:rounded-2xl overflow-hidden">
        <header className="h-16 md:h-20 flex items-center justify-between bg-[#111] px-4 md:px-6 border-b border-[#222] shrink-0">
          <div className="flex items-center gap-3">
            <div className="p-2 bg-orange-500/10 rounded-lg hidden sm:block">
              <Shield className="w-5 h-5 md:w-6 md:h-6 text-orange-500" />
            </div>
            <div>
              <h1 className="text-sm md:text-xl font-bold tracking-tight">AI SURVEILLANCE CORE</h1>
            </div>
          </div>
          <Settings className="w-5 h-5 text-gray-600 cursor-pointer hover:text-white transition-colors" />
        </header>

        <main className={`flex-1 grid gap-px md:gap-1 bg-[#1a1a1a] overflow-y-auto md:overflow-hidden ${maximizedCam ? 'grid-cols-1 grid-rows-1' : 'grid-cols-1 md:grid-cols-2 md:grid-rows-2'}`}>
          {CAMERAS.map((cam) => (
            <div 
              key={cam.id}
              className={`relative group bg-black flex flex-col min-h-[250px] md:min-h-0 ${maximizedCam && maximizedCam !== cam.id ? 'hidden' : ''}`}
              onDoubleClick={() => handleMaximize(cam.id)}
            >
              <div className="absolute top-0 left-0 right-0 p-3 bg-gradient-to-b from-black/90 to-transparent z-20 flex justify-between items-center pointer-events-none">
                <div className="flex items-center gap-2">
                  <div className="w-1.5 h-1.5 bg-red-600 rounded-full animate-pulse" />
                  <span className="text-[10px] md:text-xs font-mono font-bold text-white/80 drop-shadow-md">
                    {cam.id.toUpperCase()} // {cam.name}
                  </span>
                </div>
              </div>

              <div className="relative flex-1 flex items-center justify-center overflow-hidden">
                {cam.url ? (
                  <WebRTCCamera streamUrl={cam.url} camId={cam.id} />
                ) : (
                  <div className="w-full h-full bg-black flex items-center justify-center">
                    <span className="text-gray-500 text-xs font-mono">NO SIGNAL</span>
                  </div>
                )}
              </div>

              <div className="absolute bottom-0 left-0 right-0 p-2 flex justify-between items-center bg-gradient-to-t from-black/60 to-transparent z-20 pointer-events-none">
                <div className="text-[9px] font-mono text-white/50">{currentTime.toLocaleTimeString()}</div>
                <div className="flex gap-2">
                  <div className="px-1.5 py-0.5 bg-black/50 rounded text-[8px] font-mono text-green-400 border border-green-400/30">AI Active</div>
                  <div className="px-1.5 py-0.5 bg-black/50 rounded text-[8px] font-mono text-blue-400 border border-blue-400/30">WebRTC WHEP</div>
                </div>
              </div>
            </div>
          ))}
        </main>

        <footer className="h-8 bg-[#0a0a0a] border-t border-[#222] flex items-center justify-between px-4 shrink-0 hidden md:flex">
          <div className="flex gap-4">
            <span className="text-[9px] text-gray-600 font-mono">CPU: 12%</span>
            <span className="text-[9px] text-gray-600 font-mono">MEM: 1.2GB</span>
            <span className="text-[9px] text-gray-600 font-mono">NET: Real-time</span>
          </div>
          <div className="text-[9px] text-gray-600 font-mono uppercase tracking-widest">Secure AI Node v2.4.0-Stable</div>
        </footer>
      </div>
    </div>
  );
}