"""Join real device observations and human labels for RNF02/RNF03.

python tools/evaluate.py logs/session.jsonl labels.csv
CSV: ms,truth (present/empty/unknown). Label held-out observations only.
Use a single boot per file, because monotonic ms restarts on reboot.
"""
import argparse
import csv
import json
from pathlib import Path
from protocol import decode_event

CLASSES = ("present", "empty", "unknown")


def evaluate(events, labels):
    observations = {}
    boots = 0
    for event in events:
        if event.get("event") == "boot":
            boots += 1
        if event.get("event") == "observation":
            key = int(event["ms"])
            if key in observations:
                raise ValueError("Timestamp repetido: use uma sessão/boot por arquivo")
            observations[key] = event
    if boots > 1:
        raise ValueError("Mais de um boot: separe as sessões antes da avaliação")
    matrix = {a: {b: 0 for b in CLASSES} for a in CLASSES}
    durations, seen = [], set()
    for row in labels:
        key, truth = int(row["ms"]), row["truth"]
        if key in seen or key not in observations or truth not in CLASSES:
            raise ValueError(f"Rótulo inválido, duplicado ou sem observação: {row}")
        seen.add(key)
        event = observations[key]
        if event["label"] not in CLASSES:
            raise ValueError("Classe do firmware desconhecida")
        matrix[truth][event["label"]] += 1
        durations.append(event["duration_ms"])
    totals = {label: sum(matrix[label].values()) for label in CLASSES}
    accuracy = {label: matrix[label][label] / totals[label] if totals[label] else None for label in CLASSES}
    enough = totals["present"] >= 30 and totals["empty"] >= 30 and totals["unknown"] >= 10
    return {"matrix": matrix, "samples": totals, "accuracy_by_class": accuracy,
            "sufficient_samples": enough,
            "classification_target_met": enough and accuracy["present"] >= .9 and accuracy["empty"] >= .9,
            "unknown_classified_empty": matrix["unknown"]["empty"],
            "capture_target_met": bool(durations) and max(durations) <= 2000,
            "max_duration_ms": max(durations) if durations else None,
            "note": "Classificação por imagem; verificar separadamente 10 ciclos com oclusão e 20 ciclos de estabilidade. Não comprova ingestão."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("labels", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with args.log.open("rb") as file:
        events = [event for line in file if (event := decode_event(line))]
    with args.labels.open(encoding="utf-8-sig", newline="") as file:
        report = evaluate(events, list(csv.DictReader(file)))
    text = json.dumps(report, indent=2, ensure_ascii=False)
    print(text)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
