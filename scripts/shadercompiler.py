import os
import sys
import hashlib
import json
import subprocess

def compile_shader(dxc, input_path, output_path, target, entry, macros, include_directories, output_debug_path):
    command = [
        dxc,
        input_path,
        "-T", target,
        "-E", entry,
        "-Fo", output_path,
        "/Zi",
        "-HV", "2021",
        "-Fd", output_debug_path
    ]
    for macro in macros:
        command.append("-D" + macro)
    for include_dir in include_directories:
        command.append("-I" + include_dir)
    subprocess.run(command, check=True)

def compile_shaders_in_material(dxc, material, output_extension, include_directories, output_dir, output_dir_for_runtime, debug_output_dir, compiled_shaders):
    json = []
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
        json.append({"target": target.split("_")[0], "output": output_dir_for_runtime + "/" + os.path.basename(output_name)})
    return json

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
    compiled_shaders = set()
    for material in materials["materials"]:
        shader_list = compile_shaders_in_material(dxc, material, output_extension, include_directories, output_dir, output_dir_for_runtime, debug_output_dir, compiled_shaders)
        output_json.append({"name": material["name"], "shader_list": shader_list})

if __name__ == "__main__":
    dxc = sys.argv[1]
    input_json = sys.argv[2]
    output = []
    with open(input_json) as f:
        data = json.load(f)
    compile_materials(dxc, data, output)
    print(json.dumps(output, indent=4))
