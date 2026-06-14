def can_build(env, platform):
    return True


import methods
import os
import sys
import shutil

def _apply_core_patches(env):
    import importlib.util

    patches_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "patches.py")
    spec = importlib.util.spec_from_file_location("gdsdecomp_build_patches", patches_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"gdsdecomp: failed to load patches module from {patches_path}")
    patches_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patches_module)
    patches_module.apply_core_patches(env)


# A terrible hack to force-enable our dependent modules being included on non-editor builds.
# etcpak has dependencies our decompressor requires,
# astcenc has the decompression functions.
# without these, we can't decompress either astc or etc textures.
#
# astcenc and etcpak's can_build functions returns False if the editor_build flag is False,
# and it can't be overridden by any flags. Also, Since they come before us in the modules list,
# we can't monkey patch that.
#
# During configure, env.module_list isn't set, and it's not possible to add modules to it.
# sort_module_list is called right after after env.module_list is set with all the modules,
# so we can monkey patch that to add the modules we need.
def monkey_patch_sort_module_list():
    old_sort_module_list = methods.sort_module_list

    def sort_module_list(env):
        if not "etcpak" in env.module_list:
            env.module_list["etcpak"] = "modules/etcpak"
        if not "astcenc" in env.module_list:
            env.module_list["astcenc"] = "modules/astcenc"
            # no need to run configure on etcpak
        if not "tinyexr" in env.module_list:
            env.module_list["tinyexr"] = "modules/tinyexr"
        if not "xatlas_unwrap" in env.module_list:
            env.module_list["xatlas_unwrap"] = "modules/xatlas_unwrap"
        return old_sort_module_list(env)

    methods.sort_module_list = sort_module_list


def configure(env):
    _apply_core_patches(env)
    if not env.editor_build:
        monkey_patch_sort_module_list()

def get_doc_classes():
    return [
        "GDScriptDecomp",
        "Glob",
        "SemVer",
        "GodotVer",
        "GodotREEditorStandalone",
        "ImportExporter",
        "ImportInfo",
        "PckDumper",
        "ImportExporter",
        "ResourceCompatLoader",
        "Exporter",
        "ResourceExporter",
        "CustomDecryptor",
        "AESContextGDRE",
        "CamelliaContext",
        "AriaContext",
        "GDRESettings",
        "GDREConfig",
        "PckCreator",
    ]


def get_doc_path():
    return "doc_classes"
