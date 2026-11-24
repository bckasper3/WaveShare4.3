"""Kasper Backpack desktop case: Waveshare ESP32-S3-Touch-LCD-4.3B + Kasper Backpack carrier.
Units mm. Case frame: X across (centre 0), Y front(0)->back, Z up (base bottom = 0).
Parts: shell (print upside down on its flat top), base (print flat, feet down... flip), clamp x4.
"""
import math, cadquery as cq

# ---------------- parameters ----------------
T = 2.5                 # wall
WI = 140.0              # interior width
W = WI + 2 * T          # 145
H_LIP = 12.0            # front vertical lip height
TH = math.radians(30)   # screen tilt back from vertical
FACE_L = 89.0           # slanted face length along slope
BASE_T = 3.0            # base plate thickness
SO_H = 14.0             # board standoff height above base top (battery space)
BOARD_W, BOARD_D = 131.44, 52.32

# Waveshare (front view, glass coords: u right, v up, from glass centre)
GLASS_W, GLASS_H = 112.4, 75.1
GLASS_S0 = 8.0                          # glass bottom edge position along slope
S_GC = GLASS_S0 + GLASS_H / 2          # glass centre along slope
ACT_W, ACT_H, ACT_DV = 95.54, 54.36, 1.94
WIN_MARGIN = 1.0
PCB_DV = 5.55                           # Waveshare PCB centre above glass centre
HOLE_U, HOLE_V = 46.0, 25.0             # Waveshare PCB holes +-46, +-25 about PCB centre
PCB_BACK = 6.2                          # glass front -> Waveshare PCB back face (estimate)
BOSS_U = 63.0                           # clamp bosses, outside glass
BOSS_K = 10.5                           # boss height from glass front plane
INSERT_D, INSERT_L = 4.2, 6.0           # M3 heat-set insert hole

d = (math.sin(TH), math.cos(TH))        # up-slope (Y,Z)
n_out = (-math.cos(TH), math.sin(TH))
n_in = (math.cos(TH), -math.sin(TH))
P0 = (0.0, BASE_T + H_LIP)              # outer lip top corner (Y,Z) ... lip measured from base top? keep simple
Z_TOP = P0[1] + FACE_L * d[1]
Y_FTOP = P0[0] + FACE_L * d[0]

# back wall: board front edge (y=0) at YB_IN - 0.3
BOARD_FRONT_Y = 44.0                    # case Y of board's rear (header) edge
YB_IN = BOARD_FRONT_Y + BOARD_D + 0.3   # inner back wall face
YB_OUT = YB_IN + T
Z_BOARD = BASE_T + SO_H                 # board bottom
X0 = BOARD_W / 2                        # board x -> X = X0 - x


def bx(x):
    return X0 - x


def by(y):
    return YB_IN - 0.3 - y


# face local plane: x=u (X), y=s (slope), z=w (outward normal); outer surface w=0
FACE = cq.Plane(origin=(0, P0[0], P0[1]), xDir=(1, 0, 0), normal=(0, n_out[0], n_out[1]))


def fw():
    return cq.Workplane(FACE)


def glass_sv(v):
    return S_GC + v


