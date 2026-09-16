<<<<<<< HEAD
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TRAINER = ROOT / "train.py"
GAME = ROOT / "game.exe"
REPORT_INTERVAL = 10
RESTART_DELAY = 2


class AutoTraining:
    def __init__(self):
        self.trainer = None
        self.game = None
        self.started_at = time.monotonic()
        self.last_training_report = "waiting for trainer output"
        self.stop_requested = False
        self.mode = "auto"

    def read_trainer_output(self):
        if self.trainer is None or self.trainer.stdout is None:
            return
        for line in self.trainer.stdout:
            message = line.rstrip()
            if message:
                self.last_training_report = message
                print(f"[trainer] {message}", flush=True)

    def start_trainer(self):
        print("[launcher] Starting train.py", flush=True)
        self.trainer = subprocess.Popen(
            [sys.executable, "-u", str(TRAINER)],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        threading.Thread(target=self.read_trainer_output, daemon=True).start()

    def start_game(self):
        if self.mode == "player":
            print("[launcher] Starting player mode: WASD moves, Space jumps", flush=True)
            command = [str(GAME)]
        else:
            print("[launcher] Starting game.exe in autoplay mode", flush=True)
            command = [str(GAME), "--autoplay"]
        self.game = subprocess.Popen(command, cwd=ROOT)

    def stop_process(self, process, name):
        if process is None or process.poll() is not None:
            return
        print(f"[launcher] Stopping {name}", flush=True)
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()

    def print_report(self):
        elapsed = int(time.monotonic() - self.started_at)
        game_status = "running" if self.game and self.game.poll() is None else "stopped"
        trainer_status = "running" if self.trainer and self.trainer.poll() is None else "stopped"
        print(
            f"[report] uptime={elapsed}s trainer={trainer_status} game={game_status} "
            f"last={self.last_training_report}",
            flush=True,
        )

    def run(self):
        if not TRAINER.exists():
            raise FileNotFoundError(TRAINER)
        if not GAME.exists():
            raise FileNotFoundError(f"{GAME} (build the game first)")

        self.start_trainer()
        time.sleep(1)
        self.start_game()
        next_report = time.monotonic() + REPORT_INTERVAL
        try:
            while not self.stop_requested:
                if self.trainer.poll() is not None:
                    print(f"[launcher] train.py exited with code {self.trainer.returncode}", flush=True)
                    break
                if self.game is None or self.game.poll() is not None:
                    if self.game is not None:
                        print(f"[launcher] game.exe exited with code {self.game.returncode}", flush=True)
                    print("[launcher] game.exe closed; stopping training.", flush=True)
                    break
                if time.monotonic() >= next_report:
                    self.print_report()
                    next_report = time.monotonic() + REPORT_INTERVAL
                time.sleep(0.25)
        finally:
            self.stop_process(self.game, "game.exe")
            self.stop_process(self.trainer, "train.py")
            print("[launcher] Automatic training stopped.", flush=True)


def main():
    supervisor = AutoTraining()
    while True:
        choice = input("Choose mode: [P]layer or [A]uto training: ").strip().lower()
        if choice in ("p", "player"):
            supervisor.mode = "player"
            break
        if choice in ("a", "auto", "autoplay"):
            supervisor.mode = "auto"
            break
        print("Please enter P for player mode or A for auto training.")
    try:
        supervisor.run()
    except KeyboardInterrupt:
        print("\n[launcher] Ctrl+C received.", flush=True)
    finally:
        supervisor.stop_process(supervisor.game, "game.exe")
        supervisor.stop_process(supervisor.trainer, "train.py")


if __name__ == "__main__":
    main()
=======
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TRAINER = ROOT / "train.py"
GAME = ROOT / "game.exe"
REPORT_INTERVAL = 10
RESTART_DELAY = 2


class AutoTraining:
    def __init__(self):
        self.trainer = None
        self.game = None
        self.started_at = time.monotonic()
        self.last_training_report = "waiting for trainer output"
        self.stop_requested = False
        self.mode = "auto"

    def read_trainer_output(self):
        if self.trainer is None or self.trainer.stdout is None:
            return
        for line in self.trainer.stdout:
            message = line.rstrip()
            if message:
                self.last_training_report = message
                print(f"[trainer] {message}", flush=True)

    def start_trainer(self):
        print("[launcher] Starting train.py", flush=True)
        self.trainer = subprocess.Popen(
            [sys.executable, "-u", str(TRAINER)],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        threading.Thread(target=self.read_trainer_output, daemon=True).start()

    def start_game(self):
        if self.mode == "player":
            print("[launcher] Starting player mode: WASD moves, Space jumps", flush=True)
            command = [str(GAME)]
        else:
            print("[launcher] Starting game.exe in autoplay mode", flush=True)
            command = [str(GAME), "--autoplay"]
        self.game = subprocess.Popen(command, cwd=ROOT)

    def stop_process(self, process, name):
        if process is None or process.poll() is not None:
            return
        print(f"[launcher] Stopping {name}", flush=True)
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()

    def print_report(self):
        elapsed = int(time.monotonic() - self.started_at)
        game_status = "running" if self.game and self.game.poll() is None else "stopped"
        trainer_status = "running" if self.trainer and self.trainer.poll() is None else "stopped"
        print(
            f"[report] uptime={elapsed}s trainer={trainer_status} game={game_status} "
            f"last={self.last_training_report}",
            flush=True,
        )

    def run(self):
        if not TRAINER.exists():
            raise FileNotFoundError(TRAINER)
        if not GAME.exists():
            raise FileNotFoundError(f"{GAME} (build the game first)")

        self.start_trainer()
        time.sleep(1)
        self.start_game()
        next_report = time.monotonic() + REPORT_INTERVAL
        try:
            while not self.stop_requested:
                if self.trainer.poll() is not None:
                    print(f"[launcher] train.py exited with code {self.trainer.returncode}", flush=True)
                    break
                if self.game is None or self.game.poll() is not None:
                    if self.game is not None:
                        print(f"[launcher] game.exe exited with code {self.game.returncode}", flush=True)
                    print("[launcher] game.exe closed; stopping training.", flush=True)
                    break
                if time.monotonic() >= next_report:
                    self.print_report()
                    next_report = time.monotonic() + REPORT_INTERVAL
                time.sleep(0.25)
        finally:
            self.stop_process(self.game, "game.exe")
            self.stop_process(self.trainer, "train.py")
            print("[launcher] Automatic training stopped.", flush=True)


def main():
    supervisor = AutoTraining()
    while True:
        choice = input("Choose mode: [P]layer or [A]uto training: ").strip().lower()
        if choice in ("p", "player"):
            supervisor.mode = "player"
            break
        if choice in ("a", "auto", "autoplay"):
            supervisor.mode = "auto"
            break
        print("Please enter P for player mode or A for auto training.")
    try:
        supervisor.run()
    except KeyboardInterrupt:
        print("\n[launcher] Ctrl+C received.", flush=True)
    finally:
        supervisor.stop_process(supervisor.game, "game.exe")
        supervisor.stop_process(supervisor.trainer, "train.py")


if __name__ == "__main__":
    main()
>>>>>>> 5e631328c3d7ad6d2e743e2326a19b3e59e14e80
