import subprocess, re

out = subprocess.run(["./lab/probes/probe_hud.exe"], capture_output=True, text=True, errors="replace").stdout

def dump(tag):
    i = out.find(tag)
    if i < 0:
        print("missing:", tag)
        return
    print(out[i:i + 600])

dump("dump 'p'")
print("--- ink ranges ---")
for line in out.splitlines():
    if re.search(r"'p'|'e'|'s'|solid", line):
        print(line)
