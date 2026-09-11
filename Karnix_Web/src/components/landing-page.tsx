"use client";

import { AnimatePresence, motion } from "motion/react";
import { useEffect, useMemo, useState } from "react";

import { useTheme } from "@/components/theme-provider";
import {
  blogPosts,
  faqs,
  features,
  heroBadge,
  logos,
  navLinks,
  pricingTiers,
  principles,
  siteConfig,
  stats,
  testimonials,
} from "@/config/site";

const highlightStyle =
  "bg-white/10 text-white ring-1 ring-white/10 backdrop-blur-sm";

function NavMenu() {
  return (
    <nav className="hidden items-center gap-7 text-sm text-slate-200/80 md:flex">
      {navLinks.map((link) => (
        <div key={link.label} className="group relative">
          {link.href ? (
            <a href={link.href} className="transition hover:text-white">
              {link.label}
            </a>
          ) : (
            <button
              type="button"
              className="flex items-center gap-1 transition hover:text-white"
            >
              {link.label}
              <span className="text-[10px]">▾</span>
            </button>
          )}
          {link.items && (
            <div className="invisible absolute left-0 top-full mt-4 w-64 rounded-2xl border border-white/10 bg-slate-950/90 p-3 opacity-0 shadow-2xl transition-all duration-200 group-hover:visible group-hover:opacity-100">
              {link.items.map((item) => (
                <a
                  key={item.label}
                  href="#"
                  className="block rounded-xl px-3 py-2 hover:bg-white/5"
                >
                  <div className="text-sm text-white">{item.label}</div>
                  <div className="mt-1 text-xs text-slate-400">{item.description}</div>
                </a>
              ))}
            </div>
          )}
        </div>
      ))}
    </nav>
  );
}

function LogoMark() {
  return (
    <div className="flex items-center gap-3">
      <div className="h-5 w-5 rounded-full bg-white shadow-[0_0_20px_rgba(255,255,255,0.7)]" />
      <span className="text-lg font-medium tracking-tight text-white">{siteConfig.name}</span>
    </div>
  );
}

function FeatureChart() {
  return (
    <div className="mt-5 flex h-24 items-end gap-2">
      {[32, 52, 41, 60, 46, 68, 55, 75, 64, 90, 82, 98].map((val, idx) => (
        <motion.span
          key={idx}
          initial={{ height: 0, opacity: 0.4 }}
          animate={{ height: `${val}%`, opacity: 1 }}
          transition={{ delay: idx * 0.05, duration: 0.5 }}
          className="w-full rounded-t-md bg-gradient-to-t from-blue-500 via-blue-400 to-cyan-300"
          style={{ height: `${val}%` }}
        />
      ))}
    </div>
  );
}

function FeatureCode() {
  const lines = [
    "--data '{",
    "  'account-balance'",
    "  'portfolio-value'",
    "  'transaction-history'",
    "  'pending-transfers'",
    "  'card-details'",
    "  'exchange-rates'",
    "}'",
  ];

  return (
    <div className="mt-5 space-y-1 font-mono text-sm text-blue-300">
      {lines.map((line, idx) => (
        <div key={idx} className={idx === 0 ? "text-blue-200" : "text-blue-300/90"}>
          {line}
        </div>
      ))}
    </div>
  );
}

function StatCounter({ value, prefix, suffix }: { value: number; prefix?: string; suffix?: string }) {
  const [current, setCurrent] = useState(0);

  useEffect(() => {
    let start: number | null = null;
    const duration = 1400;

    const tick = (time: number) => {
      if (!start) start = time;
      const progress = Math.min((time - start) / duration, 1);
      setCurrent(value * progress);
      if (progress < 1) requestAnimationFrame(tick);
    };

    const handle = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(handle);
  }, [value]);

  const displayedValue =
    value >= 100 ? Math.round(current) : Number(current.toFixed(1));

  return (
    <span>
      {prefix}
      {displayedValue}
      {suffix}
    </span>
  );
}

