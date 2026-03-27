FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libgstreamer1.0-0 \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-tools \
    gstreamer1.0-rtsp \
    libgstrtspserver-1.0-dev \
    libopencv-dev \
    && rm -rf /var/lib/apt/lists/*

COPY builddir/gst-plugin/libgstmyfilter.so /usr/lib/x86_64-linux-gnu/gstreamer-1.0/
COPY builddir/gst-plugin/libgstyolodetection.so /usr/lib/x86_64-linux-gnu/gstreamer-1.0/
COPY builddir/gst-plugin/libgstyolojsonoverlay.so /usr/lib/x86_64-linux-gnu/gstreamer-1.0/
COPY libs/libonnxruntime.so.1.14.0 /usr/lib/
COPY libs/libonnxruntime.so /usr/lib/

ENV GST_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/gstreamer-1.0/

CMD ["gst-inspect-1.0"]