import json
import math
import random
import socket
from pathlib import Path

HOST = "127.0.0.1"
PORT = 5000
Q_TABLE_PATH = Path(__file__).with_name("q_table.json")
TRAINING_STATE_PATH = Path(__file__).with_name("training_state.json")
BRANCH_LOG_PATH = Path(__file__).with_name("branch_training.jsonl")
Q_HISTORY_PATH = Path(__file__).with_name("q_table_history.jsonl")
ACTIONS = (0, 1, 2, 3, 4, 5)
ACTION_NAMES = {0: "WAIT", 1: "UP", 2: "DOWN", 3: "LEFT", 4: "RIGHT", 5: "JUMP"}


def load_q_table():
    if not Q_TABLE_PATH.exists():
        return {}
    try:
        table = json.loads(Q_TABLE_PATH.read_text(encoding="utf-8"))
        if not isinstance(table, dict):
            return {}
        for key, values in table.items():
            if not isinstance(values, list):
                table[key] = [0.0] * len(ACTIONS)
            elif len(values) < len(ACTIONS):
                table[key] = values + [0.0] * (len(ACTIONS) - len(values))
        return table
    except (OSError, json.JSONDecodeError):
        return {}


def load_training_state():
    if not TRAINING_STATE_PATH.exists():
        return 0.25, 1
    try:
        state = json.loads(TRAINING_STATE_PATH.read_text(encoding="utf-8"))
        return float(state.get("epsilon", 0.25)), int(state.get("episodes", 1))
    except (OSError, json.JSONDecodeError, TypeError, ValueError):
        return 0.25, 1


def save_training(q_table, epsilon, episode, branch_records):
    q_table_temp = Q_TABLE_PATH.with_suffix(".tmp")
    state_temp = TRAINING_STATE_PATH.with_suffix(".tmp")
    q_table_temp.write_text(json.dumps(q_table, indent=2), encoding="utf-8")
    state_temp.write_text(json.dumps({"epsilon": epsilon, "episodes": episode}, indent=2), encoding="utf-8")
    q_table_temp.replace(Q_TABLE_PATH)
    state_temp.replace(TRAINING_STATE_PATH)
    all_values = [value for values in q_table.values() for value in values]
    if all_values:
        with Q_HISTORY_PATH.open("a", encoding="utf-8") as history:
            history.write(json.dumps({
                "episode": episode,
                "states": len(q_table),
                "mean_q": sum(all_values) / len(all_values),
                "max_q": max(all_values),
            }) + "\n")
    if branch_records:
        with BRANCH_LOG_PATH.open("a", encoding="utf-8") as branch_log:
            for record in branch_records:
                branch_log.write(json.dumps(record) + "\n")
        branch_records.clear()


def state_key(state):
    dx = int(round(state["px"] - state["tx"]))
    dz = int(round(state["pz"] - state["tz"]))
    return f"{max(-18, min(18, dx))},{max(-18, min(18, dz))}"


def choose_action(values, epsilon):
    hunting_actions = (1, 2, 3, 4, 5)
    if random.random() < epsilon:
        return random.choice(hunting_actions)
    best_value = max(values[action] for action in hunting_actions)
    best_actions = [action for action in hunting_actions if values[action] == best_value]
    return random.choice(best_actions)


def simulate_branch(state, action):
    next_x = state["tx"]
    next_z = state["tz"]
    if action == 1:
        next_z -= 0.12
    elif action == 2:
        next_z += 0.12
    elif action == 3:
        next_x -= 0.12
    elif action == 4:
        next_x += 0.12
    next_x = max(-8.3, min(8.3, next_x))
    next_z = max(-8.3, min(8.3, next_z))
    next_distance = math.hypot(state["px"] - next_x, state["pz"] - next_z)
    return next_x, next_z, next_distance


