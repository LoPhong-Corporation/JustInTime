"""
JustInTime Dashboard - chi chay local (127.0.0.1), khong
chia se ra ngoai. Dang nhap bang chinh tai khoan Supabase
cua ban (cung he thong auth voi app JustInTime.exe).

Chay:
    pip install -r requirements.txt
    python app.py

Roi mo trinh duyet: http://127.0.0.1:5000
"""

import os
import io
import csv
import json
import time
import secrets
from datetime import datetime, timedelta
from functools import wraps

from flask import (
    Flask, render_template, redirect, url_for,
    request, session, jsonify, Response,
)

import supabase_client
import system_stats
import dashboard_settings
import i18n


def get_or_create_secret_key():
    appdata = os.environ.get("APPDATA")

    if not appdata:
        return secrets.token_hex(32)

    config_dir = os.path.join(appdata, "JustInTime")
    os.makedirs(config_dir, exist_ok=True)

    key_path = os.path.join(config_dir, "dashboard_secret.key")

    if os.path.exists(key_path):
        with open(key_path, "r", encoding="utf-8") as f:
            return f.read().strip()

    key = secrets.token_hex(32)

    with open(key_path, "w", encoding="utf-8") as f:
        f.write(key)

    return key


app = Flask(__name__)
app.secret_key = get_or_create_secret_key()


def login_required(view_func):
    @wraps(view_func)
    def wrapper(*args, **kwargs):
        if "access_token" not in session:
            return redirect(url_for("login"))
        return view_func(*args, **kwargs)
    return wrapper


@app.context_processor
def inject_globals():
    ds = dashboard_settings.load()
    t = i18n.get_dict(ds["language"])
    font_stack = dashboard_settings.FONT_STACKS.get(ds["font"], dashboard_settings.FONT_STACKS["mono"])

    return {
        "t": t,
        "dash_settings": ds,
        "font_stack": font_stack,
        "current_email": session.get("email"),
    }


def range_to_dates(range_key):
    """Trả về (day_from, day_to) dạng YYYY-MM-DD cho khoảng đã chọn."""
    today = datetime.now().date()

    if range_key == "week":
        return (today - timedelta(days=7)).isoformat(), today.isoformat()

    if range_key == "month":
        return (today - timedelta(days=30)).isoformat(), today.isoformat()

    return today.isoformat(), today.isoformat()


def with_token_refresh(func, *args, **kwargs):
    """Gọi 1 hàm supabase_client, tự refresh token 1 lần nếu hết hạn."""
    try:
        return func(session["access_token"], *args, **kwargs)
    except supabase_client.SupabaseAuthError:
        refreshed = supabase_client.refresh_session(session["refresh_token"])
        session["access_token"] = refreshed["access_token"]
        session["refresh_token"] = refreshed["refresh_token"]
        return func(session["access_token"], *args, **kwargs)


# ---------------- Auth ----------------

@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "GET":
        return render_template("login.html", error=None)

    email = request.form.get("email", "").strip()
    password = request.form.get("password", "")

    if not email or not password:
        return render_template("login.html", error="Please enter both email and password.")

    try:
        result = supabase_client.login(email, password)
    except supabase_client.SupabaseAuthError as e:
        return render_template("login.html", error=e.message)

    session["access_token"] = result["access_token"]
    session["refresh_token"] = result["refresh_token"]
    session["user_id"] = result["user_id"]
    session["email"] = result["email"]

    return redirect(url_for("index"))


@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for("login"))


# ---------------- Pages ----------------

@app.route("/")
@login_required
def index():
    return render_template("dashboard.html", active_page="system")


@app.route("/settings", methods=["GET", "POST"])
@login_required
def settings_page():
    ds = dashboard_settings.load()
    message = None
    error = None

    if request.method == "POST":
        form_type = request.form.get("form_type")

        if form_type == "appearance":
            ds["font"] = request.form.get("font", ds["font"])
            ds["language"] = request.form.get("language", ds["language"])
            ds["cpu_threshold"] = int(request.form.get("cpu_threshold", ds["cpu_threshold"]))
            ds["ram_threshold"] = int(request.form.get("ram_threshold", ds["ram_threshold"]))
            ds["disk_threshold"] = int(request.form.get("disk_threshold", ds["disk_threshold"]))
            dashboard_settings.save(ds)
            ds = dashboard_settings.load()
            message = "save_success"

        elif form_type == "password":
            new_password = request.form.get("new_password", "")
            confirm_password = request.form.get("confirm_password", "")

            if new_password != confirm_password:
                error = "password_mismatch"
            else:
                try:
                    with_token_refresh(supabase_client.change_password, new_password)
                    message = "password_changed"
                except supabase_client.SupabaseAuthError as e:
                    error = e.message

    return render_template(
        "settings.html",
        ds=ds,
        message=message,
        error=error,
        active_page="settings",
    )


