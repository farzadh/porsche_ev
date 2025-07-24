import os
import re

# --- Utility Functions ---
def parse_lib_table(path):
    libs = {}
    if not os.path.exists(path):
        return libs
    with open(path, 'r') as f:
        text = f.read()

    # Use a more permissive regex that spans across lines
    pattern = re.compile(
        r'\(lib\s*\(\s*name\s*"([^"]+)"\)\s*'
        r'\(\s*type\s*"([^"]+)"\)\s*'
        r'\(\s*uri\s*"([^"]+)"\)', re.DOTALL
    )

    for match in pattern.finditer(text):
        name, typ, uri = match.groups()
        libs[name] = {'type': typ, 'uri': uri}
    return libs


def is_within_project(proj_dir, path):
    proj_abs = os.path.realpath(proj_dir)
    path_abs = os.path.realpath(path)
    return os.path.commonpath([proj_abs, path_abs]) == proj_abs

# --- Symbol Scanner ---
def parse_symbols_from_schematic(sch_path):
    if not os.path.exists(sch_path):
        print(f"[WARN] Schematic file not found: {sch_path}")
        return []
    with open(sch_path, 'r') as f:
        content = f.read()
    pattern = r'\(symbol\s+\(lib_id\s+"([^:"]+):([^"]+)"\)'
    matches = re.findall(pattern, content)
    refs = re.findall(r'\(property\s+"Reference"\s+"([^"]+)"', content)
    print(f"[DEBUG] Parsed {len(matches)} symbols")
    return [{'lib': m[0], 'name': m[1], 'ref': refs[i] if i < len(refs) else 'UNKNOWN'} for i, m in enumerate(matches)]

# --- Symbol Checker ---
def check_symbol(lib, name, ref, project_symlibs, global_symlibs, project_dir):
    libs = {}
    origin = None
    if lib in project_symlibs:
        libs = project_symlibs
        origin = 'project'
    elif lib in global_symlibs:
        libs = global_symlibs
        origin = 'global'
    else:
        # Fallback: Search default global symbols directory
        kicad7_symbol_dir = os.environ.get("KICAD7_SYMBOL_DIR", "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols")
        fallback_path = os.path.join(kicad7_symbol_dir, f"{lib}.kicad_sym")
        if os.path.isfile(fallback_path):
            return f"{ref}\t{lib}:{name}\t✅ Found in default global symbol folder ({fallback_path})"
        else:
            return f"{ref}\t{lib}:{name}\t❌ Symbol library NOT FOUND in lib table or fallback"

    lib_uri = libs[lib]['uri']

    # Handle known env vars
    lib_uri = lib_uri.replace('${KIPRJMOD}', project_dir)
    lib_uri = lib_uri.replace('${KICAD7_SYMBOL_DIR}', os.environ.get("KICAD7_SYMBOL_DIR", "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols"))
    lib_uri = os.path.expandvars(lib_uri)
    lib_uri = os.path.normpath(lib_uri)

    if origin == 'project' and not is_within_project(project_dir, lib_uri):
        return f"{ref}\t{lib}:{name}\t❌ Listed in project table but NOT inside project folder ({lib_uri})"

    if os.path.isfile(lib_uri):
        if origin == 'project':
            # Check inside the file for the symbol only if project library
            if symbol_exists_in_file(name, lib_uri):
                return f"{ref}\t{lib}:{name}\t✅ Found in {origin}-local symbol library ({lib_uri})"
            else:
                return f"{ref}\t{lib}:{name}\t❌ Symbol {name} not found in {origin} library file ({lib_uri})"
        else:
            # For global libs, just confirm file presence
            return f"{ref}\t{lib}:{name}\t✅ Found in {origin} symbol library ({lib_uri})"
    else:
        return f"{ref}\t{lib}:{name}\t❌ Entry found in {origin} table, but file not found at {lib_uri}"

def symbol_exists_in_file(symbol_name, file_path):
    try:
        with open(file_path, "r", encoding="utf-8") as f:
            content = f.read()
        # Strip library prefix from symbol name, if present
        symbol_name_stripped = symbol_name.split(":")[-1]
        return f'(symbol "{symbol_name_stripped}"' in content
    except Exception as e:
        print(f"Error reading symbol file {file_path}: {e}")
        return False




# --- Main ---
def analyze_kicad_symbols(project_dir):
    sch_path = os.path.join(project_dir, f"{os.path.basename(project_dir)}.kicad_sch")
    proj_sym_table = os.path.join(project_dir, "sym-lib-table")

    # Global table (typical KiCad config path)
    kicad_config_dir = os.path.expanduser("~/.config/kicad")
    global_sym_table = os.path.join(kicad_config_dir, "sym-lib-table")

    print("--- Symbol Report ---")
    project_symlibs = parse_lib_table(proj_sym_table)
    global_symlibs = parse_lib_table(global_sym_table)
    symbols = parse_symbols_from_schematic(sch_path)
    for sym in symbols:
        print(check_symbol(sym['lib'], sym['name'], sym['ref'], project_symlibs, global_symlibs, project_dir))

# Example usage:
# analyze_kicad_symbols("/path/to/your/kicad/project")




# Run
analyze_kicad_symbols("./PWM_Converter")
