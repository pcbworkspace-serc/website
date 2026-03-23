import { useState, useEffect, useCallback, useRef } from "react";
import { getNNStatus, getAlignmentCorrection, detectComponent, validatePlacement, startPretraining, startFinetuning, getTrainingStatus, pingNNServer } from "@/lib/nn";
import type { NNStatus, AlignmentResult, DetectionResult, ValidationResult, TrainingStatus } from "@/lib/nn";

function StatusDot({ ok }: { ok: boolean }) {
  return <span className={["inline-block w-2 h-2 rounded-full mr-2", ok ? "bg-emerald-400" : "bg-red-500"].join(" ")} />;
}
function Label({ children }: { children: React.ReactNode }) {
  return <div className="text-[10px] uppercase tracking-widest text-cyan-300/60 mb-0.5">{children}</div>;
}
function Card({ children }: { children: React.ReactNode }) {
  return <div className="rounded-lg border border-cyan-300/10 bg-black/30 p-3">{children}</div>;
}
function NNButton({ onClick, disabled, children, variant = "default" }: { onClick: () => void; disabled?: boolean; children: React.ReactNode; variant?: "default"|"green"|"orange" }) {
  const colors = { default: "border-cyan-300/30 text-cyan-100", green: "border-emerald-400/30 text-emerald-300", orange: "border-orange-400/30 text-orange-300" };
  return (
    <button type="button" onClick={onClick} disabled={disabled}
      className={["w-full rounded-md px-3 py-1.5 text-xs font-semibold border bg-black/20 hover:bg-white/5 transition-colors disabled:opacity-40 disabled:cursor-not-allowed", colors[variant]].join(" ")}>
      {children}
    </button>
  );
}

