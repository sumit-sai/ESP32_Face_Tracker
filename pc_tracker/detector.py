"""SCRFD ONNX inference; face boxes only, no identity recognition."""
from pathlib import Path
import cv2
import numpy as np
import onnxruntime as ort

def nms(boxes, scores, threshold=0.4):
    order = scores.argsort()[::-1]
    keep = []
    areas = np.maximum(0, boxes[:, 2]-boxes[:, 0]) * np.maximum(0, boxes[:, 3]-boxes[:, 1])
    while order.size:
        i = int(order[0]); keep.append(i)
        rest = order[1:]
        intersection = np.maximum(0, np.minimum(boxes[i, 2:], boxes[rest, 2:]) -
                                  np.maximum(boxes[i, :2], boxes[rest, :2])).prod(axis=1)
        iou = intersection / np.maximum(areas[i]+areas[rest]-intersection, 1e-6)
        order = rest[iou <= threshold]
    return keep

class Detector:
    def __init__(self, model: Path, cpu=False, size=640, threshold=0.5):
        if not model.is_file():
            raise RuntimeError('Model missing. Run download_model.py first.')
        self.size, self.threshold = size, threshold
        if not cpu:
            ort.preload_dlls(directory='')
        providers = ['CPUExecutionProvider'] if cpu else [
            ('CUDAExecutionProvider', {'device_id': 0, 'gpu_mem_limit': 1024*1024*1024,
                                       'cudnn_conv_algo_search': 'HEURISTIC'}), 'CPUExecutionProvider']
        self.session = ort.InferenceSession(str(model), providers=providers)
        if not cpu and 'CUDAExecutionProvider' not in self.session.get_providers():
            raise RuntimeError('CUDA failed to load; refusing silent CPU fallback. Check NVIDIA DLL installation or pass --cpu.')
        self.session.disable_fallback()
        self.input = self.session.get_inputs()[0].name
        self.outputs = [o.name for o in self.session.get_outputs()]
        if len(self.outputs) not in (6, 9):
            raise RuntimeError(f'Unsupported SCRFD output layout: {len(self.outputs)} tensors')
        self.anchors = {}
        for stride in (8, 16, 32):
            height = width = size // stride
            yy, xx = np.mgrid[:height, :width]
            self.anchors[stride] = np.repeat(np.stack((xx, yy), axis=-1).reshape(-1, 2)*stride, 2, axis=0)

    def detect(self, image):
        height, width = image.shape[:2]
        scale = min(self.size/width, self.size/height)
        rw, rh = int(width*scale), int(height*scale)
        padded = np.zeros((self.size, self.size, 3), dtype=np.uint8)
        padded[:rh, :rw] = cv2.resize(image, (rw, rh))
        blob = cv2.dnn.blobFromImage(padded, 1/128.0, (self.size, self.size),
                                    (127.5, 127.5, 127.5), swapRB=True)
        outputs = self.session.run(self.outputs, {self.input: blob})
        boxes, scores = [], []
        for i, stride in enumerate((8, 16, 32)):
            score = outputs[i].reshape(-1)
            distance = outputs[i+3].reshape(-1, 4)*stride
            anchor = self.anchors[stride]
            if len(score) != len(anchor) or len(distance) != len(anchor):
                raise RuntimeError('SCRFD tensor dimensions do not match configured input size')
            selected = np.flatnonzero(score >= self.threshold)
            a, d = anchor[selected], distance[selected]
            box = np.column_stack((a[:, 0]-d[:, 0], a[:, 1]-d[:, 1], a[:, 0]+d[:, 2], a[:, 1]+d[:, 3])) / scale
            box[:, (0, 2)] = np.clip(box[:, (0, 2)], 0, width-1)
            box[:, (1, 3)] = np.clip(box[:, (1, 3)], 0, height-1)
            valid = (box[:, 2] > box[:, 0]) & (box[:, 3] > box[:, 1])
            boxes.append(box[valid]); scores.append(score[selected][valid])
        boxes, scores = np.concatenate(boxes), np.concatenate(scores)
        keep = nms(boxes, scores)
        return boxes[keep], scores[keep]

class TargetTracker:
    """Position association and smoothing; retains a missing target for 0.8 s."""
    def __init__(self):
        self.box = None
        self.center = None
        self.last_seen = 0.0

    def reset(self):
        self.__init__()

    def update(self, boxes, now, width, height):
        if not len(boxes):
            if now-self.last_seen > 0.8:
                self.reset()
            return None
        centers = (boxes[:, :2]+boxes[:, 2:])/2
        if self.box is not None and now-self.last_seen <= 0.8:
            last_center = (self.box[:2]+self.box[2:])/2
            distances = np.linalg.norm((centers-last_center)/np.array([width, height]), axis=1)
            index = int(distances.argmin())
            if distances[index] > 0.25:
                return None  # avoid jumping straight to an unrelated face
        else:
            index = int(((boxes[:, 2]-boxes[:, 0])*(boxes[:, 3]-boxes[:, 1])).argmax())
            self.center = None
        self.box = boxes[index].copy()
        raw = centers[index]
        self.center = raw if self.center is None else self.center*0.25 + raw*0.75
        self.last_seen = now
        return index, self.center.copy()
