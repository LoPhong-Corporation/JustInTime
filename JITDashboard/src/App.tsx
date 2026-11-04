import { useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { openUrl } from "@tauri-apps/plugin-opener";
import appLogo from "../src-tauri/icons/icon.png";
import "./App.css";

type View = "home" | "apps" | "history" | "cloud" | "insights" | "limits" | "devices" | "parental" | "settings";

type AppUsage = { processName: string; totalSeconds: number };
type DailyTotal = { date: string; totalSeconds: number };
type Activity = {
  processName: string;
  windowTitle: string;
  durationSeconds: number;
  startTime: number;
  endTime: number;
};
type LocalDashboardData = {
  available: boolean;
  todaySeconds: number;
  yesterdaySeconds: number;
  todayActivityCount: number;
  applicationCount: number;
  hourlyTotals: number[];
  dailyTotals: DailyTotal[];
  appUsage: AppUsage[];
  recentActivities: Activity[];
};
type GoAuth = { logged_in: boolean; email: string };
type CloudData = {
  apps: { process_name: string; total_seconds: number }[];
  days: string[];
  totals: number[];
  records: { process_name: string; window_title: string; duration_seconds: number; start_time: number; end_time: number; device_id: string }[];
};
type DeviceMessage = {
  id: number;
  sender_device_id: string;
  target_device_id?: string;
  kind: string;
  payload: string;
  created_at: string;
  read_at?: string | null;
};
type FamilyLink = {
  id: number;
  other_user_id: string;
  other_email: string;
  status: string;
  permission_level?: string;
};
type FamilyData = {
  links: FamilyLink[];
  apps: { process_name: string; total_seconds: number }[];
  recent: CloudData["records"];
  machines: { device_id: string; hostname: string; last_seen: string }[];
  limits: { id: number; process_name: string; daily_limit_sec: number | null; blocked: boolean }[];
};
type DeviceHeartbeat = { device_id: string; hostname: string; last_seen: string };
type InsightData = {
  summary: string;
  apps: { process_name: string; category: string; note: string }[];
  recommendations: string[];
};
type ChatMessage = { role: "user" | "model"; text: string };

const GO_API_BASE = import.meta.env.VITE_GO_API_BASE_URL ?? "http://127.0.0.1:5000";
const isTauriRuntime = "__TAURI_INTERNALS__" in window;

async function goApi<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${GO_API_BASE}${path}`, {
    ...init,
    headers: { ...(init?.body ? { "Content-Type": "application/json" } : {}), ...init?.headers },
  });
  const body = await response.text();
  let result: unknown = body;
  try {
    result = body ? JSON.parse(body) : {};
  } catch {
    if (!response.ok) throw new Error(body || `Backend returned HTTP ${response.status}`);
  }
  if (!response.ok) {
    const message = typeof result === "object" && result !== null && "error" in result
      ? String((result as { error: unknown }).error)
      : `Backend returned HTTP ${response.status}`;
    throw new Error(message);
  }
  return result as T;
}

async function downloadCsv(path: string, filename: string) {
  const response = await fetch(`${GO_API_BASE}${path}`);
  if (!response.ok) {
    const result = await response.json().catch(() => null) as { error?: string } | null;
    throw new Error(result?.error ?? `Export failed (HTTP ${response.status})`);
  }
  const url = URL.createObjectURL(await response.blob());
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

const navigation: { id: View; label: string; icon: string }[] = [
  { id: "home", label: "Tổng quan", icon: "overview" },
  { id: "apps", label: "Ứng dụng", icon: "apps" },
  { id: "history", label: "Lịch sử", icon: "history" },
  { id: "cloud", label: "Đồng bộ", icon: "devices" },
  { id: "insights", label: "AI Insights", icon: "app" },
  { id: "limits", label: "Giới hạn", icon: "limits" },
  { id: "devices", label: "Thiết bị", icon: "devices" },
  { id: "parental", label: "Phụ huynh", icon: "family" },
];

const pageTitles: Record<View, string> = {
  home: "Tổng quan hôm nay",
  apps: "Ứng dụng",
  history: "Lịch sử",
  cloud: "Hoạt động đã đồng bộ",
  insights: "AI Insights",
  limits: "Giới hạn ứng dụng",
  devices: "Thiết bị",
  parental: "Quản lý của phụ huynh",
  settings: "Cài đặt",
};

function Icon({ name }: { name: string }) {
  const paths: Record<string, React.ReactNode> = {
    overview: <><rect x="3" y="3" width="8" height="8" rx="1.5" /><rect x="13" y="3" width="8" height="5" rx="1.5" /><rect x="13" y="10" width="8" height="11" rx="1.5" /><rect x="3" y="13" width="8" height="8" rx="1.5" /></>,
    apps: <><rect x="3" y="3" width="18" height="18" rx="2" /><path d="M3 9h18M9 9v12" /></>,
    history: <><circle cx="12" cy="12" r="9" /><path d="M12 7v5l3 2" /></>,
    limits: <><path d="M6 3h12M6 21h12M8 3c0 5 4 6 4 9s-4 4-4 9m8-18c0 5-4 6-4 9s4 4 4 9" /></>,
    devices: <><rect x="3" y="4" width="18" height="13" rx="2" /><path d="M8 21h8m-4-4v4" /></>,
    family: <><circle cx="9" cy="8" r="3" /><path d="M3 20v-1a6 6 0 0 1 12 0v1m2-9a3 3 0 1 0-1-5.8M18 14a5 5 0 0 1 3 4.6v1" /></>,
    settings: <><circle cx="12" cy="12" r="3" /><path d="m19.4 15 .1.1 1.4 1.1-1.4 2.4-1.7-.7a7.8 7.8 0 0 1-1.6.9l-.3 1.8h-2.8l-.3-1.8a7.8 7.8 0 0 1-1.6-.9l-1.7.7-1.4-2.4 1.4-1.1a7 7 0 0 1 0-1.9l-1.4-1.1 1.4-2.4 1.7.7a7.8 7.8 0 0 1 1.6-.9l.3-1.8h2.8l.3 1.8a7.8 7.8 0 0 1 1.6.9l1.7-.7 1.4 2.4-1.4 1.1a7 7 0 0 1 0 1.9Z" /></>,
    clock: <><circle cx="12" cy="12" r="9" /><path d="M12 7v5l3 2" /></>,
    arrow: <path d="M5 12h14m-6-6 6 6-6 6" />,
    sun: <><circle cx="12" cy="12" r="4" /><path d="M12 2v2m0 16v2M4.93 4.93l1.42 1.42m11.3 11.3 1.42 1.42M2 12h2m16 0h2M4.93 19.07l1.42-1.42m11.3-11.3 1.42-1.42" /></>,
    moon: <path d="M20.2 15.1A8.5 8.5 0 0 1 8.9 3.8 8.7 8.7 0 1 0 20.2 15.1Z" />,
    search: <><circle cx="10.8" cy="10.8" r="6.8" /><path d="m16 16 5 5" /></>,
    app: <><rect x="3" y="4" width="18" height="16" rx="2" /><path d="M3 9h18m-13 4h3m-3 3h7" /></>,
  };
  return <svg className={`icon icon-${name}`} viewBox="0 0 24 24" aria-hidden="true">{paths[name] ?? paths.app}</svg>;
}

function EmptyState({ children }: { children: React.ReactNode }) {
  return <div className="empty-state">{children}</div>;
}

function formatDuration(seconds: number) {
  const totalMinutes = Math.floor(seconds / 60);
  return `${Math.floor(totalMinutes / 60)}h ${String(totalMinutes % 60).padStart(2, "0")}m`;
}

function formatClock(timestamp: number) {
  return new Intl.DateTimeFormat("vi-VN", { hour: "2-digit", minute: "2-digit" })
    .format(new Date(timestamp * 1000));
}

function ActivityRows({ activities }: { activities: Activity[] }) {
  if (activities.length === 0) {
    return <EmptyState>Chưa có hoạt động nào trong khoảng thời gian này.</EmptyState>;
  }
  return (
    <div className="activity-list">
      {activities.map((activity, index) => (
        <div className="activity-row" key={`${activity.startTime}-${activity.processName}-${index}`}>
          <div className="activity-app">
            <strong>{activity.processName}</strong>
            <span title={activity.windowTitle}>{activity.windowTitle || "Không có tiêu đề cửa sổ"}</span>
          </div>
          <div className="activity-time">
            <span>{formatClock(activity.startTime)} – {formatClock(activity.endTime)}</span>
            <strong>{formatDuration(activity.durationSeconds)}</strong>
          </div>
        </div>
      ))}
    </div>
  );
}

function App() {
  const [view, setView] = useState<View>("home");
  const [data, setData] = useState<LocalDashboardData | null>(null);
  const [dataError, setDataError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [refreshToken, setRefreshToken] = useState(0);
  const [appSearch, setAppSearch] = useState("");
  const [historyPeriod, setHistoryPeriod] = useState<"today" | "yesterday" | "week">("today");
  const [theme, setTheme] = useState<"light" | "dark">(() => {
    const saved = localStorage.getItem("jitdashboard-theme");
    if (saved === "light" || saved === "dark") return saved;
    return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
  });
  const [focusMinutes, setFocusMinutes] = useState(25);
  const [focusSeconds, setFocusSeconds] = useState<number | null>(null);
  const [dailyGoalMinutes, setDailyGoalMinutes] = useState(() => {
    const saved = Number(localStorage.getItem("jitdashboard-daily-goal-minutes"));
    return Number.isInteger(saved) && saved >= 30 && saved <= 1440 ? saved : 480;
  });
  const [goAuth, setGoAuth] = useState<GoAuth | null>(null);
  const [goBackendError, setGoBackendError] = useState<string | null>(null);
  const [apiLoading, setApiLoading] = useState(false);
  const [apiError, setApiError] = useState<string | null>(null);
  const [cloudData, setCloudData] = useState<CloudData | null>(null);
  const [cloudRange, setCloudRange] = useState<"week" | "month">("week");
  const [deviceMessages, setDeviceMessages] = useState<DeviceMessage[]>([]);
  const [deviceThread, setDeviceThread] = useState<DeviceMessage[]>([]);
  const [deviceMachines, setDeviceMachines] = useState<DeviceHeartbeat[]>([]);
  const [deviceSelfId, setDeviceSelfId] = useState("");
  const [deviceTarget, setDeviceTarget] = useState("");
  const [deviceMessageInput, setDeviceMessageInput] = useState("");
  const [messageSending, setMessageSending] = useState(false);
  const [deviceRemoving, setDeviceRemoving] = useState<string | null>(null);
  const [familyLinks, setFamilyLinks] = useState<FamilyLink[]>([]);
  const [parentLinks, setParentLinks] = useState<FamilyLink[]>([]);
  const [familySelected, setFamilySelected] = useState("");
  const [familyData, setFamilyData] = useState<FamilyData | null>(null);
  const [inviteEmail, setInviteEmail] = useState("");
  const [familyActionLoading, setFamilyActionLoading] = useState(false);
  const [insightSource, setInsightSource] = useState<"local" | "cloud">("local");
  const [insights, setInsights] = useState<InsightData | null>(null);
  const [chatMessages, setChatMessages] = useState<ChatMessage[]>([]);
  const [chatInput, setChatInput] = useState("");
  const [aiLoading, setAiLoading] = useState(false);
  const [loginEmail, setLoginEmail] = useState("");
  const [loginPassword, setLoginPassword] = useState("");
  const [loginLoading, setLoginLoading] = useState(false);
  const [oauthPending, setOAuthPending] = useState(false);
  const [limitProcessName, setLimitProcessName] = useState("");
  const [limitMinutes, setLimitMinutes] = useState("");
  const [limitBlocked, setLimitBlocked] = useState(false);

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    localStorage.setItem("jitdashboard-theme", theme);
  }, [theme]);

  useEffect(() => {
    localStorage.setItem("jitdashboard-daily-goal-minutes", String(dailyGoalMinutes));
  }, [dailyGoalMinutes]);

  useEffect(() => {
    let active = true;
    let attempts = 0;
    let retryTimer: number | undefined;
    const loadAuthStatus = async () => {
      try {
        const status = await goApi<GoAuth>("/api/auth/status");
        if (!active) return;
        setGoAuth(status);
        setGoBackendError(null);
      } catch (error) {
        if (!active) return;
        if (isTauriRuntime && attempts < 20) {
          attempts += 1;
          retryTimer = window.setTimeout(() => void loadAuthStatus(), 250);
          return;
        }
        setGoBackendError(String(error));
        setGoAuth(null);
      }
    };
    void loadAuthStatus();
    return () => {
      active = false;
      if (retryTimer !== undefined) window.clearTimeout(retryTimer);
    };
  }, [refreshToken]);

  useEffect(() => {
    if (!oauthPending) return;
    let active = true;
    let attempts = 0;
    const timer = window.setInterval(() => {
      attempts += 1;
      goApi<GoAuth>("/api/auth/status").then((status) => {
        if (!active) return;
        if (status.logged_in) {
          setGoAuth(status);
          setGoBackendError(null);
          setOAuthPending(false);
        } else if (attempts >= 90) {
          setOAuthPending(false);
        }
      }).catch((error: unknown) => {
        if (!active) return;
        setOAuthPending(false);
        setApiError(String(error));
      });
    }, 2000);
    return () => {
      active = false;
      window.clearInterval(timer);
    };
  }, [oauthPending]);

  useEffect(() => {
    let active = true;
    const loadData = async () => {
      if (!isTauriRuntime) {
        setDataError(null);
        setLoading(false);
        return;
      }
      try {
        const result = await invoke<LocalDashboardData>("get_local_dashboard_data");
        if (!active) return;
        setData(result);
        setDataError(null);
      } catch (error) {
        if (!active) return;
        setDataError(String(error));
      } finally {
        if (active) setLoading(false);
      }
    };
    void loadData();
    const interval = window.setInterval(() => void loadData(), 30_000);
    return () => {
      active = false;
      window.clearInterval(interval);
    };
  }, [refreshToken]);

  useEffect(() => {
    let active = true;
    const loadApiView = async () => {
      setApiError(null);
      if (["cloud", "devices", "limits", "parental"].includes(view) && goAuth?.logged_in !== true) {
        return;
      }
      if (view === "cloud") {
        setApiLoading(true);
        try {
          const query = `?range=${cloudRange}`;
          const [apps, daily, recent] = await Promise.all([
            goApi<{ apps: CloudData["apps"] }>(`/api/cloud/summary${query}`),
            goApi<{ days: string[]; totals: number[] }>(`/api/cloud/daily${query}`),
            goApi<{ records: CloudData["records"] }>("/api/cloud/recent"),
          ]);
          if (active) setCloudData({ ...apps, ...daily, ...recent });
        } catch (error) {
          if (active) setApiError(String(error));
        } finally {
          if (active) setApiLoading(false);
        }
      } else if (view === "devices") {
        setApiLoading(true);
        try {
          const [inbox, machines] = await Promise.all([
            goApi<{ messages: DeviceMessage[] }>("/api/inbox"),
            goApi<{ machines: DeviceHeartbeat[]; self_device_id: string }>("/api/machines"),
          ]);
          if (active) {
            setDeviceMessages(inbox.messages ?? []);
            setDeviceMachines(machines.machines ?? []);
            setDeviceSelfId(machines.self_device_id ?? "");
            if (!machines.machines?.some((machine) => machine.device_id === deviceTarget)) {
              setDeviceTarget(machines.machines?.find((machine) => machine.device_id !== machines.self_device_id)?.device_id ?? "");
            }
          }
        } catch (error) {
          if (active) setApiError(String(error));
        } finally {
          if (active) setApiLoading(false);
        }
      } else if (view === "parental" || view === "limits") {
        setApiLoading(true);
        try {
          const [children, parents] = await Promise.all([
            goApi<{ links: FamilyLink[] }>("/api/parent/children"),
            view === "parental"
              ? goApi<{ links: FamilyLink[] }>("/api/parent/parents")
              : Promise.resolve({ links: [] as FamilyLink[] }),
          ]);
          if (!active) return;
          const links = children.links ?? [];
          setFamilyLinks(links);
          setParentLinks(parents.links ?? []);
          if (!links.some((link) => link.other_user_id === familySelected && link.status === "approved")) {
            setFamilySelected(links.find((link) => link.status === "approved")?.other_user_id ?? "");
          }
        } catch (error) {
          if (active) setApiError(String(error));
        } finally {
          if (active) setApiLoading(false);
        }
      }
    };
    void loadApiView();
    return () => { active = false; };
  }, [view, refreshToken, cloudRange, familySelected, goAuth]);

  useEffect(() => {
    if (view !== "devices" || !deviceTarget || goAuth?.logged_in !== true) {
      setDeviceThread([]);
      return;
    }
    let active = true;
    goApi<{ messages: DeviceMessage[] }>(`/api/thread?device_id=${encodeURIComponent(deviceTarget)}`)
      .then((result) => {
        if (active) setDeviceThread(result.messages ?? []);
      })
      .catch((error: unknown) => {
        if (active) setApiError(String(error));
      });
    return () => { active = false; };
  }, [view, deviceTarget, refreshToken, goAuth]);

  useEffect(() => {
    if ((view !== "parental" && view !== "limits") || !familySelected || goAuth?.logged_in !== true) {
      setFamilyData(null);
      return;
    }
    let active = true;
    setApiLoading(true);
    setApiError(null);
    const child = encodeURIComponent(familySelected);
    Promise.all([
      goApi<{ apps: FamilyData["apps"]; recent: FamilyData["recent"] }>(`/api/parent/child-summary?child_id=${child}`),
      goApi<{ machines: FamilyData["machines"] }>(`/api/parent/child-heartbeat?child_id=${child}`),
      goApi<{ limits: FamilyData["limits"] }>(`/api/parent/limits?child_id=${child}`),
    ]).then(([summary, heartbeat, limits]) => {
      if (active) setFamilyData({ links: familyLinks, ...summary, machines: heartbeat.machines ?? [], limits: limits.limits ?? [] });
    }).catch((error: unknown) => {
      if (active) setApiError(String(error));
    }).finally(() => {
      if (active) setApiLoading(false);
    });
    return () => { active = false; };
  }, [view, familySelected, familyLinks, refreshToken, goAuth]);

  useEffect(() => {
    if (focusSeconds === null) return;
    if (focusSeconds <= 0) {
      setFocusSeconds(null);
      return;
    }
    const timer = window.setTimeout(() => setFocusSeconds(focusSeconds - 1), 1000);
    return () => window.clearTimeout(timer);
  }, [focusSeconds]);

  const dateLabel = new Intl.DateTimeFormat("vi-VN", {
    weekday: "long",
    day: "2-digit",
    month: "long",
    year: "numeric",
  }).format(new Date());
  const countdown = focusSeconds === null
    ? `${String(focusMinutes).padStart(2, "0")}:00`
    : `${String(Math.floor(focusSeconds / 60)).padStart(2, "0")}:${String(focusSeconds % 60).padStart(2, "0")}`;
  const available = data?.available === true;
  const todaySeconds = data?.todaySeconds ?? 0;
  const yesterdaySeconds = data?.yesterdaySeconds ?? 0;
  const comparisonLabel = !available ? "Chưa có dữ liệu"
    : todaySeconds === yesterdaySeconds ? "Bằng hôm qua"
      : todaySeconds > yesterdaySeconds ? "Tăng so với hôm qua" : "Giảm so với hôm qua";
  const maxHourSeconds = Math.max(0, ...(data?.hourlyTotals ?? []));
  const maxDaySeconds = Math.max(0, ...(data?.dailyTotals.map((day) => day.totalSeconds) ?? []));
  const maxAppSeconds = Math.max(0, ...(data?.appUsage.map((app) => app.totalSeconds) ?? []));
  const appUsage = (data?.appUsage ?? []).filter((item) =>
    item.processName.toLowerCase().includes(appSearch.toLowerCase()),
  );
  const localMidnight = (daysAgo: number) => {
    const date = new Date();
    date.setHours(0, 0, 0, 0);
    date.setDate(date.getDate() - daysAgo);
    return Math.floor(date.getTime() / 1000);
  };
  const historyStart = historyPeriod === "today" ? localMidnight(0)
    : historyPeriod === "yesterday" ? localMidnight(1) : localMidnight(6);
  const historyEnd = historyPeriod === "yesterday" ? localMidnight(0) : localMidnight(-1);
  const historyActivities = (data?.recentActivities ?? []).filter((activity) =>
    activity.startTime >= historyStart && activity.startTime < historyEnd,
  );
  const refresh = () => setRefreshToken((token) => token + 1);
  const cloudActivities: Activity[] = (cloudData?.records ?? []).map((activity) => ({
    processName: activity.process_name,
    windowTitle: activity.window_title,
    durationSeconds: activity.duration_seconds,
    startTime: activity.start_time,
    endTime: activity.end_time,
  }));
  const familyActivities: Activity[] = (familyData?.recent ?? []).map((activity) => ({
    processName: activity.process_name,
    windowTitle: activity.window_title,
    durationSeconds: activity.duration_seconds,
    startTime: activity.start_time,
    endTime: activity.end_time,
  }));
  const selectedFamilyLink = familyLinks.find((link) => link.other_user_id === familySelected);
  const runLogin = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    setLoginLoading(true);
    setApiError(null);
    try {
      await goApi("/api/auth/login", {
        method: "POST",
        body: JSON.stringify({ email: loginEmail, password: loginPassword }),
      });
      setLoginPassword("");
      refresh();
    } catch (error) {
      setApiError(String(error));
    } finally {
      setLoginLoading(false);
    }
  };
  const runLogout = async () => {
    setApiError(null);
    try {
      await goApi("/api/auth/logout", { method: "POST" });
      setGoAuth({ logged_in: false, email: "" });
      setCloudData(null);
      setDeviceMessages([]);
      setFamilyData(null);
    } catch (error) {
      setApiError(String(error));
    }
  };
  const startOAuthLogin = async (provider: "google" | "azure") => {
    setApiError(null);
    setOAuthPending(true);
    try {
      const url = `${GO_API_BASE}/auth/oauth/start?provider=${provider}`;
      if (isTauriRuntime) {
        await openUrl(url);
      } else {
        window.open(url, "_blank", "noopener,noreferrer");
      }
    } catch (error) {
      setOAuthPending(false);
      setApiError(String(error));
    }
  };
  const generateInsights = async () => {
    setAiLoading(true);
    setApiError(null);
    try {
      const result = await goApi<InsightData>(`/api/local/insights?source=${insightSource}`);
      setInsights(result);
    } catch (error) {
      setApiError(String(error));
    } finally {
      setAiLoading(false);
    }
  };
  const sendChat = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const text = chatInput.trim();
    if (!text || aiLoading) return;
    const history = [...chatMessages, { role: "user" as const, text }];
    setChatMessages(history);
    setChatInput("");
    setAiLoading(true);
    setApiError(null);
    try {
      const result = await goApi<{ reply: string }>("/api/local/chat", {
        method: "POST",
        body: JSON.stringify({ history }),
      });
      setChatMessages([...history, { role: "model", text: result.reply }]);
    } catch (error) {
      setApiError(String(error));
    } finally {
      setAiLoading(false);
    }
  };
  const markRead = async (message: DeviceMessage) => {
    if (message.read_at) return;
    setApiError(null);
    try {
      await goApi("/api/inbox/read", {
        method: "POST",
        body: JSON.stringify({ id: message.id }),
      });
      setDeviceMessages((messages) => messages.map((item) =>
        item.id === message.id ? { ...item, read_at: new Date().toISOString() } : item,
      ));
      setDeviceThread((messages) => messages.map((item) =>
        item.id === message.id ? { ...item, read_at: new Date().toISOString() } : item,
      ));
    } catch (error) {
      setApiError(String(error));
    }
  };
  const sendDeviceMessage = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const payload = deviceMessageInput.trim();
    if (!deviceTarget || !payload || messageSending) return;
    setMessageSending(true);
    setApiError(null);
    try {
      await goApi("/api/send", {
        method: "POST",
        body: JSON.stringify({ target_device_id: deviceTarget, kind: "message", payload }),
      });
      setDeviceMessageInput("");
      refresh();
    } catch (error) {
      setApiError(String(error));
    } finally {
      setMessageSending(false);
    }
  };
  const removeDevice = async (deviceId: string) => {
    setDeviceRemoving(deviceId);
    setApiError(null);
    try {
      await goApi("/api/machines/remove", {
        method: "POST",
        body: JSON.stringify({ device_id: deviceId }),
      });
      refresh();
    } catch (error) {
      setApiError(String(error));
    } finally {
      setDeviceRemoving(null);
    }
  };
  const saveFamilyLimit = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (!familySelected || !limitProcessName.trim()) return;
    const minutes = limitMinutes.trim() ? Number(limitMinutes) : null;
    if (minutes !== null && (!Number.isInteger(minutes) || minutes < 1 || minutes > 1440)) {
      setApiError("Giới hạn cần từ 1 đến 1440 phút mỗi ngày.");
      return;
    }
    setApiError(null);
    try {
      await goApi("/api/parent/limits/set", {
        method: "POST",
        body: JSON.stringify({
          child_user_id: familySelected,
          process_name: limitProcessName.trim(),
          daily_limit_min: minutes,
          blocked: limitBlocked,
        }),
      });
      setLimitProcessName("");
      setLimitMinutes("");
      setLimitBlocked(false);
      refresh();
    } catch (error) {
      setApiError(String(error));
    }
  };
  const deleteFamilyLimit = async (limitId: number) => {
    if (!familySelected) return;
    setApiError(null);
    try {
      await goApi("/api/parent/limits/delete", {
        method: "POST",
        body: JSON.stringify({ id: limitId, child_user_id: familySelected }),
      });
      refresh();
    } catch (error) {
      setApiError(String(error));
    }
  };
  const familyAction = async (path: string, payload: object) => {
    setFamilyActionLoading(true);
    setApiError(null);
    try {
      await goApi(path, { method: "POST", body: JSON.stringify(payload) });
      refresh();
      return true;
    } catch (error) {
      setApiError(String(error));
      return false;
    } finally {
      setFamilyActionLoading(false);
    }
  };
  const inviteFamilyMember = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (await familyAction("/api/parent/invite", { child_email: inviteEmail.trim() })) setInviteEmail("");
  };
  const runExport = async (kind: "local" | "cloud") => {
    setApiError(null);
    try {
      if (kind === "local") {
        const period = historyPeriod === "week" ? "this_week" : historyPeriod;
        await downloadCsv(`/export/local.csv?period=${period}`, `justintime-${period}.csv`);
      } else {
        await downloadCsv(`/export/csv?range=${cloudRange}`, `justintime-cloud-${cloudRange}.csv`);
      }
    } catch (error) {
      setApiError(String(error));
    }
  };

  return (
    <div className="app-window">
      <div className="app-body">
        <aside className="sidebar">
          <div className="sidebar-brand"><img className="brand-mark" src={appLogo} alt="" /><div><strong>JustInTime</strong><small>Thời Gian Máy</small></div></div>
          <nav className="navigation" aria-label="Điều hướng chính">
            {navigation.map((item) => (
              <button
                type="button"
                key={item.id}
                className={`nav-item${view === item.id ? " active" : ""}`}
                aria-label={item.label}
                onClick={() => setView(item.id)}
                aria-current={view === item.id ? "page" : undefined}
              >
                <Icon name={item.icon} /><span>{item.label}</span>
              </button>
            ))}
          </nav>
          <div className="sidebar-bottom">
            <button type="button" className={`nav-item${view === "settings" ? " active" : ""}`} onClick={() => setView("settings")}>
              <Icon name="settings" /><span>Cài đặt</span>
            </button>
            <button type="button" className="nav-item theme-nav" aria-label="Đổi giao diện" onClick={() => setTheme(theme === "dark" ? "light" : "dark")}>
              <Icon name={theme === "dark" ? "sun" : "moon"} /><span>Giao diện sáng/tối</span>
            </button>
            <p>Thời gian sử dụng được ghi nhận trên thiết bị này.</p>
          </div>
        </aside>

        <main className="main-content">
          <div className="page-heading">
            <div><h1>{pageTitles[view]}</h1><p className="sub">{dateLabel}</p></div>
            <div className="heading-actions">
              <span className={`local-badge${available ? "" : " disconnected"}`}><span />{available ? "Cơ sở dữ liệu cục bộ" : "Chưa kết nối dữ liệu"}</span>
              <span className={`local-badge${goAuth?.logged_in ? "" : " disconnected"}`}><span />{goAuth?.logged_in ? `Đã đăng nhập ${goAuth.email}` : goBackendError ? "Backend Go ngoại tuyến" : "Chưa đăng nhập cloud"}</span>
              <button type="button" className="button refresh-button" onClick={refresh} aria-label="Làm mới dữ liệu">Làm mới</button>
            </div>
          </div>

          {dataError && <div className="data-notice error" role="alert">Không thể đọc dữ liệu từ backend: {dataError}</div>}
          {!isTauriRuntime && ["home", "apps", "history"].includes(view) && <div className="data-notice" role="status">Dữ liệu thiết bị cục bộ chỉ khả dụng khi chạy trong ứng dụng Tauri.</div>}
          {goBackendError && ["cloud", "insights", "devices", "parental", "limits", "settings"].includes(view) && <div className="data-notice error" role="alert">Không kết nối được Go backend tại {GO_API_BASE}: {goBackendError}. {isTauriRuntime ? "Hãy khởi động lại ứng dụng desktop rồi thử lại." : "Hãy khởi chạy Dashboard Go rồi thử làm mới."}</div>}
          {goAuth?.logged_in === false && ["cloud", "devices", "parental", "limits"].includes(view) && <div className="data-notice" role="status">Đăng nhập tài khoản trong <button type="button" className="text-button" onClick={() => setView("settings")}>Cài đặt</button> để sử dụng các tính năng này.</div>}
          {apiError && <div className="data-notice error" role="alert">{apiError}</div>}
          {apiLoading && <div className="data-notice" role="status">Đang tải dữ liệu từ Go backend…</div>}
          {!loading && !dataError && data && !available && (
            <div className="data-notice" role="status">Chưa tìm thấy dữ liệu hoạt động. Hãy chạy agent JustInTime; ứng dụng sẽ tự tải lại dữ liệu.</div>
          )}
          {loading && !data && <div className="data-notice" role="status">Đang tải dữ liệu hoạt động…</div>}

          {view === "home" && (
            <>
              <section className="grid hero">
                <article className="card goal-card">
                  <h2>Thời gian dùng máy hôm nay</h2>
                  <div className="ring" style={{ "--progress": `${Math.min(100, todaySeconds / (dailyGoalMinutes * 60) * 100)}%` } as React.CSSProperties}>
                    <div className="ring-content"><strong>{available ? formatDuration(todaySeconds) : "—"}</strong><small>mục tiêu {Math.floor(dailyGoalMinutes / 60)}h {String(dailyGoalMinutes % 60).padStart(2, "0")}m</small></div>
                  </div>
                  <div className="live"><span className={`status-dot${available ? "" : " offline"}`} /><span>{available ? `${data?.todayActivityCount ?? 0} lần ghi nhận hôm nay` : "Đang chờ dữ liệu từ agent"}</span></div>
                </article>
                <div className="hero-side">
                  <div className="grid stats">
                    <article className="card stat"><span>Hoạt động hôm nay</span><strong>{available ? data?.todayActivityCount : "—"}</strong><em>Lần ghi nhận</em></article>
                    <article className="card stat"><span>So với hôm qua</span><strong>{available ? formatDuration(Math.abs(todaySeconds - yesterdaySeconds)) : "—"}</strong><em>{comparisonLabel}</em></article>
                    <article className="card stat"><span>Ứng dụng hôm nay</span><strong>{available ? data?.applicationCount : "—"}</strong><em>Ứng dụng được ghi nhận</em></article>
                  </div>
                  <article className="card heat-card">
                    <h2>Khung giờ hoạt động</h2>
                    <div className="heat" aria-label="Thời gian hoạt động theo giờ">
                      {Array.from({ length: 24 }, (_, hour) => {
                        const seconds = data?.hourlyTotals[hour] ?? 0;
                        const intensity = maxHourSeconds > 0 ? seconds / maxHourSeconds : 0;
                        return <i key={hour} style={{ opacity: seconds > 0 ? 0.25 + intensity * 0.75 : 0.32 }} title={`${String(hour).padStart(2, "0")}:00 · ${available ? formatDuration(seconds) : "Chưa có dữ liệu"}`} />;
                      })}
                    </div>
                    <div className="heat-labels"><span>0h</span><span>6h</span><span>12h</span><span>18h</span><span>24h</span></div>
                  </article>
                </div>
              </section>

              <section className="card focus-card">
                <div><h2>Phiên tập trung</h2><p>Đặt khoảng thời gian tập trung và nghỉ ngơi.</p></div>
                <div className="focus-actions">
                  {focusSeconds === null && (
                    <select aria-label="Thời lượng tập trung" value={focusMinutes} onChange={(event) => setFocusMinutes(Number(event.target.value))}>
                      <option value={25}>25 phút</option><option value={50}>50 phút</option>
                    </select>
                  )}
                  <strong className={focusSeconds !== null ? "timer-running" : ""}>{countdown}</strong>
                  <button type="button" className="button primary" onClick={() => setFocusSeconds(focusSeconds === null ? focusMinutes * 60 : null)}>
                    {focusSeconds === null ? "Bắt đầu tập trung" : "Dừng phiên"}
                  </button>
                </div>
              </section>

              <section className="grid two">
                <article className="card">
                  <h2>7 ngày gần nhất</h2>
                  {available && data?.dailyTotals.length ? (
                    <div className="bars">
                      {data.dailyTotals.map((day) => {
                        const date = new Date(`${day.date}T00:00:00`);
                        const label = new Intl.DateTimeFormat("vi-VN", { weekday: "short" }).format(date);
                        const height = maxDaySeconds > 0 ? Math.max(day.totalSeconds > 0 ? 4 : 0, day.totalSeconds / maxDaySeconds * 100) : 0;
                        return <div className="bar" key={day.date} title={`${day.date}: ${formatDuration(day.totalSeconds)}`}><span>{(day.totalSeconds / 3600).toFixed(1)}h</span><i style={{ height: `${height}%` }} /><span>{label}</span></div>;
                      })}
                    </div>
                  ) : <EmptyState>{available ? "Chưa có hoạt động trong 7 ngày qua." : "Biểu đồ sẽ hiển thị khi có dữ liệu từ agent."}</EmptyState>}
                  <p className="chart-caption">Tổng thời gian sử dụng được đọc từ nhật ký trên thiết bị.</p>
                </article>
                <article className="card">
                  <h2>Ứng dụng dùng nhiều nhất</h2>
                  {available && data?.appUsage.length ? (
                    <div className="usage-list">
                      {data.appUsage.slice(0, 5).map((app) => (
                        <div className="usage-row" key={app.processName}>
                          <div><strong>{app.processName}</strong><span>{formatDuration(app.totalSeconds)}</span></div>
                          <div className="usage-track"><i style={{ width: `${maxAppSeconds > 0 ? Math.min(100, app.totalSeconds / maxAppSeconds * 100) : 0}%` }} /></div>
                        </div>
                      ))}
                    </div>
                  ) : <EmptyState>{available ? "Chưa có ứng dụng được ghi nhận hôm nay." : "Ứng dụng sẽ xuất hiện khi agent ghi nhận hoạt động."}</EmptyState>}
                </article>
              </section>

              <article className="card app-card">
                <div className="section-heading"><h2>Hoạt động gần đây</h2><button type="button" className="text-button" onClick={() => setView("history")}>Xem lịch sử <Icon name="arrow" /></button></div>
                {available ? <ActivityRows activities={(data?.recentActivities ?? []).slice(0, 5)} /> : <EmptyState>Hoạt động sẽ xuất hiện tại đây sau khi agent ghi nhận dữ liệu.</EmptyState>}
              </article>
            </>
          )}

          {view === "apps" && (
            <article className="card page-card">
              <div className="section-heading"><div><h2>Ứng dụng hôm nay</h2><p>Thời gian tổng hợp từ nhật ký hoạt động trên thiết bị.</p></div><label className="search-box"><Icon name="search" /><input type="search" placeholder="Tìm ứng dụng" value={appSearch} onChange={(event) => setAppSearch(event.target.value)} /></label></div>
              {available && appUsage.length ? (
                <div className="usage-list app-page-list">
                  {appUsage.map((app) => (
                    <div className="usage-row" key={app.processName}>
                      <div><strong>{app.processName}</strong><span>{formatDuration(app.totalSeconds)}</span></div>
                      <div className="usage-track"><i style={{ width: `${Math.min(100, app.totalSeconds / Math.max(1, data?.appUsage[0]?.totalSeconds ?? 1) * 100)}%` }} /></div>
                    </div>
                  ))}
                </div>
              ) : <EmptyState>{available ? "Không tìm thấy ứng dụng phù hợp hôm nay." : "Chưa có dữ liệu ứng dụng. Hãy chạy agent JustInTime."}</EmptyState>}
            </article>
          )}
          {view === "history" && (
            <article className="card page-card">
              <div className="section-heading"><div><h2>Lịch sử hoạt động</h2><p>Tối đa 100 hoạt động gần nhất từ cơ sở dữ liệu cục bộ.</p></div><div className="inline-actions"><select aria-label="Khoảng thời gian" value={historyPeriod} onChange={(event) => setHistoryPeriod(event.target.value as typeof historyPeriod)}><option value="today">Hôm nay</option><option value="yesterday">Hôm qua</option><option value="week">7 ngày qua</option></select><button type="button" className="button" onClick={() => void runExport("local")}>Xuất CSV</button><button type="button" className="button" onClick={() => window.print()}>In</button></div></div>
              {available ? <ActivityRows activities={historyActivities} /> : <EmptyState>Chưa có lịch sử hoạt động. Hãy chạy agent JustInTime.</EmptyState>}
            </article>
          )}
          {view === "limits" && (
            <article className="card page-card">
              <div className="section-heading"><div><h2>Giới hạn ứng dụng gia đình</h2><p>Giới hạn được quản lý bởi phụ huynh trong tài khoản Go.</p></div>
                <select aria-label="Thành viên gia đình" value={familySelected} onChange={(event) => setFamilySelected(event.target.value)}>
                  <option value="">Chọn thành viên</option>{familyLinks.filter((link) => link.status === "approved").map((link) => <option key={link.id} value={link.other_user_id}>{link.other_email}</option>)}
                </select>
              </div>
              {familySelected && selectedFamilyLink?.permission_level === "full" && <form className="limit-form" onSubmit={(event) => void saveFamilyLimit(event)}>
                <label>Ứng dụng<input required value={limitProcessName} onChange={(event) => setLimitProcessName(event.target.value)} placeholder="Ví dụ: browser.exe" /></label>
                <label>Phút mỗi ngày<input type="number" min="1" max="1440" value={limitMinutes} onChange={(event) => setLimitMinutes(event.target.value)} placeholder="Để trống nếu không giới hạn" /></label>
                <label className="check-label"><input type="checkbox" checked={limitBlocked} onChange={(event) => setLimitBlocked(event.target.checked)} /> Chặn ứng dụng</label>
                <button type="submit" className="button primary">Lưu giới hạn</button>
              </form>}
              {familyData?.limits.length ? <div className="usage-list">{familyData.limits.map((limit) => <div className="setting-row" key={limit.id}><div><strong>{limit.process_name}</strong><small>{limit.blocked ? "Đang bị chặn" : limit.daily_limit_sec ? `${Math.floor(limit.daily_limit_sec / 60)} phút mỗi ngày` : "Không giới hạn thời gian"}</small></div><span className={limit.blocked ? "limit-blocked" : "muted"}>{limit.blocked ? "Chặn" : "Cho phép"}</span>{selectedFamilyLink?.permission_level === "full" && <button type="button" className="text-button" onClick={() => void deleteFamilyLimit(limit.id)}>Xóa</button>}</div>)}</div>
                : <EmptyState>{familySelected ? "Thành viên này chưa có giới hạn ứng dụng." : "Chọn một thành viên đã liên kết và được phê duyệt."}</EmptyState>}
              {selectedFamilyLink?.permission_level === "view_only" && <p className="data-notice">Tài khoản này chỉ cấp quyền xem; bạn không thể sửa giới hạn.</p>}
            </article>
          )}
          {view === "cloud" && (
            <article className="card page-card">
              <div className="section-heading"><div><h2>Hoạt động đã đồng bộ</h2><p>Tổng hợp từ các thiết bị trong tài khoản cloud.</p></div>
                <div className="inline-actions"><select aria-label="Khoảng đồng bộ" value={cloudRange} onChange={(event) => setCloudRange(event.target.value as typeof cloudRange)}><option value="week">7 ngày</option><option value="month">30 ngày</option></select><button type="button" className="button" onClick={() => void runExport("cloud")}>Xuất CSV</button><button type="button" className="button" onClick={() => window.print()}>In</button></div>
              </div>
              {!goAuth?.logged_in ? <EmptyState>Đăng nhập tài khoản trong Cài đặt để xem hoạt động đồng bộ.</EmptyState> : <>
                <div className="grid two cloud-summary">
                  <section className="card cloud-chart"><h2>Thời gian theo ngày</h2>
                    {cloudData?.days.length ? <div className="bars">{cloudData.days.map((day, index) => {
                      const seconds = cloudData.totals[index] ?? 0;
                      const max = Math.max(1, ...cloudData.totals);
                      return <div className="bar" key={day} title={`${day}: ${formatDuration(seconds)}`}><span>{(seconds / 3600).toFixed(1)}h</span><i style={{ height: `${Math.max(seconds ? 4 : 0, seconds / max * 100)}%` }} /><span>{new Intl.DateTimeFormat("vi-VN", { weekday: "short" }).format(new Date(`${day}T00:00:00`))}</span></div>;
                    })}</div> : <EmptyState>Chưa có dữ liệu đồng bộ trong khoảng này.</EmptyState>}
                  </section>
                  <section className="card"><h2>Ứng dụng dùng nhiều</h2>{cloudData?.apps.length ? <div className="usage-list">{cloudData.apps.slice(0, 8).map((app) => {
                    const max = Math.max(1, ...cloudData.apps.map((item) => item.total_seconds));
                    return <div className="usage-row" key={app.process_name}><div><strong>{app.process_name}</strong><span>{formatDuration(app.total_seconds)}</span></div><div className="usage-track"><i style={{ width: `${app.total_seconds / max * 100}%` }} /></div></div>;
                  })}</div> : <EmptyState>Chưa có thống kê ứng dụng.</EmptyState>}</section>
                </div>
                <section className="cloud-recent"><h2>Hoạt động gần đây</h2><ActivityRows activities={cloudActivities.slice(0, 20)} /></section>
              </>}
            </article>
          )}
          {view === "insights" && (
            <section className="grid two insight-layout">
              <article className="card page-card">
                <div className="section-heading"><div><h2>Phân tích sử dụng</h2><p>Chỉ gửi yêu cầu AI khi bạn chủ động bấm tạo.</p></div><select aria-label="Nguồn dữ liệu AI" value={insightSource} onChange={(event) => setInsightSource(event.target.value as typeof insightSource)}><option value="local">Thiết bị này</option><option value="cloud">Cloud đã đồng bộ</option></select></div>
                <button type="button" className="button primary" disabled={aiLoading} onClick={() => void generateInsights()}>{aiLoading ? "Đang phân tích…" : "Tạo AI Insights"}</button>
                {insights ? <div className="insight-result"><p>{insights.summary}</p><h2>Ứng dụng</h2><div className="usage-list">{insights.apps.map((app) => <div className="setting-row" key={app.process_name}><div><strong>{app.process_name}</strong><small>{app.note}</small></div><span className="muted">{app.category}</span></div>)}</div><h2>Gợi ý</h2><ul>{insights.recommendations.map((item, index) => <li key={`${index}-${item}`}>{item}</li>)}</ul></div> : <EmptyState>Nhập Gemini API key trong Settings của Go dashboard để sử dụng.</EmptyState>}
              </article>
              <article className="card page-card chat-card">
                <h2>Trò chuyện về thời gian sử dụng</h2>
                <p className="sub">AI chỉ nhận câu hỏi sau khi bạn gửi.</p>
                <div className="chat-messages" aria-live="polite">{chatMessages.map((message, index) => <div className={`chat-message ${message.role}`} key={`${index}-${message.text}`}><strong>{message.role === "user" ? "Bạn" : "AI"}</strong><p>{message.text}</p></div>)}{!chatMessages.length && <EmptyState>Ví dụ: “Tuần này mình dùng trình duyệt bao lâu?”</EmptyState>}</div>
                <form className="chat-form" onSubmit={(event) => void sendChat(event)}><input aria-label="Câu hỏi cho AI" value={chatInput} onChange={(event) => setChatInput(event.target.value)} placeholder="Nhập câu hỏi…" /><button type="submit" className="button primary" disabled={aiLoading || !chatInput.trim()}>Gửi</button></form>
              </article>
            </section>
          )}
          {view === "devices" && (
            <section className="grid two">
              <article className="card page-card"><h2>Thiết bị đã kết nối</h2><p className="sub">Thiết bị được đồng bộ qua tài khoản.</p>
                {deviceMachines.length ? <div className="activity-list">{deviceMachines.map((device) => {
                  const lastSeen = new Date(device.last_seen).getTime();
                  const stale = Number.isFinite(lastSeen) && Date.now() - lastSeen > 90_000;
                  return <div className="activity-row" key={device.device_id}>
                    <div className="activity-app"><strong>{device.hostname || device.device_id}</strong><span>{device.device_id}</span></div>
                    <div className="activity-time"><span>{device.last_seen ? new Date(device.last_seen).toLocaleString("vi-VN") : "Chưa có trạng thái"}</span></div>
                    {stale && device.device_id !== deviceSelfId && <button type="button" className="text-button" disabled={deviceRemoving === device.device_id} onClick={() => void removeDevice(device.device_id)}>{deviceRemoving === device.device_id ? "Đang xóa…" : "Xóa thiết bị cũ"}</button>}
                  </div>;
                })}</div> : <EmptyState>Chưa có thiết bị đồng bộ.</EmptyState>}
              </article>
              <article className="card page-card"><div className="section-heading"><h2>Cuộc trò chuyện thiết bị</h2>{deviceMachines.some((device) => device.device_id !== deviceSelfId) && <select aria-label="Cuộc trò chuyện với thiết bị" value={deviceTarget} onChange={(event) => setDeviceTarget(event.target.value)}>{deviceMachines.filter((device) => device.device_id !== deviceSelfId).map((device) => <option key={device.device_id} value={device.device_id}>{device.hostname || device.device_id}</option>)}</select>}</div><p className="sub">Tin nhắn được chuyển tiếp an toàn qua tài khoản của bạn.</p>
                {deviceThread.length ? <div className="message-list">{deviceThread.map((message) => <article className={`message-card${message.sender_device_id === deviceSelfId ? " sent-message" : ""}`} key={message.id}><div className="section-heading"><strong>{message.sender_device_id === deviceSelfId ? "Thiết bị này" : message.sender_device_id}</strong><span className="muted">{message.created_at ? new Date(message.created_at).toLocaleString("vi-VN") : ""}</span></div><p>{message.payload}</p></article>)}</div> : <EmptyState>{deviceTarget ? "Chưa có tin nhắn trong cuộc trò chuyện này." : "Chọn một thiết bị khác để bắt đầu trò chuyện."}</EmptyState>}
                {deviceTarget && <form className="chat-form" onSubmit={(event) => void sendDeviceMessage(event)}>
                  <input aria-label="Tin nhắn thiết bị" value={deviceMessageInput} onChange={(event) => setDeviceMessageInput(event.target.value)} placeholder="Soạn tin nhắn…" maxLength={1000} />
                  <button type="submit" className="button primary" disabled={messageSending || !deviceMessageInput.trim()}>{messageSending ? "Đang gửi…" : "Gửi"}</button>
                </form>}
                <h2 className="inbox-heading">Hộp thư đến</h2>
                {deviceMessages.length ? <div className="message-list">{deviceMessages.map((message) => <article className={`message-card${message.read_at ? "" : " unread"}`} key={message.id}><div className="section-heading"><strong>{message.sender_device_id}</strong><span className="muted">{message.created_at ? new Date(message.created_at).toLocaleString("vi-VN") : ""}</span></div><p>{message.payload}</p>{!message.read_at && <button type="button" className="text-button" onClick={() => void markRead(message)}>Đánh dấu đã đọc</button>}</article>)}</div> : <EmptyState>Hộp thư hiện chưa có tin nhắn.</EmptyState>}
              </article>
            </section>
          )}
          {view === "parental" && (
            <article className="card page-card">
              <div className="section-heading"><div><h2>Hoạt động thành viên gia đình</h2><p>Dữ liệu chỉ hiển thị với liên kết gia đình đã được phê duyệt.</p></div>
                <select aria-label="Thành viên gia đình" value={familySelected} onChange={(event) => setFamilySelected(event.target.value)}><option value="">Chọn thành viên</option>{familyLinks.filter((link) => link.status === "approved").map((link) => <option key={link.id} value={link.other_user_id}>{link.other_email}</option>)}</select>
              </div>
              {goAuth?.logged_in && <section className="family-links">
                <h2>Mời thành viên</h2>
                <form className="chat-form" onSubmit={(event) => void inviteFamilyMember(event)}>
                  <input aria-label="Email thành viên" type="email" required value={inviteEmail} onChange={(event) => setInviteEmail(event.target.value)} placeholder="Email tài khoản thành viên" />
                  <button type="submit" className="button primary" disabled={familyActionLoading || !inviteEmail.trim()}>Gửi lời mời</button>
                </form>
                <h2>Thành viên bạn quản lý</h2>
                {familyLinks.length ? <div className="message-list">{familyLinks.map((link) => <article className="message-card" key={link.id}>
                  <div className="section-heading"><div><strong>{link.other_email}</strong><span className="muted">{link.status === "approved" ? "Đã chấp thuận" : link.status === "pending" ? "Đang chờ" : "Đã thu hồi"}</span></div>
                    <div className="inline-actions">
                      {link.status === "approved" && <select aria-label={`Quyền của ${link.other_email}`} value={link.permission_level ?? "full"} disabled={familyActionLoading} onChange={(event) => void familyAction("/api/parent/permission", { id: link.id, permission_level: event.target.value })}><option value="full">Xem và quản lý</option><option value="view_only">Chỉ xem</option></select>}
                      {link.status !== "revoked" && <button type="button" className="text-button" disabled={familyActionLoading} onClick={() => void familyAction("/api/parent/revoke", { id: link.id })}>Thu hồi</button>}
                    </div>
                  </div>
                </article>)}</div> : <EmptyState>Chưa mời thành viên nào.</EmptyState>}
                <h2>Lời mời bạn nhận được</h2>
                {parentLinks.length ? <div className="message-list">{parentLinks.map((link) => <article className="message-card" key={link.id}>
                  <div className="section-heading"><div><strong>{link.other_email}</strong><span className="muted">{link.status === "approved" ? "Đã chấp thuận" : link.status === "pending" ? "Đang chờ bạn xác nhận" : "Đã thu hồi"}</span></div>
                    <div className="inline-actions">
                      {link.status === "pending" && <button type="button" className="button primary" disabled={familyActionLoading} onClick={() => void familyAction("/api/parent/approve", { id: link.id })}>Chấp thuận</button>}
                      {link.status !== "revoked" && <button type="button" className="text-button" disabled={familyActionLoading} onClick={() => void familyAction("/api/parent/revoke", { id: link.id })}>Thu hồi quyền truy cập</button>}
                    </div>
                  </div>
                </article>)}</div> : <EmptyState>Không có lời mời gia đình nào đang chờ.</EmptyState>}
              </section>}
              {familySelected && familyData ? <>
                <div className="grid stats family-status">{familyData.machines.map((machine) => {
                  const seen = new Date(machine.last_seen).getTime();
                  const online = Number.isFinite(seen) && Date.now() - seen < 90_000;
                  return <article className="card stat" key={machine.device_id}><span>{machine.hostname || machine.device_id}</span><strong className="device-presence">{online ? "Đang hoạt động" : "Ngoại tuyến"}</strong><em>{machine.last_seen ? new Date(machine.last_seen).toLocaleString("vi-VN") : "Chưa có lần kết nối gần đây"}</em></article>;
                })}</div>
                <div className="grid two cloud-summary"><section className="card"><h2>Ứng dụng hôm nay</h2>{familyData.apps.length ? <div className="usage-list">{familyData.apps.map((app) => <div className="usage-row" key={app.process_name}><div><strong>{app.process_name}</strong><span>{formatDuration(app.total_seconds)}</span></div></div>)}</div> : <EmptyState>Chưa ghi nhận hoạt động hôm nay.</EmptyState>}</section><section className="card"><h2>Giới hạn hiện tại</h2>{familyData.limits.length ? <div className="usage-list">{familyData.limits.map((limit) => <div className="setting-row" key={limit.id}><div><strong>{limit.process_name}</strong><small>{limit.blocked ? "Đang bị chặn" : limit.daily_limit_sec ? `${Math.floor(limit.daily_limit_sec / 60)} phút/ngày` : "Không giới hạn"}</small></div></div>)}</div> : <EmptyState>Chưa cấu hình giới hạn ứng dụng.</EmptyState>}</section></div>
                <section className="cloud-recent"><h2>Hoạt động gần đây</h2><ActivityRows activities={familyActivities.slice(0, 20)} /></section>
              </> : familyLinks.some((link) => link.status === "approved") ? <EmptyState>Đang tải thông tin thành viên…</EmptyState> : <EmptyState>Chọn thành viên đã chấp thuận để xem hoạt động. Bạn cũng có thể quản lý lời mời bên trên.</EmptyState>}
            </article>
          )}
          {view === "settings" && (
            <article className="card page-card settings-card">
              <h2>Cài đặt</h2>
              <div className="setting-row"><div><strong>Giao diện</strong><small>Chọn giao diện sáng hoặc tối.</small></div><button type="button" className="button" onClick={() => setTheme(theme === "dark" ? "light" : "dark")}>{theme === "dark" ? "Tối" : "Sáng"}</button></div>
              <div className="setting-row"><div><strong>Mục tiêu sử dụng hằng ngày</strong><small>Được lưu trên thiết bị này và dùng cho vòng tiến độ.</small></div><select aria-label="Mục tiêu sử dụng hằng ngày" value={dailyGoalMinutes} onChange={(event) => setDailyGoalMinutes(Number(event.target.value))}><option value={120}>2 giờ</option><option value={240}>4 giờ</option><option value={360}>6 giờ</option><option value={480}>8 giờ</option><option value={600}>10 giờ</option><option value={720}>12 giờ</option><option value={960}>16 giờ</option><option value={1440}>24 giờ</option></select></div>
              <section className="account-section">
                <h2>Tài khoản đồng bộ</h2>
                <p className="sub">Đăng nhập qua backend Go đang chạy tại {GO_API_BASE}. Mật khẩu không được lưu trong ứng dụng này.</p>
                {goAuth?.logged_in ? <div className="account-signed-in"><span>Đã đăng nhập bằng {goAuth.email}</span><button type="button" className="button" onClick={() => void runLogout()}>Đăng xuất</button></div> : <form className="login-form" onSubmit={(event) => void runLogin(event)}>
                  <label>Email<input type="email" autoComplete="username" required value={loginEmail} onChange={(event) => setLoginEmail(event.target.value)} /></label>
                  <label>Mật khẩu<input type="password" autoComplete="current-password" required value={loginPassword} onChange={(event) => setLoginPassword(event.target.value)} /></label>
                  <button type="submit" className="button primary" disabled={loginLoading || !loginEmail || !loginPassword}>{loginLoading ? "Đang đăng nhập…" : "Đăng nhập"}</button>
                </form>}
                {!goAuth?.logged_in && <div className="oauth-actions"><button type="button" className="button" disabled={oauthPending} onClick={() => startOAuthLogin("google")}>Tiếp tục với Google</button><button type="button" className="button" disabled={oauthPending} onClick={() => startOAuthLogin("azure")}>Tiếp tục với Microsoft</button></div>}
                {oauthPending && <p className="sub account-help" role="status">Hoàn tất đăng nhập trong cửa sổ trình duyệt vừa mở; dashboard sẽ tự cập nhật.</p>}
                <p className="sub account-help">Cài Gemini API key tại <a href={`${GO_API_BASE}/settings`} target="_blank" rel="noreferrer">Settings của Go dashboard</a>. AI Insights chỉ gửi dữ liệu khi bạn yêu cầu.</p>
              </section>
            </article>
          )}
        </main>
      </div>
    </div>
  );
}

export default App;
