"""
Client goi Supabase Auth (dang nhap) va REST API (lay du
lieu activity_logs / activity_daily_totals cua chinh
nguoi dung dang nhap - duoc gioi han boi RLS
"auth.uid() = user_id" da cau hinh trong supabase_schema.sql).
"""

import requests

from config import SUPABASE_URL, SUPABASE_ANON_KEY

AUTH_URL = f"{SUPABASE_URL}/auth/v1"
REST_URL = f"{SUPABASE_URL}/rest/v1"


class SupabaseAuthError(Exception):
    def __init__(self, message):
        super().__init__(message)
        self.message = message


def login(email, password):
    """
    Dang nhap qua Supabase Auth (email + password).
    Tra ve dict: {access_token, refresh_token, user_id, email}
    Nem SupabaseAuthError neu sai thong tin / loi.
    """
    resp = requests.post(
        f"{AUTH_URL}/token",
        params={"grant_type": "password"},
        json={"email": email, "password": password},
        headers={"apikey": SUPABASE_ANON_KEY},
        timeout=15,
    )

    data = resp.json()

    if resp.status_code != 200:
        msg = (
            data.get("error_description")
            or data.get("msg")
            or data.get("message")
            or f"Loi khong xac dinh (HTTP {resp.status_code})"
        )
        raise SupabaseAuthError(msg)

    return {
        "access_token": data.get("access_token"),
        "refresh_token": data.get("refresh_token"),
        "user_id": (data.get("user") or {}).get("id"),
        "email": (data.get("user") or {}).get("email"),
    }


def refresh_session(refresh_token):
    """Làm mới access_token bằng refresh_token đã lưu."""
    resp = requests.post(
        f"{AUTH_URL}/token",
        params={"grant_type": "refresh_token"},
        json={"refresh_token": refresh_token},
        headers={"apikey": SUPABASE_ANON_KEY},
        timeout=15,
    )

    data = resp.json()

    if resp.status_code != 200:
        raise SupabaseAuthError(
            data.get("error_description") or "Refresh session that bai"
        )

    return {
        "access_token": data.get("access_token"),
        "refresh_token": data.get("refresh_token"),
        "user_id": (data.get("user") or {}).get("id"),
        "email": (data.get("user") or {}).get("email"),
    }


def _auth_headers(access_token):
    return {
        "apikey": SUPABASE_ANON_KEY,
        "Authorization": f"Bearer {access_token}",
    }


def fetch_daily_totals(access_token, day_from=None, day_to=None):
    """
    Lay du lieu tu view activity_daily_totals cua CHINH
    nguoi dung dang nhap (RLS tu dong loc theo auth.uid()).
    day_from/day_to: "YYYY-MM-DD". Neu khong truyen, lay tat ca.
    """
    params = {"select": "*", "order": "day.desc,total_seconds.desc", "limit": "1000"}

    if day_from:
        params["day"] = f"gte.{day_from}"

    if day_to:
        # PostgREST khong cho 2 dieu kien tren cung 1 key qua params
        # thong thuong, nen dung "and" filter neu can ca 2 dau.
        if day_from:
            del params["day"]
            params["and"] = f"(day.gte.{day_from},day.lte.{day_to})"
        else:
            params["day"] = f"lte.{day_to}"

    resp = requests.get(
        f"{REST_URL}/activity_daily_totals",
        headers=_auth_headers(access_token),
        params=params,
        timeout=15,
    )

    if resp.status_code != 200:
        raise SupabaseAuthError(
            f"Khong lay duoc du lieu (HTTP {resp.status_code}): {resp.text}"
        )

    return resp.json()


def change_password(access_token, new_password):
    """
    Doi mat khau cua chinh nguoi dung dang dang nhap, qua
    Supabase Auth (PUT /auth/v1/user). Can access_token that
    (khong phai publishable key) de xac dinh dung tai khoan.
    """
    resp = requests.put(
        f"{AUTH_URL}/user",
        json={"password": new_password},
        headers=_auth_headers(access_token),
        timeout=15,
    )

    data = resp.json()

    if resp.status_code != 200:
        msg = (
            data.get("error_description")
            or data.get("msg")
            or data.get("message")
            or f"Loi khong xac dinh (HTTP {resp.status_code})"
        )
        raise SupabaseAuthError(msg)

    return True


def fetch_recent_logs(access_token, limit=100):
    """Lay danh sach record gan nhat cua chinh nguoi dung dang nhap."""
    params = {
        "select": "device_id,process_name,window_title,duration_seconds,start_time,end_time",
        "order": "start_time.desc",
        "limit": str(limit),
    }

    resp = requests.get(
        f"{REST_URL}/activity_logs",
        headers=_auth_headers(access_token),
        params=params,
        timeout=15,
    )

    if resp.status_code != 200:
        raise SupabaseAuthError(
            f"Khong lay duoc du lieu (HTTP {resp.status_code}): {resp.text}"
        )

    return resp.json()
