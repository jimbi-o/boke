import os
import sys
import hashlib
import json
import subprocess

def create_rootsig_json(rootsig, output_path, output_dir_for_runtime):
    return {"name": rootsig["name"], "file": output_dir_for_runtime + "/" + output_path}

def compile_rootsignature(rootsig, compiled_rootsig, output_dir, output_dir_for_runtime):
    output_path = os.path.splitext(rootsig["file"])[0].replace("/", "_") + rootsig["define"] + ".rs"
    if output_path in compiled_rootsig:
        return create_rootsig_json(rootsig, output_path, output_dir_for_runtime)
    command = [
        dxc,
        rootsig["file"],
        "-T", rootsig["target"],
        "-E", rootsig["define"],
        "-Fo", output_dir + "/" + output_path
    ]
    subprocess.run(command, check=True)
    compiled_rootsig.add(output_path)
    return create_rootsig_json(rootsig, output_path, output_dir_for_runtime)

def compile_shader(dxc, input_path, output_path, target, entry, macros, include_directories, output_debug_path):
    command = [
        dxc,
        input_path,
        "-T", target,
        "-E", entry,
        "-Fo", output_path,
        "/Zi",
        "-HV", "2021",
        "-Fd", output_debug_path,
        "-Qstrip_debug",
        "-Qstrip_reflect",
        "-Qstrip_rootsignature"
    ]
    for macro in macros:
        command.append("-D" + macro)
    for include_dir in include_directories:
        command.append("-I" + include_dir)
    subprocess.run(command, check=True)

def compile_shaders_in_material(dxc, material, output_extension, include_directories, output_dir, output_dir_for_runtime, debug_output_dir, compiled_shaders):
    shader_list = []
    for shader in material.get("shaders", []):
        input_path = shader["input"]
        target = shader["target"]
        entry = shader["entry"]
        macros = shader.get("macros", [])
        hash_string = '{}_{}'.format(input_path, str(shader.get('macros', {})))
        shader_hash = hashlib.md5(hash_string.encode('utf-8')).hexdigest()
        output_name = os.path.join(output_dir, os.path.splitext(os.path.basename(input_path))[0] + shader_hash + "." + output_extension)
        if shader_hash not in compiled_shaders:
            compile_shader(dxc, input_path, output_name, target, entry, macros, include_directories, debug_output_dir)
            compiled_shaders.add(shader_hash)
        shader_list.append({"target": target.split("_")[0], "filename": output_dir_for_runtime + "/" + os.path.basename(output_name)})
    return shader_list

def compile_materials(dxc, materials, output_json):
    output_extension = materials.get("output_extension", "cso")
    include_directories = materials.get("include_directories", [])
    output_dir = materials.get("output_dir", "bin")
    output_dir_for_runtime = materials.get("output_dir_for_runtime", "shaders/bin")
    debug_output_dir = materials.get("debug_output_dir", "bin/debug/")
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    if not os.path.exists(debug_output_dir):
        os.makedirs(debug_output_dir)
    compiled_rootsig = set()
    compiled_shaders = set()
    for material in materials["materials"]:
        rootsig = compile_rootsignature(material["rootsig"], compiled_rootsig, output_dir, output_dir_for_runtime)
        shader_list = compile_shaders_in_material(dxc, material, output_extension, include_directories, output_dir, output_dir_for_runtime, debug_output_dir, compiled_shaders)
        output_material_json = {
            "rootsig": rootsig,
            "shader_list": shader_list
        }
        del material["rootsig"]
        del material["shaders"]
        output_material_json = {**output_material_json, **material}
        output_json.append(output_material_json)

if __name__ == "__main__":
    dxc = sys.argv[1]
    input_json = sys.argv[2]
    output = []
    with open(input_json) as f:
        data = json.load(f)
    compile_materials(dxc, data, output)
    print(json.dumps(output, indent=4))
