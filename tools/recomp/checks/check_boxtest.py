"""Check the ported SegmentIntersectsBox against brute force.

The analytic test is Woo's candidate-plane algorithm as ported into CMapObj.cpp. The reference here
is a dumb parametric sweep: walk the segment in small steps and ask whether any sample is inside
the box. The two are independent -- one solves for the entry plane, the other just looks -- so
agreement is evidence rather than a restatement.

Caveat the sweep has and the analytic test does not: a segment that clips a corner between two
samples is missed by brute force. So a "analytic yes, brute force no" on a near-tangent case is the
sweep being wrong, not the port. Those are counted separately and inspected.
"""
import random

SLACK = 9.999999747378752e-06


def ported(bmin, bmax, start, end):
    d = [end[i] - start[i] for i in range(3)]
    t = [-1.0, -1.0, -1.0]
    inside = True

    for i in range(3):
        if bmin[i] <= start[i]:
            if bmax[i] < start[i]:
                if bmax[i] < end[i]:
                    return False
                inside = False
                if d[i] != 0.0:
                    t[i] = (bmax[i] - start[i]) / d[i]
        else:
            if end[i] < bmin[i]:
                return False
            inside = False
            if d[i] != 0.0:
                t[i] = (bmin[i] - start[i]) / d[i]

    if inside:
        return True

    which = 0
    if t[0] < t[1]:
        which = 1
    if t[which] < t[2]:
        which = 2

    if t[which] < 0.0:
        return False

    for i in range(3):
        if i == which:
            continue
        hit = start[i] + t[which] * d[i]
        if bmin[i] - SLACK > hit:
            return False
        if bmax[i] + SLACK < hit:
            return False

    return True


def brute(bmin, bmax, start, end, steps=20000):
    for k in range(steps + 1):
        u = k / steps
        p = [start[i] + (end[i] - start[i]) * u for i in range(3)]
        if all(bmin[i] <= p[i] <= bmax[i] for i in range(3)):
            return True
    return False


def run(n, span, tag):
    agree = bad_miss = bad_hit = 0
    examples = []
    rnd = random.Random(1234)

    for _ in range(n):
        c = [rnd.uniform(-span, span) for _ in range(3)]
        h = [rnd.uniform(0.2, span) for _ in range(3)]
        bmin = [c[i] - h[i] for i in range(3)]
        bmax = [c[i] + h[i] for i in range(3)]

        a = [rnd.uniform(-span * 1.5, span * 1.5) for _ in range(3)]
        b = [rnd.uniform(-span * 1.5, span * 1.5) for _ in range(3)]

        p = ported(bmin, bmax, a, b)
        q = brute(bmin, bmax, a, b)

        if p == q:
            agree += 1
        elif q and not p:
            # Brute force found an interior point the analytic test rejected. This is a REAL defect.
            bad_miss += 1
            if len(examples) < 3:
                examples.append((bmin, bmax, a, b))
        else:
            # Analytic yes, sweep no: either a corner clipped between samples, or a false positive.
            bad_hit += 1

    print('%s: %d cases, agree %d, analytic-rejected-a-real-hit %d, analytic-only %d'
          % (tag, n, agree, bad_miss, bad_hit))
    for e in examples:
        print('   MISS', e)
    return bad_miss


# Hand cases first, where the answer is not in doubt.
cases = [
    # straight through the middle
    (([-1, -1, -1], [1, 1, 1], [-5, 0, 0], [5, 0, 0]), True, 'through x'),
    (([-1, -1, -1], [1, 1, 1], [0, -5, 0], [0, 5, 0]), True, 'through y'),
    (([-1, -1, -1], [1, 1, 1], [0, 0, -5], [0, 0, 5]), True, 'through z'),
    # starts inside
    (([-1, -1, -1], [1, 1, 1], [0, 0, 0], [5, 5, 5]), True, 'from inside'),
    # entirely inside
    (([-1, -1, -1], [1, 1, 1], [-0.5, 0, 0], [0.5, 0, 0]), True, 'wholly inside'),
    # parallel and clear of it
    (([-1, -1, -1], [1, 1, 1], [-5, 3, 0], [5, 3, 0]), False, 'parallel, above'),
    # pointing away
    (([-1, -1, -1], [1, 1, 1], [3, 0, 0], [9, 0, 0]), False, 'away from it'),
    # stops short
    (([-1, -1, -1], [1, 1, 1], [-5, 0, 0], [-2, 0, 0]), False, 'stops short'),
    # diagonal through a corner
    (([-1, -1, -1], [1, 1, 1], [-3, -3, -3], [3, 3, 3]), True, 'body diagonal'),
    # skims past a corner, clearly outside
    (([-1, -1, -1], [1, 1, 1], [-3, 2, 2], [3, 2, 2]), False, 'past a corner'),
    # degenerate: a point inside
    (([-1, -1, -1], [1, 1, 1], [0, 0, 0], [0, 0, 0]), True, 'point inside'),
    # degenerate: a point outside
    (([-1, -1, -1], [1, 1, 1], [5, 0, 0], [5, 0, 0]), False, 'point outside'),
]

print('hand cases')
wrong = 0
for (args, want, name) in cases:
    got = ported(*args)
    ok = 'ok ' if got == want else 'WRONG'
    if got != want:
        wrong += 1
    print('  %s %-16s want %-5s got %s' % (ok, name, want, got))

print()
misses = 0
misses += run(3000, 10.0, 'random, boxes comparable to the segment span')
misses += run(3000, 100.0, 'random, larger span')
print()
print('hand-case failures:', wrong, ' real misses:', misses)