function Icon({ name }: { name: "card" | "chart" | "shield" | "eye" }) {
  const common = "h-8 w-8 stroke-current";
  switch (name) {
    case "card":
      return (
        <svg viewBox="0 0 24 24" fill="none" className={common}>
          <rect x="2.5" y="5" width="19" height="14" rx="3" strokeWidth="1.5" />
          <path d="M2.5 10h19" strokeWidth="1.5" />
          <path d="M7 15h3" strokeWidth="1.5" strokeLinecap="round" />
        </svg>
      );
    case "chart":
      return (
        <svg viewBox="0 0 24 24" fill="none" className={common}>
          <path d="M4 18h16" strokeWidth="1.5" strokeLinecap="round" />
          <path d="M7 15V9" strokeWidth="1.5" strokeLinecap="round" />
          <path d="M12 15V6" strokeWidth="1.5" strokeLinecap="round" />
          <path d="M17 15v-3" strokeWidth="1.5" strokeLinecap="round" />
        </svg>
      );
    case "shield":
      return (
        <svg viewBox="0 0 24 24" fill="none" className={common}>
          <path d="M12 3l7 3v5c0 4.2-2.7 7.9-7 10-4.3-2.1-7-5.8-7-10V6l7-3Z" strokeWidth="1.5" />
          <path d="M9.5 12.5l1.7 1.7 3.3-4.2" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      );
    default:
      return (
        <svg viewBox="0 0 24 24" fill="none" className={common}>
          <path d="M2 12s3.5-6 10-6 10 6 10 6-3.5 6-10 6S2 12 2 12Z" strokeWidth="1.5" />
          <circle cx="12" cy="12" r="3" strokeWidth="1.5" />
        </svg>
      );
  }
}

