FROM node:20-alpine

WORKDIR /app

# Copy package files và cài đặt dependencies
COPY package*.json ./
RUN npm install

# Cài thêm 2 thư viện siêu nhẹ để chạy server.js
RUN npm install express ws

# Copy toàn bộ mã nguồn và build React
COPY . .
RUN npm run build

# Mở port 5002 ra ngoài container
EXPOSE 5002

# Khởi chạy Bridge Server
CMD ["node", "server.js"]