# ---------------- shell ----------------
def make_shell():
    prof = [(0, 0), (0, P0[1]), (Y_FTOP, Z_TOP), (YB_OUT, Z_TOP), (YB_OUT, 0)]
    body = (cq.Workplane("YZ").polyline(prof).close().extrude(W / 2, both=True))
    body = body.edges("|X and >Z").fillet(6)              # top front & top back
    body = body.edges("|X").edges(cq.selectors.NearestToPointSelector((0, 0, P0[1]))).fillet(5)
    body = body.edges("|Z").fillet(5)
    shell = body.faces("<Z").shell(-T)

    # screen window (with outer chamfer)
    sw = glass_sv(ACT_DV)
    win = (fw().workplane(offset=1).center(0, sw)
           .rect(ACT_W + 2 * WIN_MARGIN, ACT_H + 2 * WIN_MARGIN).extrude(-(T + 2)))
    win = win.edges(cq.selectors.ParallelDirSelector(cq.Vector(0, n_out[0], n_out[1]))).fillet(1.5)
    shell = shell.cut(win)
    cham = (fw().workplane(offset=0.01).center(0, sw)
            .rect(ACT_W + 2 * WIN_MARGIN + 3, ACT_H + 2 * WIN_MARGIN + 3)
            .workplane(offset=-1.5).rect(ACT_W + 2 * WIN_MARGIN, ACT_H + 2 * WIN_MARGIN)
            .loft())
    shell = shell.cut(cham)

    # glass locating rim (1.5 tall, 1.2 thick) on inside, glass + 0.3 clearance
    gw, gh = GLASS_W + 0.6, GLASS_H + 0.6
    rim = (fw().workplane(offset=-T + 0.01).center(0, S_GC)
           .rect(gw + 2.4, gh + 2.4).rect(gw, gh).extrude(-1.5))
    # gaps in rim corners make printing / glass insertion easier
    for su in (-1, 1):
        for sv in (-1, 1):
            rim = rim.cut(fw().workplane(offset=-T + 1).center(su * gw / 2, S_GC + sv * gh / 2)
                          .rect(14, 14).extrude(-4))
    shell = shell.union(rim)

    # clamp bosses
    for su in (-1, 1):
        for vv in (PCB_DV + HOLE_V, PCB_DV - HOLE_V):
            boss = (fw().workplane(offset=-T + 0.5).center(su * BOSS_U, glass_sv(vv))
                    .circle(4.0).extrude(-(BOSS_K + 0.5)))
            hole = (fw().workplane(offset=-T - BOSS_K).center(su * BOSS_U, glass_sv(vv))
                    .circle(INSERT_D / 2).extrude(INSERT_L))
            shell = shell.union(boss).cut(hole)

    # base screw bosses (insert from below), Z from BASE_T to BASE_T+9
    for (x, y) in base_screw_xy():
        b = (cq.Workplane("XY").workplane(offset=BASE_T).center(x, y).circle(4.0).extrude(9))
        h = (cq.Workplane("XY").workplane(offset=BASE_T).center(x, y).circle(INSERT_D / 2).extrude(INSERT_L))
        shell = shell.union(b).cut(h)

    # ---- back wall cut-outs (board front edge connectors) ----
    def back_cut(x, z, w, h, r=0.8):
        c = (cq.Workplane("XZ", origin=(0, YB_OUT + 1, 0)).center(x, z).rect(w, h)
             .extrude(T + 3))  # XZ normal is -Y: extrude toward front
        if r > 0:
            c = c.edges("|Y").fillet(r)
        return c

    zb = Z_BOARD + 1.59                                      # board top
    cuts = [
        back_cut(bx(44.196), Z_BOARD + 2.79, 12.6, 7.0, 3.0),     # USBC1 (plug overmold)
        back_cut(bx(56.81), Z_BOARD + 7.09, 9.8, 11.8, 0.8),      # DC1 barrel jack body
        back_cut(bx(80.772), Z_BOARD + 7.84, 20.5, 11.8, 2.0),    # DB1 D-shell
    ]
    for c in cuts:
        shell = shell.cut(c)
    for hx in (68.272, 93.272):                              # DB9 hex jack-screw posts pass through
        shell = shell.cut(cq.Workplane("XZ", origin=(0, YB_OUT + 1, 0))
                          .center(bx(hx), Z_BOARD + 7.84).circle(3.0).extrude(T + 3))
    for lx in (13.589, 24.003, 34.671):                      # LED light-pipe holes (3 mm)
        shell = shell.cut(cq.Workplane("XZ", origin=(0, YB_OUT + 1, 0))
                          .center(bx(lx), zb + 1.6).circle(1.6).extrude(T + 3))
    # rocker switch (KCD1 snap-in, 19.2 x 12.9 cut-out), vertical, above DC jack / U7 side
    shell = shell.cut(back_cut(ROCKER_X, ROCKER_Z, 13.0, 19.3, 0.5))
    return shell


ROCKER_X = 48.0
ROCKER_Z = 58.0


def base_screw_xy():
    xi = WI / 2 - 3.5
    return [(-xi, T + 3.5), (xi, T + 3.5), (0, T + 3.5),
            (-xi, YB_IN - 26), (xi, YB_IN - 26), (0, YB_IN - 4)]


def board_holes_xy():
    return [(bx(x), by(y)) for x in (3.81, 127.635) for y in (4.064, 48.26)]


