#!/usr/bin/env python3
"""Compare two ui_dump outputs.

Usage: compare.py before.txt after.txt [--allow-removed REGEX ...] [--allow-appended]
                  [--ignore-sections REGEX]

Sections ("===== name =====") are compared as multisets of lines, except the focus chain
sections, which are compared as sequences after dropping non-focusable (fp=0) entries.
Lines matching an --allow-removed regex may be missing in the second file; such lines are
summarised, everything else is reported as a real difference.
With --allow-appended the second file may have more sections at its end, for states that were
added to the end of the script after the first file was made; they are counted, not compared.
With --ignore-sections sections whose title line matches the regex are dropped from both files
before anything is compared, for states that were rewritten on purpose; they are counted.
"""
import collections
import re
import sys

def sections(path):
    result = []
    name, lines = '<preamble>', []
    text = open(path, encoding='utf-8', errors='replace').read()
    # Without the empty string after the final newline, so that the last section of one file
    # compares equal to the same section followed by more sections in the other.
    for line in text.removesuffix('\n').split('\n'):
        if line.startswith('===== '):
            result.append((name, lines))
            name, lines = line, []
        else:
            lines.append(line)
    result.append((name, lines))
    return result

args = sys.argv[1:]
allowed = []
while '--allow-removed' in args:
    i = args.index('--allow-removed')
    allowed.append(re.compile(args[i + 1]))
    del args[i:i + 2]
allow_appended = '--allow-appended' in args
args = [a for a in args if a != '--allow-appended']
ignored = None
while '--ignore-sections' in args:
    i = args.index('--ignore-sections')
    ignored = re.compile(args[i + 1])
    del args[i:i + 2]
before, after = sections(args[0]), sections(args[1])
ignored_counts = None
if ignored:
    kept_before = [s for s in before if not ignored.search(s[0])]
    kept_after = [s for s in after if not ignored.search(s[0])]
    ignored_counts = (len(before) - len(kept_before), len(after) - len(kept_after))
    before, after = kept_before, kept_after
appended = []
if allow_appended and len(after) > len(before):
    appended = [n for n, _ in after[len(before):]]
    after = after[:len(before)]
real = 0
allowed_count = collections.Counter()
focus_moves = 0
focus_reordered = 0
if [n for n, _ in before] != [n for n, _ in after]:
    print('SECTION LIST DIFFERS')
    real += 1
for index, ((name, b), (_, a)) in enumerate(zip(before, after)):
    if name.startswith('===== focus chain'):
        if b != a:
            focus_moves += 1
        fb = [l for l in b if not l.endswith('fp=0')]
        fa = [l for l in a if not l.endswith('fp=0')]
        fb_allowed = [l for l in fb if not any(r.search(l) for r in allowed)]
        for l in fb:
            if any(r.search(l) for r in allowed):
                allowed_count['[focus chain] ' + l] += 1
        if fb_allowed != fa:
            real += 1
            focus_reordered += 1
            print(f'#{index} {name}: focusable order differs')
            for x, y in zip(fb_allowed, fa):
                if x != y:
                    print('   <', x)
                    print('   >', y)
                    break
        if sorted(l for l in b if l.endswith('fp=0')) != sorted(l for l in a if l.endswith('fp=0')):
            cb = collections.Counter(l for l in b if l.endswith('fp=0'))
            ca = collections.Counter(l for l in a if l.endswith('fp=0'))
            print(f'#{index} {name}: non-focusable entries: removed {dict(cb - ca)} added {dict(ca - cb)}')
        continue
    cb, ca = collections.Counter(b), collections.Counter(a)
    removed, added = cb - ca, ca - cb
    for line, count in removed.items():
        if any(r.search(line) for r in allowed):
            allowed_count[re.sub(r'@-?\d+,-?\d+ \d+x\d+', '@geom', line)] += count
        else:
            real += 1
            print(f'#{index} {name}\n   < {line}')
    for line, count in added.items():
        real += 1
        print(f'#{index} {name}\n   > {line}')
print(f'sections: {len(before)} / {len(after)}')
if allow_appended:
    print(f'appended sections, not compared: {len(appended)}')
if ignored_counts:
    print(f'ignored sections, not compared: {ignored_counts[0]} / {ignored_counts[1]}')
print(f'focus chain sections with moved entries: {focus_moves}')
print(f'focus chain sections with a different focusable order: {focus_reordered}')
print(f'differences outside the focus order: {real - focus_reordered}')
print(f'allowed removed lines: {sum(allowed_count.values())}')
for line, count in allowed_count.most_common():
    print(f'  {count:5d}  {line[:200]}')
print('REAL DIFFERENCES:', real)
sys.exit(1 if real else 0)
