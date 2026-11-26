#!/usr/bin/env python3
"""Hokkaido's changes to the shader tools of the Banana Native release it embeds (assets/pack/tools.zip), made at build time.

Usage: patch_pack_tools.py <generated banana dir (holds assets/pack)> <nan_exact.py>

nan_exact.py is appended to dm/gfx/tools/vk_android_spv.py. That file is one of the files the Android library version DMSH_VK_CHAIN hashes
(vk_android_build.py tools_digest), and the release's host loads only libraries of its own version, so the new tools digest gets a
vk_android_same.tsv row naming the release's version: the phone's builds stay that version. assets/pack/version.txt changes with the
tools, so a phone unpacks them again. assets/pack/shader_tools.txt gets the new tools digest: what a shader library is made with, which
stays the same across releases whose shader tools are the same (HokkaidoBridge.dropOldShaders). The script fails when the release's
tools do not compute the version its header names (another layout than this script knows), rather than shipping libraries the host
would refuse.
"""
import ast, hashlib, importlib.util, os, re, shutil, sys, tempfile, zipfile

MARK = '# ---- Hokkaido (src/android/banana/pack_tools'


def chain_version(gfx):
    spec = importlib.util.spec_from_file_location('hk_dmsh_chain', os.path.join(gfx, 'tools', 'dmsh_chain.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.version()


def vk_files(gfx):
    tree = ast.parse(open(os.path.join(gfx, 'tools', 'vk_android_build.py')).read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'VK_FILES' for t in node.targets):
            return ast.literal_eval(node.value)
    sys.exit('patch_pack_tools: no VK_FILES in vk_android_build.py')


def tools_digest(gfx, chain):
    h = hashlib.sha256(b'dmsh_chain\0' + chain.encode() + b'\0')
    for f in vk_files(gfx): h.update(f.encode() + b'\0' + open(os.path.join(gfx, f), 'rb').read() + b'\0')
    return h.hexdigest()[:12]


def same_rows(path):
    rows = {}
    for line in open(path):
        if line.startswith('#') or not line.strip(): continue
        digest, version = line.rstrip('\n').split('\t')[:2]
        rows[digest] = version
    return rows


def main():
    gen, addition = sys.argv[1], sys.argv[2]
    pack = os.path.join(gen, 'assets', 'pack')
    zpath, vpath = os.path.join(pack, 'tools.zip'), os.path.join(pack, 'version.txt')
    work = tempfile.mkdtemp(prefix='hk-pack-tools-')
    try:
        with zipfile.ZipFile(zpath) as z:
            entries = [(i, z.read(i.filename)) for i in z.infolist()]
            z.extractall(work)
        gfx = os.path.join(work, 'dm', 'gfx')
        spv, same = os.path.join(gfx, 'tools', 'vk_android_spv.py'), os.path.join(gfx, 'tools', 'vk_android_same.tsv')
        header = open(os.path.join(gfx, 'nvn', 'dmsh_chain_vk.h')).read()
        want = re.search(r'#define DMSH_VK_CHAIN "([0-9a-f]+)"', header).group(1)
        if MARK in open(spv).read(): sys.exit('patch_pack_tools: vk_android_spv.py is patched already')
        chain = chain_version(gfx)
        before = tools_digest(gfx, chain)
        if same_rows(same).get(before, before) != want:
            sys.exit(f'patch_pack_tools: the release tools make library version {same_rows(same).get(before, before)}, its header names {want}')
        with open(spv, 'a') as f: f.write(open(addition).read())
        after = tools_digest(gfx, chain)
        lead = '' if open(same, 'rb').read().endswith(b'\n') else '\n'
        with open(same, 'a') as f: f.write(f'{lead}{after}\t{want}\tHokkaido: NaN-exact min / max / clamp and float controls (src/android/banana/pack_tools), not byte-identical\n')
        if same_rows(same).get(tools_digest(gfx, chain)) != want: sys.exit('patch_pack_tools: the patched tools do not make the release version')
        changed = {'dm/gfx/tools/vk_android_spv.py': spv, 'dm/gfx/tools/vk_android_same.tsv': same}
        tmp = zpath + '.tmp'
        with zipfile.ZipFile(tmp, 'w') as out:
            for info, data in entries:
                if info.filename in changed: data = open(changed[info.filename], 'rb').read()
                out.writestr(info, data)
        os.replace(tmp, zpath)
        old = open(vpath).read().strip()
        new = hashlib.sha256(f'{old}\0{after}'.encode()).hexdigest()[:16]
        open(vpath, 'w').write(new + '\n')
        open(os.path.join(pack, 'shader_tools.txt'), 'w').write(after + '\n')
        print(f'patch_pack_tools: library version {want} (tools {before} -> {after}), pack {old} -> {new}')
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
