"""Local SCRFD camera tracker. Run with the project's isolated Python environment."""
import argparse
from dataclasses import dataclass
from pathlib import Path
import threading
import time
import cv2
import numpy as np
import requests
from detector import Detector, TargetTracker

ROOT = Path(__file__).resolve().parent

def session():
    client = requests.Session()
    client.trust_env = False  # local robot must not be routed through HTTP proxies
    return client

@dataclass
class Frame:
    image: np.ndarray
    frame_id: int
    boot_id: int
    started: float

class LatestCamera:
    """One replaceable slot, never a growing queue of delayed frames."""
    def __init__(self, host):
        self.url = f'http://{host}:81/frame'
        self.lock = threading.Lock()
        self.latest = None
        self.error = ''
        self.stop = threading.Event()
        self.worker = threading.Thread(target=self.run, daemon=True)

    def run(self):
        with session() as client:
            while not self.stop.is_set():
                try:
                    started = time.monotonic()
                    with client.get(self.url, timeout=(2, 2)) as response:
                        response.raise_for_status()
                        image = cv2.imdecode(np.frombuffer(response.content, np.uint8), cv2.IMREAD_COLOR)
                        if image is None:
                            raise ValueError('Invalid JPEG')
                        frame = Frame(image, int(response.headers['X-Frame-Id']),
                                      int(response.headers['X-Boot-Id']), started)
                    with self.lock:
                        self.latest, self.error = frame, ''
                except (requests.RequestException, ValueError, KeyError) as error:
                    with self.lock:
                        self.latest, self.error = None, str(error)
                    self.stop.wait(0.3)

    def get(self):
        with self.lock:
            return self.latest, self.error

