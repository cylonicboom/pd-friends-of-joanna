
import os

# Constants from constants.h
STAGE_MP_SKEDAR = 0x32
STAGE_MP_PIPES = 0x29
STAGE_MP_RAVINE = 0x03
STAGE_MP_G5BUILDING = 0x20
STAGE_MP_SEWERS = 0x42
STAGE_MP_WAREHOUSE = 0x3c
STAGE_MP_GRID = 0x47
STAGE_MP_RUINS = 0x41
STAGE_MP_AREA52 = 0x3b
STAGE_MP_BASE = 0x39
STAGE_MP_FORTRESS = 0x44
STAGE_MP_VILLA = 0x45
STAGE_MP_CARPARK = 0x3d
STAGE_MP_TEMPLE = 0x25
STAGE_MP_COMPLEX = 0x1f
STAGE_MP_FELICITY = 0x43

STAGE_DEFECTION = 0x30
STAGE_INVESTIGATION = 0x33
STAGE_VILLA = 0x2c
STAGE_CHICAGO = 0x1d
STAGE_G5BUILDING = 0x1e
STAGE_INFILTRATION = 0x2f
STAGE_AIRBASE = 0x27
STAGE_AIRFORCEONE = 0x31
STAGE_CRASHSITE = 0x1c
STAGE_PELAGIC = 0x21
STAGE_DEEPSEA = 0x38
STAGE_DEFENSE = 0x2d
STAGE_ATTACKSHIP = 0x34
STAGE_SKEDARRUINS = 0x2a

STAGE_TEST_MP6 = 0x3e
STAGE_TEST_MP2 = 0x3a
STAGE_EXTRA6 = 0x0b
STAGE_EXTRA2 = 0x06
STAGE_EXTRA8 = 0x0d
STAGE_EXTRA9 = 0x0e
STAGE_EXTRA13 = 0x12
STAGE_EXTRA15 = 0x15
STAGE_EXTRA10 = 0x0f
STAGE_EXTRA11 = 0x10
STAGE_EXTRA4 = 0x08
STAGE_EXTRA12 = 0x11
STAGE_EXTRA14 = 0x13
STAGE_TEST_MP17 = 0x49
STAGE_EXTRA1 = 0x05
STAGE_TEST_SILO = 0x01
STAGE_TEST_MP16 = 0x48
STAGE_TEST_MP14 = 0x46
STAGE_EXTRA3 = 0x07
STAGE_TEST_MP18 = 0x4a
STAGE_EXTRA5 = 0x0a
STAGE_TEST_MP20 = 0x4c
STAGE_TEST_MP19 = 0x4b
STAGE_EXTRA7 = 0x0c
STAGE_TEST_MP8 = 0x40
STAGE_24 = 0x24
STAGE_TEST_MP7 = 0x3f
STAGE_TEST_ARCH = 0x04
STAGE_TEST_DEST = 0x1a
STAGE_EXTRA16 = 0x51
STAGE_EXTRA17 = 0x52
STAGE_EXTRA18 = 0x53
STAGE_EXTRA19 = 0x54
STAGE_EXTRA20 = 0x55
STAGE_EXTRA21 = 0x56
STAGE_EXTRA22 = 0x57
STAGE_EXTRA23 = 0x58
STAGE_EXTRA24 = 0x59
STAGE_EXTRA25 = 0x5a
STAGE_EXTRA26 = 0x5b
STAGE_TEST_LAM = 0x50
STAGE_MP_RANDOM_MULTI = 0x02
STAGE_MP_RANDOM_SOLO = 0x03
STAGE_MP_RANDOM_GEX = 0x04

# Text IDs
L_MPMENU_BASE = 0x5000
L_OPTIONS_BASE = 0x5600

def mpmenu(id):
    return L_MPMENU_BASE + id

def options(id):
    return L_OPTIONS_BASE + id

