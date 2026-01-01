import pandas as pd
import plotly.graph_objects as go

def main():
    df = pd.read_csv("ab_results.csv")

    fig = go.Figure()

    fig.add_trace(go.Scatter(
        x=df["concurrency"],
        y=df["rps"],
        mode="lines+markers",
        name="Requests per second"
    ))

    fig.update_layout(
        title="Производительность сервера при росте количества соединений",
        xaxis_title="Количество параллельных соединений (concurrency)",
        yaxis_title="RPS (запросов в секунду)",
        template="plotly_white"
    )

    fig.write_html("max_connections.html")
    print("График сохранён в max_connections.html")

if __name__ == "__main__":
    main()
