import re

HTML_FILE = "index.html"   # change if your file has a different name/path

with open(HTML_FILE, "r", encoding="utf-8") as f:
    html = f.read()

# --- 1. Replace MiniMEE in Action iframe with clickable thumbnail ---
minimee_old = '''        <div class="yt-wrap">
          <iframe
            src="https://www.youtube.com/embed/NKKJLQduE7Y"
            title="MiniMEE Robot Arm Demo"
            allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture"
            allowfullscreen></iframe>
        </div>'''

minimee_new = '''        <a href="https://www.youtube.com/watch?v=NKKJLQduE7Y" target="_blank" rel="noopener" class="yt-thumb">
          <img src="https://img.youtube.com/vi/NKKJLQduE7Y/hqdefault.jpg" alt="MiniMEE Robot Arm Demo" />
          <span class="yt-play"></span>
        </a>'''

# --- 2. Replace PCB Workspace Software iframe with clickable thumbnail ---
pcb_old = '''        <div class="yt-wrap">
          <iframe
            src="https://www.youtube.com/embed/HpRibWbAz1g"
            title="PCB Workspace Software Demo"
            allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture"
            allowfullscreen></iframe>
        </div>'''

pcb_new = '''        <a href="https://www.youtube.com/watch?v=HpRibWbAz1g" target="_blank" rel="noopener" class="yt-thumb">
          <img src="https://img.youtube.com/vi/HpRibWbAz1g/hqdefault.jpg" alt="PCB Workspace Software Demo" />
          <span class="yt-play"></span>
        </a>'''

# --- 3. CSS for the thumbnails ---
css_anchor = '''    .yt-wrap iframe {
      position: absolute;
      top: 0; left: 0;
      width: 100%; height: 100%;
      border: 0;
    }'''

css_new = css_anchor + '''

    .yt-thumb {
      position: relative;
      display: block;
      margin-top: 16px;
      border-radius: 10px;
      overflow: hidden;
      border: 2px solid #aac4e8;
      background: #000;
    }
    .yt-thumb img { width: 100%; display: block; }
    .yt-play {
      position: absolute;
      top: 50%; left: 50%;
      transform: translate(-50%, -50%);
      width: 68px; height: 48px;
      background: rgba(13,45,110,.85);
      border-radius: 12px;
    }
    .yt-play::after {
      content: "";
      position: absolute;
      top: 50%; left: 52%;
      transform: translate(-50%, -50%);
      border-style: solid;
      border-width: 11px 0 11px 18px;
      border-color: transparent transparent transparent #fff;
    }
    .yt-thumb:hover .yt-play { background: var(--blue); }'''

# --- Apply replacements with safety checks ---
def replace_once(text, old, new, label):
    count = text.count(old)
    if count == 0:
        print(f"  [SKIP] {label}: anchor not found (already patched or different text?)")
        return text
    if count > 1:
        print(f"  [WARN] {label}: found {count} matches, replacing all")
    print(f"  [OK]   {label}: patched")
    return text.replace(old, new)

print("Patching", HTML_FILE)
html = replace_once(html, minimee_old, minimee_new, "MiniMEE in Action video")
html = replace_once(html, pcb_old, pcb_new, "PCB Workspace Software video")
html = replace_once(html, css_anchor, css_new, "Thumbnail CSS")

with open(HTML_FILE, "w", encoding="utf-8") as f:
    f.write(html)

print("Done. Hard-refresh your browser (Ctrl+Shift+R).")