# Arena list
arenas = [
    (STAGE_MP_RAVINE,       0, mpmenu(121), "Ravine (MP)"),
    (STAGE_MP_G5BUILDING,   0, mpmenu(122), "G5 Building (MP)"),
    (STAGE_MP_SEWERS,       0, mpmenu(123), "Sewers (MP)"),
    (STAGE_MP_WAREHOUSE,    0, mpmenu(124), "Warehouse (MP)"),
    (STAGE_MP_GRID,         0, mpmenu(125), "Grid (MP)"),
    (STAGE_MP_RUINS,        0, mpmenu(126), "Ruins (MP)"),
    (STAGE_MP_AREA52,       0, mpmenu(127), "Area 52 (MP)"),
    (STAGE_MP_BASE,         0, mpmenu(128), "Base (MP)"),
    (STAGE_MP_FORTRESS,     0, mpmenu(130), "Fortress (MP)"),
    (STAGE_MP_VILLA,        0, mpmenu(131), "Villa (MP)"),
    (STAGE_MP_CARPARK,      0, mpmenu(132), "Car Park (MP)"),
    (STAGE_DEFECTION,       0, options(133), "dataDyne Central"),
    (STAGE_INVESTIGATION,   0, options(135), "dataDyne Research"),
    (STAGE_VILLA,           0, options(139), "Carrington Villa"),
    (STAGE_CHICAGO,         0, options(141), "Chicago"),
    (STAGE_G5BUILDING,      0, options(143), "G5 Building"),
    (STAGE_INFILTRATION,    0, options(145), "Area 51"),
    (STAGE_AIRBASE,         0, options(151), "Air Base"),
    (STAGE_AIRFORCEONE,     0, options(153), "Air Force One"),
    (STAGE_CRASHSITE,       0, options(155), "Crash Site"),
    (STAGE_PELAGIC,         0, options(157), "Pelagic II"),
    (STAGE_DEEPSEA,         0, options(159), "Deep Sea"),
    (STAGE_DEFENSE,         0, options(161), "Carrington Institute"),
    (STAGE_ATTACKSHIP,      0, options(163), "Attack Ship"),
    (STAGE_SKEDARRUINS,     0, options(165), "Skedar Ruins"),
    (STAGE_MP_TEMPLE,       0, mpmenu(133), "Temple"),
    (STAGE_MP_COMPLEX,      0, mpmenu(134), "Complex"),
    (STAGE_TEST_MP6,        0, mpmenu(306), "Caves (PD Plus)"),
    (STAGE_TEST_MP2,        0, mpmenu(129), "Stack (PD Plus)"),
    (STAGE_MP_FELICITY,     0, mpmenu(135), "Felicity"),
    # GoldenEye X Mod
    (STAGE_EXTRA6,          0, mpmenu(133), "Temple"),
    (STAGE_EXTRA2,          0, mpmenu(134), "Complex"),
    (STAGE_EXTRA8,          0, mpmenu(306), "Caves"),
    (STAGE_EXTRA9,          0, mpmenu(303), "Library"),
    (STAGE_EXTRA13,         0, mpmenu(302), "Basement"),
    (STAGE_EXTRA15,         0, mpmenu(309), "Stack"),
    (STAGE_EXTRA10,         0, mpmenu(311), "Facility"),
    (STAGE_EXTRA11,         0, mpmenu(300), "Bunker"),
    (STAGE_EXTRA4,          0, mpmenu(299), "Archives"),
    (STAGE_EXTRA12,         0, mpmenu(305), "Caverns"),
    (STAGE_EXTRA14,         0, mpmenu(312), "Egyptian"),
    (STAGE_TEST_MP17,       0, mpmenu(307), "Facility BZ"),
    (STAGE_EXTRA1,          0, mpmenu(298), "Frigate"),
    (STAGE_TEST_SILO,       0, mpmenu(314), "Archives 1F (GE-X 5e)"),
    (STAGE_TEST_MP16,       0, mpmenu(322), "Archives BZ"),
    (STAGE_TEST_MP14,       0, mpmenu(315), "Streets"),
    (STAGE_EXTRA3,          0, mpmenu(310), "Train"),
    (STAGE_TEST_MP18,       0, mpmenu(304), "Cradle"),
    (STAGE_EXTRA5,          0, mpmenu(313), "Aztec"),
    (STAGE_TEST_MP20,       0, mpmenu(308), "Citadel"),
    (STAGE_TEST_MP19,       0, mpmenu(301), "Labyrinth"),
    (STAGE_EXTRA7,          0, mpmenu(316), "Icicle Pyramid"),
    (STAGE_TEST_MP8,        0, mpmenu(323), "Cliff Base"),
    # Bonus
    (STAGE_24,              0, mpmenu(319), "Kakariko Village (Stormy)"),
    (STAGE_TEST_MP7,        0, mpmenu(321), "Dark Noon Mod Valley"),
    (STAGE_TEST_ARCH,       0, mpmenu(324), "Suburb"),
    (STAGE_TEST_DEST,       0, mpmenu(325), "Training Day"),
    (STAGE_EXTRA16,         0, mpmenu(327), "Runway"),
    (STAGE_EXTRA17,         0, mpmenu(328), "Control"),
    (STAGE_EXTRA18,         0, mpmenu(329), "Tawfret Ruins"),
    (STAGE_EXTRA19,         0, mpmenu(330), "Targitzan's Temple"),
    (STAGE_EXTRA20,         0, mpmenu(331), "Junkyard"),
    (STAGE_EXTRA21,         0, mpmenu(332), "Steel Mill"),
    (STAGE_EXTRA22,         0, mpmenu(333), "Mall"),
    (STAGE_EXTRA23,         0, mpmenu(334), "Tunnels"),
    (STAGE_EXTRA24,         0, mpmenu(335), "Rogue"),
    (STAGE_EXTRA25,         0, mpmenu(336), "Paradox"),
    (STAGE_EXTRA26,         0, mpmenu(337), "War Colors"),
    (STAGE_TEST_LAM,        0, mpmenu(338), "Grand Library"),
    # Random
    (STAGE_MP_RANDOM_MULTI, 0, mpmenu(294), "Random Multi"),
    (STAGE_MP_RANDOM_SOLO,  0, mpmenu(295), "Random Solo"),
    (STAGE_MP_RANDOM_GEX,   0, mpmenu(317), "Random GoldenEye X"),
    (1,                     0, mpmenu(136), "Random"),
]

with open("mod_aio_arenas.txt", "w") as f:
    for stage, feature, name, label in arenas:
        f.write("MpArena {\n")
        f.write(f"    stagenum {stage}\n")
        f.write(f"    requirefeature {feature}\n")
        f.write(f"    name {name}\n")
        f.write(f"    label \"{label}\"\n")
        f.write("}\n")

print("Generated mod_aio_arenas.txt")
