import contextlib
import subprocess
import time
from pathlib import Path

from tritonclient.http import InferenceServerClient

from ultralytics import YOLO

runtime = "docker"  # set to "podman" to use Podman

# 1) Exporting YOLO26 to ONNX Format

# Load a model
model = YOLO("yolo26n.pt")  # load an official model

# Retrieve metadata during export. Metadata needs to be added to config.pbtxt. See next section.
metadata = []


def export_cb(exporter):
    metadata.append(exporter.metadata)


model.add_callback("on_export_end", export_cb)

# Export the model
onnx_file = model.export(format="onnx", dynamic=True)


# 2) Setting Up Triton Model Repository

# Define paths
model_name = "yolo"
triton_repo_path = Path("tmp") / "triton_repo"
triton_model_path = triton_repo_path / model_name

# Create directories
(triton_model_path / "1").mkdir(parents=True, exist_ok=True)

# Move ONNX model to Triton Model path
Path(onnx_file).rename(triton_model_path / "1" / "model.onnx")

# Create config file
(triton_model_path / "config.pbtxt").touch()

data = """
# Add metadata
parameters {
  key: "metadata"
  value {
    string_value: "%s"
  }
}

# (Optional) Enable TensorRT for GPU inference
# First run will be slow due to TensorRT engine conversion
optimization {
  execution_accelerators {
    gpu_execution_accelerator {
      name: "tensorrt"
      parameters {
        key: "precision_mode"
        value: "FP16"
      }
      parameters {
        key: "max_workspace_size_bytes"
        value: "3221225472"
      }
      parameters {
        key: "trt_engine_cache_enable"
        value: "1"
      }
      parameters {
        key: "trt_engine_cache_path"
        value: "/models/yolo/1"
      }
    }
  }
}
""" % metadata[0]  # noqa

with open(triton_model_path / "config.pbtxt", "w") as f:
    f.write(data)

# 3) Running Triton Inference Server

# Define image https://catalog.ngc.nvidia.com/orgs/nvidia/containers/tritonserver
tag = "nvcr.io/nvidia/tritonserver:26.02-py3"  # 16.17 GB (Compressed Size)

subprocess.call(f"{runtime} pull {tag}", shell=True)

# GPU flags differ between Docker and Podman
gpu_flags = "--device nvidia.com/gpu=all" if runtime == "podman" else ""

container_name = "triton_server"

# Note: The :z flag on the volume mount is necessary for systems with SELinux (like Fedora/RHEL)
subprocess.call(
    f"{runtime} run -d --rm --name {container_name} {gpu_flags} -v {triton_repo_path.absolute()}:/models:z -p 8000:8000 {tag} tritonserver --model-repository=/models",
    shell=True,
)

# Wait for the Triton server to start
triton_client = InferenceServerClient(url="127.0.0.1:8000", verbose=False, ssl=False)

# Wait until model is ready
for _ in range(10):
    with contextlib.suppress(Exception):
        assert triton_client.is_model_ready(model_name)
        break
    time.sleep(1)