#!/usr/bin/env python3
"""Build, render and compare the CloudGreyVerb 2x2 and 4x4 FDNs."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import math
import os
from pathlib import Path
import shlex
import subprocess
import sys
from typing import Iterable


NUMERIC_METRICS = (
    "delay_capacity_frames",
    "peak",
    "rms",
    "onset_ms",
    "rt60_seconds",
    "rt60_fit_r2",
    "rt60_low_seconds",
    "rt60_mid_seconds",
    "rt60_high_seconds",
    "tail_level_db",
    "stereo_correlation",
    "side_energy_percent",
    "c80_db",
    "density_percent",
    "energy_l",
    "energy_r",
    "min_safety_gain",
    "max_loop_energy",
)


def parse_args() -> argparse.Namespace:
    script = Path(__file__).resolve()
    repository = script.parents[2]
    parser = argparse.ArgumentParser(
        description=(
            "Compile the same H5 Balanced profile as FDN 2x2 and 4x4, "
            "render deterministic impulse responses and produce a comparison report."
        )
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=repository / "build" / "fdn-ir",
        help="artifact directory (default: build/fdn-ir)",
    )
    parser.add_argument("--seconds", type=float, default=8.0, help="render duration")
    parser.add_argument(
        "--memory-seconds",
        type=float,
        default=3.0,
        help="external DSP memory in mono-equivalent seconds",
    )
    parser.add_argument("--sample-rate", type=int, default=48000)
    parser.add_argument("--preset", default="all", help="factory preset name or all")
    parser.add_argument("--compiler", default="g++", help="C++17 compiler")
    parser.add_argument(
        "--no-wav",
        action="store_true",
        help="calculate metrics without retaining WAV files",
    )
    return parser.parse_args()


def print_command(command: Iterable[str]) -> None:
    print("$", shlex.join(str(part) for part in command), flush=True)


def run(command: list[str], cwd: Path) -> None:
    print_command(command)
    subprocess.run(command, cwd=cwd, check=True)


def build_analyzer(
    compiler: str,
    order: int,
    executable: Path,
    dsp_directory: Path,
) -> None:
    executable.parent.mkdir(parents=True, exist_ok=True)
    command = [
        compiler,
        "-O3",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-DCLOUD_GREY_PROFILE_H5_BALANCED=1",
        "-DCGV_ENABLE_SHIMMER=1",
        f"-DCGV_FDN_ORDER_OVERRIDE={order}",
        str(dsp_directory / "fdn_ir_analyzer.cpp"),
        str(dsp_directory / "cloud_grey_verb.cpp"),
        "-I",
        str(dsp_directory),
        "-o",
        str(executable),
    ]
    run(command, dsp_directory)


def render(
    executable: Path,
    output_directory: Path,
    args: argparse.Namespace,
    dsp_directory: Path,
) -> None:
    output_directory.mkdir(parents=True, exist_ok=True)
    command = [
        str(executable),
        "--output-dir",
        str(output_directory),
        "--seconds",
        str(args.seconds),
        "--memory-seconds",
        str(args.memory_seconds),
        "--sample-rate",
        str(args.sample_rate),
        "--preset",
        args.preset,
    ]
    if args.no_wav:
        command.append("--no-wav")
    run(command, dsp_directory)


def read_metrics(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise RuntimeError(f"No metric rows found in {path}")
    return {row["preset"]: row for row in rows}


def number(row: dict[str, str], key: str) -> float:
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return math.nan


def finite_mean(values: Iterable[float]) -> float:
    valid = [value for value in values if math.isfinite(value)]
    return sum(valid) / len(valid) if valid else math.nan


def format_number(value: float, digits: int = 2) -> str:
    if not math.isfinite(value):
        return "—"
    return f"{value:.{digits}f}"


def format_rt60(row: dict[str, str]) -> str:
    value = number(row, "rt60_seconds")
    if not math.isfinite(value):
        return "—"
    suffix = "*" if row.get("rt60_truncated") == "1" else ""
    return f"{value:.2f}{suffix}"


def arrow(row2: dict[str, str], row4: dict[str, str], key: str, digits: int = 2) -> str:
    return f"{format_number(number(row2, key), digits)} → {format_number(number(row4, key), digits)}"


def write_comparison_csv(
    path: Path,
    metrics2: dict[str, dict[str, str]],
    metrics4: dict[str, dict[str, str]],
    presets: list[str],
) -> None:
    fields = ["preset"]
    for metric in NUMERIC_METRICS:
        fields.extend((f"{metric}_2x2", f"{metric}_4x4", f"{metric}_delta_4x4_minus_2x2"))

    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for preset in presets:
            output: dict[str, str | float] = {"preset": preset}
            for metric in NUMERIC_METRICS:
                value2 = number(metrics2[preset], metric)
                value4 = number(metrics4[preset], metric)
                output[f"{metric}_2x2"] = value2
                output[f"{metric}_4x4"] = value4
                output[f"{metric}_delta_4x4_minus_2x2"] = value4 - value2
            writer.writerow(output)


def write_markdown_report(
    path: Path,
    metrics2: dict[str, dict[str, str]],
    metrics4: dict[str, dict[str, str]],
    presets: list[str],
    args: argparse.Namespace,
) -> None:
    avg_rt2 = finite_mean(number(metrics2[preset], "rt60_seconds") for preset in presets)
    avg_rt4 = finite_mean(number(metrics4[preset], "rt60_seconds") for preset in presets)
    avg_density2 = finite_mean(number(metrics2[preset], "density_percent") for preset in presets)
    avg_density4 = finite_mean(number(metrics4[preset], "density_percent") for preset in presets)
    decorrelated = sum(
        abs(number(metrics4[preset], "stereo_correlation"))
        < abs(number(metrics2[preset], "stereo_correlation"))
        for preset in presets
    )
    denser = sum(
        number(metrics4[preset], "density_percent")
        > number(metrics2[preset], "density_percent")
        for preset in presets
    )
    safety_activations2 = sum(number(metrics2[preset], "min_safety_gain") < 0.999 for preset in presets)
    safety_activations4 = sum(number(metrics4[preset], "min_safety_gain") < 0.999 for preset in presets)
    first_preset = presets[0]
    capacity2 = number(metrics2[first_preset], "delay_capacity_frames")
    capacity4 = number(metrics4[first_preset], "delay_capacity_frames")

    lines = [
        "# Comparativo de respostas ao impulso: FDN 2×2 versus 4×4",
        "",
        f"Gerado em {dt.datetime.now().astimezone().isoformat(timespec='seconds')}.",
        "",
        "## Configuração",
        "",
        "- Perfil: `CLOUD_GREY_PROFILE_H5_BALANCED`.",
        "- Shimmer: habilitado nas duas variantes.",
        "- Excitação: impulso dual-mono normalizado (`0.7071` por canal; energia total unitária).",
        "- Mix: 100% wet; Freeze e Hard Freeze desativados para a medição.",
        f"- Sample rate: {args.sample_rate} Hz.",
        f"- Duração: {args.seconds:g} s por preset.",
        f"- Memória externa: {args.memory_seconds:g} segundos-mono ({round(args.memory_seconds * args.sample_rate)} floats).",
        "- Orçamento de memória idêntico: quatro linhas recebem menos frames por linha que duas linhas.",
        "",
        "## Resumo",
        "",
        f"- RT60 médio estimado: **{format_number(avg_rt2)} s (2×2)** → **{format_number(avg_rt4)} s (4×4)**.",
        f"- Capacidade por linha: **{format_number(capacity2 / args.sample_rate, 3)} s ({format_number(capacity2, 0)} frames)** → **{format_number(capacity4 / args.sample_rate, 3)} s ({format_number(capacity4, 0)} frames)**.",
        f"- Densidade média entre 50–500 ms: **{format_number(avg_density2)}%** → **{format_number(avg_density4)}%**.",
        f"- A 4×4 reduziu a correlação estéreo absoluta em **{decorrelated}/{len(presets)}** presets.",
        f"- A 4×4 aumentou a densidade inicial em **{denser}/{len(presets)}** presets.",
        f"- Safety Guard ativado: **{safety_activations2}** presets na 2×2 e **{safety_activations4}** na 4×4.",
        "- Sob orçamento fixo, a redução de frames por linha é parte estrutural da diferença de RT60; compare também densidade e imagem estéreo antes de retunar `Size`/`Feedback`.",
        "",
        "## Decaimento",
        "",
        "| Preset | Audição | RT60 2×2 | RT60 4×4 | Δ 4−2 | Low RT60 | Mid RT60 | High RT60 | Tail dB |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    for preset in presets:
        row2 = metrics2[preset]
        row4 = metrics4[preset]
        rt2 = number(row2, "rt60_seconds")
        rt4 = number(row4, "rt60_seconds")
        delta = rt4 - rt2 if math.isfinite(rt2) and math.isfinite(rt4) else math.nan
        if args.no_wav:
            audio = "—"
        else:
            audio = f"[2×2](fdn2/{preset}.wav) · [4×4](fdn4/{preset}.wav)"
        lines.append(
            "| "
            + " | ".join(
                (
                    preset,
                    audio,
                    format_rt60(row2),
                    format_rt60(row4),
                    format_number(delta),
                    arrow(row2, row4, "rt60_low_seconds"),
                    arrow(row2, row4, "rt60_mid_seconds"),
                    arrow(row2, row4, "rt60_high_seconds"),
                    arrow(row2, row4, "tail_level_db", 1),
                )
            )
            + " |"
        )

    lines.extend(
        (
            "",
            "## Imagem estéreo e densidade",
            "",
            "| Preset | Correlação 2×2 → 4×4 | Side % 2×2 → 4×4 | Densidade % 2×2 → 4×4 | C80 dB 2×2 → 4×4 | Peak 2×2 → 4×4 | Safety mínimo 2×2 → 4×4 |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
        )
    )
    for preset in presets:
        row2 = metrics2[preset]
        row4 = metrics4[preset]
        lines.append(
            "| "
            + " | ".join(
                (
                    preset,
                    arrow(row2, row4, "stereo_correlation", 3),
                    arrow(row2, row4, "side_energy_percent"),
                    arrow(row2, row4, "density_percent"),
                    arrow(row2, row4, "c80_db"),
                    arrow(row2, row4, "peak", 3),
                    arrow(row2, row4, "min_safety_gain", 3),
                )
            )
            + " |"
        )

    lines.extend(
        (
            "",
            "## Interpretação das métricas",
            "",
            "- RT60 é estimado por regressão da curva de Schroeder entre −5 e −35 dB, com fallback T20/T10.",
            "- Um `*` no RT60 indica que o último bloco de 250 ms ainda está acima de −40 dB; aumente `--seconds` antes de tomar o valor como definitivo.",
            "- Low, Mid e High usam separação aproximada em `<250 Hz`, `250 Hz–2 kHz` e `>2 kHz`.",
            "- Correlação próxima de zero indica maior decorrelação; valores próximos de ±1 indicam imagem mais dependente entre canais.",
            "- Side % mede a parcela de energia lateral M/S. Perto de 50% representa energia Mid/Side equilibrada.",
            "- C80 positivo privilegia energia nos primeiros 80 ms; negativo privilegia a cauda tardia.",
            "",
            "Dados completos: [comparison.csv](comparison.csv), [metrics 2×2](fdn2/metrics.csv) e [metrics 4×4](fdn4/metrics.csv).",
            "",
        )
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    args = parse_args()
    script = Path(__file__).resolve()
    dsp_directory = script.parent
    repository = script.parents[2]
    output = args.output.resolve()
    binary_directory = output / "bin"
    executable_suffix = ".exe" if os.name == "nt" else ""

    analyzer2 = binary_directory / f"fdn_ir_2x2{executable_suffix}"
    analyzer4 = binary_directory / f"fdn_ir_4x4{executable_suffix}"
    output2 = output / "fdn2"
    output4 = output / "fdn4"

    output.mkdir(parents=True, exist_ok=True)
    build_analyzer(args.compiler, 2, analyzer2, dsp_directory)
    build_analyzer(args.compiler, 4, analyzer4, dsp_directory)
    render(analyzer2, output2, args, dsp_directory)
    render(analyzer4, output4, args, dsp_directory)

    metrics2 = read_metrics(output2 / "metrics.csv")
    metrics4 = read_metrics(output4 / "metrics.csv")
    presets = [preset for preset in metrics2 if preset in metrics4]
    if not presets or len(presets) != len(metrics2) or len(presets) != len(metrics4):
        raise RuntimeError("The two analyzers did not produce matching preset sets")

    write_comparison_csv(output / "comparison.csv", metrics2, metrics4, presets)
    write_markdown_report(output / "comparison.md", metrics2, metrics4, presets, args)

    print(f"\nComparison report: {output / 'comparison.md'}")
    print(f"Machine-readable data: {output / 'comparison.csv'}")
    print(f"Repository: {repository}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(f"Command failed with exit code {error.returncode}", file=sys.stderr)
        raise SystemExit(error.returncode)
    except Exception as error:  # keep CLI errors concise
        print(f"run_fdn_ir_analysis: {error}", file=sys.stderr)
        raise SystemExit(1)
