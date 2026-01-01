import pandas as pd
import plotly.graph_objects as go

def main():
    df = pd.read_csv("ab_results.csv")

    # Скорость на одно соединение
    df["speed_per_conn"] = df["transfer"] / df["concurrency"]

    fig = go.Figure()

    fig.add_trace(go.Scatter(
        x=df["concurrency"],
        y=df["transfer"],
        mode="lines+markers",
        name="Совокупная скорость (KB/s)"
    ))

    fig.add_trace(go.Scatter(
        x=df["concurrency"],
        y=df["speed_per_conn"],
        mode="lines+markers",
        name="Скорость на соединение (KB/s)"
    ))

    fig.update_layout(
        title="Скорость отдачи данных",
        xaxis_title="Количество параллельных соединений",
        yaxis_title="KB/s",
        template="plotly_white"
    )

    fig.write_html("transfer_speed.html")
    print("График сохранён в transfer_speed.html")

if __name__ == "__main__":
    main()
