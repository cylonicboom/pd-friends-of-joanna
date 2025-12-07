import os
import glob
import subprocess
import sys

# Get the directory of this script
script_dir = os.path.dirname(os.path.abspath(__file__))
tex2png_script = os.path.join(script_dir, 'tex2png.py')
python_exe = sys.executable

# Target directory containing textures
target_dir = "/home/catherine/.local/share/perfectdark-friends-of-joanna/mods/mod_aio/textures/"
output_dir = "/home/catherine/src/pd/perfect-dark-foj/"
files = glob.glob(os.path.join(target_dir, "*.bin"))

print(f"Found {len(files)} files in {target_dir}")
print(f"Outputting to {output_dir}")

for f in files:
    filename = os.path.basename(f)
    output_filename = filename.replace(".bin", ".png")
    output = os.path.join(output_dir, output_filename)
    print(f"Converting {filename} -> {output_filename}")
    try:
        subprocess.run([python_exe, tex2png_script, f, output], check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error converting {f}: {e}")
