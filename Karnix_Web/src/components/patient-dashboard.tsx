"use client";

import { useEffect, useMemo, useState } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";
import {
  Activity,
  ArrowRight,
  Bookmark,
  Check,
  CircleHelp,
  ClipboardList,
  HeartPulse,
  LayoutDashboard,
  LogOut,
  Menu,
  Search,
  ShieldCheck,
  ShoppingBag,
  Sparkles,
  Stethoscope,
  UserRound,
  X,
  type LucideIcon,
} from "lucide-react";
import { useAuth } from "@/components/auth-provider";
import PatientRecordAssistant from "@/components/patient-record-assistant";
import { api, ApiError, type RiskAssessment } from "@/lib/api";

type CareTool = {
  id: "assessment" | "records";
  title: string;
  description: string;
  category: "Assessments" | "Your records";
  label: string;
  icon: LucideIcon;
  tint: string;
  action: string;
};

const careTools: CareTool[] = [
  {
    id: "assessment",
    title: "Lung health assessment",
    description:
      "Record lifestyle and exposure information in a guided questionnaire. Results are for research only, not a diagnosis.",
    category: "Assessments",
    label: "Research tool",
    icon: Stethoscope,
    tint: "bg-[#e9f2e8] text-[#39724b]",
    action: "Open assessment",
  },
  {
    id: "records",
    title: "Ask about your records",
    description:
      "Get answers to questions about your saved lung-risk assessments, with the record used as the source.",
    category: "Your records",
    label: "Private record lookup",
    icon: ClipboardList,
    tint: "bg-[#f8eee1] text-[#a66d32]",
    action: "Ask a question",
  },
];

const categories = ["All tools", "Assessments", "Your records"] as const;
type Category = (typeof categories)[number];

