# builds index.html + devlog/index.html from the brand pieces. run: python3 tools/build.py
import re, html, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
C = dict(ink="#111111", paper="#F4EFE6", ultra="#4B3CF5", ember="#FF6B2C", forest="#0F5C3A", lime="#E4FF7A",
         pink="#FF2E88", sun="#FFD23F", lilac="#C9A0DC", mint="#2AD18B", blush="#FFC7E8", wine="#7A1E2C")

def quarter(cx, cy, r, fill, q):
    # q: 0 = top-right, 1 = bottom-right
    if q == 0:
        return f'<path fill="{fill}" d="M{cx} {cy-r}A{r} {r} 0 0 1 {cx+r} {cy}H{cx}Z"/>'
    return f'<path fill="{fill}" d="M{cx} {cy}H{cx+r}A{r} {r} 0 0 1 {cx} {cy+r}Z"/>'

def rings(cx, cy, r, col, n=4):
    s, step = "", r / n
    for i in range(n):
        ro = r - i * step; ri = ro - step * .62
        s += (f'<path fill="{col}" d="M{cx} {cy+ro:g}A{ro:g} {ro:g} 0 0 0 {cx+ro:g} {cy}'
              f'H{cx+ri:g}A{ri:g} {ri:g} 0 0 1 {cx} {cy+ri:g}Z"/>')
    return s

def checker(x, y, n, m, s, a, b):
    return "".join(f'<rect x="{x+j*s}" y="{y+i*s}" width="{s}" height="{s}" fill="{a if (i+j)%2 else b}"/>'
                   for i in range(n) for j in range(m))

def svg(vb, body, label=None):
    a = f'role="img" aria-label="{label}"' if label else 'aria-hidden="true"'
    return f'<svg viewBox="{vb}" {a}>{body}</svg>'

collage = svg("0 0 600 600",
    f'<rect width="600" height="600" fill="{C["lilac"]}"/>'
    f'<rect x="56" y="56" width="150" height="488" fill="{C["ultra"]}"/>'
    + checker(56, 56, 2, 2, 75, C["ultra"], C["lime"])
    + quarter(206, 300, 244, C["ember"], 0)
    + quarter(206, 300, 244, C["lime"], 1) + rings(206, 300, 244, C["forest"], 5)
    + f'<circle cx="510" cy="96" r="58" fill="{C["pink"]}"/>'
    + checker(478, 452, 3, 3, 30, C["ink"], C["paper"]),
    "the daysetta mark as a collage: a stem, a rising quarter sun and a quarter of rings")

ico = {
 "sun":   svg("0 0 96 96", f'<circle cx="48" cy="48" r="40" fill="currentColor"/>'),
 "arch":  svg("0 0 96 96", quarter(8, 8, 80, "currentColor", 1).replace('fill="currentColor"', 'fill="none"') + rings(8, 8, 80, "currentColor", 3)),
 "check": svg("0 0 96 96", checker(8, 8, 4, 4, 20, "currentColor", "transparent")),
 "quart": svg("0 0 96 96", quarter(8, 88, 80, "currentColor", 0)),
 "stem":  svg("0 0 96 96", '<rect x="30" y="6" width="36" height="84" fill="currentColor"/>'),
 "dot":   svg("0 0 96 96", '<circle cx="28" cy="28" r="20" fill="currentColor"/><circle cx="68" cy="68" r="20" fill="currentColor"/>'),
 "half":  svg("0 0 96 96", '<path fill="currentColor" d="M8 88A40 40 0 0 1 88 88Z"/><rect x="8" y="8" width="80" height="20" fill="currentColor"/>'),
}

def nav(prefix, current=""):
    links = [("how it works", f"{prefix}#how"), ("status", f"{prefix}#status"), ("roadmap", f"{prefix}#roadmap"),
             ("faq", f"{prefix}#faq"), ("devlog", f"{prefix}devlog/" if prefix == "./" else "./")]
    a = "".join(f'<a href="{h}"{" aria-current=\"page\"" if t == current else ""}>{t}</a>' for t, h in links)
    b = "./" if prefix == "./" else "../"
    return f'''<a class="skip" href="#main">skip to content</a>
<header class="nav"><div class="wrap nav-in">
  <a class="brand" href="{b}" aria-label="daysetta home"><img class="lt" src="{b}brand/lockup-horizontal-color.svg" alt="daysetta" width="190" height="34"><img class="dk" src="{b}brand/lockup-horizontal-color-dark.svg" alt="" width="190" height="34"></a>
  <nav class="nav-links" aria-label="main">{a}</nav>
  <div class="nav-actions">
    <button class="theme-btn" id="themeBtn" type="button" aria-label="switch dark mode" aria-pressed="false">
      <svg class="i-sun" viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><circle cx="12" cy="12" r="6" fill="currentColor"/></svg>
      <svg class="i-moon" viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path d="M20 14.5A8.5 8.5 0 0 1 9.5 4a8.5 8.5 0 1 0 10.5 10.5Z" fill="currentColor"/></svg>
    </button>
    <a class="btn btn-solid" href="https://github.com/ducminh1110/DaySetta">GitHub</a>
  </div>
</div></header>'''

