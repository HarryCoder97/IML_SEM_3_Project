import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HISTORY = ROOT / "q_table_history.jsonl"
REPORT = ROOT / "training_report.csv"
OUTPUT = ROOT / "training_progress.png"


def read_history():
    rows = []
    if HISTORY.exists():
        for line in HISTORY.read_text(encoding="utf-8").splitlines():
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return rows


def read_attempts():
    if not REPORT.exists():
        return []
    with REPORT.open(newline="", encoding="utf-8") as report_file:
        return list(csv.DictReader(report_file))


def main():
    try:
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise SystemExit("Install matplotlib first: python -m pip install matplotlib") from error

    history = read_history()
    attempts = read_attempts()
    if not history:
        raise SystemExit("No q_table_history.jsonl yet. Run auto_train.py first.")

    figure, axes = plt.subplots(2, 1, figsize=(11, 8), constrained_layout=True)
    episodes = [row["episode"] for row in history]
    axes[0].plot(episodes, [row["mean_q"] for row in history], label="Mean Q value", color="#1479ff")
    axes[0].plot(episodes, [row["max_q"] for row in history], label="Max Q value", color="#ef5b5b")
    axes[0].set_title("Q-table value progress")
    axes[0].set_xlabel("Training attempts / episodes")
    axes[0].set_ylabel("Q value")
    axes[0].grid(alpha=0.25)
    axes[0].legend()

    if attempts:
        attempt_numbers = [int(row["attempt_no"]) for row in attempts]
        caught = [5 if row["caught"].lower() == "yes" else 0 for row in attempts]
        times = [float(row["time_taken"]) for row in attempts]
        modes = [7.5 if row.get("mode", "a").lower() == "p" else 2.5 for row in attempts]
        axes[1].plot(attempt_numbers, caught, "o-", label="Caught (5=yes, 0=no)", color="#f08a24")
        axes[1].plot(attempt_numbers, times, "o-", label="Time taken (seconds)", color="#22a06b")
        axes[1].plot(attempt_numbers, modes, "o-", label="Mode (player=7.5, auto=2.5)", color="#7b61ff")
        axes[1].set_title("Attempt outcomes")
        axes[1].set_xlabel("Attempt number")
        axes[1].set_ylabel("Outcome / seconds")
        axes[1].grid(alpha=0.25)
        axes[1].legend()
    else:
        axes[1].text(0.5, 0.5, "No completed attempts recorded yet", ha="center", va="center")
        axes[1].set_axis_off()

    figure.suptitle("Cube Tag Training Progress")
    figure.savefig(OUTPUT, dpi=160)
    print(f"Saved graph to {OUTPUT}")
    print(f"Training checkpoints: {len(history)}; attempts: {len(attempts)}")


if __name__ == "__main__":
    main()
