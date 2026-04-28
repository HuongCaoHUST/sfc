import React, { useEffect, useState, useRef } from 'react';
import { WifiOff } from 'lucide-react';
import { AIInference } from '../types';
import { wsListeners, connectSharedWS, PredictionCallback } from '../services/aiWebSocket';

interface WebRTCCameraProps {
  streamUrl: string;
  camId: string;
  aiEnabled?: boolean;
}

const WebRTCCamera = ({ streamUrl, camId, aiEnabled = true }: WebRTCCameraProps) => {
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  const [streamError, setStreamError] = useState<string | null>(null);
  const [aiError, setAiError] = useState<boolean>(false);
  const [predictions, setPredictions] = useState<AIInference[]>([]);
  const [videoFps, setVideoFps] = useState<number>(0);

  const lastVideoFrameTimes = useRef<number[]>([]);

  // 1. Kết nối WebRTC (Video Stream)
  useEffect(() => {
    if (!streamUrl) return;

    let isMounted = true;
    const peerConnection = new RTCPeerConnection({ iceServers: [] });

    peerConnection.ontrack = (event) => {
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

  // 2. Kết nối WebSocket (AI Data)
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

  // 3. Tính toán Video FPS sử dụng requestVideoFrameCallback
  useEffect(() => {
    const video = videoRef.current;
    if (!video) return;

    let frameCount = 0;
    let lastTime = performance.now();
    let requestId: number;

    const updateFps = () => {
      const now = performance.now();
      frameCount++;

      if (now - lastTime >= 1000) {
        setVideoFps(frameCount);
        frameCount = 0;
        lastTime = now;
      }

      if ('requestVideoFrameCallback' in video) {
        requestId = (video as any).requestVideoFrameCallback(updateFps);
      } else {
        // Fallback cho trình duyệt cũ
        requestId = requestAnimationFrame(updateFps);
      }
    };

    if ('requestVideoFrameCallback' in video) {
      requestId = (video as any).requestVideoFrameCallback(updateFps);
    } else {
      requestId = requestAnimationFrame(updateFps);
    }

    return () => {
      if ('cancelVideoFrameCallback' in video) {
        (video as any).cancelVideoFrameCallback(requestId);
      } else {
        cancelAnimationFrame(requestId);
      }
    };
  }, []);

  // 3. Vẽ Bounding Box
  useEffect(() => {
    const video = videoRef.current;
    const canvas = canvasRef.current;
    if (!canvas || !video) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    canvas.width = video.clientWidth;
    canvas.height = video.clientHeight;
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    if (!aiEnabled || aiError || predictions.length === 0 || !video.videoWidth) return;

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

      const color = box.class_id === 0 ? '#3b82f6' : (box.class_id === 1 ? '#00ff00' : '#ff3333');

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
  }, [predictions, aiError, aiEnabled]);

  if (streamError) {
    return (
      <div className="w-full h-full bg-black flex items-center justify-center">
        <span className="text-red-500 text-xs font-mono animate-pulse">{streamError}</span>
      </div>
    );
  }

  return (
    <>
      <video ref={videoRef} autoPlay playsInline muted className="absolute inset-0 w-full h-full object-contain bg-black z-0" />
      <canvas ref={canvasRef} className="absolute inset-0 w-full h-full object-contain pointer-events-none z-10" />
      
      {aiError && (
        <div className="absolute top-3 right-3 flex items-center gap-1.5 text-blue-400 bg-black/80 px-2.5 py-1 rounded border border-blue-500/30 z-20 shadow-lg pointer-events-none">
          <WifiOff className="w-3.5 h-3.5 animate-pulse" />
          <span className="text-[10px] font-mono font-bold">AI OFFLINE</span>
        </div>
      )}

      {/* Hiển thị Video FPS ở góc trên bên trái của khung hình video */}
      {!streamError && (
        <div className="absolute top-12 left-3 bg-black/60 px-1.5 py-0.5 rounded border border-white/10 z-20 pointer-events-none">
          <span className="text-[9px] font-mono font-bold text-blue-400">VIDEO FPS: {videoFps}</span>
        </div>
      )}
    </>
  );
};

export default WebRTCCamera;
