/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React, { useEffect, useState } from 'react';
import Header from './components/Header';
import Footer from './components/Footer';
import WebRTCCamera from './components/WebRTCCamera';
import { CAMERAS } from './constants/cameras';

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
    <div className="h-screen bg-[#050505] text-white font-sans flex items-center justify-center p-0 md:p-4 overflow-hidden">
      <div className="w-full max-w-[1920px] h-full bg-[#0a0a0a] flex flex-col shadow-2xl shadow-black/50 border-0 md:border md:border-[#222] md:rounded-2xl overflow-hidden">
        <Header />

        <main className={`flex-1 grid gap-px md:gap-1 bg-[#1a1a1a] overflow-y-auto md:overflow-hidden ${maximizedCam ? 'grid-cols-1 grid-rows-1' : 'grid-cols-1 md:grid-cols-2 md:grid-rows-2'}`}>
          {CAMERAS.map((cam) => (
            <div 
              key={cam.id}
              className={`relative group bg-black flex flex-col min-h-0 ${maximizedCam && maximizedCam !== cam.id ? 'hidden' : ''}`}
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

        <Footer />
      </div>
    </div>
  );
}