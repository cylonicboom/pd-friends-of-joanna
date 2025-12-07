import json
import os

mods_dir = "/home/catherine/.local/share/perfectdark-friends-of-joanna/mods/"
output_dir = "/home/catherine/src/pd/perfect-dark-foj/"

mods = [
    "mod_aio",
    "mod_dark_noon",
    "mod_gex",
    "mod_goldfinger_64",
    "mod_kakariko"
]

mod_info = {
    "mod_aio": {
        "name": "All in One",
        "version": "13.0",
        "description": "Perfect Dark Plus & All Solos in Multi Mod",
        "author": "Jonaeru / Atari-Dude",
        "stage_map": {
            "Ump_setupameZ": {"id": "0x30", "name": "Defection"},
            "Ump_setupearZ": {"id": "0x33", "name": "Investigation"},
            "Ump_setupeldZ": {"id": "0x2c", "name": "Villa"},
            "Ump_setuppeteZ": {"id": "0x1d", "name": "Chicago"},
            "Ump_setuplueZ": {"id": "0x2f", "name": "Infiltration"},
            "Ump_setuppamZ": {"id": "0x38", "name": "Deep Sea"},
            "Ump_setupimpZ": {"id": "0x2d", "name": "CI Defense"},
            "Ump_setupleeZ": {"id": "0x34", "name": "Attack Ship"},
            "Ump_setupstatZ": {"id": "0x16", "name": "War"}, # Assuming War based on fojo
            "Ump_setupdamZ": {"name": "Dam"},
            "Ump_setupdepoZ": {"name": "Depot"},
            "Ump_setupdestZ": {"name": "Destroyer"},
            "Ump_setupaztZ": {"name": "Aztec"},
            "Ump_setupcaveZ": {"name": "Caves"},
            "Ump_setuparchZ": {"name": "Archives"},
            "Ump_setupmp2Z": {"name": "Complex"},
            "Ump_setupmp6Z": {"name": "Facility"}
        }
    },
    "mod_dark_noon": {
        "name": "Dark Noon",
        "version": "3.0",
        "description": "Dark Noon Mod for All in One Mod",
        "author": "Jonaeru / Atari-Dude",
        "stage_map": {
            "Ump_setupmp7Z": {"id": "0x3f", "name": "Valley"}
        }
    },
    "mod_gex": {
        "name": "GoldenEye X",
        "version": "10.0",
        "description": "GoldenEye X Multiplayer Mod for All in One Mod",
        "author": "Jonaeru / Atari-Dude",
        "stage_map": {
            "Ump_setupmp1Z": {"name": "Temple"},
            "Ump_setupmp2Z": {"name": "Complex"},
            "Ump_setupmp3Z": {"name": "Caves"},
            "Ump_setupmp4Z": {"name": "Library"},
            "Ump_setupmp5Z": {"name": "Basement"},
            "Ump_setupmp6Z": {"name": "Stack"},
            "Ump_setupmp7Z": {"name": "Facility"},
            "Ump_setupmp8Z": {"name": "Bunker"},
            "Ump_setupmp9Z": {"name": "Archives"},
            "Ump_setupmp10Z": {"name": "Caverns"},
            "Ump_setupmp11Z": {"name": "Egyptian"},
            "Ump_setupmp12Z": {"name": "Facility BZ"},
            "Ump_setupmp13Z": {"name": "Frigate"},
            "Ump_setupmp14Z": {"name": "Archives BZ"},
            "Ump_setupmp15Z": {"name": "Archives 1F"},
            "Ump_setupmp16Z": {"name": "Streets"},
            "Ump_setupmp17Z": {"name": "Train"},
            "Ump_setupmp18Z": {"name": "Cradle"},
            "Ump_setupmp19Z": {"name": "Aztec"},
            "Ump_setupmp20Z": {"name": "Labyrinth"},
            "Ump_setuparkZ": {"name": "Archives"},
            "Ump_setupcradZ": {"name": "Cradle"},
            "Ump_setupcrypZ": {"name": "Crypt"},
            "Ump_setupjunZ": {"name": "Junkyard"},
            "Ump_setuprefZ": {"name": "Refinery"},
            "Ump_setupsiloZ": {"name": "Silo"},
            "Ump_setupstatZ": {"name": "Statue"}
        }
    },
    "mod_goldfinger_64": {
        "name": "Goldfinger 64",
        "version": "1.0",
        "description": "Goldfinger 64 Mod for All in One Mod",
        "author": "Jonaeru / Atari-Dude",
        "stage_map": {
             "Ump_setuparecZ": {"name": "Arecibo/Cradle"},
             "Ump_setupcradZ": {"name": "Cradle"},
             "Ump_setuprefZ": {"name": "Refinery"}
        }
    },
    "mod_kakariko": {
        "name": "Kakariko Village",
        "version": "4.0",
        "description": "Kakariko Village Mod for All in One Mod",
        "author": "Jonaeru / Atari-Dude",
        "stage_map": {
            "Ump_setupmp20Z": {"id": "0x24", "name": "Kakariko Village"},
            "Ump_setuparecZ": {"name": "Arecibo/Cradle"},
            "Ump_setuprefZ": {"name": "Refinery"}
        }
    }
}

for mod in mods:
    files_dir = os.path.join(mods_dir, mod, "files")
    if not os.path.exists(files_dir):
        continue
    
    file_list = []
    for f in os.listdir(files_dir):
        if f.startswith("U") and f.endswith("Z"): # Assuming setup files start with U and end with Z
            file_entry = {
                "name": f,
                "path": f"files/{f}",
                "description": f"Setup file {f}"
            }
            
            if mod in mod_info and f in mod_info[mod]["stage_map"]:
                info = mod_info[mod]["stage_map"][f]
                if "id" in info:
                    file_entry["stageId"] = info["id"]
                if "name" in info:
                    file_entry["stageName"] = info["name"]
            
            file_list.append(file_entry)
    
    json_data = {
        "modName": mod_info[mod]["name"],
        "modVersion": mod_info[mod]["version"],
        "description": mod_info[mod]["description"],
        "author": mod_info[mod]["author"],
        "files": file_list
    }
    
    output_file = os.path.join(output_dir, f"{mod}_filetable.json")
    with open(output_file, 'w') as outfile:
        json.dump(json_data, outfile, indent=2)
    
    print(f"Created {output_file}")

