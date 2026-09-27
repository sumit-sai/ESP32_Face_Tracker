"""Fetch only the SCRFD detector from InsightFace's official model pack."""
from pathlib import Path
import hashlib
import json
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent
URL = 'https://github.com/deepinsight/insightface/releases/download/v0.7/buffalo_l.zip'

def main():
    folder = ROOT / 'models'
    folder.mkdir(exist_ok=True)
    target = folder / 'det_10g.onnx'
    archive = folder / 'buffalo_l.zip'
    if not target.exists():
        if not archive.exists():
            print('Downloading official InsightFace buffalo_l model pack...', flush=True)
            temporary = archive.with_suffix('.download')
            with urllib.request.urlopen(URL, timeout=60) as src, temporary.open('wb') as dst:
                while data := src.read(1024 * 1024):
                    dst.write(data)
            temporary.replace(archive)
        with zipfile.ZipFile(archive) as pack:
            matches = [name for name in pack.namelist() if Path(name).name == 'det_10g.onnx']
            if len(matches) != 1:
                raise RuntimeError('Expected one SCRFD det_10g.onnx in official archive')
            # Write this known file only; never extract arbitrary archive paths.
            target.write_bytes(pack.read(matches[0]))
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    (folder / 'source.json').write_text(json.dumps({'url': URL, 'file': target.name,
        'sha256': digest, 'license_note': 'InsightFace pretrained models: non-commercial research use'}, indent=2))
    print(f'Ready: {target}\nSHA256: {digest}')

if __name__ == '__main__':
    main()
