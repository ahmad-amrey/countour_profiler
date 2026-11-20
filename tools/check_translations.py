# Checks that every tr() string in src/app has an Arabic entry in src/app/i18n_ar.inc.
# Usage: python tools/check_translations.py .
import glob
import os
import re
import sys

root = sys.argv[1]
lit = r'"((?:[^"\\]|\\.)*)"'
tr_pat = re.compile(r'\btr\(\s*((?:' + lit + r'\s*)+)')
used = []
for f in sorted(glob.glob(os.path.join(root, 'src', 'app', '*.cpp'))):
    src = open(f, encoding='utf-8').read()
    for m in tr_pat.finditer(src):
        s = ''.join(re.findall(lit, m.group(1)))
        if s and s not in used:
            used.append(s)

inc = open(os.path.join(root, 'src', 'app', 'i18n_ar.inc'), encoding='utf-8').read()
keys = set(m.group(1) for m in re.finditer(r'\{' + lit + r'\s*,', inc))
missing = [s for s in used if s not in keys]
unused = [k for k in keys if k not in used]
sys.stdout.reconfigure(encoding='utf-8')
print('tr strings:', len(used), 'translated:', len(used) - len(missing))
for s in missing:
    print('  MISSING:', s)
for s in unused:
    print('  unused key:', s)
sys.exit(1 if missing else 0)
