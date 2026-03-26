plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-template-plugin')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer template Plug-ins')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://gstreamer.freedesktop.org')
configure_file(output : 'config.h', configuration : cdata)
opencv_dep = dependency('opencv4', required : true)

gstaudio_dep = dependency('gstreamer-audio-1.0',
    fallback: ['gst-plugins-base', 'audio_dep'])

# Manually find ONNX runtime library and includes
onnx_lib = cpp.find_library('onnxruntime', dirs: ['/usr/local/lib'])
onnx_inc = include_directories('/usr/local/include')

# The myfilter Plugin
gstmyfilter_sources = [
  'src/gstmyfilter.cpp',
  'src/yolo_engine.cpp'
  ]

gstmyfilterexample = library('gstmyfilter',
  gstmyfilter_sources,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, opencv_dep, onnx_lib],
  include_directories: [onnx_inc],
  install : true,
  install_dir : plugins_install_dir,
)
