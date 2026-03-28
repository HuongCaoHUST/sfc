import express from 'express';
import { WebSocketServer } from 'ws';
import dgram from 'dgram'; // 👉 Thư viện UDP có sẵn của Node.js
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
app.use(express.static(path.join(__dirname, 'dist')));

// ==========================================
// 1. CẤU HÌNH WEBSOCKET CHO TRÌNH DUYỆT (REACT)
// ==========================================
// Dùng HTTP Server ở port 3000 để phục vụ React và WebSocket
// (Chúng ta tách port 3000 cho web, để dành riêng port 5002 cho UDP của AI)
const HTTP_PORT = 3000; 
const server = app.listen(HTTP_PORT, () => {
  console.log(`✅ [HTTP] Dashboard Server đang chạy tại port ${HTTP_PORT}`);
});

const wss = new WebSocketServer({ server });
let reactClients = [];

wss.on('connection', (ws) => {
  console.log('🟢 [WebSocket] Trình duyệt (React) đã kết nối!');
  reactClients.push(ws);
  ws.on('close', () => { 
    reactClients = reactClients.filter(c => c !== ws); 
  });
});

// Cấu hình định tuyến cho React SPA
app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, 'dist', 'index.html'));
});

// ==========================================
// 2. CẤU HÌNH TRẠM THU SÓNG UDP TỪ AI
// ==========================================
const UDP_PORT = 5002;
const udpServer = dgram.createSocket('udp4');

udpServer.on('error', (err) => {
  console.error(`❌ [UDP] Lỗi Server:\n${err.stack}`);
  udpServer.close();
});

udpServer.on('listening', () => {
  const address = udpServer.address();
  console.log(`✅ [UDP] Đang lắng nghe dữ liệu AI tại port ${address.port}...`);
});

// Khi có gói tin UDP bay tới
udpServer.on('message', (msg, rinfo) => {
  try {
    // Chuyển đổi gói tin nhị phân thành chuỗi văn bản
    const jsonString = msg.toString('utf-8');
    
    // (Tùy chọn) In ra log 1 phần để kiểm tra xem nó có sống không
    // console.log(`📦 [UDP] Đã nhận ${jsonString.length} bytes từ AI`);

    // Bắn thẳng chuỗi JSON này lên cho tất cả các tab React đang mở
    reactClients.forEach(client => {
      if (client.readyState === 1) {
        client.send(jsonString);
      }
    });
  } catch (error) {
    console.error("❌ [UDP] Lỗi xử lý tin nhắn UDP:", error);
  }
});

// Mở cổng bắt đầu lắng nghe
udpServer.bind(UDP_PORT);