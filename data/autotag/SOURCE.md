# Bundled YOLOX-Nano subject detector

Upstream: https://github.com/Megvii-BaseDetection/YOLOX
Release: `0.1.1rc0`, official ONNX model, downloaded 2026-10-02.
Model URL: https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_nano.onnx
Licence: Apache-2.0; the release's unmodified `LICENSE` is beside this file.
Copyright: Megvii, Inc. and its affiliates.

Model size: 3,659,407 bytes.
SHA-256: `c789161ed43c8269fcd4e67c67eeeb4e80c622da2eb296a20bc6007bd18a0b7d`.
The embedded file is checked against this size and digest before inference.
No model modification or quantisation. No runtime download is required.

The official COCO model recognises 80 classes. OmaRAW exposes all 80, grouped
into People, Animals, Transport, Sports equipment, Food and Other objects.
The class order and translated labels are defined in src/autotag.cpp; the
existing People/Cats/Dogs keys are retained. Input is 416×416,
BGR float32 in 0–255, top-left letterboxing with 114 padding. The native ONNX
output uses strides 8/16/32; confidence is objectness × class score. Tags use
the largest valid in-image score per class, with a 0.55 threshold. Duplicate
boxes cannot change this maximum, so bounding-box NMS is unnecessary for
presence tagging. Scores are not calibrated probabilities.

The model and licence are embedded through `src/autotag.qrc`. It runs in the
private CPU worker using OpenCV DNN. No Python, PyTorch, CUDA or optional AI
model installation is needed. Test photographs are not included in this
model directory or distributed with the application.