def head(title, desc, b):
    return f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="{desc}">
<meta property="og:title" content="{title}">
<meta property="og:description" content="{desc}">
<meta property="og:image" content="{b}brand/lockup-horizontal-color.png">
<meta name="theme-color" content="#F4EFE6">
<link rel="stylesheet" href="{b}fonts/fonts.css">
<link rel="stylesheet" href="{b}css/site.css">
<link rel="icon" href="{b}brand/favicon.svg" type="image/svg+xml">
<link rel="icon" href="{b}brand/favicon.ico" sizes="any">
<link rel="apple-touch-icon" href="{b}brand/app-icon-180.png">
<script>
try {{
  var t = localStorage.getItem('ds-theme');
  if (t === 'dark' || (!t && matchMedia('(prefers-color-scheme: dark)').matches)) document.documentElement.dataset.theme = 'dark';
}} catch (e) {{}}
</script>
</head>
<body>'''

def foot(b):
    return f'''<footer class="foot"><div class="wrap">
  <img class="foot-word" src="{b}brand/wordmark-white.svg" alt="daysetta" width="948" height="305">
  <div class="foot-grid">
    <p>the real macOS desktop on arm linux. no vm.<br>a personal project, for fun.</p>
    <nav aria-label="footer"><a href="{b}#how">how it works</a><a href="{b}#status">status</a><a href="{b}#roadmap">roadmap</a><a href="{b}devlog/">devlog</a><a href="https://github.com/ducminh1110/DaySetta">GitHub</a><a href="https://github.com/darlinghq/darling">Darling</a><a href="https://github.com/steelbrain/reims-vgpu">reims-vgpu</a></nav>
  </div>
  <p class="legal">macOS is a trademark of Apple Inc. DaySetta is not affiliated with Apple. Built on Darling, reims-vgpu and metal2vulkan. No Apple artwork on this site.</p>
</div></footer>
<script src="{b}js/site.js"></script>
</body>
</html>
'''

# devlog entries come from the old page (each one quotes ~/daysetta/devlog/decisions.md)
old = (ROOT / "devlog/index.html").read_text()
posts = re.findall(r'<article class="post-card" id="([^"]+)">\s*<span class="mono tag tag-([a-z]+)">([^<]+)</span>\s*<h3>(.*?)</h3>\s*<p>(.*?)</p>\s*<span class="mono post-src">(.*?)</span>', old, re.S)
if posts:
    (ROOT / "tools/devlog-posts.txt").write_text("\n\x1e\n".join("\x1f".join(p) for p in posts))
else:
    posts = [tuple(x.split("\x1f")) for x in (ROOT / "tools/devlog-posts.txt").read_text().split("\n\x1e\n")]
TAGC = {"gpu": "c1", "kernel": "c2", "boot": "c3", "input": "c4", "display": "c5", "rules": "c6"}

def card(p, short=False, i=None):
    pid, tag, tagtxt, h, body, src = p
    cls = f"c{i % 6 + 1}" if i is not None else TAGC.get(tag, "c6")
    if short:
        body = re.split(r'(?<=\.)\s', html.unescape(body), maxsplit=1)[0]
        body = html.escape(body)
    return (f'<article class="card {cls}" id="{pid}"><span class="tag">{tagtxt}</span>'
            f'<h3>{h}</h3><p>{body}</p><span class="src">{src}</span></article>')

INDEX = head("daysetta: the real macOS desktop on arm linux. no vm.",
             "DaySetta runs Apple's own macOS 27 userland unmodified on a plain ARM Linux box. No VM, no hypervisor. A personal project, with an honest devlog.", "./") + nav("./") + f'''
<main id="main">
<section class="hero"><div class="wrap hero-grid">
  <div>
    <span class="chip lime">a personal systems project</span>
    <h1>the real macOS desktop. on arm linux. <span class="pill">no&nbsp;vm.</span></h1>
    <p class="lede">DaySetta runs Apple's own macOS&nbsp;27 userland (WindowServer, AppKit, the daemons) unmodified, on a plain ARM Linux box. Everything below it is faked: a userspace XNU, fake kexts, Metal translated to Vulkan.</p>
    <div class="cta"><a class="btn btn-solid" href="devlog/">read the devlog</a><a class="btn btn-ghost" href="https://github.com/ducminh1110/DaySetta">view on GitHub</a></div>
  </div>
  <div class="collage">{collage}</div>
