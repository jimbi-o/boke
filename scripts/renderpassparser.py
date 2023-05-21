import os
import sys
import json
import shadercompiler
        
def register_resource(buffer_name, resource_options):
    data = {}
    data["name"] = buffer_name
    data["format"] = resource_options[buffer_name]["format"] if buffer_name in resource_options and "format" in resource_options[buffer_name] else resource_options["default_format"]
    data["size"] = resource_options[buffer_name]["size"] if buffer_name in resource_options and "size" in resource_options[buffer_name] else resource_options["default_size"]
    data["flags"] = []
    data["pingpong"] = False
    return data

def process_single_resource(buffer_name, buffer_type, resource_options, resource_list):
    if not buffer_name in resource_list:
        resource_list[buffer_name] = register_resource(buffer_name, resource_options)
        resource_list[buffer_name]["initial_flag"] = buffer_type
        if buffer_name == "swapchain":
            resource_list[buffer_name]["initial_flag"] = "present"
    if not buffer_type in resource_list[buffer_name]["flags"]:
        if buffer_name != "swapchain":
            resource_list[buffer_name]["flags"].append(buffer_type)

def iterate_resource(buffer_type, render_pass, resource_options, resource_list):
    if not buffer_type in render_pass:
        return
    for buffer_name in render_pass[buffer_type]:
        process_single_resource(buffer_name, buffer_type, resource_options, resource_list)

def process_resource(buffer_type, render_pass, resource_options, resource_list):
    if buffer_type in render_pass:
        process_single_resource(render_pass[buffer_type], buffer_type, resource_options, resource_list)

def check_pingpong(render_pass, resource_list):
    if not "rtv" in render_pass:
        return
    if not "srv" in render_pass:
        return
    for rtv in render_pass["rtv"]:
        if resource_list[rtv]["pingpong"]:
            continue
        for srv in render_pass["srv"]:
            if not resource_list[srv]["pingpong"] and srv == rtv:
                resource_list[srv]["pingpong"] = True

def configure_resources(render_pass, resource_options, resource_list):
    iterate_resource("rtv", render_pass, resource_options, resource_list)
    iterate_resource("srv", render_pass, resource_options, resource_list)
    process_resource("dsv", render_pass, resource_options, resource_list)
    process_resource("present", render_pass, resource_options, resource_list)
    check_pingpong(render_pass, resource_list)

def get_output_buffer_format(render_pass, resource_list):
    output_buffer_info = {}
    if "rtv" in render_pass:
        rtv_list = []
        for rtv in render_pass["rtv"]:
            rtv_list.append(resource_list[rtv]["format"])
        output_buffer_info["rtv"] = rtv_list
    if "dsv" in render_pass:
        output_buffer_info["dsv"] = resource_list[render_pass["dsv"]]["format"]
    return output_buffer_info

def append_material_if_missing(material, output_buffer_info, material_list):
    name = material["name"] + "_" + format(hash(frozenset(output_buffer_info)) & 0xFFFFFFFFFFFFFFFF, 'x')
    if not name in material_list:
        merged_material = {**material, **output_buffer_info}
        merged_material["name"] = name
        material_list[name] = merged_material
    return name

def configure_material(render_pass, materials, resource_list, material_list):
    if not "material" in render_pass:
        return ""
    material = materials[render_pass["material"]]
    output_buffer_info = get_output_buffer_format(render_pass, resource_list)
    material_name = append_material_if_missing(material, output_buffer_info, material_list)
    return material_name

def parse_render_pass_json(input_json, materials):
    render_pass = []
    resource_list = {}
    material_list = {}
    for input_pass in input_json["render_pass"]:
        configure_resources(input_pass, input_json["resource_options"], resource_list)
        pass_info = input_pass
        pass_info["material"] = configure_material(pass_info, materials, resource_list, material_list)
        render_pass.append(pass_info)
    output = input_json
    del output["resource_options"]
    output["render_pass"] = render_pass
    output["resource"] = list(resource_list.values())
    output["material"] = list(material_list.values())
    return output

def parse_render_pass(input_json, materials):
    with open(input_json) as f:
        data = json.load(f)
    output = parse_render_pass_json(data, materials)
    return output

if __name__ == "__main__":
    input_json = sys.argv[1]
    dxc = sys.argv[2]
    material_json = sys.argv[3]
    shader_root_dir = sys.argv[4]
    materials = shadercompiler.parse_materials(material_json, dxc, shader_root_dir)
    materials = {item["name"]: item for item in materials}
    render_pass = parse_render_pass(input_json, materials)
    print(json.dumps(render_pass, indent=4))
