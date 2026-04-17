import os
import subprocess

from SCons.Errors import UserError


CORE_PATCHES = [
    {
        "patch": "modules/gdsdecomp/patches/main-cli-parse-inject.patch",
        "target": "main/main.cpp",
        "marker": "gdre::modify_cli_args",
    },
]


def _is_applied(repo_root, target, marker):
    target_path = os.path.join(repo_root, target)
    try:
        with open(target_path, "r", encoding="utf-8") as f:
            return marker in f.read()
    except OSError as e:
        raise UserError(f"gdsdecomp: cannot read {target} to verify patch state: {e}")


def _git_apply(repo_root, patch_rel_path):
    patch_abs_path = os.path.join(repo_root, patch_rel_path)
    if not os.path.isfile(patch_abs_path):
        raise UserError(f"gdsdecomp: patch file not found at {patch_rel_path}")
    result = subprocess.run(
        ["git", "-C", repo_root, "apply", "--whitespace=nowarn", patch_rel_path],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise UserError(
            "gdsdecomp: failed to apply "
            + patch_rel_path
            + "\n"
            + (result.stderr or result.stdout or "<no output>")
        )


def apply_core_patches(env):
    repo_root = env.Dir("#").abspath
    for entry in CORE_PATCHES:
        patch_rel = entry["patch"]
        target = entry["target"]
        marker = entry["marker"]
        patch_name = os.path.basename(patch_rel)
        if _is_applied(repo_root, target, marker):
            print(f"gdsdecomp: {patch_name} already applied to {target}, skipping")
            continue
        _git_apply(repo_root, patch_rel)
        print(f"gdsdecomp: applied {patch_name} to {target}")
