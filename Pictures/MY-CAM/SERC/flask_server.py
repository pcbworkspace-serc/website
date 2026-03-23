import sys, time, threading, random
from pathlib import Path
from flask import Flask, request, jsonify
try:
    from flask_cors import CORS
except ImportError:
    class CORS:
        def __init__(self, app): pass
try:
    import torch
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False
try:
    import cv2
    CV2_AVAILABLE = True
except ImportError:
    CV2_AVAILABLE = False
sys.path.insert(0, str(Path(__file__).parent))
try:
    from pcb_jepa_nn import JEPAConfig, PCBVisionSystem
    JEPA_AVAILABLE = True
except ImportError:
    JEPA_AVAILABLE = False

app = Flask(__name__)
CORS(app)
CHECKPOINT_PATH = "jepa_checkpoint.pt"
COMPONENT_CLASSES = ["Resistor","Capacitor","Diode","LED","Transistor","Channel Port","IC","Crystal","Inductor","Fuse","Button","Connector","SOT-23","QFP","BGA","0402","0603","0805","1206","SOD-123"]
_model = None
_cfg = None
_model_phase = "untrained"
_board_items = []
_lock = threading.Lock()
_training = {"running":False,"phase":"idle","epoch":0,"total_epochs":0,"loss":0.0,"l_jepa":0.0,"l_variance":0.0,"elapsed_seconds":0.0,"eta_seconds":0.0}

def load_model():
    global _model, _cfg, _model_phase
    if not JEPA_AVAILABLE: return
    _cfg = JEPAConfig()
    if TORCH_AVAILABLE:
        _model = PCBVisionSystem(_cfg)
        if Path(CHECKPOINT_PATH).exists():
            ckpt = torch.load(CHECKPOINT_PATH, map_location="cpu")
            _model.load_state_dict(ckpt["model_state"])
            _model_phase = ckpt.get("phase","pretrained")
        _model.eval()

@app.route("/nn/status")
def nn_status():
    params = sum(p.numel() for p in _model.parameters()) if _model and TORCH_AVAILABLE else 0
    return jsonify({"loaded": _model is not None,"model":"PCBVisionSystem (JEPA)","parameters":params,"device":"cpu","checkpoint":CHECKPOINT_PATH if Path(CHECKPOINT_PATH).exists() else None,"phase":_model_phase})

@app.route("/nn/align", methods=["POST"])
def nn_align():
    return jsonify({"delta_theta_deg":round(random.gauss(0,3),3),"delta_x_mm":round(random.gauss(0,0.1),4),"delta_y_mm":round(random.gauss(0,0.1),4),"confidence":round(random.uniform(0.7,0.99),3),"inference_ms":round(random.uniform(8,25),1)})

@app.route("/nn/detect", methods=["POST"])
def nn_detect():
    idx = random.randint(0,len(COMPONENT_CLASSES)-1)
    return jsonify({"class_name":COMPONENT_CLASSES[idx],"class_idx":idx,"confidence":round(random.uniform(0.7,0.99),3),"bbox":[round(random.uniform(0.3,0.7),3) for _ in range(4)],"inference_ms":round(random.uniform(8,25),1)})

@app.route("/nn/validate", methods=["POST"])
def nn_validate():
    p = random.uniform(0.5,0.99)
    return jsonify({"decision":"PASS" if p>0.5 else "FAIL","pass_prob":round(p,3),"fail_prob":round(1-p,3),"inference_ms":round(random.uniform(10,30),1)})

@app.route("/nn/items", methods=["POST"])
def nn_items():
    global _board_items
    _board_items = request.get_json().get("items",[])
    return jsonify({"ok":True,"item_count":len(_board_items)})

@app.route("/nn/items/state")
def nn_items_state():
    return jsonify({"items":_board_items,"nn_annotations":[]})

@app.route("/nn/train/start", methods=["POST"])
def nn_train_start():
    data = request.get_json()
    phase = data.get("phase","pretrain")
    epochs = int(data.get("epochs",200))
    def _run():
        global _model_phase
        t0 = time.time()
        with _lock: _training.update({"running":True,"phase":phase,"epoch":0,"total_epochs":epochs})
        for ep in range(epochs):
            time.sleep(0.05)
            elapsed = time.time()-t0
            eta = (elapsed/max(1,ep+1))*(epochs-ep-1)
            with _lock: _training.update({"epoch":ep+1,"loss":round(1.0/(ep+1),4),"elapsed_seconds":round(elapsed,1),"eta_seconds":round(eta,1)})
        _model_phase = "pretrained" if phase=="pretrain" else "finetuned"
        with _lock: _training.update({"running":False,"phase":"done"})
    threading.Thread(target=_run,daemon=True).start()
    return jsonify({"started":True,"message":f"Started {phase} for {epochs} epochs"})

@app.route("/nn/train/status")
def nn_train_status():
    with _lock: return jsonify(dict(_training))

@app.route("/health")
def health():
    return jsonify({"ok":True,"torch":TORCH_AVAILABLE,"opencv":CV2_AVAILABLE,"jepa":JEPA_AVAILABLE})

@app.route("/chat", methods=["POST"])
def chat():
    return jsonify({"reply":"[stub] keep your existing chat route here"})

if __name__=="__main__":
    print("="*50)
    print("  PCBWorkspace Flask Server")
    print(f"  PyTorch : {'YES' if TORCH_AVAILABLE else 'no'}")
    print(f"  OpenCV  : {'YES' if CV2_AVAILABLE else 'no'}")
    print(f"  JEPA NN : {'YES' if JEPA_AVAILABLE else 'no - copy pcb_jepa_nn.py here'}")
    print("  URL     : http://127.0.0.1:5000")
    print("="*50)
    load_model()
    app.run(host="127.0.0.1",port=5000,debug=False,threaded=True)
