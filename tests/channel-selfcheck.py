#!/usr/bin/env python3
"""Compile the actual channel conversion function, including 6 GHz edges."""
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'iwinfo_nl80211.c').read_text()
function = re.search(r'static int nl80211_freq2channel\(int freq\)\n\{.*?\n\}', source, re.S)
assert function
program = '#include <assert.h>\n' + function[0] + '''
int main(void) {
 assert(nl80211_freq2channel(2412) == 1);
 assert(nl80211_freq2channel(2484) == 14);
 assert(nl80211_freq2channel(5260) == 52);
 assert(nl80211_freq2channel(5935) == 2);
 assert(nl80211_freq2channel(5955) == 1);
 assert(nl80211_freq2channel(7115) == 233);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    binary = str(Path(temp) / 'channels')
    subprocess.run(['cc', '-x', 'c', '-', '-o', binary], input=program, text=True, check=True)
    subprocess.run([binary], check=True)
print('Channel selfcheck passed: 2.4/5/6 GHz and band edges')
