import numpy as np
import pandas as pd
import plotly.graph_objects as go

from infer import predict_delay

DATA_FILE = "combined_processed.csv"
DAY_SECONDS = 24 * 3600
DAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]

df = pd.read_csv(DATA_FILE)
df = df.dropna(subset=["delay_lag_10m"])

# prediction surface (lag30 = lag10)
week_grid = np.linspace(0, 7 * DAY_SECONDS, 120)
lag_grid = np.linspace(df["delay_lag_10m"].min(), df["delay_lag_10m"].max(), 60)
week_mesh, lag_mesh = np.meshgrid(week_grid, lag_grid)

pred_mesh = np.zeros_like(week_mesh)
for i in range(week_mesh.shape[0]):
    for j in range(week_mesh.shape[1]):
        s = week_mesh[i, j]
        pred_mesh[i, j] = predict_delay(
            s, lag_mesh[i, j], lag_mesh[i, j], is_weekend=1.0 if (s // DAY_SECONDS) >= 5 else 0.0
        )

fig = go.Figure()

fig.add_trace(
    go.Scatter3d(
        x=df["secs_since_last_monday_0000"],
        y=df["delay_lag_10m"],
        z=df["delay"],
        mode="markers",
        marker=dict(size=2.5, color="steelblue", opacity=1.0),
        name="observed",
    )
)

fig.add_trace(
    go.Surface(
        x=week_mesh,
        y=lag_mesh,
        z=pred_mesh,
        colorscale="Reds",
        opacity=0.6,
        showscale=False,
        name="predicted",
    )
)

day_ticks = [d * DAY_SECONDS for d in range(len(DAYS) + 1)]
day_labels = DAYS + [""]

fig.update_layout(
    title="delay vs time of week vs delay_lag_10m, route 601 yrt",
    scene=dict(
        xaxis=dict(title="time of week", tickvals=day_ticks, ticktext=day_labels),
        yaxis=dict(title="delay_lag_10m (s)"),
        zaxis=dict(title="delay (s)"),
        aspectratio=dict(x=3, y=1, z=1),
        camera=dict(eye=dict(x=1.8, y=-1.8, z=0.8)),
    ),
    width=1200,
    height=800,
)

fig.write_html("delay_3d.html", auto_open=True)
