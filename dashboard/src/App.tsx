/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React, { useEffect, useRef, useState } from 'react';
import { Camera, Activity, Shield, Settings, Maximize2, AlertCircle } from 'lucide-react';

declare const Hls: any;

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
  { id: 'cam1', name: 'Cổng chính (Main Gate)', url: 'http://127.0.0.1:8888/cam1/index.m3u8' },
  { id: 'cam2', name: 'Bãi đỗ xe (Parking Lot)', url: '' },
  { id: 'cam3', name: 'Hành lang tầng 1 (Hallway L1)', url: '' },
  { id: 'cam4', name: 'Kho hàng (Warehouse)', url: '' },
];

export default function App() {
  const containerRefs = useRef<{ [key: string]: HTMLDivElement | null }>({});
  const videoRefs = useRef<{ [key: string]: HTMLVideoElement | null }>({});
  const [maximizedCam, setMaximizedCam] = useState<string | null>(null);
  const [currentTime, setCurrentTime] = useState(new Date());

  useEffect(() => {
    const timer = setInterval(() => {
      setCurrentTime(new Date());
    }, 1000);
    return () => clearInterval(timer);
  }, []);

  const handleMaximize = (camId: string) => {
    if (maximizedCam === camId) {
      setMaximizedCam(null);
    } else {
      setMaximizedCam(camId);
    }
  };

  useEffect(() => {
    const hlsInstances: Hls[] = [];

    CAMERAS.forEach(cam => {
      const video = videoRefs.current[cam.id];
      if (video && cam.url) {
        if (Hls.isSupported()) {
          const hls = new Hls();
          hls.loadSource(cam.url);
          hls.attachMedia(video);
          hlsInstances.push(hls);
        } else if (video.canPlayType('application/vnd.apple.mpegurl')) {
          video.src = cam.url;
        }
      }
    });

    return () => {
      hlsInstances.forEach(hls => {
        hls.destroy();
      });
    };
  }, []);

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
            </div>
          </div>

          <div className="flex items-center gap-4 md:gap-8">
            <Settings className="w-5 h-5 text-gray-600 cursor-pointer hover:text-white transition-colors" />
          </div>
        </header>

        {/* Camera Grid - Tự động lấp đầy không gian còn lại */}
        <main className={`flex-1 grid gap-px md:gap-1 bg-[#1a1a1a] overflow-y-auto md:overflow-hidden ${maximizedCam ? 'grid-cols-1 grid-rows-1' : 'grid-cols-1 md:grid-cols-2 md:grid-rows-2'}`}>
          {CAMERAS.map((cam) => (
            <div 
              key={cam.id}
              className={`relative group bg-black flex flex-col min-h-[250px] md:min-h-0 ${maximizedCam && maximizedCam !== cam.id ? 'hidden' : ''}`}
            >
              {/* Camera Info Overlay */}
              <div className="absolute top-0 left-0 right-0 p-3 bg-gradient-to-b from-black/90 to-transparent z-20 flex justify-between items-center pointer-events-none">
                <div className="flex items-center gap-2">
                  <div className="w-1.5 h-1.5 bg-red-600 rounded-full animate-pulse" />
                  <span className="text-[10px] md:text-xs font-mono font-bold text-white/80 drop-shadow-md">
                    {cam.id.toUpperCase()} // {cam.name}
                  </span>
                </div>
                <Maximize2 
                  className="w-4 h-4 text-white/40 pointer-events-auto cursor-pointer hover:text-white transition-colors" 
                  onClick={() => handleMaximize(cam.id)}
                />
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
                </video>
              </div>

              {/* Bottom Bar */}
              <div className="absolute bottom-0 left-0 right-0 p-2 flex justify-between items-center bg-gradient-to-t from-black/60 to-transparent pointer-events-none">
                <div className="text-[9px] font-mono text-white/50">
                  {currentTime.toLocaleTimeString()}
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