</div></section>

<div class="wrap stats" aria-label="facts">
  <div class="stat s1"><b>macOS 27</b><span>guest userland,<br>arm64e</span></div>
  <div class="stat s2"><b>xnu-12377</b><span>kernel, running<br>in userspace</span></div>
  <div class="stat s3"><b>ARMv8.0</b><span>host cpu, PAC + LSE<br>emulated</span></div>
  <div class="stat s4"><b>0 VMs</b><span>no hypervisor.<br>really.</span></div>
</div>

<section class="sec" id="what"><div class="wrap">
  <div class="sec-head"><span class="chip">what this is</span><h2>don't reimplement macOS. run it, and fake the floor.</h2></div>
  <div class="what-grid">
    <div class="what-text">
      <p>Most macOS-on-Linux attempts rewrite Apple's software. DaySetta goes the other way: it runs Apple's own binaries (WindowServer, SkyLight, AppKit, the launchd daemons) exactly as shipped in the macOS&nbsp;27 IPSW, and fakes everything below them.</p>
      <p>It's the follow-up to Daylight, an older x86 experiment that tried to reimplement WindowServer. That taught the lesson this project is built on: the protocol is the hard part, so let Apple's code speak it.</p>
    </div>
    <div class="note"><span class="chip lime">rule #1</span><p>Apple binaries never live in git. Everything Apple comes from your own IPSW.</p></div>
  </div>
</div></section>

<section class="sec" id="how"><div class="wrap">
  <div class="sec-head"><span class="chip ultra">how it works</span><h2>five floors. each one pretends to be what the floor above expects.</h2></div>
  <div class="floors">
    <div class="floor f1"><span class="n">05</span><h3>macOS 27 userland</h3><p>real arm64e binaries from the IPSW: WindowServer, AppKit apps, launchd daemons.</p>{ico["sun"]}</div>
    <div class="joint">↕ Mach · XPC · IOKit</div>
    <div class="floor f2"><span class="n">04</span><h3>Darling</h3><p>the kernel that isn't: userspace XNU (darlingserver), dyld, libsystem_kernel, ported up to xnu-12377.</p>{ico["arch"]}</div>
    <div class="floor f3"><span class="n">03</span><h3>iokitd</h3><p>there are no kexts. iokitd answers their user clients: IOMobileFramebuffer, AppleParavirtGPU (IOGPU), IOSurface, IOHID.</p>{ico["check"]}</div>
    <div class="floor f4"><span class="n">02</span><h3>reims-vgpu</h3><p>a paravirt GPU device: the guest's Metal command buffers are translated to Vulkan. Metal stays Apple's own driver.</p>{ico["quart"]}</div>
    <div class="joint">↕ syscalls</div>
    <div class="floor f5"><span class="n">01</span><h3>Linux aarch64</h3><p>plain ARMv8.0 with no PAC or LSE, so those instructions get emulated (src/exec).</p>{ico["stem"]}</div>
  </div>
</div></section>

<section class="sec" id="status"><div class="wrap">
  <div class="sec-head"><span class="chip pink">honest status · nov 2026</span><h2>login completes. the pixels are still black.</h2><p class="lede">No mock-ups here. This is exactly where the project stands, with the log line to prove it.</p></div>
  <div class="status-grid">
    <div class="box ok"><h3>works</h3><ul>
      <li>WindowServer 27 boots and stays up</li>
      <li>the Metal driver comes up, command buffers execute, swaps reach the display</li>
      <li>first real pixels through the whole GPU → present → readback path (a test pattern, not a window yet)</li>
      <li>an AppKit test app launches, connects to WindowServer and runs its whole script</li>
      <li>login completes: Dock, MenuBarAgent, SystemUIServer, WallpaperAgent and WindowManager all run</li>
    </ul></div>
    <div class="box nope"><h3>not yet</h3><ul>
      <li>composited frames are still black. the current fight: tracing where between draw and present the pixels turn to zero</li>
      <li>keyboard / mouse (HID) is half done</li>
      <li>Finder and the default apps as a usable desktop</li>
      <li>a desktop screenshot. none exists yet, so this page has no fake one</li>
    </ul></div>
    <div class="log"><span class="chip lime">boot log · lw452</span><p class="mono"><span class="k">LoginComplete</span>, ALL desktop agents alive (Dock, MenuBarAgent, SystemUIServer, WallpaperAgent, WindowManager), 12 frames all zero. the black is real, not probe damage.</p></div>
  </div>
  <p class="guess">Honest guess from the repo: “a usable desktop is still days to weeks out, not hours.”</p>
</div></section>

