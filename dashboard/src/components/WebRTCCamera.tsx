import React, { useEffect, useState, useRef } from 'react';
import { WifiOff } from 'lucide-react';
import { AIInference } from '../types';
import { wsListeners, connectSharedWS, PredictionCallback } from '../services/aiWebSocket';

interface WebRTCCameraProps {
  streamUrl: string;
  camId: string;
}

const WebRTCCamera = ({ streamUrl, camId }: WebRTCCameraProps) => {
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  const [streamError, setStreamError] = useState<string | null>(null);
  const [aiError, setAiError] = useState<boolean>(false);
  const [predictions, setPredictions] = useState<AIInference[]>([]);

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

    if (aiError || predictions.length === 0 || !video.videoWidth) return;

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
    </>
  );
};

export default WebRTCCamera;
