import { Canvas } from "@react-three/fiber";
import { OrbitControls, Grid, Text } from "@react-three/drei";
import * as THREE from "three";
import { handleChatInput } from "@/lib/robot";
import { useState, useCallback, useEffect, useMemo, useRef } from "react";
import miniMeeBg from "@/assets/minimee.png";
import NNPanel from "@/components/NNPanel";
import { syncBoardItems } from "@/lib/nn";

interface DroppedItem { type: string; x: number; y: number; }
type PCBWorkspaceProps = { items?: DroppedItem[]; onItemsChange?: (items: DroppedItem[]) => void; };
type ChatMessage = { role: "user"|"assistant"; content: string; };

function Resistor({ position }: { position: [number,number,number] }) {
  return (
    <group position={position}>
      <mesh position={[0,0.12,0]} rotation={[0,0,Math.PI/2]}><cylinderGeometry args={[0.08,0.08,0.35,16]} /><meshStandardMaterial color="#d2b48c" roughness={0.6} /></mesh>
      {[{offset:-0.12,color:"#8B4513"},{offset:-0.05,color:"#000000"},{offset:0.02,color:"#ff0000"},{offset:0.09,color:"#FFD700"}].map((band,i) => (
        <mesh key={i} position={[band.offset,0.12,0]} rotation={[0,0,Math.PI/2]}><cylinderGeometry args={[0.085,0.085,0.02,16]} /><meshStandardMaterial color={band.color} /></mesh>
      ))}
      <mesh position={[-0.25,0.12,0]} rotation={[0,0,Math.PI/2]}><cylinderGeometry args={[0.012,0.012,0.18,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.9} roughness={0.2} /></mesh>
      <mesh position={[0.25,0.12,0]} rotation={[0,0,Math.PI/2]}><cylinderGeometry args={[0.012,0.012,0.18,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.9} roughness={0.2} /></mesh>
    </group>
  );
}
function Capacitor({ position }: { position: [number,number,number] }) {
  return (
    <group position={position}>
      <mesh position={[0,0.18,0]}><cylinderGeometry args={[0.12,0.12,0.3,20]} /><meshStandardMaterial color="#1a1a6e" roughness={0.4} /></mesh>
      <mesh position={[0,0.34,0]}><cylinderGeometry args={[0.12,0.11,0.02,20]} /><meshStandardMaterial color="#888888" metalness={0.8} roughness={0.2} /></mesh>
      <mesh position={[-0.04,0.01,0]}><cylinderGeometry args={[0.012,0.012,0.08,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.9} roughness={0.2} /></mesh>
      <mesh position={[0.04,0.01,0]}><cylinderGeometry args={[0.012,0.012,0.08,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.9} roughness={0.2} /></mesh>
    </group>
  );
}
function Diode({ position }: { position: [number,number,number] }) {
  return (
    <group position={position}>
      <mesh position={[0,0.1,0]} rotation={[0,0,Math.PI/2]}><cylinderGeometry args={[0.05,0.05,0.25,12]} /><meshStandardMaterial color="#2a2a2a" roughness={0.3} /></mesh>
      <mesh position={[0.08,0.1,0]} rotation={[0,0,Math.PI/2]}><cylinderGeometry args={[0.055,0.055,0.03,12]} /><meshStandardMaterial color="#C0C0C0" metalness={0.7} roughness={0.3} /></mesh>
      <mesh position={[-0.2,0.1,0]} rotation={[0,0,Math.PI/2]}><cylinderGeometry args={[0.012,0.012,0.18,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.9} roughness={0.2} /></mesh>
      <mesh position={[0.2,0.1,0]} rotation={[0,0,Math.PI/2]}><cylinderGeometry args={[0.012,0.012,0.18,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.9} roughness={0.2} /></mesh>
    </group>
  );
}
function LED({ position }: { position: [number,number,number] }) {
  return (
    <group position={position}>
      <mesh position={[0,0.22,0]}><sphereGeometry args={[0.1,16,16,0,Math.PI*2,0,Math.PI/2]} /><meshStandardMaterial color="#ff2200" transparent opacity={0.7} emissive="#ff2200" emissiveIntensity={0.5} /></mesh>
      <mesh position={[0,0.13,0]}><cylinderGeometry args={[0.1,0.1,0.18,16]} /><meshStandardMaterial color="#ff3300" transparent opacity={0.6} emissive="#ff2200" emissiveIntensity={0.3} /></mesh>
      <mesh position={[0,0.04,0]}><cylinderGeometry args={[0.12,0.12,0.02,16]} /><meshStandardMaterial color="#cccccc" metalness={0.6} roughness={0.3} /></mesh>
      <mesh position={[-0.03,0.01,0]}><cylinderGeometry args={[0.01,0.01,0.08,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.9} roughness={0.2} /></mesh>
      <mesh position={[0.03,0.01,0]}><cylinderGeometry args={[0.01,0.01,0.06,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.9} roughness={0.2} /></mesh>
      <pointLight position={[0,0.3,0]} color="#ff2200" intensity={0.3} distance={1} />
    </group>
  );
}
function Transistor({ position }: { position: [number,number,number] }) {
  return (
    <group position={position}>
      <mesh position={[0,0.12,0]}><cylinderGeometry args={[0.1,0.1,0.18,16,1,false,0,Math.PI]} /><meshStandardMaterial color="#1a1a1a" roughness={0.3} /></mesh>
      <mesh position={[0,0.12,0]}><boxGeometry args={[0.2,0.18,0.02]} /><meshStandardMaterial color="#1a1a1a" roughness={0.3} /></mesh>
      {[-0.05,0,0.05].map((x,i) => <mesh key={i} position={[x,0.01,0]}><cylinderGeometry args={[0.01,0.01,0.06,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.9} roughness={0.2} /></mesh>)}
    </group>
  );
}
function ChannelPort({ position, channelNumber }: { position: [number,number,number]; channelNumber: number }) {
  const edgesGeo = useMemo(() => new THREE.EdgesGeometry(new THREE.BoxGeometry(0.38,0.22,0.25)), []);
  return (
    <group position={position}>
      <mesh position={[0,0.1,0]}><boxGeometry args={[0.35,0.2,0.22]} /><meshStandardMaterial color="#222222" roughness={0.4} metalness={0.2} /></mesh>
      {[-0.08,0,0.08].map((x,i) => <mesh key={i} position={[x,0,0]}><cylinderGeometry args={[0.012,0.012,0.1,8]} /><meshStandardMaterial color="#C0C0C0" metalness={0.95} roughness={0.1} /></mesh>)}
      <lineSegments position={[0,0.1,0]}><primitive object={edgesGeo} attach="geometry" /><lineBasicMaterial color="#ffffff" /></lineSegments>
      <Text position={[0,0.28,0]} rotation={[-Math.PI/2,0,0]} fontSize={0.13} color="#ffffff" anchorX="center" anchorY="middle">{`CH${channelNumber}`}</Text>
    </group>
  );
}
function PCBBoard() {
  const traces: {x:number;z:number;w:number;h:number}[] = [];
  for (let i=0;i<7;i++) traces.push({x:0,z:-1.4+i*0.48,w:5.6,h:0.04});
  for (let i=0;i<11;i++) traces.push({x:-2.5+i*0.5,z:0,w:0.04,h:3.6});
  const vias: [number,number][] = [];
  for (let r=0;r<7;r++) for (let c=0;c<11;c++) vias.push([-2.5+c*0.5,-1.4+r*0.48]);
  const boardEdgesGeo = useMemo(() => new THREE.EdgesGeometry(new THREE.BoxGeometry(6.2,0.001,4.2)), []);
  return (
    <group>
      <mesh position={[0,-0.08,0]} receiveShadow><boxGeometry args={[6.2,0.06,4.2]} /><meshStandardMaterial color="#0d4f25" roughness={0.7} /></mesh>
      <mesh position={[0,-0.04,0]}><boxGeometry args={[6.2,0.02,4.2]} /><meshStandardMaterial color="#1a8a4a" roughness={0.5} /></mesh>
      {traces.map((t,i) => <mesh key={i} position={[t.x,-0.028,t.z]}><boxGeometry args={[t.w,0.005,t.h]} /><meshStandardMaterial color="#c87533" metalness={0.85} roughness={0.2} /></mesh>)}
      {vias.map(([vx,vz],i) => (
        <group key={i}>
          <mesh position={[vx,-0.025,vz]}><cylinderGeometry args={[0.06,0.06,0.008,12]} /><meshStandardMaterial color="#d4a84b" metalness={0.9} roughness={0.15} /></mesh>
          <mesh position={[vx,-0.02,vz]}><cylinderGeometry args={[0.025,0.025,0.01,8]} /><meshStandardMaterial color="#0a3318" /></mesh>
        </group>
      ))}
      <lineSegments position={[0,-0.018,0]}><primitive object={boardEdgesGeo} attach="geometry" /><lineBasicMaterial color="#ffffff" /></lineSegments>
    </group>
  );
}
function PCBComponent({ position, label, channelNumber }: { position: [number,number,number]; label: string; channelNumber?: number }) {
  switch (label) {
    case "Resistor": return <Resistor position={position} />;
    case "Capacitor": return <Capacitor position={position} />;
    case "Diode": return <Diode position={position} />;
    case "LED": return <LED position={position} />;
    case "Transistor": return <Transistor position={position} />;
    case "Channel Port": case "ChannelPort": return <ChannelPort position={position} channelNumber={channelNumber??1} />;
    default: return null;
  }
}
export default function PCBWorkspace({ items, onItemsChange }: PCBWorkspaceProps) {
  const [droppedItems, setDroppedItems] = useState<DroppedItem[]>(items ?? []);
  const channelCountRef = useRef(0);
  const [chatOpen, setChatOpen] = useState(false);
  const [chatInput, setChatInput] = useState("");
  const [chatBusy, setChatBusy] = useState(false);
  const [chatMessages, setChatMessages] = useState<ChatMessage[]>([{ role:"assistant", content:"Hi! I am MiniMEE. Try: Place R1. (Requires Flask server on 127.0.0.1:5000)" }]);
  const [nnPanelOpen, setNNPanelOpen] = useState(false);
  useEffect(() => { if (items) setDroppedItems(items); }, [items]);
  const handleDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    const type = e.dataTransfer.getData("text/plain");
    if (!type) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const x = Math.round(((e.clientX-rect.left)/rect.width*6-3)/0.1)*0.1;
    const y = Math.round(((e.clientY-rect.top)/rect.height*-4+2)/0.1)*0.1;
    setDroppedItems(prev => {
      const updated = [...prev, {type,x,y}];
      onItemsChange?.(updated);
      syncBoardItems(updated).catch(()=>{});
      return updated;
    });
  }, [onItemsChange]);
  const submitChat = useCallback(async () => {
    const text = chatInput.trim();
    if (!text||chatBusy) return;
    setChatBusy(true); setChatInput("");
    setChatMessages(prev => [...prev, {role:"user",content:text}]);
    try {
      const reply = await handleChatInput(text);
      setChatMessages(prev => [...prev, {role:"assistant",content:reply}]);
    } catch(err) {
      const message = err instanceof Error ? err.message : String(err);
      setChatMessages(prev => [...prev, {role:"assistant",content:`Error: ${message}`}]);
    } finally { setChatBusy(false); }
  }, [chatInput, chatBusy]);
  return (
    <div className="w-full h-full relative" onDragOver={e=>e.preventDefault()} onDrop={handleDrop}>
      <Canvas camera={{position:[4,5,4],fov:50}} shadows>
        <ambientLight intensity={0.5} />
        <directionalLight position={[5,8,5]} intensity={1.2} castShadow />
        <directionalLight position={[-3,4,-2]} intensity={0.3} />
        <PCBBoard />
        <Grid infiniteGrid cellSize={0.1} cellThickness={0.3} cellColor="#004466" sectionSize={0.5} sectionThickness={0.8} sectionColor="#0077aa" fadeDistance={25} fadeStrength={1.5} followCamera={false} position={[0,-0.12,0]} />
        {(() => {
          channelCountRef.current = 0;
          return droppedItems.map((item,i) => {
            const isCh = item.type==="Channel Port"||item.type==="ChannelPort";
            if (isCh) channelCountRef.current++;
            return <PCBComponent key={i} position={[item.x,-0.03,item.y]} label={item.type} channelNumber={isCh?channelCountRef.current:undefined} />;
          });
        })()}
        <OrbitControls enablePan enableZoom enableRotate maxPolarAngle={Math.PI/2.1} minDistance={2} maxDistance={15} />
      </Canvas>
      <button type="button" onClick={()=>setChatOpen(v=>!v)} className="absolute top-3 right-3 z-[9999] rounded-md px-4 py-2 font-semibold border border-cyan-300/30 text-cyan-50 bg-gradient-to-b from-[#0b2a46]/90 to-[#06182b]/90 shadow-[0_8px_20px_rgba(0,0,0,0.35)] hover:border-cyan-200/50 hover:text-white">
        {chatOpen ? "Close MiniMEE" : "Ask MiniMEE"}
      </button>
      <button type="button" onClick={()=>setNNPanelOpen(v=>!v)} className="absolute top-14 right-3 z-[9999] rounded-md px-4 py-2 font-semibold border border-emerald-400/30 text-emerald-200 bg-gradient-to-b from-[#071f12]/90 to-[#030f09]/90 shadow-[0_8px_20px_rgba(0,0,0,0.35)] hover:border-emerald-300/50 hover:text-white">
        {nnPanelOpen ? "Close Vision" : "JEPA Vision"}
      </button>
      {chatOpen && (
        <div className="absolute top-14 right-3 z-[9999] w-[360px] max-w-[90vw] h-[520px] max-h-[75vh] rounded-xl border border-cyan-300/30 bg-gradient-to-b from-[#071a2e] to-[#04101f] shadow-[0_12px_28px_rgba(0,0,0,0.55)] flex flex-col overflow-hidden"
          style={{backgroundImage:`linear-gradient(to bottom, rgba(7,26,46,0.55), rgba(4,16,31,0.65)), url(${miniMeeBg})`,backgroundSize:"contain",backgroundPosition:"center 75%",backgroundRepeat:"no-repeat"}}>
          <div className="px-4 py-3 border-b border-cyan-200/15 flex items-center justify-between">
            <div className="font-bold tracking-wide text-cyan-50">Mini<span className="text-cyan-300">MEE</span></div>
            <div className="text-xs text-cyan-100/75">Robot Assistant</div>
          </div>
          <div className="flex-1 overflow-auto p-4 space-y-3 text-sm">
            {chatMessages.map((m,idx) => (
              <div key={idx} className={m.role==="user"?"text-right":"text-left"}>
                <div className={["inline-block max-w-[90%] px-3 py-2 rounded-lg border", m.role==="user"?"bg-cyan-500/15 border-cyan-300/25 text-cyan-50":"bg-black/25 border-white/10 text-white"].join(" ")}>
                  <div className="text-[11px] mb-1 opacity-75">{m.role==="user"?"you":"MiniMEE"}</div>
                  <div className="whitespace-pre-wrap">{m.content}</div>
                </div>
              </div>
            ))}
          </div>
          <div className="p-3 border-t border-cyan-200/15 flex gap-2">
            <input value={chatInput} onChange={e=>setChatInput(e.target.value)} onKeyDown={e=>{if(e.key==="Enter")submitChat();}} className="flex-1 rounded-md px-3 py-2 text-sm bg-[#e8f3ff] text-[#001524] border border-cyan-700/30 focus:outline-none focus:ring-2 focus:ring-cyan-400/40" placeholder="Try: Place R1" disabled={chatBusy} />
            <button type="button" onClick={submitChat} className="px-4 py-2 rounded-md font-semibold text-sm bg-cyan-300 text-[#001524] border border-cyan-200/30 hover:bg-cyan-200 disabled:opacity-50" disabled={chatBusy||!chatInput.trim()}>{chatBusy?"...":"Send"}</button>
          </div>
        </div>
      )}
      <NNPanel open={nnPanelOpen} onClose={()=>setNNPanelOpen(false)} />
    </div>
  );
}
