"""Reproduce M6 before/after using identical harness, probes and compiler.

Only the three circular readers are restored from --base for the baseline;
all musical DSP remains identical. Artifacts are written outside source trees.
"""
import argparse
import os
import pathlib
import shutil
import subprocess


def function_span(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return start, end


def restore_function(current, baseline, signature):
    start, end = function_span(current, signature)
    old_start, old_end = function_span(baseline, signature)
    return current[:start] + baseline[old_start:old_end] + current[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="48f1cf1005cb3ed1edbe1d6219567d8965d99d3e")
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--output", default="build/m6-comparison")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    output = (root / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = root / "src/dsp"
    names = ["cloud_grey_verb.cpp", "cloud_grey_verb.hpp", "dsp_utils.hpp",
             "dsp_safety_probe.hpp", "cloud_grey_verb_granular_continuity_test.cpp",
             "circular_buffer_safety_test.cpp", "cloud_grey_verb_granular_bench.cpp"]
    for version in ("before", "after"):
        folder = output / version
        folder.mkdir(exist_ok=True)
        for name in names:
            shutil.copyfile(source / name, folder / name)
        if version == "before":
            for name, signatures in {
                "cloud_grey_verb.cpp": ["float ShimmerPitcher::readDelay(",
                                        "void CloudGreyVerb::readGrainHead("],
                "dsp_utils.hpp": ["    float read(float delayFrames) const"],
            }.items():
                old = subprocess.check_output(["git", "show", f"{args.base}:src/dsp/{name}"],
                                              cwd=root).decode("utf-8")
                current = (folder / name).read_text(encoding="utf-8")
                for signature in signatures:
                    current = restore_function(current, old, signature)
                if name == "dsp_utils.hpp":
                    # Observe the defective index before accessing the pool.
                    current = current.replace(
                        "        float frac = readPos - static_cast<float>(idx1);",
                        """        float frac = readPos - static_cast<float>(idx1);
#if defined(CGV_DSP_SAFETY_TESTING)
        if (idx1 >= size_ || !(frac >= 0 && frac < 1))
            delayReadProbe = {buffer_, size_, idx1, fDelayed, readPos, frac,
                              delayReadProbe.invalidReads + 1};
#endif""")
                (folder / name).write_text(current, encoding="utf-8")
        flags = ["-O3", "-std=c++17", "-DCLOUD_GREY_PROFILE_DESKTOP_STUDIO=1"]
        for test, harness, extra, command in [
            ("extreme", "cloud_grey_verb_granular_continuity_test.cpp",
             ["-DCGV_DSP_SAFETY_TESTING=1"], [str(folder / "extreme"), "--extreme"]),
            ("circular", "circular_buffer_safety_test.cpp",
             ["-DCGV_M6_BASELINE=1"] if version == "before" else [], []),
        ]:
            exe = folder / (test + (".exe" if os.name == "nt" else ""))
            subprocess.run([args.compiler, *flags, *extra, str(folder / "cloud_grey_verb.cpp"),
                            str(folder / harness), "-o", str(exe)], check=True, cwd=root)
            result = subprocess.run([str(exe), *command], capture_output=True, text=True, cwd=root)
            (folder / (test + ".txt")).write_text(result.stdout + result.stderr, encoding="utf-8")
            expected = 1 if version == "before" else 0
            print(f"{version} {test}: exit={result.returncode}, expected={expected}")
            if result.returncode != expected:
                raise SystemExit("Unexpected regression result; inspect logs")
    print(f"Evidence: {output}")


if __name__ == "__main__":
    main()
