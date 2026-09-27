# Third-party sources

## Camera board configuration and examples

`firmware/PCFaceTrack/board_config.h`, `camera_pins.h`, and `camera_setup.cpp`
derive from the Freenove ESP32-S3 WROOM Board camera examples used for this
project. Camera setup was adapted for RGB565 QVGA capture, the working sensor
orientation, neutral exposure, and software JPEG conversion in PCFaceTrack.

Source: https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board

The local source distribution includes a Creative Commons
Attribution-NonCommercial-ShareAlike 3.0 license. Its complete text is retained
in [licenses/Freenove-LICENSE.txt](licenses/Freenove-LICENSE.txt). Retain this
attribution and any source notices when redistributing the camera adaptations.

## Face detector weights

SCRFD is from InsightFace: https://github.com/deepinsight/insightface/tree/master/detection/scrfd

`download_model.py` downloads the official `buffalo_l.zip` release and extracts
`det_10g.onnx`. Weights and archives are not distributed in this repository.
InsightFace describes its pretrained models as available for non-commercial
research use; consult the upstream model terms for your intended use.

Release: https://github.com/deepinsight/insightface/releases/tag/v0.7

## Runtime dependencies

OpenCV, NumPy, Requests, ONNX Runtime, and NVIDIA CUDA/cuDNN libraries are
installed separately using `pc_tracker/requirements.txt`; they are not bundled.
Their respective licenses and notices apply. No blanket license is assigned
here to the project's original code; the upstream notices above are retained.
