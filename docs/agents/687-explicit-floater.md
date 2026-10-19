# #687: take Floater only from its FBI key

Authority: owner-approved scheduling in the continuation brief, section B.
Reference: BT-64 in the spec-room findings, byte-tactics revision
`97d7ac59b460417bcdfa629cf90da99f31818c65`, retail GOG v3.1. Static comparison;
no original-game run. The original's yard parser does not set the Floater bit.
The documented census found 22 ships with Floater, 24 sea buildings with water
yard letters, and no overlap. The reader census has medium confidence and does
not prove the absence of further consumers.

Implementation scope: remove the water-yard inference in parseUnitDefinition;
retain the existing assignment from the parsed FBI Floater key. Placement keeps
using the yard letters and WaterLine. No new sim state.

Shipped fixtures inspected read-only using tools/hpi.py, totala1.hpi:

| Unit | Footprint | YardMap | WaterLine | Floater |
| --- | --- | --- | --- | --- |
| ARMSY | 8x8 | wCCCCCCw, six CCCCCCCC rows, wCCCCCCw | 1 | absent |
| CORSY | 8x8 | same | 22 | absent |
| ARMPT | 4x4 | absent; MovementClass BOATS4 | default | 1 |

Consumer review: building creation already takes its separate computeBuildHeight
path, so its height continues to use the water yard and waterline. The torpedo
reach test rejects nonfloaters only above sea level; the shipped yards remain
below it. The mobile height, speed, pitch/roll and factory-product rules can
distinguish a modded mobile water-yard unit whose Floater key is absent. The
definition change is therefore potentially hash-visible and old replay outcomes
can differ for such data. This is conformance, not an RWE policy divergence.

Independent shipped-data census: read units/*.fbi from the installed
totala1.hpi, ccdata.ccx, btdata.ccx and rev31.gp3 in that override order. This
snapshot contains 272 retained file paths (not BT-64's broader 278-file set),
22 explicit floaters and 24 water-yard units, with zero overlap. The relevant
counts corroborate the finding without claiming identical archive populations.
Scratch report: `D:\tmp\rwe-687-census.json`.

Verification: Debug and Release rwe and every Debug target built. Focused parser,
seahold and targeting tests passed: 26 cases, 177 assertions. The selector also
named [waterbuilding], which does not exist; waterbuilding.test.cpp uses [building]
and was included by the full suite. The first test compilation failed on a missing
declaration header for isWater; adding UnitState.h fixed it.

The full permitted suite passed in two parts: 1797 cases / 182925 assertions using
`~*checksum*`, and the five packet/lobby checksum cases / 33 assertions using
explicit positive names. Total: 1802 cases / 182958 assertions. Both prohibited
TaChecksums cases were excluded. Section budget passed over 1049 objects;
peak 24276 / 24575 in GameScene.cpp, unchanged. Logs: `D:\tmp\rwe-687-*`.
No executed original oracle or visual play-test is claimed.
