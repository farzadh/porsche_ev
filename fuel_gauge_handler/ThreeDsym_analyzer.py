import os
import re

def extract_balanced_blocks(text, start_token="(footprint"):
    blocks = []
    pos = 0
    while True:
        start_idx = text.find(start_token, pos)
        if start_idx == -1:
            break

        depth = 0
        idx = start_idx
        while idx < len(text):
            if text[idx] == '(':
                depth += 1
            elif text[idx] == ')':
                depth -= 1
                if depth == 0:
                    idx += 1
                    break
            idx += 1

        block = text[start_idx:idx]
        blocks.append(block)
        pos = idx
    return blocks

def parse_footprint_model_pairs(kicad_pcb_path):
    if not os.path.exists(kicad_pcb_path):
        print(f"[WARN] PCB file not found: {kicad_pcb_path}")
        return []

    with open(kicad_pcb_path, 'r', encoding='utf-8') as f:
        content = f.read()

    footprint_blocks = extract_balanced_blocks(content, "(footprint")

    pairs = []
    for block in footprint_blocks:
        # Extract footprint source name after (footprint "
        footprint_match = re.match(r'\(footprint\s+"([^"]+)"', block)
        footprint_name = footprint_match.group(1) if footprint_match else "UNKNOWN"

        # Extract all model paths inside footprint block
        models = re.findall(r'\(model\s+"([^"]+)"', block)
        for model in models:
            pairs.append({"footprint": footprint_name, "model": model})

    print(f"[DEBUG] Found {len(pairs)} footprint-model pairs")
    return pairs


# --- 3D Model Checker ---
def check_3d_model(model_path, footprint_ref, project_dir):
    original_path = model_path

    if not model_path:
        return "⚠️ No 3D model assigned"

    if "KICAD6_3DMODEL_DIR" in model_path:
        return f"✅ Found 3D model (global): {original_path}"

    # Expand environment variables and KIPRJMOD in model path
    expanded_path = model_path.replace('${KIPRJMOD}', project_dir)
    expanded_path = os.path.expandvars(expanded_path)
    expanded_path = os.path.normpath(expanded_path)

    if os.path.isabs(expanded_path) and os.path.isfile(expanded_path):
        return f"✅ Found 3D model: {original_path}"

    # --- Resolve footprint path from fp-lib-table ---
    fp_lib_table_path = os.path.join(project_dir, "fp-lib-table")
    footprint_lib_paths = {}  # Map footprint library name to absolute path

    if os.path.exists(fp_lib_table_path):
        with open(fp_lib_table_path, 'r', encoding='utf-8') as f:
            for line in f:
                # Extract library folder relative path from uri
                match = re.search(r'\(uri\s+"\$\{KIPRJMOD\}/([^"\)]+)"\)', line)
                if match:
                    lib_rel_path = match.group(1)
                    lib_abs_path = os.path.normpath(os.path.join(project_dir, lib_rel_path))
                    # Extract library name from line
                    name_match = re.search(r'\(lib\s+\(name\s+"([^"]+)"\)', line)
                    lib_name = name_match.group(1) if name_match else None
                    if lib_name:
                        footprint_lib_paths[lib_name] = lib_abs_path

    # The footprint_ref is like "libname:footprintname"
    if ':' in footprint_ref:
        lib_name, fp_name = footprint_ref.split(':', 1)
    else:
        lib_name, fp_name = None, footprint_ref

    fp_path_abs = None
    if lib_name and lib_name in footprint_lib_paths:
        # Construct absolute footprint file path: library folder + fp_name + ".kicad_mod"
        fp_path_abs_candidate = os.path.join(footprint_lib_paths[lib_name], fp_name + ".kicad_mod")
        if os.path.isfile(fp_path_abs_candidate):
            fp_path_abs = fp_path_abs_candidate

    # If footprint absolute path found, try to resolve model path relative to footprint
    if fp_path_abs:
        fp_dir = os.path.dirname(fp_path_abs)
        test_path = os.path.normpath(os.path.join(fp_dir, model_path))
        if os.path.isfile(test_path):
            return f"✅ Found 3D model (relative to footprint file): {original_path}"

    return f"❌ Missing 3D model: {original_path} → Resolved path: {expanded_path}"


# --- Main ---
def analyze_3d_models(project_dir):
    kicad_pcb_path = os.path.join(project_dir, f"{os.path.basename(project_dir)}.kicad_pcb")
    pairs = parse_footprint_model_pairs(kicad_pcb_path)
    print("--- 3D Model Report ---")
    
    for pair in pairs:
        footprint_ref = pair['footprint']
        model_path = pair['model']
        status = check_3d_model(model_path, footprint_ref, project_dir)
        print(f"Footprint: {footprint_ref}, 3D Model: {model_path} -> {status}")

# Run
#analyze_3d_models("./PWM_Converter")
