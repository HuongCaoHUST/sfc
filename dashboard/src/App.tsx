/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React, { useEffect, useRef, useState } from 'react';
import { Camera, Activity, Shield, Settings, Maximize2, AlertCircle } from 'lucide-react';

interface BoundingBox {
  label: string;
  conf: number;
  x: number;
  y: number;
  w: number;
  h: number;
}

interface CameraData {
  camera_id: string;
  video_width: number;
  video_height: number;
  boxes: BoundingBox[];
}

const CAMERAS = [
  { id: 'cam1', name: 'Cổng chính (Main Gate)', url: 'https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8' },
  { id: 'cam2', name: 'Bãi đỗ xe (Parking Lot)', url: '' },
  { id: 'cam3', name: 'Hành lang tầng 1 (Hallway L1)', url: '' },
  { id: 'cam4', name: 'Kho hàng (Warehouse)', url: '' },
];

export default function App() {
  const [wsStatus, setWsStatus] = useState<'connected' | 'disconnected' | 'connecting'>('disconnected');
  const [isSimulating, setIsSimulating] = useState(true);
  const containerRefs = useRef<{ [key: string]: HTMLDivElement | null }>({});
  const videoRefs = useRef<{ [key: string]: HTMLVideoElement | null }>({});
  const canvasRefs = useRef<{ [key: string]: HTMLCanvasElement | null }>({});

  // WebSocket Logic
  useEffect(() => {
    if (isSimulating) {
      setWsStatus('connected');
      const interval = setInterval(() => {
        // Giả lập dữ liệu nhận diện cho 4 camera
        CAMERAS.forEach(cam => {
          const mockData: CameraData = {
            camera_id: cam.id,
            video_width: 1920,
            video_height: 1080,
            boxes: [
              {
                label: Math.random() > 0.5 ? 'person' : 'car',
                conf: 0.7 + Math.random() * 0.25,
                x: 200 + Math.random() * 1000,
                y: 200 + Math.random() * 500,
                w: 100 + Math.random() * 200,
                h: 200 + Math.random() * 300
              }
            ]
          };
          drawOverlay(mockData);
        });
      }, 500);
      return () => clearInterval(interval);
    }

    setWsStatus('connecting');
    const ws = new WebSocket('ws://localhost:8765');

    ws.onopen = () => setWsStatus('connected');
    ws.onclose = () => setWsStatus('disconnected');
    ws.onmessage = (event) => {
      try {
        const data: CameraData = JSON.parse(event.data);
        drawOverlay(data);
      } catch (e) {
        console.error('Error parsing WS data', e);
      }
    };

    return () => ws.close();
  }, [isSimulating]);

  // Đồng bộ kích thước Canvas với Video
  useEffect(() => {
    const observers: ResizeObserver[] = [];

    CAMERAS.forEach(cam => {
      const video = videoRefs.current[cam.id];
      const canvas = canvasRefs.current[cam.id];

      if (video && canvas) {
        const observer = new ResizeObserver(() => {
          canvas.width = video.clientWidth;
          canvas.height = video.clientHeight;
        });
        observer.observe(video);
        observers.push(observer);
      }
    });

    return () => observers.forEach(obs => obs.disconnect());
  }, []);

  const drawOverlay = (data: CameraData) => {
    const canvas = canvasRefs.current[data.camera_id];
    const video = videoRefs.current[data.camera_id];
    if (!canvas || !video) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Xóa frame cũ
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // Tính toán tỷ lệ scale
    // video_width/height là độ phân giải luồng AI gửi về
    // canvas.width/height là kích thước hiển thị thực tế trên trình duyệt
    const scaleX = canvas.width / data.video_width;
    const scaleY = canvas.height / data.video_height;

    data.boxes.forEach(box => {
      const x = box.x * scaleX;
      const y = box.y * scaleY;
      const w = box.w * scaleX;
      const h = box.h * scaleY;

      // Vẽ khung (Bounding Box)
      ctx.strokeStyle = box.label === 'person' ? '#00ff00' : '#ff3300';
      ctx.lineWidth = 2;
      ctx.strokeRect(x, y, w, h);

      // Vẽ nhãn (Label)
      const labelText = `${box.label} ${Math.round(box.conf * 100)}%`;
      ctx.font = '14px Inter, sans-serif';
      const textWidth = ctx.measureText(labelText).width;
      
      ctx.fillStyle = box.label === 'person' ? '#00ff00' : '#ff3300';
      ctx.fillRect(x, y - 20, textWidth + 10, 20);
      
      ctx.fillStyle = '#000000';
      ctx.fillText(labelText, x + 5, y - 5);
    });
  };

  return (
    <div className="min-h-screen bg-[#050505] text-white font-sans flex items-center justify-center p-0 md:p-4">
      {/* 
          DASHBOARD SHELL 
          - Trên desktop: Cố định tỷ lệ 16:9, tối đa 1920px.
          - Trên mobile: Tự do theo chiều dọc.
      */}
      <div className="w-full max-w-[1920px] h-screen md:h-auto md:aspect-video bg-[#0a0a0a] flex flex-col shadow-2xl shadow-black/50 border-0 md:border md:border-[#222] md:rounded-2xl overflow-hidden">
        
        {/* Header - Chiếm chiều cao cố định */}
        <header className="h-16 md:h-20 flex items-center justify-between bg-[#111] px-4 md:px-6 border-b border-[#222] shrink-0">
          <div className="flex items-center gap-3">
            <div className="p-2 bg-orange-500/10 rounded-lg hidden sm:block">
              <Shield className="w-5 h-5 md:w-6 md:h-6 text-orange-500" />
            </div>
            <div>
              <h1 className="text-sm md:text-xl font-bold tracking-tight">AI SURVEILLANCE CORE</h1>
              <div className="flex items-center gap-2">
                <div className={`w-1.5 h-1.5 rounded-full ${wsStatus === 'connected' ? 'bg-green-500 animate-pulse' : 'bg-red-500'}`} />
                <p className="text-[10px] text-gray-500 uppercase tracking-widest">System Live: {wsStatus.toUpperCase()}</p>
              </div>
            </div>
          </div>

          <div className="flex items-center gap-4 md:gap-8">
            <button 
              onClick={() => setIsSimulating(!isSimulating)}
              className={`px-3 py-1 md:px-5 md:py-2 rounded-full text-[10px] md:text-xs font-black transition-all ${
                isSimulating ? 'bg-orange-500 text-black' : 'bg-[#222] text-gray-400 border border-[#333]'
              }`}
            >
              {isSimulating ? 'SIMULATION' : 'LIVE'}
            </button>
            <Settings className="w-5 h-5 text-gray-600 cursor-pointer hover:text-white transition-colors" />
          </div>
        </header>

        {/* Camera Grid - Tự động lấp đầy không gian còn lại */}
        <main className="flex-1 grid grid-cols-1 md:grid-cols-2 md:grid-rows-2 gap-px md:gap-1 bg-[#1a1a1a] overflow-y-auto md:overflow-hidden">
          {CAMERAS.map((cam) => (
            <div 
              key={cam.id}
              className="relative group bg-black flex flex-col min-h-[250px] md:min-h-0"
            >
              {/* Camera Info Overlay */}
              <div className="absolute top-0 left-0 right-0 p-3 bg-gradient-to-b from-black/90 to-transparent z-20 flex justify-between items-center pointer-events-none">
                <div className="flex items-center gap-2">
                  <div className="w-1.5 h-1.5 bg-red-600 rounded-full animate-pulse" />
                  <span className="text-[10px] md:text-xs font-mono font-bold text-white/80 drop-shadow-md">
                    {cam.id.toUpperCase()} // {cam.name}
                  </span>
                </div>
                <Maximize2 className="w-4 h-4 text-white/40 pointer-events-auto cursor-pointer hover:text-white transition-colors" />
              </div>

              {/* Video & Canvas Layer */}
              <div className="relative flex-1 flex items-center justify-center overflow-hidden">
                <video
                  ref={el => videoRefs.current[cam.id] = el}
                  className="w-full h-full object-cover md:object-contain"
                  autoPlay
                  muted
                  loop
                  playsInline
                >
                  <source src="https://assets.mixkit.co/videos/preview/mixkit-security-camera-of-a-parking-lot-at-night-34440-large.mp4" type="video/mp4" />
                </video>
                
                <canvas
                  ref={el => canvasRefs.current[cam.id] = el}
                  className="absolute top-0 left-0 pointer-events-none z-10"
                />

                {/* Loading/Error State */}
                {!isSimulating && wsStatus !== 'connected' && (
                  <div className="absolute inset-0 flex flex-col items-center justify-center bg-black/80 backdrop-blur-sm z-30">
                    <Activity className="w-8 h-8 text-gray-700 animate-spin mb-3" />
                    <p className="text-gray-500 text-[10px] font-mono tracking-widest">CONNECTING TO STREAM...</p>
                  </div>
                )}
              </div>

              {/* Bottom Bar */}
              <div className="absolute bottom-0 left-0 right-0 p-2 flex justify-between items-center bg-gradient-to-t from-black/60 to-transparent pointer-events-none">
                <div className="text-[9px] font-mono text-white/50">
                  {new Date().toLocaleTimeString()}
                </div>
                <div className="flex gap-2">
                  <div className="px-1.5 py-0.5 bg-black/50 rounded text-[8px] font-mono text-green-500 border border-green-500/30">
                    H.264
                  </div>
                </div>
              </div>
            </div>
          ))}
        </main>

        {/* Footer - Thanh trạng thái nhỏ */}
        <footer className="h-8 bg-[#0a0a0a] border-t border-[#222] flex items-center justify-between px-4 shrink-0 hidden md:flex">
          <div className="flex gap-4">
            <span className="text-[9px] text-gray-600 font-mono">CPU: 12%</span>
            <span className="text-[9px] text-gray-600 font-mono">MEM: 1.2GB</span>
            <span className="text-[9px] text-gray-600 font-mono">NET: 4.5MB/s</span>
          </div>
          <div className="text-[9px] text-gray-600 font-mono uppercase tracking-widest">
            Secure AI Node v2.4.0-Stable
          </div>
        </footer>
      </div>
    </div>
  );
}