export default function LandingPage() {
  const { theme, toggleTheme } = useTheme();
  const [activeFaq, setActiveFaq] = useState<number | null>(0);
  const [activeTestimonial, setActiveTestimonial] = useState(0);

  const visibleTestimonials = useMemo(
    () => [...testimonials, ...testimonials].slice(0, 6),
    []
  );

  useEffect(() => {
    const id = window.setInterval(() => {
      setActiveTestimonial((prev) => (prev + 1) % testimonials.length);
    }, 3500);
    return () => window.clearInterval(id);
  }, []);

  return (
    <div className="min-h-screen bg-[radial-gradient(circle_at_top,_rgba(36,77,139,0.2),_transparent_36%),linear-gradient(180deg,#010914_0%,#020b17_20%,#020b17_100%)] text-slate-100 dark:text-slate-100">
      <header className="sticky top-0 z-50 border-b border-white/10 bg-[#020b17]/75 backdrop-blur-xl">
        <div className="mx-auto flex max-w-7xl items-center justify-between px-6 py-5">
          <LogoMark />
          <NavMenu />
          <div className="flex items-center gap-3">
            <button
              type="button"
              aria-label="Toggle color theme"
              onClick={toggleTheme}
              className="flex h-11 w-11 items-center justify-center rounded-full border border-white/10 bg-white/5 text-lg text-white transition hover:bg-white/10"
            >
              {theme === "dark" ? "☀" : "☾"}
            </button>
            <button className="hidden rounded-full border border-white/10 bg-white/5 px-5 py-2.5 text-sm text-white/90 transition hover:bg-white/10 md:inline-flex">
              Log in
            </button>
            <button className="rounded-full bg-white px-5 py-2.5 text-sm font-medium text-slate-900 transition hover:scale-[1.02]">
              Sign up
            </button>
          </div>
        </div>
      </header>

      <main>
        <section className="relative overflow-hidden">
          <div className="pointer-events-none absolute inset-0">
            <div className="absolute left-1/3 top-20 h-72 w-72 rounded-full bg-cyan-500/10 blur-3xl" />
            <div className="absolute right-1/3 top-28 h-80 w-80 rounded-full bg-pink-500/10 blur-3xl" />
          </div>

          <div className="mx-auto max-w-7xl px-6 pb-12 pt-16 md:pt-20">
            <div className="mt-8 flex justify-center">
              <div className="marquee-wrap overflow-hidden rounded-full border border-white/10 bg-white/5 px-4 py-2 text-sm text-slate-300 shadow-[0_0_40px_rgba(15,23,42,0.7)] backdrop-blur-xl">
                <div className="marquee-track flex w-max items-center gap-12 whitespace-nowrap">
                  {[...logos, ...logos].map((logo, idx) => (
                    <div key={`${logo}-${idx}`} className="flex items-center gap-3 opacity-80">
                      <span className="inline-block h-3 w-3 rounded-full border border-white/40 bg-white/80" />
                      <span className="text-lg font-medium tracking-tight text-white/80">{logo}</span>
                    </div>
                  ))}
                </div>
              </div>
            </div>

            <motion.div
              initial={{ opacity: 0, y: 18 }}
              animate={{ opacity: 1, y: 0 }}
              transition={{ duration: 0.6 }}
              className="mx-auto mt-14 max-w-5xl text-center"
            >
              <div className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-4 py-2 text-xs font-medium tracking-[0.04em] text-white/90">
                <span className="h-2 w-2 rounded-full bg-emerald-400" />
                {heroBadge}
              </div>
              <h1 className="mt-8 text-5xl font-medium leading-[0.95] tracking-[-0.06em] text-white sm:text-6xl lg:text-[7rem]">
                The new standard
                <span className="block font-serif italic text-white/95">for modern banking</span>
              </h1>
            </motion.div>

            <div className="mt-14 grid gap-6 lg:grid-cols-3">
              <motion.article
                initial={{ opacity: 0, y: 18 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ delay: 0.15, duration: 0.6 }}
                className="glass-panel rounded-[28px] p-6"
              >
                <div className="grid grid-cols-3 gap-2 text-xs uppercase tracking-[0.12em] text-slate-400">
                  <span>Speed</span>
                  <span>Fees</span>
                  <span className="text-right">Transfers</span>
                </div>
                <div className="mt-5 space-y-3 text-sm text-slate-200">
                  {[
                    ["finaro", "Instant", "$0"],
                    ["Bank", "3-5 days", "$25-50"],
                    ["Wire", "1-2 days", "$15-35"],
                  ].map(([name, speed, fee], idx) => (
                    <div
                      key={name}
                      className={`grid grid-cols-[1fr_auto_auto] items-center gap-2 rounded-xl border px-3 py-2 ${idx === 0 ? "border-blue-400/30 bg-blue-500/5" : "border-white/5 bg-white/[0.02]"}`}
                    >
                      <span className="font-medium text-white">{name}</span>
                      <span className="text-slate-300">{speed}</span>
                      <span className="text-slate-200">{fee}</span>
                    </div>
                  ))}
                </div>
                <p className="mt-6 text-lg text-slate-100">Send money globally. Instantly.</p>
                <p className="mt-2 text-sm leading-6 text-slate-400">
                  Transfer funds to 160+ countries with the real exchange rate.
                </p>
              </motion.article>

              <motion.article
                initial={{ opacity: 0, y: 18 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ delay: 0.25, duration: 0.6 }}
                className="glass-panel rounded-[28px] p-6"
              >
                <div className="flex items-center justify-between">
                  <span className="text-sm uppercase tracking-[0.18em] text-slate-400">Current APY</span>
                  <span className="rounded-full bg-blue-500/10 px-2 py-1 text-xs text-blue-300">4.50%</span>
                </div>
                <FeatureChart />
                <div className="mt-2 text-right text-xs text-slate-400">vs avg savings 0.5%</div>
                <p className="mt-6 text-lg text-slate-100">Earn more on your savings.</p>
                <p className="mt-2 text-sm leading-6 text-slate-400">
                  Get up to 4.5% APY on your balance and keep it instantly accessible.
                </p>
              </motion.article>

              <motion.article
                initial={{ opacity: 0, y: 18 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ delay: 0.35, duration: 0.6 }}
                className="glass-panel rounded-[28px] p-6"
              >
                <div className="rounded-2xl border border-white/10 bg-slate-950/60 p-4 text-sm shadow-inner shadow-white/5">
                  <FeatureCode />
                </div>
                <p className="mt-6 text-lg text-slate-100">Build with our developer API.</p>
                <p className="mt-2 text-sm leading-6 text-slate-400">
                  Access real-time payment data and automate your finances with our REST API.
                </p>
              </motion.article>
            </div>
          </div>
        </section>

        <section className="relative mx-auto max-w-7xl px-6 pb-10 pt-10 md:pt-20">
          <div className="grid gap-6 md:grid-cols-2 xl:grid-cols-4">
            {features.map((feature, idx) => (
              <motion.article
                key={feature.title}
                initial={{ opacity: 0, y: 18 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ delay: idx * 0.12, duration: 0.55 }}
                className="glass-panel rounded-[28px] p-7"
              >
                <div className="mb-4 flex h-12 w-12 items-center justify-center rounded-2xl bg-white/5 text-lg text-white/90">
                  {idx === 0 ? "↗" : idx === 1 ? "◎" : "⌘"}
                </div>
                <h3 className="text-2xl font-medium tracking-[-0.04em] text-white">
                  {feature.title}
                </h3>
                <p className="mt-4 text-sm leading-6 text-slate-400">{feature.description}</p>
                <a href={feature.href} className="mt-6 inline-flex items-center gap-2 text-sm font-medium text-white hover:text-slate-300">
                  Learn more <span aria-hidden>→</span>
                </a>
              </motion.article>
            ))}
          </div>
        </section>

        <section className="relative mx-auto max-w-7xl px-6 pb-10 pt-16">
          <div className="grid gap-10 lg:grid-cols-[1.2fr_0.8fr] lg:items-center">
            <div>
              <p className="text-5xl font-medium tracking-[-0.06em] text-white sm:text-6xl">security at the core</p>
              <p className="mt-5 max-w-xl text-lg leading-8 text-slate-400">
                From instant payments to global transfers, every feature is designed to give you complete control over your finances.
              </p>
              <button className="mt-8 inline-flex items-center gap-3 rounded-full border border-white/15 bg-white px-6 py-3 text-base font-medium text-slate-900 transition hover:scale-[1.02]">
                Open an account <span aria-hidden>→</span>
              </button>
            </div>

            <div className="grid gap-4 sm:grid-cols-2">
              {principles.slice(0, 2).map((principle) => (
                <div key={principle.title} className="glass-panel flex min-h-[180px] flex-col items-center justify-center rounded-[28px] p-8 text-center">
                  <div className="mb-4 flex h-12 w-12 items-center justify-center text-white/90">
                    <Icon name={principle.icon as "card" | "chart" | "shield" | "eye"} />
                  </div>
                  <h3 className="text-xl font-medium text-white">{principle.title}</h3>
                </div>
              ))}
            </div>
          </div>

          <div className="mt-16 grid gap-6 sm:grid-cols-2 lg:grid-cols-4">
            {stats.map((stat) => (
              <div key={stat.label} className="border-b border-white/10 pb-4 text-center lg:text-left">
                <div className="text-5xl font-medium tracking-[-0.06em] text-white sm:text-6xl">
                  {stat.prefix ? stat.prefix : ""}
                  <StatCounter value={stat.value} prefix={stat.prefix} suffix={stat.suffix} />
                </div>
                <p className="mt-3 text-base text-slate-400">{stat.label}</p>
              </div>
            ))}
          </div>
        </section>

        <section className="mx-auto max-w-7xl px-6 py-20">
          <div className="mb-10 text-center">
            <h2 className="text-5xl font-medium tracking-[-0.06em] text-white sm:text-7xl">
              Trusted by Finance Leaders
            </h2>
          </div>

          <div className="grid gap-6 lg:grid-cols-3">
            {visibleTestimonials.slice(0, 3).map((person, idx) => (
              <motion.article
                key={`${person.name}-${idx}`}
                initial={{ opacity: 0, y: 20 }}
                animate={{ opacity: 1, y: 0 }}
                transition={{ delay: idx * 0.08, duration: 0.45 }}
                className="overflow-hidden rounded-[28px] border border-white/10 bg-gradient-to-br from-white/10 to-transparent"
              >
                <div className="h-52 bg-[radial-gradient(circle_at_top,_rgba(255,255,255,0.2),_transparent_40%),linear-gradient(135deg,#d9d1ff,#7cc7ff_40%,#1d2745_100%)]" />
                <div className="p-6">
                  <div className="mb-4 flex items-center gap-3">
                    <div className="h-10 w-10 rounded-full bg-white/10" />
                    <div>
                      <p className="font-medium text-white">{person.name}</p>
                      <p className="text-xs text-slate-400">{person.role}</p>
                    </div>
                  </div>
                  <p className="text-sm leading-7 text-slate-300">“{person.quote}”</p>
                </div>
              </motion.article>
            ))}
          </div>
        </section>

        <section id="pricing" className="mx-auto max-w-7xl px-6 py-6">
          <div className="mb-10 text-center">
            <p className="text-5xl font-medium tracking-[-0.06em] text-white sm:text-7xl">Simple pricing</p>
          </div>

          <div className="grid gap-6 lg:grid-cols-3">
            {pricingTiers.map((tier) => (
              <div
                key={tier.name}
                className={`rounded-[30px] border p-7 ${tier.highlighted ? "border-blue-400/40 bg-blue-500/5 shadow-[0_0_50px_rgba(59,130,246,0.2)]" : "border-white/10 bg-white/[0.02]"}`}
              >
                <div className="flex items-center justify-between">
                  <p className="text-xl font-medium text-white">{tier.name}</p>
                  {tier.highlighted && (
                    <span className="rounded-full bg-blue-500/15 px-2 py-1 text-[10px] uppercase tracking-[0.18em] text-blue-200">
                      Most popular
                    </span>
                  )}
                </div>
                <div className="mt-6 flex items-end gap-2">
                  <span className="text-5xl font-medium tracking-[-0.06em] text-white">${tier.price}</span>
                  <span className="pb-2 text-sm text-slate-400">{tier.period}</span>
                </div>
                <p className="mt-4 text-sm leading-6 text-slate-400">{tier.description}</p>
                <ul className="mt-6 space-y-3">
                  {tier.features.map((feature) => (
                    <li key={feature} className="flex items-center gap-3 text-sm text-slate-200">
                      <span className="inline-flex h-5 w-5 items-center justify-center rounded-full bg-emerald-400/15 text-xs text-emerald-300">
                        ✓
                      </span>
                      {feature}
                    </li>
                  ))}
                </ul>
                <button
                  className={`mt-8 w-full rounded-full px-4 py-3 text-sm font-medium transition ${tier.highlighted ? "bg-white text-slate-900 hover:bg-slate-200" : "border border-white/10 bg-transparent text-white hover:bg-white/5"}`}
                >
                  {tier.cta}
                </button>
              </div>
            ))}
          </div>
        </section>

        <section className="mx-auto max-w-5xl px-6 py-20">
          <div className="space-y-4">
            {faqs.map((faq, idx) => (
              <div key={faq.question} className="overflow-hidden rounded-2xl border border-white/10 bg-white/[0.02]">
                <button
                  type="button"
                  onClick={() => setActiveFaq(activeFaq === idx ? null : idx)}
                  className="flex w-full items-center justify-between gap-4 px-5 py-4 text-left text-lg font-medium text-white"
                >
                  {faq.question}
                  <span className="text-2xl text-slate-400">{activeFaq === idx ? "−" : "+"}</span>
                </button>
                <AnimatePresence initial={false}>
                  {activeFaq === idx && (
                    <motion.div
                      initial={{ height: 0, opacity: 0 }}
                      animate={{ height: "auto", opacity: 1 }}
                      exit={{ height: 0, opacity: 0 }}
                      transition={{ duration: 0.2 }}
                    >
                      <div className="border-t border-white/10 px-5 py-4 text-sm leading-7 text-slate-400">
                        {faq.answer}
                      </div>
                    </motion.div>
                  )}
                </AnimatePresence>
              </div>
            ))}
          </div>
        </section>

        <section className="mx-auto max-w-7xl px-6 py-10">
          <div className="mb-8 text-center">
            <p className="text-4xl font-medium tracking-[-0.06em] text-white sm:text-6xl">Insights & updates</p>
          </div>
          <div className="grid gap-6 lg:grid-cols-3">
            {blogPosts.map((post, idx) => (
              <article key={post.title} className="overflow-hidden rounded-[28px] border border-white/10 bg-white/[0.02]">
                <div className={`h-52 ${idx === 0 ? "bg-[linear-gradient(135deg,#d8d5ff,#d2f0ff_38%,#1d2a43)]" : idx === 1 ? "bg-[linear-gradient(135deg,#f5d5ff,#d6e3ff_45%,#1c1948)]" : "bg-[linear-gradient(135deg,#e7d3c2,#cde4ff_36%,#0d1a30)]"}`} />
                <div className="p-6">
                  <div className="mb-4 flex items-center justify-between text-xs uppercase tracking-[0.18em] text-slate-400">
                    <span>{post.category}</span>
                    <span>{post.date}</span>
                  </div>
                  <h3 className="text-2xl font-medium leading-tight text-white">{post.title}</h3>
                  <a href="#" className="mt-6 inline-flex items-center gap-2 text-sm font-medium text-white hover:text-slate-300">
                    Read article <span aria-hidden>→</span>
                  </a>
                </div>
              </article>
            ))}
          </div>
        </section>

        <section className="mx-auto max-w-6xl px-6 pb-20 pt-14">
          <div className="rounded-[32px] border border-white/10 bg-[radial-gradient(circle_at_top,_rgba(99,102,241,0.22),_transparent_40%),linear-gradient(180deg,rgba(15,23,42,0.96),rgba(15,23,42,0.92))] p-10 text-center shadow-[0_0_80px_rgba(49,46,129,0.24)]">
            <p className="text-5xl font-medium tracking-[-0.06em] text-white sm:text-7xl">Spend, save, &amp; invest with one powerful app</p>
            <p className="mx-auto mt-5 max-w-2xl text-lg leading-8 text-slate-300">
              Join 45 million people managing their money better with instant transfers, smart budgeting, and zero foreign exchange fees.
            </p>
            <div className="mt-8 flex flex-col justify-center gap-4 sm:flex-row">
              <input
                aria-label="Email"
                placeholder="Enter your email"
                className="w-full max-w-md rounded-full border border-white/15 bg-white/5 px-5 py-3 text-white placeholder:text-slate-400 focus:border-white/30 focus:outline-none sm:w-[340px]"
              />
              <button className="rounded-full bg-white px-6 py-3 text-base font-medium text-slate-900 transition hover:scale-[1.02]">
                Get Started
              </button>
            </div>
            <p className="mt-5 text-sm text-slate-400">Industry-leading security. No hidden fees.</p>
          </div>
        </section>
      </main>
    </div>
  );
}
