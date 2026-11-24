

# ---- Hokkaido (src/android/banana/pack_tools, appended to Banana Native's vk_android_spv.py at build time) ----
# NMin / NMax / NClamp with Maxwell's NaN results written out as IsNan selects, and float controls that keep NaN and Inf, in every module.
# RADV on Xclipse (Galaxy S26) did not keep GLSL.std.450's NaN rules: the froxel fog compute shaders of Mario Kart World and Ocarina of
# Time wrote NaN that the game's FMNMX / saturate guards remove on the Switch, and bloom spread it over the screen (white picture, white blocks)

import sys

OP_IS_NAN = 156
GLSL_N_MIN, GLSL_N_MAX, GLSL_N_CLAMP = 79, 80, 81
CAP_SZ_INF_NAN_PRESERVE, MODE_SZ_INF_NAN_PRESERVE = 4466, 4461


def nan_exact_minmax(m):
    """Each float NMin / NMax: the other operand when one is NaN; NClamp: its low bound when x is NaN (saturation's 0). → instructions rewritten"""
    glsl = {x[1] for x in m.ins if x[0] & 0xffff == OP_EXT_INST_IMPORT and string_at(x, 2)[0] == 'GLSL.std.450'}

    def wanted(x):
        return (x[0] & 0xffff == OP_EXT_INST and x[3] in glsl and x[4] in (GLSL_N_MIN, GLSL_N_MAX, GLSL_N_CLAMP)
                and m.op(m.scalar_of(x[1])) == OP_TYPE_FLOAT)
    todo = [x for x in m.ins if wanted(x)]
    if not todo: return 0
    b1 = m.add_type(OP_TYPE_BOOL)
    bools = {}
    for x in todo:
        n = m.types[x[1]][3] if m.op(x[1]) == OP_TYPE_VECTOR else 1
        if n not in bools: bools[n] = b1 if n == 1 else m.add_type(OP_TYPE_VECTOR, b1, n)
    out = []
    for x in m.ins:
        if not wanted(x):
            out.append(x)
            continue
        ty, res = x[1], x[2]
        bt = bools[m.types[ty][3] if m.op(ty) == OP_TYPE_VECTOR else 1]
        t = m.new_id()
        out.append([x[0], ty, t, *x[3:]])
        if x[4] == GLSL_N_CLAMP:
            nx = m.new_id()
            out += [[4 << 16 | OP_IS_NAN, bt, nx, x[5]], [6 << 16 | OP_SELECT, ty, res, nx, x[6], t]]
        else:
            a, b = x[5], x[6]
            na, nb, s = m.new_id(), m.new_id(), m.new_id()
            out += [[4 << 16 | OP_IS_NAN, bt, na, a], [4 << 16 | OP_IS_NAN, bt, nb, b],
                    [6 << 16 | OP_SELECT, ty, s, na, b, t], [6 << 16 | OP_SELECT, ty, res, nb, a, s]]
    m.ins = out
    m.index()
    return len(todo)


def nan_inf_preserve(m):
    """SignedZeroInfNanPreserve for the module's 32-bit floats, so the driver neither assumes away NaN / Inf nor folds the IsNan tests"""
    if not m.entry or not any(x[0] & 0xffff == OP_TYPE_FLOAT and x[2] == 32 for x in m.ins): return False
    fn = m.entry[2]
    if any(x[0] & 0xffff == OP_EXECUTION_MODE and x[1] == fn and x[2] == MODE_SZ_INF_NAN_PRESERVE and x[3] == 32 for x in m.ins): return False
    if CAP_SZ_INF_NAN_PRESERVE not in m.caps:
        at = max(k for k, x in enumerate(m.ins) if x[0] & 0xffff == OP_CAPABILITY) + 1
        m.ins.insert(at, [2 << 16 | OP_CAPABILITY, CAP_SZ_INF_NAN_PRESERVE])
        if m.header[1] < 0x00010400 and 'SPV_KHR_float_controls' not in m.exts:
            s = lit_string('SPV_KHR_float_controls')
            m.ins.insert(at + 1, [(1 + len(s)) << 16 | OP_EXTENSION, *s])
    at = max(k for k, x in enumerate(m.ins) if x[0] & 0xffff in (OP_ENTRY_POINT, OP_EXECUTION_MODE)) + 1
    m.ins.insert(at, [4 << 16 | OP_EXECUTION_MODE, fn, MODE_SZ_INF_NAN_PRESERVE, 32])
    m.index()
    return True


def _hk_validator():   # the build's SPIR-V validator (vk_android_build.py VALIDATE: data → None when valid, else the error), None outside a build
    for mod in list(sys.modules.values()):
        f = getattr(mod, 'VALIDATE', None)
        if callable(f): return f
    return None


def _hk_pass(m, fn, check):   # fn applied to m; undone when it raises, or when check says the module it leaves is not valid (it was before)
    saved = [list(x) for x in m.ins]
    try:
        if not fn(m) or check is None: return
        err = check(m.data())
        if err is None: return
        print(f'[RED] Hokkaido {fn.__name__}: the module it left is not valid ({str(err)[:160]}): left out of this module', flush=True)
    except Exception as e:
        print(f'[RED] Hokkaido {fn.__name__}: {e!r}: left out of this module', flush=True)
    m.ins = saved
    m.index()


_banana_precise_minmax = precise_minmax


def precise_minmax(m):
    n = _banana_precise_minmax(m)
    done = m.entry and any(x[0] & 0xffff == OP_EXECUTION_MODE and x[1] == m.entry[2] and x[2] == MODE_SZ_INF_NAN_PRESERVE for x in m.ins)
    if not done:   # (a module this ran on already keeps its selects as they are)
        # Each change is checked by the build's validator when the module is valid before it, and left out of a module it would break, so a shader
        # never fails to build over them (Ocarina of Time's library stopped at 96 % with some shaders not built)
        val = _hk_validator()
        check = val if val is not None and val(m.data()) is None else None
        for fn in (nan_exact_minmax, nan_inf_preserve): _hk_pass(m, fn, check)
    return n
