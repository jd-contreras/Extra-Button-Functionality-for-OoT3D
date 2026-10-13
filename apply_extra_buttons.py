"""Add Extra Buttons to a checkout of the OoT3D Randomizer source.

    python apply_extra_buttons.py <path to an OoT3D_Randomizer checkout>

Then build the patch there as usual (make REGION=USA or REGION=EUR in its code/ folder).

Extra Buttons is only added on top of the randomizer; nothing of the randomizer's is replaced:

- item_buttons.c is copied into code/src/
- hooks.s and patches.s (the files next to this script) are appended after the randomizer's own
- one linker entry per hooked game address (SITES below) is added to code/oot.ld
- five declarations are added to code/include/input.h
- two calls are added to code/src/main.c

Tested on randomizer v4.0.2 and nightly 6bd4b80, which lay their files out differently; both
layouts are handled. If a later version moved one of the anchors used here, the script stops
with an assertion instead of producing a wrong build.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# Game addresses Extra Buttons hooks (USA). The Europe game only differs between SHIFT_START and
# 0x4A5B00: by 0x24 up to SHIFT_CHANGE, then 0x20. Sites outside that range take no offset.
SHIFT_START, SHIFT_CHANGE = 0x41A144, 0x436690
SITES = [
    ('ItemAssignXDestination1', 0x2EC58C),
    ('ItemAssignXPosition1', 0x2EC598),
    ('ItemAssignIPosition1', 0x2EC5A4),
    ('ItemAssignYDestination1', 0x2EC7D4),
    ('ItemAssignYPosition1', 0x2EC7E0),
    ('ItemAssignIIPosition1', 0x2EC7EC),
    ('ItemAssignXDestination2', 0x2ECDE8),
    ('ItemAssignXPosition2', 0x2ECDF4),
    ('ItemAssignIPosition2', 0x2ECDFC),
    ('ItemAssignYDestination2', 0x2ECF5C),
    ('ItemAssignYPosition2', 0x2ECF68),
    ('ItemAssignIIPosition2', 0x2ECF70),
    ('GearPageActivated', 0x2F0444),
    ('ItemsPageActivated', 0x2F8A34),
    ('MenuRepeat', 0x33B5D0),
    ('MenuButtons', 0x33B5EC),
    ('DpadHudDraw1', 0x42BCF4),
    ('DpadHudDraw2', 0x42BE68),
    ('FilterTouch', 0x436CD8),
    ('DpadHudBuildBoards', 0x45588C),
]
DECLARATIONS = '''
void ItemButtons_Update(GlobalContext* globalCtx);
void ItemButtons_FilterTouch(void* output, s32* count);
void ItemButtons_UpdateHud(void);
void ItemButtons_BuildBoards(void);
void ItemButtons_DrawBoards(void);
'''


def rd(p):
    return open(p, encoding='utf-8', newline='').read().replace('\r\n', '\n')


def wr(p, s):
    open(p, 'w', encoding='utf-8', newline='\n').write(s)


def once(s, pattern, repl):
    out, n = re.subn(pattern, repl, s, flags=re.M)
    assert n == 1, (n, pattern)
    return out


def main(checkout):
    code = os.path.join(checkout, 'code') + '/'
    assert os.path.isfile(code + 'oot.ld'), 'not an OoT3D_Randomizer checkout: ' + checkout
    assert not os.path.exists(code + 'src/item_buttons.c'), 'Extra Buttons is already applied here'
    asm = code + ('src/asm/' if os.path.isdir(code + 'src/asm') else 'src/')  # nightly : v4.0.x

    wr(code + 'src/item_buttons.c', rd(os.path.join(HERE, 'item_buttons.c')))
    for name in ('hooks.s', 'patches.s'):
        wr(asm + name, rd(asm + name).rstrip('\n') + '\n\n' + rd(os.path.join(HERE, name)))

    # Linker entries, after the randomizer's entry for the per-frame hook, in that version's syntax.
    ld = rd(code + 'oot.ld')
    if 'region_offset' in ld:  # v4.0.x
        anchor = r'^\t\.patch_before_GlobalContext_Update 0x452390 \+ region_offset : \{\n.*\n\t\}\n'
        early, late = ' + (DEFINED(_EUR_) ? 0x24 : 0)', ' + region_offset'
        entry = '\t.patch_%s 0x%X%s : {\n\t\t*(.patch_%s)\n\t}\n'
    else:  # nightly
        anchor = r'^\t\.patch_before_GlobalContext_Update 0x452390 \+ _LD_OFF : \{.*\}\n'
        early, late = ' + (_LD_EUR ? 0x24 : 0)', ' + _LD_OFF'
        entry = '\t.patch_%s 0x%X%s : { *(.patch_%s) }\n'
    lines = ['\t/* Extra item buttons */\n']
    for name, addr in SITES:
        off = '' if addr < SHIFT_START else early if addr < SHIFT_CHANGE else late
        lines.append(entry % (name, addr, off, name))
    wr(code + 'oot.ld', once(ld, anchor, lambda m: m.group(0) + ''.join(lines)))

    wr(code + 'include/input.h', rd(code + 'include/input.h').rstrip('\n') + '\n' + DECLARATIONS)

    m = rd(code + 'src/main.c')
    m = once(m, r'^    Input_Update\(\);\n', '    Input_Update();\n    ItemButtons_Update(globalCtx);\n')
    m = once(m, r'^void after_GlobalContext_Update\((void)?\) \{\n', lambda x: x.group(0) + '    ItemButtons_UpdateHud();\n')
    wr(code + 'src/main.c', m)
    print('Extra Buttons added to', checkout)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
