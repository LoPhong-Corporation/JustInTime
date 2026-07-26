"""
Cau hinh Supabase cho dashboard. Mac dinh khop voi
include/config.h cua app JustInTime - co the ghi de bang
bien moi truong neu ban doi Supabase project.
"""

import os

SUPABASE_URL = os.environ.get(
    "SUPABASE_URL",
    "https://crdvfasjtrfrasqehwkc.supabase.co",
)

SUPABASE_ANON_KEY = os.environ.get(
    "SUPABASE_ANON_KEY",
    "sb_publishable_2BDazw0ggLN0GC9Zyu2hOQ_XrcqaR7v",
)
