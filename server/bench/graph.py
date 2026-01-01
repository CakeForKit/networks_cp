import pandas as pd
import plotly.graph_objects as go

def moving_average(data, window=5):
    """Скользящее среднее с указанным окном"""
    if len(data) < window:
        return data
    return pd.Series(data).rolling(window=window, center=True, min_periods=1).mean().tolist()

def graph_rps(df, smooth_window=2):
    fig = go.Figure()
    # smoothed_rps = moving_average(list(df["rps"]), smooth_window)
    fig.add_trace(go.Scatter(
        x=df["connections"],
        y=df["rps"],
        mode="lines+markers",
        name="RPS"
    ))
    fig.update_layout(
        title="Пропускная способность",
        xaxis_title="Количество параллельных соединений",
        yaxis_title="RPS (запросов в секунду)",
        template="plotly_white",
        font=dict(size=16)
    )
    fig.write_html("./graphs/rps_graph.html")
    print("График сохранён в ./graphs/rps_graph.html")

def graph_transfer(df, smooth_window=2):
    df["speed_per_conn"] = df["transfer"] / df["connections"]
    fig = go.Figure()
    # smoothed_transfer = moving_average(list(df["transfer"]), smooth_window)
    fig.add_trace(go.Scatter(
        x=df["connections"],
        y=df["transfer"],
        mode="lines+markers",
        marker=dict(
            size=10,                    # Размер
            symbol='triangle-down',           # Тип символа
            color='blue',                # Цвет
            # line=dict(width=2, color='black')  # Обводка
        ),
        name="Совокупная скорость (KB/s)"
    ))
    fig.add_trace(go.Scatter(
        x=df["connections"],
        y=df["speed_per_conn"],
        mode="lines+markers",
        name="Скорость на соединение (KB/s)"
    ))
    fig.update_layout(
        title="Скорость отдачи данных",
        xaxis_title="Количество параллельных соединений",
        yaxis_title="KB/s",
        template="plotly_white",
        legend=dict(
            yanchor="top",
            y=0.99,
            xanchor="right",
            x=0.99,
            bgcolor="rgba(255, 255, 255, 0.8)",
            bordercolor="black",
            borderwidth=1
        ),
        font=dict(size=16)
    )
    fig.write_html("./graphs/transfer_graph.html")
    print("График сохранён в ./graphs/transfer_graph.html")


def main():
    df = pd.read_csv("wrk_results.csv")
    # print(df)
    # print(list(df["transfer"]))
    # print(list(df["rps"]))
    graph_rps(df)
    graph_transfer(df)
    

if __name__ == "__main__":
    main()