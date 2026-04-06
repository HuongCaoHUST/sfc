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

# Plugin gstyolodetection
gstyolodetection_sources = [
  'src/gstyolodetection.cpp',
  'src/yolo_engine.cpp'
  ]

gstyolodetectionexample = library('gstyolodetection',
  gstyolodetection_sources,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, opencv_dep, onnx_lib],
  install : true,
  install_dir : plugins_install_dir,
)

# Plugin yolojsonoverlay
gstyolojsonoverlay_sources = [
  'src/gstyolojsonoverlay.cpp'
]

gstyolojsonoverlay_plugin = library('gstyolojsonoverlay',
  gstyolojsonoverlay_sources,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, opencv_dep],
  install : true,
  install_dir : plugins_install_dir,
)

# Added by Gemini:
# Dependency for UDP communication in yolobackend
gio_dep = dependency('gio-2.0', required: true)

# Plugin yoloforward
gstyoloforward_sources = [
  'src/gstyoloforward.cpp',
  'src/yolo_engine.cpp'
  ]

gstyoloforward_plugin = library('gstyoloforward',
  gstyoloforward_sources,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, opencv_dep, onnx_lib],
  include_directories: [onnx_inc],
  install : true,
  install_dir : plugins_install_dir,
)

# Plugin yolobackend
gstyolobackend_sources = [
  'src/gstyolobackend.cpp',
  'src/yolo_engine.cpp'
  ]

gstyolobackend_plugin = library('gstyolobackend',
  gstyolobackend_sources,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, opencv_dep, onnx_lib, gio_dep],
  include_directories: [onnx_inc],
  install : true,
  install_dir : plugins_install_dir,
)

# Plugin seiidinserter
gstseiidinserter_sources = [
  'src/gstseiidinserter.cpp'
]

gstseiidinserter_plugin = library('gstseiidinserter',
  gstseiidinserter_sources,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)

# Plugin yolobatchdetection (GstAggregator-based multi-camera batch inference)
gstyolobatchdetection_sources = [
  'src/gstyolobatchdetection.cpp',
  'src/yolo_engine.cpp'
]

gstyolobatchdetection_plugin = library('gstyolobatchdetection',
  gstyolobatchdetection_sources,
  cpp_args: plugin_c_args,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, opencv_dep, onnx_lib],
  include_directories: [onnx_inc],
  install : true,
  install_dir : plugins_install_dir,
)