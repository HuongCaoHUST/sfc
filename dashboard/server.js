import express from 'express';
import { WebSocketServer } from 'ws';
import dgram from 'dgram';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
app.use(express.static(path.join(__dirname, 'dist')));

const HTTP_PORT = 3000;
const server = app.listen(HTTP_PORT, () => {
  console.log(`✅ [HTTP] Dashboard tại port ${HTTP_PORT}`);
});

const wss = new WebSocketServer({ server });
let reactClients = [];

wss.on('connection', (ws) => {
  reactClients.push(ws);
  ws.on('close', () => { reactClients = reactClients.filter(c => c !== ws); });
});

const START_PORT = 5101;
const NUM_CAMS = 8;

for (let i = 0; i < NUM_CAMS; i++) {
  const port = START_PORT + i;
  const camId = `cam${i + 1}`;
  
  const udpSocket = dgram.createSocket('udp4');

  udpSocket.on('message', (msg) => {
    try {
      const rawData = JSON.parse(msg.toString('utf-8'));
    
      const packet = {
        camera_id: camId,
        predictions: rawData.predictions || []
      };

      const jsonString = JSON.stringify(packet);
      reactClients.forEach(client => {
        if (client.readyState === 1) client.send(jsonString);
      });
    } catch (err) {
      console.error(`❌ Lỗi format JSON tại port ${port}`);
    }
  });

  udpSocket.on('listening', () => {
    console.log(`🚀 [UDP] ${camId.toUpperCase()} đang đợi tại port ${port}`);
  });

  udpSocket.bind(port);
}

app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, 'dist', 'index.html'));
});