const FLASK = "http://127.0.0.1:5000";
export interface NNStatus { loaded: boolean; model: string; parameters: number; device: string; checkpoint: string | null; phase: "untrained"|"pretrained"|"finetuned"; }
export interface AlignmentResult { delta_theta_deg: number; delta_x_mm: number; delta_y_mm: number; confidence: number; inference_ms: number; }
export interface DetectionResult { class_name: string; class_idx: number; confidence: number; bbox: [number,number,number,number]; inference_ms: number; }
export interface ValidationResult { decision: "PASS"|"FAIL"|"UNCERTAIN"; pass_prob: number; fail_prob: number; inference_ms: number; }
export interface TrainingStatus { running: boolean; phase: string; epoch: number; total_epochs: number; loss: number; l_jepa: number; l_variance: number; elapsed_seconds: number; eta_seconds: number; }
async function post<T>(path: string, body: unknown): Promise<T> { const res = await fetch(`${FLASK}${path}`, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }); if (!res.ok) throw new Error(`NN error ${res.status}`); return res.json() as Promise<T>; }
async function get<T>(path: string): Promise<T> { const res = await fetch(`${FLASK}${path}`); if (!res.ok) throw new Error(`NN error ${res.status}`); return res.json() as Promise<T>; }
export async function getNNStatus(): Promise<NNStatus> { return get("/nn/status"); }
export async function getAlignmentCorrection(): Promise<AlignmentResult> { return post("/nn/align", {}); }
export async function detectComponent(): Promise<DetectionResult> { return post("/nn/detect", {}); }
export async function validatePlacement(): Promise<ValidationResult> { return post("/nn/validate", {}); }
export async function syncBoardItems(items: Array<{type:string;x:number;y:number}>): Promise<{ok:boolean}> { return post("/nn/items", { items }); }
export async function startPretraining(epochs=200): Promise<{started:boolean;message:string}> { return post("/nn/train/start", { phase:"pretrain", epochs }); }
export async function startFinetuning(epochs=50): Promise<{started:boolean;message:string}> { return post("/nn/train/start", { phase:"finetune", epochs }); }
export async function getTrainingStatus(): Promise<TrainingStatus> { return get("/nn/train/status"); }
export async function pingNNServer(): Promise<boolean> { try { await get("/nn/status"); return true; } catch { return false; } }
