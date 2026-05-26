#!/usr/bin/env python3
"""
capture.py — Descarga automática de imágenes desde ESP32-CAM.

Organiza las imágenes en carpetas por clase para construir el dataset
de Edge Impulse para el clasificador de PCBs (IoT CNC PCB Monitor).

Clases válidas: PCB_Mixta, PCB_SMD, PCB_TH, Sin_PCB

Uso:
    uv run python capture.py --host 192.168.1.XX --class PCB_SMD --count 60
    uv run python capture.py --host 192.168.1.XX --class Sin_PCB --count 60 --delay 1.5

Estructura generada:
    dataset/
    ├── PCB_Mixta/   → PCB_Mixta_20240101_120000_001.jpg ...
    ├── PCB_SMD/     → PCB_SMD_20240101_120100_001.jpg ...
    ├── PCB_TH/      → PCB_TH_20240101_120200_001.jpg ...
    └── Sin_PCB/     → Sin_PCB_20240101_120300_001.jpg ...
"""
from __future__ import annotations

import argparse
import sys
import time
from datetime import datetime
from pathlib import Path

import requests

# ── Constantes ────────────────────────────────────────────────────────────────
VALID_CLASSES     = ("PCB_Mixta", "PCB_SMD", "PCB_TH", "Sin_PCB")
DATASET_ROOT      = Path(__file__).parent / "dataset"
CAPTURE_TIMEOUT_S = 10
PROBE_TIMEOUT_S   = 5


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Captura imágenes desde ESP32-CAM y las organiza por clase.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Ejemplos:\n"
            "  uv run python capture.py --host 192.168.1.50 --class PCB_SMD --count 60\n"
            "  uv run python capture.py --host 192.168.1.50 --class Sin_PCB --count 40 --delay 2\n"
        ),
    )
    parser.add_argument(
        "--host",
        required=True,
        help="IP de la ESP32-CAM (ej: 192.168.1.50)",
    )
    parser.add_argument(
        "--class",
        dest="pcb_class",
        required=True,
        choices=VALID_CLASSES,
        metavar="CLASE",
        help=f"Clase del objeto en la bancada. Opciones: {', '.join(VALID_CLASSES)}",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=60,
        help="Número de imágenes a capturar (default: 60)",
    )
    parser.add_argument(
        "--delay",
        type=float,
        default=1.2,
        help="Segundos entre capturas (default: 1.2). Ajusta según necesites variar el ángulo.",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=80,
        help="Puerto HTTP de la ESP32-CAM (default: 80)",
    )
    return parser.parse_args()


def ensure_class_dir(pcb_class: str) -> Path:
    target = DATASET_ROOT / pcb_class
    target.mkdir(parents=True, exist_ok=True)
    return target


def capture_image(url: str, session: requests.Session) -> bytes:
    resp = session.get(url, timeout=CAPTURE_TIMEOUT_S, stream=False)
    resp.raise_for_status()
    content_type = resp.headers.get("Content-Type", "")
    if "image/jpeg" not in content_type:
        raise ValueError(
            f"Content-Type inesperado: {content_type!r}. "
            "¿El firmware capture_express está corriendo?"
        )
    if len(resp.content) < 1000:
        raise ValueError(
            f"Imagen demasiado pequeña ({len(resp.content)} bytes). "
            "Posible fallo de cámara."
        )
    return resp.content


def format_progress_bar(current: int, total: int, width: int = 28) -> str:
    filled = int(width * current / total)
    bar    = "█" * filled + "░" * (width - filled)
    pct    = int(100 * current / total)
    return f"[{bar}] {pct:3d}%  {current}/{total}"


def main() -> None:
    args    = parse_args()
    base_url = f"http://{args.host}:{args.port}"
    capture_url = f"{base_url}/capture"
    dest    = ensure_class_dir(args.pcb_class)

    print(f"\n{'─'*54}")
    print(f"  CNC PCB Dataset Capture — IoT CNC PCB Monitor")
    print(f"{'─'*54}")
    print(f"  Endpoint  : {capture_url}")
    print(f"  Clase     : {args.pcb_class}")
    print(f"  Destino   : {dest}")
    print(f"  Capturas  : {args.count}")
    print(f"  Delay     : {args.delay}s entre tomas")
    print(f"{'─'*54}\n")

    with requests.Session() as session:
        # Verificar conectividad
        try:
            probe = session.get(base_url, timeout=PROBE_TIMEOUT_S)
            probe.raise_for_status()
            print(f"  ✓ ESP32-CAM responde en {args.host}:{args.port}\n")
        except requests.RequestException as exc:
            print(f"  ✗ No se puede conectar a {args.host}:{args.port}\n    {exc}")
            print(
                "\n  Verifica:\n"
                "    1. La IP es correcta (revisa el Serial Monitor del Arduino IDE).\n"
                "    2. El firmware capture_express está flasheado y en la misma red WiFi.\n"
                "    3. No hay firewall bloqueando el puerto 80.\n"
            )
            sys.exit(1)

        success = 0
        errors  = 0

        for i in range(1, args.count + 1):
            ts       = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = dest / f"{args.pcb_class}_{ts}_{i:04d}.jpg"

            try:
                data = capture_image(capture_url, session)
                filename.write_bytes(data)
                success += 1
                bar = format_progress_bar(success, args.count)
                print(f"\r  {bar}  {filename.name}", end="", flush=True)
            except (requests.RequestException, ValueError, OSError) as exc:
                errors += 1
                print(f"\n  ⚠ Captura {i} falló: {exc}")

            if i < args.count:
                time.sleep(args.delay)

    print(f"\n\n{'─'*54}")
    print(f"  Completado: {success} imágenes guardadas, {errors} errores.")
    print(f"  Directorio: {dest.resolve()}")
    print(f"{'─'*54}\n")

    if errors > 0:
        print(
            "  Consejo: si hubo errores de captura, revisa la iluminación y\n"
            "  asegúrate de que la ESP32-CAM tiene alimentación estable (5 V / 2 A).\n"
        )

    sys.exit(0 if errors == 0 else 1)


if __name__ == "__main__":
    main()