def observation(frame, center):
    payload = {'frame_id': frame.frame_id, 'boot_id': frame.boot_id, 'found': int(center is not None)}
    if center is not None:
        height, width = frame.image.shape[:2]
        payload['dx'] = f'{np.clip((center[0]-(width-1)/2)/((width-1)/2), -1, 1):.6f}'
        payload['dy'] = f'{np.clip((center[1]-(height-1)/2)/((height-1)/2), -1, 1):.6f}'
    return payload

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.4.1')
    parser.add_argument('--model', type=Path, default=ROOT/'models'/'det_10g.onnx')
    parser.add_argument('--cpu', action='store_true', help='Explicitly allow CPU inference')
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--dry-run', action='store_true', help='Show detections with servos paused')
    parser.add_argument('--no-search', action='store_true')
    parser.add_argument('--self-test', action='store_true', help='Check model/GPU on a blank image; no robot connection')
    args = parser.parse_args()
    detector = Detector(args.model, cpu=args.cpu)
    blank = np.zeros((480, 640, 3), np.uint8)
    for _ in range(3):
        detector.detect(blank)
    started = time.perf_counter()
    for _ in range(10):
        detector.detect(blank)
    print('Providers:', detector.session.get_providers(), flush=True)
    print(f'Blank-frame inference average: {(time.perf_counter()-started)*100:.1f} ms', flush=True)
    if args.self_test:
        return

    base = f'http://{args.host}'
    client = session()
    def post(route, params):
        response = client.post(base+route, params=params, timeout=(1, 1))
        response.raise_for_status()
        return response
    response = client.get(base+'/status', timeout=(2, 2))
    response.raise_for_status()
    status = response.json()
    if status.get('firmware') != 'PCFaceTrack' or status.get('protocol') != 1:
        raise RuntimeError('Upload the NEW PCFaceTrack sketch first. Existing FaceTrack is not this protocol.')

    camera = LatestCamera(args.host)
    tracker = TargetTracker()
    paused, searching = args.dry_run, not args.no_search
    boot, last_key, last_log = status['boot_id'], None, 0.0
    display = blank.copy()
    message = 'Waiting for camera'
    try:
        post('/search', {'enabled': int(searching)})
        post('/tracking', {'enabled': int(not paused)})
        camera.worker.start()
        print('Tracking started. Q: quit | Space: pause/resume | S: search on/off | R: choose target again', flush=True)
        while True:
            frame, error = camera.get()
            if frame is not None and (frame.boot_id, frame.frame_id) != last_key:
                last_key = (frame.boot_id, frame.frame_id)
                now = time.monotonic()
                if now-frame.started > 0.8:
                    message = 'Old frame discarded; board watchdog holds position'
                    tracker.reset()
                else:
                    started = time.perf_counter()
                    boxes, scores = detector.detect(frame.image)
                    inference_ms = (time.perf_counter()-started)*1000
                    height, width = frame.image.shape[:2]
                    target = tracker.update(boxes, time.monotonic(), width, height)
                    center = None if target is None else target[1]
                    age = time.monotonic()-frame.started
                    message = 'PAUSED' if paused else ('TRACKING' if target else ('SEARCH enabled' if searching else 'No face; holding'))
                    try:
                        if boot != frame.boot_id:
                            tracker.reset()
                            post('/search', {'enabled': int(searching)})
                            post('/tracking', {'enabled': int(not paused)})
                            boot = frame.boot_id
                            center = None
                        if not paused and age <= 0.8:
                            post('/observation', observation(frame, center))
                        elif age > 0.8:
                            message = 'Frame too old; no movement command sent'
                    except requests.RequestException as exc:
                        message = f'Command rejected/unavailable: {exc}'
                    display = frame.image.copy()
                    cv2.drawMarker(display, ((width-1)//2, (height-1)//2), (255, 255, 255), cv2.MARKER_CROSS, 20, 1)
                    for index, (box, score) in enumerate(zip(boxes, scores)):
                        x1, y1, x2, y2 = box.astype(int)
                        color = (0, 255, 0) if target is not None and index == target[0] else (160, 160, 160)
                        cv2.rectangle(display, (x1, y1), (x2, y2), color, 2)
                        cv2.putText(display, f'{score:.2f}', (x1, max(15, y1-5)), cv2.FONT_HERSHEY_SIMPLEX, .45, color, 1)
                    if center is not None:
                        cv2.circle(display, tuple(center.astype(int)), 4, (0, 255, 255), -1)
                    cv2.putText(display, f'Inference {inference_ms:.0f} ms | image age {age*1000:.0f} ms',
                                (8, height-15), cv2.FONT_HERSHEY_SIMPLEX, .5, (255, 255, 255), 1)
            if error or (frame is not None and time.monotonic()-frame.started > 1):
                message = 'Camera disconnected/stale; board holds position'
                display = blank.copy()
                tracker.reset()
            now = time.monotonic()
            if now-last_log >= 2:
                print(message, flush=True); last_log = now
            if not args.headless:
                shown = display.copy()
                cv2.putText(shown, message[:85], (8, 22), cv2.FONT_HERSHEY_SIMPLEX, .45, (0, 220, 255), 1)
                cv2.imshow('PC Face Tracker | Space pause | S search | R reset | Q quit', shown)
                key = cv2.waitKey(1) & 0xff
                if key in (ord('q'), 27): break
                try:
                    if key == ord(' ') and not args.dry_run:
                        paused = not paused
                        post('/tracking', {'enabled': int(not paused)})
                        tracker.reset()
                    elif key == ord('s'):
                        searching = not searching
                        post('/search', {'enabled': int(searching)})
                    elif key == ord('r'): tracker.reset()
                except requests.RequestException as exc:
                    # A failed pause must not silently leave motors active: stop sending observations.
                    paused = True
                    message = f'Control failed; commands stopped: {exc}'
            time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    finally:
        camera.stop.set()
        try: post('/tracking', {'enabled': 0})
        except requests.RequestException: pass  # firmware's frame-age watchdog still stops motion
        if camera.worker.is_alive(): camera.worker.join(timeout=3)
        client.close()
        cv2.destroyAllWindows()

if __name__ == '__main__':
    main()
