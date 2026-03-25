mkdir -p libs
cp -a /usr/local/lib/libonnxruntime.so* ./libs/
ls -l libs/
docker build -t gst-myfilter-opencv .
docker run --rm gst-myfilter-opencv gst-inspect-1.0 myfilter