# ---------------- base ----------------
def make_base():
    cl = 0.3
    w = WI - 2 * cl
    y0, y1 = T + cl, YB_IN - cl
    base = (cq.Workplane("XY").center(0, (y0 + y1) / 2).rect(w, y1 - y0).extrude(BASE_T)
            .edges("|Z").fillet(2.5))
    # board standoffs
    for (x, y) in board_holes_xy():
        s = cq.Workplane("XY").workplane(offset=BASE_T - 0.1).center(x, y).circle(4.0).extrude(SO_H + 0.1)
        h = cq.Workplane("XY").workplane(offset=Z_BOARD - INSERT_L).center(x, y).circle(INSERT_D / 2).extrude(INSERT_L + 1)
        base = base.union(s).cut(h)
    # screw holes to shell bosses, countersunk from bottom (M3 flat head)
    for (x, y) in base_screw_xy():
        base = base.cut(cq.Workplane("XY").workplane(offset=-5).center(x, y).circle(1.7).extrude(BASE_T + 6))
        base = base.cut(cq.Workplane("XY").center(x, y).circle(3.3).workplane(offset=1.7).circle(1.65).loft())
    # feet (with 10 mm recess for optional rubber pads)
    for fx in (-56, 56):
        for fy in (16, YB_IN - 14):
            foot = (cq.Workplane("XY").workplane(offset=0.1).center(fx, fy).circle(7).extrude(-4.1)
                    .faces("<Z").edges().fillet(1.5))
            base = base.union(foot)
            base = base.cut(cq.Workplane("XY").workplane(offset=-4.01).center(fx, fy).circle(5.1).extrude(0.8))
    # battery outline guide (0.4 mm engraved line) under board slot area
    bx0, bx1 = bx(40), bx(6)
    return base


def battery_box():
    # 104050-class pouch 40 x 50 x 10 under the board, between board and base
    x0, x1 = bx(42), bx(8.5)
    y0, y1 = by(51), by(1.5)
    return (cq.Workplane("XY").box(x1 - x0, y1 - y0, 10, centered=False)
            .translate((x0, y0, Z_BOARD - 11.5)))


# ---------------- clamp ----------------
def make_clamp_local():
    """Clamp in face-local coords for the (+u, top) position. Bar k=BOSS_K..BOSS_K+3."""
    L = BOSS_U - HOLE_U
    bar = (cq.Workplane("XY").center((BOSS_U + HOLE_U) / 2, 0).slot2D(L + 9, 9).extrude(3))
    bar = bar.cut(cq.Workplane("XY").center(BOSS_U, 0).circle(1.7).extrude(3))
    pad_len = BOSS_K - PCB_BACK - 0.3
    pad = cq.Workplane("XY").center(HOLE_U, 0).circle(2.6).extrude(-pad_len)
    return bar.union(pad), pad_len


def clamp_in_case(su, vv):
    sv = glass_sv(vv)
    pad_len = BOSS_K - PCB_BACK - 0.3
    cu = su * (BOSS_U + HOLE_U) / 2
    bar = (fw().workplane(offset=-T - BOSS_K).center(cu, sv).slot2D(BOSS_U - HOLE_U + 9, 9).extrude(-3))
    bar = bar.cut(fw().workplane(offset=-T - BOSS_K + 1).center(su * BOSS_U, sv).circle(1.7).extrude(-5))
    pad = fw().workplane(offset=-T - BOSS_K - 0.01).center(su * HOLE_U, sv).circle(2.6).extrude(pad_len)
    return bar.union(pad)


# ---------------- Waveshare dummy for checks ----------------
def waveshare_dummy():
    parts = []
    def blk(u, v, w, h, k0, k1):
        return (fw().workplane(offset=-T - k0).center(u, glass_sv(v)).rect(w, h).extrude(-(k1 - k0)))
    parts.append(blk(0, 0, GLASS_W, GLASS_H, 0, 4.8).edges(cq.selectors.ParallelDirSelector(cq.Vector(0, n_out[0], n_out[1]))).fillet(3.9))      # glass+LCD
    parts.append(blk(0, PCB_DV, 104, 56, 4.8, 9.1))                                   # PCB+parts
    # terminal block (16p 3.5 mm), from PCB left 26.3 (back view) -> front-view u
    tb_u0 = -52 + 26.3 - 2.5
    tb_u1 = tb_u0 + 52.5 + 5
    tb_u = -(tb_u0 + tb_u1) / 2      # mirrored for front view
    parts.append(blk(tb_u, PCB_DV - 28 + 2, 58, 18, 6.0, 17.4))
    s = parts[0]
    for p in parts[1:]:
        s = s.union(p)
    return s


if __name__ == "__main__":
    import sys
    shell = make_shell()
    base = make_base()
    print("shell ok", shell.val().isValid(), "base ok", base.val().isValid())
    print("size W x D x H:", W, round(YB_OUT, 1), round(Z_TOP, 1), " +4 feet")
    cq.exporters.export(shell, "out/shell.step")
    cq.exporters.export(base, "out/base.step")
    clamp, pad_len = make_clamp_local()
    print("pad length", pad_len)
    cq.exporters.export(clamp, "out/clamp.step")
