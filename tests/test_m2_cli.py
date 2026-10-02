from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

ENGINE = Path(__file__).resolve().parents[1] / "build" / "mm_engine"


@pytest.mark.parametrize("flag", ["--md-latency-ms", "--order-latency-ms"])
@pytest.mark.parametrize(
    "value", ["-1", "abc", "1x", "inf", "nan", "60000.001", "1e308"]
)
def test_latency_cli_rejects_invalid_values_with_flag(flag: str, value: str) -> None:
    result = subprocess.run(
        [str(ENGINE), flag, value, "--md-latency-ms", "0", "--order-latency-ms", "0"],
        input="",
        capture_output=True,
        text=True,
        check=False,
    )

    assert result.returncode == 2
    assert flag in result.stderr
    assert "must be a number from 0 to 60000" in result.stderr


def test_latency_cli_accepts_zero() -> None:
    result = subprocess.run(
        [str(ENGINE), "--md-latency-ms", "0", "--order-latency-ms", "0"],
        input="",
        capture_output=True,
        text=True,
        check=False,
    )

    assert result.returncode == 0, result.stderr
    assert "latency: md 0.000 ms, order 0.000 ms" in result.stderr


@pytest.mark.parametrize("flag", ["--md-latency-ms", "--order-latency-ms"])
def test_latency_cli_accepts_maximum(flag: str) -> None:
    args = [str(ENGINE), flag, "60000"]
    args += ["--order-latency-ms", "0"] if flag == "--md-latency-ms" else ["--md-latency-ms", "0"]
    result = subprocess.run(
        args,
        input="",
        capture_output=True,
        text=True,
        check=False,
    )

    assert result.returncode == 0, result.stderr
    assert "60000.000 ms" in result.stderr
