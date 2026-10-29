"""The Game View's own icons, for what Blender 2.79 has no icon for, drawn in
the style of Blender's icons (16 x 16, a dark outline around light shapes,
a coloured accent). Writes one SVG per icon: new_icons.py <outdir>."""
import os
import sys

OUTLINE = 'stroke="#000" stroke-opacity="0.75" stroke-width="1" stroke-linejoin="round" stroke-linecap="round"'
LIGHT = 'fill="#e8e8e8"'
MID = 'fill="#b8b8b8"'
NONE = 'fill="none"'


def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">'
            + body + '</svg>')


def gate(kind):
    """logic gates: AND (flat back, round front), OR (curved), with a bubble for NOT,
    a second back line for XOR"""
    parts = []
    if kind in ("AND", "NAND"):
        d = "M2.5,3.5 H7.5 A4.5,4.5 0 0 1 7.5,12.5 H2.5 Z"
    else:
        d = "M2.5,3.5 H6.5 Q11,3.5 13,8 Q11,12.5 6.5,12.5 H2.5 Q4.5,8 2.5,3.5 Z"
    parts.append('<path d="%s" %s %s/>' % (d, LIGHT, OUTLINE))
    if kind in ("XOR", "XNOR"):
        parts.append('<path d="M1,3.5 Q3,8 1,12.5" %s %s/>' % (NONE, OUTLINE))
    if kind in ("NAND", "NOR", "XNOR"):
        parts.append('<circle cx="14" cy="8" r="1.4" fill="#5a9cff" %s/>' % OUTLINE)
    else:
        parts.append('<circle cx="13.6" cy="8" r="0.9" fill="#5a9cff"/>')
    return svg("".join(parts))


ICONS = {
    "GV_AND": gate("AND"), "GV_NAND": gate("NAND"),
    "GV_OR": gate("OR"), "GV_NOR": gate("NOR"),
    "GV_XOR": gate("XOR"), "GV_XNOR": gate("XNOR"),
    # a keyboard: a key board with three rows of keys
    "GV_KEYBOARD": svg('<rect x="0.5" y="4.5" width="15" height="8" rx="1.5" %s %s/>' % (LIGHT, OUTLINE) +
                       "".join('<rect x="%g" y="%g" width="1.6" height="1.4" fill="#555"/>' % (2 + 2.4 * i, 6)
                               for i in range(5)) +
                       "".join('<rect x="%g" y="%g" width="1.6" height="1.4" fill="#555"/>' % (3 + 2.4 * i, 8.2)
                               for i in range(4)) +
                       '<rect x="4" y="10.4" width="8" height="1.2" fill="#e8a33a"/>'),
    # two bodies touching, with the contact spark
    "GV_COLLISION": svg('<circle cx="5" cy="9" r="3.6" %s %s/>' % (LIGHT, OUTLINE) +
                        '<rect x="8.6" y="3.5" width="6" height="6" rx="0.8" %s %s/>' % (MID, OUTLINE) +
                        '<path d="M8.3,6.8 L9.2,8 L8,8.6 L9.4,10" stroke="#ff7a1a" stroke-width="1.2" fill="none"/>'),
    # a radar: a cone of view from a dot
    "GV_RADAR": svg('<path d="M2,13.5 L14,5 A8,8 0 0 1 14.6,13.5 Z" fill="#9fd3ff" fill-opacity="0.9" %s/>' % OUTLINE +
                    '<path d="M2,13.5 L11,8.2 M2,13.5 L11.7,12" stroke="#2b6fb3" stroke-width="0.8" fill="none"/>' +
                    '<circle cx="2.4" cy="13.2" r="1.6" %s %s/>' % (LIGHT, OUTLINE)),
    # an envelope
    "GV_MESSAGE": svg('<rect x="1.5" y="3.5" width="13" height="9" rx="1" %s %s/>' % (LIGHT, OUTLINE) +
                      '<path d="M1.8,4 L8,9 L14.2,4" %s %s/>' % (NONE, OUTLINE) +
                      '<circle cx="12.5" cy="11" r="1.3" fill="#e8a33a"/>'),
    # a navigation mesh: a floor of triangles with a path across it
    "GV_NAVMESH": svg('<path d="M1.5,12.5 L5,4.5 L11,3.5 L14.5,10 L9,13.5 Z" fill="#a6e3a1" %s/>' % OUTLINE +
                      '<path d="M5,4.5 L9,13.5 M11,3.5 L9,13.5 M5,4.5 L14.5,10" stroke="#3d7a37" stroke-width="0.6" fill="none"/>' +
                      '<path d="M3,11 Q7,7 12.5,6" stroke="#ff7a1a" stroke-width="1.3" fill="none" stroke-dasharray="1.5,1"/>'),
    # an ear (the listener)
    "GV_EAR": svg('<path d="M5,13.5 Q3,13.5 3.5,11.5 Q4.5,9 4.5,6.5 Q4.5,2.5 8.5,2.5 Q12.5,2.5 12.5,6.5 '
                  'Q12.5,9.5 9.5,10.5 Q8,11 8,12.5 Q8,13.8 6.5,14 Z" %s %s/>' % (LIGHT, OUTLINE) +
                  '<path d="M7,6.5 Q7,4.8 8.6,4.8 Q10.2,4.8 10.2,6.5 Q10.2,8 8.6,8.4" stroke="#555" stroke-width="0.9" fill="none"/>'),
    # a screen (the player's screen, the interface panel)
    "GV_SCREEN": svg('<rect x="1" y="2.5" width="14" height="9.5" rx="1" %s %s/>' % (LIGHT, OUTLINE) +
                     '<rect x="2.6" y="4" width="10.8" height="6.5" fill="#5a9cff"/>' +
                     '<rect x="3.4" y="4.8" width="3.5" height="1.1" fill="#fff"/>' +
                     '<path d="M5.5,14.5 H10.5 M8,12 V14.5" %s/>' % OUTLINE),
}


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, body in ICONS.items():
        with open(os.path.join(out, name + ".svg"), "w") as f:
            f.write(body)
        print("ok", name)


if __name__ == "__main__":
    main()
