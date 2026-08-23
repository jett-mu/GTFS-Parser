import torch
import os
import json
import numpy as np
import pandas as pd

DATA_FILE = "combined_processed.csv"
WEIGHTS_PATH = "model_weights.pt"

CONTINUOUS_COLUMNS = [
    "temperature",
    "precipitation",
    "rain",
    "showers",
    "snowfall",
    "rain_sum_1hr",
    "rain_sum_3hr",
    "rain_sum_7hr",
    "precipitation_sum_1hr",
    "precipitation_sum_3hr",
    "precipitation_sum_7hr",
    "day_of_week",
]
BINARY_COLUMNS = ["is_weekend", "is_holiday"]
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
    return df

def encode_time_batch(secs_since_last_monday_0000):
    t = secs_since_last_monday_0000 % WEEK_SECONDS
    week_angle = 2 * np.pi * t / WEEK_SECONDS
    day_angle = 2 * np.pi * (t % DAY_SECONDS) / DAY_SECONDS
    return np.stack(
        [np.sin(week_angle), np.cos(week_angle), np.sin(day_angle), np.cos(day_angle)],
        axis=1,
    )

def encode_time(secs_since_last_monday_0000):
    return encode_time_batch(np.array([float(secs_since_last_monday_0000)]))[0].tolist()

def build_feature_matrix(df):
    continuous = df[CONTINUOUS_COLUMNS].astype(np.float32).values
    time_features = encode_time_batch(df["secs_since_last_monday_0000"].astype(np.float64).values)
    binary = df[BINARY_COLUMNS].astype(np.float32).values
    return np.hstack([continuous, time_features, binary]).astype(np.float32)

def build_feature_vector(row):
    continuous = [float(row[col]) for col in CONTINUOUS_COLUMNS]
    binary = [float(row[col]) for col in BINARY_COLUMNS]
    return continuous + encode_time(row["secs_since_last_monday_0000"]) + binary

df = load_data(DATA_FILE)

X_raw = torch.from_numpy(build_feature_matrix(df))
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
print("enter values for inference:")
continuous_values = [float(input(f"{col}: ")) for col in CONTINUOUS_COLUMNS]
binary_values = [float(input(f"{col}: ")) for col in BINARY_COLUMNS]
secs_since_last_monday_0000 = input("secs_since_last_monday_0000: ")

values = continuous_values + encode_time(secs_since_last_monday_0000) + binary_values

with torch.no_grad():
    test_input_raw = torch.tensor([values])
    test_input = (test_input_raw - X_mean) / X_std
    predicted_delay = model(test_input) * y_std + y_mean
    print(f"Predicted delay: {predicted_delay.item():.2f}")
