FROM node:20-alpine

WORKDIR /app
COPY package*.json ./
RUN npm install
RUN npm install express ws
COPY . .
RUN npm run build
EXPOSE 5002
CMD ["node", "server.js"]