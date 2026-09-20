#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# Generates docs/diagrams/synthesis.svg, the synthesis block diagram of the README ("How it works"), in the
# style of a synthesizer manual: audio path on the top row (solid arrows), modulators on the bottom
# row (dashed arrows going up into the module they control), MIDI input on the left.
# The SVG is the artefact, this script is its source: edit here, run it, check the render (see
# docs/diagrams/README.md), commit both. Plain Python 3, no dependency.
import math, sys

def make():
    ink, soft, bg, box = "#222222", "#666666", "#ffffff", "#ffffff"   # ink, secondary ink, card, block fill
    W, H = 1260, 470
    o = []
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Inter, Helvetica, Arial, sans-serif">')
    o.append(f'<rect width="{W}" height="{H}" rx="12" fill="{bg}"/>')
    o.append('<defs>'
             f'<marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="9" markerHeight="9" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="{ink}"/></marker>'
             f'<marker id="c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="{soft}"/></marker>'
             '</defs>')
    def block(x, y, w, h, title, sub, glyph=None):
        o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{box}" stroke="{ink}" stroke-width="1.6"/>')
        o.append(f'<text x="{x + w/2}" y="{y + 26}" text-anchor="middle" font-size="15" font-weight="600" fill="{ink}" letter-spacing="0.3">{title}</text>')
        if sub:
            o.append(f'<text x="{x + w/2}" y="{y + 46}" text-anchor="middle" font-size="12" fill="{soft}">{sub}</text>')
        if glyph: glyph(x, y, w, h)
    def audio(x1, y1, x2, y2):
        o.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{ink}" stroke-width="2" marker-end="url(#a)"/>')
    def ctrl(points, label=None, lx=None, ly=None, anchor="middle"):
        d = " ".join(f"{'M' if i == 0 else 'L'}{p[0]},{p[1]}" for i, p in enumerate(points))
        o.append(f'<path d="{d}" fill="none" stroke="{soft}" stroke-width="1.6" stroke-dasharray="6 4" marker-end="url(#c)"/>')
        if label:
            o.append(f'<text x="{lx}" y="{ly}" text-anchor="{anchor}" font-size="11.5" fill="{soft}" font-style="italic">{label}</text>')
    # glyphs
    def g_osc(x, y, w, h):
        cx, cy = x + w/2, y + 72
        pts = [(cx - 70 + i, cy - 9 * math.sin(i / 40 * 2 * math.pi)) for i in range(0, 81)]
        o.append('<polyline points="' + " ".join(f"{px:.1f},{py:.1f}" for px, py in pts) + f'" fill="none" stroke="{ink}" stroke-width="1.5"/>')
        o.append(f'<path d="M{cx + 20},{cy} L{cx + 20},{cy - 9} L{cx + 40},{cy - 9} L{cx + 40},{cy + 9} L{cx + 60},{cy + 9} L{cx + 60},{cy - 9} L{cx + 70},{cy - 9}" fill="none" stroke="{ink}" stroke-width="1.5"/>')
    def g_pitch(x, y, w, h):
        cx, cy = x + w/2, y + 60
        pts = [(cx - 60 + t, cy + 20 if t > 60 else cy - 4 + 24 * (1 - (1 - t/60) ** 2.2)) for t in range(0, 121)]   # fast drop first, then flat at End
        o.append('<polyline points="' + " ".join(f"{px:.1f},{py:.1f}" for px, py in pts) + f'" fill="none" stroke="{ink}" stroke-width="1.5"/>')
        o.append(f'<line x1="{cx - 64}" y1="{cy + 24}" x2="{cx + 64}" y2="{cy + 24}" stroke="{soft}" stroke-width="1"/>')
    def g_amp(x, y, w, h):
        cx, cy = x + w/2, y + 60
        o.append(f'<path d="M{cx - 60},{cy + 22} L{cx - 52},{cy - 2} L{cx},{cy - 2} L{cx + 60},{cy + 22}" fill="none" stroke="{ink}" stroke-width="1.5"/>')
        o.append(f'<line x1="{cx - 64}" y1="{cy + 24}" x2="{cx + 64}" y2="{cy + 24}" stroke="{soft}" stroke-width="1"/>')
    # audio row
    ty, th = 100, 100
    block(230, ty, 210, th, "WAVETABLE OSCILLATOR", "sine ↔ square · Shape", g_osc)
    block(500, ty, 200, th, "AMPLIFIER", "envelope × velocity")
    block(760, ty, 130, th, "DRIVE", "0 – 4 ×")
    block(950, ty, 170, th, "SOFT LIMITER", "sum of all voices")
    audio(440, ty + th/2, 500, ty + th/2)
    audio(700, ty + th/2, 760, ty + th/2)
    audio(890, ty + th/2, 950, ty + th/2)
    audio(1120, ty + th/2, 1190, ty + th/2)
    o.append(f'<text x="1193" y="{ty + th/2 - 12}" text-anchor="end" font-size="12" font-weight="600" fill="{ink}">OUT</text>')
    # modulation row
    by, bh = 300, 100
    block(230, by, 210, bh, "PITCH ENVELOPE", "Start → End · Sweep · Curve", g_pitch)
    block(500, by, 200, bh, "AMPLITUDE ENVELOPE", "Attack · Hold · Fade", g_amp)
    block(20, by, 130, bh, "MIDI IN", "note · velocity")
    ctrl([(335, by), (335, ty + th)], "pitch", 345, 262, "start")
    ctrl([(600, by), (600, ty + th)], "level", 610, 262, "start")
    ctrl([(150, by + 40), (230, by + 40)], "trigger · note", 190, by + 30)
    ctrl([(150, by + 70), (185, by + 70), (185, 430), (600, 430), (600, by + bh)], "trigger · velocity", 392, 445)
    # legend
    o.append(f'<line x1="900" y1="40" x2="940" y2="40" stroke="{ink}" stroke-width="2"/><text x="948" y="44" font-size="12" fill="{ink}">audio</text>')
    o.append(f'<line x1="1000" y1="40" x2="1040" y2="40" stroke="{soft}" stroke-width="1.6" stroke-dasharray="6 4"/><text x="1048" y="44" font-size="12" fill="{soft}">control, frozen at Note On</text>')
    o.append(f'<text x="40" y="46" font-size="13" fill="{soft}">One voice: every Note On starts a new one with the parameters of that moment.</text>')
    o.append('</svg>')
    return "\n".join(o)

out = sys.argv[1] if len(sys.argv) > 1 else "docs/diagrams/synthesis.svg"
open(out, "w").write(make() + "\n")
print("written", out)
