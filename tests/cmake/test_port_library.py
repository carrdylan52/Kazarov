"""Configure checks for cmake/KazarovPort.cmake's kz_port_library, on the fixture in port_library/.

Each test configures two port libraries into one build directory, with stand-ins for the packages
they link and for glslangValidator, and reads what each target generated.
"""

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
FIXTURE = HERE / "port_library"
THIRD_PARTY = ("dr_wav.h", "dr_flac.h", "stb_vorbis.c", "stb_truetype.h", "stb_image.h")


@pytest.fixture(autouse=True)
def require_cmake():
    if shutil.which("cmake") is None or shutil.which("ninja") is None:
        pytest.skip("cmake and ninja are needed")


def configure(tmp_path, *extra):
    third_party = tmp_path / "third_party"
    third_party.mkdir()
    for name in THIRD_PARTY:
        (third_party / name).write_text("")
    build = tmp_path / "build"
    result = subprocess.run(
        ["cmake", "-S", str(FIXTURE), "-B", str(build), "-G", "Ninja",
         "-DGLSLANG_VALIDATOR=" + sys.executable, "-DTHIRD_PARTY=" + str(third_party), *extra],
        capture_output=True, text=True)
    return build, result


def generated(build, target):
    """The target's own generated directory: the include directory that holds app/app_info.hpp."""
    dirs = [Path(d) for d in (build / (target + ".includes")).read_text().split()]
    found = [d for d in dirs if (d / "app" / "app_info.hpp").is_file()]
    assert len(found) == 1, dirs
    return found[0]


def test_each_target_generates_its_own_headers(tmp_path):
    build, result = configure(tmp_path)
    assert result.returncode == 0, result.stdout + result.stderr
    first, second = generated(build, "first"), generated(build, "second")
    assert first != second
    first_info = (first / "app" / "app_info.hpp").read_text()
    second_info = (second / "app" / "app_info.hpp").read_text()
    assert 'kName = "First"' in first_info and '"FIRST_" name' in first_info
    assert 'kName = "Second"' in second_info and '"SECOND_" name' in second_info
    assert "kMinVulkanVersion = 0x403000" in first_info
    assert "kMinVulkanVersion = 0x404000" in second_info
    assert "0x01,0x02,0x03,0x04" in (first / "app" / "icon.hpp").read_text()
    assert "0x05,0x06,0x07,0x08" in (second / "app" / "icon.hpp").read_text()
    first_shaders = set((build / "first.shaders").read_text().split())
    second_shaders = set((build / "second.shaders").read_text().split())
    assert first_shaders and second_shaders and not first_shaders & second_shaders
    assert all(Path(s).parent == first / "shaders" for s in first_shaders)


def test_shaders_of_one_name_are_refused(tmp_path):
    shaders = tmp_path / "shaders"
    shaders.mkdir()
    (shaders / "draw.frag").write_text("")
    build, result = configure(tmp_path, "-DSECOND_SHADERS=" + str(shaders))
    assert result.returncode != 0
    message = " ".join(result.stderr.split())  # CMake wraps a long message
    assert "draw.frag" in message and "would both be" in message
