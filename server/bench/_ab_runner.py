import subprocess, argparse, csv, time, signal, socket, random, re
from pathlib import Path
from statistics import mean


def free_port():
    while True:
        p = random.randint(32768, 60999)
        with socket.socket() as s:
            if s.connect_ex(("127.0.0.1", p)) != 0:
                return p


def parse_ab(text):
    rx = {
        "rps":  r"Requests per second:\s*([\d\.]+)",
        "tpr":  r"Time per request:\s*([\d\.]+) \[ms\] \(mean\)",
        "tr":   r"Transfer rate:\s*([\d\.]+)",
        "fail": r"Failed requests:\s*(\d+)",
        "done": r"Complete requests:\s*(\d+)",
        "time": r"Time taken for tests:\s*([\d\.]+)",
        "conc": r"Concurrency Level:\s*(\d+)",
    }
    out = {}
    for key, pat in rx.items():
        # print(f"{key} == {pat}")
        m = re.search(pat, text)
        # print(m, " ", m.group(1), "\n")
        if m: out[key] = float(m.group(1))
    return out


def start_server(port):
    print(f"  → Запуск сервера на порте {port}")
    p = subprocess.Popen(
        ["./server.exe", str(port)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    print(f"    Сервер запущен (PID={p.pid})")
    time.sleep(0.4)
    
    return p


def stop_server(p):
    print("  → Остановка сервера...")
    p.send_signal(signal.SIGTERM)
    try:
        p.wait(timeout=1)
    except subprocess.TimeoutExpired:
        print("    SIGTERM не сработал — SIGKILL")
        p.kill()
    print("    Сервер остановлен.")


def run_ab(url, n, conc):
    print(f"    Запуск ab: n={n}, c={conc}")
    res = subprocess.run(["ab", "-n", str(n), "-c", str(conc), url],
                         capture_output=True, text=True)
    if res.returncode != 0:
        print(f"    Ошибка запуска ab - {res}\n")
        return None

    return parse_ab(res.stdout)


# /home/kathrine/vuz/networks_cp/server/venv/bin/python /home/kathrine/vuz/networks_cp/server/bench/ab_runner.py -n 5000 -c 1+5+10 -r 2 -o ab_results.csv
def main():
    a = argparse.ArgumentParser()
    a.add_argument("-n", type=int, default=1000, help="Количество запросов")
    a.add_argument("-c", nargs="+", type=int, default=[1, 10, 50])
    a.add_argument("-r", "--repeats", type=int, default=3)
    a.add_argument("--file", default="/")
    a.add_argument("-o", default="ab_results.csv")
    args = a.parse_args()

    with open(args.o, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["concurrency", "requests", "rps", "tpr", "transfer"])

        for conc in args.c:
            print(f"\n=== Тестирование concurrency={conc} ===")
            records = []

            for i in range(1, args.repeats + 1):
                print(f"\n  --- Прогон {i}/{args.repeats} ---")

                port = free_port()
                url = f"http://127.0.0.1:{port}/"
                print(f"    URL: {url}")

                srv = start_server(port)
                m = run_ab(url, args.n, conc)
                stop_server(srv)

                if m:
                    print(f"    Результат: RPS={m['rps']:.1f}, TPR={m['tpr']:.2f} ms, TR={m['tr']:.1f}")
                    records.append(m)
                else:
                    print("    Прогон завершился ошибкой.")

            if not records:
                print("Нет успешных прогонов — пропуск.")
                continue

            avg = lambda key: mean(r[key] for r in records)
            w.writerow([conc, args.n, avg("rps"), avg("tpr"), avg("tr")])

            print(f"\n  >>> Итог по concurrency={conc}: "
                  f"RPS={avg('rps'):.1f}, TPR={avg('tpr'):.2f} ms, TR={avg('tr'):.1f}")


if __name__ == "__main__":
    main()