@app.route("/report")
@login_required
def report_print():
    range_key = request.args.get("range", "today")
    day_from, day_to = range_to_dates(range_key)

    try:
        totals = with_token_refresh(supabase_client.fetch_daily_totals, day_from, day_to)
        recent = with_token_refresh(supabase_client.fetch_recent_logs, 500)
    except supabase_client.SupabaseAuthError as e:
        return f"Error: {e.message}", 500

    app_totals = {}
    for row in totals:
        key = row["process_name"]
        app_totals[key] = app_totals.get(key, 0) + row["total_seconds"]

    sorted_apps = sorted(app_totals.items(), key=lambda x: x[1], reverse=True)

    return render_template(
        "report_print.html",
        range_key=range_key,
        apps=sorted_apps,
        records=recent,
        generated_at=datetime.now(),
    )


@app.route("/export/csv")
@login_required
def export_csv():
    range_key = request.args.get("range", "today")

    try:
        records = with_token_refresh(supabase_client.fetch_recent_logs, 1000)
    except supabase_client.SupabaseAuthError as e:
        return jsonify({"error": e.message}), 500

    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(["device_id", "process_name", "window_title", "duration_seconds", "start_time", "end_time"])

    for r in records:
        writer.writerow([
            r.get("device_id"), r.get("process_name"), r.get("window_title"),
            r.get("duration_seconds"), r.get("start_time"), r.get("end_time"),
        ])

    filename = f"justintime_report_{range_key}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"

    return Response(
        output.getvalue(),
        mimetype="text/csv",
        headers={"Content-Disposition": f"attachment; filename={filename}"},
    )


# ---------------- API: system ----------------

@app.route("/api/system/info")
@login_required
def api_system_info():
    return jsonify(system_stats.get_machine_info())


@app.route("/api/system/network-interfaces")
@login_required
def api_network_interfaces():
    return jsonify(system_stats.get_network_interfaces_info())


@app.route("/api/system/stream")
@login_required
def api_system_stream():
    """Server-Sent Events: đẩy thông số CPU/RAM/Disk/Network mỗi giây + cờ cảnh báo ngưỡng."""

    def generate():
        while True:
            try:
                data = system_stats.get_live_stats()
                ds = dashboard_settings.load()

                data["cpu_alert"] = data["cpu_percent"] >= ds["cpu_threshold"]
                data["ram_alert"] = data["ram_percent"] >= ds["ram_threshold"]
                data["disk_alert"] = data["disk_percent"] >= ds["disk_threshold"]
                data["cpu_threshold"] = ds["cpu_threshold"]
                data["ram_threshold"] = ds["ram_threshold"]
                data["disk_threshold"] = ds["disk_threshold"]

                yield f"data: {json.dumps(data)}\n\n"
            except GeneratorExit:
                break
            except Exception as e:
                yield f"data: {json.dumps({'error': str(e)})}\n\n"

            time.sleep(1)

    return Response(generate(), mimetype="text/event-stream")


# ---------------- API: cloud (Supabase) ----------------

@app.route("/api/cloud/summary")
@login_required
def api_cloud_summary():
    range_key = request.args.get("range", "today")
    day_from, day_to = range_to_dates(range_key)

    try:
        data = with_token_refresh(supabase_client.fetch_daily_totals, day_from, day_to)
    except supabase_client.SupabaseAuthError as e:
        return jsonify({"error": e.message}), 401

    # Gộp theo app (bỏ qua chiều ngày/thiết bị) cho biểu đồ top app
    app_totals = {}
    for row in data:
        key = row["process_name"]
        app_totals[key] = app_totals.get(key, 0) + row["total_seconds"]

    apps = [
        {"process_name": k, "total_seconds": v}
        for k, v in sorted(app_totals.items(), key=lambda x: x[1], reverse=True)
    ][:15]

    return jsonify({"apps": apps})


@app.route("/api/cloud/daily")
@login_required
def api_cloud_daily():
    """Tổng thời gian theo từng ngày (cho biểu đồ cột) trong khoảng đã chọn."""
    range_key = request.args.get("range", "week")
    day_from, day_to = range_to_dates(range_key)

    try:
        data = with_token_refresh(supabase_client.fetch_daily_totals, day_from, day_to)
    except supabase_client.SupabaseAuthError as e:
        return jsonify({"error": e.message}), 401

    day_totals = {}
    for row in data:
        day = row["day"]
        day_totals[day] = day_totals.get(day, 0) + row["total_seconds"]

    sorted_days = sorted(day_totals.items())

    return jsonify({
        "days": [d for d, _ in sorted_days],
        "totals": [v for _, v in sorted_days],
    })


@app.route("/api/cloud/recent")
@login_required
def api_cloud_recent():
    try:
        data = with_token_refresh(supabase_client.fetch_recent_logs, 100)
    except supabase_client.SupabaseAuthError as e:
        return jsonify({"error": e.message}), 401

    return jsonify({"records": data})


if __name__ == "__main__":
    # QUAN TRONG: chi bind 127.0.0.1 (localhost) - khong ai
    # tu may/mang khac truy cap duoc dashboard nay.
    app.run(host="127.0.0.1", port=5000, debug=False, threaded=True)
