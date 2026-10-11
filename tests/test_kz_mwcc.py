"""Tests for bin/kz-mwcc and bin/kz-mwcc-exec, using tests/fake_mwcc.py as the compiler.

The fake is the EXE and the runner is this interpreter (KZ_MWCC_RUNNER=sys.executable).
test_relative_runner runs the runner from the project root instead.
"""

import importlib.machinery
import importlib.util
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path

import pytest

TESTS = Path(__file__).resolve().parent
BIN = TESTS.parent / "bin"
KZ_MWCC = BIN / "kz-mwcc"
FAKE = TESTS / "fake_mwcc.py"
SOURCES = ("a/foo.cpp", "b/foo.cpp")


@pytest.fixture
def root(tmp_path):
    root = (tmp_path / "root").resolve()
    for rel in ("a", "b", "ps2/include", "build"):
        (root / rel).mkdir(parents=True)
    for rel in SOURCES:
        (root / rel).write_text("int f;\n")
    (root / "ps2/include/common.h").write_text("#define COMMON 1\n")
    (root / "own.h").write_text("int own;\n")
    (root / "private").mkdir()
    (root / "private/win.h").write_text("int win;\n")
    (root / ".git").mkdir()
    (root / "stale.o").write_text("stale\n")
    (root / "stale.d").write_text("stale\n")
    return root


def compile_env(log, **extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("KZ_", "FAKE_MWCC_"))}
    env.update(KZ_MWCC=str(FAKE), KZ_MWCC_RUNNER=shlex.join([sys.executable]),
               FAKE_MWCC_LOG=str(log))
    env.update(extra)
    return env


def command(obj, dep, source, flags=("-c", "-i", "ps2/include", "-lang", "c++")):
    return [str(KZ_MWCC), "--obj", obj, "--depfile", dep, "--", *flags, source]


def run(root, log, obj, dep, source, **extra):
    return subprocess.run(command(obj, dep, source), cwd=root,
                          env=compile_env(log, **extra), capture_output=True, text=True)


def record(log):
    return json.loads(log.read_text())


def root_listing(root):
    return sorted(os.listdir(root))


def test_depfile_rewrite(root, tmp_path):
    log = tmp_path / "log.json"
    proc = run(root, log, "build/a/foo.o", "build/a/foo.d", "a/foo.cpp")
    assert proc.returncode == 0, proc.stderr

    obj = str(root / "build/a/foo.o")
    deps = [root / "a/foo.cpp", root / "ps2/include/common.h", root / "own.h",
            root / "private/win.h"]
    expected = obj + ":" + "".join(" \\\n\t%s" % dep for dep in deps) + "\n"
    assert (root / "build/a/foo.d").read_text() == expected
    assert (root / "build/a/foo.o").read_text() == "object for a/foo.cpp\n"
    assert not (root / "build/a/foo.d.tmp").exists()


def test_cwd_is_private_and_removed(root, tmp_path):
    log = tmp_path / "log.json"
    before = root_listing(root)
    assert run(root, log, "build/a/foo.o", "build/a/foo.d", "a/foo.cpp").returncode == 0

    run_record = record(log)
    cwd = Path(run_record["cwd"])
    assert cwd != root
    assert cwd.name.startswith("kz-mwcc.")
    assert run_record["env_cwd"] == str(cwd)
    # Every top-level entry is symlinked, except .git and *.d / *.o names.
    assert run_record["listing"] == sorted(["a", "b", "build", "own.h", "private", "ps2"])
    assert not cwd.exists()
    assert root_listing(root) == before