def train_connection(connection):
    q_table = load_q_table()
    epsilon, episode = load_training_state()
    steps = 0
    receive_buffer = ""
    branch_records = []

    try:
        with connection:
            while True:
                packet = connection.recv(4096)
                if not packet:
                    break
                receive_buffer += packet.decode("utf-8")
                lines = receive_buffer.split("\n")
                receive_buffer = lines.pop()

                for raw in lines:
                    if not raw.strip():
                        continue
                    state = json.loads(raw)
                    if state.get("tagged"):
                        episode += 1
                        epsilon = max(0.03, epsilon * 0.995)
                    actions = []
                    tagger_records = []
                    for tagger in state["taggers"]:
                        tagger_state = {
                            "px": state["px"],
                            "pz": state["pz"],
                            "tx": tagger["x"],
                            "tz": tagger["z"],
                        }
                        distance = math.hypot(state["px"] - tagger["x"], state["pz"] - tagger["z"])
                        key = state_key(tagger_state)
                        next_values = q_table.setdefault(key, [0.0] * len(ACTIONS))
                        branches = []
                        for branch_action in ACTIONS:
                            branch_x, branch_z, branch_distance = simulate_branch(tagger_state, branch_action)
                            branch_key = state_key({"px": state["px"], "pz": state["pz"], "tx": branch_x, "tz": branch_z})
                            branch_values = q_table.setdefault(branch_key, [0.0] * len(ACTIONS))
                            branch_reward = (distance - branch_distance) * 2.0 - 0.01
                            if branch_distance < 1.2:
                                branch_reward += 10.0
                            values_before = next_values[branch_action]
                            next_values[branch_action] += 0.15 * (branch_reward + 0.92 * max(branch_values) - values_before)
                            branches.append({"action": branch_action, "next": branch_key, "reward": round(branch_reward, 4)})
                        actions.append(choose_action(next_values, epsilon))
                        tagger_records.append({"state": key, "branches": branches})
                    branch_records.append({"taggers": tagger_records})
                    steps += 1
                    if steps % 500 == 0:
                        save_training(q_table, epsilon, episode, branch_records)
                        print(f"episodes={episode} taggers={len(state['taggers'])} states={len(q_table)} branches={steps * len(state['taggers']) * len(ACTIONS)} epsilon={epsilon:.3f}")
                    connection.sendall((json.dumps({"actions": actions}) + "\n").encode("utf-8"))
    finally:
        save_training(q_table, epsilon, episode, branch_records)
    print(f"Game disconnected. Saved {len(q_table)} states.")


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(1)
        print(f"Training server listening on {HOST}:{PORT}")
        print("Start game.exe after this message. Use WASD to move the blue player.")
        while True:
            connection, address = server.accept()
            print(f"C++ game connected from {address}")
            try:
                train_connection(connection)
            except (ConnectionError, json.JSONDecodeError, KeyError) as error:
                print(f"Training session ended: {error}")


if __name__ == "__main__":
    main()
import json
import math
import random
import socket
from pathlib import Path

HOST = "127.0.0.1"
PORT = 5000
Q_TABLE_PATH = Path(__file__).with_name("q_table.json")
TRAINING_STATE_PATH = Path(__file__).with_name("training_state.json")
BRANCH_LOG_PATH = Path(__file__).with_name("branch_training.jsonl")
Q_HISTORY_PATH = Path(__file__).with_name("q_table_history.jsonl")
ACTIONS = (0, 1, 2, 3, 4, 5)
ACTION_NAMES = {0: "WAIT", 1: "UP", 2: "DOWN", 3: "LEFT", 4: "RIGHT", 5: "JUMP"}


def load_q_table():
    if not Q_TABLE_PATH.exists():
        return {}
    try:
        table = json.loads(Q_TABLE_PATH.read_text(encoding="utf-8"))
        if not isinstance(table, dict):
            return {}
        for key, values in table.items():
            if not isinstance(values, list):
                table[key] = [0.0] * len(ACTIONS)
            elif len(values) < len(ACTIONS):
                table[key] = values + [0.0] * (len(ACTIONS) - len(values))
        return table
    except (OSError, json.JSONDecodeError):
        return {}


def load_training_state():
    if not TRAINING_STATE_PATH.exists():
        return 0.25, 1
    try:
        state = json.loads(TRAINING_STATE_PATH.read_text(encoding="utf-8"))
        return float(state.get("epsilon", 0.25)), int(state.get("episodes", 1))
    except (OSError, json.JSONDecodeError, TypeError, ValueError):
        return 0.25, 1


def save_training(q_table, epsilon, episode, branch_records):
    q_table_temp = Q_TABLE_PATH.with_suffix(".tmp")
    state_temp = TRAINING_STATE_PATH.with_suffix(".tmp")
    q_table_temp.write_text(json.dumps(q_table, indent=2), encoding="utf-8")
    state_temp.write_text(json.dumps({"epsilon": epsilon, "episodes": episode}, indent=2), encoding="utf-8")
    q_table_temp.replace(Q_TABLE_PATH)
    state_temp.replace(TRAINING_STATE_PATH)
    all_values = [value for values in q_table.values() for value in values]
    if all_values:
        with Q_HISTORY_PATH.open("a", encoding="utf-8") as history:
            history.write(json.dumps({
                "episode": episode,
                "states": len(q_table),
                "mean_q": sum(all_values) / len(all_values),
                "max_q": max(all_values),
            }) + "\n")
    if branch_records:
        with BRANCH_LOG_PATH.open("a", encoding="utf-8") as branch_log:
            for record in branch_records:
                branch_log.write(json.dumps(record) + "\n")
        branch_records.clear()


