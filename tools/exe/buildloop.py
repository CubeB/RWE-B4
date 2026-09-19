#!/usr/bin/env python3
"""Replay the three build missions through TA's mission service loop, tick by tick.

This is the transcription behind docs/TOTALA-EXE.md §110, kept so the reading
can be re-run instead of trusted. It answers one question: from the tick a
nanoframe is created (the 0x09 is sent inside 0x485F50) to the tick it finishes
(the 0x12 is sent inside 0x41BA60, via 0x41B8D0), how many ticks pass, for a
factory, a ground constructor and a construction aircraft?

Everything that decides the answer is modelled and nothing else is:

  * the service loop 0x43B7C0, which re-runs the head mission IN THE SAME TICK
    whenever its wake mask is empty or a pending event matches it;
  * the event word unit+0xBA, whose bit 2 (0x4) every COB `set` raises
    (0x480B20, reached from the SET_VALUE opcode at 0x4B1B48) and which nothing
    clears except a mission that waits on it;
  * 0x438700, the INBUILDSTANCE wait (unit+0x10F bit 0), which on a clear stance
    sets the wake mask to 0xA|4 and returns 2;
  * StartBuilding, which 0x438590 only QUEUES (0x4B0B00 with run-now = 0), so the
    script's own `set INBUILDSTANCE to 1` lands in the COB pass, never inside
    the mission that asked for it;
  * the float32 completion fraction of 0x41BA60 (see tools/tad-buildtime.py).

The three missions, as their jump tables lay them out:

  factory   BuildingBuild 0x402640: wait for stance (0x4027CA), THEN create
            (0x4028EA), return 1, build every tick (0x402A09).
  ground    MobileBuild 0x403A20: create (0x403D5B) and start the script, return
            1; wait for stance and RETURN ITS RESULT (0x403DF5); build (0x403E43).
  aircraft  VTOL_MobileBuild 0x413D80: create (0x41409B) and start the script,
            return 1; call the stance wait and IGNORE ITS RESULT, falling
            straight into the build (0x41413E -> 0x414235).

No arguments needed; --build-time and --p pick the job. Prints the
finish-minus-start duration for each builder, with and without a stale bit-2
event pending when the job starts, and under both orders of the COB pass
relative to the mission pass (the corpus cannot see which, and the answer does
not depend on it).
"""

import argparse
import struct


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


class Unit:
    def __init__(self, stale_event, stance):
        self.ba = 0x4 if stale_event else 0  # unit+0xBA
        self.stance = stance  # unit+0x10F bit 0
        self.cob_queue = []  # scripts started with run-now = 0


class Mission:
    def __init__(self, state):
        self.state = state
        self.mask = 0  # mission+0x06
        self.pending = 0  # mission+0x4E
        self.deadline = None  # mission+0x0A


class Job:
    def __init__(self, build_time, p):
        self.x = f32(f32(p) / f32(build_time))
        self.frac = f32(1.0)  # unit+0x104 counts down
        self.created = None
        self.finished = None

    def lathe(self, tick):  # 0x41BA60
        if self.frac == 0.0:
            return
        self.frac = f32(min(1.0, max(0.0, self.frac - self.x)))
        if self.frac == 0.0:
            self.finished = tick  # 0x41B8D0 -> 0x4560C0, the 0x12


def wait_for_stance(unit, mission):  # 0x438700
    if unit.stance:
        return 1
    mission.mask = 0xA | 0x4
    return 2


def set_timer(mission, tick, n):  # 0x439E80
    mission.mask |= 1
    mission.deadline = tick + n


def keep_lathing(unit, mission, job, tick):  # the tail every handler shares
    job.lathe(tick)
    if job.finished is not None:
        return 5
    set_timer(mission, tick, 1)
    mission.mask |= 0xA
    return 2


def create(unit, job, tick):  # 0x485F50 sends the 0x09 synchronously
    job.created = tick
    unit.cob_queue.append("StartBuilding")  # 0x438590


def handler(kind, unit, mission, job, tick):
    s = mission.state
    if kind == "factory":
        if s == 0:
            return wait_for_stance(unit, mission)
        if s == 1:
            create(unit, job, tick)
            return 1
        return keep_lathing(unit, mission, job, tick)
    if kind == "ground":
        if s == 0:
            create(unit, job, tick)
            return 1
        if s == 1:
            return wait_for_stance(unit, mission)
        return keep_lathing(unit, mission, job, tick)
    if kind == "aircraft":
        if s == 0:
            create(unit, job, tick)
            return 1
        wait_for_stance(unit, mission)  # result discarded
        return keep_lathing(unit, mission, job, tick)
    raise ValueError(kind)


def service(kind, unit, mission, job, tick):  # 0x43B7C0
    while True:
        if mission.deadline is not None and tick >= mission.deadline:
            mission.pending |= 1
            mission.deadline = None
        flags = (mission.pending | unit.ba) & mission.mask
        if mission.mask and not flags:
            return True
        unit.ba &= ~flags
        mission.pending &= ~flags
        mission.mask = 0
        ret = handler(kind, unit, mission, job, tick)
        if ret == 1:
            mission.state += 1
        elif ret == 5:
            return False


def cob_pass(unit):
    for script in unit.cob_queue:
        if script == "StartBuilding":  # set INBUILDSTANCE to 1, no sleep
            unit.stance = True
            unit.ba |= 0x4
    unit.cob_queue.clear()


def run(kind, build_time, p, stale_event, cob_first):
    # A factory's stance is already set by the time it builds; a mobile builder's
    # was cleared by its previous StopBuilding.
    unit = Unit(stale_event, stance=(kind == "factory"))
    mission = Mission(0)
    job = Job(build_time, p)
    for tick in range(10 * build_time + 100):
        if cob_first:
            cob_pass(unit)
        if not service(kind, unit, mission, job, tick):
            break
        if not cob_first:
            cob_pass(unit)
    increments = 0
    frac = f32(1.0)
    while frac != 0.0:
        frac = f32(min(1.0, max(0.0, frac - job.x)))
        increments += 1
    return job.finished - job.created, increments


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-time", type=int, default=1130, help="the product's BuildTime (default CORDRAG)")
    ap.add_argument("--p", type=int, default=2, help="the builder's WorkerTime // 30 (default CORCA)")
    args = ap.parse_args()
    print(f"BuildTime {args.build_time}, p {args.p}")
    print(f"  {'builder':<9} {'stale 0x4':<10} {'COB pass':<15} {'duration':>8} {'increments':>11} {'offset':>7}")
    for kind in ("factory", "ground", "aircraft"):
        for stale in (True, False):
            for cob_first in (False, True):
                duration, n = run(kind, args.build_time, args.p, stale, cob_first)
                order = "before missions" if cob_first else "after missions"
                print(f"  {kind:<9} {str(stale):<10} {order:<15} {duration:>8} {n:>11} {duration - (n - 1):>+7}")


if __name__ == "__main__":
    main()