def test_concurrent_compiles_get_their_own_depfile(root, tmp_path):
    before = root_listing(root)
    procs = []
    for src in SOURCES:
        name = src.split("/")[0]
        procs.append(subprocess.Popen(
            command("build/%s/foo.o" % name, "build/%s/foo.d" % name, src), cwd=root,
            env=compile_env(tmp_path / ("log-%s.json" % name)),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
    for proc in procs:
        _, err = proc.communicate()
        assert proc.returncode == 0, err

    for name in ("a", "b"):
        other = "b" if name == "a" else "a"
        text = (root / ("build/%s/foo.d" % name)).read_text()
        assert text.startswith("%s/build/%s/foo.o:" % (root, name))
        assert "%s/%s/foo.cpp" % (root, name) in text
        assert "%s/%s/foo.cpp" % (root, other) not in text
        assert (root / ("build/%s/foo.o" % name)).read_text() == \
            "object for %s/foo.cpp\n" % name
    assert root_listing(root) == before
    assert sorted(os.listdir(root / "build")) == ["a", "b"]
    assert sorted(os.listdir(root / "build/a")) == ["foo.d", "foo.o"]


def test_nothing_left_in_root(root, tmp_path):
    before = root_listing(root)
    assert run(root, tmp_path / "log.json", "build/a/foo.o", "build/a/foo.d",
               "a/foo.cpp").returncode == 0
    assert root_listing(root) == before
    assert (root / "stale.o").exists() and (root / "stale.d").exists()


def test_missing_dependency_fails_without_depfile(root, tmp_path):
    (root / "own.h").unlink()
    proc = run(root, tmp_path / "log.json", "build/a/foo.o", "build/a/foo.d", "a/foo.cpp")
    assert proc.returncode == 1
    assert ("kz-mwcc-exec: dependency map names a missing file: %s/own.h "
            "(paths with spaces are not supported)" % root) in proc.stderr
    assert not (root / "build/a/foo.d").exists()
    assert not (root / "build/a/foo.d.tmp").exists()


def test_exit_code_passes_through(root, tmp_path):
    proc = run(root, tmp_path / "log.json", "build/a/foo.o", "build/a/foo.d",
               "a/foo.cpp", FAKE_MWCC_EXIT="3")
    assert proc.returncode == 3
    assert not (root / "build/a/foo.o").exists()
    assert not (root / "build/a/foo.d").exists()
    assert not (root / "build/a/foo.d.tmp").exists()


@pytest.mark.parametrize("mode", ["empty", "none"])
def test_missing_or_empty_output_fails(root, tmp_path, mode):
    proc = run(root, tmp_path / "log.json", "build/a/foo.o", "build/a/foo.d",
               "a/foo.cpp", FAKE_MWCC_OUTPUT=mode)
    assert proc.returncode != 0
    assert "no output" in proc.stderr
    assert not (root / "build/a/foo.o").exists()
    assert not (root / "build/a/foo.d").exists()


def test_depfile_falls_back_to_object_rule(root, tmp_path):
    (root / "build/a").mkdir(exist_ok=True)
    (root / "build/a/foo.d").write_text("stale\n")
    (root / "build/a/foo.d.tmp").write_text("stale\n")
    proc = run(root, tmp_path / "log.json", "build/a/foo.o", "build/a/foo.d",
               "a/foo.cpp", FAKE_MWCC_DEP="none")
    assert proc.returncode == 0, proc.stderr
    assert (root / "build/a/foo.d").read_text() == "%s/build/a/foo.o:\n" % root
    assert not (root / "build/a/foo.d.tmp").exists()


def test_missing_map_fails_when_a_dependency_flag_is_given(root, tmp_path):
    proc = subprocess.run(
        command("build/a/foo.o", "build/a/foo.d", "a/foo.cpp", flags=("-c", "-MD", "-i", "ps2/include")),
        cwd=root, env=compile_env(tmp_path / "log.json", FAKE_MWCC_DEP="none"),
        capture_output=True, text=True)
    assert proc.returncode != 0
    assert "no dependency map" in proc.stderr
    assert not (root / "build/a/foo.o").exists()
    assert not (root / "build/a/foo.d").exists()
    assert not (root / "build/a/foo.d.tmp").exists()


def test_map_with_a_dependency_flag_is_rewritten(root, tmp_path):
    proc = subprocess.run(
        command("build/a/foo.o", "build/a/foo.d", "a/foo.cpp", flags=("-c", "-MMD", "-i", "ps2/include")),
        cwd=root, env=compile_env(tmp_path / "log.json"), capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    assert (root / "build/a/foo.d").read_text().startswith("%s/build/a/foo.o:" % root)


def test_map_rewrite_escapes_space_hash_and_dollar(tmp_path):
    """Emitted target and dependency paths escape space, # and $; input is still split on whitespace."""
    loader = importlib.machinery.SourceFileLoader("kz_mwcc_exec", str(BIN / "kz-mwcc-exec"))
    spec = importlib.util.spec_from_loader("kz_mwcc_exec", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    text = "a.o: src/a.c \\\n  inc/h.h\n"
    out = mod.rewrite_map(text, "/my root/build dir/a#1$x.o", "/my root", [])
    assert out == ("/my\\ root/build\\ dir/a\\#1$$x.o: \\\n\t/my\\ root/src/a.c \\\n\t/my\\ root/inc/h.h\n")


def test_map_named_after_object(root, tmp_path):
    proc = run(root, tmp_path / "log.json", "build/x/bar.o", "build/x/bar.d",
               "a/foo.cpp", FAKE_MWCC_DEP="object")
    assert proc.returncode == 0, proc.stderr
    text = (root / "build/x/bar.d").read_text()
    assert text.startswith("%s/build/x/bar.o:" % root)
    assert "%s/a/foo.cpp" % root in text


def test_relative_runner_and_paths_resolve_from_root(root, tmp_path):
    runner_log = tmp_path / "runner.log"
    runner = root / "tools/runner"
    runner.parent.mkdir()
    runner.write_text(
        "#!%s\n"
        "import os, sys\n"
        "with open(%r, 'a') as f:\n"
        "    f.write(os.getcwd() + '\\n')\n"
        "os.execv(sys.executable, [sys.executable] + sys.argv[1:])\n"
        % (sys.executable, str(runner_log)))
    runner.chmod(0o755)

    proc = run(root, tmp_path / "log.json", "build/a/foo.o", "build/a/foo.d",
               "a/foo.cpp", KZ_MWCC_RUNNER="tools/runner")
    assert proc.returncode == 0, proc.stderr
    ran_in = Path(runner_log.read_text().strip())
    assert ran_in.name.startswith("kz-mwcc.")
    assert (root / "build/a/foo.d").exists()


def test_missing_runner_fails(root, tmp_path):
    proc = run(root, tmp_path / "log.json", "build/a/foo.o", "build/a/foo.d",
               "a/foo.cpp", KZ_MWCC_RUNNER="no-such-runner-kz-test")
    assert proc.returncode != 0
    assert not (root / "build/a/foo.o").exists()


def batch_command(outdir, dep, sources, flags=("-c", "-i", "ps2/include", "-lang", "c++")):
    return [str(KZ_MWCC), "--batch", "--outdir", outdir, "--depfile", dep, "--",
            *flags, *sources]


def run_batch(root, log, sources, flags=("-c", "-i", "ps2/include", "-lang", "c++"), **extra):
    return subprocess.run(batch_command("build/whole", "build/whole/batch.d", sources, flags),
                          cwd=root, env=compile_env(log, **extra), capture_output=True, text=True)


def test_batch_compiles_every_source_in_one_run(root, tmp_path):
    (root / "a/bar.c").write_text("int g;\n")
    log = tmp_path / "log.json"
    before = root_listing(root)
    proc = run_batch(root, log, ["a/foo.cpp", "a/bar.c"])
    assert proc.returncode == 0, proc.stderr

    argv = record(log)["argv"]
    assert argv[-2:] == ["a/foo.cpp", "a/bar.c"]
    assert Path(argv[-3]).parent == root / "build/whole"
    whole = root / "build/whole"
    assert sorted(os.listdir(whole)) == ["bar.c.o", "batch.d", "foo.cpp.o"]
    assert (whole / "foo.cpp.o").read_text() == "object for a/foo.cpp\n"
    assert (whole / "bar.c.o").read_text() == "object for a/bar.c\n"

    # The maps are merged in name order, each header listed once.
    deps = [root / "a/bar.c", root / "ps2/include/common.h", root / "own.h",
            root / "private/win.h", root / "a/foo.cpp"]
    expected = ("%s %s:" % (whole / "foo.cpp.o", whole / "bar.c.o")
                + "".join(" \\\n\t%s" % dep for dep in deps) + "\n")
    assert (whole / "batch.d").read_text() == expected
    assert root_listing(root) == before


def test_batch_options_with_arguments_are_not_sources(root, tmp_path):
    (root / "a/bar.c").write_text("int g;\n")
    log = tmp_path / "log.json"
    flags = ("-c", "-pragma", "divbyzerocheck on", "-strings", "readonly", "-i", "ps2/include")
    assert run_batch(root, log, ["a/foo.cpp", "a/bar.c"], flags).returncode == 0
    argv = record(log)["argv"]
    assert argv[:len(flags) + 1] == ["-c", *flags]
    assert argv[-2:] == ["a/foo.cpp", "a/bar.c"]


def test_batch_rejects_sources_that_share_an_object_name(root, tmp_path):
    log = tmp_path / "log.json"
    proc = run_batch(root, log, list(SOURCES))
    assert proc.returncode == 2
    assert "a/foo.cpp and b/foo.cpp would both compile to foo.o" in proc.stderr
    assert not log.exists()


@pytest.mark.parametrize("args", [
    ["--batch", "--obj", "x.o", "--outdir", "build/w", "--", "-c", "a/foo.cpp"],
    ["--batch", "--depfile", "x.d", "--", "-c", "a/foo.cpp"],
    ["--batch", "--outdir", "build/w", "--", "-c", "-i", "ps2/include"],
])
def test_batch_usage_errors(root, tmp_path, args):
    proc = subprocess.run([str(KZ_MWCC), *args], cwd=root,
                          env=compile_env(tmp_path / "log.json"), capture_output=True, text=True)
    assert proc.returncode == 2
    assert "usage: kz-mwcc" in proc.stderr


@pytest.mark.parametrize("extra, code", [({"FAKE_MWCC_EXIT": "3"}, 3),
                                         ({"FAKE_MWCC_OUTPUT": "none"}, 1),
                                         ({"FAKE_MWCC_OUTPUT": "empty"}, 1)])
def test_batch_leaves_no_object_when_one_is_missing(root, tmp_path, extra, code):
    (root / "a/bar.c").write_text("int g;\n")
    whole = root / "build/whole"
    whole.mkdir(parents=True)
    for name in ("foo.cpp.o", "bar.c.o", "batch.d"):
        (whole / name).write_text("stale\n")
    proc = run_batch(root, tmp_path / "log.json", ["a/foo.cpp", "a/bar.c"], **extra)
    assert proc.returncode == code
    assert os.listdir(whole) == []


def test_batch_missing_maps_fail_when_a_dependency_flag_is_given(root, tmp_path):
    (root / "a/bar.c").write_text("int g;\n")
    proc = run_batch(root, tmp_path / "log.json", ["a/foo.cpp", "a/bar.c"],
                     ("-c", "-MD", "-i", "ps2/include"), FAKE_MWCC_DEP="none")
    assert proc.returncode == 1
    assert "no dependency map" in proc.stderr
    assert os.listdir(root / "build/whole") == []


def test_batch_merges_maps_left_in_the_private_directory(root, tmp_path):
    proc = run_batch(root, tmp_path / "log.json", ["a/foo.cpp"], FAKE_MWCC_DEP="cwd")
    assert proc.returncode == 0, proc.stderr
    whole = root / "build/whole"
    text = (whole / "batch.d").read_text()
    assert text.startswith("%s:" % (whole / "foo.cpp.o"))
    assert "%s/a/foo.cpp" % root in text


def test_batch_without_maps_names_every_object(root, tmp_path):
    (root / "a/bar.c").write_text("int g;\n")
    proc = run_batch(root, tmp_path / "log.json", ["a/foo.cpp", "a/bar.c"],
                     FAKE_MWCC_DEP="none")
    assert proc.returncode == 0, proc.stderr
    whole = root / "build/whole"
    assert (whole / "batch.d").read_text() == "%s %s:\n" % (whole / "foo.cpp.o",
                                                            whole / "bar.c.o")
