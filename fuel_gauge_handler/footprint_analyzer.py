import os
import re


def resolve_kicad_path(path, project_dir):
    """Resolve KiCad path variables like ${KIPRJMOD}."""
    if "${KIPRJMOD}" in path:
        path = path.replace("${KIPRJMOD}", project_dir)
    return os.path.normpath(os.path.expandvars(path))


def load_fp_lib_table(path, project_dir=".", include_kicad_defaults=False):
    libs = {}
    # Load from file if available
    if path is not None:
        print(f"File found: {path}")
        if path and os.path.exists(path):
            with open(path, 'r', encoding='utf-8') as f:
                text = f.read()
            # Find all (lib (name "...") (type "...") (uri "...") ...)
            #entries = re.findall(r'\(lib\s+\(name\s+"([^"]+)"\)\s+\(type\s+"([^"]+)"\)\s+\(uri\s+"([^"]+)"\)', text)
            entries = re.findall(r'\(lib\s*\(name\s*"([^"]+)"\)\s*\(type\s*"([^"]+)"\)\s*\(uri\s*"([^"]+)"\)', text)

            print(f"Num entries: {entries}")
            for name, type_, uri in entries:
                resolved_uri = resolve_kicad_path(uri, project_dir)
                libs[name] = {"type": type_, "uri": resolved_uri}
            
        else:
            raise FileNotFoundError(f"Footprint path '{path}' does not exist.")

    # Add KiCad default libraries if requested
    if include_kicad_defaults:
        kicad_fp_dir = os.environ.get("KICAD7_FOOTPRINT_DIR")
        if not kicad_fp_dir:
            # Try common default if env var not set
            kicad_fp_dir = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
        if os.path.exists(kicad_fp_dir):
            for libname in os.listdir(kicad_fp_dir):
                if libname.endswith(".pretty"):
                    name = libname[:-7]  # Strip .pretty
                    libs[name] = {
                        "type": "KiCad-default",
                        "uri": os.path.join(kicad_fp_dir, libname)
                    }
        else:
            raise FileNotFoundError(f"Could not locate KICAD default libraries.")

    return libs


def extract_footprints_with_refs(pcb_file_path):
    with open(pcb_file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Extract each (footprint ...) block
    footprint_blocks = re.findall(r'\(footprint\s+"([^:"]+):([^"]+)"(.*?)\n\s*\)', content, re.DOTALL)

    result = []
    for lib_name, footprint_name, block in footprint_blocks:
        # Look for the (fp_text reference "R1") line
        ref_match = re.search(r'\(fp_text\s+reference\s+"([^"]+)"', block)
        ref = ref_match.group(1) if ref_match else "UNKNOWN"
        result.append({
            "ref": ref,
            "lib": lib_name,
            "footprint": footprint_name
        })
    return result

def analyze(project_dir):
    pcb_file = None
    for file in os.listdir(project_dir):
        if file.endswith(".kicad_pcb"):
            pcb_file = os.path.join(project_dir, file)
            break
    if not pcb_file:
        print("PCB file not found.")
        return

    project_fp_table_path = os.path.join(project_dir, "fp-lib-table")
    global_fp_table_path = None  # Adjust for your version

    project_libs = load_fp_lib_table(project_fp_table_path, project_dir=project_dir)
    global_libs = load_fp_lib_table(global_fp_table_path, include_kicad_defaults=True)

    print ("Project libs = {}".format(project_libs))

    footprint_data = extract_footprints_with_refs(pcb_file)

    print("📦 Footprint Library Usage Report (with References)")
    print("====================================================")

    for fp in sorted(footprint_data, key=lambda x: x["ref"]):
        lib = fp["lib"]
        ref = fp["ref"]
        name = fp["footprint"]

        lib_uri = None
        lib_type = ""
        found_in = ""

        if lib in project_libs:
            raw_uri = project_libs[lib]['uri']
            lib_uri = resolve_kicad_path(raw_uri, project_dir)
            lib_type = "Project-local"
            found_in = "project"
            print(f"project_dir = {project_dir}, lib_uri = {lib_uri}")
            # Ensure it's truly inside the project folder
            proj_abs = os.path.realpath(project_dir)
            lib_abs = os.path.realpath(lib_uri)
            if not os.path.commonpath([proj_abs, lib_abs]) == proj_abs:
                source = f"❌ Listed in project fp-lib-table but NOT inside project folder ({lib_uri})"
                lib_uri = None  # Mark as invalid
        elif lib in global_libs:
            raw_uri = global_libs[lib]['uri']
            lib_uri = resolve_kicad_path(raw_uri, project_dir)
            lib_type = "Global"
            found_in = "global"

        # Check if the footprint file exists (only if we have a valid URI)
        if lib_uri:
            footprint_path = os.path.join(lib_uri, f"{name}.kicad_mod")
            if os.path.isfile(footprint_path):
                source = f"✅ {lib_type} ({lib_uri})"
            else:
                source = f"❌ Footprint file NOT FOUND in {found_in} lib folder ({footprint_path})"
        elif not source:  # Avoid overwriting a previously set error
            source = "❌ Library NOT FOUND in project/global fp-lib-table"

        print(f"{ref:8s}  {lib}:{name:<25s}  →  {source}")

# Run
analyze("./PWM_Converter")
