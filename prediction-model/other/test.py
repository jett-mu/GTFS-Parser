import torch
import os
import json
import numpy as np
import pandas as pd

DATA_FILE = "data.csv"
WEIGHTS_PATH = "model_weights.pt"

CONTINUOUS_COLUMNS = ["temperature", "precipitation", "rain", "showers", "snowfall"]
OUTPUT_COLUMN = "delay"


WEEK_SECONDS = 7 * 24 * 3600
DAY_SECONDS = 24 * 3600

class test_neural_network(torch.nn.Module):
    def __init__(self, in_size, hidden_size, out_size, dropout=0.3):
        super().__init__()
        self.net = torch.nn.Sequential(
            torch.nn.Linear(in_size, hidden_size),
            torch.nn.ReLU(),
            torch.nn.Dropout(dropout),
            torch.nn.Linear(hidden_size, hidden_size),
            torch.nn.ReLU(),
            torch.nn.Dropout(dropout),
            torch.nn.Linear(hidden_size, out_size)
        )
    def forward(self, x):
        return self.net(x)

def save_weights_readable(state_dict, path):
    readable = {name: tensor.tolist() for name, tensor in state_dict.items()}
    with open(path, "w") as f:
        json.dump(readable, f, indent=2)

def load_weights_readable(path):
    with open(path) as f:
        readable = json.load(f)
    return {name: torch.tensor(values) for name, values in readable.items()}

def load_data(data_file):
    df = pd.read_csv(data_file, dtype=str, keep_default_na=False)
    df = df[df["route_id"] != ""].reset_index(drop=True)
    return df

def build_route_vocab(df):
    route_vocab = sorted(df["route_id"].unique().tolist())
    route_to_idx = {route_id: i for i, route_id in enumerate(route_vocab)}
    return route_vocab, route_to_idx

def encode_time_batch(unix_timestamps):
    t = unix_timestamps % WEEK_SECONDS
    week_angle = 2 * np.pi * t / WEEK_SECONDS


    day_angle = 2 * np.pi * (t % DAY_SECONDS) / DAY_SECONDS
    return np.stack(
        [np.sin(week_angle), np.cos(week_angle), np.sin(day_angle), np.cos(day_angle)],
        axis=1,
    )

def encode_time(unix_timestamp):
    return encode_time_batch(np.array([float(unix_timestamp)]))[0].tolist()

def encode_route_batch(route_ids, route_to_idx):
    n = len(route_ids)
    one_hot = np.zeros((n, len(route_to_idx)), dtype=np.float32)
    rows = [i for i, r in enumerate(route_ids) if r in route_to_idx]
    cols = [route_to_idx[r] for r in route_ids if r in route_to_idx]
    one_hot[rows, cols] = 1.0
    return one_hot

def encode_route(route_id, route_to_idx):
    one_hot = [0.0] * len(route_to_idx)
    if route_id in route_to_idx:
        one_hot[route_to_idx[route_id]] = 1.0
    return one_hot

def build_feature_matrix(df, route_to_idx):
    continuous = df[CONTINUOUS_COLUMNS].astype(np.float32).values
    time_features = encode_time_batch(df["unix_timestamp"].astype(np.float64).values)
    route_features = encode_route_batch(df["route_id"].tolist(), route_to_idx)
    return np.hstack([continuous, time_features, route_features]).astype(np.float32)

def build_feature_vector(row, route_to_idx):
    continuous = [float(row[col]) for col in CONTINUOUS_COLUMNS]
    return continuous + encode_time(row["unix_timestamp"]) + encode_route(row["route_id"], route_to_idx)

df = load_data(DATA_FILE)
route_vocab, route_to_idx = build_route_vocab(df)

X_raw = torch.from_numpy(build_feature_matrix(df, route_to_idx))
y = torch.from_numpy(df[[OUTPUT_COLUMN]].astype(np.float32).values.copy())


torch.manual_seed(0)
n = X_raw.shape[0]
perm = torch.randperm(n)
n_val = max(1, int(0.15 * n))
val_idx, train_idx = perm[:n_val], perm[n_val:]


n_continuous = len(CONTINUOUS_COLUMNS)
X_mean = torch.zeros(X_raw.shape[1])
X_std = torch.ones(X_raw.shape[1])
X_mean[:n_continuous] = X_raw[train_idx][:, :n_continuous].mean(dim=0)
X_std[:n_continuous] = X_raw[train_idx][:, :n_continuous].std(dim=0)
X_std[X_std == 0] = 1.0
X = (X_raw - X_mean) / X_std

y_mean = y[train_idx].mean(dim=0)
y_std = y[train_idx].std(dim=0)
y_std[y_std == 0] = 1.0
y_norm = (y - y_mean) / y_std

X_train, y_train = X[train_idx], y_norm[train_idx]
X_val, y_val = X[val_idx], y_norm[val_idx]

model = test_neural_network(in_size=X.shape[1], hidden_size=64, out_size=1)


criterion = torch.nn.HuberLoss(delta=1.0)


optimizer = torch.optim.AdamW(model.parameters(), lr=0.01, weight_decay=1e-3)


scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(optimizer, factor=0.5, patience=200, min_lr=1e-4)


epochs = int(input("train epochs: "))

def try_load_weights(model, path):
    try:
        model.load_state_dict(torch.load(path))
        return True
    except RuntimeError as e:
        print(f"weights {path} don't match the current architecture ({e})")
        return False

if epochs == 0:
    if os.path.exists(WEIGHTS_PATH):
        if try_load_weights(model, WEIGHTS_PATH):
            print(f"loaded weights from {WEIGHTS_PATH}")
    else:
        print(f"no weights found at {WEIGHTS_PATH}")
else:
    if os.path.exists(WEIGHTS_PATH):
        if try_load_weights(model, WEIGHTS_PATH):
            print(f"loaded weights from {WEIGHTS_PATH}, continuing training")

    best_val_loss = float("inf")
    best_state = None

    for epoch in range(epochs):
        model.train()
        optimizer.zero_grad()
        preds = model(X_train)
        loss = criterion(preds, y_train)
        loss.backward()
        optimizer.step()

        model.eval()
        with torch.no_grad():
            val_loss = criterion(model(X_val), y_val)
        scheduler.step(val_loss)

        if val_loss.item() < best_val_loss:
            best_val_loss = val_loss.item()
            best_state = {k: v.clone() for k, v in model.state_dict().items()}

        if (epoch + 1) % 20 == 0 or epoch == 0:
            lr = optimizer.param_groups[0]["lr"]
            print(f"epoch {epoch+1}/{epochs}, loss: {loss.item():.4f}, val loss: {val_loss.item():.4f}, lr: {lr:.5f}")


    model.load_state_dict(best_state)
    torch.save(model.state_dict(), WEIGHTS_PATH)
    print(f"best val loss: {best_val_loss:.4f}. weights saved to {WEIGHTS_PATH}")


model.eval()
print("\enter values for inference:")
continuous_values = [float(input(f"{col}: ")) for col in CONTINUOUS_COLUMNS]
unix_timestamp = input("unix_timestamp: ")
route_id = input("route_id: ")

if route_id not in route_to_idx:
    print(f"warning: route_id {route_id!r} was not seen during training, using an unknown-route encoding.")

values = continuous_values + encode_time(unix_timestamp) + encode_route(route_id, route_to_idx)

with torch.no_grad():
    test_input_raw = torch.tensor([values])
    test_input = (test_input_raw - X_mean) / X_std
    predicted_delay = model(test_input) * y_std + y_mean
    print(f"Predicted delay: {predicted_delay.item():.2f}")