<section class="sec" id="roadmap"><div class="wrap">
  <div class="sec-head"><span class="chip">the road ahead</span><h2>one milestone at a time.</h2></div>
  <div class="road">
    <div class="step r1">{ico["sun"]}<span class="chip">now</span><h3>first real window pixels</h3><p>The display path works end to end; the composite still comes out black. Find the stage where it turns to zero, fix it.</p></div>
    <div class="step r2">{ico["dot"]}<span class="chip">next</span><h3>input, end to end</h3><p>HID is half done: the event plumbing exists, the last dispatch gaps don't. Keys and clicks need to land in AppKit.</p></div>
    <div class="step r3">{ico["half"]}<span class="chip">then</span><h3>a desktop you can use</h3><p>Finder, the Dock, the default macOS&nbsp;27 apps behaving like a daily desktop.</p></div>
    <div class="step r4">{ico["check"]}<span class="chip">planned</span><h3>third-party apps</h3><p>.dmg and .pkg install paths the way macOS does them, then app-by-app work. No app-specific hacks.</p></div>
    <div class="step r5">{ico["quart"]}<span class="chip lime">north star</span><h3>drop an IPSW in a window</h3><p>A desktop app where you drag a macOS IPSW in and the desktop appears inside that window. Planned, not started.</p></div>
  </div>
  <p class="road-note">The old from-scratch design docs (VISION, ARCHITECTURE, ROADMAP) describe a dropped plan with no Darling. They're kept in the repo for history; the list above is the live direction.</p>
</div></section>

<section class="sec" id="faq"><div class="wrap faq-grid">
  <div class="vm"><span class="chip lime">is this a VM?</span><p class="no">no.</p><p>There's no hypervisor and no virtual machine. macOS code runs as ordinary Linux processes on the same kernel; the “hardware” it sees is answered in userspace.</p></div>
  <div class="qa">
    <details><summary>so… is it a Hackintosh?</summary><p>No. Linux stays Linux. DaySetta doesn't boot macOS or replace your OS. It runs the macOS userland (WindowServer, AppKit, daemons) as processes and fakes the kernel and devices below them.</p></details>
    <details><summary>where do the Apple binaries come from?</summary><p>Your own macOS 27 IPSW. No Apple binaries are committed to git; the repo only holds the translation layers and tooling.</p></details>
    <details><summary>does it work today?</summary><p>Not as a usable desktop, no. Login completes and all the desktop agents run, but composited frames are still black. The repo's own guess is days to weeks, not hours.</p></details>
    <details><summary>what hardware do I need?</summary><p>An ARM (aarch64) Linux box. The reference host is plain ARMv8.0; macOS 27 is arm64e, so PAC and LSE instructions get emulated. A GPU helps: without one, Vulkan falls back to llvmpipe and it's slow.</p></details>
    <details><summary>why not just use Darling's GUI?</summary><p>Darling is the kernel floor here, not the whole stack. DaySetta adds the display, GPU and input side (iokitd and reims-vgpu) so Apple's real WindowServer and Metal driver can run instead of a reimplemented server.</p></details>
    <details><summary>what about x86 hosts?</summary><p>Later. The goal is running ARM Mac apps first; the ARM build has to mature before an x86_64 path (via an ARM64 emulator) makes sense.</p></details>
    <details><summary>is this affiliated with Apple?</summary><p>No. It's a personal project, for fun. macOS is a trademark of Apple Inc.</p></details>
  </div>
</div></section>

<section class="sec" id="devlog"><div class="wrap">
  <div class="sec-head"><span class="chip">notes from the lab</span><h2>the devlog is the product demo.</h2><p class="lede">Real bugs and real fixes, quoted straight from the project's decisions file.</p></div>
  <div class="cards">{"".join(card(p, True, i) for i, p in enumerate(posts[:3]))}</div>
  <div class="more"><a class="btn btn-ghost" href="devlog/">all entries →</a></div>
</div></section>
</main>
''' + foot("./")

DEVLOG = head("devlog · daysetta", "Notes from the lab: the DaySetta devlog, quoted from the project's decisions file. Real bugs, real fixes, no marketing.", "../") + nav("../", "devlog") + f'''
<main id="main"><div class="wrap">
  <div class="page-head"><span class="chip lime">notes from the lab</span><h1>devlog.</h1><p class="lede">Every entry is quoted from <span class="mono">devlog/decisions.md</span> in the repo: what broke, why, what fixed it. Newest first, {len(posts)} entries.</p></div>
  <div class="log-list">{"".join(card(p, i=i) for i, p in enumerate(posts))}</div>
</div></main>
''' + foot("../")

(ROOT / "index.html").write_text(INDEX)
(ROOT / "devlog/index.html").write_text(DEVLOG)
print("built", len(posts), "posts")