export default function NNPanel({ open, onClose }: { open: boolean; onClose: () => void }) {
  const [serverOnline, setServerOnline] = useState(false);
  const [nnStatus, setNNStatus] = useState<NNStatus | null>(null);
  const [alignment, setAlignment] = useState<AlignmentResult | null>(null);
  const [detection, setDetection] = useState<DetectionResult | null>(null);
  const [validation, setValidation] = useState<ValidationResult | null>(null);
  const [training, setTraining] = useState<TrainingStatus | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  useEffect(() => {
    if (!open) return;
    pingNNServer().then(ok => {
      setServerOnline(ok);
      if (ok) getNNStatus().then(setNNStatus).catch(e => setError(String(e)));
    });
  }, [open]);

  useEffect(() => { return () => { if (pollRef.current) clearInterval(pollRef.current); }; }, []);

  const startPoll = useCallback(() => {
    if (pollRef.current) return;
    pollRef.current = setInterval(async () => {
      const s = await getTrainingStatus();
      setTraining(s);
      if (!s.running) { clearInterval(pollRef.current!); pollRef.current = null; getNNStatus().then(setNNStatus); }
    }, 2000);
  }, []);

  const runAlign = useCallback(async () => { setBusy("align"); setError(null); try { setAlignment(await getAlignmentCorrection()); } catch(e) { setError(String(e)); } finally { setBusy(null); } }, []);
  const runDetect = useCallback(async () => { setBusy("detect"); setError(null); try { setDetection(await detectComponent()); } catch(e) { setError(String(e)); } finally { setBusy(null); } }, []);
  const runValidate = useCallback(async () => { setBusy("validate"); setError(null); try { setValidation(await validatePlacement()); } catch(e) { setError(String(e)); } finally { setBusy(null); } }, []);
  const runPretrain = useCallback(async () => { setBusy("train"); setError(null); try { await startPretraining(200); startPoll(); } catch(e) { setError(String(e)); } finally { setBusy(null); } }, [startPoll]);
  const runFinetune = useCallback(async () => { setBusy("fine"); setError(null); try { await startFinetuning(50); startPoll(); } catch(e) { setError(String(e)); } finally { setBusy(null); } }, [startPoll]);

  if (!open) return null;

  return (
    <div className="absolute bottom-3 left-3 z-[9999] w-[290px] rounded-xl border border-cyan-300/25 bg-gradient-to-b from-[#071a2e]/95 to-[#04101f]/95 shadow-[0_12px_28px_rgba(0,0,0,0.6)] flex flex-col gap-3 p-4 text-white">
      <div className="flex items-center justify-between">
        <div className="font-bold text-sm text-cyan-100">JEPA <span className="text-cyan-400">Vision</span></div>
        <button type="button" onClick={onClose} className="text-white/40 hover:text-white/80 text-lg leading-none">×</button>
      </div>

      <Card>
        <div className="flex items-center text-xs"><StatusDot ok={serverOnline} /><span className={serverOnline ? "text-emerald-300" : "text-red-400"}>{serverOnline ? "Server online" : "Server offline (127.0.0.1:5000)"}</span></div>
        {nnStatus && <div className="mt-2 space-y-1">
          <div className="flex justify-between text-[11px]"><span className="text-white/50">Phase</span><span className="font-mono text-cyan-300">{nnStatus.phase}</span></div>
          <div className="flex justify-between text-[11px]"><span className="text-white/50">Params</span><span className="font-mono text-white/80">{nnStatus.parameters.toLocaleString()}</span></div>
          <div className="flex justify-between text-[11px]"><span className="text-white/50">Device</span><span className="font-mono text-white/80">{nnStatus.device}</span></div>
        </div>}
      </Card>

      <Card>
        <Label>Alignment Correction</Label>
        {alignment ? (
          <div className="grid grid-cols-3 gap-1 text-center mt-1.5">
            {[["Δθ", `${alignment.delta_theta_deg.toFixed(2)}°`, Math.abs(alignment.delta_theta_deg)>5], ["Δx", `${alignment.delta_x_mm.toFixed(3)}mm`, Math.abs(alignment.delta_x_mm)>0.3], ["Δy", `${alignment.delta_y_mm.toFixed(3)}mm`, Math.abs(alignment.delta_y_mm)>0.3]].map(([l,v,bad]) => (
              <div key={String(l)}><div className="text-[10px] text-cyan-300/60">{l}</div><div className={["font-mono text-sm", bad ? "text-orange-300":"text-emerald-300"].join(" ")}>{v}</div></div>
            ))}
          </div>
        ) : <div className="text-white/40 text-sm mt-1">—</div>}
        <div className="mt-2"><NNButton onClick={runAlign} disabled={!serverOnline||busy!==null} variant="green">{busy==="align"?"Running…":"▶ Run Alignment"}</NNButton></div>
      </Card>

      <Card>
        <Label>Component Detection</Label>
        {detection ? <div className="mt-1"><div className="font-mono text-sm text-cyan-200">{detection.class_name}</div><div className="text-[10px] text-white/50">{(detection.confidence*100).toFixed(1)}% conf · {detection.inference_ms.toFixed(0)}ms</div></div> : <div className="text-white/40 text-sm">—</div>}
        <div className="mt-2"><NNButton onClick={runDetect} disabled={!serverOnline||busy!==null}>{busy==="detect"?"Running…":"▶ Detect Component"}</NNButton></div>
      </Card>

      <Card>
        <Label>Placement Validation</Label>
        {validation ? (
          <div className="flex items-center gap-2 mt-1"><StatusDot ok={validation.decision==="PASS"} /><span className={["font-bold text-sm", validation.decision==="PASS"?"text-emerald-300":"text-red-400"].join(" ")}>{validation.decision}</span><span className="text-[10px] text-white/40 ml-auto">{(validation.pass_prob*100).toFixed(0)}% pass</span></div>
        ) : <div className="text-white/40 text-sm">—</div>}
        <div className="mt-2"><NNButton onClick={runValidate} disabled={!serverOnline||busy!==null}>{busy==="validate"?"Running…":"▶ Validate Placement"}</NNButton></div>
      </Card>

      {training?.running && (
        <Card>
          <Label>Training — {training.phase} {training.epoch}/{training.total_epochs}</Label>
          <div className="w-full bg-white/10 rounded-full h-2 mt-1.5 mb-1">
            <div className="h-2 rounded-full bg-gradient-to-r from-cyan-500 to-emerald-400 transition-all" style={{width:`${Math.round((training.epoch/Math.max(1,training.total_epochs))*100)}%`}} />
          </div>
          <div className="flex justify-between text-[10px] text-white/50 font-mono"><span>loss {training.loss.toFixed(4)}</span><span>ETA ~{Math.round(training.eta_seconds/60)}m</span></div>
        </Card>
      )}

      <Card>
        <Label>Training</Label>
        <div className="flex gap-2 mt-1.5">
          <NNButton onClick={runPretrain} disabled={!serverOnline||busy!==null||training?.running===true}>{busy==="train"?"Starting…":"Pretrain"}</NNButton>
          <NNButton onClick={runFinetune} disabled={!serverOnline||busy!==null||training?.running===true||nnStatus?.phase==="untrained"} variant="green">{busy==="fine"?"Starting…":"Finetune"}</NNButton>
        </div>
      </Card>

      {error && <div className="rounded-md border border-red-500/30 bg-red-900/20 px-3 py-2 text-xs text-red-300">{error}</div>}
    </div>
  );
}