def state_key(state):
    dx = int(round(state["px"] - state["tx"]))
    dz = int(round(state["pz"] - state["tz"]))
    return f"{max(-18, min(18, dx))},{max(-18, min(18, dz))}"


def choose_action(values, epsilon):
    hunting_actions = (1, 2, 3, 4, 5)
    if random.random() < epsilon:
        return random.choice(hunting_actions)
    best_value = max(values[action] for action in hunting_actions)
    best_actions = [action for action in hunting_actions if values[action] == best_value]
    return random.choice(best_actions)


def simulate_branch(state, action):
    next_x = state["tx"]
    next_z = state["tz"]
    if action == 1:
        next_z -= 0.12
    elif action == 2:
        next_z += 0.12
    elif action == 3:
        next_x -= 0.12
    elif action == 4:
        next_x += 0.12
    next_x = max(-8.3, min(8.3, next_x))
    next_z = max(-8.3, min(8.3, next_z))
    next_distance = math.hypot(state["px"] - next_x, state["pz"] - next_z)
    return next_x, next_z, next_distance


def train_connection(connection):
    q_table = load_q_table()
    epsilon, episode = load_training_state()
    steps = 0
    receive_buffer = ""
    branch_records = []

    try:
        with connection:
            while True:
                packet = connection.recv(4096)
                if not packet:
                    break
                receive_buffer += packet.decode("utf-8")
                lines = receive_buffer.split("\n")
                receive_buffer = lines.pop()

                for raw in lines:
                    if not raw.strip():
                        continue
                    state = json.loads(raw)
                    if state.get("tagged"):
                        episode += 1
                        epsilon = max(0.03, epsilon * 0.995)
                    actions = []
                    tagger_records = []
                    for tagger in state["taggers"]:
                        tagger_state = {
                            "px": state["px"],
                            "pz": state["pz"],
                            "tx": tagger["x"],
                            "tz": tagger["z"],
                        }
                        distance = math.hypot(state["px"] - tagger["x"], state["pz"] - tagger["z"])
                        key = state_key(tagger_state)
                        next_values = q_table.setdefault(key, [0.0] * len(ACTIONS))
                        branches = []
                        for branch_action in ACTIONS:
                            branch_x, branch_z, branch_distance = simulate_branch(tagger_state, branch_action)
                            branch_key = state_key({"px": state["px"], "pz": state["pz"], "tx": branch_x, "tz": branch_z})
                            branch_values = q_table.setdefault(branch_key, [0.0] * len(ACTIONS))
                            branch_reward = (distance - branch_distance) * 2.0 - 0.01
                            if branch_distance < 1.2:
                                branch_reward += 10.0
                            values_before = next_values[branch_action]
                            next_values[branch_action] += 0.15 * (branch_reward + 0.92 * max(branch_values) - values_before)
                            branches.append({"action": branch_action, "next": branch_key, "reward": round(branch_reward, 4)})
                        actions.append(choose_action(next_values, epsilon))
                        tagger_records.append({"state": key, "branches": branches})
                    branch_records.append({"taggers": tagger_records})
                    steps += 1
                    if steps % 500 == 0:
                        save_training(q_table, epsilon, episode, branch_records)
                        print(f"episodes={episode} taggers={len(state['taggers'])} states={len(q_table)} branches={steps * len(state['taggers']) * len(ACTIONS)} epsilon={epsilon:.3f}")
                    connection.sendall((json.dumps({"actions": actions}) + "\n").encode("utf-8"))
    finally:
        save_training(q_table, epsilon, episode, branch_records)
    print(f"Game disconnected. Saved {len(q_table)} states.")


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(1)
        print(f"Training server listening on {HOST}:{PORT}")
        print("Start game.exe after this message. Use WASD to move the blue player.")
        while True:
            connection, address = server.accept()
            print(f"C++ game connected from {address}")
            try:
                train_connection(connection)
            except (ConnectionError, json.JSONDecodeError, KeyError) as error:
                print(f"Training session ended: {error}")


if __name__ == "__main__":
    main()