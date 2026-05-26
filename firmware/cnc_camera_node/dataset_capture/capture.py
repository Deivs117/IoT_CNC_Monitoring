#!/usr/bin/env python3
"""
capture.py — Captura automática de imágenes con previsualización en tiempo real.

Organiza las imágenes en carpetas por clase para construir el dataset
de Edge Impulse para el clasificador de PCBs (IoT CNC PCB Monitor).

Clases válidas: PCB_Mixta, PCB_SMD, PCB_TH, Sin_PCB

Uso:
    uv run python capture.py --host 192.168.1.XX --class PCB_SMD --count 60
    uv run python capture.py --host 192.168.1.XX --class Sin_PCB --count 60 --delay 1.5
    uv run python capture.py --host 192.168.1.XX --class PCB_TH --count 40 --no-preview

Flujo con previsualización (por defecto):
    1. Conecta a la ESP32-CAM y muestra una ventana OpenCV en tiempo real.
    2. Permite verificar orientación, enfoque, encuadre e iluminación.
    3. El usuario presiona ENTER (o 's' en la ventana de preview) para confirmar
       y arrancar la ráfaga, o ESC/Ctrl-C para cancelar.
    4. Durante la ráfaga la ventana sigue mostrando el frame actual.
    5. Al terminar cierra la ventana y muestra el resumen.

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

import numpy as np
import requests

# cv2 is optional — only required when --no-preview is NOT set.
# Import it once at module level so it is not repeated in every function.
try:
    import cv2 as _cv2  # type: ignore[import]
except ImportError:
    _cv2 = None  # type: ignore[assignment]

# ── Constantes ────────────────────────────────────────────────────────────────
VALID_CLASSES     = ("PCB_Mixta", "PCB_SMD", "PCB_TH", "Sin_PCB")
DATASET_ROOT      = Path(__file__).parent / "dataset"
CAPTURE_TIMEOUT_S = 10
PROBE_TIMEOUT_S   = 5

# Teclas en la ventana OpenCV (ASCII / código de tecla)
_KEY_ESC   = 27
_KEY_ENTER = 13
_KEY_S     = ord("s")
_KEY_Q     = ord("q")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Captura imágenes desde ESP32-CAM y las organiza por clase.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Ejemplos:\n"
            "  uv run python capture.py --host 192.168.1.50 --class PCB_SMD --count 60\n"
            "  uv run python capture.py --host 192.168.1.50 --class Sin_PCB --count 40 --delay 2\n"
            "  uv run python capture.py --host 192.168.1.50 --class PCB_TH  --count 40 --no-preview\n"
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
    parser.add_argument(
        "--no-preview",
        dest="no_preview",
        action="store_true",
        help="Desactivar la previsualización OpenCV (útil en entornos sin GUI o headless).",
    )
    return parser.parse_args()


def ensure_class_dir(pcb_class: str) -> Path:
    target = DATASET_ROOT / pcb_class
    target.mkdir(parents=True, exist_ok=True)
    return target


def fetch_jpeg(url: str, session: requests.Session) -> bytes:
    """Descarga un JPEG desde la ESP32-CAM y lo valida."""
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


def jpeg_to_bgr(data: bytes):
    """Convierte bytes JPEG a imagen BGR para OpenCV."""
    arr = np.frombuffer(data, dtype=np.uint8)
    img = _cv2.imdecode(arr, _cv2.IMREAD_COLOR)  # type: ignore[union-attr]
    return img


def run_preview_loop(
    capture_url: str,
    session: requests.Session,
    pcb_class: str,
) -> bool:
    """
    Muestra una ventana de previsualización en tiempo real.

    Retorna True si el usuario confirma con ENTER o 's', False si cancela.
    """
    window = f"Preview — {pcb_class} | ENTER/s = iniciar  ESC/q = cancelar"
    _cv2.namedWindow(window, _cv2.WINDOW_NORMAL)  # type: ignore[union-attr]
    _cv2.resizeWindow(window, 640, 480)  # type: ignore[union-attr]

    print(
        "\n  ── Previsualización activa ────────────────────────────────────\n"
        "  Ajusta orientación, enfoque, encuadre e iluminación.\n"
        "  Cuando estés listo:\n"
        "    • Presiona  ENTER  o  's'  en la ventana  → iniciar captura\n"
        "    • Presiona  ESC   o  'q'                  → cancelar\n"
        "    • O presiona  Ctrl-C  en la terminal       → cancelar\n"
        "  ───────────────────────────────────────────────────────────────\n"
    )

    confirmed = False
    while True:
        try:
            data  = fetch_jpeg(capture_url, session)
            frame = jpeg_to_bgr(data)
        except (requests.RequestException, ValueError) as exc:
            print(f"\r  ⚠ Error de frame en preview: {exc}   ", end="", flush=True)
            time.sleep(0.5)
            # No abandonar el loop; esperar a que la cámara responda
            if _cv2.waitKey(1) & 0xFF in (_KEY_ESC, _KEY_Q):  # type: ignore[union-attr]
                break
            continue

        if frame is not None:
            label = f"Clase: {pcb_class}  |  ENTER/s=OK  ESC/q=Cancelar"
            _cv2.putText(  # type: ignore[union-attr]
                frame, label, (10, 28),
                _cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2, _cv2.LINE_AA,
            )
            _cv2.imshow(window, frame)  # type: ignore[union-attr]

        key = _cv2.waitKey(1) & 0xFF  # type: ignore[union-attr]
        if key in (_KEY_ENTER, _KEY_S):
            confirmed = True
            break
        if key in (_KEY_ESC, _KEY_Q):
            break

    _cv2.destroyAllWindows()  # type: ignore[union-attr]
    return confirmed


def format_progress_bar(current: int, total: int, width: int = 28) -> str:
    filled = int(width * current / total)
    bar    = "█" * filled + "░" * (width - filled)
    pct    = int(100 * current / total)
    return f"[{bar}] {pct:3d}%  {current}/{total}"


def main() -> None:  # noqa: C901
    args        = parse_args()
    base_url    = f"http://{args.host}:{args.port}"
    capture_url = f"{base_url}/capture"
    dest        = ensure_class_dir(args.pcb_class)

    print(f"\n{'─'*54}")
    print(f"  CNC PCB Dataset Capture — IoT CNC PCB Monitor")
    print(f"{'─'*54}")
    print(f"  Endpoint  : {capture_url}")
    print(f"  Clase     : {args.pcb_class}")
    print(f"  Destino   : {dest}")
    print(f"  Capturas  : {args.count}")
    print(f"  Delay     : {args.delay}s entre tomas")
    print(f"  Preview   : {'desactivado (--no-preview)' if args.no_preview else 'OpenCV'}")
    print(f"{'─'*54}\n")

    with requests.Session() as session:
        # ── Verificar conectividad ────────────────────────────────────────────
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

        # ── Previsualización (si está activa) ─────────────────────────────────
        if not args.no_preview and _cv2 is not None:
            try:
                confirmed = run_preview_loop(capture_url, session, args.pcb_class)
            except KeyboardInterrupt:
                print("\n\n  Previsualización cancelada por el usuario.")
                sys.exit(0)

            if not confirmed:
                print("\n  Captura cancelada durante la previsualización.")
                sys.exit(0)

            print("\n  ✓ Previsualización confirmada. Iniciando captura...\n")
        else:
            if not args.no_preview and _cv2 is None:
                print("  ⚠ opencv-python no instalado — continuando sin preview.\n")
            # Sin preview: confirmar por terminal
            print(
                f"  ¿Iniciar la captura de {args.count} imágenes para la clase "
                f"'{args.pcb_class}'?\n"
                "  Presiona ENTER para continuar o Ctrl-C para cancelar... ",
                end="",
                flush=True,
            )
            try:
                input()
            except KeyboardInterrupt:
                print("\n\n  Cancelado.")
                sys.exit(0)

        # ── Ráfaga de captura ─────────────────────────────────────────────────
        success = 0
        errors  = 0

        show_live = not args.no_preview and _cv2 is not None
        if show_live:
            window_burst = f"Capturando {args.pcb_class}…"
            _cv2.namedWindow(window_burst, _cv2.WINDOW_NORMAL)  # type: ignore[union-attr]
            _cv2.resizeWindow(window_burst, 480, 360)  # type: ignore[union-attr]

        for i in range(1, args.count + 1):
            ts       = datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:20]
            filename = dest / f"{args.pcb_class}_{ts}_{i:04d}.jpg"

            try:
                data = fetch_jpeg(capture_url, session)
                filename.write_bytes(data)
                success += 1

                if show_live:
                    frame = jpeg_to_bgr(data)
                    if frame is not None:
                        label = f"{i}/{args.count} — {args.pcb_class}"
                        _cv2.putText(  # type: ignore[union-attr]
                            frame, label, (10, 28),
                            _cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 220, 0), 2, _cv2.LINE_AA,
                        )
                        _cv2.imshow(window_burst, frame)  # type: ignore[union-attr]
                        _cv2.waitKey(1)  # type: ignore[union-attr]

                bar = format_progress_bar(success, args.count)
                print(f"\r  {bar}  {filename.name}", end="", flush=True)
            except (requests.RequestException, ValueError, OSError) as exc:
                errors += 1
                print(f"\n  ⚠ Captura {i} falló: {exc}")

            if i < args.count:
                time.sleep(args.delay)

        if show_live:
            _cv2.destroyAllWindows()  # type: ignore[union-attr]

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
