# Giai đoạn 1: Build ứng dụng
FROM node:20-alpine AS build

WORKDIR /app

# Copy package files và cài đặt dependencies
COPY package*.json ./
RUN npm install

# Copy toàn bộ mã nguồn và build
COPY . .
RUN npm run build

# Giai đoạn 2: Serve bằng Nginx
FROM nginx:alpine

# Copy file build từ giai đoạn 1 vào thư mục của Nginx
COPY --from=build /app/dist /usr/share/nginx/html

# Cấu hình Nginx để hỗ trợ Single Page Application (SPA)
RUN echo 'server { \
    listen 80; \
    location / { \
        root /usr/share/nginx/html; \
        index index.html index.htm; \
        try_files $uri $uri/ /index.html; \
    } \
}' > /etc/nginx/conf.d/default.conf

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]