export default function PatientDashboard() {
  const router = useRouter();
  const { user, loading, logout } = useAuth();
  const [assessments, setAssessments] = useState<RiskAssessment[]>([]);
  const [recordsLoading, setRecordsLoading] = useState(true);
  const [recordsError, setRecordsError] = useState("");
  const [recordsRefresh, setRecordsRefresh] = useState(0);
  const [query, setQuery] = useState("");
  const [category, setCategory] = useState<Category>("All tools");
  const [savedTools, setSavedTools] = useState<string[]>([]);
  const [activeTool, setActiveTool] = useState<CareTool["id"] | null>(null);
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false);
  const [logoutError, setLogoutError] = useState("");

  useEffect(() => {
    if (!loading && !user) router.replace("/login");
  }, [loading, router, user]);

  useEffect(() => {
    if (activeTool === "records") {
      document
        .getElementById("record-assistant")
        ?.scrollIntoView({ behavior: "smooth" });
    }
  }, [activeTool]);

  useEffect(() => {
    if (!user) return;

    let active = true;
    api
      .listRiskAssessments()
      .then((items) => {
        if (active) setAssessments(items);
      })
      .catch((caught: unknown) => {
        if (!active) return;
        setRecordsError(
          caught instanceof ApiError
            ? caught.message
            : "Unable to load your saved assessments. Please try again.",
        );
      })
      .finally(() => {
        if (active) setRecordsLoading(false);
      });

    return () => {
      active = false;
    };
  }, [recordsRefresh, user]);

  const filteredTools = useMemo(() => {
    const normalizedQuery = query.trim().toLowerCase();
    return careTools.filter((tool) => {
      const matchesCategory =
        category === "All tools" || tool.category === category;
      const matchesQuery =
        !normalizedQuery ||
        `${tool.title} ${tool.description} ${tool.category}`
          .toLowerCase()
          .includes(normalizedQuery);
      return matchesCategory && matchesQuery;
    });
  }, [category, query]);

  async function handleLogout() {
    setLogoutError("");
    try {
      await logout();
      router.replace("/login");
    } catch {
      setLogoutError("Unable to log out right now. Please try again.");
    }
  }

  function openTool(toolId: CareTool["id"]) {
    setActiveTool(toolId);
    if (toolId === "assessment") router.push("/assessment");
  }

  if (loading || !user) {
    return (
      <main className="flex min-h-screen items-center justify-center bg-[#f5f6f1]">
        <span
          className="h-8 w-8 animate-spin rounded-full border-2 border-[#d9e1d7] border-t-[#416d4e]"
          aria-label="Loading dashboard"
        />
      </main>
    );
  }

  const firstName = user.name.trim().split(/\s+/)[0] || "there";
  const initials = user.name
    .trim()
    .split(/\s+/)
    .slice(0, 2)
    .map((part) => part[0])
    .join("")
    .toUpperCase();
  const today = new Intl.DateTimeFormat("en", {
    weekday: "long",
    month: "long",
    day: "numeric",
  }).format(new Date());

  return (
    <main className="min-h-screen bg-[#f5f6f1] text-[#25332a]">
      <div className="flex min-h-screen">
        <aside
          className={`fixed inset-y-0 left-0 z-30 flex w-[258px] flex-col border-r border-[#e9ece5] bg-white px-5 py-6 transition-transform lg:static lg:translate-x-0 ${mobileMenuOpen ? "translate-x-0" : "-translate-x-full"}`}
          aria-label="Main navigation"
        >
          <Link href="/welcome" className="flex items-center gap-3 px-2">
            <span className="flex h-10 w-10 items-center justify-center rounded-[14px] bg-[#315c40] text-white">
              <HeartPulse className="h-5 w-5" />
            </span>
            <span>
              <span className="block text-[19px] font-semibold tracking-[-0.04em] text-[#253b2d]">
                karnix
              </span>
              <span className="block text-[10px] font-semibold uppercase tracking-[0.18em] text-[#839187]">
                Patient care
              </span>
            </span>
          </Link>

          <p className="mb-3 mt-10 px-3 text-[10px] font-bold uppercase tracking-[0.16em] text-[#a1aaa2]">
            Workspace
          </p>
          <nav className="space-y-1.5">
            <a
              href="#dashboard"
              onClick={() => setMobileMenuOpen(false)}
              className="flex items-center gap-3 rounded-xl bg-[#eef4ee] px-3.5 py-3 text-sm font-semibold text-[#315c40]"
            >
              <LayoutDashboard className="h-[18px] w-[18px]" />
              Dashboard
            </a>
            <a
              href="#records"
              onClick={() => setMobileMenuOpen(false)}
              className="flex items-center gap-3 rounded-xl px-3.5 py-3 text-sm font-medium text-[#758178] transition hover:bg-[#f6f8f4] hover:text-[#315c40]"
            >
              <ClipboardList className="h-[18px] w-[18px]" />
              My records
            </a>
            <a
              href="#tools"
              onClick={() => setMobileMenuOpen(false)}
              className="flex items-center gap-3 rounded-xl px-3.5 py-3 text-sm font-medium text-[#758178] transition hover:bg-[#f6f8f4] hover:text-[#315c40]"
            >
              <ShoppingBag className="h-[18px] w-[18px]" />
              Care tools
            </a>
          </nav>

          <div className="mt-auto rounded-2xl bg-[#f5f7f2] p-4">
            <div className="mb-3 flex h-9 w-9 items-center justify-center rounded-xl bg-white text-[#55745a] shadow-sm">
              <ShieldCheck className="h-[18px] w-[18px]" />
            </div>
            <p className="text-sm font-semibold text-[#3a493d]">
              Your information, protected
            </p>
            <p className="mt-1.5 text-xs leading-5 text-[#869087]">
              Your account and saved assessments stay private to you.
            </p>
          </div>

          <div className="mt-5 border-t border-[#edf0ea] pt-4">
            <div className="flex items-center gap-3 px-1">
              <span className="flex h-10 w-10 shrink-0 items-center justify-center rounded-full bg-[#e9efe7] text-xs font-bold text-[#47634b]">
                {initials}
              </span>
              <span className="min-w-0 flex-1">
                <span className="block truncate text-sm font-semibold text-[#39463c]">
                  {user.name}
                </span>
                <span className="block truncate text-xs text-[#929b92]">
                  {user.email ?? user.username ?? "Patient account"}
                </span>
              </span>
              <button
                type="button"
                onClick={handleLogout}
                className="rounded-lg p-2 text-[#8a958c] transition hover:bg-white hover:text-[#315c40]"
                aria-label="Log out"
              >
                <LogOut className="h-4 w-4" />
              </button>
            </div>
          </div>
        </aside>

        {mobileMenuOpen && (
          <button
            type="button"
            aria-label="Close navigation"
            className="fixed inset-0 z-20 bg-[#17221a]/25 lg:hidden"
            onClick={() => setMobileMenuOpen(false)}
          />
        )}

        <section className="min-w-0 flex-1">
          <header className="sticky top-0 z-10 flex h-[76px] items-center justify-between border-b border-[#e9ece5] bg-[#f8f9f6]/95 px-5 backdrop-blur sm:px-8 xl:px-11">
            <div className="flex min-w-0 items-center gap-3">
              <button
                type="button"
                className="rounded-lg p-2 text-[#647268] hover:bg-white lg:hidden"
                onClick={() => setMobileMenuOpen(true)}
                aria-label="Open navigation"
              >
                <Menu className="h-5 w-5" />
              </button>
              <label className="flex w-full max-w-[400px] items-center gap-2.5 rounded-xl border border-[#e8ebe5] bg-white px-3.5 py-2.5 text-[#8b968c] focus-within:border-[#a8b9a8]">
                <Search className="h-4 w-4 shrink-0" />
                <input
                  value={query}
                  onChange={(event) => setQuery(event.target.value)}
                  placeholder="Search care tools..."
                  className="w-full bg-transparent text-sm text-[#39483d] outline-none placeholder:text-[#a5ada5]"
                  aria-label="Search care tools"
                />
                <span className="hidden rounded-md border border-[#edf0eb] px-1.5 py-0.5 text-[10px] font-medium text-[#a3aaa3] sm:block">
                  ⌘ K
                </span>
              </label>
            </div>
            <div className="ml-3 flex shrink-0 items-center gap-3">
              <a
                href="#tools"
                className="hidden items-center gap-2 rounded-xl border border-[#e8ebe5] bg-white px-3 py-2 text-xs font-semibold text-[#67766a] transition hover:border-[#c6d2c5] sm:flex"
              >
                <Bookmark className="h-4 w-4" />
                Saved tools
                <span className="flex h-5 min-w-5 items-center justify-center rounded-full bg-[#edf4ed] px-1.5 text-[10px] text-[#41684a]">
                  {savedTools.length}
                </span>
              </a>
              <span className="flex h-9 w-9 items-center justify-center rounded-full bg-[#e7eee5] text-[11px] font-bold text-[#46654b] sm:hidden">
                {initials}
              </span>
            </div>
          </header>

          <div className="mx-auto max-w-[1440px] px-5 pb-12 pt-7 sm:px-8 sm:pt-9 xl:px-11">
            {logoutError && (
              <p
                role="alert"
                className="mb-4 rounded-xl border border-red-200 bg-red-50 px-4 py-3 text-sm text-red-800"
              >
                {logoutError}
              </p>
            )}
            <section
              id="dashboard"
              className="scroll-mt-24"
              aria-labelledby="welcome-title"
            >
              <div className="mb-6 flex flex-wrap items-end justify-between gap-3">
                <div>
                  <p className="text-[11px] font-bold uppercase tracking-[0.17em] text-[#87948a]">
                    {today}
                  </p>
                  <h1
                    id="welcome-title"
                    className="mt-1.5 text-[27px] font-semibold tracking-[-0.04em] text-[#2f4134] sm:text-[32px]"
                  >
                    Good to see you, {firstName}{" "}
                    <span aria-hidden="true">✦</span>
                  </h1>
                  <p className="mt-1 text-sm text-[#859087]">
                    Your personal health space, all in one place.
                  </p>
                </div>
                <button
                  type="button"
                  onClick={() => router.push("/assessment")}
                  className="inline-flex items-center gap-2 rounded-xl bg-[#315c40] px-4 py-2.5 text-sm font-semibold text-white shadow-[0_6px_14px_rgba(49,92,64,0.16)] transition hover:bg-[#274d35]"
                >
                  <Sparkles className="h-4 w-4" />
                  Start an assessment
                  <ArrowRight className="h-4 w-4" />
                </button>
              </div>

              <div className="relative overflow-hidden rounded-[22px] bg-[#294d37] px-6 py-7 text-white sm:px-9 sm:py-8">
                <div className="absolute -right-10 -top-24 h-72 w-72 rounded-full border border-white/[0.08]" />
                <div className="absolute -right-2 -top-16 h-56 w-56 rounded-full border border-white/[0.08]" />
                <div className="absolute bottom-[-135px] right-[20%] h-64 w-64 rounded-full bg-[#7a9e6d]/20 blur-3xl" />
                <div className="relative z-[1] grid items-center gap-7 md:grid-cols-[1fr_auto]">
                  <div className="max-w-[630px]">
                    <span className="inline-flex items-center gap-1.5 rounded-full border border-white/15 bg-white/[0.08] px-3 py-1.5 text-[10px] font-semibold uppercase tracking-[0.12em] text-[#d4e1d2]">
                      <HeartPulse className="h-3.5 w-3.5" />
                      Your care, your pace
                    </span>
                    <h2 className="mt-4 text-2xl font-semibold leading-tight tracking-[-0.035em] sm:text-[30px]">
                      Small steps make a meaningful difference.
                    </h2>
                    <p className="mt-2 max-w-[530px] text-sm leading-6 text-[#d0ddcf]">
                      Explore your health tools, keep your information
                      organized, and learn what’s in your saved records.
                    </p>
                    <button
                      type="button"
                      onClick={() => router.push("/assessment")}
                      className="mt-5 inline-flex items-center gap-2 rounded-xl bg-[#f1f3e9] px-4 py-2.5 text-sm font-semibold text-[#31543a] transition hover:bg-white"
                    >
                      Explore your health profile
                      <ArrowRight className="h-4 w-4" />
                    </button>
                  </div>
                  <div className="hidden h-[142px] w-[142px] items-center justify-center rounded-full border border-white/15 bg-white/[0.07] md:flex">
                    <div className="flex h-[104px] w-[104px] items-center justify-center rounded-full border border-white/20 bg-[#315c40] shadow-[0_16px_50px_rgba(12,28,17,0.18)]">
                      <HeartPulse
                        className="h-12 w-12 text-[#d4e2cb]"
                        strokeWidth={1.3}
                      />
                    </div>
                  </div>
                </div>
              </div>
            </section>

            <section aria-label="Account overview" className="mt-5 grid gap-3 sm:grid-cols-3">
              <OverviewCard
                icon={ClipboardList}
                label="Saved assessments"
                value={recordsLoading ? "—" : String(assessments.length)}
                note={recordsLoading ? "Loading your records" : assessments.length === 1 ? "One personal record" : "Your personal records"}
                color="green"
              />
              <OverviewCard
                icon={UserRound}
                label="Patient profile"
                value="Your account"
                note={user.email ?? user.username ?? "Profile details"}
                color="sand"
              />
              <OverviewCard
                icon={ShieldCheck}
                label="Private workspace"
                value="Just for you"
                note="Personal account access"
                color="blue"
              />
            </section>

            <div className="mt-9 grid items-start gap-6 xl:grid-cols-[minmax(0,1.6fr)_minmax(300px,0.82fr)]">
              <section id="tools" className="scroll-mt-24">
                <div className="flex flex-wrap items-end justify-between gap-3">
                  <div>
                    <div className="flex items-center gap-2">
                      <ShoppingBag className="h-[17px] w-[17px] text-[#6b826e]" />
                      <h2 className="text-[19px] font-semibold tracking-[-0.025em] text-[#334438]">
                        Care tools
                      </h2>
                    </div>
                    <p className="mt-1 text-xs text-[#909a90]">
                      Thoughtful tools to support your health journey.
                    </p>
                  </div>
                  <span className="text-xs font-medium text-[#929a91]">
                    {filteredTools.length}{" "}
                    {filteredTools.length === 1 ? "tool" : "tools"}
                  </span>
                </div>

                <div
                  className="mt-4 flex gap-2 overflow-x-auto pb-1"
                  role="group"
                  aria-label="Filter care tools"
                >
                  {categories.map((item) => (
                    <button
                      key={item}
                      type="button"
                      onClick={() => setCategory(item)}
                      aria-pressed={category === item}
                      className={`whitespace-nowrap rounded-full px-3.5 py-2 text-xs font-semibold transition ${category === item ? "bg-[#315c40] text-white" : "border border-[#e7ebe4] bg-white text-[#7b877d] hover:border-[#cdd8ca] hover:text-[#45634a]"}`}
                    >
                      {item}
                    </button>
                  ))}
                </div>

                {filteredTools.length > 0 ? (
                  <div className="mt-4 grid gap-4 lg:grid-cols-2">
                    {filteredTools.map((tool, index) => {
                      const Icon = tool.icon;
                      const isSaved = savedTools.includes(tool.id);
                      return (
                        <article
                          key={tool.id}
                          className="group overflow-hidden rounded-[18px] border border-[#e9ece5] bg-white transition duration-200 hover:-translate-y-0.5 hover:border-[#d5dfd2] hover:shadow-[0_14px_35px_rgba(35,57,39,0.07)]"
                        >
                          <div
                            className={`relative flex h-[142px] items-center justify-center overflow-hidden ${index === 0 ? "bg-[#edf3eb]" : "bg-[#f7f0e7]"}`}
                          >
                            <div
                              className={`absolute -right-4 -top-12 h-40 w-40 rounded-full ${index === 0 ? "bg-[#dce9d7]" : "bg-[#efe3d4]"}`}
                            />
                            <div
                              className={`absolute -bottom-16 -left-3 h-36 w-36 rounded-full ${index === 0 ? "bg-[#e3ecdf]" : "bg-[#f1e8dc]"}`}
                            />
                            <div
                              className={`relative flex h-[76px] w-[76px] items-center justify-center rounded-[24px] shadow-[0_12px_24px_rgba(42,65,45,0.08)] ${tool.tint}`}
                            >
                              <Icon className="h-9 w-9" strokeWidth={1.5} />
                            </div>
                            <span className="absolute left-4 top-4 rounded-full border border-white/70 bg-white/75 px-2.5 py-1 text-[10px] font-semibold text-[#617064] backdrop-blur">
                              {tool.label}
                            </span>
                            <button
                              type="button"
                              onClick={() =>
                                setSavedTools((current) =>
                                  isSaved
                                    ? current.filter((id) => id !== tool.id)
                                    : [...current, tool.id],
                                )
                              }
                              className={`absolute right-4 top-4 flex h-8 w-8 items-center justify-center rounded-full bg-white/85 transition hover:bg-white ${isSaved ? "text-[#315c40]" : "text-[#8e998e]"}`}
                              aria-label={
                                isSaved
                                  ? `Remove ${tool.title} from saved tools`
                                  : `Save ${tool.title}`
                              }
                            >
                              {isSaved ? (
                                <Check className="h-4 w-4" />
                              ) : (
                                <Bookmark className="h-4 w-4" />
                              )}
                            </button>
                          </div>
                          <div className="p-5">
                            <span className="text-[10px] font-bold uppercase tracking-[0.13em] text-[#98a197]">
                              {tool.category}
                            </span>
                            <h3 className="mt-1.5 text-[16px] font-semibold tracking-[-0.02em] text-[#35443a]">
                              {tool.title}
                            </h3>
                            <p className="mt-2 min-h-[60px] text-xs leading-[1.65] text-[#828d83]">
                              {tool.description}
                            </p>
                            <button
                              type="button"
                              onClick={() => openTool(tool.id)}
                              className="mt-4 inline-flex items-center gap-2 text-xs font-bold text-[#42664a] transition hover:gap-3"
                            >
                              {tool.action}
                              <ArrowRight className="h-3.5 w-3.5" />
                            </button>
                          </div>
                        </article>
                      );
                    })}
                  </div>
                ) : (
                  <div className="mt-4 rounded-2xl border border-dashed border-[#dfe5dc] bg-white/70 px-5 py-10 text-center">
                    <Search className="mx-auto h-5 w-5 text-[#a2aca1]" />
                    <p className="mt-2 text-sm font-semibold text-[#607064]">
                      No tools found
                    </p>
                    <p className="mt-1 text-xs text-[#909a90]">
                      Try a different search or category.
                    </p>
                  </div>
                )}
              </section>

              <section
                id="records"
                className="scroll-mt-24 rounded-[18px] border border-[#e9ece5] bg-white p-5 sm:p-6"
              >
                <div className="flex items-start justify-between gap-3">
                  <div>
                    <div className="flex items-center gap-2">
                      <Activity className="h-[17px] w-[17px] text-[#6b826e]" />
                      <h2 className="text-[17px] font-semibold tracking-[-0.025em] text-[#35443a]">
                        Your records
                      </h2>
                    </div>
                    <p className="mt-1 text-xs text-[#929b92]">
                      A simple overview of your saved information.
                    </p>
                  </div>
                  <span className="rounded-lg bg-[#f4f7f1] p-2 text-[#55745a]">
                    <CircleHelp className="h-4 w-4" />
                  </span>
                </div>

                <div className="mt-5 rounded-xl bg-[#f7f8f5] p-4">
                  <div className="flex items-center justify-between">
                    <span className="text-xs font-medium text-[#79857b]">
                      Lung-risk assessments
                    </span>
                    <span className="text-sm font-semibold text-[#47634b]">
                      {recordsLoading ? "—" : assessments.length}
                    </span>
                  </div>
                  <div className="mt-3 h-1.5 overflow-hidden rounded-full bg-[#e8ede5]">
                    <div
                      className="h-full rounded-full bg-[#88a383]"
                      style={{ width: assessments.length ? "100%" : "0%" }}
                    />
                  </div>
                  <p className="mt-2 text-[10px] leading-4 text-[#969f96]">
                    Only assessments you have submitted are shown here.
                  </p>
                </div>

                {recordsError ? (
                  <div className="mt-4 rounded-xl border border-[#f0ddcd] bg-[#fff9f3] p-3.5">
                    <p role="alert" className="text-xs leading-5 text-[#8c5e37]">
                      {recordsError}
                    </p>
                    <button
                      type="button"
                      onClick={() => {
                        setRecordsLoading(true);
                        setRecordsError("");
                        setRecordsRefresh((value) => value + 1);
                      }}
                      className="mt-2 text-xs font-bold text-[#765130] underline underline-offset-2"
                    >
                      Try again
                    </button>
                  </div>
                ) : recordsLoading ? (
                  <p role="status" className="mt-4 text-xs text-[#8d988f]">
                    Loading your records…
                  </p>
                ) : assessments.length ? (
                  <p className="mt-4 rounded-xl border border-[#e7ece4] px-3.5 py-3 text-xs leading-5 text-[#748075]">
                    You have {assessments.length} saved{" "}
                    {assessments.length === 1 ? "assessment" : "assessments"}.
                    Open the assessment tool to review your information.
                  </p>
                ) : (
                  <div className="mt-4 rounded-xl border border-dashed border-[#e1e7df] px-3.5 py-4 text-center">
                    <p className="text-xs font-semibold text-[#67756a]">
                      No saved assessments yet
                    </p>
                    <p className="mt-1 text-[11px] leading-5 text-[#98a198]">
                      Your records will appear here after you submit an
                      assessment.
                    </p>
                  </div>
                )}

                <div className="mt-5 border-t border-[#eff1ed] pt-4">
                  <div className="flex items-center gap-2 text-xs font-semibold text-[#59695d]">
                    <UserRound className="h-4 w-4 text-[#819481]" />
                    Account details
                  </div>
                  <p className="mt-2 truncate text-xs text-[#8c978d]">
                    {user.email ?? user.username ?? user.name}
                  </p>
                </div>

                <div className="mt-5 rounded-xl border border-[#e8eee6] bg-[#f9faf7] p-3.5">
                  <div className="flex items-center gap-2">
                    <Sparkles className="h-4 w-4 text-[#7c957b]" />
                    <p className="text-xs font-semibold text-[#526453]">
                      Need a little context?
                    </p>
                  </div>
                  <p className="mt-1.5 text-[11px] leading-5 text-[#8a958b]">
                    Ask a question about information in your saved assessments.
                  </p>
                  <button
                    type="button"
                    onClick={() => {
                      setActiveTool("records");
                    }}
                    className="mt-2 inline-flex items-center gap-1 text-[11px] font-bold text-[#41644a]"
                  >
                    Open record assistant <ArrowRight className="h-3 w-3" />
                  </button>
                </div>
              </section>
            </div>

            {activeTool === "records" && (
              <section id="record-assistant" className="mt-6 scroll-mt-24">
                <div className="mb-3 flex items-center justify-between gap-3">
                  <div>
                    <p className="text-[10px] font-bold uppercase tracking-[0.15em] text-[#89958a]">
                      Your records
                    </p>
                    <h2 className="mt-1 text-lg font-semibold text-[#35443a]">
                      Record assistant
                    </h2>
                  </div>
                  <button
                    type="button"
                    onClick={() => setActiveTool(null)}
                    className="rounded-lg p-2 text-[#849085] hover:bg-white"
                    aria-label="Close record assistant"
                  >
                    <X className="h-4 w-4" />
                  </button>
                </div>
                <PatientRecordAssistant />
              </section>
            )}

            <footer className="mt-8 flex flex-wrap items-center justify-between gap-2 border-t border-[#e8ece5] pt-4 text-[10px] leading-5 text-[#99a198]">
              <span>Karnix patient workspace</span>
              <span>
                Assessment information is research-only and is not a diagnosis
                or medical advice.
              </span>
            </footer>
          </div>
        </section>
      </div>
    </main>
  );
}

function OverviewCard({
  icon: Icon,
  label,
  value,
  note,
  color,
}: {
  icon: LucideIcon;
  label: string;
  value: string;
  note: string;
  color: "green" | "sand" | "blue";
}) {
  const colors = {
    green: "bg-[#edf4ec] text-[#547253]",
    sand: "bg-[#f8f0e6] text-[#a27849]",
    blue: "bg-[#edf1f2] text-[#5f7d82]",
  };

  return (
    <article className="flex min-w-0 items-center gap-3.5 rounded-[15px] border border-[#e9ece5] bg-white p-4">
      <span
        className={`flex h-10 w-10 shrink-0 items-center justify-center rounded-xl ${colors[color]}`}
      >
        <Icon className="h-[18px] w-[18px]" />
      </span>
      <span className="min-w-0">
        <span className="block text-[10px] font-semibold text-[#929b92]">
          {label}
        </span>
        <span className="mt-0.5 block truncate text-[15px] font-semibold tracking-[-0.02em] text-[#405044]">
          {value}
        </span>
        <span className="mt-0.5 block truncate text-[10px] text-[#a0a8a0]">
          {note}
        </span>
      </span>
    </article>
  );
}
